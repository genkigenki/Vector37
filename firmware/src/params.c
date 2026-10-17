/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Parameter descriptors (range, default, how a value reads) and the initial sound. */
enum { F_NUM, F_ENUM, F_ONOFF, F_SIGNED, F_BLEND };
typedef struct {
    const char *label;
    int16_t min, max, def;
    uint8_t fmt;
    const char *const *names;
} pdesc_t;

static const char *const N_PAT[PAT_COUNT] = {"A", "B", "C", "D", "E", "F", "N", "W1", "W2", "W3", "W4", "W5", "W6", "W7", "W8"};
static const char *const N_WT[WT_NB] = {"SCAN", "PULSE", "VOWEL", "ORGAN", "SYNC", "FM", "STEPS", "WARP"};   /* (gen_tables.py) */
static const char *const N_BMODE[BM_N] = {"AM", "PM", "PM+AM", "PD+AM", "RPD+AM"};
static const char *const N_FTYPE[FT_N] = {"LP12", "LP24", "BP", "HP", "VOX", "OFF"};
static const char *const N_RATIO[5] = {"1", "1/2", "1/3", "1/4", "1/5"};
static const char *const N_SHAPE[SH_N] = {"LISSAJOUS", "STAR", "SPIRO", "PULSE", "ADSR"};
static const char *const N_POLY[PM_N] = {"POLY", "DUAL", "MONO"};
static const char *const N_ARP[ARP_N] = {"OFF", "UP", "DOWN", "UP-DN", "RANDOM"};
static const char *const N_RATE[9] = {"32", "16", "8", "4", "2", "1", "1/2", "1/4", "1/8"};   /* beats a round */

static const pdesc_t CDESC[CP_N] = {
    [CP_G1] = {"GEN 1", 0, PAT_COUNT - 1, PAT_C, F_ENUM, N_PAT},
    [CP_G2] = {"GEN 2", 0, PAT_COUNT - 1, PAT_D, F_ENUM, N_PAT},
    [CP_BLEND] = {"BLEND", -64, 63, 0, F_BLEND, 0},
    [CP_CUT] = {"CUTOFF", 0, 127, 70, F_NUM, 0},
    [CP_RES] = {"RESO", 0, 127, 20, F_NUM, 0},
    [CP_BMODE] = {"MODE", 0, BM_N - 1, BM_AM, F_ENUM, N_BMODE},
    [CP_FTYPE] = {"FILTER", 0, FT_N - 1, FT_LP12, F_ENUM, N_FTYPE},
    [CP_TRACK] = {"TRACK", 0, 1, 1, F_ONOFF, 0},
    [CP_T1] = {"TUNE 1", 0, 4, 0, F_ENUM, N_RATIO},
    [CP_T2] = {"TUNE 2", 0, 4, 0, F_ENUM, N_RATIO},
    [CP_LEVEL] = {"LEVEL", 0, 127, 127, F_NUM, 0},
    [CP_MUTE] = {"MUTE", 0, 1, 0, F_ONOFF, 0},
    [CP_WP1] = {"WT POS 1", 0, 127, 0, F_NUM, 0},          /* GEN 1's wavetable: where it reads (frame 1 .. 32) */
    [CP_WO1] = {"WT ORBIT 1", -64, 63, 40, F_SIGNED, 0},   /* ... how far the moon moves it as it nears the planet */
    [CP_WP2] = {"WT POS 2", 0, 127, 0, F_NUM, 0},
    [CP_WO2] = {"WT ORBIT 2", -64, 63, 40, F_SIGNED, 0},
};

static const pdesc_t GDESC[G_COUNT] = {
    [G_SHAPE] = {"FAMILY", 0, 124, 0, F_NUM, 0},             /* (Lissajous 0, spiro 31, star 62, pulse 93, ADSR 124) */
    [G_WARPX] = {"WARP", -64, 63, 0, F_SIGNED, 0},
    [G_WARPY] = {"WARP Y", -64, 63, 0, F_SIGNED, 0},
    [G_RATE] = {"SPEED", 0, 8, 4, F_ENUM, N_RATE},
    [G_SIZE] = {"SIZE", 0, 127, 90, F_NUM, 0},
    [G_ROT] = {"ROTATE", 0, 127, 0, F_NUM, 0},
    [G_CX] = {"CENTRE X", -64, 63, 0, F_SIGNED, 0},
    [G_CY] = {"CENTRE Y", -64, 63, 0, F_SIGNED, 0},
    [G_SUBSZ] = {"MOON DIST", 0, 127, 30, F_NUM, 0},
    [G_SUBSPD] = {"MOON SPEED", 0, 127, 60, F_NUM, 0},
    [G_RETRIG] = {"RESTART", 0, 1, 0, F_ONOFF, 0},
    [G_POLY] = {"VOICES", 0, PM_N - 1, PM_POLY, F_ENUM, N_POLY},
    [G_STACK] = {"STACK", 1, NVOICE, 4, F_NUM, 0},
    [G_DETUNE] = {"DETUNE", 0, 127, 24, F_NUM, 0},
    [G_GLIDE] = {"GLIDE", 0, 127, 40, F_NUM, 0},
    [G_STEREO] = {"STEREO", 0, 127, 60, F_NUM, 0},
    [G_FOLVOL] = {"FOLLOWER", 0, 127, 90, F_NUM, 0},
    [G_AA] = {"ATTACK", 0, 127, 20, F_NUM, 0},
    [G_AD] = {"DECAY", 0, 127, 70, F_NUM, 0},
    [G_AS] = {"SUSTAIN", 0, 127, 100, F_NUM, 0},
    [G_AR] = {"RELEASE", 0, 127, 70, F_NUM, 0},
    [G_OA] = {"O ATTACK", 0, 127, 70, F_NUM, 0},
    [G_OD] = {"O DECAY", 0, 127, 80, F_NUM, 0},
    [G_OS] = {"O SUSTAIN", 0, 127, 64, F_NUM, 0},
    [G_OR] = {"O RELEASE", 0, 127, 80, F_NUM, 0},
    [G_DRIVE] = {"DRIVE", 0, 127, 0, F_NUM, 0},
    [G_DCOL] = {"COLOUR", 0, 127, 60, F_NUM, 0},
    [G_CHDEP] = {"CH DEPTH", 0, 127, 60, F_NUM, 0},
    [G_CHSPD] = {"CH SPEED", 0, 127, 30, F_NUM, 0},
    [G_DLTIME] = {"DL TIME", 0, 127, 60, F_NUM, 0},
    [G_DLFB] = {"FEEDBACK", 0, 127, 50, F_NUM, 0},
    [G_RVSIZE] = {"RV SIZE", 0, 127, 90, F_NUM, 0},
    [G_RVCOL] = {"RV COLOUR", 0, 127, 70, F_NUM, 0},
    [G_RVMIX] = {"REVERB", 0, 127, 40, F_NUM, 0},
    [G_DLMIX] = {"DELAY", 0, 127, 0, F_NUM, 0},
    [G_CHMIX] = {"CHORUS", 0, 127, 30, F_NUM, 0},
    [G_VOLUME] = {"VOLUME", 0, 127, 100, F_NUM, 0},
    [G_TEMPO] = {"BPM", 40, 240, 110, F_NUM, 0},
    [G_ARP] = {"ARP", 0, ARP_N - 1, ARP_OFF, F_ENUM, N_ARP},
    [G_BEND] = {"BEND", 0, 12, 2, F_NUM, 0},
    [G_TRIM] = {"TRIM", 0, 12, 0, F_NUM, 0},
    [G_SHARP] = {"CONTRAST", 1, 8, 3, F_NUM, 0},
    [G_MORPH] = {"MORPH", -64, 63, -64, F_SIGNED, 0},
    [G_AUDITION] = {"AUDITION", 0, 1, 1, F_ONOFF, 0},
    [G_SPIN] = {"SPIN", -64, 63, 0, F_SIGNED, 0},            /* (0 still; the sign the direction; v^2: turns a beat) */
    [G_WIDTH] = {"WIDTH", 0, 127, 85, F_NUM, 0},             /* the orbit in stereo: 0 mono, 127 the planets at the sides */
};

/* the initial sound: C + D (a saw) round four corners of the classic patterns, low-passed */
static void patch_init(patch_t *p)
{
    uint32_t c, k;
    for (c = 0; c < NC; c++)
        for (k = 0; k < CP_N; k++)
            p->c[c][k] = CDESC[k].def;
    for (k = 0; k < G_COUNT; k++)
        p->g[k] = GDESC[k].def;
}

static void patch_sanitize(patch_t *p)
{
    uint32_t c, k;
    for (c = 0; c < NC; c++)
        for (k = 0; k < CP_N; k++)
            p->c[c][k] = (int16_t)clamp(p->c[c][k], CDESC[k].min, CDESC[k].max);
    for (k = 0; k < G_COUNT; k++)
        p->g[k] = (int16_t)clamp(p->g[k], GDESC[k].min, GDESC[k].max);
}
