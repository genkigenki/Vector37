/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The screen in motion, for the docs (tools/docs_img.sh). Frames (PPM, 40 ms apart) into OUTDIR.
 *   tools/anim OUTDIR        the tour: the boot; a chord on a figure eight, the moon circling slowly; every screen in turn
 *   tools/anim OUTDIR N      sound N (0 .. 7) playing
 *   tools/anim OUTDIR family fader 1 through the path families
 *   tools/anim OUTDIR glide  new sounds, the faders' values gliding to them
 *   tools/anim OUTDIR planet the orbit's knobs (none selected); editing planets, alone and together; mute; the mod wheel */
#include <stdio.h>
#include <stdlib.h>
#include "../tests/panel.h"

static const char *odir;
static uint32_t fr;
static void snap(void)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/f%04u.ppm", odir, fr++);
    if (!(f = fopen(path, "wb"))) { perror(path); exit(1); }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                          (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void play(uint32_t ms) { uint32_t t; for (t = 0; t < ms; t += 40u) { panel_ms(40); snap(); } }

/* one sound: a chord held, then single notes (a bell, a pluck: each its own) */
static void one_sound(uint32_t n)
{
    uint32_t k;
    panel_init();
    fm1_in.frames = 400;
    preset_load(n);
    play(400);
    panel_key(12, 1, 100), panel_key(16, 1, 90), panel_key(19, 1, 90);
    play(3200);
    panel_key(12, 0, 0), panel_key(16, 0, 0), panel_key(19, 0, 0);
    for (k = 0; k < 4u; k++) {
        panel_key(24u - k * 5u, 1, 100);
        play(560);
        panel_key(24u - k * 5u, 0, 0);
    }
    play(600);
}

/* the families: fader 1 slowly from Lissajous to ADSR and back, fader 4 (MORPH) on the way */
static void family_demo(void)
{
    uint32_t k;
    panel_init();
    fm1_in.frames = 400;
    preset_load(0);
    panel_key(12, 1, 100), panel_key(19, 1, 90);
    panel_fader(0, 4), panel_ms(20), panel_fader(0, 0);   /* (F1 to the start: it glides there) */
    play(800);
    for (k = 0; k <= 127u; k += 2u) {               /* through all five */
        panel_fader(0, k);
        play(80);
    }
    for (k = 100; k >= 40u; k -= 3u) {               /* MORPH */
        panel_fader(3, k);
        play(80);
    }
    for (k = 127; k >= 4u; k -= 4u) {               /* and back */
        panel_fader(0, k);
        play(80);
    }
    play(600);
}

/* glide to the fader: a new sound brings its path; a fader moved: the value glides there (the path morphs) */
static void glide_demo(void)
{
    uint32_t n;
    panel_init();
    fm1_in.frames = 400;
    panel_key(12, 1, 100), panel_key(19, 1, 90);
    play(600);
    for (n = 0; n < 3u; n++) {
        static const uint8_t TO[3][4] = {{40, 50, 60, 20}, {100, 127, 40, 90}, {70, 80, 50, 60}};
        uint32_t i;
        panel_button(3, 1), panel_ms(40), panel_button(3, 0);   /* (>: the next sound, its own path) */
        play(900);
        for (i = 0; i < 4u; i++) {                  /* each fader nudged: its value glides to it */
            panel_fader(i, TO[n][i] + 3u), panel_ms(20), panel_fader(i, TO[n][i]);
            play(500);
        }
        play(600);
    }
}

/* editing a planet with no key held: each one picked (it zooms), knobs turned slowly (its satellite swells, the
 * planet changes, it is heard alone), a MODE pad, a FILTER pad */
static void planet_demo(void)
{
    static const uint8_t KN[6] = {0, 3, 5, 6, 2, 7};   /* GEN 1, BLEND, CUTOFF, RESO, TUNE 2, LEVEL */
    static const int8_t ST[6] = {1, 4, 3, 4, 1, -3};
    uint32_t c, k, j;
    panel_init();
    fm1_in.frames = 400;
    preset_load(0);
    play(600);
    for (k = 0; k < 3u; k++) {                      /* none selected: the knobs are the orbit (WARP, SPIN, MOON DIST) */
        static const uint8_t OK[3] = {0, 5, 4};
        static const int8_t OS[3] = {4, 3, 5};
        for (j = 0; j < 8u; j++) {
            panel_enc(OK[k], OS[k]);
            play(100);
        }
        play(900);
    }
    for (c = 0; c < 4u; c++) {
        uint32_t pad = (c & 1u) + (c & 2u ? 8u : 0u);
        panel_pad(pad, 100), panel_ms(40), panel_pad(pad, 0);   /* SELECT: it zooms up */
        play(700);
        for (k = 0; k < 3u; k++) {
            uint32_t kn = KN[(c + k) % 6u];
            for (j = 0; j < 8u; j++) {
                panel_enc(kn, ST[(c + k) % 6u]);
                play(120);
            }
            play(400);
        }
        panel_pad(pad + 2u, 100), panel_ms(40), panel_pad(pad + 2u, 0);   /* MODE */
        play(900);
        panel_pad(pad + 4u, 100), panel_ms(40), panel_pad(pad + 4u, 0);   /* FILTER */
        play(1100);
    }
    panel_pad(0, 100), panel_ms(40), panel_pad(9, 100), panel_ms(40);   /* planets 1 and 4 together */
    panel_pad(0, 0), panel_pad(9, 0);
    play(900);
    for (j = 0; j < 10u; j++) {                     /* CUTOFF: both, each from its own value */
        panel_enc(5, j < 5u ? 4 : -4);
        play(120);
    }
    play(1200);
    panel_pad(9, 100), panel_ms(40), panel_pad(9, 0);   /* 4 out; then 1 out: the orbit's knobs */
    play(600);
    panel_pad(0, 100), panel_ms(40), panel_pad(0, 0);
    play(900);
    for (k = 0; k < 2u; k++)                        /* MUTE: each planet off (a flash, it collapses), then on (it ignites) */
        for (c = 0; c < 4u; c++) {
            uint32_t pad = 6u + (c & 1u) + (c & 2u ? 8u : 0u);
            panel_pad(pad, 100), panel_ms(40), panel_pad(pad, 0);
            play(600);
        }
    panel_key(12, 1, 100), panel_key(19, 1, 90);    /* the mod wheel: the moon's speed, its ring lit */
    for (j = 0; j <= 24u; j++) {
        panel_wheel(1, 615u - j * 5u);
        play(80);
    }
    play(1200);
    for (j = 0; j <= 12u; j++) {
        panel_wheel(1, 495u + j * 8u);
        play(80);
    }
    play(1500);
}

/* the tour: the boot; then a chord held on a figure eight (Lissajous 1:2), the moon circling the sun slowly, while
 * every screen is shown in turn: the orbit's knobs (none selected), a planet selected and edited, MODE, FILTER, two
 * planets together, MUTE, the faders (each back to the eight), the mod wheel, the held buttons' knob sets, the guide */
static void tap_pad(uint32_t p) { panel_pad(p, 100), panel_ms(40), panel_pad(p, 0); }
static uint32_t padof(uint32_t g, uint32_t c) { return g * 2u + (c & 1u) + (c & 2u ? 8u : 0u); }
static void turn(uint32_t k, int32_t step, uint32_t n, uint32_t ms)   /* knob k, n steps, one each ms */
{
    uint32_t j;
    for (j = 0; j < n; j++) {
        panel_enc(k, step);
        play(ms);
    }
}
static void fader_to(uint32_t i, uint32_t from, uint32_t to, uint32_t st, uint32_t ms)   /* a fader moved slowly */
{
    uint32_t v = from;
    while (v != to) {
        v = to > v ? (to - v > st ? v + st : to) : (v - to > st ? v - st : to);
        panel_fader(i, v);
        play(ms);
    }
}
#define WHEEL_AT(r) (603u - (uint32_t)(r) * 118u / 127u)   /* the mod wheel's reading for MOON SPEED r */
static void hold(uint32_t b, uint32_t ms) { panel_button(b, 1), play(ms), panel_button(b, 0); }

static void tour(void)
{
    uint32_t t, k;
    panel_init();
    fm1_in.frames = 400;
    panel_ms(300);
    preset_load(3);                                 /* TALKING: a figure eight, Lissajous x 1 : y 2, exact */
    pt.g[G_SHAPE] = FAM_LISSA, pt.g[G_WARPX] = 0, pt.g[G_WARPY] = LX1, pt.g[G_MORPH] = LY2;
    pt.g[G_RATE] = R16, pt.g[G_SIZE] = 70, pt.g[G_ROT] = 0, pt.g[G_SUBSZ] = 54;   /* (calm: a round in 9 s) */
    for (t = 0; t <= BOOT_MS; t += 60u) {           /* the boot (a little faster) */
        boot_frame(t);
        snap();
    }
    ui.help = 1, panel_ms(60), ui.help = 0;         /* (unseen: all drawn anew; the faders and the wheel taken up, */
    panel_fader(0, 10), panel_fader(1, 70), panel_fader(2, 22), panel_fader(3, 25);   /* the chord) (where the sound is) */
    panel_wheel(1, WHEEL_AT(44));                   /* (MOON SPEED 44: a turn in about 3 s) */
    panel_ms(2200);
    panel_key(12, 1, 100), panel_key(16, 1, 90), panel_key(19, 1, 90);   /* a chord, held throughout */
    panel_ms(600);
    play(1500);
    for (k = 0; k < 8u; k++) {                      /* none selected: the orbit's knobs, one by one */
        static const int8_t ST[8] = {2, 2, 6, 1, 0, 1, 1, 6};   /* (WARP, WARP Y, ROTATE, SHARP, MOON DIST, SPIN, RETRIG, TEMPO) */
        if (k == 4u) {                              /* MOON DIST: out, the moon's ring wider */
            turn(4, 2, 6, 100);
            play(600);
            continue;
        }
        panel_enc(k, ST[k]);
        play(550);
        panel_enc(k, -ST[k]);
        play(450);
    }
    tap_pad(padof(0, 0));                           /* SELECT planet 1: it zooms, the knobs are its own */
    play(800);
    turn(5, 3, 6, 100);                             /* K6 CUTOFF */
    play(500);
    turn(3, 5, 6, 100);                             /* K4 BLEND */
    play(500);
    tap_pad(padof(1, 0));                           /* MODE */
    play(1000);
    tap_pad(padof(2, 0));                           /* FILTER */
    play(1000);
    panel_pad(padof(0, 0), 100), panel_ms(30), panel_pad(padof(0, 3), 100), panel_ms(30);   /* planets 1 and 4 together */
    panel_pad(padof(0, 0), 0), panel_pad(padof(0, 3), 0);
    play(600);
    turn(5, -3, 8, 100);                            /* K6: both, each from its own value */
    play(600);
    tap_pad(padof(0, 3));                           /* 4 out, then 1: none selected */
    play(300);
    tap_pad(padof(0, 0));
    play(600);
    tap_pad(padof(3, 1));                           /* MUTE planet 2: a flash, it collapses */
    play(1100);
    tap_pad(padof(3, 1));                           /* ... and on again: it ignites */
    play(1100);
    fader_to(0, 10, 40, 3, 80);                     /* F1 FAMILY: to the spirograph ... */
    play(600);
    fader_to(0, 40, 10, 3, 80);                     /* ... and back to the eight */
    play(400);
    fader_to(1, 70, 40, 3, 80);                    /* F2 SIZE */
    play(300);
    fader_to(1, 40, 70, 3, 80);
    play(500);
    panel_fader(2, 38);                             /* F3 SPEED: twice as fast, and back */
    play(900);
    panel_fader(2, 22);
    play(500);
    fader_to(3, 25, 50, 3, 80);                     /* F4 MORPH: y 3, a knot ... */
    play(600);
    fader_to(3, 50, 25, 3, 80);                     /* ... and back: y 2, the eight */
    play(600);
    for (k = 0; k <= 8u; k++) {                     /* the mod wheel: the moon faster, then slow again */
        panel_wheel(1, WHEEL_AT(44 + k * 3u));
        play(80);
    }
    play(900);
    for (k = 0; k <= 8u; k++) {
        panel_wheel(1, WHEEL_AT(68 - k * 3u));
        play(80);
    }
    play(1600);                                     /* (its box gone: the held buttons' own) */
    hold(B_FX, 1400);                               /* held buttons: their knob sets */
    play(300);
    hold(B_FADERBANK, 1400);
    play(300);
    hold(B_CHORD, 1400);
    play(400);
    panel_button(B_KNOBBANK, 1), panel_ms(40), panel_button(B_KNOBBANK, 0);   /* the guide; K1 scrolls it */
    play(1100);
    turn(0, 1, 16, 80);
    play(900);
    panel_button(B_KNOBBANK, 1), panel_ms(40), panel_button(B_KNOBBANK, 0);
    play(800);
}

int main(int argc, char **argv)
{
    odir = argc > 1 ? argv[1] : ".";
    if (argc > 2 && argv[2][0] == 'f') {            /* tools/anim OUTDIR family */
        family_demo();
        return 0;
    }
    if (argc > 2 && argv[2][0] == 'p') {            /* tools/anim OUTDIR planet */
        planet_demo();
        return 0;
    }
    if (argc > 2 && argv[2][0] == 'g') {            /* tools/anim OUTDIR glide */
        glide_demo();
        return 0;
    }
    if (argc > 2) {                                 /* tools/anim OUTDIR N: sound N alone */
        one_sound((uint32_t)atoi(argv[2]));
        return 0;
    }
    tour();
    return 0;
}
