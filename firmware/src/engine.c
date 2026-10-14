/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The sound engine, fixed point. A voice plays four corners at once; the green orb's place weights them.
 * A corner: two overtone generators (band-limited wavetables, one per pattern and octave band), their blend
 * (left of centre added, right of centre modulated: AM, PM, PM+AM, PD+AM, reversed PD+AM), then its filter.
 * Per control tick (CTL samples): envelopes, pitch, the orbit, the weights and the filter's coefficients; per
 * sample: the generators, the blend, the filter, the weighted sum. */
static patch_t pt;                         /* the sound */
static voice_t vs[NVOICE];
static volatile uint8_t vmax = NVOICE;     /* voices allowed now (the CPU guard, mix.c) */
static volatile int32_t joy_x, joy_y;      /* wheels, Q15: bend -32767..32767, mod 0..32767 */
static volatile uint8_t focus;             /* 0 off, 1..4 a corner, 5 the centre: the orbit stands there */
static volatile uint8_t solo;              /* bit c: corner c alone (its mute pad held) */
static volatile uint8_t latch;             /* notes stay after the keys let go */
static volatile uint8_t freeze;            /* the orbit stands still where it is (SEQ) */
static struct {
    int32_t peak, mpeak;                   /* |out| max since the last read (console, the screen's meter), Q15 */
    uint32_t overs, ticks;
    uint32_t oph, sph;                     /* the free-running orbit (no voice sounding, or no retrigger) */
    int32_t ox, oy, gx, gy;                /* its orbs */
    int32_t w[NC];                         /* the weights the screen and pads show (the newest voice's) */
    uint8_t newest;                        /* the voice played last */
    uint8_t latched;                       /* latch as last seen */
    uint32_t age, arp_ph, rnd;
    uint8_t arp_step, arp_run;
    int8_t arp_note;
} syn = {.rnd = 0x1234567u, .arp_note = -1};

static inline uint32_t xorshift(uint32_t *x)
{
    uint32_t v = *x ? *x : 0x9E3779B9u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return *x = v;
}

/* cents above MIDI 0 (8.18 Hz), -3600 .. 13199 -> phase increment (2^32 = FS) */
static uint32_t cents_inc(int32_t c)
{
    uint32_t o, r;
    c = clamp(c, -3600, 13199) + 3600;
    o = (uint32_t)c / 1200u;
    r = (uint32_t)c - o * 1200u;
    return INC_TOP[r] >> (13u - o);
}
/* a phase increment -> its place on the filter's cutoff curve (SVF_G index; log2 by the top bit + 8 linear bits) */
static int32_t inc_idx(uint32_t inc)
{
    int32_t o = 31;
    if (!inc)
        return 0;
    while (!(inc >> o))
        o--;
    return (((o << 8) + (int32_t)(((inc << (31 - o)) >> 23) & 255u) - 5498) * 113) >> 8;   /* 5498: 30 Hz */
}

/* ------------------------------------------------------------- generators --- */
/* the wavetables live in RAM (copied at start): every voice reads them every sample */
static int16_t ovt[sizeof OVT_SRC / sizeof OVT_SRC[0]] __attribute__((section(".pool")));
static const uint8_t RATIO_MUL[5] = {60, 30, 20, 15, 12};   /* the master phase runs at 1/60: x1, 1/2 .. 1/5 */

static inline int32_t tlu(const int16_t *t, uint32_t sh, uint32_t ph)   /* table lookup, interpolated */
{
    uint32_t i = ph >> sh;
    int32_t a = t[i], f = (int32_t)((ph << (32u - sh)) >> 17);
    return a + (((t[i + 1u] - a) * f) >> 15);
}
static const int16_t *table_for(uint32_t pat, uint32_t inc, uint32_t *sh)   /* the band for this pitch */
{
    uint32_t b = OVT_B0;
    while (b < OVT_B0 + OVT_NB - 1u && inc > (1u << (20u + b)))
        b++;
    b -= OVT_B0;
    *sh = OVT_SH[b];
    return ovt + OVT_OFF[pat * OVT_NB + b];
}

/* ------------------------------------------------------------- the filter --- */
typedef struct { int32_t a1, a2, a3, k; } svf_t;
static void svf_coef(svf_t *f, int32_t idx, int32_t k)  /* idx 0..1023 on SVF_G, k (Q16) = 2 - 2 res */
{
    int32_t g = (int32_t)SVF_G[clamp(idx, 0, 1023)], den = 65536 + (int32_t)(((int64_t)g * (g + k)) >> 16);
    f->k = k;
    f->a1 = (int32_t)(0xFFFFFFFFu / (uint32_t)den);
    f->a2 = (int32_t)(((int64_t)g * f->a1) >> 16);
    f->a3 = (int32_t)(((int64_t)g * f->a2) >> 16);
}
#define SVF_LIM 200000                     /* the states' bound: a screaming resonance cannot run away */
/* one TPT state-variable filter (A. Simper's form) over CTL samples: mode 0 low, 1 band (unity peak), 2 high */
static void svf_run(const svf_t *f, int32_t *st, const int32_t *x, int32_t *o, uint32_t mode)
{
    int32_t s1 = st[0], s2 = st[1], a1 = f->a1, a2 = f->a2, a3 = f->a3, k = f->k, i;
    for (i = 0; i < CTL; i++) {
        int32_t in = x[i], v3 = in - s2, v1, v2;
        v1 = (int32_t)(((int64_t)a1 * s1 + (int64_t)a2 * v3) >> 16);
        v2 = s2 + (int32_t)(((int64_t)a2 * s1 + (int64_t)a3 * v3) >> 16);
        s1 = clamp(2 * v1 - s1, -SVF_LIM, SVF_LIM);
        s2 = clamp(2 * v2 - s2, -SVF_LIM, SVF_LIM);
        o[i] = mode == 0u ? v2 : mode == 1u ? (int32_t)(((int64_t)v1 * k) >> 16)
                                            : in - (int32_t)(((int64_t)v1 * k) >> 16) - v2;
    }
    st[0] = s1, st[1] = s2;
}

/* a voice's filters this tick (the cutoff and resonance where the orbit is, shared by its corners) */
typedef struct { svf_t m, b, f1, f2; int32_t vox; } vflt_t;
static vflt_t vf[NVOICE];

/* noise (pattern N): pink noise through a resonant high-pass at the generator's pitch */
static void noise_fill(voice_t *v, cstate_t *cs, uint32_t k, uint32_t inc, int32_t *o)
{
    int32_t *pk = cs->pk[k], *h = cs->nh[k], b0 = pk[0], b1 = pk[1], b2 = pk[2], i;
    svf_t f;
    svf_coef(&f, inc_idx(inc), 40000);                 /* k 0.6: a resonant edge at the pitch */
    for (i = 0; i < CTL; i++) {
        int32_t w = (int32_t)xorshift(&v->rnd) >> 17;  /* white, +-16384; then Kellet's three poles */
        b0 = ((b0 * 32691) >> 15) + ((w * 3245) >> 15);
        b1 = ((b1 * 31556) >> 15) + ((w * 9716) >> 15);
        b2 = ((b2 * 18678) >> 15) + ((w * 34494) >> 15);
        o[i] = (b0 + b1 + b2 + ((w * 6055) >> 15)) >> 1;
    }
    svf_run(&f, h, o, o, 2);
    pk[0] = b0, pk[1] = b1, pk[2] = b2;
}

/* ------------------------------------------------------------- a corner --- */
/* corner c of voice v, weighted (ramped from w0 to w1), into its filter type's buffer tb[type] (the first one
 * there sets it: bit type of *used). The voice's corners share the cutoff and resonance and the filter is linear,
 * so corners of one type share one filter (voice_filters). */
static void corner_render(voice_t *v, uint32_t c, int32_t (*tb)[CTL], uint32_t *used, int32_t w0, int32_t w1)
{
    const int16_t *cp = pt.c[c];
    cstate_t *cs = &v->cs[c];
    uint32_t inc = v->inc, ph = v->ph, i, sh1 = 0, sh2 = 0, ft = (uint32_t)cp[CP_FTYPE] % FT_N;
    uint32_t m1 = RATIO_MUL[cp[CP_T1] % 5], m2 = RATIO_MUL[cp[CP_T2] % 5], p1 = (uint32_t)cp[CP_G1] % PAT_COUNT;
    uint32_t p2 = (uint32_t)cp[CP_G2] % PAT_COUNT, mode = (uint32_t)cp[CP_BMODE] % BM_N;
    int32_t b = cp[CP_BLEND], a = b < 0 ? -b * 512 : b * 520, y[CTL], g2[CTL], dw, *o = tb[ft];
    const int16_t *t1 = 0, *t2 = 0;
    if (a > 32767)
        a = 32767;
    if (p1 != PAT_N)
        t1 = table_for(p1, inc * m1, &sh1);
    if (p2 != PAT_N)
        t2 = table_for(p2, inc * m2, &sh2);
    if (b != 0) {                                      /* generator 2 (PD: its phase squeezed) */
        if (p2 == PAT_N)
            noise_fill(v, cs, 1, inc * m2, g2);
        else if (b > 0 && (mode == BM_PDAM || mode == BM_RPDAM)) {
            uint32_t kn = mode == BM_PDAM ? 0x80000000u - (uint32_t)a * 58982u : 0x80000000u + (uint32_t)a * 58982u;
            uint32_t s1 = 0x80000000u / (kn >> 16), s2 = 0x80000000u / ((0u - kn) >> 16);
            for (i = 0; i < CTL; i++) {                /* 0 .. knee -> 0 .. 1/2, knee .. 1 -> 1/2 .. 1 */
                uint32_t q = (ph + i * inc) * m2;
                q = q < kn ? (q >> 16) * s1 : 0x80000000u + ((q - kn) >> 16) * s2;
                g2[i] = tlu(t2, sh2, q);
            }
        } else
            for (i = 0; i < CTL; i++)
                g2[i] = tlu(t2, sh2, (ph + i * inc) * m2);
    }
    if (p1 == PAT_N) {                                 /* noise as generator 1: no phase to modulate */
        noise_fill(v, cs, 0, inc * m1, y);
        if (b < 0)
            for (i = 0; i < CTL; i++)
                y[i] += (g2[i] * a) >> 15;
        else if (b > 0)
            for (i = 0; i < CTL; i++)
                y[i] += (a * (((y[i] * g2[i]) >> 13) - y[i])) >> 15;
    } else if (b == 0) {                               /* the centre: generator 1 alone */
        for (i = 0; i < CTL; i++)
            y[i] = tlu(t1, sh1, (ph + i * inc) * m1);
    } else if (b < 0) {                                /* added: G2's overtones join (+) or cancel (-) G1's */
        int32_t sub = p2 != PAT_N;                     /* (its fundamental out: the note has one) */
        for (i = 0; i < CTL; i++) {
            int32_t o2 = g2[i] - (sub ? (sinq((ph + i * inc) * m2) * GEN_AMP) >> 15 : 0);
            y[i] = tlu(t1, sh1, (ph + i * inc) * m1) + ((o2 * a) >> 15);
        }
    } else if (mode == BM_PM || mode == BM_PMAM) {     /* G2 moves G1's phase (up to 3/4 of a cycle) */
        uint32_t kpm = (uint32_t)(a >> 3) * 78u;
        for (i = 0; i < CTL; i++) {
            int32_t s = tlu(t1, sh1, (ph + i * inc) * m1 + (uint32_t)g2[i] * kpm);
            y[i] = mode == BM_PM ? s : s + ((a * (((s * g2[i]) >> 13) - s)) >> 15);
        }
    } else                                             /* AM, PD+AM: G1 times G2, faded in */
        for (i = 0; i < CTL; i++) {
            int32_t s = tlu(t1, sh1, (ph + i * inc) * m1);
            y[i] = s + ((a * (((s * g2[i]) >> 13) - s)) >> 15);
        }
    w0 >>= 2, w1 >>= 2;                                /* (Q13) */
    dw = (w1 - w0) / CTL;
    if ((*used >> ft) & 1u)
        for (i = 0; i < CTL; i++, w0 += dw)
            o[i] += (y[i] * w0) >> 13;
    else
        for (i = 0; i < CTL; i++, w0 += dw)
            o[i] = (y[i] * w0) >> 13;
    *used |= 1u << ft;
}

/* each filter type in use over its corners' sum, into x (+=) */
static void voice_filters(voice_t *v, int32_t (*tb)[CTL], uint32_t used, int32_t *x)
{
    const vflt_t *f = &vf[v - vs];
    uint32_t t, i;
    for (t = 0; t < FT_N; t++) {
        int32_t *y = tb[t], *st = v->fs[t];
        if (!((used >> t) & 1u)) {
            st[0] = st[1] = st[2] = st[3] = 0;        /* (unused: from rest when it comes back) */
            continue;
        }
        switch (t) {
        case FT_LP12:
            svf_run(&f->m, st, y, y, 0);
            break;
        case FT_LP24:
            svf_run(&f->m, st, y, y, 0);
            svf_run(&f->b, st + 2, y, y, 0);
            break;
        case FT_BP:
            svf_run(&f->m, st, y, y, 1);
            break;
        case FT_HP:
            svf_run(&f->m, st, y, y, 2);
            break;
        case FT_VOX: {                                 /* two formants; RES: how much of them */
            int32_t f1[CTL], f2[CTL];
            svf_run(&f->f1, st, y, f1, 1);
            svf_run(&f->f2, st + 2, y, f2, 1);
            for (i = 0; i < CTL; i++)
                y[i] += (f->vox * ((f1[i] + f2[i] + (f2[i] >> 1)) * 2 - y[i])) >> 15;
            break;
        }
        default:
            break;
        }
        for (i = 0; i < CTL; i++)
            x[i] += y[i];
    }
}

/* ------------------------------------------------------------- envelopes --- */
/* attack linear (the amplitude: its square), decay and release exponential */
static void env_run(uint8_t *stage, int32_t *e, uint32_t gate, int32_t a, int32_t d, int32_t s, int32_t r)
{
    int32_t top = 1 << 24, sus = s * 132104;
    if (!gate && *stage && *stage != 3u)
        *stage = 3;
    if (*stage == 1u) {
        *e += (int32_t)ENV_STEP[a & 127];
        if (*e >= top)
            *e = top, *stage = 2;
    } else if (*stage == 2u)
        *e = sus + (int32_t)((((uint32_t)(*e - sus)) >> 8) * ENV_FALL[d & 127] >> 8);
    else if (*stage == 3u) {
        *e = (int32_t)(((uint32_t)*e >> 8) * ENV_FALL[r & 127] >> 8);
        if (*e < 1 << 10)
            *e = 0, *stage = 0;
    }
}
static inline int32_t env_amp(int32_t e) { int32_t x = e >> 9; return (x * x) >> 15; }   /* Q15 */

/* ------------------------------------------------------------- notes --- */
enum { EV_ON = 1, EV_OFF, EV_ALLOFF, EV_AUD_ON, EV_AUD_OFF };   /* AUD: a planet heard alone (no arp, no latch) */
#define EVQ 64u
static volatile uint32_t ev_q[EVQ], ev_w, ev_r;
static void ev_push(uint32_t type, uint32_t note, uint32_t vel)   /* (the UI, main loop) */
{
    if (ev_w - ev_r >= EVQ)
        return;
    ev_q[ev_w % EVQ] = type | note << 8 | vel << 16;
    RING_PUBLISH();
    ev_w++;
}

typedef struct { int8_t n[16], vel[16]; uint8_t cnt; } nlist_t;   /* notes, oldest first */
static nlist_t down, lat;                  /* keys held now; the latched chord */
static void nl_del(nlist_t *l, uint32_t note)
{
    uint32_t i, j;
    for (i = j = 0; i < l->cnt; i++)
        if ((uint32_t)l->n[i] != note)
            l->n[j] = l->n[i], l->vel[j] = l->vel[i], j++;
    l->cnt = (uint8_t)j;
}
static void nl_add(nlist_t *l, uint32_t note, uint32_t vel)
{
    uint32_t i;
    nl_del(l, note);
    if (l->cnt == 16u) {
        for (i = 1; i < 16u; i++)
            l->n[i - 1u] = l->n[i], l->vel[i - 1u] = l->vel[i];
        l->cnt = 15;
    }
    l->n[l->cnt] = (int8_t)note, l->vel[l->cnt] = (int8_t)vel;
    l->cnt++;
}

/* a voice takes a note: k of n in a stack / pair (detune, pan); legato: only the pitch moves */
static void voice_start(voice_t *v, uint32_t note, uint32_t vel, uint32_t k, uint32_t n, int legato)
{
    const int16_t *g = pt.g;
    int32_t spread = 0, pan = 16384;
    if (g[G_POLY] == PM_MONO && n > 1u) {                  /* the stack: detuned around the note, spread out */
        int32_t pos = (int32_t)(k * 65536u / (n - 1u)) - 32768;   /* -32768 .. 32768 */
        spread = (pos >> 4) * g[G_DETUNE] * 50 / 127 / 128;   /* cents x 16: +-50 cents at most */
        pan = 16384 + pos / 2 * g[G_STEREO] / 127;
    } else if (g[G_POLY] == PM_DUAL) {                     /* the follower: an interval away, the other side */
        int32_t bias = (g[G_STEREO] - 64) * 200;
        spread = k ? (g[G_DETUNE] - 64) * 300 + 112 : 0;   /* +-1 octave (x 16), and a few cents */
        pan = 16384 + (k ? -bias : bias);
    } else if (g[G_STEREO]) {                              /* POLY: scattered */
        pan = 16384 + ((int32_t)(xorshift(&syn.rnd) >> 17) - 16384) * g[G_STEREO] / 127;
    }
    v->follower = (uint8_t)(g[G_POLY] == PM_DUAL && k);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->dcents = spread;
    v->pan = clamp(pan, 0, 32767);
    v->target = (int32_t)note * 1600;
    v->legato = (uint8_t)(legato && v->stage);
    if (!v->legato) {
        if (!v->stage) {                                   /* silent: a clean start */
            uint32_t c;
            v->ph = 0;
            v->amp = 0;
            for (c = 0; c < NC; c++)
                v->w[c] = 0;
            v->gl = v->gr = 0;
            v->cut = -1;
        }
        if (!v->rnd)
            v->rnd = 0x2545F491u * (uint32_t)(v - vs + 1);
        v->pitch = v->target;
        v->stage = 1;
        v->ostage = 1;
        v->oprog = 0;
        if (g[G_RETRIG]) {
            v->oph = 0;
            v->sph = v->follower ? 0x80000000u : 0u;
        } else {
            v->oph = syn.oph;
            v->sph = syn.sph + (v->follower ? 0x80000000u : 0u);
        }
    }
    v->on = 1;
    v->age = ++syn.age;
    syn.newest = (uint8_t)(v - vs);
}

static uint32_t stack_n(void) { return (uint32_t)clamp(pt.g[G_STACK], 1, (int32_t)vmax); }
static void note_on(uint32_t note, uint32_t vel)
{
    uint32_t i, mode = (uint32_t)pt.g[G_POLY] % PM_N, lim = vmax;
    if (mode == PM_MONO) {                                 /* one note, the stack plays it; legato glides */
        uint32_t n = stack_n(), any = 0;
        for (i = 0; i < n; i++)
            any |= vs[i].on;
        for (i = 0; i < n; i++)
            voice_start(&vs[i], note, vel, i, n, (int)any);
        for (; i < NVOICE; i++)
            vs[i].on = 0;
        return;
    }
    {   /* POLY / DUAL: a free voice (pair), else the oldest (released ones first) */
        uint32_t step = mode == PM_DUAL ? 2u : 1u, best = 0, bage = 0xFFFFFFFFu, b;
        int found = 0;
        for (i = 0; i + step <= lim; i += step) {
            uint32_t age = vs[i].age - (vs[i].on ? 0u : 0x40000000u);
            if (!vs[i].stage && (step == 1u || !vs[i + 1u].stage)) {
                best = i, found = 1;
                break;
            }
            if (age < bage)
                best = i, bage = age;
        }
        (void)found;
        for (b = 0; b < step; b++)
            voice_start(&vs[best + b], note, vel, b, step, 0);
    }
}
static void note_off(uint32_t note)
{
    uint32_t i;
    if (pt.g[G_POLY] == PM_MONO) {
        const nlist_t *l = latch ? &lat : &down;
        if (l->cnt && (uint32_t)l->n[l->cnt - 1u] != note) {   /* back to the last key still held */
            uint32_t n = stack_n();
            for (i = 0; i < n; i++)
                voice_start(&vs[i], (uint32_t)l->n[l->cnt - 1u], vs[i].vel, i, n, 1);
            return;
        }
        for (i = 0; i < NVOICE; i++)
            vs[i].on = 0;
        return;
    }
    for (i = 0; i < NVOICE; i++)
        if (vs[i].on && vs[i].note == note)
            vs[i].on = 0;
}
static void all_off(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        vs[i].on = 0;
}

/* the arpeggiator: sixteenths at the tempo over the notes (held or latched), two octaves, the gate half a step */
static void arp_tick(uint32_t mode)
{
    const nlist_t *l = latch ? &lat : &down;
    uint32_t inc = (uint32_t)pt.g[G_TEMPO] * SIXTEENTH_K, old = syn.arp_ph;
    if (!mode || !l->cnt) {
        if (syn.arp_note >= 0)
            note_off((uint32_t)syn.arp_note);
        syn.arp_note = -1;
        syn.arp_run = 0;
        return;
    }
    if (!syn.arp_run)                                      /* the first step at once */
        syn.arp_run = 1, syn.arp_step = 0, old = syn.arp_ph = 0xFFFFFFFFu;
    syn.arp_ph += inc;
    if (syn.arp_ph < old) {
        int8_t s[16];
        uint32_t n = l->cnt, tot = n * 2u, x, y, k;
        for (x = 0; x < n; x++)
            s[x] = l->n[x];
        for (x = 1; x < n; x++)                            /* low to high */
            for (y = x; y > 0u && s[y - 1u] > s[y]; y--) {
                int8_t t = s[y];
                s[y] = s[y - 1u], s[y - 1u] = t;
            }
        k = syn.arp_step % tot;
        if (mode == ARP_DOWN)
            k = tot - 1u - k;
        else if (mode == ARP_UPDN && tot > 1u) {
            k = syn.arp_step % (2u * tot - 2u);
            if (k >= tot)
                k = 2u * tot - 2u - k;
        } else if (mode == ARP_RND)
            k = xorshift(&syn.rnd) % tot;
        syn.arp_step++;
        if (syn.arp_note >= 0)
            note_off((uint32_t)syn.arp_note);
        syn.arp_note = (int8_t)clamp(s[k % n] + 12 * (int32_t)(k / n), 0, 127);
        note_on((uint32_t)syn.arp_note, (uint32_t)l->vel[l->cnt - 1u]);
    } else if (syn.arp_ph > 0x80000000u && syn.arp_note >= 0) {
        note_off((uint32_t)syn.arp_note);
        syn.arp_note = -1;
    }
}

/* the keys (events from the UI) with latch: a new chord, once all keys are up, replaces the latched one */
static void keys_tick(void)
{
    uint32_t arp = (uint32_t)pt.g[G_ARP] % ARP_N, i;
    if (syn.latched && !latch) {                           /* latch let go: what no key holds ends */
        for (i = 0; i < lat.cnt; i++) {
            uint32_t j, n = (uint32_t)lat.n[i], k = 0;
            for (j = 0; j < down.cnt; j++)
                k |= (uint32_t)down.n[j] == n;
            if (!k && !arp)
                note_off(n);
        }
        lat.cnt = 0;
    }
    if (!syn.latched && latch) {                           /* latch on: what is held now is the chord */
        lat = down;
    }
    syn.latched = latch;
    while (ev_r != ev_w) {
        uint32_t e = ev_q[ev_r % EVQ], type = e & 255u, note = (e >> 8) & 127u, vel = (e >> 16) & 127u;
        RING_PUBLISH();
        ev_r++;
        if (type == EV_AUD_ON) {
            note_on(note, vel);
            continue;
        }
        if (type == EV_AUD_OFF) {
            note_off(note);
            continue;
        }
        if (type == EV_ALLOFF) {
            down.cnt = lat.cnt = 0;
            all_off();
        } else if (type == EV_ON) {
            if (latch && !down.cnt) {                      /* a new chord */
                if (!arp)
                    for (i = 0; i < lat.cnt; i++)
                        note_off((uint32_t)lat.n[i]);
                lat.cnt = 0;
            }
            nl_add(&down, note, vel);
            if (latch)
                nl_add(&lat, note, vel);
            if (!arp)
                note_on(note, vel);
        } else if (type == EV_OFF) {
            nl_del(&down, note);
            if (!latch && !arp)
                note_off(note);
        }
    }
    arp_tick(arp);
}

/* ------------------------------------------------------------- the control tick --- */
static const uint16_t RATE_BEATS8[9] = {256, 128, 64, 32, 16, 8, 4, 2, 1};   /* 32 beats a cycle .. 1/8 beat */
static const int16_t FOCUS_XY[6][2] = {{0, 0}, {-32767, -32767}, {32767, -32767}, {-32767, 32767}, {32767, 32767}, {0, 0}};

static int32_t spin_inc(void)                              /* SPIN's turn per tick: (v / 64)^2 turns a beat */
{
    int32_t v = pt.g[G_SPIN];
    uint32_t beat = (uint32_t)pt.g[G_TEMPO] * BEAT8_K / 8u;   /* (a turn a beat) */
    return (int32_t)((beat >> 12) * (uint32_t)(v * v)) * (v < 0 ? -1 : 1);
}
static uint32_t orbit_inc(void)                            /* the orbit's phase step per tick */
{
    uint32_t beats8 = RATE_BEATS8[clamp(pt.g[G_RATE], 0, 8)];
    return (uint32_t)pt.g[G_TEMPO] * BEAT8_K / beats8;
}

/* the suborbit around (x, y): the green orb; the focus overrides both */
static void sub_at(uint32_t sp, int32_t *ox, int32_t *oy, int32_t *gx, int32_t *gy)
{
    int32_t r = clamp((ost.p.sub * 160) >> 8, 0, 30000);   /* (gliding) */
    *gx = clamp(*ox + ((sinq(sp) * r) >> 15), -32767, 32767);
    *gy = clamp(*oy - ((cosq(sp) * r) >> 15), -32767, 32767);
    if (focus) {
        *ox = *gx = FOCUS_XY[focus % 6u][0];
        *oy = *gy = FOCUS_XY[focus % 6u][1];
    }
}

static void voice_tick(voice_t *v, uint32_t oinc, uint32_t sinc)
{
    const int16_t *g = pt.g;
    uint32_t c, p, out = (uint32_t)(v - vs) >= vmax;
    int32_t w[NC], cut = 0, res = 0, cents, a, x, y;
    vflt_t *f = &vf[v - vs];
    if (out)                                               /* (the CPU guard took it: out, quickly) */
        v->on = 0;
    env_run(&v->stage, &v->env, v->on, g[G_AA], g[G_AD], g[G_AS], out ? 8 : g[G_AR]);
    if (!v->stage) {
        v->on = 0;
        return;
    }
    /* the orbit envelope: a timed walk along the ADSR path; A, D (then held at the sustain point), R */
    if (!v->on && v->ostage < 3u) {
        v->ostage = 3, v->oprog = 0;
        v->rx = v->ox, v->ry = v->oy;
    }
    if (v->oprog < 65535 && !freeze) {
        int32_t t = g[v->ostage == 1u ? G_OA : v->ostage == 2u ? G_OD : G_OR];
        v->oprog += (int32_t)(ENV_STEP[t & 127] >> 8);
        if (v->oprog >= 65535) {
            v->oprog = 65535;
            if (v->ostage == 1u)
                v->ostage = 2, v->oprog = 0;
        }
    }
    p = (uint32_t)(v->ostage - 1u) * 0x55555555u + (uint32_t)v->oprog * 0x5555u;   /* (the ADSR walk) */
    v->oph += oinc;
    v->sph += sinc;
    orbit_path(v->oph - oinc, p, &x, &y);
    orbit_place(&x, &y);
    if (orbit_all_adsr() && v->ostage == 3u) {             /* the release starts where the orb was */
        x = v->rx + (int32_t)(((int64_t)(x - v->rx) * v->oprog) >> 16);
        y = v->ry + (int32_t)(((int64_t)(y - v->ry) * v->oprog) >> 16);
    }
    v->ox = x, v->oy = y;
    sub_at(v->sph, &v->ox, &v->oy, &v->gx, &v->gy);
    orbit_weights(v->gx, v->gy, w, g[G_SHARP]);
    /* pitch: glide (MONO legato), bend, detune, POLY drift */
    if (v->pitch != v->target)
        v->pitch = g[G_GLIDE] && v->legato
                   ? v->target + (int32_t)(((int64_t)(v->pitch - v->target) * ENV_FALL[g[G_GLIDE] & 127]) >> 16)
                   : v->target;
    if (g[G_POLY] == PM_POLY && g[G_DETUNE]) {
        if ((xorshift(&v->rnd) & 1023u) == 0u)
            v->dtgt = (int32_t)(xorshift(&v->rnd) >> 21) - 1024;   /* +-1024 x detune / 512: cents x 16 */
        v->drift += (v->dtgt - v->drift) >> 8;
    } else
        v->drift = 0;
    cents = v->pitch / 16 + ((joy_x * g[G_BEND] * 100) >> 15) + (v->dcents + ((v->drift * g[G_DETUNE]) >> 9)) / 16;
    v->inc = cents_inc(cents) / 60u;
    /* the corners: level, mute, solo; the filter where the orbit is (each corner's cutoff, tracked or not) */
    for (c = 0; c < NC; c++) {
        const int16_t *cp = pt.c[c];
        int32_t lv = cp[CP_LEVEL] * cp[CP_LEVEL] * 2;
        int32_t idx = cp[CP_TRACK] ? (((cents - SVF_C0) * SVF_KC) >> 16) + cp[CP_CUT] * 8 - 160 : cp[CP_CUT] * 8 + 7;
        cut += w[c] * idx;
        res += w[c] * cp[CP_RES];
        if (cp[CP_MUTE] || (solo && !((solo >> c) & 1u)))
            lv = 0;
        v->wt[c] = (w[c] * (lv > 32767 ? 32767 : lv)) >> 15;
        if (solo && ((solo >> c) & 1u))                    /* a corner alone: heard whole */
            v->wt[c] = lv > 32767 ? 32767 : lv;
    }
    cut >>= 15, res >>= 15;
    if (v->cut < 0)
        v->cut = cut << 8;
    v->cut += ((cut << 8) - v->cut) >> 2;                  /* (no zipper) */
    cut = v->cut >> 8;
    svf_coef(&f->m, cut, 131072 - res * 1010);
    svf_coef(&f->b, cut, 92682);                           /* LP24: a second, gentle pole pair */
    a = clamp((pt.c[0][CP_CUT] * w[0] + pt.c[1][CP_CUT] * w[1] + pt.c[2][CP_CUT] * w[2] + pt.c[3][CP_CUT] * w[3]) >> 13,
              0, 511);                                     /* VOX: the vowel by the cutoff, U E O I A */
    {
        uint32_t k = (uint32_t)a >> 7, fr = (uint32_t)a & 127u;
        svf_coef(&f->f1, VOWEL[k * 2u] + (((VOWEL[k * 2u + 2u] - VOWEL[k * 2u]) * (int32_t)fr) >> 7), 9000);   /* Q ~7 */
        svf_coef(&f->f2, VOWEL[k * 2u + 1u] + (((VOWEL[k * 2u + 3u] - VOWEL[k * 2u + 1u]) * (int32_t)fr) >> 7), 11000);
    }
    f->vox = res * 258;
    a = env_amp(v->env);                                   /* amplitude: envelope, velocity, follower / stack */
    a = (a * (9830 + v->vel * 180)) >> 15;
    if (v->follower || (g[G_POLY] == PM_MONO && v != vs))
        a = (a * g[G_FOLVOL] * 258) >> 15;
    v->at = a;
}

static void synth_init(void)
{
    uint32_t i;
    for (i = 0; i < sizeof ovt / sizeof ovt[0]; i++)
        ovt[i] = OVT_SRC[i];
    syn.arp_note = -1;
}
