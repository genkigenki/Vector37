/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The factory sounds (< >), one of each kind. The rule for every one: four corners that differ as much as possible
 * (dark and round / bright and buzzing / metallic PM / noisy or vowel; low-passed / high-passed / band-passed; an
 * octave or a twelfth below in one), a path of its own, as big as fits (SIZE 127: orbit_fit keeps it whole inside
 * the square) and reaching for the planets, and SHARP (the weighting's power) so that near a planet you hear that
 * corner: the orbit is heard and seen as motion.
 * tests/host.c measures it: how far the spectrum moves with the orbit, how far apart the corners are. */
#define NPRESET 16u                                /* sounds a bank (one a pad: the sound list) */
#define NFACT 32u                                  /* the factory sounds: FACTORY 1..16, FACTORY 2 17..32 */
/* FACTORY 2 (17..32): the basics, each a whole scene (its planets, path, moon, filters, effects; tempo and arpeggio
 * where it plays one): the top pads pads, strings, choir, organ and keys; the bottom pads basses, sequences, a
 * brass and a lead, wind, drums. Levels matched (tests/host.c: the loudest 0.4 s of each within a few dB) */
static const char *const PRESET_NAME[NFACT] = {"NEBULA PAD", "ORBIT BASS", "GLASS BELLS", "TALKING",
                                               "DEEP DRONE", "PLUCK STEP", "SYNC LEAD", "SPACE STATION",
                                               "WAVE ORBIT", "PPG GLASS", "SPIRO PAD", "ACID BASS",
                                               "KALIMBA", "STAR LEAD", "TIDE DRONE", "METAL PERC",
                                               "WARM STRINGS", "PWM PAD", "CHOIR AAH", "DRAWBAR ORGAN",
                                               "E.PIANO", "HARP", "MARIMBA", "MUSIC BOX",
                                               "SUB BASS", "FM BASS", "BASS SEQ", "SAW ARP",
                                               "BRASS SWELL", "FLUTE LEAD", "NIGHT WIND", "DRUM KIT"};
static const char *const PRESET_SHORT[NFACT] = {"NEBULA PAD", "ORBIT BASS", "GLASS BELLS", "TALKING",   /* (the sound
                                                list's columns: 11 letters at most) */
                                                "DEEP DRONE", "PLUCK STEP", "SYNC LEAD", "SPACE STN",
                                                "WAVE ORBIT", "PPG GLASS", "SPIRO PAD", "ACID BASS",
                                                "KALIMBA", "STAR LEAD", "TIDE DRONE", "METAL PERC",
                                                "STRINGS", "PWM PAD", "CHOIR AAH", "ORGAN",
                                                "E.PIANO", "HARP", "MARIMBA", "MUSIC BOX",
                                                "SUB BASS", "FM BASS", "BASS SEQ", "SAW ARP",
                                                "BRASS", "FLUTE LEAD", "NIGHT WIND", "DRUM KIT"};

/* corner c: generators, blend, filter (cutoff, resonance), the blend's mode, the filter's type */
static void cn(patch_t *p, uint32_t c, int32_t g1, int32_t g2, int32_t blend, int32_t cut, int32_t res, int32_t mode,
               int32_t ft)
{
    int16_t *q = p->c[c];
    q[CP_G1] = (int16_t)g1, q[CP_G2] = (int16_t)g2, q[CP_BLEND] = (int16_t)blend;
    q[CP_CUT] = (int16_t)cut, q[CP_RES] = (int16_t)res, q[CP_BMODE] = (int16_t)mode, q[CP_FTYPE] = (int16_t)ft;
}
static void wtab(patch_t *p, uint32_t c, int32_t pos1, int32_t orb1, int32_t pos2, int32_t orb2)   /* a corner's wavetables */
{
    p->c[c][CP_WP1] = (int16_t)pos1, p->c[c][CP_WO1] = (int16_t)orb1, p->c[c][CP_WP2] = (int16_t)pos2, p->c[c][CP_WO2] = (int16_t)orb2;
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

static void fixed(patch_t *p, uint32_t c) { p->c[c][CP_TRACK] = 0; }   /* a corner's cutoff not following the key */
static void preset_make2(uint32_t n, patch_t *p);
static void preset_make(uint32_t n, patch_t *p)
{
    patch_init(p);
    if (n % NFACT >= NPRESET) {                    /* FACTORY 2 */
        preset_make2(n % NFACT - NPRESET, p);
        patch_sanitize(p);
        return;
    }
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
        p->g[G_DRIVE] = 34, p->g[G_TRIM] = 6, p->g[G_WIDTH] = 40;   /* (a bass: nearer the middle) */
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
        p->g[G_RVSIZE] = 124, p->g[G_DRIVE] = 20, p->g[G_TRIM] = 8, p->g[G_WIDTH] = 110;
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
    case 8:   /* WAVE ORBIT: four wavetables, each sweeping as the moon comes near its planet: a resonance climbing
               * the harmonics, vowels opening, a sync saw tearing, a phase-bent cosine sharpening; a slow circle */
        cn(p, 0, PAT_W1, PAT_C, 0, 96, 30, BM_AM, FT_LP12);
        cn(p, 1, PAT_W1 + 2, PAT_C, 0, 110, 20, BM_AM, FT_LP12);
        cn(p, 2, PAT_W1 + 4, PAT_D, 0, 70, 50, BM_AM, FT_LP24);
        cn(p, 3, PAT_W1 + 7, PAT_W1 + 5, 36, 100, 20, BM_PM, FT_LP12);
        wtab(p, 0, 0, 63, 0, 0);
        wtab(p, 1, 0, 63, 0, 0);
        wtab(p, 2, 10, 56, 0, 0);
        wtab(p, 3, 0, 63, 40, -40);
        tune(p, 3, 0, 1);
        lvl(p, 120, 116, 110, 127);
        orb(p, FAM_LISSA, 0, LX1, LY1, R8, 127, 0, 30, 40);   /* a slow circle round all four */
        p->g[G_SHARP] = 4;
        amp(p, 50, 80, 115, 85);
        mixer(p, 70, 30, 50);
        p->g[G_WIDTH] = 100, p->g[G_TRIM] = 4;
        break;
    case 9:   /* PPG GLASS: plucked digital waves: a sine in steps, FM bells, a pulse narrowing, drawbars; the star's
               * points at the planets sweep each one's table as the note rings */
        cn(p, 0, PAT_W1 + 6, PAT_C, 0, 120, 10, BM_AM, FT_LP12);
        cn(p, 1, PAT_W1 + 5, PAT_C, 0, 120, 30, BM_AM, FT_LP12);
        cn(p, 2, PAT_W1 + 1, PAT_C, 0, 60, 70, BM_AM, FT_LP24);
        cn(p, 3, PAT_W1 + 3, PAT_W1 + 6, -40, 40, 20, BM_AM, FT_HP);
        wtab(p, 0, 0, 63, 0, 0);
        wtab(p, 1, 20, 63, 0, 0);
        wtab(p, 2, 127, -64, 0, 0);
        wtab(p, 3, 0, 63, 60, 50);
        lvl(p, 110, 127, 120, 110);
        orb(p, FAM_STAR, 63, -40, ST4, R2, 127, DIAG, 20, 70);   /* a four-point star, its points at the planets */
        p->g[G_SHARP] = 4;
        p->g[G_RETRIG] = 1;
        amp(p, 0, 84, 30, 80);
        mixer(p, 80, 40, 30);
        p->g[G_DLTIME] = 60, p->g[G_DLFB] = 60, p->g[G_RVSIZE] = 110, p->g[G_TRIM] = 8;
        break;
    case 10:  /* SPIRO PAD: a slow spirograph through four soft colours: a warm hollow square, a chorused saw an
               * octave down, a vowel O, glassy wavetable air; wide and long */
        cn(p, 0, PAT_D, PAT_A, -30, 34, 30, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_C, -64, 70, 20, BM_AM, FT_LP12);
        cn(p, 2, PAT_C, PAT_D, -64, 16, 110, BM_AM, FT_VOX);
        cn(p, 3, PAT_W1 + 5, PAT_C, 20, 64, 20, BM_AM, FT_HP);
        wtab(p, 3, 30, 50, 0, 0);
        tune(p, 1, 1, 1);
        lvl(p, 110, 75, 127, 127);
        orb(p, FAM_SPIRO, 0, 30, -14, R8, 127, 0, 24, 30);   /* a spirograph's petals, each near a planet */
        p->g[G_SHARP] = 4;
        amp(p, 90, 90, 120, 100);
        mixer(p, 90, 20, 80);
        p->g[G_RVSIZE] = 118, p->g[G_WIDTH] = 120, p->g[G_DETUNE] = 20;
        break;
    case 11:  /* ACID BASS: one voice, gliding: the path pulses between a squelching resonant low-pass and a
               * buzzing PD; a short envelope, drive */
        cn(p, 0, PAT_C, PAT_D, -64, 20, 120, BM_AM, FT_LP24);
        cn(p, 1, PAT_D, PAT_C, -64, 96, 116, BM_AM, FT_LP24);
        cn(p, 2, PAT_C, PAT_B, 60, 60, 60, BM_PDAM, FT_LP12);
        cn(p, 3, PAT_D, PAT_D, -64, 50, 90, BM_AM, FT_BP);
        tune(p, 3, 0, 1);
        lvl(p, 80, 127, 127, 127);
        orb(p, FAM_PULSE, 0, -20, 0, RH, 127, DIAG, 10, 90);   /* the corners in turn, eighths */
        p->g[G_SHARP] = 5;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 1, p->g[G_GLIDE] = 30, p->g[G_RETRIG] = 1;
        amp(p, 0, 50, 60, 20);
        mixer(p, 10, 30, 0);
        p->g[G_DRIVE] = 70, p->g[G_DLTIME] = 50, p->g[G_DLFB] = 40, p->g[G_WIDTH] = 30, p->g[G_TRIM] = 8;
        break;
    case 12:  /* KALIMBA: a struck tine: each note walks its ADSR path from the bright PM strike to a soft round
               * body; a woody band-passed knock, a pure tone */
        cn(p, 0, PAT_A, PAT_F, 50, 90, 20, BM_PM, FT_LP12);
        cn(p, 1, PAT_E, PAT_A, 40, 60, 60, BM_PM, FT_BP);
        cn(p, 2, PAT_A, PAT_C, 0, 24, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_B, PAT_F, 30, 100, 30, BM_PMAM, FT_BP);
        tune(p, 1, 0, 2);
        lvl(p, 60, 127, 100, 127);
        orb(p, FAM_ADSR, -30, 30, 0, R4, 127, DIAG, 10, 50);   /* each note: the strike, the knock, the body */
        p->g[G_SHARP] = 5;
        p->g[G_OA] = 0, p->g[G_OD] = 60, p->g[G_OS] = 0, p->g[G_OR] = 60;
        p->g[G_RETRIG] = 1;
        amp(p, 0, 80, 30, 70);
        mixer(p, 60, 30, 10);
        p->g[G_RVSIZE] = 90, p->g[G_TRIM] = 10;
        break;
    case 13:  /* STAR LEAD: a bright lead on a fast five-point star: a raspy sync saw, a hollow square, a vowel
               * cry, a high FM edge; one voice, a little glide */
        cn(p, 0, PAT_C, PAT_B, 60, 110, 40, BM_PDAM, FT_LP12);
        cn(p, 1, PAT_D, PAT_A, -10, 50, 60, BM_AM, FT_LP24);
        cn(p, 2, PAT_C, PAT_D, -64, 96, 120, BM_AM, FT_VOX);
        cn(p, 3, PAT_C, PAT_F, 63, 70, 20, BM_PM, FT_HP);
        lvl(p, 110, 127, 110, 100);
        orb(p, FAM_STAR, 63, 30, ST5, R1, 127, 0, 16, 100);   /* a fast five-point star */
        p->g[G_SHARP] = 4;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 2, p->g[G_DETUNE] = 14, p->g[G_GLIDE] = 20, p->g[G_FOLVOL] = 100;
        amp(p, 4, 70, 110, 40);
        mixer(p, 40, 60, 20);
        p->g[G_DLTIME] = 70, p->g[G_DLFB] = 50, p->g[G_DRIVE] = 40;
        break;
    case 14:  /* TIDE DRONE: wavetables rising and falling as a very slow knot passes their planets: dark low
               * tables two octaves down, vowels, a tearing sync, noise surf */
        cn(p, 0, PAT_W1, PAT_A, 0, 40, 40, BM_AM, FT_LP24);
        cn(p, 1, PAT_W1 + 2, PAT_C, 0, 100, 30, BM_AM, FT_LP12);
        cn(p, 2, PAT_W1 + 4, PAT_D, 20, 96, 30, BM_AM, FT_BP);
        cn(p, 3, PAT_N, PAT_W1 + 1, -20, 60, 100, BM_AM, FT_HP);
        wtab(p, 0, 0, 63, 0, 0);
        wtab(p, 1, 0, 63, 0, 0);
        wtab(p, 2, 20, 50, 0, 0);
        tune(p, 0, 2, 1), tune(p, 2, 1, 1);
        lvl(p, 90, 64, 127, 56);
        orb(p, FAM_LISSA, 30, LX2, LY3, R16, 127, 0, 34, 15);   /* a slow 2:3 knot */
        p->g[G_SHARP] = 4;
        amp(p, 110, 90, 120, 120);
        mixer(p, 100, 30, 60);
        p->g[G_RVSIZE] = 126, p->g[G_WIDTH] = 127, p->g[G_DRIVE] = 16, p->g[G_TRIM] = 6;
        break;
    case 15:  /* METAL PERC: short hits: the arpeggio steps a fast square path over a clank, a ring-modulated
               * noise snap, a low thud and a high tick */
        cn(p, 0, PAT_F, PAT_E, 63, 90, 40, BM_PM, FT_BP);
        cn(p, 1, PAT_N, PAT_F, 40, 110, 80, BM_AM, FT_HP);
        cn(p, 2, PAT_A, PAT_D, 0, 22, 60, BM_AM, FT_LP24);
        cn(p, 3, PAT_N, PAT_E, 63, 120, 100, BM_PMAM, FT_BP);
        tune(p, 2, 1, 1);
        lvl(p, 120, 127, 60, 127);
        orb(p, FAM_PULSE, 0, -64, 0, RQ, 127, 0, 8, 120);   /* the four corners, sixteenths */
        p->g[G_SHARP] = 6;
        p->g[G_RETRIG] = 1, p->g[G_ARP] = ARP_RND, p->g[G_TEMPO] = 112;
        amp(p, 0, 60, 30, 40);
        mixer(p, 40, 50, 0);
        p->g[G_DLTIME] = 30, p->g[G_DLFB] = 40, p->g[G_TRIM] = 10;
        break;
    case 7:
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
        p->g[G_DLFB] = 100, p->g[G_RVSIZE] = 120, p->g[G_TRIM] = 4, p->g[G_WIDTH] = 110;
        break;
    }
    patch_sanitize(p);
}

/* FACTORY 2: the basics. The same rule: corners apart (dark / bright / metallic / noisy or vowel), a path that
 * reaches them, so the orbit is heard; the ADSR family for the struck and plucked ones (each note walks from its
 * strike to its body: corner 1 at the start, 2 at the attack's end, towards 4 while it sustains, 3 at its release) */
static void preset_make2(uint32_t n, patch_t *p)
{
    switch (n) {
    case 0:   /* WARM STRINGS: an ensemble: a warm saw, cellos an octave down, a nasal viola, the bow's air; a slow
               * circle, the chorus wide */
        cn(p, 0, PAT_C, PAT_D, -64, 56, 10, BM_AM, FT_LP12);
        cn(p, 1, PAT_C, PAT_D, -64, 40, 20, BM_AM, FT_LP24);
        cn(p, 2, PAT_D, PAT_B, -40, 76, 40, BM_AM, FT_BP);
        cn(p, 3, PAT_C, PAT_D, -64, 36, 10, BM_AM, FT_HP);
        tune(p, 1, 1, 1);
        lvl(p, 100, 96, 127, 127);
        orb(p, FAM_LISSA, 0, LX1, LY1, R8, 127, 0, 24, 30);   /* a slow circle round all four */
        p->g[G_SHARP] = 3;
        p->g[G_DETUNE] = 50, p->g[G_STEREO] = 100;
        amp(p, 72, 90, 116, 88);
        mixer(p, 70, 0, 100);
        p->g[G_CHDEP] = 90, p->g[G_CHSPD] = 40, p->g[G_RVSIZE] = 110, p->g[G_WIDTH] = 110;
        break;
    case 1:   /* PWM PAD: pulses whose width the moon sweeps (wavetable W2), one an octave down, a saw, a thin
               * high pulse; a slow spirograph */
        cn(p, 0, PAT_W1 + 1, PAT_C, 0, 70, 20, BM_AM, FT_LP12);
        cn(p, 1, PAT_W1 + 1, PAT_C, 0, 50, 30, BM_AM, FT_LP24);
        cn(p, 2, PAT_C, PAT_D, -64, 50, 40, BM_AM, FT_LP12);
        cn(p, 3, PAT_W1 + 1, PAT_C, 0, 40, 10, BM_AM, FT_HP);
        wtab(p, 0, 0, 63, 0, 0);
        wtab(p, 1, 60, -50, 0, 0);
        wtab(p, 3, 100, 20, 0, 0);
        tune(p, 1, 1, 1);
        lvl(p, 116, 120, 100, 104);
        orb(p, FAM_SPIRO, 0, 30, -14, R8, 127, 0, 26, 36);   /* petals near each planet */
        p->g[G_SHARP] = 3;
        p->g[G_DETUNE] = 40, p->g[G_STEREO] = 110;
        amp(p, 80, 90, 116, 96);
        mixer(p, 80, 0, 100);
        p->g[G_RVSIZE] = 116, p->g[G_WIDTH] = 110;
        break;
    case 2:   /* CHOIR AAH: voices: a saw through the vowels A and O, a vowel wavetable, a breathy U; a figure eight
               * sings them, the chorus a section */
        cn(p, 0, PAT_C, PAT_D, -64, 127, 100, BM_AM, FT_VOX);    /* A */
        cn(p, 1, PAT_C, PAT_D, -64, 64, 110, BM_AM, FT_VOX);     /* O */
        cn(p, 2, PAT_W1 + 2, PAT_C, 0, 64, 0, BM_AM, FT_LP12);
        cn(p, 3, PAT_C, PAT_N, -30, 0, 100, BM_AM, FT_VOX);      /* U, breath */
        wtab(p, 2, 0, 40, 0, 0);
        lvl(p, 120, 120, 100, 110);
        orb(p, FAM_LISSA, 0, LX1, LY2, R8, 127, 0, 20, 24);   /* a slow figure eight */
        p->g[G_SHARP] = 3;
        p->g[G_DETUNE] = 60, p->g[G_STEREO] = 110;
        amp(p, 88, 90, 120, 96);
        mixer(p, 100, 0, 90);
        p->g[G_RVSIZE] = 120, p->g[G_WIDTH] = 100;
        break;
    case 3:   /* DRAWBAR ORGAN: the drawbar wavetable (W4) in three registrations, jazz, gospel, bright, and a reedy
               * clarinet stop; the path slow, the chorus a turning speaker, a little drive */
        cn(p, 0, PAT_W1 + 3, PAT_C, 0, 110, 0, BM_AM, FT_LP12);
        cn(p, 1, PAT_W1 + 3, PAT_C, 0, 120, 0, BM_AM, FT_LP12);
        cn(p, 2, PAT_W1 + 3, PAT_C, 0, 100, 0, BM_AM, FT_LP12);
        cn(p, 3, PAT_D, PAT_E, -30, 60, 30, BM_AM, FT_BP);
        wtab(p, 0, 0, 0, 0, 0);
        wtab(p, 1, 127, 0, 0, 0);
        wtab(p, 2, 84, 0, 0, 0);
        lvl(p, 90, 82, 86, 96);
        orb(p, FAM_LISSA, 0, LX1, LY1, R16, 127, 0, 14, 40);   /* a very slow circle: the registrations in turn */
        p->g[G_SHARP] = 4;
        p->g[G_DETUNE] = 0, p->g[G_STEREO] = 40;
        amp(p, 0, 60, 127, 24);
        mixer(p, 40, 0, 70);
        p->g[G_CHDEP] = 40, p->g[G_CHSPD] = 100, p->g[G_DRIVE] = 30, p->g[G_WIDTH] = 70;
        break;
    case 4:   /* E.PIANO: a tine: struck bright (a phase-modulated bell), its bark, a round body, a warm sustain */
        cn(p, 0, PAT_A, PAT_F, 50, 110, 0, BM_PM, FT_LP12);
        cn(p, 1, PAT_A, PAT_E, 30, 70, 10, BM_PM, FT_LP12);
        cn(p, 2, PAT_A, PAT_C, 0, 36, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_B, PAT_D, -20, 50, 10, BM_AM, FT_LP24);
        lvl(p, 110, 120, 110, 127);
        orb(p, FAM_ADSR, 30, -30, 0, R4, 127, 0, 12, 70);    /* the strike, the bark, the body */
        p->g[G_SHARP] = 4;
        p->g[G_OA] = 10, p->g[G_OD] = 70, p->g[G_OS] = 20, p->g[G_OR] = 70;
        p->g[G_RETRIG] = 1, p->g[G_DETUNE] = 10;
        amp(p, 0, 100, 30, 70);
        mixer(p, 40, 0, 50);
        p->g[G_CHDEP] = 50, p->g[G_CHSPD] = 60, p->g[G_WIDTH] = 90;
        break;
    case 5:   /* HARP: a plucked string: bright at the pluck, a nylon twang, a round body that rings on */
        cn(p, 0, PAT_C, PAT_D, -64, 90, 10, BM_AM, FT_LP12);
        cn(p, 1, PAT_D, PAT_F, -30, 64, 40, BM_AM, FT_BP);
        cn(p, 2, PAT_A, PAT_C, 0, 34, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_B, PAT_C, -20, 56, 10, BM_AM, FT_LP12);
        lvl(p, 110, 120, 116, 127);
        orb(p, FAM_ADSR, -30, 30, 0, R4, 127, 0, 10, 50);
        p->g[G_SHARP] = 4;
        p->g[G_OA] = 0, p->g[G_OD] = 64, p->g[G_OS] = 0, p->g[G_OR] = 60;
        p->g[G_RETRIG] = 1, p->g[G_DETUNE] = 10;
        amp(p, 0, 92, 0, 84);
        mixer(p, 70, 0, 30);
        p->g[G_RVSIZE] = 100, p->g[G_WIDTH] = 90, p->g[G_TRIM] = 9;
        break;
    case 6:   /* MARIMBA: a mallet on wood: the hard strike, a knock of noise, the woody bar, a pure fade */
        cn(p, 0, PAT_A, PAT_F, 30, 76, 40, BM_PM, FT_BP);
        cn(p, 1, PAT_N, PAT_A, 0, 50, 60, BM_AM, FT_BP);
        cn(p, 2, PAT_A, PAT_C, 0, 30, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_A, PAT_C, -20, 64, 10, BM_AM, FT_LP12);
        lvl(p, 116, 90, 120, 127);
        orb(p, FAM_ADSR, 30, 30, 0, R4, 127, 0, 8, 50);
        p->g[G_SHARP] = 5;
        p->g[G_OA] = 0, p->g[G_OD] = 40, p->g[G_OS] = 0, p->g[G_OR] = 50;
        p->g[G_RETRIG] = 1;
        amp(p, 0, 76, 0, 60);
        mixer(p, 40, 0, 10);
        p->g[G_RVSIZE] = 80, p->g[G_WIDTH] = 90, p->g[G_TRIM] = 11;
        break;
    case 7:   /* MUSIC BOX: a small comb: a metallic strike, a pure tine, a glassy ring, a high shimmer */
        cn(p, 0, PAT_A, PAT_F, 40, 120, 0, BM_PM, FT_LP12);
        cn(p, 1, PAT_A, PAT_C, 0, 60, 0, BM_AM, FT_LP12);
        cn(p, 2, PAT_E, PAT_A, 30, 50, 20, BM_PMAM, FT_HP);
        cn(p, 3, PAT_F, PAT_A, 20, 80, 50, BM_PM, FT_BP);
        lvl(p, 96, 100, 127, 127);
        orb(p, FAM_ADSR, 30, -30, 0, R4, 127, DIAG, 10, 80);
        p->g[G_SHARP] = 4;
        p->g[G_OA] = 0, p->g[G_OD] = 60, p->g[G_OS] = 10, p->g[G_OR] = 70;
        p->g[G_RETRIG] = 1;
        amp(p, 0, 88, 0, 80);
        mixer(p, 80, 20, 20);
        p->g[G_RVSIZE] = 110, p->g[G_DLTIME] = 60, p->g[G_DLFB] = 40, p->g[G_WIDTH] = 100, p->g[G_TRIM] = 12;
        break;
    case 8:   /* SUB BASS: one voice: a round low sine, its octave below, a dark square, a growling saw; slow hops */
        cn(p, 0, PAT_A, PAT_C, 0, 26, 0, BM_AM, FT_LP24);
        cn(p, 1, PAT_D, PAT_A, -30, 36, 30, BM_AM, FT_LP24);
        cn(p, 2, PAT_A, PAT_C, 0, 30, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_C, PAT_D, -64, 46, 60, BM_AM, FT_LP24);
        tune(p, 2, 1, 1);
        lvl(p, 127, 110, 120, 100);
        orb(p, FAM_PULSE, 0, -40, 0, R2, 127, 0, 10, 40);   /* corner to corner, every two beats */
        p->g[G_SHARP] = 4;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 1, p->g[G_GLIDE] = 20, p->g[G_STEREO] = 0;
        amp(p, 0, 70, 110, 20);
        mixer(p, 0, 0, 0);
        p->g[G_DRIVE] = 20, p->g[G_WIDTH] = 0, p->g[G_TRIM] = 5;
        break;
    case 9:   /* FM BASS: a plucked phase-modulated bass: each note from its bright FM bite to a dark round tail */
        cn(p, 0, PAT_A, PAT_C, 63, 70, 10, BM_PM, FT_LP12);
        cn(p, 1, PAT_D, PAT_A, 50, 50, 70, BM_PDAM, FT_LP24);
        cn(p, 2, PAT_C, PAT_F, 40, 60, 60, BM_PM, FT_BP);
        cn(p, 3, PAT_A, PAT_C, 0, 24, 0, BM_AM, FT_LP24);
        lvl(p, 110, 116, 100, 127);
        orb(p, FAM_ADSR, 0, -30, 0, R4, 127, 0, 8, 60);
        p->g[G_SHARP] = 4;
        p->g[G_OA] = 0, p->g[G_OD] = 66, p->g[G_OS] = 0, p->g[G_OR] = 40;
        p->g[G_RETRIG] = 1;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 1, p->g[G_GLIDE] = 10, p->g[G_STEREO] = 0;
        amp(p, 0, 80, 30, 24);
        mixer(p, 10, 0, 0);
        p->g[G_DRIVE] = 40, p->g[G_WIDTH] = 20, p->g[G_TRIM] = 10;
        break;
    case 10:  /* BASS SEQ: hold a note or two: a bass line runs (the arpeggio, up, two octaves), each step a planet
               * of its own: a squelchy square, a saw, a sub, a buzzing PD */
        cn(p, 0, PAT_D, PAT_A, -20, 30, 100, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_D, -64, 50, 80, BM_AM, FT_LP24);
        cn(p, 2, PAT_A, PAT_C, 0, 26, 0, BM_AM, FT_LP24);
        cn(p, 3, PAT_C, PAT_B, 50, 60, 40, BM_PDAM, FT_LP12);
        lvl(p, 120, 110, 127, 110);
        orb(p, FAM_SPIRO, 0, 30, -39, R2, 127, DIAG, 10, 60);
        p->g[G_SHARP] = 4;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 1, p->g[G_GLIDE] = 0, p->g[G_STEREO] = 0;
        p->g[G_ARP] = ARP_UP, p->g[G_TEMPO] = 124;
        amp(p, 0, 64, 40, 20);
        mixer(p, 10, 30, 0);
        p->g[G_DRIVE] = 50, p->g[G_DLTIME] = 40, p->g[G_DLFB] = 30, p->g[G_WIDTH] = 30, p->g[G_TRIM] = 10;
        break;
    case 11:  /* SAW ARP: hold a chord: it runs up and down, sixteenths; the free-running star gives each step its
               * own planet: a resonant saw, a bright one, a squelchy square, a bell */
        cn(p, 0, PAT_C, PAT_D, -64, 60, 70, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_D, -64, 90, 20, BM_AM, FT_LP12);
        cn(p, 2, PAT_D, PAT_A, -20, 40, 90, BM_AM, FT_LP24);
        cn(p, 3, PAT_C, PAT_F, 50, 50, 20, BM_PM, FT_HP);
        lvl(p, 120, 110, 120, 100);
        orb(p, FAM_STAR, 63, -40, ST4, R1, 127, DIAG, 16, 70);   /* a four-point star, a round a beat */
        p->g[G_SHARP] = 4;
        p->g[G_ARP] = ARP_UPDN, p->g[G_TEMPO] = 120;
        amp(p, 0, 70, 0, 50);
        mixer(p, 40, 50, 20);
        p->g[G_DLTIME] = 50, p->g[G_DLFB] = 60, p->g[G_WIDTH] = 100, p->g[G_TRIM] = 8;
        break;
    case 12:  /* BRASS SWELL: saws that open as each note swells: dark at the start, the blare, a rasp, a nasal fade */
        cn(p, 0, PAT_C, PAT_D, -64, 30, 20, BM_AM, FT_LP24);
        cn(p, 1, PAT_C, PAT_D, -64, 84, 30, BM_AM, FT_LP12);
        cn(p, 2, PAT_D, PAT_B, -40, 70, 40, BM_AM, FT_BP);
        cn(p, 3, PAT_C, PAT_B, 40, 56, 20, BM_PDAM, FT_LP12);
        lvl(p, 106, 103, 98, 98);
        orb(p, FAM_ADSR, 0, 30, 0, R4, 127, 0, 100, 56);   /* (the moon wide: it sways while held) */
        p->g[G_SHARP] = 5;
        p->g[G_OA] = 70, p->g[G_OD] = 80, p->g[G_OS] = 60, p->g[G_OR] = 60;
        p->g[G_RETRIG] = 1, p->g[G_DETUNE] = 30, p->g[G_STEREO] = 60;
        amp(p, 30, 80, 110, 50);
        mixer(p, 50, 0, 30);
        p->g[G_DRIVE] = 30, p->g[G_WIDTH] = 80;
        break;
    case 13:  /* FLUTE LEAD: one voice, gliding: a soft sine, its breath, a hollow ocarina, a pitched wind */
        cn(p, 0, PAT_A, PAT_C, 0, 40, 10, BM_AM, FT_LP12);
        cn(p, 1, PAT_A, PAT_N, -30, 60, 30, BM_AM, FT_LP12);
        cn(p, 2, PAT_E, PAT_A, 0, 50, 10, BM_AM, FT_LP12);
        cn(p, 3, PAT_N, PAT_A, 0, 34, 110, BM_AM, FT_BP);
        lvl(p, 120, 110, 116, 100);
        orb(p, FAM_LISSA, 0, LX1, LY2, R2, 127, 0, 14, 90);   /* a figure eight */
        p->g[G_SHARP] = 3;
        p->g[G_POLY] = PM_MONO, p->g[G_STACK] = 1, p->g[G_GLIDE] = 30, p->g[G_STEREO] = 0;
        amp(p, 30, 70, 116, 40);
        mixer(p, 50, 40, 20);
        p->g[G_DLTIME] = 70, p->g[G_DLFB] = 40, p->g[G_WIDTH] = 60;
        break;
    case 14:  /* NIGHT WIND: noise, not notes: a howl, a high whistle, a low rumble, a vowel moan, the cutoffs
               * fixed; a slow knot blows between them */
        cn(p, 0, PAT_N, PAT_A, 0, 60, 120, BM_AM, FT_BP);
        cn(p, 1, PAT_N, PAT_A, 0, 100, 110, BM_AM, FT_BP);
        cn(p, 2, PAT_N, PAT_A, 0, 50, 40, BM_AM, FT_LP24);
        cn(p, 3, PAT_N, PAT_A, 0, 64, 120, BM_AM, FT_VOX);
        fixed(p, 0), fixed(p, 1), fixed(p, 2), fixed(p, 3);
        lvl(p, 120, 100, 110, 120);
        orb(p, FAM_LISSA, 22, LX3, LY2, R16, 127, 0, 30, 20);   /* a slow 3:2 knot */
        p->g[G_SHARP] = 3;
        amp(p, 110, 90, 127, 110);
        mixer(p, 100, 40, 40);
        p->g[G_RVSIZE] = 126, p->g[G_DLTIME] = 80, p->g[G_DLFB] = 70, p->g[G_WIDTH] = 127, p->g[G_TRIM] = 9;
        break;
    default:  /* DRUM KIT: hold a chord: random hits, sixteenths, each planet a drum: a low thump, a snare, a
               * hi-hat, a ringing tom */
        cn(p, 0, PAT_A, PAT_C, 0, 30, 20, BM_AM, FT_LP24);
        cn(p, 1, PAT_N, PAT_D, 30, 70, 40, BM_AM, FT_BP);
        cn(p, 2, PAT_N, PAT_A, 0, 110, 20, BM_AM, FT_HP);
        cn(p, 3, PAT_A, PAT_F, 50, 50, 70, BM_PM, FT_BP);
        tune(p, 0, 2, 2);
        fixed(p, 2);
        lvl(p, 127, 127, 116, 127);
        orb(p, FAM_PULSE, 0, -64, 0, RQ, 127, 0, 8, 120);   /* the four corners, sixteenths */
        p->g[G_SHARP] = 8;
        p->g[G_ARP] = ARP_RND, p->g[G_TEMPO] = 104;
        amp(p, 0, 74, 0, 30);
        mixer(p, 30, 20, 0);
        p->g[G_DLTIME] = 30, p->g[G_DLFB] = 30, p->g[G_WIDTH] = 90, p->g[G_TRIM] = 8;
        break;
    }
}
