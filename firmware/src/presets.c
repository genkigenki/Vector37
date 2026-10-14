/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The factory sounds (< >), one of each kind. The rule for every one: four corners that differ as much as possible
 * (dark and round / bright and buzzing / metallic PM / noisy or vowel; low-passed / high-passed / band-passed; an
 * octave or a twelfth below in one), a path of its own, as big as fits (SIZE 127: orbit_fit keeps it whole inside
 * the square) and reaching for the planets, and SHARP (the weighting's power) so that near a planet you hear that
 * corner: the orbit is heard and seen as motion.
 * tests/host.c measures it: how far the spectrum moves with the orbit, how far apart the corners are. */
#define NPRESET 8u
static const char *const PRESET_NAME[NPRESET] = {"NEBULA PAD", "ORBIT BASS", "GLASS BELLS", "TALKING",
                                                 "DEEP DRONE", "PLUCK STEP", "SYNC LEAD", "SPACE STATION"};

/* corner c: generators, blend, filter (cutoff, resonance), the blend's mode, the filter's type */
static void cn(patch_t *p, uint32_t c, int32_t g1, int32_t g2, int32_t blend, int32_t cut, int32_t res, int32_t mode,
               int32_t ft)
{
    int16_t *q = p->c[c];
    q[CP_G1] = (int16_t)g1, q[CP_G2] = (int16_t)g2, q[CP_BLEND] = (int16_t)blend;
    q[CP_CUT] = (int16_t)cut, q[CP_RES] = (int16_t)res, q[CP_BMODE] = (int16_t)mode, q[CP_FTYPE] = (int16_t)ft;
}
static void tune(patch_t *p, uint32_t c, int32_t t1, int32_t t2) { p->c[c][CP_T1] = (int16_t)t1, p->c[c][CP_T2] = (int16_t)t2; }
static void lvl(patch_t *p, int32_t l1, int32_t l2, int32_t l3, int32_t l4)   /* corner levels: the four alike loud */
{
    p->c[0][CP_LEVEL] = (int16_t)l1, p->c[1][CP_LEVEL] = (int16_t)l2, p->c[2][CP_LEVEL] = (int16_t)l3, p->c[3][CP_LEVEL] = (int16_t)l4;
}
static void amp(patch_t *p, int32_t a, int32_t d, int32_t s, int32_t r)
{
    p->g[G_AA] = (int16_t)a, p->g[G_AD] = (int16_t)d, p->g[G_AS] = (int16_t)s, p->g[G_AR] = (int16_t)r;
}
/* the path: shape, warp, rate (beats a round, index), size, rotation; the suborbit */
static void orb(patch_t *p, int32_t shape, int32_t wx, int32_t wy, int32_t morph, int32_t rate, int32_t size, int32_t rot,
                int32_t subsz, int32_t subspd)
{
    p->g[G_SHAPE] = (int16_t)shape, p->g[G_WARPX] = (int16_t)wx, p->g[G_WARPY] = (int16_t)wy, p->g[G_MORPH] = (int16_t)morph;
    p->g[G_RATE] = (int16_t)rate, p->g[G_SIZE] = (int16_t)size, p->g[G_ROT] = (int16_t)rot;
    p->g[G_SUBSZ] = (int16_t)subsz, p->g[G_SUBSPD] = (int16_t)subspd;
}
static void mixer(patch_t *p, int32_t rv, int32_t dl, int32_t ch)
{
    p->g[G_RVMIX] = (int16_t)rv, p->g[G_DLMIX] = (int16_t)dl, p->g[G_CHMIX] = (int16_t)ch;
}
enum { R32, R16, R8, R4, R2, R1, RH, RQ, RE };      /* RATE: beats a round, 32 .. 1/8 */
#define DIAG 16                                    /* ROTATE: 45 degrees */
enum { FAM_LISSA = 0, FAM_SPIRO = 31, FAM_STAR = 62, FAM_PULSE = 93, FAM_ADSR = 124 };   /* FAMILY */
/* the families' whole-number settings (one more every 25 knob steps): Lissajous x frequency (WARP Y) 1 2 3, y
 * frequency (MORPH) 1 2 3; star points (MORPH) 3 4 5 */
enum { LX1 = -64, LX2 = -39, LX3 = -14, LY1 = -64, LY2 = -39, LY3 = -14, ST3 = -64, ST4 = -39, ST5 = -14 };

static void preset_make(uint32_t n, patch_t *p)
{
    patch_init(p);
    switch (n % NPRESET) {
    case 0:   /* NEBULA PAD: a square path round all four: a dark sine-ish hum, a bright thin saw, a PM bell
               * an octave below, a breathing vowel */
        cn(p, 0, PAT_A, PAT_C, 0, 22, 10, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_D, -64, 50, 40, BM_AM, FT_HP);
        cn(p, 2, PAT_C, PAT_F, 50, 100, 10, BM_PM, FT_LP12);
        cn(p, 3, PAT_N, PAT_C, -44, 64, 120, BM_AM, FT_VOX);
        tune(p, 2, 0, 1);
        lvl(p, 127, 127, 95, 95);
        orb(p, FAM_LISSA, 0, LX1, LY1, R4, 127, 0, 26, 45);   /* a circle, as large as fits */
        p->g[G_SHARP] = 5;
        amp(p, 60, 80, 110, 90);
        mixer(p, 80, 20, 60);
        break;
    case 1:   /* ORBIT BASS: the path hops two corners each beat: a deep low-passed square, a nasal PD growl */
        cn(p, 0, PAT_D, PAT_A, -20, 24, 70, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_D, -64, 92, 30, BM_AM, FT_BP);
        cn(p, 2, PAT_C, PAT_D, 56, 60, 50, BM_PM, FT_LP12);
        cn(p, 3, PAT_D, PAT_C, 54, 84, 80, BM_PDAM, FT_LP24);
        tune(p, 2, 0, 1);
        lvl(p, 101, 127, 89, 113);
        orb(p, FAM_PULSE, 0, -40, 0, R1, 127, 0, 14, 70);   /* corner 1 to corner 4 and back */
        p->g[G_SHARP] = 3;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 2, p->g[G_DETUNE] = 16, p->g[G_GLIDE] = 24, p->g[G_STEREO] = 50;
        p->g[G_FOLVOL] = 100;
        amp(p, 0, 60, 90, 30);
        mixer(p, 16, 20, 0);
        p->g[G_DRIVE] = 34, p->g[G_TRIM] = 6;
        break;
    case 2:   /* GLASS BELLS: each note walks its own ADSR path: struck PM clang, glassy PM+AM, a pure tone, shimmer */
        cn(p, 0, PAT_F, PAT_C, 60, 44, 20, BM_PM, FT_HP);
        cn(p, 1, PAT_E, PAT_A, 50, 110, 20, BM_PMAM, FT_LP12);
        cn(p, 2, PAT_A, PAT_C, 0, 30, 0, BM_AM, FT_LP12);
        cn(p, 3, PAT_C, PAT_F, 44, 100, 30, BM_RPDAM, FT_BP);
        tune(p, 0, 0, 2), tune(p, 1, 0, 3);
        lvl(p, 127, 67, 75, 127);
        orb(p, FAM_ADSR, 30, -30, 0, R4, 127, 0, 12, 60);   /* each note: 1, 2, towards 4, then 3 */
        p->g[G_SHARP] = 4;
        p->g[G_OA] = 40, p->g[G_OD] = 70, p->g[G_OS] = 10, p->g[G_OR] = 80;
        p->g[G_RETRIG] = 1;
        amp(p, 0, 88, 30, 92);
        mixer(p, 90, 40, 30);
        p->g[G_RVSIZE] = 112, p->g[G_TRIM] = 10;
        break;
    case 3:   /* TALKING: one buzzy source through four vowels far apart; a figure eight says them */
        cn(p, 0, PAT_C, PAT_D, -64, 0, 127, BM_AM, FT_VOX);       /* U */
        cn(p, 1, PAT_C, PAT_D, -64, 96, 127, BM_AM, FT_VOX);      /* I */
        cn(p, 2, PAT_C, PAT_D, -64, 127, 127, BM_AM, FT_VOX);     /* A */
        cn(p, 3, PAT_C, PAT_D, -64, 32, 127, BM_AM, FT_VOX);      /* E */
        orb(p, FAM_LISSA, 0, LX1, LY2, R2, 127, 0, 20, 30);   /* a figure eight through the four vowels */
        p->g[G_SHARP] = 3;
        amp(p, 20, 80, 110, 60);
        mixer(p, 40, 10, 40);
        break;
    case 4:   /* DEEP DRONE: an octave below, a twelfth below, noise breath, a ring-modulated haze; slow and wide */
        cn(p, 0, PAT_A, PAT_C, 0, 30, 20, BM_AM, FT_LP24);
        cn(p, 1, PAT_D, PAT_F, -40, 90, 50, BM_AM, FT_LP12);
        cn(p, 2, PAT_N, PAT_A, -30, 90, 110, BM_AM, FT_BP);
        cn(p, 3, PAT_C, PAT_D, 63, 40, 30, BM_AM, FT_HP);
        tune(p, 0, 1, 1), tune(p, 1, 2, 2);
        lvl(p, 71, 71, 107, 127);
        orb(p, FAM_LISSA, 22, LX3, LY2, R8, 127, 0, 34, 20);   /* a slow 3:2 knot, its phase opening it */
        p->g[G_SHARP] = 3;
        amp(p, 100, 90, 120, 110);
        mixer(p, 100, 30, 50);
        p->g[G_RVSIZE] = 124, p->g[G_DRIVE] = 20, p->g[G_TRIM] = 8;
        break;
    case 5:   /* PLUCK STEP: hold a chord: the arpeggio runs; each note starts bright (retrigger) and darkens round */
        cn(p, 0, PAT_C, PAT_D, -64, 110, 40, BM_AM, FT_LP12);
        cn(p, 1, PAT_F, PAT_C, 50, 50, 20, BM_PM, FT_HP);
        cn(p, 2, PAT_D, PAT_A, 0, 20, 90, BM_AM, FT_LP24);
        cn(p, 3, PAT_B, PAT_E, -64, 60, 100, BM_AM, FT_BP);
        lvl(p, 80, 127, 90, 127);
        orb(p, FAM_STAR, 63, -64, ST3, RH, 127, DIAG, 16, 80);   /* a triangle, its tip at corner 2 */
        p->g[G_SHARP] = 3;
        p->g[G_RETRIG] = 1, p->g[G_ARP] = ARP_UP, p->g[G_TEMPO] = 118;
        amp(p, 0, 82, 0, 60);
        mixer(p, 50, 60, 20);
        p->g[G_DLTIME] = 40, p->g[G_DLFB] = 70, p->g[G_TRIM] = 8;
        break;
    case 6:   /* SYNC LEAD: a detuned pair, gliding; saw, PD sync-like rasp, bright FM, a hollow square */
        cn(p, 0, PAT_C, PAT_D, -64, 96, 30, BM_AM, FT_LP12);
        cn(p, 1, PAT_D, PAT_B, 60, 110, 20, BM_PDAM, FT_LP12);
        cn(p, 2, PAT_C, PAT_E, 63, 50, 10, BM_PM, FT_HP);
        cn(p, 3, PAT_D, PAT_A, 0, 40, 60, BM_AM, FT_LP24);
        tune(p, 2, 0, 1);
        orb(p, FAM_STAR, 40, 30, ST5, R2, 127, 0, 22, 92);   /* a five-point star */
        p->g[G_SHARP] = 3;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 2, p->g[G_DETUNE] = 24, p->g[G_GLIDE] = 50, p->g[G_FOLVOL] = 110;
        amp(p, 6, 70, 110, 50);
        mixer(p, 40, 40, 30);
        p->g[G_DRIVE] = 50, p->g[G_DCOL] = 80;
        break;
    default:  /* SPACE STATION: a whistle, ring-modulated noise, metal a twelfth down, hiss; a fast star */
        cn(p, 0, PAT_N, PAT_A, 0, 104, 120, BM_AM, FT_BP);
        cn(p, 1, PAT_F, PAT_N, 64, 80, 40, BM_AM, FT_LP12);
        cn(p, 2, PAT_E, PAT_F, 63, 70, 30, BM_PM, FT_LP24);
        cn(p, 3, PAT_N, PAT_C, -20, 70, 60, BM_AM, FT_HP);
        tune(p, 2, 0, 4);
        lvl(p, 127, 80, 80, 80);
        orb(p, FAM_STAR, 63, 40, ST4, RH, 127, DIAG, 34, 110);   /* a four-point star, its points at the planets */
        p->g[G_SHARP] = 4;
        amp(p, 40, 80, 110, 100);
        mixer(p, 100, 70, 40);
        p->g[G_DLFB] = 100, p->g[G_RVSIZE] = 120, p->g[G_TRIM] = 4;
        break;
    }
    patch_sanitize(p);
}
