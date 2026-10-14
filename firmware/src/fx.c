/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The effects after the voices, independent of the orbit: drive (a band-pass into a saturator, beside the dry
 * signal), chorus (stereo, four moving taps), delay (ping-pong, synced to the tempo; at full feedback a loop),
 * reverb (a plate). The mixer sets chorus, delay and reverb against the dry sound, then the volume. */

static inline int16_t s16(int32_t x) { return (int16_t)clamp(x, -32767, 32767); }

/* ------------------------------------------------------------- drive --- */
static struct { svf_t f; int32_t st[2][2]; } drv;
static void drive_run(int32_t *l, int32_t *r)
{
    int32_t amt = pt.g[G_DRIVE], gain, i, bp[2][CTL], *x[2] = {l, r}, k;
    if (!amt)
        return;
    svf_coef(&drv.f, 300 + pt.g[G_DCOL] * 5, 70000);       /* the colour: 150 Hz .. 3 kHz */
    gain = 4096 + amt * amt;                               /* x1 .. x5 (Q12) */
    for (k = 0; k < 2; k++) {
        svf_run(&drv.f, drv.st[k], x[k], bp[k], 1);
        for (i = 0; i < CTL; i++)
            x[k][i] += (sat((bp[k][i] * gain) >> 11) * amt) >> 7;
    }
}

/* ------------------------------------------------------------- chorus --- */
#define CH_N 2048u
static int16_t ch_buf[CH_N];
static struct { uint32_t w, ph; } chs;
static inline int32_t rd_lin(const int16_t *m, uint32_t mask, uint32_t pos8)   /* position x 256 */
{
    uint32_t p = pos8 >> 8;
    int32_t a = m[p & mask], b = m[(p + 1u) & mask];
    return a + (((b - a) * (int32_t)(pos8 & 255u)) >> 8);
}
static void chorus_run(int32_t *l, int32_t *r)
{
    int32_t mix = pt.g[G_CHMIX] * 129, dep = 40 + pt.g[G_CHDEP] * 2, i, k;   /* depth: samples x 256 / 128 */
    uint32_t inc = 3000u + (uint32_t)pt.g[G_CHSPD] * (uint32_t)pt.g[G_CHSPD] * 9u;   /* ~0.03 .. 4 Hz */
    if (!mix)
        return;
    for (i = 0; i < CTL; i++) {
        int32_t wet[2];
        uint32_t w = ++chs.w;
        ch_buf[w & (CH_N - 1u)] = s16((l[i] + r[i]) >> 1);
        chs.ph += inc;
        for (k = 0; k < 2; k++) {                          /* two taps a side, a third of a turn apart */
            uint32_t pa = chs.ph + (uint32_t)k * 0x80000000u, pb = pa + 0x55555555u;
            int32_t da = (530 << 8) + ((sinq(pa) * dep) >> 7), db = (530 << 8) + ((sinq(pb) * dep) >> 7);
            wet[k] = (rd_lin(ch_buf, CH_N - 1u, (w << 8) - (uint32_t)da) + rd_lin(ch_buf, CH_N - 1u, (w << 8) - (uint32_t)db)) >> 1;
        }
        l[i] += (mix * (wet[0] - l[i])) >> 15;             /* (full: half dry, half moving) */
        r[i] += (mix * (wet[1] - r[i])) >> 15;
    }
}

/* ------------------------------------------------------------- delay --- */
#define DL_N 32768u                                /* 0.74 s a side */
static int16_t dl_buf[2][DL_N] __attribute__((section(".pool")));
static struct { uint32_t w; int32_t d, lp[2]; } dls;
static const uint8_t DL_DIV[7] = {1, 2, 3, 4, 6, 8, 12};   /* sixteenths: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4 */
static void delay_run(int32_t *l, int32_t *r)
{
    int32_t mix = pt.g[G_DLMIX] * 258, fb = pt.g[G_DLFB] * 258, i, tgt;
    uint32_t div = DL_DIV[clamp(pt.g[G_DLTIME] * 7 / 128, 0, 6)], loop = pt.g[G_DLFB] >= 127;
    tgt = (int32_t)(FS * 15u * div / (uint32_t)clamp(pt.g[G_TEMPO], 40, 240));   /* samples */
    tgt = clamp(tgt, 64, (int32_t)DL_N - 4) << 8;
    if (!dls.d)
        dls.d = tgt;
    if (!mix && !dls.lp[0] && !dls.lp[1])
        return;
    for (i = 0; i < CTL; i++) {
        uint32_t w = ++dls.w;
        int32_t a, b, in = (l[i] + r[i]) >> 1;
        dls.d += (tgt - dls.d) >> 12;                      /* (a new time slides: tape-like) */
        a = rd_lin(dl_buf[0], DL_N - 1u, (w << 8) - (uint32_t)dls.d);
        b = rd_lin(dl_buf[1], DL_N - 1u, (w << 8) - (uint32_t)dls.d);
        if (!loop) {                                       /* the repeats darken; at full feedback they stay */
            dls.lp[0] += ((a - dls.lp[0]) * 20000) >> 15;
            dls.lp[1] += ((b - dls.lp[1]) * 20000) >> 15;
            a = dls.lp[0], b = dls.lp[1];
        }
        dl_buf[0][w & (DL_N - 1u)] = s16(in + ((b * fb) >> 15));   /* ping: left to right to left */
        dl_buf[1][w & (DL_N - 1u)] = s16((a * fb) >> 15);
        l[i] += (a * mix) >> 15;
        r[i] += (b * mix) >> 15;
    }
}

/* ------------------------------------------------------------- reverb --- */
/* a plate after J. Dattorro, "Effect Design 1" (1997), lengths for 44.1 kHz, run at half rate: input diffusers,
 * the figure-eight tank (two halves: a modulated allpass, delay, damping, allpass, delay), seven taps a side.
 * SIZE: the decay, COLOUR: damping and input bandwidth. */
enum { PL_PRE = 33408, PL_PREM = 1023 };
static const uint16_t PL_LEN[12] = {210, 159, 562, 410, 1012, 6598, 2667, 5512, 1362, 6249, 3936, 4687};
static const int16_t PL_TAP[2][7] = {{394, 4407, 2835, 2958, 2949, 277, 1580}, {523, 5374, 1820, 3961, 3128, 496, 179}};
static const uint16_t PL_OFF[12] = {0, 210, 369, 931, 1341, 2353, 8951, 11618, 17130, 18492, 24741, 28677};
_Static_assert(28677 + 4687 <= PL_PRE, "PLATE: the lines below the pre-delay");
static int16_t pl_mem[PL_PRE + PL_PREM + 1] __attribute__((section(".pool")));
static struct { uint16_t ri[12]; int32_t st[2], lp[3], o[2], prev[2], in; uint32_t ph, n; } pls;
static inline int32_t pl_at(const int16_t *b, uint32_t len, int32_t i, int32_t back)
{
    i -= back;
    return b[i < 0 ? i + (int32_t)len : i];
}
static inline int32_t pl_ap(int16_t *b, uint16_t *ri, uint32_t len, int32_t x, int32_t c)
{
    int32_t o = b[*ri], w = x - ((o * c) >> 15);
    b[*ri] = s16(w);
    *ri = (uint16_t)(*ri + 1u >= len ? 0u : *ri + 1u);
    return o + ((w * c) >> 15);
}
static inline void pl_put(int16_t *b, uint16_t *ri, uint32_t len, int32_t x)
{
    b[*ri] = s16(x);
    *ri = (uint16_t)(*ri + 1u >= len ? 0u : *ri + 1u);
}
static void plate_step(int32_t x)                          /* one half-rate step: new taps in pls.o */
{
    int16_t *m = pl_mem;
    uint16_t *ri = pls.ri;
    int32_t decay = 16000 + pt.g[G_RVSIZE] * 125, damp = 4000 + pt.g[G_RVCOL] * 220, ex, k, side;
    uint32_t pc = pls.n++;
    m[PL_PRE + (pc & PL_PREM)] = s16(x);
    x = m[PL_PRE + ((pc - 400u) & PL_PREM)];               /* pre-delay ~18 ms */
    pls.lp[2] += ((x - pls.lp[2]) * (9000 + pt.g[G_RVCOL] * 180)) >> 15;   /* input bandwidth */
    x = pls.lp[2] >> 1;
    x = pl_ap(m + PL_OFF[0], &ri[0], PL_LEN[0], x, 24576);
    x = pl_ap(m + PL_OFF[1], &ri[1], PL_LEN[1], x, 24576);
    x = pl_ap(m + PL_OFF[2], &ri[2], PL_LEN[2], x, 20480);
    x = pl_ap(m + PL_OFF[3], &ri[3], PL_LEN[3], x, 20480);
    pls.ph += 154000u;                                     /* ~0.8 Hz: the tank's wobble */
    ex = SINE[pls.ph >> 22] >> 11;
    for (k = 0; k < 2; k++) {
        uint32_t h = (uint32_t)k * 4u + 4u;
        int16_t *mb = m + PL_OFF[h];
        int32_t in = x + ((pls.st[k ^ 1] * decay) >> 15), e = k ? -ex : ex, rd, oo, a, b;
        rd = pl_at(mb, PL_LEN[h], ri[h], PL_LEN[h] - 20 + e);
        oo = in + ((rd * 22938) >> 15);
        pl_put(mb, &ri[h], PL_LEN[h], oo);
        a = rd - ((oo * 22938) >> 15);
        pl_put(m + PL_OFF[h + 1u], &ri[h + 1u], PL_LEN[h + 1u], a);
        b = pl_at(m + PL_OFF[h + 1u], PL_LEN[h + 1u], ri[h + 1u], (int32_t)PL_LEN[h + 1u] - 1);
        pls.lp[k] += ((b - pls.lp[k]) * damp) >> 15;           /* damping (COLOUR up: brighter) */
        b = pl_ap(m + PL_OFF[h + 2u], &ri[h + 2u], PL_LEN[h + 2u], (pls.lp[k] * decay) >> 15, 16384);
        pl_put(m + PL_OFF[h + 3u], &ri[h + 3u], PL_LEN[h + 3u], b);
        pls.st[k] = pl_at(m + PL_OFF[h + 3u], PL_LEN[h + 3u], ri[h + 3u], (int32_t)PL_LEN[h + 3u] - 1);
    }
    for (side = 0; side < 2; side++) {                     /* seven taps a side: + + - + (its half) - - - */
        const int16_t *t = PL_TAP[side];
        uint32_t A = side ? 4u : 8u, B = side ? 8u : 4u;
        int32_t o = pl_at(m + PL_OFF[A + 1u], PL_LEN[A + 1u], ri[A + 1u], t[0]) +
                    pl_at(m + PL_OFF[A + 1u], PL_LEN[A + 1u], ri[A + 1u], t[1]) -
                    pl_at(m + PL_OFF[A + 2u], PL_LEN[A + 2u], ri[A + 2u], t[2]) +
                    pl_at(m + PL_OFF[A + 3u], PL_LEN[A + 3u], ri[A + 3u], t[3]) -
                    pl_at(m + PL_OFF[B + 1u], PL_LEN[B + 1u], ri[B + 1u], t[4]) -
                    pl_at(m + PL_OFF[B + 2u], PL_LEN[B + 2u], ri[B + 2u], t[5]) -
                    pl_at(m + PL_OFF[B + 3u], PL_LEN[B + 3u], ri[B + 3u], t[6]);
        pls.prev[side] = pls.o[side];
        pls.o[side] = (o * 19661) >> 15;
    }
}
static void reverb_run(int32_t *l, int32_t *r)
{
    int32_t mix = pt.g[G_RVMIX] * 258, i;
    static int32_t idle;
    if (!mix) {                                            /* off: the tail still rings out a while */
        if (idle > FS * 8)
            return;
        idle += CTL;
    } else
        idle = 0;
    for (i = 0; i < CTL; i++) {
        int32_t x = (l[i] + r[i]) >> 1, yl, yr;
        if (i & 1) {                                       /* half rate: the input averaged, the output */
            plate_step((x + pls.in) >> 1);                 /* between two steps interpolated */
            yl = pls.o[0], yr = pls.o[1];
        } else {
            pls.in = x;
            yl = (pls.prev[0] + pls.o[0]) >> 1, yr = (pls.prev[1] + pls.o[1]) >> 1;
        }
        l[i] += (yl * mix) >> 15;
        r[i] += (yr * mix) >> 15;
    }
}

static void fx_run(int32_t *l, int32_t *r)
{
    drive_run(l, r);
    chorus_run(l, r);
    delay_run(l, r);
    reverb_run(l, r);
}
