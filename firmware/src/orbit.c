/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The orbit: the path families (the engine moves the blue orb along them, the screen draws them), their size /
 * rotation / centre, the suborbit, and the corner weights at a point. Q15 coordinates, -1 .. 1; y = -1 is the top
 * (corners 1 2), x = -1 the left (corners 1 3).
 * Every path is one formula with continuous parameters (WARP X, WARP Y, MORPH): a figure morphs as a knob turns,
 * never jumps. Where a formula needs whole numbers (a frequency ratio, a number of points) the two neighbours are
 * blended, so every in-between figure is still a closed curve. The family too is a position (FAMILY, fader 1): the
 * families in a row, two neighbours blended between them. The parameters glide (orbit_follow), and the path is
 * scaled to fit the square whole (orbit_fit) instead of being clamped. */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline int32_t sinq(uint32_t ph)                 /* sin, Q15, interpolated */
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], f = (int32_t)((ph >> 7) & 0x7FFFu);
    return a + (((SINE[i + 1u] - a) * f) >> 15);
}
static inline int32_t cosq(uint32_t ph) { return sinq(ph + 0x40000000u); }

/* the orbit's parameters as they glide: knob value x 256 */
typedef struct { int32_t wx, wy, mo, size, rot, cx, cy, os, sub, fam; } opar_t;
static struct {
    opar_t p;                                  /* now (gliding to the patch's values) */
    int32_t lim, slim;                         /* the largest scale that fits (orbit_fit), Q15; it, gliding */
    uint32_t key, n;                           /* (the fit: what it was made for; ticks) */
    uint32_t spin;                             /* SPIN: the whole path's turn now (a full turn: 2^32) */
    uint8_t spinning;                          /* (then the fit holds for every turn) */
} ost = {.lim = 32767, .slim = 32767};
static uint32_t isqrt32(uint32_t v)            /* floor(sqrt(v)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static inline int32_t unit(int32_t v) { return clamp(v + 16384, 0, 32767); }   /* -64 .. 63 (x 256) -> 0 .. 1, Q15 */
/* a whole-number setting from a knob (x 256), Q8: lo at -64, one more every 25 steps (-64, -39, -14, 11, 36, 61),
 * at most hi */
static inline int32_t steps25(int32_t v, int32_t lo, int32_t hi) { return clamp(lo * 256 + (v + 16384) / 25, lo * 256, hi * 256); }

/* a star / polygon / rose: radius at angle th for n points; s: rounded (0) .. straight-edged (1), d: the depth */
static int32_t star_r(uint32_t n, uint32_t th, int32_t s, int32_t d)
{
    uint32_t u = th * n;                                /* 0 at a vertex */
    int32_t vs = (32767 + cosq(u)) >> 1, cpn = cosq(0x80000000u / n), ct, rp, vp, v, rb;
    ct = cosq((uint32_t)((int32_t)(u ^ 0x80000000u) / (int32_t)n));   /* the angle from the edge's middle */
    rp = (int32_t)(((uint32_t)cpn << 15) / (uint32_t)(ct > cpn ? ct : cpn));   /* the polygon's radius */
    vp = (int32_t)(((uint32_t)(rp - cpn) << 15) / (uint32_t)(32768 - cpn));
    v = vs + (((vp - vs) * s) >> 15);
    rb = 32767 + (((rp - 32767) * s) >> 15);
    return (rb * (32767 - ((d * (32767 - v)) >> 15))) >> 15;
}

/* the ADSR path: corner 1 -> 2 (attack) -> towards 4 (decay, held at the sustain point) -> 3 (release) */
static const int16_t ADSR_P[4][2] = {{-27000, -27000}, {27000, -27000}, {27000, 27000}, {-27000, 27000}};
static void bowed(const int16_t *a, const int16_t *b, int32_t t, int32_t bow, int32_t *x, int32_t *y)
{
    int32_t dx = b[0] - a[0], dy = b[1] - a[1], s = (sinq((uint32_t)t << 15) * bow) >> 15;
    *x = a[0] + (int32_t)(((int64_t)dx * t) >> 16) - ((dy * s) >> 15);
    *y = a[1] + (int32_t)(((int64_t)dy * t) >> 16) + ((dx * s) >> 15);
}

/* family f with parameters o at path position p (2^32 = once round; ADSR: thirds A, D, R), unplaced */
static void fam_xy(uint32_t f, const opar_t *o, uint32_t p, int32_t *x, int32_t *y)
{
    switch (f) {
    case SH_STAR: {                    /* MORPH: 3 .. 8 points, WARP X: rose .. polygon, WARP Y: depth (a star; at least 0.2) */
        int32_t n8 = steps25(o->mo, 3, 8), fr = n8 & 255, s = unit(o->wx), d = 6554 + ((unit(o->wy) * 19660) >> 15), r;   /* depth 0.2 .. 0.8: never a plain circle */
        uint32_t n0 = (uint32_t)n8 >> 8;
        r = star_r(n0, p, s, d);
        if (fr)
            r += ((star_r(n0 + 1u, p, s, d) - r) * fr) >> 8;
        *x = (r * sinq(p)) >> 15, *y = -((r * cosq(p)) >> 15);
        return;
    }
    case SH_SPIRO: {                   /* MORPH: 1 .. 6 loops (an epicycle), WARP Y: their size (at least 0.2), WARP X: their phase */
        int32_t m8 = steps25(o->mo, 1, 6), fr = m8 & 255, k = 6554 + ((unit(o->wy) * 13107) >> 15), ex, ey;   /* loops 0.2 .. 0.6: never a plain circle */
        uint32_t m0 = (uint32_t)m8 >> 8, ph = (uint32_t)o->wx << 16, a0 = ph - p * m0, a1 = a0 - p;
        ex = sinq(a0), ey = -cosq(a0);
        if (fr)
            ex += ((sinq(a1) - ex) * fr) >> 8, ey += ((-cosq(a1) - ey) * fr) >> 8;
        *x = ((32767 - k) * sinq(p) + k * ex) >> 15;
        *y = (-(32767 - k) * cosq(p) + k * ey) >> 15;
        return;
    }
    case SH_PULSE: {                   /* between corners 1 and 4: WARP X the duty, WARP Y the edges */
        uint32_t duty = 0x80000000u + (uint32_t)(o->wx * 0x5000), e = (1u << 25) + (uint32_t)unit(o->wy) * 0x7000u;
        int32_t s;
        if (p < e)
            s = (int32_t)((p >> 16) * 32767u / (e >> 16));
        else if (p < duty)
            s = 32767;
        else if (p - duty < e)
            s = 32767 - (int32_t)(((p - duty) >> 16) * 32767u / (e >> 16));
        else
            s = 0;
        s = (int32_t)((((int64_t)s * s >> 15) * (3 * 32768 - 2 * s)) >> 16) * 2;   /* smoothstep */
        *x = *y = -27000 + ((54000 * s) >> 15);
        return;
    }
    case SH_ADSR: {                    /* WARP X bows attack and decay, WARP Y the release; O SUSTAIN: where it waits */
        int16_t ps[2];
        int32_t sus = (o->os * 258) >> 8;
        uint32_t seg = (uint32_t)(((uint64_t)p * 3u) >> 32);
        int32_t t = (int32_t)((((uint64_t)p * 3u) >> 16) & 0xFFFFu);
        ps[0] = (int16_t)(ADSR_P[2][0] + (((ADSR_P[1][0] - ADSR_P[2][0]) * sus) >> 15));
        ps[1] = (int16_t)(ADSR_P[2][1] + (((ADSR_P[1][1] - ADSR_P[2][1]) * sus) >> 15));
        if (seg == 0u)
            bowed(ADSR_P[0], ADSR_P[1], t, o->wx, x, y);
        else if (seg == 1u)
            bowed(ADSR_P[1], ps, t, o->wx, x, y);
        else
            bowed(ps, ADSR_P[3], t, o->wy, x, y);
        return;
    }
    default: {                         /* Lissajous: x = cos(a t + phase), y = sin(b t); WARP Y: a 1 .. 6, MORPH: b
                                        * 1 .. 6, WARP X: the phase. 1:1 a circle, 1:2 a figure eight, 3:2 a knot */
        int32_t a8 = steps25(o->wy, 1, 6), b8 = steps25(o->mo, 1, 6), fa = a8 & 255, fb = b8 & 255;
        uint32_t a0 = (uint32_t)a8 >> 8, b0 = (uint32_t)b8 >> 8, ph = (uint32_t)o->wx << 16;
        *x = cosq(p * a0 + ph);
        if (fa)
            *x += ((cosq(p * (a0 + 1u) + ph) - *x) * fa) >> 8;
        *y = sinq(p * b0);
        if (fb)
            *y += ((sinq(p * (b0 + 1u)) - *y) * fb) >> 8;
    }
    }
}

/* FAMILY (0 .. 124, 31 a family) runs through the families in this order: each grows out of the one before (a
 * circle gains loops, the loops sharpen into points, the points fold into a line, the line into the envelope's walk) */
#define FAM_STEP 31
static const uint8_t FAM_ORDER[SH_N] = {SH_LISSA, SH_SPIRO, SH_STAR, SH_PULSE, SH_ADSR};
static inline int32_t fam_q8(const opar_t *o) { return clamp(o->fam, 0, 4 * FAM_STEP * 256) / FAM_STEP; }   /* 0 .. 4, Q8 */

/* the path with parameters o at p (the ADSR family: at pa, the note's walk), unplaced: two neighbouring families
 * blended by FAMILY */
static void path_xy(const opar_t *o, uint32_t p, uint32_t pa, int32_t *x, int32_t *y)
{
    int32_t fq = fam_q8(o), f0 = fq >> 8, fr = fq & 255;
    uint32_t a = FAM_ORDER[f0];
    fam_xy(a, o, a == SH_ADSR ? pa : p, x, y);
    if (fr && f0 < 4) {
        int32_t bx, by;
        uint32_t b = FAM_ORDER[f0 + 1];
        fam_xy(b, o, b == SH_ADSR ? pa : p, &bx, &by);
        *x += ((bx - *x) * fr) >> 8;
        *y += ((by - *y) * fr) >> 8;
    }
}
static void orbit_path(uint32_t p, uint32_t pa, int32_t *x, int32_t *y) { path_xy(&ost.p, p, pa, x, y); }
static int orbit_all_adsr(void) { return fam_q8(&ost.p) >= 4 * 256; }   /* (the path the envelope's walk alone) */

/* size, rotation, centre: the path where it sits. SIZE goes to x 1.41, but never past what fits (orbit_fit): a path
 * too big for the square stops growing; it is never squashed against an edge */
static void orbit_place(int32_t *x, int32_t *y)
{
    uint32_t a = ((uint32_t)ost.p.rot << 17) + ost.spin;
    int32_t s = (ost.p.size * 365) >> 8, c = cosq(a), n = sinq(a), x0 = *x, y0 = *y;
    int32_t xr = (x0 * c - y0 * n) >> 15, yr = (x0 * n + y0 * c) >> 15;
    s = s < ost.slim ? s : ost.slim;
    *x = clamp(((ost.p.cx * 512) >> 8) + ((xr * s) >> 15), -32767, 32767);
    *y = clamp(((ost.p.cy * 512) >> 8) + ((yr * s) >> 15), -32767, 32767);
}

/* the largest scale (Q15) at which the path, turned and moved, stays inside the square: 128 points round (and a
 * star's tips exactly) */
static void orbit_fit(void)
{
    uint32_t a = (uint32_t)ost.p.rot << 17, k, np = 128u, n0 = 0;
    int32_t c = cosq(a), n = sinq(a), cx = (ost.p.cx * 512) >> 8, cy = (ost.p.cy * 512) >> 8, lim = 46341;
    if (FAM_ORDER[fam_q8(&ost.p) >> 8] == SH_STAR || (fam_q8(&ost.p) >> 8 < 4 && FAM_ORDER[(fam_q8(&ost.p) >> 8) + 1] == SH_STAR))
        n0 = (uint32_t)steps25(ost.p.mo, 3, 8) >> 8;
    for (k = 0; k < np + 2u * n0 + 2u; k++) {
        int32_t x, y, xr, yr, m;
        uint32_t p = k < np ? k * (0xFFFFFFFFu / np) : k < np + n0 ? (k - np) * (0xFFFFFFFFu / n0)
                                                                    : (k - np - n0) * (0xFFFFFFFFu / (n0 + 1u));
        orbit_path(p, p, &x, &y);
        if (ost.spinning) {                            /* (turning on and on: the fit for every turn, a circle) */
            int32_t r = (int32_t)isqrt32((uint32_t)(x * x) + (uint32_t)(y * y));
            int32_t room = 32000 - ((cx < 0 ? -cx : cx) > (cy < 0 ? -cy : cy) ? (cx < 0 ? -cx : cx) : (cy < 0 ? -cy : cy));
            if (r) {
                m = room <= 0 ? 0 : (int32_t)(((uint32_t)room << 15) / (uint32_t)r);
                lim = m < lim ? m : lim;
            }
            continue;
        }
        xr = (x * c - y * n) >> 15, yr = (x * n + y * c) >> 15;
        if (xr) {
            int32_t room = 32000 - (xr > 0 ? cx : -cx);
            m = room <= 0 ? 0 : (int32_t)(((uint32_t)room << 15) / (uint32_t)(xr > 0 ? xr : -xr));   /* (32 bits) */
            lim = m < lim ? m : lim;
        }
        if (yr) {
            int32_t room = 32000 - (yr > 0 ? cy : -cy);
            m = room <= 0 ? 0 : (int32_t)(((uint32_t)room << 15) / (uint32_t)(yr > 0 ? yr : -yr));
            lim = m < lim ? m : lim;
        }
    }
    ost.lim = lim;
}

/* once a control tick: the parameters glide to the patch's (~25 ms; now: at once), the fit follows (at most every 8
 * ticks: it costs) */
static void orbit_follow(const int16_t *g, int now)
{
    static const uint8_t ID[10] = {G_WARPX, G_WARPY, G_MORPH, G_SIZE, G_ROT, G_CX, G_CY, G_OS, G_SUBSZ, G_SHAPE};
    int32_t *v = &ost.p.wx;
    uint32_t k, key = 0;
    for (k = 0; k < 10u; k++) {
        int32_t t = g[ID[k]] * 256;
        if (ID[k] == G_ROT && !now) {                      /* (the rotation: the short way round) */
            int32_t d = t - v[k];
            d = d > 16384 ? d - 32768 : d < -16384 ? d + 32768 : d;
            v[k] = (v[k] + (d >> 5) + (d > 0) - (d < 0) + 32768) % 32768;
        } else
            v[k] = now ? t : v[k] + ((t - v[k]) >> 5) + (t > v[k]) - (t < v[k]);
        key = key * 131u + (uint32_t)v[k];
    }
    ost.spinning = g[G_SPIN] != 0;
    key = key * 3u + ost.spinning;
    if (key != ost.key && (now || (++ost.n & 7u) == 0u)) {
        ost.key = key;
        orbit_fit();
    }
    ost.slim = now ? ost.lim : ost.slim + ((ost.lim - ost.slim) >> 3);   /* (the fit glides too: no step in size) */
}

/* the corner weights at (x, y), Q15, summing to 1: bilinear, raised to the power sharp (1 .. 8) and normalized.
 * The higher, the more a corner is heard alone as the moon nears it (at the centre still all four alike); the path
 * can stay well inside the square. */
static void orbit_weights(int32_t x, int32_t y, int32_t *w, int32_t sharp)
{
    int32_t u = (x + 32768) >> 1, v = (y + 32768) >> 1, c, k, sum = 0, mx = 1, r;   /* u, v 0 .. 32767 */
    w[0] = ((32767 - u) * (32767 - v)) >> 15;
    w[1] = (u * (32767 - v)) >> 15;
    w[2] = ((32767 - u) * v) >> 15;
    w[3] = (u * v) >> 15;
    for (c = 0; c < 4; c++)
        mx = w[c] > mx ? w[c] : mx;
    for (c = 0; c < 4; c++) {                      /* (relative to the largest: no precision lost to the power) */
        int32_t b = (int32_t)(((uint32_t)w[c] << 15) / (uint32_t)mx), q;
        b = b > 32767 ? 32767 : b;
        q = b;
        for (k = 1; k < sharp; k++)
            q = (q * b) >> 15;
        sum += w[c] = q;
    }
    r = (int32_t)((1u << 30) / (uint32_t)(sum > 64 ? sum : 64));
    for (c = 0; c < 4; c++)
        w[c] = (w[c] * r) >> 15;
}
