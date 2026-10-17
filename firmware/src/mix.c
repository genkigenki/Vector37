/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* One control block: the keys, every voice's tick, the voices into the stereo bus, the effects, the volume, the
 * master (DC, limiter, soft clip). And the CPU guard: too slow a render lets fewer voices play. */
static int32_t bus_l[CTL], bus_r[CTL];

static void synth_tick(void)
{
    uint32_t v, oinc = freeze ? 0u : orbit_inc(), sinc = freeze ? 0u : SUB_RATE[pt.g[G_SUBSPD] & 127];
    syn.ticks++;
    orbit_follow(pt.g, 0);                         /* the path's parameters glide */
    keys_tick();
    syn.oph += oinc;                               /* the free-running orbit: no retrigger, the screen at rest */
    if (!freeze)
        ost.spin += (uint32_t)spin_inc();          /* SPIN: the whole path turning */
    syn.sph += sinc;
    {
        int32_t x, y;
        orbit_path(syn.oph, 0x55555555u + 0xFFFF * 0x5555u, &x, &y);
        orbit_place(&x, &y);
        syn.ox = x, syn.oy = y;
        sub_at(syn.sph, &syn.ox, &syn.oy, &syn.gx, &syn.gy);
    }
    for (v = 0; v < NVOICE; v++)
        if (vs[v].stage || vs[v].on)
            voice_tick(&vs[v], oinc, sinc);
    {   /* what the screen and the pads show: the newest voice sounding, else the free orbit */
        voice_t *n = &vs[syn.newest % NVOICE];
        int32_t w[NC], c;
        if (n->stage)
            for (c = 0; c < NC; c++)
                syn.w[c] = n->wt[c];
        else {
            orbit_weights(syn.gx, syn.gy, w, pt.g[G_SHARP]);
            for (c = 0; c < NC; c++)
                syn.w[c] = w[c];
        }
    }
}

/* a voice: its corners into one buffer, the amplitude, then its place in the stereo field (the gains ramp over the
 * block: the orbit's pan moves without a zipper) */
static void voice_render(voice_t *v)
{
    static int32_t tb[FT_N][CTL];                      /* (the audio ISR's alone) */
    int32_t x[CTL], i, c, a0 = v->amp, da, gl, gr, dl, dr, tl, tr;
    /* the place: the voices' spread plus the orbit's side x WIDTH (0: the spread alone, as before); 0..1023 -> a
     * quarter sine: constant power (panning only: L + R never cancels) */
    uint32_t p = (uint32_t)(clamp(v->pan + ((v->opan * pt.g[G_WIDTH]) >> 8), 0, 32767) >> 5), used = 0;
    for (i = 0; i < CTL; i++)
        x[i] = 0;
    for (c = 0; c < NC; c++)
        if (v->w[c] || v->wt[c]) {
            const int16_t *cp = pt.c[c];
            if (PAT_IS_WT((uint32_t)cp[CP_G1] % PAT_COUNT) || PAT_IS_WT((uint32_t)cp[CP_G2] % PAT_COUNT))
                corner_render_wt(v, (uint32_t)c, tb, &used, v->w[c], v->wt[c]);   /* (a wavetable) */
            else
                corner_render(v, (uint32_t)c, tb, &used, v->w[c], v->wt[c]);
        }
    voice_filters(v, tb, used, x);
    for (c = 0; c < NC; c++)
        v->w[c] = v->wt[c];
    v->ph += v->inc * CTL;
    tl = SINE[256u + (p >> 2)], tr = SINE[p >> 2];
    gl = v->gl, gr = v->gr, dl = (tl - gl) / CTL, dr = (tr - gr) / CTL;
    da = (v->at - a0) / CTL;
    for (i = 0; i < CTL; i++) {
        int32_t y = (x[i] * (a0 >> 2)) >> 13;
        a0 += da, gl += dl, gr += dr;
        bus_l[i] += (y * gl) >> 15;
        bus_r[i] += (y * gr) >> 15;
    }
    v->amp = v->at, v->gl = tl, v->gr = tr;
}

/* one control block: CTL frames into out (L, R interleaved, Q15) */
static void synth_block(int32_t *out, uint32_t n)
{
    uint32_t v, i;
    static const uint16_t TRIM[13] = {256, 287, 323, 362, 406, 456, 512, 575, 645, 724, 812, 911, 1022};   /* +1 dB steps */
    int32_t vol = (pt.g[G_VOLUME] * pt.g[G_VOLUME] * TRIM[clamp(pt.g[G_TRIM], 0, 12)]) >> 8;   /* (127, no trim: about x 1) */
    (void)n;
    synth_tick();
    for (i = 0; i < CTL; i++)
        bus_l[i] = bus_r[i] = 0;
    for (v = 0; v < NVOICE; v++)
        if (vs[v].stage)
            voice_render(&vs[v]);
    for (i = 0; i < CTL; i++) {                         /* (eight voices at once: the bus stays in range) */
        bus_l[i] = (bus_l[i] * 23) >> 4;
        bus_r[i] = (bus_r[i] * 23) >> 4;
    }
    fx_run(bus_l, bus_r);
    for (i = 0; i < CTL; i++) {
        int32_t l = (bus_l[i] >> 5) * vol >> 9, r = (bus_r[i] >> 5) * vol >> 9, a, b;   /* (headroom: the bus to 1e6) */
        master_out(&l, &r);
#if PANEL_MASTER
        l = (l * panel_master_q12) >> 12, r = (r * panel_master_q12) >> 12;   /* (the FM-1's MASTER pot) */
#endif
        a = l < 0 ? -l : l, b = r < 0 ? -r : r;
        if (a > syn.peak)
            syn.peak = a;
        if (b > syn.peak)
            syn.peak = b;
        if (a > syn.mpeak || b > syn.mpeak)
            syn.mpeak = a > b ? a : b;
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

/* the CPU guard (audio ISR, after each half buffer): q = render time / half-buffer time x 256. Over 85 %:
 * one voice fewer (the newest above the limit fades out fast); under 55 % for a while: one more. */
static void cpu_guard(uint32_t q)
{
    static uint32_t calm;
    if (q > 218u && vmax > 2u) {
        vmax--;
        calm = 0;
    } else if (q < 141u && vmax < NVOICE) {
        if (++calm > 400u)                              /* ~2.3 s */
            vmax++, calm = 0;
    } else
        calm = 0;
}
