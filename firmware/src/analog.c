/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Melodee), Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca)
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* The master, after Melodee37's: a tanh knee, the DC blocker with error feedback, the peak limiter in front
 * of the soft clipper. */

/* 32767 tanh(x / 32768), |x| < 65536 (flat beyond), interpolated */
static inline int32_t sat(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 5, y;
    y = i >= 2048 ? SAT_Q15[2048] : SAT_Q15[i] + (((SAT_Q15[i + 1] - SAT_Q15[i]) * (a & 31)) >> 5);
    return x < 0 ? -y : y;
}

/* DC blocker (~2 Hz; the step's remainder fed back: no offset is left after silence), peak limiter (fast
 * attack, ~150 ms release) in front of the soft clipper: chords get quieter, not crushed */
#define LIM_T 18000
static struct { int32_t dc[2], dce[2], env; } mst = {{0, 0}, {0, 0}, LIM_T};
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = x * 64 - *dc + *err, d = e >> 12;     /* (x 64, x 4096: shifts of a negative value are UB) */
    *err = e - d * 4096;
    *dc += d;
    return x - ((*dc + 32) >> 6);
}
static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(clamp(*l, -1000000, 1000000), &mst.dc[0], &mst.dce[0]);
    *r = dc_block(clamp(*r, -1000000, 1000000), &mst.dc[1], &mst.dce[1]);
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > mst.env)
        mst.env += (a - mst.env) >> 2;
    else if (mst.env > LIM_T)
        mst.env -= ((mst.env - LIM_T) >> 12) + 1;
    if (mst.env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)mst.env);
        *l = ((*l >> 4) * g) >> 11;
        *r = ((*r >> 4) * g) >> 11;
        syn.overs++;
    }
    *l = sat(*l);
    *r = sat(*r);
}
