/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The screen, by meaning: the top line says what a pad, fader or button did; the orbit leads, a little universe (a
 * square in a night sky with twinkling stars; the four corners are planets glowing as much as they sound, the
 * selected one zoomed up in a red ring; the blue orb a small sun on its dotted path, its trace behind it, the green orb
 * its moon on a faint ring; every other voice a smaller pair). A planet's values show in its speech bubble (its eight
 * knobs as pictures; a pad's list), everything global in the central box; at the bottom four small path faders. The
 * planets and the overlays reach over the square's edge into the sky. KNOB BANK: the guide, with pictures. Only what
 * moved is drawn again: the sky in small rectangles. */
#define C_BG RGB(2, 3, 12)
#define FX0 32                                     /* the orbit: a square, left, top, size */
#define FY0 31
#define FW 176
#define CORNER_INSET 13
#define SX0 0                                      /* the sky round it (drawn as the field): planets, bubbles and */
#define SY0 18                                     /* boxes reach over the square's edge into it */
#define SX1 240
#define SY1 220
#define PL_OUT 26                                  /* a planet's reach outwards (glow, rings): within the sky */
/* the pace: the main loop at most every UI_LOOP_MS (main.c), the field at most every UI_FIELD_MS (~60 frames a second;
 * a frame that takes longer: the next as soon as it is done). What moves by the frame moves by the time: the old
 * pace was a field frame every 30 ms (UI_FIELD_MS 28, UI_LOOP_MS 15 give the old screens, bit for bit) */
#ifndef UI_LOOP_MS
#define UI_LOOP_MS 16u
#endif
#ifndef UI_FIELD_MS
#define UI_FIELD_MS 16u
#endif
#define SAMPLE_MS 28u                              /* (the sun's tail, the moon's trail: a place every ~30 ms at most) */
static uint32_t field_dt = 30u;                    /* (ms since the last field frame, 1 .. 60) */

static const uint8_t CCOL[NC][3] = {{255, 90, 130}, {255, 176, 60}, {176, 120, 255}, {50, 215, 195}};   /* planets */
static const uint8_t SUN[3] = {80, 150, 255}, MOON[3] = {110, 255, 140};

static uint16_t rgbk(const uint8_t *c, int32_t k)  /* colour c at k / 256 */
{
    k = clamp(k, 0, 256);
    return RGB(c[0] * k >> 8, c[1] * k >> 8, c[2] * k >> 8);
}
static inline uint32_t hash3(int32_t x, int32_t y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
/* the clip (screen coordinates): the orbit's things (path, suns, moons) stay inside the square, the rest not */
static int32_t clip_x0 = -32768, clip_y0 = -32768, clip_x1 = 32767, clip_y1 = 32767;
static inline int in_clip(int32_t x, int32_t y) { return x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1; }
/* add light (r g b at k / 256) to a canvas pixel */
static inline void cv_add_at(uint16_t *px, const uint8_t *c, int32_t k)   /* (k > 0) */
{
    uint32_t p = swap16(*px), r, g, b;
    r = ((p >> 11) << 3) + (uint32_t)(c[0] * k >> 8);
    g = (((p >> 5) & 63u) << 2) + (uint32_t)(c[1] * k >> 8);
    b = ((p & 31u) << 3) + (uint32_t)(c[2] * k >> 8);
    *px = swap16(RGB(r > 255u ? 255u : r, g > 255u ? 255u : g, b > 255u ? 255u : b));
}
static __attribute__((noinline)) void cv_add(int32_t x, int32_t y, const uint8_t *c, int32_t k)
{
    if (!in_clip(x, y))
        return;
    x += cv_ox, y += cv_oy;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || k <= 0)
        return;
    cv_add_at(&cv_buf[(uint32_t)y * cv_w + (uint32_t)x], c, k);
}
/* the part of the screen the canvas holds, within the clip (x0 <= x < x1); clip: 0 the canvas alone */
static void cv_view(int clip, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    *x0 = -cv_ox, *y0 = -cv_oy, *x1 = (int32_t)cv_w - cv_ox, *y1 = (int32_t)cv_h - cv_oy;
    if (clip) {
        *x0 = *x0 > clip_x0 ? *x0 : clip_x0, *y0 = *y0 > clip_y0 ? *y0 : clip_y0;
        *x1 = *x1 < clip_x1 ? *x1 : clip_x1, *y1 = *y1 < clip_y1 ? *y1 : clip_y1;
    }
}
/* a soft glow of radius r, brightness k (falls off with the square of the distance); only the canvas' rows and
 * columns are visited (the same pixels as each tested) */
static void glow(int32_t cx, int32_t cy, int32_t r, const uint8_t *c, int32_t k)
{
    int32_t x, y, r2 = r * r, vx0, vy0, vx1, vy1, xa, xb, ya, yb;
    uint32_t inv;
    if (k <= 0 || r <= 0)
        return;                                    /* (nothing: every pixel's light would be <= 0) */
    /* n / r2 as (n * inv) >> 32, inv = 2^32 / r2 rounded up: exact while n * r2 < 2^32 (n <= k * r2: k < 2^32 / r2^2,
     * k <= 300 and r <= 40 here); no division a pixel */
    inv = 0xFFFFFFFFu / (uint32_t)r2 + 1u;
    if ((uint64_t)(uint32_t)k * (uint32_t)r2 * (uint32_t)r2 >= 0x100000000ull)
        inv = 0;                                   /* (out of that range: divided, as before) */
    cv_view(1, &vx0, &vy0, &vx1, &vy1);
    xa = vx0 - cx > -r ? vx0 - cx : -r, xb = vx1 - 1 - cx < r ? vx1 - 1 - cx : r;
    ya = vy0 - cy > -r ? vy0 - cy : -r, yb = vy1 - 1 - cy < r ? vy1 - 1 - cy : r;
    for (y = ya; y <= yb; y++) {
        uint16_t *row = cv_buf + ((cy + y + cv_oy) * (int32_t)cv_w + cx + cv_ox);
        for (x = xa; x <= xb; x++) {
            int32_t d = x * x + y * y, v;
            if (d >= r2)
                continue;
            if (inv) {
                uint32_t e = (uint32_t)(r2 - d), q = (uint32_t)(((uint64_t)((uint32_t)k * e) * inv) >> 32);
                v = (int32_t)(uint32_t)(((uint64_t)(q * e) * inv) >> 32);
            } else
                v = k * (r2 - d) / r2 * (r2 - d) / r2;
            if (v > 0)
                cv_add_at(row + x, c, v);
        }
    }
}
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t col)
{
    int32_t x, y, vx0, vy0, vx1, vy1, xa, xb, ya, yb;
    cv_view(1, &vx0, &vy0, &vx1, &vy1);
    xa = vx0 - cx > -r ? vx0 - cx : -r, xb = vx1 - 1 - cx < r ? vx1 - 1 - cx : r;
    ya = vy0 - cy > -r ? vy0 - cy : -r, yb = vy1 - 1 - cy < r ? vy1 - 1 - cy : r;
    for (y = ya; y <= yb; y++)
        for (x = xa; x <= xb; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, col);
}

/* ------------------------------------------------------------- the field's things --- */
static inline int32_t fpx(int32_t q) { return FX0 + (((q + 32768) * (FW - 1)) >> 16); }   /* Q15 -> pixel */
static inline int32_t fpy(int32_t q) { return FY0 + (((q + 32768) * (FW - 1)) >> 16); }
static void corner_xy(uint32_t c, int32_t *x, int32_t *y)
{
    *x = c & 1u ? FX0 + FW - 1 - CORNER_INSET : FX0 + CORNER_INSET;
    *y = c & 2u ? FY0 + FW - 1 - CORNER_INSET : FY0 + CORNER_INSET;
}

#define NSTAR 96
#define TRAJ_N 1024                            /* the path's points at most (sub-pixel, ~1.5 pixels apart) */
#define TRAIL_N 10
#define TP_N 128                                   /* the sun's trace: its points at most (~1.5 pixels apart) */
#define TP_MS 300u                                 /* (as far back as the sun went in 0.3 s ... */
#define TP_LEN (90 * 16)                           /* ... but at most 90 pixels along the path) */
#define MOONS 8
static struct {
    int16_t tx[TRAJ_N], ty[TRAJ_N];                /* the path, 1/16 pixels */
    uint16_t ntj;                                  /* its points now */
    int16_t tjb[TRAJ_N / 32u][4];                  /* (each 32 points' box, pixels: x0 y0 x1 y1) */
    uint32_t thash;                                /* (the path's key) */
    int16_t sx, sy, mx, my, mr;                    /* sun, moon, the moon's ring radius */
    int32_t sx16, sy16, mr16, mx16, my16;          /* the sun, the moon's ring radius, the moon, 1/16 pixels */
    uint32_t mph, oph;                             /* the moon's phase round the sun; the sun's on the path */
    uint8_t msrc;                                  /* (whose moon: a voice, or 255 the free orbit) */
    int16_t trx[TRAIL_N], try_[TRAIL_N];           /* the comet's tail, newest first (sampled: an ADSR walk, focus) */
    uint8_t ntr;
    int16_t tpx[TP_N], tpy[TP_N];                  /* the sun's trace: the path itself behind it, 1/16 pixels, */
    uint8_t tpk[TP_N];                             /* newest first; its brightness */
    uint8_t ntp;                                   /* its points (0: the sampled tail instead) */
    uint8_t nmoon;
    int16_t ox[MOONS], oy[MOONS], gx[MOONS], gy[MOONS];   /* the other voices: their suns and moons */
    int32_t w[NC], pw[NC];                         /* corner weights now; as the planets are drawn */
    uint32_t t;                                    /* time (ms) the frame shows */
    uint8_t fader, pulse;                          /* fader: its overlay (+1); pulse: the planets pulsing */
    uint8_t ov, ovc, kb;                           /* a bubble: a pad's list (group + 1) or a knob's value (+1); its planet */
    int32_t zoom[NC];                              /* a planet's zoom: 0 small .. 256 selected */
    uint8_t gk, cbox;                              /* the central box: its kind; its knob lit (+1) */
    uint16_t shown;                                /* what the overlays show (SH_*) */
    int32_t om;                                    /* the orbit in charge (no planet selected): 0 .. 256, gliding */
    uint8_t big;                                   /* the enlarged knob (+1) */
    uint8_t ovm, tails;                            /* the bubble's planets (bits); the tails drawn (the host tests) */
} sc;

static const char *const SHAPE_S[SH_N] = {"LISA", "STAR", "SPIR", "PULS", "ADSR"};   /* (by family id) */

/* ------------------------------------------------------------- what moves: dirty tiles --- */
/* the sky as 30 x 26 tiles of 8 pixels: whatever moved marks the tiles it touched (where it was, where it is);
 * a frame draws the marked tiles again, a row's neighbours as one rectangle; most of the sky marked: all of it */
#define TILE 8
#define NTILE 26                                   /* (rows; 30 columns) */
typedef struct { int16_t x0, y0, x1, y1; } rect_t;
static uint32_t tiles[NTILE];
static rect_t prev_spr[MOONS + 2 + TRAIL_N];
static uint32_t nprev;
static void rq_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* screen pixels, x1 y1 exclusive */
{
    int32_t tx0 = (clamp(x0, SX0, SX1 - 1) - SX0) / TILE, tx1 = (clamp(x1 - 1, SX0, SX1 - 1) - SX0) / TILE;
    int32_t ty0 = (clamp(y0, SY0, SY1 - 1) - SY0) / TILE, ty1 = (clamp(y1 - 1, SY0, SY1 - 1) - SY0) / TILE, y;
    if (x1 <= SX0 || y1 <= SY0 || x0 >= SX1 || y0 >= SY1)
        return;
    for (y = ty0; y <= ty1; y++)
        tiles[y] |= ((2u << tx1) - 1u) & ~((1u << tx0) - 1u);
}

/* the path again when its parameters moved (they glide: while a fader moves, ~8 times a second): sampled finer
 * where it bends or runs fast (no two points more than ~1.5 pixels apart), so a long figure stays a line; the tiles
 * under the old and the new path are drawn again */
static void traj_mark(void)
{
    uint32_t k;
    for (k = 0; k < sc.ntj; k++)
        rq_add(sc.tx[k] >> 4, sc.ty[k] >> 4, (sc.tx[k] >> 4) + 2, (sc.ty[k] >> 4) + 2);
}
static void traj_at(uint32_t p, int32_t *nx, int32_t *ny)    /* a path point in 1/16 pixels */
{
    int32_t x, y;
    orbit_path(p, p, &x, &y);
    orbit_place(&x, &y);
    *nx = FX0 * 16 + (((x + 32768) * (FW - 1)) >> 12), *ny = FY0 * 16 + (((y + 32768) * (FW - 1)) >> 12);
}
static void traj_boxes(void)                       /* each 32 points' box (field_draw skips what a strip misses) */
{
    uint32_t i;
    for (i = 0; i < sc.ntj; i++) {
        int16_t *b = sc.tjb[i / 32u], x = (int16_t)(sc.tx[i] >> 4), y = (int16_t)(sc.ty[i] >> 4);
        if (!(i & 31u))
            b[0] = b[2] = x, b[1] = b[3] = y;
        b[0] = x < b[0] ? x : b[0], b[1] = y < b[1] ? y : b[1], b[2] = x > b[2] ? x : b[2], b[3] = y > b[3] ? y : b[3];
    }
}
static void traj_update(void)
{
    static uint32_t t_last, cost = 120u;           /* (the last redraw's pixels: SPIN redraws as often as the SPI
                                                      can spare, ~70000 pixels a second for it, 120 .. 500 ms) */
    uint32_t h = 1, k, n = 0, step = 1u << 24, done = 0, pos = 0, t0 = 0;
    int32_t lim;
    const int32_t *v = &ost.p.wx;
    int32_t px, py;
    for (k = 0; k < 10u; k++)
        h = h * 131u + (uint32_t)v[k];
    h = h * 7u + (uint32_t)ost.slim + (ost.spin >> 22) * 13u;
    if (h == sc.thash || (sc.thash && fm1_ms - t_last < (ost.spinning ? cost : 120u)))
        return;
    t_last = fm1_ms;
    for (k = 0; k < NTILE; k++)
        for (step = tiles[k]; step; step &= step - 1u)
            t0++;
    step = 1u << 24;
    traj_mark();                                   /* (where it was) */
    for (lim = 24 * 24; ; lim *= 4) {              /* (a long figure that needs more than TRAJ_N points: again with
                                                      the points twice as far apart, so it is always drawn closed) */
    n = 0, done = 0, pos = 0, step = 1u << 24;
    traj_at(0, &px, &py);
    sc.tx[n] = (int16_t)px, sc.ty[n] = (int16_t)py, n++;
    while (!done && n < TRAJ_N) {                  /* steps of 1/256 round, halved (to 1/8192) while too far apart */
        uint32_t p;
        int32_t nx, ny, d;
        for (;;) {
            p = pos > 0xFFFFFFFFu - step ? 0xFFFFFFFFu : pos + step;
            traj_at(p, &nx, &ny);
            d = (nx - px) * (nx - px) + (ny - py) * (ny - py);
            if (d <= lim || step <= (1u << 19))
                break;
            step >>= 1;
        }
        sc.tx[n] = (int16_t)nx, sc.ty[n] = (int16_t)ny, n++;
        px = nx, py = ny, pos = p;
        done = p == 0xFFFFFFFFu;
        if (d * 9 < lim && step < (1u << 24))
            step <<= 1;                            /* (straighter: longer steps again) */
    }
    if (done || lim >= 24 * 24 * 64)
        break;
    }
    sc.ntj = (uint16_t)n;
    traj_boxes();
    traj_mark();                                   /* (where it is) */
    {
        uint32_t t1 = 0, m;
        for (k = 0; k < NTILE; k++)
            for (m = tiles[k]; m; m &= m - 1u)
                t1++;
        cost = clamp((int32_t)((t1 - t0) * TILE * TILE * 1000u / 70000u), 120, 500);
    }
    sc.thash = h ? h : 1u;
}

static int16_t star_xy(uint32_t i, int32_t *x, int32_t *y)   /* a star's place; its base brightness */
{
    static uint8_t sx[NSTAR], sy[NSTAR], sb[NSTAR], ready;   /* (fixed: worked out once) */
    if (!ready) {
        uint32_t j, h;
        for (j = 0; j < NSTAR; j++) {
            h = hash3((int32_t)j, 7, 99u);
            sx[j] = (uint8_t)(SX0 + 1 + (int32_t)(h % (SX1 - SX0 - 2)));
            sy[j] = (uint8_t)(SY0 + 1 + (int32_t)((h >> 12) % (SY1 - SY0 - 2)));
            sb[j] = (uint8_t)(50 + (h >> 24) % 150u);
        }
        ready = 1;
    }
    i %= NSTAR;
    *x = sx[i], *y = sy[i];
    return (int16_t)sb[i];
}
static int32_t star_k(uint32_t i, uint32_t t)      /* its brightness now: a slow twinkle */
{
    int32_t x, y, b = star_xy(i, &x, &y);
    uint32_t h = hash3((int32_t)i, 3, 5u);
    return b * (180 + (sinq(t * (1200000u + (h & 0xFFFFFu)) + h) >> 9)) >> 8;   /* (1.5 .. 3.5 s) */
}

static void bubble_draw(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static int ov_overview(void);
/* ------------------------------------------------------------- a planet: its sound, drawn --- */
/* The selected planet zooms up (sc.zoom) and shows its eight settings: stripes = GEN 1 (number, slant; N: speckles),
 * a lower band = GEN 2 (when BLEND lets it in), turning speed = TUNE 2, a ring = BLEND (solid: added, dashed:
 * modulated, its dashes = MODE, wider with more), glow = CUTOFF, sparkles on the rim = RESO, brightness = LEVEL; and
 * eight satellites in the knobs' colours on the side facing the middle, each as big as its value. */
static int32_t sat_frac(const int16_t *q, uint32_t k)     /* knob k's value, 0 .. 256 */
{
    switch (k) {
    case 0: return q[CP_G1] * 256 / 6;
    case 1: return q[CP_G2] * 256 / 6;
    case 2: return (4 - q[CP_T2]) * 64;
    case 3: return (q[CP_BLEND] < 0 ? -q[CP_BLEND] : q[CP_BLEND]) * 4;
    case 4: return q[CP_BMODE] * 64;
    case 5: return q[CP_CUT] * 2;
    case 6: return q[CP_RES] * 2;
    default: return q[CP_LEVEL] * 2;
    }
}
static int planet_band(int32_t px, int32_t py, int32_t R, uint32_t pat, int32_t off)   /* a stripe here? */
{
    uint32_t a = pat * 0x11111111u;                 /* the slant: a pattern's own */
    int32_t u = ((px * cosq(a) + py * sinq(a)) >> 11) + off, n = 2 + (int32_t)pat;   /* (1/16 pixels) */
    if (pat == PAT_N)
        return (hash3(px, py, (uint32_t)off >> 6) & 7u) == 0u;
    return ((((u + 4096) * n) / (R * 32)) & 1) != 0;
}
static int32_t planet_r0;                          /* (the boot: a planet's radius, else 0) */
#define MUTE_ANIM_MS 450u                          /* a planet switched off / on: its animation */
static void planet(uint32_t c, int32_t x, int32_t y, int32_t mute)
{
    const int16_t *q = pt.c[c];
    int32_t z = sc.zoom[c], R0 = planet_r0 ? planet_r0 : 7 + (z * 8 >> 8), R = R0, R2, wk = sc.pw[c] >> 7, k, px, py;
    int32_t lf = 100 + q[CP_LEVEL] * 156 / 127, dim = 205 + (z * 51 >> 8), fade = 256, outline = 0;
    uint32_t age = ui.mute_t[c] ? fm1_ms - ui.mute_t[c] : 0xFFFFu;
    int anim = age < MUTE_ANIM_MS, off_anim = anim && q[CP_MUTE], on_anim = anim && !q[CP_MUTE];
    if (off_anim) {                                  /* switched off: lit (a flash), then it collapses, dims */
        int32_t t2 = age < 140u ? 0 : (int32_t)(age - 140u) * 256 / (MUTE_ANIM_MS - 140);
        mute = 0;
        R = R0 - (R0 / 4) * t2 / 256, fade = 256 - 216 * t2 / 256, outline = 110 * t2 / 256;
    } else if (mute)                                 /* muted: a dark body inside an outline */
        R = R0 - R0 / 4, fade = 40, outline = 110;
    R2 = R * R + R;
    int32_t off = z ? (int32_t)((sc.t / 48u * (uint32_t)(5 - q[CP_T2])) % (uint32_t)(R * 64 * 840)) : 0;   /* turning: TUNE 2 */
    int32_t b = q[CP_BLEND], rx = R * 3 / 2 + (b < 0 ? -b : b) / 16, ry = rx * 3 / 10, rw = 1 + (b < 0 ? -b : b) / 24;
    uint16_t rc = rgbk(CCOL[c], mute ? 50 : 190);
    if (!mute)                                       /* the glow: as loud as it is heard, as wide as its CUTOFF */
        glow(x, y, R + 2 + (q[CP_CUT] * 6 / 127) * (z + 64) / 320, CCOL[c], (40 + wk * 3 / 4) * fade >> 8);
    if (on_anim) {                                   /* switched on: it ignites, a white flash, a ring of light, rays */
        int32_t tt = (int32_t)age * 256 / MUTE_ANIM_MS, rr = R + 3 + 8 * tt / 256, kk = 256 - tt, j;
        glow(x, y, R + 9, CCOL[c], 255 * kk >> 8);
        if (tt < 110)
            glow(x, y, R + 10, (const uint8_t[]){255, 255, 255}, (110 - tt) * 5 / 2);
        for (k = 0; k < 128; k++) {
            uint32_t a = (uint32_t)k << 25;
            for (j = 0; j < 2; j++)
                cv_add(x + ((sinq(a) * (rr - j)) >> 15), y - ((cosq(a) * (rr - j)) >> 15), (const uint8_t[]){255, 240, 210}, kk);
        }
        for (k = 0; k < 8; k++) {                    /* (rays) */
            uint32_t a = (uint32_t)k * 0x20000000u + 0x10000000u;
            for (j = R + 1; j <= (rr + 3 < R + 11 ? rr + 3 : R + 11); j++)
                cv_add(x + ((sinq(a) * j) >> 15), y - ((cosq(a) * j) >> 15), (const uint8_t[]){255, 255, 255}, kk);
        }
    }
    if ((sc.pulse >> c) & 1u)                        /* touched: it pulses */
        glow(x, y, R + 9, CCOL[c], 70 + (sinq(sc.t * 9000000u) >> 9));
    for (k = 0; b && k < 128; k++) {                 /* the ring, its back half (behind the body) */
        uint32_t a = (uint32_t)k << 25;
        int32_t s = sinq(a), dash = 2 + q[CP_BMODE];
        if (s >= 0 || (b > 0 && (k / dash) & 1))
            continue;
        for (px = 0; px < rw; px++)
            cv_pset(x + ((cosq(a) * rx) >> 15), y + ((s * ry) >> 15) + px, rc);
    }
    {
    int32_t vx0, vy0, vx1, vy1, xa, xb, ya, yb;
    cv_view(0, &vx0, &vy0, &vx1, &vy1);              /* (the body: the canvas' part of it) */
    xa = vx0 - x > -R ? vx0 - x : -R, xb = vx1 - 1 - x < R ? vx1 - 1 - x : R;
    ya = vy0 - y > -R ? vy0 - y : -R, yb = vy1 - 1 - y < R ? vy1 - 1 - y : R;
    for (py = ya; py <= yb; py++)                    /* the body: lit from the top left, striped */
        for (px = xa; px <= xb; px++) {
            int32_t d = px * px + py * py, lk;
            if (d > R2)
                continue;
            lk = 210 - (px + py) * 72 / R - d * 64 / R2;
            if (planet_band(px, py, R, (uint32_t)q[CP_G1] % PAT_COUNT, off))
                lk -= 50;
            if (b && py * 3 > R && planet_band(px, py, R, (uint32_t)q[CP_G2] % PAT_COUNT, -off))   /* GEN 2 below */
                lk += 40;
            lk = ((lk * lf >> 8) * dim >> 8) * fade >> 8;
            cv_pset(x + px, y + py, rgbk(CCOL[c], lk));
        }
    }
    for (k = 0; b && k < 128; k++) {                 /* the ring's front half */
        uint32_t a = (uint32_t)k << 25;
        int32_t s = sinq(a), dash = 2 + q[CP_BMODE];
        if (s < 0 || (b > 0 && (k / dash) & 1))
            continue;
        for (px = 0; px < rw; px++)
            cv_pset(x + ((cosq(a) * rx) >> 15), y + ((s * ry) >> 15) + px, rc);
    }
    for (k = 0; !mute && k < q[CP_RES] / 18; k++) {  /* sparkles: RESO */
        uint32_t h = hash3((int32_t)c, k, sc.t / 120u), a = h << 7;
        cv_add(x + ((sinq(a) * R) >> 15), y + ((cosq(a) * R) >> 15), (const uint8_t[]){255, 255, 255}, 120 + (int32_t)(h >> 25));
    }
    if (outline)                                     /* muted (or going): the dark outline */
        for (k = 0; k < 64; k++) {
            uint32_t a = (uint32_t)k << 26;
            cv_pset(x + ((sinq(a) * (R + 1)) >> 15), y - ((cosq(a) * (R + 1)) >> 15), rgbk(CCOL[c], outline));
        }
    if (off_anim && age < 140u) {                    /* switched off: a bolt of lightning into it, a white flash */
        int32_t bx = x + (c & 1u ? 9 : -9), by = y - R - 10, j;
        glow(x, y, R + 9, (const uint8_t[]){235, 240, 255}, (int32_t)(140u - age) * 2);
        for (j = 0; j < 5; j++) {
            int32_t nx = bx + (x - bx) * (j + 1) / 5 + (j < 4 ? (int32_t)(hash3((int32_t)c, j, age / 35u) % 9u) - 4 : 0);
            int32_t ny = by + (y - R / 2 - by) * (j + 1) / 5;
            cv_line(bx, by, nx, ny, RGB(255, 255, 255));
            cv_line(bx + 1, by, nx + 1, ny, RGB(170, 190, 255));
            bx = nx, by = ny;
        }
    }
    if (z > 96)                                      /* selected: the red ring */
        for (k = 0; k < 120; k++) {
            uint32_t a = (uint32_t)k * 35791394u;
            cv_pset(x + ((sinq(a) * (R + 5)) >> 15), y + ((cosq(a) * (R + 5)) >> 15), RGB(255 * z >> 8, 40, 40));
        }
    if (z > 32 && !((sc.ov || sc.kb) && ov_overview() && ((sc.ovm >> c) & 1u))) {   /* the satellites (not under the overview) */
        uint32_t in = (c == 0u ? 0x20000000u : c == 1u ? 0xE0000000u : c == 2u ? 0x60000000u : 0xA0000000u);
        int32_t rs = R + 15, kt = ui.touch.kind == TK_CKNOB && ui.layer == L_MAIN && (int32_t)(ui.touch.until - fm1_ms) > 0 ? ui.touch.k : -1;
        for (k = 0; k < 8; k++) {
            uint32_t a = in + 0x271C71C7u - (uint32_t)k * 0x0B2D05B0u;   /* (x: sin, y: cos; 110 degrees round) */
            int32_t sx = x + ((sinq(a) * rs) >> 15), sy = y + ((cosq(a) * rs) >> 15), r = 1 + (sat_frac(q, (uint32_t)k) * 2 >> 8);
            if (k == kt)                             /* its knob turning: it swells */
                r += 1;
            disc(sx, sy, r, rgbk(KCOL[k], z));
            if (k == kt)
                cv_pset(sx, sy, RGB(255, 255, 255));
        }
    }
}

/* a speech bubble: box x0 y0 .. x1 y1 (the content's place), its tail wrapping round planet c, in its colour */
static void tri_fill(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, uint16_t col)
{
    int32_t x0 = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx), x1 = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t y0 = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy), y1 = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy), x, y;
    int32_t s = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), vx0, vy0, vx1, vy1;
    cv_view(0, &vx0, &vy0, &vx1, &vy1);              /* (only the canvas' part: the same pixels) */
    x0 = x0 > vx0 ? x0 : vx0, y0 = y0 > vy0 ? y0 : vy0, x1 = x1 < vx1 - 1 ? x1 : vx1 - 1, y1 = y1 < vy1 - 1 ? y1 : vy1 - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int32_t e0 = (bx - ax) * (y - ay) - (by - ay) * (x - ax), e1 = (cx - bx) * (y - by) - (cy - by) * (x - bx);
            int32_t e2 = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
            if (s > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0))
                cv_pset(x, y, col);
        }
}
static void bubble(uint32_t c, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t px, py, R = 7 + (sc.zoom[c] * 8 >> 8) + 7, k;
    int32_t cx = c & 1u ? x1 - 1 : x0, cy = c & 2u ? y1 - 1 : y0, dx = c & 1u ? -24 : 24, dy = c & 2u ? -24 : 24;
    uint16_t edge = rgbk(CCOL[c], 256), bg = RGB(8, 6, 22);
    corner_xy(c, &px, &py);
    for (k = 0; k < 2; k++) {                        /* the edge (k 0), then the inside two pixels in (k 1) */
        uint16_t col = k ? bg : edge;
        int32_t i = k ? 2 : 0;
        if (!k)
            tri_fill(px, py, cx + dx, cy, cx, cy + dy, col);
        disc(px, py, R - i, col);
        cv_rect(x0 + i, y0 + i, x1 - x0 - 2 * i, y1 - y0 - 2 * i, col);
        if (k)
            tri_fill(px, py, cx + dx - (dx > 0 ? 4 : -4), cy + (dy > 0 ? 2 : -2), cx + (dx > 0 ? 2 : -2), cy + dy - (dy > 0 ? 4 : -4), col);
    }
    planet(c, px, py, pt.c[c][CP_MUTE] != 0);       /* (the planet itself, inside its bubble) */
}
/* where a planet's bubble goes: from it towards the middle, w x h */
static void bubble_box(uint32_t c, int32_t w, int32_t h, int32_t *x0, int32_t *y0)
{
    int32_t px, py;
    corner_xy(c, &px, &py);
    *x0 = c & 1u ? px - 24 - w : px + 24;
    *y0 = c & 2u ? py - 14 - h : py + 14;
}
#define OV_W 158                                   /* the overview bubble */
#define OV_H 138
#define OVM_H 104                                  /* (several planets: one bubble in the middle, between them) */
#define OV_WAVE 5u                                 /* (sc.ov: the WAVE set's bubble; 1..4 the pad groups' lists) */
#define WV_H 120
static int ov_overview(void) { return sc.kb || sc.ov == PG_SELECT + 1u; }
static int ov_multi(void) { return ov_overview() && (sc.ovm & (sc.ovm - 1u)) != 0u; }
static void bubble_size(int32_t *w, int32_t *h)
{
    *w = ov_overview() || sc.ov == OV_WAVE ? OV_W : 112;
    *h = ov_multi() ? OVM_H : ov_overview() ? OV_H : sc.ov == OV_WAVE ? WV_H : 126;
}
static void bubble_place(int32_t *bx, int32_t *by, int32_t *w, int32_t *h)   /* the bubble's box now */
{
    bubble_size(w, h);
    if (ov_multi())
        *bx = FX0 + (FW - *w) / 2, *by = FY0 + (FW - *h) / 2;
    else
        bubble_box(sc.ovc & 3u, *w, *h, bx, by);
}

static const char *const BM_S[BM_N] = {"AM", "PM", "PMAM", "PDAM", "RPD"};
static const char *const KSHORT[L_N][8] = {
    [L_MAIN] = {"G1", "G2", "T2", "BL", "MD", "CUT", "RES", "LVL"},
    [L_ORBIT] = {"WRP", "WY", "ROT", "CON", "DST", "SPN", "WID", "BPM"},
    [L_VOICE] = {"ATK", "DEC", "SUS", "REL", "DET", "GLD", "VCS", "STR"},
    [L_FX] = {"REV", "DLY", "CHO", "DRV", "RVS", "DLT", "DFB", "VOL"},
    [L_CORNER] = {"T1", "TRK", "FT", "DCL", "CHD", "CHS", "RVC", "AUD"},
    [L_ARP] = {"AR", "BPM", "OA", "OD", "OS", "OR", "BND", "TRS"},
    [L_WAVE] = {"POS1", "ORB1", "POS2", "ORB2", "", "", "", ""},
    [L_SETUP] = {"OUT", "IN", "PAD", "SND", "", "", "", ""},
};
static const char *const GROUP_NAME[4] = {"SELECT", "MODE", "FILTER", "MUTE"};
#if BOARD_SMK37
#define KLAB(k, v) (v)
#define KOFF(k) 0
#define KTAG(k, x, y) ((void)0)
#else
/* the FM-1: KNOB 1..4 play one row of the eight (panel_fm1.c panel_krow): its cells carry the knob's number in a
 * tag of its colour at their right end (x: that end), the other row is dimmer */
#define KLAB(k, v) (panel_krow() < 0 || (int32_t)((k) >> 2) == panel_krow() ? (v) : (v) * 2 / 5)
#define KOFF(k) ((int32_t)((k) >> 2) == panel_krow() ? 11 : 0)
static void ktag(uint32_t k, int32_t x, int32_t y)
{
    char d[2] = {(char)('1' + (k & 3u)), 0};
    if ((int32_t)(k >> 2) != panel_krow())
        return;
    cv_rect(x - 10, y + 2, 9, 13, rgbk(KCOL[k], 256));
    cv_text(x - 9, y, &FONT_S, d, RGB(8, 8, 24));
}
#define KTAG(k, x, y) ktag(k, x, y)
#endif
static const int8_t FAM_PIC[SH_N][3] = {{0, -64, -39}, {0, 30, -14}, {30, 30, -14}, {0, -40, 0}, {30, -30, 0}};   /* a family's
                                                   picture (by FAM_ORDER place): WARP, WARP Y, MORPH */
static void shape_icon(int32_t x, int32_t y, int32_t w, int32_t h, int32_t fam, int32_t wx, int32_t wy, int32_t mo, uint16_t col);

/* ------------------------------------------------------------- the overview's pictures --- */
/* a wavetable as it reads now: the newest voice's position, else where the free orbit's moon puts it */
static int32_t wt_live(uint32_t c, uint32_t g)
{
    const voice_t *v = &vs[syn.newest % NVOICE];
    if (v->stage && v->wpos[0][0] >= 0)
        return clamp(v->wpos[c][g], 0, WT_SPAN);
    return wt_pos(pt.c[c], g, syn.gx, syn.gy, c);
}
static void g_wt(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bank, int32_t pos, uint16_t col)   /* its frame */
{
    const int16_t *a = WT_DATA + (bank % WT_NB * WT_NF + (uint32_t)(pos >> 16)) * WT_FR, *b = (pos >> 16) + 1 < WT_NF ? a + WT_FR : a;
    int32_t i, px = 0, py = 0, f = (pos & 0xFFFF) >> 1;
    w = w > 128 ? 128 : w;
    for (i = 0; i < w; i++) {                      /* (level 0: 128 samples, peak <= 28000) */
        int32_t j = i * 128 / w, v = a[j] + (((b[j] - a[j]) * f) >> 15), ny = y + h / 2 - v * (h / 2 - 1) / 28000;
        if (i)
            cv_line(px, py, x + i, ny, col);
        px = x + i, py = ny;
    }
}
/* where a generator reads in its bank: a line (frames 1 .. 32), WT POS a tick, the moon's reach (WT ORBIT) lit, the
 * place now a dot */
static void g_wtpos(int32_t x, int32_t y, int32_t w, const int16_t *q, uint32_t g, int32_t live, uint16_t tick, uint16_t reach)
{
    int32_t base = q[g ? CP_WP2 : CP_WP1] * (w - 1) / 127, amt = q[g ? CP_WO2 : CP_WO1] * (w - 1) / 64, a, b;
    a = clamp(base + (amt < 0 ? amt : 0), 0, w - 1), b = clamp(base + (amt > 0 ? amt : 0), 0, w - 1);
    cv_rect(x, y, w, 1, RGB(60, 62, 96));
    cv_rect(x + a, y, b - a + 1, 1, reach);
    cv_rect(x + base, y - 2, 1, 5, tick);
    cv_rect(x + (int32_t)((int64_t)live * (w - 1) / WT_SPAN) - 1, y - 1, 3, 3, RGB(255, 255, 255));
}
static void g_wave(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pat, uint16_t col)   /* a pattern's period */
{
    int32_t i, px = 0, py = 0, m = 1, v[80];
    uint32_t sh;
    const int16_t *t;
    w = w > 80 ? 80 : w;
    if (pat >= PAT_N) {                             /* noise: speckles, thicker in the middle */
        for (i = 0; i < w * h / 4; i++) {
            uint32_t r = hash3(i, w, 17u);
            cv_pset(x + (int32_t)(r % (uint32_t)w), y + (int32_t)(((r >> 8) % (uint32_t)h + (r >> 20) % (uint32_t)h) / 2u), col);
        }
        return;
    }
    t = table_for(pat, 1u << 21, &sh);
    for (i = 0; i < w; i++) {
        v[i] = tlu(t, sh, (uint32_t)i * (0xFFFFFFFFu / (uint32_t)w));
        m = v[i] > m ? v[i] : -v[i] > m ? -v[i] : m;
    }
    for (i = 0; i < w; i++) {
        int32_t ny = y + h / 2 - v[i] * (h / 2 - 1) / m;
        if (i)
            cv_line(px, py, x + i, ny, col);
        px = x + i, py = ny;
    }
}
static void g_sine(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t cyc, uint16_t col)
{
    int32_t i, px = 0, py = 0;
    for (i = 0; i < w; i++) {
        int32_t ny = y + h / 2 - ((sinq((uint32_t)i * cyc * (0xFFFFFFFFu / (uint32_t)w)) * (h / 2 - 1)) >> 15);
        if (i)
            cv_line(px, py, x + i, ny, col);
        px = x + i, py = ny;
    }
}
/* BLEND: a dial, + (added) left, G1 at the top, x (modulated) right; the needle where it is */
static void g_dial(int32_t x, int32_t y, int32_t w, int32_t h, int32_t b, uint16_t col, uint16_t dim)
{
    int32_t cx = x + w / 2, cy = y + h - 2, r = w / 2 - 2 < h - 3 ? w / 2 - 2 : h - 3, k, nx, ny;
    uint32_t a;
    for (k = -24; k <= 24; k++) {
        a = (uint32_t)(k * (0x40000000 / 24));
        cv_pset(cx + ((sinq(a) * r) >> 15), cy - ((cosq(a) * r) >> 15), k == -24 || k == 0 || k == 24 ? col : dim);
    }
    a = (uint32_t)(clamp(b, -64, 64) * (0x40000000 / 64));
    nx = cx + ((sinq(a) * (r - 1)) >> 15), ny = cy - ((cosq(a) * (r - 1)) >> 15);
    cv_line(cx, cy, nx, ny, col);
    cv_line(cx + 1, cy, nx + 1, ny, col);
    disc(cx, cy, 1, col);
}
/* MODE: a carrier (four periods) as the mode bends it over one period of the modulator */
static void g_mode(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t mode, uint16_t col)
{
    int32_t i, px = 0, py = 0;
    for (i = 0; i < w; i++) {
        uint32_t u = (uint32_t)i * (0xFFFFFFFFu / (uint32_t)w), p = u * 4u, q = p;
        int32_t am = 18022 + ((sinq(u) * 14746) >> 15), v, ny;
        if (mode == BM_PM || mode == BM_PMAM)
            q = p + (uint32_t)(sinq(u) * 32768);
        else if (mode == BM_PDAM || mode == BM_RPDAM) {
            uint32_t kn = mode == BM_PDAM ? 0x40000000u : 0xC0000000u, s1 = 0x80000000u / (kn >> 16), s2 = 0x80000000u / ((0u - kn) >> 16);
            q = p < kn ? (p >> 16) * s1 : 0x80000000u + ((p - kn) >> 16) * s2;
        }
        v = sinq(q);
        if (mode != BM_PM)
            v = (v * am) >> 15;
        ny = y + h / 2 - ((v * (h / 2 - 1)) >> 15);
        if (i)
            cv_line(px, py, x + i, ny, col);
        px = x + i, py = ny;
    }
}
/* CUTOFF + RESO: the filter's curve (its type), bending at the cutoff, its peak as high as the resonance */
static void g_filter(int32_t x, int32_t y, int32_t w, int32_t h, const int16_t *q, uint16_t col, uint16_t fill, int32_t lit)
{
    uint32_t ft = (uint32_t)q[CP_FTYPE] % FT_N;
    int32_t i, px = 0, py = 0, cut = q[CP_CUT], res = q[CP_RES], xc = cut * (w - 1) / 127, flat = h * 16 * 11 / 20;
    int32_t s = h * 16 * 2 / w * (ft == FT_LP24 ? 2 : 1), pk = res * h * 16 * 2 / 5 / 127, bw = w / 10 + 2, ty = y;
    for (i = 0; i < w; i++) {
        int32_t d = i - xc, ad = d < 0 ? -d : d, L, ny, bump = ad < bw ? pk * (bw - ad) * (bw - ad) / (bw * bw) : 0;
        switch (ft) {
        case FT_LP12: case FT_LP24: L = flat - (d > 0 ? s * d : 0) + bump; break;
        case FT_HP: L = flat - (d < 0 ? -s * d : 0) + bump; break;
        case FT_BP: L = flat + h * 16 / 5 - s * ad + bump; break;
        case FT_VOX: {
            int32_t d2 = d - w / 4, a2 = d2 < 0 ? -d2 : d2, b2 = bw * 3 / 2;
            L = flat / 2 + (ad < b2 ? (h * 16 / 3 + pk / 2) * (b2 - ad) / b2 : 0) + (a2 < b2 ? (h * 16 / 4 + pk / 2) * (b2 - a2) / b2 : 0);
            break;
        }
        default: L = flat;
        }
        L = clamp(L, 0, (h - 1) * 16);
        ny = y + h - 1 - L / 16;
        cv_rect(x + i, ny + 1, 1, y + h - 1 - ny, fill);
        if (i)
            cv_line(px, py, x + i, ny, col);
        if (i == xc)
            ty = ny;
        px = x + i, py = ny;
    }
    if (ft == FT_OFF)
        return;
    for (i = y; i < y + h; i += 2)                  /* the cutoff: a dotted line in K6's colour; the peak: K7's */
        cv_pset(x + xc, i, rgbk(KCOL[5], lit == 5 ? 256 : 120));
    disc(x + xc, ty, lit == 6 ? 2 : 1, rgbk(KCOL[6], lit == 6 ? 256 : 150));
}
/* knob k's picture for planet c (k 5, 6: one filter curve; lit: the knob turned, or -1; big: the enlarged one) */
static void ov_pic(uint32_t k, int32_t x, int32_t y, int32_t w, int32_t h, const int16_t *q, uint32_t c, int32_t lit, int big)
{
    uint16_t col, dim;
    if (x - 4 + cv_ox >= (int32_t)cv_w || x + w + 4 + cv_ox <= 0 || y - 4 + cv_oy >= (int32_t)cv_h || y + h + 4 + cv_oy <= 0)
        return;                                    /* (the picture, a little round it, off the canvas: not drawn) */
    col = rgbk(CCOL[c], 256), dim = rgbk(CCOL[c], 80);
    switch (k) {
    case 0: case 1: {
        uint32_t pat = (uint32_t)q[k ? CP_G2 : CP_G1] % PAT_COUNT;
        if (PAT_IS_WT(pat)) {                      /* a wavetable: its frame now, where it reads (a bar) */
            int32_t live = wt_live(c, k);
            g_wt(x, y, w, h - 3, pat - PAT_W1, live, col);
            g_wtpos(x, y + h - 1, w, q, k, live, rgbk(KCOL[2 * k], 256), rgbk(KCOL[2 * k + 1], 200));
        } else
            g_wave(x, y, w, h, pat, col);
        break;
    }
    case 2:
        if (big) {                                 /* GEN 1 over GEN 2: n periods against one */
            g_sine(x, y, w, h / 2, (uint32_t)q[CP_T2] % 5u + 1u, rgbk(KCOL[0], 230));
            g_sine(x, y + h / 2, w, h / 2, 1u, rgbk(KCOL[1], 230));
        } else
            text_c(x + w / 2, y + (h - 16) / 2 + 1, &FONT_S, N_RATIO[q[CP_T2] % 5], col);
        break;
    case 3:
        g_dial(x, y, w, h, q[CP_BLEND], col, dim);
        break;
    case 4:
        g_mode(x, y + 1, w, h - 2, (uint32_t)q[CP_BMODE] % BM_N, rgbk(GCOL[PG_MODE], q[CP_BLEND] > 0 ? 256 : 100));
        break;
    case 5: case 6:
        g_filter(x, y, w, h, q, col, rgbk(CCOL[c], 45), lit);
        break;
    default: {                                     /* LEVEL: a bar */
        int32_t bh = big ? 14 : 8, by = y + (h - bh) / 2, j;
        cv_rect(x, by, w, bh, RGB(20, 18, 46));
        cv_rect(x, by, w * q[CP_LEVEL] / 127, bh, col);
        for (j = 1; j < 4; j++)
            cv_rect(x + w * j / 4, by + bh, 1, 2, dim);
    }
    }
}
/* the overview: a planet's eight knobs, two rows of four as on the panel; picked (SELECT): which planet, its filter;
 * a knob turned: its name and value large beside its picture enlarged, its cell lit */
#define SH_OVERVIEW 1u                             /* (what the bubbles and boxes show: the host tests read it) */
#define SH_BIG 2u
#define SH_LIST 4u
#define SH_SPEAKER 8u
#define SH_CBOX 16u
#define SH_FAMLIST 32u
static void overview(uint32_t c, int32_t bx, int32_t by, int32_t w)
{
    const int16_t *q = pt.c[c];
    int32_t kl = sc.kb ? (int32_t)sc.kb - 1 : -1, k, cw = (w - 10) / 4;
    sc.shown |= SH_OVERVIEW;
    if (kl >= 0) {
        const kdest_t *d = &LAYERS[L_MAIN].k[kl];
        char v[12];
        val_text(&CDESC[d->id], q[d->id], d->id, 1, v, sizeof v);
        if (d->id == CP_BMODE)
            str_cpy(v, BM_S[q[CP_BMODE] % BM_N], sizeof v);
        cv_text(bx + 6, by + 4, &FONT_S, CDESC[d->id].label, rgbk(KCOL[kl], 256));
        if (text_w(&FONT_L, v) <= w - 84)
            cv_text(bx + 6, by + 19, &FONT_L, v, rgbk(CCOL[c], 256));
        else
            cv_text(bx + 6, by + 26, &FONT_S, v, rgbk(CCOL[c], 256));
        cv_rect(bx + w - 73, by + 4, 68, 48, RGB(18, 16, 44));
        ov_pic((uint32_t)kl, bx + w - 71, by + 6, 64, 44, q, c, kl, 1);
        sc.shown |= SH_BIG, sc.big = (uint8_t)(kl + 1);
    } else {
        char d[2] = {(char)('1' + c), 0};
        uint32_t i;
        cv_text(bx + 6, by + 4, &FONT_S, GROUP_NAME[PG_SELECT], rgbk(GCOL[PG_SELECT], 256));
        cv_text(bx + 6, by + 19, &FONT_L, d, rgbk(CCOL[c], 256));
        cv_text(bx + 34, by + 21, &FONT_S, N_FTYPE[q[CP_FTYPE] % FT_N], rgbk(GCOL[PG_FILTER], 256));
        cv_text(bx + 34, by + 36, &FONT_S, q[CP_MUTE] ? "MUTE" : "ON", q[CP_MUTE] ? rgbk(MUTED_COL, 256) : rgbk(GCOL[PG_MUTE], 220));
        for (i = 0; i < NC; i++) {                 /* the 2 x 2: which pad, which corner */
            int32_t gx = bx + w - 32 + (int32_t)(i & 1u) * 13, gy = by + 8 + (int32_t)(i >> 1) * 13;
            cv_rect(gx, gy, 11, 11, (ui.sel >> i) & 1u ? rgbk(GCOL[PG_SELECT], 256) : RGB(30, 34, 60));
        }
    }
    cv_rect(bx + 4, by + 54, w - 8, 1, rgbk(CCOL[c], 90));
    for (k = 0; k < 8; k++)                        /* the lit cell's ground */
        if (k == kl) {
            int32_t x = bx + 5 + (k & 3) * cw, y = by + 57 + (k >> 2) * 39;
            cv_rect(x - 1, y - 1, cw, 38, rgbk(KCOL[k], 256));
            cv_rect(x, y, cw - 2, 36, RGB(30, 26, 64));
        }
    for (k = 0; k < 8; k++) {
        int32_t x = bx + 5 + (k & 3) * cw, y = by + 57 + (k >> 2) * 39;
        KTAG(k, x + cw - 2, y);
        cv_text(x + 1, y, &FONT_S, KSHORT[L_MAIN][k], rgbk(KCOL[k], KLAB(k, kl < 0 || k == kl ? 256 : 200)));
        if (k <= 1 && !KOFF(k) && !PAT_IS_WT((uint32_t)q[k ? CP_G2 : CP_G1] % PAT_COUNT)) {   /* (the pattern's letter;
                                                      the FM-1's tag in its place; a wavetable: its bar instead) */
            const char *l = N_PAT[q[k ? CP_G2 : CP_G1] % PAT_COUNT];
            cv_text(x + cw - 4 - text_w(&FONT_S, l), y, &FONT_S, l, rgbk(CCOL[c], 256));
        }
        if (k != 6)
            ov_pic((uint32_t)k, x + 1, y + 17, k == 5 ? 2 * cw - 4 : cw - 4, 19, q, c, kl, 0);
    }
}
/* a pad's list (MODE, FILTER, MUTE): the group's name in its colour, which corner (a 2 x 2), its list, the item lit */
static void pad_list(uint32_t c, uint32_t g, int32_t bx, int32_t by, int32_t w, int32_t h)
{
    const int16_t *q = pt.c[c];
    const char *const *items;
    uint32_t n, now, i;
    char b[12] = "CORNER ";
    sc.shown |= SH_LIST;
    switch (g) {
    case PG_MODE: items = N_BMODE, n = BM_N, now = (uint32_t)q[CP_BMODE] % BM_N; break;
    case PG_FILTER: items = N_FTYPE, n = FT_N, now = (uint32_t)q[CP_FTYPE] % FT_N; break;
    default: items = N_MUTE, n = 2, now = (uint32_t)q[CP_MUTE] & 1u;
    }
    cv_text(bx + 6, by + 4, &FONT_S, GROUP_NAME[g], rgbk(GCOL[g], 256));
    b[7] = (char)('1' + c), b[8] = 0;
    cv_text(bx + 6, by + 20, &FONT_S, b, rgbk(CCOL[c], 256));
    for (i = 0; i < NC; i++) {
        int32_t gx = bx + w - 30 + (int32_t)(i & 1u) * 12, gy = by + 6 + (int32_t)(i >> 1) * 12;
        cv_rect(gx, gy, 10, 10, i == c ? rgbk(GCOL[g], 256) : RGB(30, 34, 60));
    }
    for (i = 0; i < n; i++) {
        int32_t y = by + 38 + (int32_t)i * 14;
        if (i == now)
            cv_rect(bx + 4, y, w - 8, 14, rgbk(GCOL[g], 256));
        cv_text(bx + 10, y - 1, &FONT_S, items[i], i == now ? RGB(8, 8, 16) : RGB(140, 145, 180));
    }
    if (g == PG_MODE && q[CP_BLEND] <= 0)
        cv_text(bx + 6, by + h - 17, &FONT_S, "HEARD: BLEND >", RGB(150, 160, 200));
}
#define SH_MULTI 64u
#define SH_PERPLANET 128u
/* several planets: their eight knobs, each cell a track with a dot per planet (its colour) where its value is; a knob
 * turned: its name and each planet's value in its colour */
static void overview_multi(uint32_t m, int32_t bx, int32_t by, int32_t w)
{
    int32_t kl = sc.kb ? (int32_t)sc.kb - 1 : -1, k, cw = (w - 10) / 4, x;
    uint32_t c, c0 = 0;
    for (c = NC; c-- > 0u;)
        if ((m >> c) & 1u)
            c0 = c;
    sc.shown |= SH_OVERVIEW | SH_MULTI;
    if (kl >= 0) {
        uint32_t id = LAYERS[L_MAIN].k[kl].id;
        x = cv_text(bx + 6, by + 3, &FONT_S, CDESC[id].label, rgbk(KCOL[kl], 256)) + 8;
        for (c = 0; c < NC; c++)
            if ((m >> c) & 1u) {
                char v[12];
                val_text(&CDESC[id], pt.c[c][id], id, 1, v, sizeof v);
                if (id == CP_BMODE)
                    str_cpy(v, BM_S[pt.c[c][CP_BMODE] % BM_N], sizeof v);
                x = cv_text(x, by + 3, &FONT_S, v, rgbk(CCOL[c], 256)) + 7;
            }
        sc.shown |= SH_PERPLANET;
    } else {
        x = cv_text(bx + 6, by + 3, &FONT_S, GROUP_NAME[PG_SELECT], rgbk(GCOL[PG_SELECT], 256)) + 8;
        for (c = 0; c < NC; c++)
            if ((m >> c) & 1u) {
                char d[2] = {(char)('1' + c), 0};
                x = cv_text(x, by + 3, &FONT_S, d, rgbk(CCOL[c], 256)) + 7;
            }
    }
    cv_rect(bx + 4, by + 21, w - 8, 1, RGB(90, 95, 130));
    for (k = 0; k < 8; k++)
        if (k == kl) {
            int32_t cx = bx + 5 + (k & 3) * cw, cy = by + 24 + (k >> 2) * 39;
            cv_rect(cx - 1, cy - 1, cw, 38, rgbk(KCOL[k], 256));
            cv_rect(cx, cy, cw - 2, 36, RGB(30, 26, 64));
        }
    for (k = 0; k < 8; k++) {
        const pdesc_t *p = &CDESC[LAYERS[L_MAIN].k[k].id];
        uint32_t id = LAYERS[L_MAIN].k[k].id;
        int32_t cx = bx + 5 + (k & 3) * cw, cy = by + 24 + (k >> 2) * 39, tw = cw - 6;
        KTAG(k, cx + cw - 2, cy);
        cv_text(cx + 1, cy, &FONT_S, KSHORT[L_MAIN][k], rgbk(KCOL[k], KLAB(k, kl < 0 || k == kl ? 256 : 200)));
        if (k != 6)
            ov_pic((uint32_t)k, cx + 1, cy + 16, k == 5 ? 2 * cw - 4 : cw - 4, 14, pt.c[c0], c0, kl, 0);
        cv_rect(cx + 1, cy + 34, tw, 1, RGB(70, 72, 110));     /* the track: a dot per planet */
        for (c = 0; c < NC; c++)
            if ((m >> c) & 1u)
                disc(cx + 1 + (pt.c[c][id] - p->min) * (tw - 1) / (p->max - p->min), cy + 34, 1, rgbk(CCOL[c], 256));
    }
}
/* one bubble for several planets: in the middle, a tail to each (in its colour), the box's edge pale */
static void bubble_multi(uint32_t m, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t c, k;
    uint16_t bg = RGB(8, 6, 22);
    for (k = 0; k < 2u; k++) {
        if (k)                                     /* (between: the box, its edge, its inside) */
            cv_rect(x0, y0, x1 - x0, y1 - y0, RGB(200, 205, 230)), cv_rect(x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4, bg);
        for (c = 0; c < NC; c++)
            if ((m >> c) & 1u) {
                int32_t px, py, R = 7 + (sc.zoom[c] * 8 >> 8) + 7;
                int32_t cx = c & 1u ? x1 - 1 : x0, cy = c & 2u ? y1 - 1 : y0, dx = c & 1u ? -24 : 24, dy = c & 2u ? -24 : 24;
                corner_xy(c, &px, &py);
                if (!k) {
                    tri_fill(px, py, cx + dx, cy, cx, cy + dy, rgbk(CCOL[c], 256));
                    disc(px, py, R, rgbk(CCOL[c], 256));
                } else {
                    disc(px, py, R - 2, bg);
                    tri_fill(px, py, cx + dx - (dx > 0 ? 4 : -4), cy + (dy > 0 ? 2 : -2), cx + (dx > 0 ? 2 : -2), cy + dy - (dy > 0 ? 4 : -4), bg);
                }
            }
    }
    for (c = 0; c < NC; c++)
        if ((m >> c) & 1u) {
            int32_t px, py;
            corner_xy(c, &px, &py);
            planet(c, px, py, pt.c[c][CP_MUTE] != 0);
        }
    sc.tails = (uint8_t)m;
}
/* the WAVE set's bubble (SCALE held, a planet selected): each generator's wavetable, its frame as it reads now, where
 * it reads in its bank (WT POS a tick, the moon's reach WT ORBIT lit, the place now a dot), the knobs' values in their
 * colours */
static void wave_panel(uint32_t c, int32_t bx, int32_t by, int32_t w)
{
    const int16_t *q = pt.c[c];
    char d[2] = {(char)('1' + c), 0};
    uint32_t g;
    int32_t kl = sc.gk ? (int32_t)sc.gk - 1 : -1;
    cv_text(bx + 6, by + 3, &FONT_S, "WAVE", RGB(220, 228, 255));
    cv_text(bx + 46, by + 3, &FONT_S, d, rgbk(CCOL[c], 256));
    for (g = 0; g < 2u; g++) {
        uint32_t pat = (uint32_t)q[g ? CP_G2 : CP_G1] % PAT_COUNT;
        int32_t y = by + 22 + (int32_t)g * 48, k;
        char b[24];
        str_cpy(b, g ? "G2 " : "G1 ", sizeof b);
        str_cat(b, N_PAT[pat], sizeof b);
        if (!PAT_IS_WT(pat)) {
            cv_text(bx + 6, y, &FONT_S, b, rgbk(CCOL[c], 140));
            cv_text(bx + 6, y + 16, &FONT_S, "NO WAVETABLE", RGB(90, 96, 140));
            continue;
        }
        str_cat(b, " ", sizeof b);
        str_cat(b, N_WT[pat - PAT_W1], sizeof b);
        cv_text(bx + 6, y, &FONT_S, b, rgbk(CCOL[c], 256));
        {
            int32_t live = wt_live(c, g);
            cv_rect(bx + w - 66, y + 17, 60, 27, RGB(18, 16, 44));
            g_wt(bx + w - 66, y + 18, 60, 25, pat - PAT_W1, live, rgbk(CCOL[c], 256));
            g_wtpos(bx + 6, y + 40, w - 78, q, g, live, rgbk(KCOL[2 * g], 256), rgbk(KCOL[2 * g + 1], 220));
        }
        for (k = 0; k < 2; k++) {                  /* WT POS, WT ORBIT: a mark in the knob's colour (the FM-1: its
                                                      number), the value; white while turned */
            uint32_t kk = 2u * g + (uint32_t)k;
            int32_t v = q[(g ? CP_WP2 : CP_WP1) + k], x = bx + 6 + k * 40;
            char t[8];
            if (k && v > 0) {
                t[0] = '+';
                fmt_int(t + 1, v);
            } else
                fmt_int(t, v);
            if (KOFF(kk))
                KTAG(kk, x + 10, y + 18);
            else
                cv_rect(x + 1, y + 21, 5, 10, rgbk(KCOL[kk], 256));
            cv_text(x + 12, y + 18, &FONT_S, t, (int32_t)kk == kl ? RGB(255, 255, 255) : rgbk(KCOL[kk], 230));
        }
    }
}
static void bubble_draw(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t c = sc.ovc & 3u;
    int32_t bx, by, w, h, px, py;
    bubble_place(&bx, &by, &w, &h);
    if (ov_multi()) {
        bubble_multi(sc.ovm, bx, by, bx + w, by + h);
        overview_multi(sc.ovm, bx, by, w);
        return;
    }
    corner_xy(c, &px, &py);
    if ((bx < px ? bx : px) - 26 >= x1 || (bx + w > px ? bx + w : px) + 27 <= x0 ||
        (by < py ? by : py) - 26 >= y1 || (by + h > py ? by + h : py) + 27 <= y0)
        return;
    bubble(c, bx, by, bx + w, by + h);
    sc.tails = (uint8_t)(1u << c);
    if (ov_overview())
        overview(c, bx, by, w);
    else if (sc.ov == OV_WAVE)
        wave_panel(c, bx, by, w);
    else
        pad_list(c, (sc.ov - 1u) & 3u, bx, by, w, h);
}

/* AUDITION: a small speaker beside the planet heard alone (outwards, over the square's edge) */
static void spk_xy(uint32_t c, int32_t *x, int32_t *y)
{
    corner_xy(c, x, y);
    *x += c & 1u ? 25 : -35, *y -= 6;
}
static void speaker(int32_t x, int32_t y, uint16_t col)   /* 10 x 13 */
{
    int32_t k;
    cv_rect(x, y + 4, 3, 5, col);
    for (k = 0; k < 3; k++)
        cv_rect(x + 3 + k, y + 3 - k, 1, 7 + 2 * k, col);
    for (k = -4; k <= 4; k++)
        cv_pset(x + 9 - k * k / 6, y + 6 + k, col);
}

/* ------------------------------------------------------------- the central box: what is global --- */
/* square, blue-white, a small sun in its corner (the sun is the path; the planets are the corners): a held button's
 * knob set; a fader's value and its place; for F1 the families, a list scrolling with the fader */
enum { CB_NONE, CB_LAYER, CB_FADER, CB_FAM, CB_MOON };
static void cbox_rect(uint32_t kind, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *w = kind == CB_LAYER ? 172 : kind == CB_FAM ? 152 : 160;
    *h = kind == CB_LAYER ? (ui.layer == L_ORBIT ? 90 : 128) : kind == CB_FAM ? 124 : 46;   /* (the orbit's: one item,
                                                      the path and the moon around it stay in sight) */
    *x = FX0 + (FW - *w) / 2, *y = FY0 + (FW - *h) / 2;
}
static void cbox_frame(int32_t x, int32_t y, int32_t w, int32_t h)
{
    cv_rect(x, y, w, h, RGB(170, 195, 255));
    cv_rect(x + 1, y + 1, w - 2, h - 2, RGB(5, 9, 28));
    cv_rect(x + 3, y + 3, w - 6, h - 6, RGB(50, 70, 130));
    cv_rect(x + 4, y + 4, w - 8, h - 8, RGB(5, 9, 28));
    glow(x + 12, y + 12, 7, SUN, 230);
    disc(x + 12, y + 12, 2, RGB(225, 240, 255));
}
#define SH_HINT 0x100u
#define SH_TRAIL 0x200u
static const char *orbit_hint(uint32_t id)          /* the orbit's knobs, said plainly ('|': the next line) */
{
    switch (id) {
    case G_WARPX: return "BENDS THE PATH:|ITS PHASE, EDGES";
    case G_WARPY: return "THE PATH'S OTHER|BEND: LOOPS, DEPTH";
    case G_ROT: return "TURNS THE PATH|ROUND THE MIDDLE";
    case G_SHARP: return "HOW MUCH A PLANET|TAKES OVER NEAR IT";
    case G_SUBSZ: return "HOW FAR THE MOON|IS FROM THE SUN";
#if BOARD_SMK37
    case G_SUBSPD: return "HOW FAST THE MOON|CIRCLES (MOD WHEEL)";
#else
    case G_SUBSPD: return "HOW FAST THE MOON|CIRCLES THE SUN";
#endif
    case G_SPIN: return "THE WHOLE PATH|TURNS, IN TIME";
    case G_RETRIG: return "EACH NOTE STARTS|THE PATH ANEW";
    case G_WIDTH: return "THE ORBIT HEARD|LEFT AND RIGHT";
    case G_TEMPO: return "THE CLOCK: PATH|SPEED, DELAY, ARP";
    default: return "";
    }
}
static int32_t fam_snap_k(void)                    /* F1 clicked in on a family: 256 fading to 0 over 0.25 s */
{
    static int32_t was = -1;
    static uint32_t t;
    int32_t on = pt.g[G_SHAPE] % FAM_STEP == 0;
    if (on && was == 0)
        t = fm1_ms | 1u;
    was = on;
    if (!on || !t || fm1_ms - t >= 250u)
        return 0;
    return (int32_t)(250u - (fm1_ms - t)) * 256 / 250;
}
static void central_draw(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t x, y, w, h;
    cbox_rect(sc.cbox, &x, &y, &w, &h);
    if (x >= x1 || x + w <= x0 || y >= y1 || y + h <= y0)
        return;
    sc.shown |= SH_CBOX;
    cbox_frame(x, y, w, h);
    if (sc.cbox == CB_LAYER) {                     /* the held button's eight knobs; the one turned large */
        uint32_t c = sel_first(), k;
        int32_t kl = sc.gk ? (int32_t)sc.gk - 1 : -1;
        cv_text(x + 24, y + 5, &FONT_S, LAYERS[ui.layer].name, RGB(210, 222, 255));
        if (kl >= 0) {
            const kdest_t *d = &LAYERS[ui.layer].k[kl];
            const char *lab;
            char v[12];
            int32_t tw;
            uint16_t col = d->corner == 1u ? rgbk(CCOL[c], 256) : RGB(255, 255, 255);
            knob_text((uint32_t)kl, &lab, v, sizeof v);
            cv_text(x + 8, y + 27, &FONT_S, lab, rgbk(KCOL[kl], 256));
            tw = text_w(&FONT_L, v);
            if (tw <= w - 20 - text_w(&FONT_S, lab))
                cv_text(x + w - 8 - tw, y + 20, &FONT_L, v, col);
            else
                cv_text(x + w - 8 - text_w(&FONT_S, v), y + 27, &FONT_S, v, col);
        } else
            cv_text(x + 8, y + 27, &FONT_S, "TURN A KNOB", RGB(90, 100, 150));
        if (ui.layer == L_ORBIT) {                 /* (no planet: what the knob does, in two short lines) */
            if (kl >= 0) {
                const char *e = orbit_hint(LAYERS[L_ORBIT].k[kl].id);
                char l1[24];
                uint32_t i = 0;
                while (e[i] && e[i] != '|' && i < sizeof l1 - 1u)
                    l1[i] = e[i], i++;
                l1[i] = 0;
                cv_rect(x + 6, y + 54, w - 12, 1, RGB(50, 70, 130));
                cv_text(x + 8, y + 56, &FONT_S, l1, RGB(190, 200, 235));
                if (e[i] == '|')
                    cv_text(x + 8, y + 71, &FONT_S, e + i + 1, RGB(190, 200, 235));
                sc.shown |= SH_HINT;
            }
            return;
        }
        cv_rect(x + 6, y + 54, w - 12, 1, RGB(50, 70, 130));
        for (k = 0; k < 8u; k++) {
            const kdest_t *d = &LAYERS[ui.layer].k[k];
            const char *lab;
            char v[12];
            int32_t cx = x + 7 + (int32_t)(k & 3u) * 40, cy = y + 58 + (int32_t)(k >> 2) * 33;
            if (d->corner == 0xFF)
                continue;
            if ((int32_t)k == kl)
                cv_rect(cx - 2, cy - 1, 40, 33, RGB(26, 34, 76));
            knob_text(k, &lab, v, sizeof v);
            if (d->corner == 0u && d->id == G_ARP)
                str_cpy(v, (const char *const[]){"OFF", "UP", "DOWN", "UPDN", "RND"}[pt.g[G_ARP] % ARP_N], sizeof v);
            if (d->corner == 3u && pt.g[G_POLY] == PM_MONO)   /* (VOICES: "MON3") */
                v[3] = (char)('0' + clamp(pt.g[G_STACK], 1, NVOICE));
            v[4] = 0;
            KTAG(k, cx + 39, cy);
            cv_text(cx, cy, &FONT_S, KSHORT[ui.layer][k], rgbk(KCOL[k], KLAB(k, 240)));
            if (d->corner == 1u) {                 /* (a planet's: its dot, its colour) */
                disc(cx + text_w(&FONT_S, KSHORT[ui.layer][k]) + 4, cy + 8, 2, rgbk(CCOL[c], 256));
                cv_text(cx, cy + 15, &FONT_S, v, rgbk(CCOL[c], 256));
            } else
                cv_text(cx, cy + 15, &FONT_S, v, RGB(230, 236, 255));
        }
        return;
    }
    if (sc.cbox == CB_MOON) {                      /* the mod wheel: MOON SPEED, the bar, the wheel (a tick) */
        int32_t v = pt.g[G_SUBSPD], j, n = 2 + v * 12 / 127, mx = x + w - 52, my = y + 13;
        char b[8];
        cv_text(x + 24, y + 5, &FONT_S, GDESC[G_SUBSPD].label, rgbk(MOON, 256));
        fmt_int(b, v);
        cv_text(x + w - 8 - text_w(&FONT_S, b), y + 5, &FONT_S, b, ui.mw_mode == 2u ? RGB(255, 255, 255) : RGB(150, 220, 170));
        for (j = 0; j <= n; j++) {                 /* (a small moon with its trail) */
            uint32_t a = 0x60000000u - (uint32_t)j * 0x04000000u;
            cv_add(mx + ((sinq(a) * 8) >> 15), my - ((cosq(a) * 8) >> 15), MOON, j ? 220 - j * 160 / (n + 1) : 0);
        }
        disc(mx + ((sinq(0x60000000u) * 8) >> 15), my - ((cosq(0x60000000u) * 8) >> 15), 2, rgbk(MOON, 256));
        cv_rect(x + 8, y + 28, w - 16, 8, RGB(14, 34, 24));
        cv_rect(x + 8, y + 28, (w - 16) * v / 127, 8, rgbk(MOON, 220));
        if (ui.mw_last >= 0)
            cv_rect(x + 8 + (w - 17) * ui.mw_last / 127, y + 24, 2, 16, RGB(255, 255, 255));
        return;
    }
    {
        uint32_t f = (sc.fader - 1u) & 3u, id = FADER_G[f];
        const pdesc_t *d = &GDESC[id];
        int32_t fp = ui.fad_last[f];
        char b[12];
        if (sc.cbox == CB_FADER) {                 /* the value (bar), the fader (a white tick) */
            int32_t v = (pt.g[id] - d->min) * 127 / (d->max - d->min);
            cv_text(x + 24, y + 5, &FONT_S, d->label, RGB(210, 222, 255));
            val_text(d, pt.g[id], id, 0, b, sizeof b);
            cv_text(x + w - 8 - text_w(&FONT_S, b), y + 5, &FONT_S, b, ui.fad_mode[f] == 2u ? RGB(255, 255, 255) : RGB(150, 170, 230));
            cv_rect(x + 8, y + 28, w - 16, 8, RGB(16, 22, 50));
            cv_rect(x + 8, y + 28, (w - 16) * v / 127, 8, RGB(80, 150, 255));
            cv_rect(x + 8 + (w - 17) * fp / 127, y + 24, 2, 16, RGB(255, 255, 255));
            return;
        }
        {                                          /* F1: the families, scrolling with the fader (up: the one above) */
            int32_t mid = y + h / 2, RH = 27, f256 = pt.g[G_SHAPE] * 256 / FAM_STEP, i, L = h - 16, th = L / SH_N, ty;
            int32_t snap = fam_snap_k();             /* (just clicked in on a family: 256 .. 0) */
            sc.shown |= SH_FAMLIST;
            cv_rect(x + 22, mid - 13, w - 42, 27, rgbk((const uint8_t[]){24, 38, 92}, 256));
            if (pt.g[G_SHAPE] % FAM_STEP == 0) {   /* on a family: locked in, its row framed */
                uint16_t fc = snap ? RGB(255, 255, 255) : RGB(170, 195, 255);
                cv_rect(x + 22, mid - 13, w - 42, 1, fc), cv_rect(x + 22, mid + 13, w - 42, 1, fc);
                cv_rect(x + w - 21, mid - 13, 1, 27, fc);
                if (snap)                          /* (the click: a flash round the row) */
                    cv_rect(x + 20 - snap / 64, mid - 15 - snap / 64, w - 38 + snap / 32, 1, RGB(255, 255, 255)),
                    cv_rect(x + 20 - snap / 64, mid + 15 + snap / 64, w - 38 + snap / 32, 1, RGB(255, 255, 255));
            }
            cv_rect(x + 22, mid - 13, 2, 27, RGB(170, 195, 255));
            for (i = 0; i < SH_N; i++) {
                int32_t dy = (f256 - i * 256) * RH / 256, ry = mid + dy, ad = dy < 0 ? -dy : dy, kk;
                if (ad > h / 2 - 16)
                    continue;
                kk = 256 - ad * 210 / (h / 2);
                shape_icon(x + 26, ry - 10, 28, 21, i * FAM_STEP, FAM_PIC[i][0], FAM_PIC[i][1], FAM_PIC[i][2],
                           rgbk((const uint8_t[]){110, 170, 255}, kk));
                cv_text(x + 58, ry - 8, &FONT_S, N_SHAPE[FAM_ORDER[i]], ad < RH / 2 ? rgbk((const uint8_t[]){255, 255, 255}, kk)
                                                                                    : rgbk((const uint8_t[]){150, 165, 215}, kk));
            }
            cv_rect(x + w - 14, y + 8, 6, L, RGB(16, 22, 50));   /* the scrollbar: a family's thumb where the fader is */
            for (i = 1; i < SH_N; i++)
                cv_rect(x + w - 14, y + 8 + L * i / SH_N, 6, 1, RGB(40, 52, 100));
            ty = y + 8 + (L - th) * (124 - pt.g[G_SHAPE]) / 124;
            cv_rect(x + w - 15, ty, 8, th, snap ? RGB(255, 255, 255) : pt.g[G_SHAPE] % FAM_STEP == 0 ? RGB(150, 190, 255)
                                                                                                : RGB(80, 130, 230));
            if (ui.fad_mode[f] != 2u)              /* (gliding to the fader: where the fader is, a tick) */
                cv_rect(x + w - 17, y + 8 + (L - 2) * (127 - fp) / 127, 12, 2, RGB(255, 255, 255));
        }
    }
}

/* the field over the canvas now (rectangle x0 y0 .. x1 y1, screen coordinates) */
/* the moon's trail: its real places on the screen, joined by a soft line fading from the moon to the oldest in six
 * steps (by age). As long as one turn of the moon takes, but at least 0.5 s: a place every frame, or for a slow moon
 * (a turn longer than the ring holds frames) evenly every turn / 1000, the last piece always reaching the moon. A
 * frame draws again only the newest pieces, the pieces that went and the ones that dimmed a step */
#define MT_N 448                                   /* (~13 s of places at one each 30 ms) */
/* the moon's trail: its places (newest first, one each ~30 ms), each the older end of a piece to the newer one (piece
 * 0: to the moon now). Where a piece is long (a fast moon: round the sun, the sun along the path, between two
 * places) the way between is filled in from the path and the moon's ring (the phases at the newer end, the speeds
 * then), once, when the piece is complete, so the trail follows the curve the moon went and never cuts across; the
 * piece to the moon is filled in anew each frame. Each piece keeps its box (its tiles marked when it changes) */
static struct {
    int16_t x, y;                                  /* (1/16 pixels) */
    uint16_t t;                                    /* (ms, 16 bits) */
    uint8_t lv;                                    /* (its step of brightness, by its age) */
    uint8_t fill;                                  /* (the piece to the newer place filled in) */
    uint32_t oph, sph;                             /* (the sun's and the moon's phases here) */
    uint32_t pa;                                   /* (the filled-in points: from mp[pa % MP_N], np of them) */
    uint16_t np, len;                              /* (... and the piece's length along, 1/16 pixels) */
    int16_t b[4];                                  /* (the piece's box, 1/16 pixels: x0 y0 x1 y1) */
} mt[MT_N] __attribute__((section(".pool")));
#define MT_AGE(k) ((uint16_t)((uint16_t)fm1_ms - mt[mt_idx(k)].t))
static uint32_t mt_w, mt_n;
static uint8_t mt_src = 254;
static int16_t mt_hx = -1, mt_hy;                  /* (the moon, as the last piece was drawn to it) */
static const int16_t MT_K[7] = {0, 35, 70, 110, 150, 195, 245};
#define MP_N 2048                                  /* (the filled-in points, all pieces: a ring) */
#define TL_SUB 48                                  /* (a piece longer than 3 pixels: filled in) */
#define TL_LEN (480 * 16)                          /* (at most 480 pixels of filled-in way: a very fast moon's swirl) */
static int16_t mp[MP_N][2];
static uint32_t mp_w, mt_len;                      /* (points written; the filled-in pieces' length) */
#define HP_N 256
static int16_t hp[HP_N][2], hb[4];                 /* (the piece to the moon: its points, its box) */
static uint32_t hn;
static uint8_t hfill;
static uint32_t mt_idx(uint32_t k) { return (mt_w + MT_N * 4u - 1u - k) % MT_N; }   /* k: 0 the newest */
static void rq_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
static void tl_at(uint32_t oph, uint32_t sph, uint32_t spin, int32_t *x, int32_t *y)   /* the moon at these phases, the
                                                                                      path turned so (1/16 pixels) */
{
    int32_t px, py, r = clamp((ost.p.sub * 160) >> 8, 0, 30000);   /* (as the engine places the moon: sub_at) */
    orbit_path(oph, oph, &px, &py);
    orbit_place_spin(&px, &py, spin);
    px = clamp(px + ((sinq(sph) * r) >> 15), -32767, 32767), py = clamp(py - ((cosq(sph) * r) >> 15), -32767, 32767);
    *x = FX0 * 16 + (((px + 32768) * (FW - 1)) >> 12), *y = FY0 * 16 + (((py + 32768) * (FW - 1)) >> 12);
}
static int tl_curve(void)                          /* the moon's place: the path's and the ring's (not an ADSR walk) */
{
    return !focus && !(sc.msrc != 255u && orbit_all_adsr()) && fam_q8(&ost.p) <= 3 * 256;
}
static void box_add(int16_t *b, int32_t x, int32_t y)
{
    b[0] = (int16_t)(x < b[0] ? x : b[0]), b[1] = (int16_t)(y < b[1] ? y : b[1]);
    b[2] = (int16_t)(x > b[2] ? x : b[2]), b[3] = (int16_t)(y > b[3] ? y : b[3]);
}
/* the way from (ax, ay) (phases ao, as at ta ms) back to (bx, by) (at tb): points no more than 2 pixels apart into
 * out (at most max), its box into b; returns how many, *len its length */
static uint32_t tl_fill(int32_t ax, int32_t ay, uint32_t ao, uint32_t as, uint16_t ta, int32_t bx, int32_t by, uint16_t tb,
                        int16_t (*out)[2], uint32_t max, uint32_t step0, int16_t *b, uint32_t *len)
{
    uint32_t oinc = freeze ? 0u : orbit_inc(), sinc = freeze ? 0u : SUB_RATE[pt.g[G_SUBSPD] & 127];
    uint32_t spi = freeze ? 0u : (uint32_t)spin_inc(), n = 0, at = 0, span = (uint16_t)(ta - tb) * 1378u, st;
    uint32_t back = (uint16_t)((uint16_t)fm1_ms - ta) * 1378u;   /* (SPIN: turned back from now, thousandths of a tick) */
    int32_t px = ax, py = ay, l = 0;
    b[0] = b[2] = (int16_t)ax, b[1] = b[3] = (int16_t)ay;
    box_add(b, bx, by);
    if ((bx > ax ? bx - ax : ax - bx) <= TL_SUB && (by > ay ? by - ay : ay - by) <= TL_SUB) {
        *len = 0;
        return 0;
    }
    st = span / 8u + 1u;
    while (at < span && n < max) {                 /* (to the older place itself) */
        int32_t nx, ny, d;
        for (;;) {                                 /* (no further than 2 pixels a step: halved; in thousandths of a tick:
                                                      a fast moon goes more than that in one) */
            uint64_t tk;
            st = st > span - at ? span - at : st;
            tk = at + st;
            tl_at(ao - (uint32_t)((uint64_t)oinc * tk / 1000u), as - (uint32_t)((uint64_t)sinc * tk / 1000u),
                  ost.spin - (uint32_t)((int64_t)(int32_t)spi * (int64_t)(tk + back) / 1000), &nx, &ny);
            d = (nx - px > 0 ? nx - px : px - nx) + (ny - py > 0 ? ny - py : py - ny);
            if (d <= 32 || st <= 16u)
                break;
            st >>= 1;
        }
        at += st;
        if (d < 12)
            st <<= 1;                              /* (closer than a pixel: longer steps) */
        if (at >= span)
            break;                                 /* (the older place itself: its own point) */
        out[n][0] = (int16_t)nx, out[n][1] = (int16_t)ny, n++;
        box_add(b, nx, ny);
        l += d, px = nx, py = ny;
    }
    (void)step0;
    *len = (uint32_t)l;
    return n;
}
static void mt_mark(uint32_t k)                    /* piece k (from place k to the newer one; 0: to the moon): its box */
{
    const int16_t *b;
    if (k >= mt_n || (!k && mt_hx < 0))
        return;
    b = k ? mt[mt_idx(k)].b : hb;
    rq_add(b[0] / 16 - 2, b[1] / 16 - 2, b[2] / 16 + 4, b[3] / 16 + 4);
}
static void mt_drop(void)                          /* the oldest place gone (its piece marked while it is there) */
{
    mt_mark(mt_n - 1u);
    if (mt[mt_idx(mt_n - 1u)].fill)
        mt_len -= mt[mt_idx(mt_n - 1u)].len;
    mt_n--;
}
static void head_fill(void)                        /* the piece to the moon now */
{
    uint32_t l;
    if (!mt_n || mt_hx < 0) {
        hn = 0, hfill = 0;
        return;
    }
    if (tl_curve())
        hn = tl_fill(mt_hx, mt_hy, sc.oph, sc.mph, (uint16_t)fm1_ms, mt[mt_idx(0)].x, mt[mt_idx(0)].y, mt[mt_idx(0)].t, hp,
                     HP_N, 0, hb, &l);
    else {
        hn = 0;
        hb[0] = hb[2] = mt_hx, hb[1] = hb[3] = mt_hy;
        box_add(hb, mt[mt_idx(0)].x, mt[mt_idx(0)].y);
    }
    hfill = (uint8_t)(hn != 0u);
}
static void piece_fill(void)                       /* a new place: the piece from it to the one before, complete */
{
    uint32_t e = mt_idx(1), l = 0, n = 0;
    static int16_t tmp[512][2];
    int16_t *b = mt[e].b;
    const int32_t ax = mt[mt_idx(0)].x, ay = mt[mt_idx(0)].y;
    if (tl_curve())
        n = tl_fill(ax, ay, mt[mt_idx(0)].oph, mt[mt_idx(0)].sph, mt[mt_idx(0)].t, mt[e].x, mt[e].y, mt[e].t, tmp, 512u, 0,
                    b, &l);
    else
        b[0] = b[2] = (int16_t)ax, b[1] = b[3] = (int16_t)ay, box_add(b, mt[e].x, mt[e].y);
    while (mt_n > 2u && mp_w + n - mt[mt_idx(mt_n - 1u)].pa > MP_N)
        mt_drop();                                 /* (no room for its points: the oldest give way) */
    mt[e].pa = mp_w, mt[e].np = (uint16_t)n, mt[e].len = (uint16_t)(l > 65535u ? 65535u : l), mt[e].fill = (uint8_t)(n != 0u);
    for (; n; n--, mp_w++)
        mp[mp_w % MP_N][0] = tmp[mt[e].np - n][0], mp[mp_w % MP_N][1] = tmp[mt[e].np - n][1];
    if (mt[e].fill)
        mt_len += mt[e].len;
    while (mt_n > 2u && mt[mt_idx(mt_n - 1u)].fill && mt_len - mt[mt_idx(mt_n - 1u)].len >= TL_LEN)
        mt_drop();                                 /* (a very fast moon: its way, but not without end) */
}
/* every point of the trail as drawn, newest first (the moon, the filled-in way, each place): the host tests */
static __attribute__((unused)) uint32_t trail_walk(int16_t (*out)[2], uint32_t max)
{
    uint32_t k, i, n = 0;
    if (mt_hx < 0 || !mt_n)
        return 0;
    out[n][0] = mt_hx, out[n][1] = mt_hy, n++;
    for (k = 0; k < mt_n && n < max; k++) {
        const uint32_t e = mt_idx(k), np = k ? mt[e].np : hn;
        for (i = 0; i < np && n < max; i++, n++) {
            const int16_t *q = k ? mp[(mt[e].pa + i) % MP_N] : hp[i];
            out[n][0] = q[0], out[n][1] = q[1];
        }
        if (n < max)
            out[n][0] = mt[e].x, out[n][1] = mt[e].y, n++;
    }
    return n;
}
static uint32_t trail_ms(void)                     /* one turn of the moon, ms; at least 0.5 s */
{
    uint32_t sr = SUB_RATE[pt.g[G_SUBSPD] & 127], t;
    if (!sr)
        return 60000u;
    t = (0xFFFFFFFFu / sr) / 1378u * 1000u;
    return t < 500u ? 500u : t > 60000u ? 60000u : t;
}
static void mh_take(void)
{
    uint32_t k, T = trail_ms();
    if (sc.mr <= 2 || sc.msrc != mt_src) {         /* (no ring, another moon: the line anew) */
        for (k = 0; k < mt_n; k++)
            mt_mark(k);
        mt_n = 0, mt_src = sc.msrc, mt_hx = -1, mt_len = 0, hn = 0;
        return;
    }
    mt_mark(0);                                    /* (the piece to the moon: where it was) */
    if (!mt_n || (MT_AGE(0) >= T / 1000u && MT_AGE(0) >= SAMPLE_MS)) {   /* (a new place: every ~30 ms, or every
                                                      turn / 1000) */
        uint32_t e;
        if (mt_n == MT_N)
            mt_drop();                             /* (the oldest gives way) */
        e = mt_w % MT_N;
        mt[e].x = (int16_t)sc.mx16, mt[e].y = (int16_t)sc.my16, mt[e].t = (uint16_t)fm1_ms;
        mt[e].lv = 6, mt[e].oph = sc.oph, mt[e].sph = sc.mph, mt[e].fill = 0, mt[e].np = 0, mt[e].len = 0, mt[e].pa = mp_w;
        mt_w++, mt_n++;
        if (mt_n > 1u)
            piece_fill();
        mt_mark(1);
    }
    mt_hx = (int16_t)sc.mx16, mt_hy = (int16_t)sc.my16;
    head_fill();
    mt_mark(0);                                    /* (the piece to the moon: where it is) */
    while (mt_n > 1u && MT_AGE(mt_n - 1u) > T)   /* (too old: gone) */
        mt_drop();
    for (k = 1; k < mt_n; k++) {                   /* each place's step, by its age */
        uint32_t age = MT_AGE(k);
        uint8_t lv = (uint8_t)(age >= T ? 1u : 1u + (T - age) * 6u / (T + 1u));
        if (lv != mt[mt_idx(k)].lv) {
            mt[mt_idx(k)].lv = lv;
            mt_mark(k), mt_mark(k + 1u);
        }
    }
}
static int32_t lit_k(uint32_t t)                   /* lit: 256 while it changes, fading over 0.4 s after */
{
    uint32_t age = fm1_ms - t;
    if (!t || age > 900u)
        return 0;
    return age < 500u ? 256 : (int32_t)(900u - age) * 256 / 400;
}
static int32_t moon_lit(void) { return lit_k(ui.moon_t); }         /* the moon's ring (the wheel, K5, K6) */
static int32_t path_lit(void) { return lit_k(ui.path_t) & ~63; }   /* the path (K1-K4 with no planet), in steps */
static void field_draw(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    static const uint8_t STARC[3] = {200, 215, 255}, GRID[3] = {20, 30, 70}, PATH[3] = {70, 110, 220};
    uint32_t i, c;
    int32_t k;
#define HIT(ax, ay, r) ((ax) + (r) >= x0 && (ax) - (r) < x1 && (ay) + (r) >= y0 && (ay) - (r) < y1)
    for (i = 0; i < NSTAR; i++) {                  /* the stars */
        int32_t x, y;
        star_xy(i, &x, &y);
        if (!HIT(x, y, 1))
            continue;
        k = star_k(i, sc.t);
        cv_add(x, y, STARC, k);
        if (k > 170) {                             /* (the bright ones: a little cross) */
            cv_add(x - 1, y, STARC, k / 4), cv_add(x + 1, y, STARC, k / 4);
            cv_add(x, y - 1, STARC, k / 4), cv_add(x, y + 1, STARC, k / 4);
        }
    }
    if (sc.om) {                                   /* the orbit in charge: a faint blue haze behind it (no square) */
        static const uint8_t HZ[3] = {40, 80, 200};
        int32_t x, y, R2 = 108 * 108, cx = FX0 + FW / 2, cy = FY0 + FW / 2, a = sc.om * 34 >> 8, vx0, vy0, vx1, vy1;
        cv_view(1, &vx0, &vy0, &vx1, &vy1);        /* (the strip's pixels within it: as cv_add would test them) */
        vx0 = vx0 > x0 ? vx0 : x0, vy0 = vy0 > y0 ? vy0 : y0, vx1 = vx1 < x1 ? vx1 : x1, vy1 = vy1 < y1 ? vy1 : y1;
        for (y = vy0; y < vy1; y++) {
            uint16_t *row = cv_buf + ((y + cv_oy) * (int32_t)cv_w + cv_ox);
            for (x = vx0; x < vx1; x++) {
                int32_t d = (x - cx) * (x - cx) + (y - cy) * (y - cy), v;
                if (d < R2 && (v = a * (R2 - d) / R2) > 0)
                    cv_add_at(row + x, HZ, v);
            }
        }
    }
    for (k = 0; k < FW; k += 2) {                  /* the centre cross, faint, dotted */
        if (k % 6 == 0)
            cv_add(FX0 + k, FY0 + FW / 2, GRID, 160), cv_add(FX0 + FW / 2, FY0 + k, GRID, 160);
    }
    for (c = 0; c < NC; c++) {                     /* the planets */
        int32_t x, y;
        corner_xy(c, &x, &y);
        if (HIT(x, y, 34))
            planet(c, x, y, pt.c[c][CP_MUTE] || (solo && !((solo >> c) & 1u)));
    }
    clip_x0 = SX0, clip_y0 = SY0, clip_x1 = SX1, clip_y1 = SY1;   /* (the orbit's things: the sky, not the square) */
    {
        int32_t pk = 150 + (sc.om * 70 >> 8) + path_lit() * 106 / 256, gk = sc.om * 36 >> 8;   /* (in charge: brighter, */
        const uint8_t *pc = path_lit() || sc.om > 128 ? (const uint8_t[]){120, 170, 255} : PATH;  /* a soft glow) */
        for (i = 0; i < sc.ntj; i++) {             /* the path: each point spread over its four pixels */
            int32_t ax = sc.tx[i] >> 4, ay = sc.ty[i] >> 4, fx = sc.tx[i] & 15, fy = sc.ty[i] & 15;
            if (!(i & 31u)) {                      /* (32 points away from the strip: all skipped) */
                const int16_t *b = sc.tjb[i / 32u];
                if (b[2] + 2 < x0 || b[0] - 2 >= x1 || b[3] + 2 < y0 || b[1] - 2 >= y1) {
                    i += 31u;
                    continue;
                }
            }
            if (!HIT(ax, ay, 2))
                continue;
            cv_add(ax, ay, pc, (16 - fx) * (16 - fy) * pk >> 8);
            cv_add(ax + 1, ay, pc, fx * (16 - fy) * pk >> 8);
            cv_add(ax, ay + 1, pc, (16 - fx) * fy * pk >> 8);
            cv_add(ax + 1, ay + 1, pc, fx * fy * pk >> 8);
            if (gk && !(i & 1u))
                cv_add(ax - 1, ay, pc, gk), cv_add(ax + 2, ay, pc, gk), cv_add(ax, ay - 1, pc, gk), cv_add(ax, ay + 2, pc, gk);
        }
    }
    if (sc.mr > 2 && HIT(sc.sx, sc.sy, sc.mr + 2)) {   /* the moon's ring: lit while the mod wheel sets the moon */
        int32_t lit = moon_lit(), n = lit ? 128 : 64;
        for (k = 0; k < n; k++) {
            uint32_t a = (uint32_t)k * (0xFFFFFFFFu / (uint32_t)n);
            int32_t rx = sc.sx + ((sinq(a) * sc.mr) >> 15), ry = sc.sy - ((cosq(a) * sc.mr) >> 15);
            cv_add(rx, ry, MOON, 70 + lit * 186 / 256);
            if (lit) {
                cv_add(rx + 1, ry, MOON, lit / 3), cv_add(rx - 1, ry, MOON, lit / 3);
                cv_add(rx, ry + 1, MOON, lit / 3), cv_add(rx, ry - 1, MOON, lit / 3);
            }
        }
    }
    for (i = 0; i < mt_n && mt_hx >= 0; i++) {     /* the moon's trail: the way it went, fading behind it; each piece:
                                                      from the newer place through its filled-in way to this one */
        const int16_t *b = i ? mt[mt_idx(i)].b : hb;
        int32_t ax0 = i ? mt[mt_idx(i - 1u)].x : mt_hx, ay0 = i ? mt[mt_idx(i - 1u)].y : mt_hy, k = MT_K[i ? mt[mt_idx(i)].lv % 7u : 6u];
        uint32_t v, np = i ? mt[mt_idx(i)].np : hn, pa = i ? mt[mt_idx(i)].pa : 0u;
        if (b[0] / 16 - 2 >= x1 || b[2] / 16 + 3 <= x0 || b[1] / 16 - 2 >= y1 || b[3] / 16 + 3 <= y0)
            continue;                              /* (the piece away from the strip) */
        for (v = 0; v <= np; v++) {
            int32_t bx = v < np ? (i ? mp[(pa + v) % MP_N][0] : hp[v][0]) : mt[mt_idx(i)].x;
            int32_t by = v < np ? (i ? mp[(pa + v) % MP_N][1] : hp[v][1]) : mt[mt_idx(i)].y;
            int32_t dx = bx - ax0, dy = by - ay0, n, j;
            int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            if (adx > 640 || ady > 640 ||          /* (a jump: no line) */
                (ax0 < bx ? ax0 : bx) / 16 - 2 >= x1 || (ax0 > bx ? ax0 : bx) / 16 + 3 <= x0 ||
                (ay0 < by ? ay0 : by) / 16 - 2 >= y1 || (ay0 > by ? ay0 : by) / 16 + 3 <= y0) {
                ax0 = bx, ay0 = by;
                continue;
            }
            n = (adx > ady ? adx : ady) / 12 + 1;
            for (j = 0; j < n; j++) {              /* (soft: each step over its four pixels; two pixels wide) */
                int32_t px = ax0 + dx * j / n, py = ay0 + dy * j / n, w;
                for (w = 0; w < 2; w++) {
                    int32_t qx = px + (w && ady > adx ? 16 : 0), qy = py + (w && ady <= adx ? 16 : 0), kk = w ? k / 2 : k;
                    int32_t fx = qx & 15, fy = qy & 15, ax = qx >> 4, ay = qy >> 4;
                    cv_add(ax, ay, MOON, (16 - fx) * (16 - fy) * kk >> 8);
                    cv_add(ax + 1, ay, MOON, fx * (16 - fy) * kk >> 8);
                    cv_add(ax, ay + 1, MOON, (16 - fx) * fy * kk >> 8);
                    cv_add(ax + 1, ay + 1, MOON, fx * fy * kk >> 8);
                }
            }
            sc.shown |= SH_TRAIL;
            ax0 = bx, ay0 = by;
        }
    }
    for (i = 0; i < sc.nmoon; i++) {               /* the other voices */
        if (HIT(sc.ox[i], sc.oy[i], 4))
            glow(sc.ox[i], sc.oy[i], 4, SUN, 110);
        if (HIT(sc.gx[i], sc.gy[i], 3)) {
            disc(sc.gx[i], sc.gy[i], 1, rgbk(MOON, 170));
        }
    }
    for (i = sc.ntp ? 0u : sc.ntr; i-- > 1u;) {    /* the tail, oldest first (sampled) */
        int32_t kk = 200 - (int32_t)i * 200 / TRAIL_N;
        if (HIT(sc.trx[i], sc.try_[i], 3))
            glow(sc.trx[i], sc.try_[i], 3 - (int32_t)i * 2 / TRAIL_N, SUN, kk);
    }
    for (i = sc.ntp; i-- > 0u;) {                  /* the trace: the path behind the sun, fading; soft, wider near it */
        static const uint8_t TC[3] = {150, 200, 255};
        int32_t ax = sc.tpx[i] >> 4, ay = sc.tpy[i] >> 4, fx = sc.tpx[i] & 15, fy = sc.tpy[i] & 15, kk = sc.tpk[i];
        if (!HIT(ax, ay, 2))
            continue;
        cv_add(ax, ay, TC, (16 - fx) * (16 - fy) * kk >> 8);
        cv_add(ax + 1, ay, TC, fx * (16 - fy) * kk >> 8);
        cv_add(ax, ay + 1, TC, (16 - fx) * fy * kk >> 8);
        cv_add(ax + 1, ay + 1, TC, fx * fy * kk >> 8);
        if (kk > 110)
            kk = (kk - 110) / 3, cv_add(ax - 1, ay, SUN, kk), cv_add(ax + 2, ay, SUN, kk), cv_add(ax, ay - 1, SUN, kk),
            cv_add(ax, ay + 2, SUN, kk), cv_add(ax + 1, ay - 1, SUN, kk), cv_add(ax + 1, ay + 2, SUN, kk);
    }
    if (HIT(sc.sx, sc.sy, 10)) {                   /* the sun */
        glow(sc.sx, sc.sy, 10, SUN, 230 + (sc.om * 26 >> 8));
        glow(sc.sx, sc.sy, 5, (const uint8_t[]){200, 225, 255}, sc.om * 120 >> 8);
        disc(sc.sx, sc.sy, 2 + (sc.om > 128), RGB(225, 240, 255));
    }
    if (HIT(sc.mx, sc.my, 6)) {                    /* the moon: bright, a halo */
        glow(sc.mx, sc.my, 6, MOON, 220);
        disc(sc.mx, sc.my, 3, rgbk(MOON, 256));
        disc(sc.mx - 1, sc.my - 1, 1, RGB(235, 255, 240));
    }
    clip_x0 = clip_y0 = -32768, clip_x1 = clip_y1 = 32767;
    if (sc.ov || sc.kb)
        bubble_draw(x0, y0, x1, y1);
    if (ui.aud.on || ui.aud.rel) {                 /* AUDITION: the speaker beside the planet */
        int32_t ax, ay;
        spk_xy(ui.aud.corner & 3u, &ax, &ay);
        if (HIT(ax + 5, ay + 6, 8)) {
            speaker(ax, ay, rgbk(CCOL[ui.aud.corner & 3u], ui.aud.on ? 256 : 120));
            sc.shown |= SH_SPEAKER;
        }
    }
    if (sc.cbox)
        central_draw(x0, y0, x1, y1);
#undef HIT
}

/* the field's rectangle (screen coordinates, clipped to it) drawn and sent, in strips the canvas holds */
/* What the screen shows, per 8 x 8 tile of the sky: a hash of its pixels (0: not known). A drawn strip sends only its
 * tiles that differ from the screen: per row of tiles the span from the first to the last changed one, the spans
 * moved together in the canvas (rows of the same span: one transfer). The look is the same; the SPI carries less. */
static uint32_t field_th[NTILE * 30u] __attribute__((section(".pool")));
static uint32_t fstat_sent;                        /* (the host tests: the pixels the strips sent) */
static void field_forget(void)                     /* the screen was drawn over: every tile is sent again */
{
    uint32_t i;
    for (i = 0; i < NTILE * 30u; i++)
        field_th[i] = 0;
}
static void field_send(int32_t x0, int32_t y0)     /* the canvas (a strip at x0 y0, tile aligned): its changed tiles */
{
    uint32_t w = cv_w, h = cv_h, ntx = w / TILE, tx0 = (uint32_t)(x0 - SX0) / TILE, r, o = 0, go = 0, gy = 0, gh = 0;
    int32_t ga = -1, gb = -1;
    for (r = 0; r < h; r += TILE) {                /* each row of tiles */
        uint32_t bh = h - r < TILE ? h - r : TILE, ty = (uint32_t)(y0 - SY0) / TILE + r / TILE, t, j, i;
        int32_t a = -1, b = -1;
        for (t = 0; t < ntx; t++) {
            typedef uint32_t __attribute__((may_alias)) word_t;
            uint32_t hs = 0x811C9DC5u + bh, *th = &field_th[ty * 30u + tx0 + t];
            for (j = 0; j < bh; j++) {             /* (each step one to one: a word changed always shows; mixed down) */
                const word_t *p = (const word_t *)(const void *)(cv_buf + (r + j) * w + t * TILE);
                for (i = 0; i < 4u; i++)
                    hs = (hs ^ p[i]) * 0x9E3779B1u, hs ^= hs >> 15;
            }
            hs += !hs;
            if (*th != hs) {
                *th = hs;
                if (a < 0)
                    a = (int32_t)t;
                b = (int32_t)t;
            }
        }
        if (a < 0 || a != ga || b != gb || gy + gh != r) {   /* (a new span: the last group goes out) */
            if (gh)
                lcd_blit((uint32_t)(x0 + ga * TILE), (uint32_t)(y0 + (int32_t)gy), (uint32_t)(gb - ga + 1) * TILE, gh, cv_buf + go);
            gh = 0, ga = a, gb = b, gy = r, go = o;
        }
        if (a < 0)
            continue;
        {
            uint32_t sw = (uint32_t)(b - a + 1) * TILE;   /* (moved to o: never past where it is read from) */
            for (j = 0; j < bh; j++, o += sw) {
                const uint16_t *src = cv_buf + (r + j) * w + (uint32_t)a * TILE;
                uint16_t *dst = cv_buf + o;
                if (dst != src)
                    for (i = 0; i < sw; i++)
                        dst[i] = src[i];
            }
            gh += bh;
            fstat_sent += sw * bh;
        }
    }
    if (gh)
        lcd_blit((uint32_t)(x0 + ga * TILE), (uint32_t)(y0 + (int32_t)gy), (uint32_t)(gb - ga + 1) * TILE, gh, cv_buf + go);
}
static void field_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t w, h, y;
    x0 = clamp(x0, SX0, SX1), x1 = clamp(x1, SX0, SX1);
    y0 = clamp(y0, SY0, SY1), y1 = clamp(y1, SY0, SY1);
    w = x1 - x0;
    if (w <= 0 || y1 <= y0)
        return;
    h = (int32_t)(CV_HALF / (uint32_t)w);
    if (h >= TILE)
        h &= ~(TILE - 1);                          /* (strips of whole tile rows) */
    for (y = y0; y < y1; y += h) {
        int32_t hh = y1 - y < h ? y1 - y : h;
        cv_begin2((uint32_t)w, (uint32_t)hh, C_BG);
        cv_ox = -x0, cv_oy = -y;
        field_draw(x0, y, x1, y + hh);
        cv_ox = cv_oy = 0;
        if ((((x0 - SX0) | (y - SY0) | w | (h & (TILE - 1))) & (TILE - 1))) {   /* (not on the tiles: all of it) */
            int32_t tx, ty;
            for (ty = (y - SY0) / TILE; ty <= (y + hh - 1 - SY0) / TILE; ty++)
                for (tx = (x0 - SX0) / TILE; tx <= (x1 - 1 - SX0) / TILE; tx++)
                    field_th[ty * 30 + tx] = 0;
            cv_blit((uint32_t)x0, (uint32_t)y);
            fstat_sent += (uint32_t)(w * hh);
        } else
            field_send(x0, y);
    }
}

/* the sun's trace: the path itself from the sun (at phase ph) back as far as it went in TP_MS, at most TP_LEN along
 * it; a point every 1 .. 2 pixels (the step in phase halved or doubled to keep them so), so it is smooth at any
 * speed. Where the path is not the place (an ADSR walk, the focus): the sampled tail. The tiles where it was and is */
static void trace_take(uint32_t ph, int adsr)
{
    uint32_t i, n = 0, back = 0, span, step;
    int32_t len = 0, px, py;
    for (i = 0; i < sc.ntp; i++)                   /* (where it was) */
        rq_add((sc.tpx[i] >> 4) - 2, (sc.tpy[i] >> 4) - 2, (sc.tpx[i] >> 4) + 4, (sc.tpy[i] >> 4) + 4);
    sc.ntp = 0;
    if (adsr || focus || fam_q8(&ost.p) > 3 * 256)
        return;
    {
        uint64_t sp = (uint64_t)orbit_inc() * (TP_MS * FS / CTL / 1000u);
        span = sp > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sp;
    }
    if (span < (1u << 16))
        return;
    step = span / 64u;
    traj_at(ph, &px, &py);
    sc.tpx[n] = (int16_t)px, sc.tpy[n] = (int16_t)py, n++;
    while (n < TP_N && back < span && len < TP_LEN) {
        int32_t nx, ny, d;
        uint32_t st = step;
        for (;;) {                                 /* (no further than 2 pixels: halved) */
            st = st > span - back ? span - back : st;
            traj_at(ph - back - st, &nx, &ny);
            d = (nx - px) * (nx - px) + (ny - py) * (ny - py);
            if (d <= 32 * 32 || st <= 256u)
                break;
            st >>= 1;
        }
        back += st, step = st;
        if (d < 16 * 16 && step < (1u << 28))
            step <<= 1;                            /* (closer than a pixel: longer steps) */
        len += isqrt32((uint32_t)d);
        sc.tpx[n] = (int16_t)nx, sc.tpy[n] = (int16_t)ny, n++;
        px = nx, py = ny;
    }
    {   /* bright at the sun, dark at the end: by the way along it (of the longer of its length, half the cap) */
        int32_t full = len > TP_LEN / 2 ? len : TP_LEN / 2, at = 0;
        for (i = 0; i < n; i++) {
            if (i)
                at += isqrt32((uint32_t)((sc.tpx[i] - sc.tpx[i - 1u]) * (sc.tpx[i] - sc.tpx[i - 1u]) +
                                         (sc.tpy[i] - sc.tpy[i - 1u]) * (sc.tpy[i] - sc.tpy[i - 1u])));
            sc.tpk[i] = (uint8_t)clamp(230 * (full - at) / full, 0, 255);
            rq_add((sc.tpx[i] >> 4) - 2, (sc.tpy[i] >> 4) - 2, (sc.tpx[i] >> 4) + 4, (sc.tpy[i] >> 4) + 4);
        }
    }
    sc.ntp = (uint8_t)n;
}

/* this frame's orbs from the engine: the newest voice sounding is the sun and moon, the others small */
static void orbs_take(void)
{
    uint32_t v, n = syn.newest % NVOICE, c;
    int32_t r = clamp((ost.p.sub * 160) >> 8, 0, 30000);
    const voice_t *s = vs[n].stage ? &vs[n] : 0;
    int32_t sx = fpx(s ? s->ox : syn.ox), sy = fpy(s ? s->oy : syn.oy);
    if (sx != sc.sx || sy != sc.sy || !sc.ntr) {   /* the tail: a place every ~30 ms (between: the newest moves) */
        static uint32_t t_tail;
        uint32_t i;
        if (fm1_ms - t_tail >= SAMPLE_MS || !sc.ntr) {
            t_tail = fm1_ms;
            for (i = TRAIL_N - 1u; i > 0u; i--)
                sc.trx[i] = sc.trx[i - 1u], sc.try_[i] = sc.try_[i - 1u];
            if (sc.ntr < TRAIL_N)
                sc.ntr++;
        }
        sc.trx[0] = (int16_t)sx, sc.try_[0] = (int16_t)sy;
    }
    sc.sx = (int16_t)sx, sc.sy = (int16_t)sy;
    trace_take(s ? s->oph - (freeze ? 0u : orbit_inc()) : syn.oph, s && orbit_all_adsr());
    sc.mx = (int16_t)fpx(s ? s->gx : syn.gx), sc.my = (int16_t)fpy(s ? s->gy : syn.gy);
    sc.mx16 = FX0 * 16 + ((((s ? s->gx : syn.gx) + 32768) * (FW - 1)) >> 12);
    sc.my16 = FY0 * 16 + ((((s ? s->gy : syn.gy) + 32768) * (FW - 1)) >> 12);
    sc.sx16 = FX0 * 16 + ((((s ? s->ox : syn.ox) + 32768) * (FW - 1)) >> 12);
    sc.sy16 = FY0 * 16 + ((((s ? s->oy : syn.oy) + 32768) * (FW - 1)) >> 12);
    sc.mph = s ? s->sph : syn.sph, sc.msrc = (uint8_t)(s ? n : 255u);
    sc.oph = s ? s->oph - (freeze ? 0u : orbit_inc()) : syn.oph;
    sc.mr16 = (r * (FW - 1)) >> 12;
    sc.mr = (int16_t)(focus ? 0 : (r * (FW - 1)) >> 16);
    sc.nmoon = 0;
    for (v = 0; v < NVOICE; v++)
        if (v != n && vs[v].stage && sc.nmoon < MOONS) {
            sc.ox[sc.nmoon] = (int16_t)fpx(vs[v].ox), sc.oy[sc.nmoon] = (int16_t)fpy(vs[v].oy);
            sc.gx[sc.nmoon] = (int16_t)fpx(vs[v].gx), sc.gy[sc.nmoon] = (int16_t)fpy(vs[v].gy);
            sc.nmoon++;
        }
    for (c = 0; c < NC; c++)
        sc.w[c] = syn.w[c];
}

/* what the last touch shows on the field: a planet's bubble (its knob, its pad), the central box (a held button's
 * knobs, a fader), the touched planets pulsing */
static void notes_take(void)
{
    uint32_t kind = (int32_t)(ui.touch.until - fm1_ms) > 0 ? ui.touch.kind : TK_NONE, c;
    sc.fader = 0, sc.pulse = 0, sc.ov = 0, sc.kb = 0, sc.gk = 0, sc.cbox = CB_NONE, sc.ovm = 0;
    if (kind == TK_CKNOB && ui.touch.layer == L_MAIN && ui.layer == L_MAIN && ui.sel)   /* a planet's knob: the bubble */
        sc.kb = (uint8_t)(ui.touch.k + 1u), sc.ovc = (uint8_t)sel_first(), sc.ovm = ui.sel;
    if (kind == TK_FADER)
        sc.fader = (uint8_t)(ui.touch.k + 1u);
    if (kind == TK_PAD && ui.touch.grp == PG_MUTE)  /* MUTE: no bubble, the planet shows it */
        kind = TK_NONE;
    if (kind == TK_CKNOB || kind == TK_PAD)
        sc.pulse = ui.touch.cmask;
    if (kind == TK_PAD && ui.touch.grp == PG_SELECT) {   /* the selected planets' overview (none: nothing) */
        if (ui.sel)
            sc.ov = PG_SELECT + 1u, sc.ovc = (uint8_t)sel_first(), sc.ovm = ui.sel;
    } else if (kind == TK_PAD) {                   /* a pad's list, its planet */
        sc.ov = (uint8_t)(ui.touch.grp + 1u);
        for (c = 0; c < NC; c++)
            if ((ui.touch.cmask >> c) & 1u)
                sc.ovc = (uint8_t)c;
        sc.ovm = (uint8_t)(1u << sc.ovc);
    }
    if (ui.layer == L_WAVE && ui.sel && !ui.help) {   /* the WAVE set: the selected planet's bubble, not the box */
        sc.ov = OV_WAVE, sc.ovc = (uint8_t)sel_first(), sc.ovm = (uint8_t)(1u << sc.ovc), sc.kb = 0;
        if (kind == TK_CKNOB && ui.touch.layer == L_WAVE)
            sc.gk = (uint8_t)(ui.touch.k + 1u);
    } else if (LAYER_HELD(ui.layer) && !ui.help) {   /* a button held: its knobs in the central box */
        sc.cbox = CB_LAYER;
        if ((kind == TK_GKNOB || kind == TK_CKNOB) && ui.touch.layer == ui.layer)
            sc.gk = (uint8_t)(ui.touch.k + 1u);
    } else if (ui.layer == L_ORBIT && kind == TK_GKNOB && ui.touch.layer == L_ORBIT)   /* no planet: the orbit's */
        sc.cbox = CB_LAYER, sc.gk = (uint8_t)(ui.touch.k + 1u);
    if (kind == TK_WHEEL)
        sc.cbox = CB_MOON;
    if (sc.fader)
        sc.cbox = sc.fader == 1u ? CB_FAM : CB_FADER;
}
/* the field: what moved since the last frame (and a few stars) drawn again; all of it when much moved */
static uint32_t fstat_full, fstat_n, fstat_area;    /* (the host tests: how much is drawn) */
static void field_frame(int full)
{
    int forget = full;                             /* (drawn anew over whatever the screen shows) */
    static uint32_t ov_drawn;
    static uint8_t prev_pulse;
    static int32_t drawn_w[NC], tw;
    static uint8_t drawn_sel, drawn_mute;
    uint32_t i, c, nspr = 0, area = 0, ty;
    rect_t spr[MOONS + 2 + TRAIL_N];
    uint8_t mute = 0;
    sc.t = fm1_ms;
    for (ty = 0; ty < NTILE; ty++)
        tiles[ty] = 0;
    orbs_take();
    traj_update();
    notes_take();
    mh_take();
    {   /* the orbit in charge (no planet selected): bright, glides in ~0.2 s; each step of it: the sky anew */
        static uint32_t t_om;
        static int32_t om_drawn = -1;
        int32_t to = !ui.sel && !LAYER_HELD(ui.layer) ? 256 : 0, st = (int32_t)(fm1_ms - t_om) * 256 / 200;
        t_om = fm1_ms;
        sc.om += clamp(to - sc.om, -st, st);
        if ((sc.om >> 6) != om_drawn)
            full = 1, om_drawn = sc.om >> 6;
    }
    {
        static int32_t pl_drawn;
        if (path_lit() != pl_drawn)                /* (the path lit or fading: drawn again where it is) */
            traj_mark(), pl_drawn = path_lit();
    }
    {   /* the overlays: drawn again when they change (or come, or go) */
        static uint32_t cb_drawn, aud_drawn, aud_c;
        static int32_t bb[4], cbr[4];
        uint32_t h = 0, hc = 0, au = ui.aud.on ? 2u : ui.aud.rel ? 1u : 0u, k;
        static uint32_t hs_drawn;
        uint32_t hs = 0;
        if (sc.ov || sc.kb) {                      /* the bubble: its shape (kind, planets); its content (their values) */
            hs = sc.ov * 7u + (sc.kb ? 13u : 0u) + sc.ovc * 131u + sc.ovm * 1031u + ui.sel * 3u + 1u;
            h = hs * 17u + sc.kb;
            for (c = 0; c < NC; c++)
                if ((sc.ovm >> c) & 1u) {
                    for (k = 0; k < CP_N; k++)
                        h = h * 31u + (uint32_t)(uint16_t)pt.c[c][k];
                    for (k = 0; k < 2u; k++)       /* (a wavetable read now: drawn again every half frame it moves) */
                        if (PAT_IS_WT((uint32_t)pt.c[c][k ? CP_G2 : CP_G1] % PAT_COUNT))
                            h = h * 31u + (uint32_t)(wt_live(c, k) >> 15);
                }
            h = h * 7u + sc.gk;
        }
        if (sc.cbox) {                             /* the central box: its kind, its values */
            hc = sc.cbox * 7u + ui.layer * 131u + sc.gk * 17u + sc.fader * 1000u + ui.sel * 5u + 1u;
            if (sc.cbox == CB_LAYER)
                for (k = 0; k < 8u; k++)
                    hc = hc * 33u + (uint32_t)(uint16_t)kd_get(&LAYERS[ui.layer].k[k]);
            else if (sc.cbox == CB_MOON)
                hc = hc * 41u + (uint32_t)pt.g[G_SUBSPD] * 3u + (uint32_t)(uint16_t)ui.mw_last * 7u + ui.mw_mode;
            else
                hc = hc * 37u + (uint32_t)(uint16_t)pt.g[FADER_G[sc.fader - 1u]] * 3u + ui.fad_last[sc.fader - 1u] * 7u +
                     ui.fad_mode[sc.fader - 1u] + (sc.cbox == CB_FAM ? (uint32_t)fam_snap_k() / 32u * 1009u : 0u);
        }
        if (au != aud_drawn || (au && aud_c != ui.aud.corner)) {   /* the speaker: where it was, where it is */
            int32_t ax, ay;
            spk_xy(aud_c & 3u, &ax, &ay);
            rq_add(ax - 1, ay - 1, ax + 12, ay + 15);
            spk_xy(ui.aud.corner & 3u, &ax, &ay);
            rq_add(ax - 1, ay - 1, ax + 12, ay + 15);
            aud_drawn = au, aud_c = ui.aud.corner;
            sc.shown &= ~SH_SPEAKER;
        }
        if (hs != hs_drawn) {                      /* the bubble (its box, its tails, its planets), where it was and is */
            int32_t bx, by, px, py, w, hh;
            rq_add(bb[0], bb[1], bb[2], bb[3]);
            sc.shown &= ~(SH_OVERVIEW | SH_BIG | SH_LIST | SH_MULTI | SH_PERPLANET), sc.big = 0, sc.tails = 0;
            if (hs) {
                bubble_place(&bx, &by, &w, &hh);
                bb[0] = bx, bb[1] = by, bb[2] = bx + w, bb[3] = by + hh;
                for (c = 0; c < NC; c++)
                    if ((sc.ovm >> c) & 1u) {
                        corner_xy(c, &px, &py);
                        bb[0] = px - 26 < bb[0] ? px - 26 : bb[0], bb[1] = py - 26 < bb[1] ? py - 26 : bb[1];
                        bb[2] = px + 27 > bb[2] ? px + 27 : bb[2], bb[3] = py + 27 > bb[3] ? py + 27 : bb[3];
                    }
                rq_add(bb[0], bb[1], bb[2], bb[3]);
            } else
                bb[0] = bb[1] = bb[2] = bb[3] = 0;
        } else if (h != ov_drawn) {                /* (only its values: the box again; the planets redraw themselves) */
            int32_t bx, by, w, hh;
            bubble_place(&bx, &by, &w, &hh);
            rq_add(bx, by, bx + w, by + hh);
            sc.shown &= ~(SH_BIG | SH_PERPLANET);
        }
        hs_drawn = hs;
        if (hc != cb_drawn) {                      /* the central box, where it was and is */
            int32_t x, y, w, hh;
            rq_add(cbr[0], cbr[1], cbr[2], cbr[3]);
            sc.shown &= ~(SH_CBOX | SH_FAMLIST);
            if (hc) {
                cbox_rect(sc.cbox, &x, &y, &w, &hh);
                cbr[0] = x, cbr[1] = y, cbr[2] = x + w, cbr[3] = y + hh;
                rq_add(x, y, x + w, y + hh);
            } else
                cbr[0] = cbr[1] = cbr[2] = cbr[3] = 0;
        }
        ov_drawn = h, cb_drawn = hc;
    }
    {   /* the sprites: sun + tail + ring + moon; each other voice */
        int32_t x0 = sc.sx - 11, y0 = sc.sy - 11, x1 = sc.sx + 12, y1 = sc.sy + 12;
#define GROW(ax, ay, r) x0 = ax - (r) < x0 ? ax - (r) : x0, y0 = ay - (r) < y0 ? ay - (r) : y0, \
                        x1 = ax + (r) + 1 > x1 ? ax + (r) + 1 : x1, y1 = ay + (r) + 1 > y1 ? ay + (r) + 1 : y1
        for (i = 1; !sc.ntp && i < sc.ntr; i++) {  /* (the tail: a small box each, not one round them all) */
            spr[nspr].x0 = (int16_t)clamp(sc.trx[i] - 4, SX0, SX1), spr[nspr].y0 = (int16_t)clamp(sc.try_[i] - 4, SY0, SY1);
            spr[nspr].x1 = (int16_t)clamp(sc.trx[i] + 5, SX0, SX1), spr[nspr].y1 = (int16_t)clamp(sc.try_[i] + 5, SY0, SY1);
            nspr++;
        }
        GROW(sc.mx, sc.my, 7);
        if (sc.mr > 2)
            GROW(sc.sx, sc.sy, sc.mr + 2);
        spr[nspr].x0 = (int16_t)clamp(x0, SX0, SX1), spr[nspr].y0 = (int16_t)clamp(y0, SY0, SY1);   /* (in the sky) */
        spr[nspr].x1 = (int16_t)clamp(x1, SX0, SX1), spr[nspr].y1 = (int16_t)clamp(y1, SY0, SY1);
        nspr++;
        for (i = 0; i < sc.nmoon; i++) {
            x0 = sc.ox[i] - 5, y0 = sc.oy[i] - 5, x1 = sc.ox[i] + 6, y1 = sc.oy[i] + 6;
            GROW(sc.gx[i], sc.gy[i], 3);
            spr[nspr].x0 = (int16_t)clamp(x0, SX0, SX1), spr[nspr].y0 = (int16_t)clamp(y0, SY0, SY1);   /* (in the sky) */
        spr[nspr].x1 = (int16_t)clamp(x1, SX0, SX1), spr[nspr].y1 = (int16_t)clamp(y1, SY0, SY1);
            nspr++;
        }
#undef GROW
    }
    for (i = 0; i < nprev; i++)
        rq_add(prev_spr[i].x0, prev_spr[i].y0, prev_spr[i].x1, prev_spr[i].y1);
    for (i = 0; i < nspr; i++) {
        rq_add(spr[i].x0, spr[i].y0, spr[i].x1, spr[i].y1);
        prev_spr[i] = spr[i];
    }
    nprev = nspr;
    for (c = 0; c < NC; c++)
        mute |= (uint8_t)((pt.c[c][CP_MUTE] || (solo && !((solo >> c) & 1u))) << c);
    for (c = 0; c < NC; c++) {                     /* a planet whose glow (or state) changed, zooming, turning, pulsing */
        static uint32_t t_turn[NC];
        int32_t d = sc.w[c] - drawn_w[c], zt = (ui.sel >> c) & 1u ? 256 : 0, zoomed = sc.zoom[c] != zt;
        int32_t turn = sc.zoom[c] && fm1_ms - t_turn[c] >= 125u;  /* (the selected one turns: ~8 frames a second) */
        int32_t anim = ui.mute_t[c] && fm1_ms - ui.mute_t[c] < MUTE_ANIM_MS + 60u;   /* (switched on / off) */
        sc.zoom[c] += clamp(zt - sc.zoom[c], -48 * (int32_t)field_dt / 30, 48 * (int32_t)field_dt / 30);   /* (~0.16 s) */
        if (turn)
            t_turn[c] = fm1_ms;
        if (d > 1600 || d < -1600 || ((drawn_sel ^ ui.sel) >> c & 1u) || ((drawn_mute ^ mute) >> c & 1u) ||
            ((sc.pulse | prev_pulse) >> c & 1u) || zoomed || turn || sc.zoom[c] || anim) {
            int32_t x, y, r;
            if (!zoomed && !turn && !anim && sc.zoom[c] && !(d > 1600 || d < -1600) && !((sc.pulse | prev_pulse) >> c & 1u) &&
                !((drawn_sel ^ ui.sel) >> c & 1u) && !((drawn_mute ^ mute) >> c & 1u) && sc.kb == 0u)
                continue;                          /* (selected, nothing new: wait for the next turn) */
            r = anim ? (sc.zoom[c] || zoomed ? 34 : 26) : !sc.zoom[c] && !zoomed ? 16 :   /* (turning, glowing: the body, */
                zoomed || ((sc.pulse | prev_pulse | (drawn_sel ^ ui.sel) | (drawn_mute ^ mute)) >> c & 1u) || sc.kb ? 34 : 27;   /* rings) */
            corner_xy(c, &x, &y);
            rq_add(x - r, y - r, x + r + 1, y + r + 1);
            drawn_w[c] = sc.pw[c] = sc.w[c];
        }
    }
    drawn_sel = ui.sel, drawn_mute = mute, prev_pulse = sc.pulse;
    static uint32_t tw_acc;
    tw_acc += 5u * field_dt;
    for (i = tw_acc / 30u, tw_acc %= 30u; i > 0u; i--) {   /* a few stars twinkle (5 a 30 ms) */
        int32_t x, y;
        star_xy((uint32_t)(tw++ % NSTAR), &x, &y);
        rq_add(x - 1, y - 1, x + 2, y + 2);
    }
    for (ty = 0; ty < NTILE; ty++)
        for (c = tiles[ty]; c; c &= c - 1u)
            area += TILE * TILE;
    fstat_area += area, fstat_n++;
    if (forget)
        field_forget();
    if (full || area > (SX1 - SX0) * (SY1 - SY0) * 7u / 10u) {
        fstat_full++;
        field_rect(SX0, SY0, SX1, SY1);
        return;
    }
    for (ty = 0; ty < NTILE; ty++) {               /* a row's runs of marked tiles, each carried down the rows */
        uint32_t tx = 0;                           /* below that have it too: one rectangle each */
        while (tiles[ty]) {
            uint32_t t1, run, ty1 = ty + 1u;
            while (!((tiles[ty] >> tx) & 1u))
                tx++;
            for (t1 = tx; t1 < 32u && (tiles[ty] >> t1) & 1u; t1++)
                ;
            run = (t1 >= 32u ? 0xFFFFFFFFu : (1u << t1) - 1u) & ~((1u << tx) - 1u);
            while (ty1 < NTILE && (tiles[ty1] & run) == run)
                tiles[ty1++] &= ~run;
            tiles[ty] &= ~run;
            field_rect(SX0 + (int32_t)tx * TILE, SY0 + (int32_t)ty * TILE, SX0 + (int32_t)t1 * TILE, SY0 + (int32_t)ty1 * TILE);
            tx = t1;
        }
    }
}

/* ------------------------------------------------------------- a path's picture --- */
static void shape_icon(int32_t x, int32_t y, int32_t w, int32_t h, int32_t fam, int32_t wx, int32_t wy, int32_t mo, uint16_t col)
{                                                  /* fam: FAMILY, 0 .. 124 */
    opar_t o = {wx * 256, wy * 256, mo * 256, 0, 0, 0, 0, 64 * 256, 0, fam * 256};
    int32_t k, px = 0, py = 0;
    for (k = 0; k <= 64; k++) {
        int32_t sx, sy, nx, ny;
        uint32_t ph = (uint32_t)k * (0xFFFFFFFFu / 64u);
        path_xy(&o, ph, ph, &sx, &sy);
        nx = x + (((sx + 32768) * (w - 1)) >> 16), ny = y + (((sy + 32768) * (h - 1)) >> 16);
        if (k)
            cv_line(px, py, nx, ny, col);
        px = nx, py = ny;
    }
}

/* ------------------------------------------------------------- the top line --- */
static int32_t peak_db(int32_t p)                  /* dBFS of a Q15 peak (rough), -60 .. 0 */
{
    int32_t d = 0;
    if (p < 33)
        return -60;
    while (p < 16384)
        p <<= 1, d -= 6;
    return d - (32767 - p) * 6 / 16384;
}
static void level_meter(int32_t x, int32_t y)      /* the output level (into the canvas) */
{
    static int32_t shown;
    int32_t p = syn.mpeak, w = 40, k, l;
    syn.mpeak = 0;
    shown = p > shown ? p : shown - (shown >> 3);
    l = w * (60 + peak_db(shown)) / 60;
    for (k = 0; k < w; k += 3)
        cv_rect(x + k, y, 2, 6, k < l ? (k > w - 7 ? RGB(255, 60, 60) : k > w - 13 ? RGB(255, 200, 60) : RGB(80, 200, 255))
                                      : RGB(16, 20, 40));
}
#if BOARD_SMK37
static const char *const BTN_NAME[FM1_NBTN] = {"KNOB BANK", "FADER BANK", "<", ">", "OCT-", "OCT+", "ARP", "REPEAT",
                                               "SCALE", "CHORD", "GLOBE", "BT", "PATCH", "PARA", "FX", "SEQ", "PLAY", "REC"};
#else
static const char *const BTN_NAME[FM1_NBTN] = {"OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                               "ARP", "SEQ", "PLAY", "REC"};
#endif
/* what the control touched does now; else the sound, the voice mode and the level */
static void top_line(int force)
{
    static uint32_t drawn;
    char b[40];
    uint32_t h = 3, i, idle = 0;
    uint16_t col = RGB(255, 255, 255);
    if (ui.help)
        str_cpy(b, "GUIDE  K1 SCROLLS", sizeof b), col = RGB(120, 170, 255);
    else if (ui.list && (int32_t)(ui.msg_until - fm1_ms) <= 0)
        str_cpy(b, ui.lbank == BK_USER ? "USER  < > K1 BANK  K2 NEXT" : ui.lbank == BK_F2 ? "FACTORY 2 < > K1 BANK K2 NEXT"
                   : "FACTORY  < > K1 BANK  K2 NEXT", sizeof b),
            col = ui.lbank == BK_USER ? RGB(80, 200, 255) : ui.lbank == BK_F2 ? RGB(255, 120, 200) : RGB(255, 200, 90);
    else if (LAYER_HELD(ui.layer)) {
        b[0] = 0;
        if (LAYERS[ui.layer].btn < FM1_NBTN) {     /* (the FM-1's WAVE set: page 4, no button) */
            str_cat(b, BTN_NAME[LAYERS[ui.layer].btn], sizeof b);
            str_cat(b, ": ", sizeof b);
        }
        str_cat(b, LAYERS[ui.layer].name, sizeof b);
        str_cat(b, " KNOBS", sizeof b);
        col = RGB(150, 190, 255);
    } else if ((int32_t)(ui.msg_until - fm1_ms) > 0)
        str_cpy(b, ui.msg, sizeof b);
    else {
        idle = 1;
        preset_label(b, sizeof b);                 /* (saved: marked *) */
        col = RGB(210, 220, 255);
    }
    for (i = 0; b[i]; i++)
        h = h * 31u + (uint8_t)b[i];
    h = h * 7u + (uint32_t)pt.g[G_POLY] * 131u + (uint32_t)pt.g[G_ARP] * 1031u + idle;
    if (!force && h == drawn) {
        if (idle) {                                /* (the meter alone) */
            cv_begin(42, 8, C_BG);
            level_meter(0, 1);
            cv_blit(196, 5);
        }
        return;
    }
    drawn = h;
    cv_begin(240, 18, C_BG);
    cv_text(4, 1, &FONT_S, b, col);
    if (idle) {
        static const char *const PS[PM_N] = {"POLY", "DUAL", "MONO"};
        const char *m = pt.g[G_ARP] ? "ARP" : PS[pt.g[G_POLY] % PM_N];
        cv_text(192 - text_w(&FONT_S, m), 1, &FONT_S, m, RGB(120, 140, 200));
        level_meter(196, 6);
    }
    cv_rect(0, 17, 240, 1, RGB(20, 28, 60));
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- the bottom strip --- */
/* the path and the moon at a glance, always: six small pictures with bars (F1 FAMILY: the family's figure; F2 SIZE: a
 * circle as large; F3 SPEED: an arrow, longer when faster; F4 MORPH: the figure now; WARP: a circle bent by it; MOON:
 * a moon on its ring, as wide as its distance, the bar its speed). Faders and the wheel: the bar is the value, the
 * thin tick the control's place; while a value glides the bar runs to the tick */
static void path_line(int force)
{
    static uint32_t drawn;
    static const uint8_t PC[3] = {110, 170, 255};
    uint32_t k, h = 17;
    for (k = 0; k < 4u; k++)
        h = h * 37u + (uint32_t)(uint16_t)pt.g[FADER_G[k]] * 3u + ui.fad_last[k] * 7u + ui.fad_mode[k];
    h = h * 31u + (uint32_t)(uint16_t)pt.g[G_WARPX] + (uint32_t)(uint16_t)pt.g[G_WARPY] * 5u + ui.fad_ready;
    h = h * 29u + (uint32_t)pt.g[G_SUBSZ] * 3u + (uint32_t)pt.g[G_SUBSPD] * 7u + (uint32_t)(uint16_t)ui.mw_last * 11u + ui.mw_mode;
    if (!force && h == drawn)
        return;
    drawn = h;
    cv_begin(240, 20, C_BG);
    cv_rect(0, 0, 240, 1, RGB(20, 28, 60));
    for (k = 0; k < 6u; k++) {
        int32_t x = (int32_t)k * 40 + 1, ix = x + 8, iy = 10, bx = x + 20, bw = 18, v, t = -1, j;
        uint16_t col = rgbk(PC, 256), bar = RGB(80, 150, 255);
        cv_rect(x, 2, 17, 17, RGB(6, 8, 26));
        if (k < 4u) {
            const pdesc_t *d = &GDESC[FADER_G[k]];
            v = (pt.g[FADER_G[k]] - d->min) * bw / (d->max - d->min);
            if (ui.fad_ready)
                t = ui.fad_last[k] * (bw - 1) / 127;
            if (ui.fad_mode[k] != 2u)
                bar = RGB(60, 110, 200);
        } else if (k == 4u)
            v = (pt.g[G_WARPX] + 64) * bw / 127;
        else {
            v = pt.g[G_SUBSPD] * bw / 127, bar = rgbk(MOON, 220);
            if (ui.mw_last >= 0)
                t = ui.mw_last * (bw - 1) / 127;
        }
        switch (k) {
        case 0: {                                  /* the family (its own figure) */
            int32_t f = clamp((pt.g[G_SHAPE] + FAM_STEP / 2) / FAM_STEP, 0, SH_N - 1);
            shape_icon(x + 1, 3, 15, 15, pt.g[G_SHAPE], FAM_PIC[f][0], FAM_PIC[f][1], FAM_PIC[f][2], col);
            break;
        }
        case 1: {                                  /* the size: a circle */
            int32_t r = 1 + pt.g[G_SIZE] * 6 / 127;
            for (j = 0; j < 32; j++) {
                uint32_t a = (uint32_t)j << 27;
                cv_pset(ix + ((sinq(a) * r) >> 15), iy - ((cosq(a) * r) >> 15), col);
            }
            break;
        }
        case 2: {                                  /* the speed: an arrow */
            int32_t l = 3 + pt.g[G_RATE] * 10 / 8, x0 = x + 2;
            cv_rect(x0, iy, l, 1, col);
            cv_line(x0 + l, iy, x0 + l - 3, iy - 3, col), cv_line(x0 + l, iy, x0 + l - 3, iy + 3, col);
            break;
        }
        case 3:                                    /* the morph: the path's figure now */
            shape_icon(x + 1, 3, 15, 15, pt.g[G_SHAPE], pt.g[G_WARPX], pt.g[G_WARPY], pt.g[G_MORPH], col);
            break;
        case 4:                                    /* the warp: a circle it bends (a Lissajous 1:1, its phase) */
            shape_icon(x + 1, 3, 15, 15, 0, pt.g[G_WARPX], -64, -64, col);
            break;
        default: {                                 /* the moon on its ring: as wide as its distance */
            int32_t r = 2 + pt.g[G_SUBSZ] * 5 / 127;
            for (j = 0; j < 24; j++) {
                uint32_t a = (uint32_t)j * 178956971u;
                cv_add(ix + ((sinq(a) * r) >> 15), iy - ((cosq(a) * r) >> 15), MOON, 120);
            }
            disc(ix, iy, 1, rgbk(SUN, 256));
            disc(ix + ((sinq(0x60000000u) * r) >> 15), iy - ((cosq(0x60000000u) * r) >> 15), 1, rgbk(MOON, 256));
        }
        }
        cv_rect(bx, 8, bw, 5, RGB(16, 22, 50));
        cv_rect(bx, 8, v, 5, bar);
        if (t >= 0)
            cv_rect(bx + t, 5, 1, 11, RGB(255, 255, 255));
    }
    cv_blit(0, SY1);
}

/* ------------------------------------------------------------- the guide (KNOB BANK; K1 scrolls) --- */
static void man_head(int32_t y, const char *s) { cv_text(6, y, &FONT_S, s, RGB(255, 200, 90)); cv_rect(6, y + 16, 228, 1, RGB(60, 50, 30)); }
static void man_text(int32_t y, const char *s) { cv_text(6, y, &FONT_S, s, RGB(200, 205, 230)); }
static void man_planet(int32_t x, int32_t y, uint32_t c, int32_t r)
{
    char d[2] = {(char)('1' + c), 0};
    glow(x, y, r + 5, CCOL[c], 120);
    disc(x, y, r, rgbk(CCOL[c], 220));
    cv_text(x - 3, y - 7, &FONT_S, d, RGB(10, 10, 20));
}
static void man_orbit(int32_t x, int32_t y, int32_t s)   /* a small orbit: the square, planets, path, sun, moon */
{
    int32_t k;
    uint32_t c;
    for (k = 0; k < s; k += 2) {
        cv_add(x + k, y, (const uint8_t[]){30, 40, 90}, 256), cv_add(x + k, y + s - 1, (const uint8_t[]){30, 40, 90}, 256);
        cv_add(x, y + k, (const uint8_t[]){30, 40, 90}, 256), cv_add(x + s - 1, y + k, (const uint8_t[]){30, 40, 90}, 256);
    }
    for (k = 0; k < 64; k++) {
        uint32_t a = (uint32_t)k << 26;
        cv_add(x + s / 2 + ((sinq(a) * (s * 2 / 5)) >> 15), y + s / 2 - ((cosq(a) * (s * 2 / 5)) >> 15), (const uint8_t[]){70, 110, 220}, 160);
    }
    for (c = 0; c < NC; c++)
        man_planet(c & 1u ? x + s - 12 : x + 11, c & 2u ? y + s - 12 : y + 11, c, 7);
    {
        int32_t sx = x + s / 2 + s * 2 / 5 * 7 / 10, sy = y + s / 2 - s * 2 / 5 * 7 / 10;
        for (k = 0; k < 40; k++) {
            uint32_t a = (uint32_t)k * 107374183u;
            cv_add(sx + ((sinq(a) * 11) >> 15), sy + ((cosq(a) * 11) >> 15), MOON, 90);
        }
        glow(sx, sy, 8, SUN, 240);
        disc(sx, sy, 2, RGB(225, 240, 255));
        disc(sx + 8, sy - 7, 2, rgbk(MOON, 256));
    }
}
static void manual_content(void)
{
    static const char *const SN[6] = {"LISSAJOUS", "SPIRO", "STAR", "PULSE", "ADSR", "SPIRO>STAR"};
    static const int8_t SP[6][4] = {{0, 0, -64, -39}, {31, 0, 30, -14}, {62, 30, 30, -14},
                                    {93, 0, -40, 0}, {124, 30, -30, 0}, {46, 30, 30, -14}};   /* FAMILY, WARP, WARP Y, MORPH */
    static const char *const GTXT[4] = {"PICK; AGAIN: OUT", "NEXT MODULATION", "NEXT FILTER TYPE", "PLANET ON / OFF"};
    int32_t i, k;
    text_c(120, 0, &FONT_L, "GUIDE", RGB(120, 170, 255));
    text_c(120, 36, &FONT_S, "K1 SCROLLS  KNOB BANK CLOSES", RGB(110, 120, 160));
    man_head(64, "THE ORBIT");
    man_orbit(70, 88, 100);
    man_text(196, "THE MOON'S PLACE MIXES THE");
    man_text(211, "FOUR CORNERS: NEAR A PLANET");
    man_text(226, "YOU HEAR THAT CORNER'S SOUND.");
    man_text(241, "SUN: THE PATH (THE FADERS)");
    man_text(256, "MOON: ITS SPEED = THE MOD WHEEL");
    man_head(284, "THE PANEL");
    cv_rect(6, 306, 228, 1, RGB(50, 60, 100)), cv_rect(6, 392, 228, 1, RGB(50, 60, 100));
    cv_rect(10, 312, 40, 40, RGB(10, 14, 40));
    cv_text(14, 324, &FONT_S, "LCD", RGB(90, 110, 170));
    for (i = 0; i < 8; i++) {                      /* knobs over pads, the pads in their groups' colours */
        int32_t x = 66 + i * 21;
        char d[2] = {(char)('1' + i), 0};
        disc(x, 318, 7, rgbk(CCOL[0], 200));
        cv_text(x - 3, 328, &FONT_S, d, RGB(150, 155, 190));
        for (k = 0; k < 2; k++)
            cv_rect(x - 8, 348 + k * 18, 16, 14, rgbk(GCOL[i >> 1], 220));
    }
    for (i = 0; i < 4; i++)                        /* the 2 x 2 groups outlined */
        cv_rect(56 + i * 42, 386, 38, 2, rgbk(GCOL[i], 256));
    man_text(398, "KNOBS: THE SELECTED PLANETS");
    man_text(413, " NONE SELECTED: ORBIT + MOON");
    man_text(428, "FADERS: THE PATH");
    man_text(443, " FAMILY SIZE SPEED MORPH");
    man_text(458, "WHEELS: BEND, MOON SPEED");
    man_head(486, "PADS: FOUR GROUPS OF 2X2");
    for (i = 0; i < 4; i++) {                      /* one group = the four corners */
        char d[2] = {(char)('1' + i), 0};
        int32_t x = 30 + (i & 1) * 30, y = 514 + (i >> 1) * 30;
        cv_rect(x, y, 26, 26, rgbk(GCOL[0], 220));
        cv_text(x + 9, y + 5, &FONT_S, d, RGB(10, 10, 20));
    }
    text_c(118, 530, &FONT_L, "=", RGB(200, 205, 230));
    man_orbit(146, 510, 64);
    for (i = 0; i < 4; i++) {
        int32_t y = 586 + i * 18;
        cv_rect(6, y + 2, 12, 12, rgbk(GCOL[i], 256));
        cv_text(24, y, &FONT_S, GROUP_NAME[i], rgbk(GCOL[i], 256));
        cv_text(84, y, &FONT_S, GTXT[i], RGB(200, 205, 230));
    }
    man_text(662, "EACH PRESS ONE STEP; THE LIST");
    man_text(677, "SHOWS OVER THE ORBIT.");
    man_head(704, "A CORNER");
    cv_rect(8, 728, 52, 18, RGB(20, 30, 70)), cv_text(12, 729, &FONT_S, "GEN 1", RGB(200, 205, 230));
    cv_rect(8, 756, 52, 18, RGB(20, 30, 70)), cv_text(12, 757, &FONT_S, "GEN 2", RGB(200, 205, 230));
    cv_line(60, 737, 88, 750, RGB(90, 110, 170)), cv_line(60, 765, 88, 752, RGB(90, 110, 170));
    cv_rect(88, 742, 58, 18, RGB(60, 40, 10)), cv_text(94, 743, &FONT_S, "BLEND", rgbk(GCOL[PG_MODE], 256));
    cv_line(146, 751, 164, 751, RGB(90, 110, 170));
    cv_rect(164, 742, 66, 18, RGB(40, 14, 70)), cv_text(170, 743, &FONT_S, "FILTER", rgbk(GCOL[PG_FILTER], 256));
    man_text(784, "GEN: A-C EVEN, D-F ODD");
    man_text(799, "OVERTONES, N NOISE. C+D: SAW");
    man_text(814, "BLEND: <ADD | G1 | MODULATE>");
    man_text(829, "MODULATED: AS MODE SAYS");
    man_head(856, "FADER 1: THE FAMILIES");
    for (i = 0; i < 6; i++) {                      /* the families (and one in between) */
        int32_t x = 12 + (i % 3) * 76, y = 880 + (i / 3) * 66;
        cv_rect(x, y, 64, 44, RGB(6, 8, 26));
        shape_icon(x + 4, y + 4, 56, 36, (uint32_t)SP[i][0], SP[i][1], SP[i][2], SP[i][3], RGB(90, 150, 255));
        cv_text(x, y + 46, &FONT_S, SN[i], RGB(150, 160, 200));
    }
    man_head(1018, "BUTTONS");
    {
        static const char *const B[] = {"< > SOUND    OCT- + OCTAVE", "SEQ FREEZE", "CHORD: VOICE KNOBS (TAP)",
                                        "FX: MIXER KNOBS (TAP)", "FADER BANK: MORE KNOBS", "ARP: ARP KNOBS (TAP)",
                                        "GLOBE FOCUS 1-4 CENTRE OFF", "PARA RANDOM, HELD: UNDO", "REC LATCH   PLAY TAP BPM",
                                        "REPEAT RESTART", "F3 SPEED: THE SUN, IN BEATS", "MOD WHEEL: THE MOON'S SPEED", "PATCH SOUNDS  TWICE: ALL OFF"};
        for (i = 0; i < (int32_t)(sizeof B / sizeof B[0]); i++)
            man_text(1040 + i * 15, B[i]);
    }
}
static void draw_manual(void)                      /* the guide, from ui.mscroll, under the top line */
{
    uint32_t s;
    for (s = 0; s < 2u; s++) {
        cv_begin(240, 111, C_BG);
        cv_oy = -(ui.mscroll + (int32_t)s * 111);
        manual_content();
        cv_oy = 0;
        cv_blit(0, 18u + s * 111u);
    }
}

/* ------------------------------------------------------------- the sound list --- */
/* PATCH: the bank shown (FACTORY, USER, FACTORY 2), its sixteen slots as the pads lie: the left column the top row (1-8), the
 * right column the bottom row (9-16); the sound now marked, a pad held lit, empty USER slots dim */
static uint32_t list_hash(void)
{
    uint32_t h = 0x811C9DC5u, i;
    h = (h ^ ui.lbank) * 16777619u, h = (h ^ ui.bank) * 16777619u, h = (h ^ ui.preset) * 16777619u;
    h = (h ^ usr.h.mask) * 16777619u, h = (h ^ ui.lpad) * 16777619u;
    for (i = 0; i < USR_SLOTS; i++)
        h = (h ^ usr.src[i]) * 16777619u;
    return h;
}
static void list_content(void)
{
    uint32_t i, user = ui.lbank == BK_USER, first = bank_first(ui.lbank);
    uint16_t bc = user ? RGB(80, 200, 255) : ui.lbank == BK_F2 ? RGB(255, 120, 200) : RGB(255, 200, 90);
    text_c(120, 0, &FONT_L, user ? "USER" : ui.lbank == BK_F2 ? "FACTORY 2" : "FACTORY", bc);
    cv_text(4, 34, &FONT_S, "TOP PADS", RGB(110, 120, 160));
    cv_text(124, 34, &FONT_S, "BOTTOM PADS", RGB(110, 120, 160));
    for (i = 0; i < NPRESET; i++) {
        char nb[4];
        int32_t x = i < 8u ? 0 : 120, y = 52 + (int32_t)(i % 8u) * 19;
        int cur = ui.lbank == ui.bank && i == ui.preset, held = (ui.lpad >> i) & 1u, empty = user && !usr_has(i);
        const char *name = empty ? "EMPTY" : PRESET_SHORT[user ? usr.src[i] % NFACT : first + i];
        if (cur || held)
            cv_rect(x + 1, y - 1, 118, 18, held ? RGB(70, 40, 20) : RGB(30, 40, 80));
        fmt_int(nb, first + i + 1);
        cv_text(x + 2 + (first + i + 1 < 10u ? 8 : 0), y, &FONT_S, nb, cur ? C_WHITE : RGB(110, 120, 160));   /* (right-aligned) */
        cv_text(x + 26, y, &FONT_S, name, cur ? C_WHITE : empty ? RGB(70, 75, 100) : RGB(200, 205, 230));
    }
    text_c(120, 206, &FONT_S, "TAP: PLAY  HOLD: SAVE IN USER", RGB(170, 175, 200));
}
static void draw_list(void)
{
    uint32_t s;
    for (s = 0; s < 2u; s++) {
        cv_begin(240, 111, C_BG);
        cv_oy = -((int32_t)s * 111);
        list_content();
        cv_oy = 0;
        cv_blit(0, 18u + s * 111u);
    }
}

/* ------------------------------------------------------------- the frame --- */
static uint8_t boot_handover;                      /* (the boot's last frame on the screen: below) */
static void draw_frame(void)
{
    static uint32_t t_field;
    static uint8_t shown_help = 0xFF;
    static int16_t shown_scroll = -1;
    static uint32_t shown_list;
    uint32_t now = fm1_ms, over = ui.help ? 1u : ui.list ? 2u : 0u;   /* the guide, the sound list or the orbit */
    int force = 0;
    if (over != shown_help) {                      /* in or out of the guide or the list: all anew */
        if (shown_help != 0xFFu || !boot_handover) /* (after the boot: drawn over its last frame, no blank) */
            lcd_fill(0, 0, 240, 240, C_BG);
        shown_help = (uint8_t)over;
        shown_scroll = -1;
        shown_list = 0;
        force = 1;
    }
    top_line(force);
    if (over == 2u) {
        uint32_t h = list_hash() | 1u;
        if (h != shown_list) {
            shown_list = h;
            draw_list();
        }
        ui.dirty = 0;
        return;
    }
    if (ui.help) {
        if (ui.mscroll != shown_scroll) {
            shown_scroll = ui.mscroll;
            draw_manual();
        }
        return;
    }
    if (force || now - t_field >= UI_FIELD_MS) {   /* the field: up to ~60 frames a second */
        field_dt = force ? 30u : (uint32_t)clamp((int32_t)(now - t_field), 1, 60);   /* (anew: as a frame of old) */
        t_field = now;
        field_frame(force);
    }
    path_line(force);
    ui.dirty = 0;
}

/* ------------------------------------------------------------- boot: a solar system --- */
/* we fly on into the stars (streaming out from the middle, brighter the nearer); a solar system ahead, seen a little
 * from above (its orbits ellipses, y x 0.6), coming nearer: the sun, four orbits, a planet on each (as on the orbit
 * screen, twice as large; the far side smaller, behind the sun) going round, most of a turn; the name; then the
 * planets go out to their corners and the sky stays: the orbit screen. Smooth: every place from the time (ms), and
 * only the 8 x 8 tiles that changed go to the screen (their hashes against the last frame's), so the frames come
 * fast and even; fades in steps (a fading band otherwise sends the whole band every frame). */
#define BOOT_MS 3000u
#define BOOT_NAME 1100u                            /* (the name: from here) */
#define BOOT_OUT 2400u                             /* (the hand-over: from here to the end) */
#define BOOT_TILT 154                              /* (/256: the orbits' height against their width) */
static uint32_t boot_th[30u * 30u];                /* each tile's hash as on the screen (0: unknown, sent) */
static uint8_t boot_handover;                      /* the boot's last frame is on the screen: the orbit screen's
                                                      first frame draws over it (no blank between) */
static void boot_push(uint32_t y0)                 /* the canvas (240 x 120 at row y0): its changed tiles out */
{
    uint16_t *tmp = cv_px + 240u * 120u;           /* (the canvas' spare rows: 960 pixels, a run of 15 tiles) */
    uint32_t ty, tx, j, i;
    for (ty = 0; ty < 15u; ty++) {
        uint32_t *th = &boot_th[(y0 / 8u + ty) * 30u], ch = 0;
        for (tx = 0; tx < 30u; tx++) {
            const uint16_t *p = cv_px + ty * 8u * 240u + tx * 8u;
            uint32_t h = 2166136261u;
            for (j = 0; j < 8u; j++, p += 240)
                for (i = 0; i < 8u; i++)
                    h = (h ^ p[i]) * 16777619u;
            h |= 1u;
            if (h != th[tx])
                th[tx] = h, ch |= 1u << tx;
        }
        while (ch) {                               /* runs of changed tiles, 15 at most each */
            uint32_t t0 = 0, n = 0, w;
            while (!((ch >> t0) & 1u))
                t0++;
            while (t0 + n < 30u && n < 15u && ((ch >> (t0 + n)) & 1u))
                n++;
            ch &= ~(((1u << n) - 1u) << t0);
            w = n * 8u;
            lcd_sync();                            /* (the last run's transfer reads tmp) */
            for (j = 0; j < 8u; j++)
                for (i = 0; i < w; i++)
                    tmp[j * w + i] = cv_px[(ty * 8u + j) * 240u + t0 * 8u + i];
            lcd_blit(t0 * 8u, y0 + ty * 8u, w, 8u, tmp);
        }
    }
}
static int32_t ease(int32_t v) { return (v * v >> 8) * (3 * 256 - 2 * v) >> 8; }   /* (smoothstep, 0 .. 256) */
static void boot_frame(uint32_t t)
{
    static const int16_t ORB_R[NC] = {38, 60, 82, 104};      /* (the orbits, close up) */
    static const uint32_t TO[NC] = {0xE0000000u, 0x20000000u, 0xA0000000u, 0x60000000u};   /* (towards its corner) */
    static const uint16_t PERIOD[NC] = {2600, 3400, 4400, 5600};   /* (ms a round: the inner ones faster) */
    static const uint8_t STARC[3] = {200, 215, 255}, RING[3] = {70, 110, 220};
    int32_t half, k, c, pass, in = clamp((int32_t)t * 256 / 350, 0, 256);
    int32_t zt = clamp((int32_t)t * 256 / 1000, 0, 256), zoom, out, nk, sysk, ringk;
    int32_t px[NC], py[NC], pr[NC];
    zt = 256 - ((256 - zt) * (256 - zt) >> 8);     /* (nearer, slowing; still from 1 s on) */
    zoom = 150 + (106 * zt >> 8);                  /* 0.59 .. 1 */
    out = ease(clamp(((int32_t)t - (int32_t)BOOT_OUT) * 256 / (int32_t)(BOOT_MS - BOOT_OUT), 0, 256));
    nk = clamp(((int32_t)t - (int32_t)BOOT_NAME) * 256 / 400, 0, 256) *
         (256 - clamp(((int32_t)t - (int32_t)BOOT_OUT + 100) * 256 / 300, 0, 256)) >> 8;
    nk = (nk + 16) >> 5 << 5;                      /* (in steps of 1/8: the band is sent only when it steps) */
    sysk = in * (256 - (nk * 140 >> 8)) >> 8;      /* (the sun and the orbits dimmer under the name) */
    ringk = ((sysk * 3 / 5) * (256 - out) >> 8) >> 5 << 5;
    for (c = 0; c < NC; c++) {                     /* the planets: going round; at the end out to their corners */
        uint32_t a = TO[c] + (uint32_t)(((int32_t)t - (int32_t)BOOT_OUT) * (int32_t)(0xFFFFFFFFu / PERIOD[c]));
        int32_t r = ORB_R[c] * zoom >> 8, cs = cosq(a), cx, cy;
        int32_t x = 120 * 256 + ((sinq(a) * r) >> 7), y = 120 * 256 - ((((cs * r) >> 7) * BOOT_TILT) >> 8);   /* (1/256 px) */
        corner_xy((uint32_t)c, &cx, &cy);
        x += (cx * 256 - x) * out >> 8, y += (cy * 256 - y) * out >> 8;
        px[c] = (x + 128) >> 8, py[c] = (y + 128) >> 8;
        pr[c] = ((14 * zoom >> 8) * (256 - ((cs * 40 >> 15) * (256 - out) >> 8)) >> 8) - (7 * out >> 8);   /* (far: smaller) */
        pr[c] = pr[c] * in >> 8;                   /* (coming out of the dark with the sun) */
    }
    for (half = 0; half < 2; half++) {
        int32_t y0 = half * 120;
        cv_begin(240, 120, C_BG);
        cv_oy = -y0;
        for (k = 0; k < 80; k++) {                 /* the stars: out from the middle, a short streak, nearer brighter */
            uint32_t h = hash3(k, 11, 3u), a = h << 8;
            int32_t z = 1000 - (int32_t)(((h >> 20) % 1000u + t * 45u / 64u) % 970u), r0, r1, s, kk;
            r0 = 26000 / z, r1 = 26000 / (z + 14);
            if (r0 > 175 || r1 < 4)
                continue;
            r1 = r0 - r1 > 7 ? r0 - 7 : r1;
            kk = clamp(50 + 24000 / z, 0, 256) * (256 - out) >> 8;
            for (s = r1; s <= r0; s++)
                cv_add(120 + ((sinq(a) * s) >> 15), 120 + ((cosq(a) * s) >> 15), STARC, kk * (s - r1 + 1) / (r0 - r1 + 1));
        }
        for (k = 0; out && k < NSTAR; k++) {       /* (the orbit screen's sky, coming) */
            int32_t x, y;
            star_xy((uint32_t)k, &x, &y);
            if (y >= y0 && y < y0 + 120)
                cv_add(x, y, STARC, star_k((uint32_t)k, t) * (out >> 5 << 5) >> 8);
        }
        for (c = 0; c < NC && ringk > 0; c++) {    /* the orbits: ellipses */
            int32_t r = ORB_R[c] * zoom >> 8, ry = r * BOOT_TILT >> 8, n = r * 6;
            if (y0 > 120 + ry || y0 + 120 < 120 - ry)
                continue;
            for (k = 0; k < n; k++) {
                uint32_t a = (uint32_t)k * (0xFFFFFFFFu / (uint32_t)n);
                cv_add(120 + ((sinq(a) * r) >> 15), 120 - ((cosq(a) * ry) >> 15), RING, ringk);
            }
        }
        for (pass = 0; pass < 2; pass++) {         /* the far planets, the sun, the near planets */
            if (pass) {
                int32_t sk = (sysk * (256 - out) >> 8) >> 5 << 5;
                glow(120, 120, 20 * zoom >> 8, SUN, sk);
                glow(120, 120, 8 * zoom >> 8, (const uint8_t[]){200, 225, 255}, sk * 3 / 4);
                disc(120, 120, (4 * zoom >> 8) * (256 - out) >> 8, rgbk((const uint8_t[]){225, 240, 255}, sk));
            }
            for (c = 0; c < NC; c++) {
                if ((py[c] >= 120) != pass || pr[c] < 2 || py[c] + 30 < y0 || py[c] - 30 >= y0 + 120)
                    continue;
                planet_r0 = pr[c];
                sc.pw[c] = 0;
                planet((uint32_t)c, px[c], py[c], 0);
            }
        }
        planet_r0 = 0;
        if (nk) {                                  /* the name, on a band of shade */
            int32_t y, x, f = 256 - (nk * 170 >> 8);
            for (y = 84; y < 162; y++) {
                int32_t fy = y < 96 ? 256 - (256 - f) * (y - 83) / 12 : y >= 150 ? 256 - (256 - f) * (162 - y) / 12 : f, cy = y - y0;
                if (cy < 0 || cy >= 120)
                    continue;
                for (x = 0; x < 240; x++) {
                    uint32_t q = swap16(cv_px[cy * 240 + x]);
                    cv_px[cy * 240 + x] = swap16(RGB(((q >> 11) << 3) * fy >> 8, (((q >> 5) & 63u) << 2) * fy >> 8,
                                                      (uint32_t)(8 + (((int32_t)((q & 31u) << 3) - 8) * fy >> 8))));   /* (to the sky's blue) */
                }
            }
            text_c(120, 92, &FONT_XL, "VECTOR37", RGB(120 * nk >> 8, 170 * nk >> 8, 255 * nk >> 8));
            text_c(120, 142, &FONT_S, "by genkigenki", RGB(150 * nk >> 8, 160 * nk >> 8, 210 * nk >> 8));
        }
        cv_oy = 0;
        boot_push((uint32_t)y0);
    }
}

static void boot_anim(void)
{
    uint32_t t0 = fm1_ms;
    for (;;) {
        uint32_t t = fm1_ms - t0;
        fm1_wdt_feed();
        if (t > BOOT_MS || (PANEL_KEYS() && t > 300u))
            break;
        boot_frame(t);                             /* (as fast as it goes: the places follow the time) */
    }
    boot_frame(BOOT_MS);                           /* (the planets in their corners: the orbit screen takes over) */
    lcd_sync();
    boot_handover = 1;
}

static void splash(void)                           /* before the audio runs: the boot's first frame (stars only) */
{
    uint32_t i;
    for (i = 0; i < 30u * 30u; i++)
        boot_th[i] = 0;
    boot_frame(0);
    lcd_sync();
}
