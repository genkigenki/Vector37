/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Vector37 data model: four corners (each two overtone generators, a blend, a filter), the orbit that mixes
 * them, the voices, the effects. */
#include <stdint.h>
#define NC 4                    /* corners: 1 top left, 2 top right, 3 bottom left, 4 bottom right */
#define NVOICE 8                /* voices rendered at most (the CPU guard may allow fewer) */

/* per-corner parameters */
enum { CP_G1, CP_G2, CP_BLEND, CP_CUT, CP_RES, CP_BMODE, CP_FTYPE, CP_TRACK, CP_T1, CP_T2, CP_LEVEL, CP_MUTE, CP_N };
enum { PAT_A, PAT_B, PAT_C, PAT_D, PAT_E, PAT_F, PAT_N, PAT_COUNT };        /* N: noise */
enum { BM_AM, BM_PM, BM_PMAM, BM_PDAM, BM_RPDAM, BM_N };                    /* the right half of BLEND */
enum { FT_LP12, FT_LP24, FT_BP, FT_HP, FT_VOX, FT_OFF, FT_N };              /* VOX: the formant filter */

/* global parameters */
enum {
    G_SHAPE, G_WARPX, G_WARPY, G_RATE, G_SIZE, G_ROT, G_CX, G_CY,           /* the orbit */
    G_SUBSZ, G_SUBSPD, G_RETRIG,                                            /* the suborbit; retrigger */
    G_POLY, G_STACK, G_DETUNE, G_GLIDE, G_STEREO, G_FOLVOL,                 /* voices */
    G_AA, G_AD, G_AS, G_AR,                                                 /* the amp envelope */
    G_OA, G_OD, G_OS, G_OR,                                                 /* the orbit envelope (ADSR shape) */
    G_DRIVE, G_DCOL, G_CHDEP, G_CHSPD, G_DLTIME, G_DLFB, G_RVSIZE, G_RVCOL, /* effects */
    G_RVMIX, G_DLMIX, G_CHMIX, G_VOLUME,                                    /* the mixer (faders) */
    G_TEMPO, G_ARP, G_BEND,
    G_TRIM,                                                                 /* the sound's level, 0 .. +12 dB */
    G_SHARP,                                                                /* how alone a corner is near it, 1 .. 8 */
    G_MORPH,                                                                /* the path family's third parameter */
    G_AUDITION,                                                             /* a changed planet heard alone (UI) */
    G_SPIN,                                                                 /* the path turning on and on, in time */
    G_COUNT
};
enum { SH_LISSA, SH_STAR, SH_SPIRO, SH_PULSE, SH_ADSR, SH_N };              /* the path families */
enum { PM_POLY, PM_DUAL, PM_MONO, PM_N };
enum { ARP_OFF, ARP_UP, ARP_DOWN, ARP_UPDN, ARP_RND, ARP_N };

typedef struct {                /* a sound */
    int16_t c[NC][CP_N];
    int16_t g[G_COUNT];
} patch_t;

typedef struct {                /* one corner of a voice: its noise generators' state */
    int32_t pk[2][3];           /* noise: pink filter states per generator */
    int32_t nh[2][2];           /* noise: its high-pass at the fundamental */
} cstate_t;

typedef struct {                /* a voice (audio side) */
    uint8_t on;                 /* gate */
    uint8_t stage, ostage;      /* amp / orbit envelope: 0 idle, 1 attack, 2 decay / sustain, 3 release */
    uint8_t note, vel, follower, legato;
    uint32_t age;               /* note-on order (stealing: the oldest goes) */
    int32_t env, oenv, oprog;   /* amp envelope Q24, orbit envelope Q24, the orbit envelope's segment progress Q16 */
    int32_t amp;                /* amplitude now (Q15), ramped per block */
    int32_t pitch, target;      /* cents x 16 (gliding), its aim */
    int32_t dcents;             /* fixed detune (stack, follower), cents x 16 */
    int32_t drift, dtgt;        /* POLY drift now / aim, cents x 16 */
    uint32_t ph, inc;           /* the master phase (runs at 1/60 of the note: generator ratios 1 .. 1/5) */
    uint32_t oph, sph;          /* orbit phase, suborbit phase */
    int32_t ox, oy, gx, gy;     /* the orbs: blue (on the trajectory), green (the suborbit); Q15, -1 .. 1 */
    int32_t w[NC], wt[NC];      /* corner weights now / this tick's aim (Q15), ramped per block */
    int32_t at;                 /* amplitude aim this tick */
    int32_t rx, ry;             /* ADSR shape: where the release started */
    int32_t cut, res;           /* the filter at the orbit's position: cutoff index x 256, resonance */
    int32_t pan;                /* 0 .. 32767 (left .. right) */
    int32_t gl, gr;             /* gains now */
    uint32_t rnd;
    cstate_t cs[NC];
    int32_t fs[FT_N][4];        /* the filters' states, one set per type (its corners share it; LP24, VOX: two SVFs) */
} voice_t;
