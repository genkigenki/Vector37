/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Host test of Vector37's FM-1 panel (firmware/src/panel_fm1.c) with the real engine, UI and drawing against a
 * simulated FM-1 (tests/panel.h with BOARD_SMK37 0): ALGORITHM the planets, KNOB 1..4 on the pages (SEL, SELECT,
 * HOME), the PATH page (the SMK's faders), MOON SPEED on the ORBIT set, PRESETS the sounds, the held sets (FX, ENV,
 * LFO, ARP), EDIT + the lowest 16 keys as the pads with their LEDs, the keys at velocity 100, the buttons (SEQ, REC,
 * PLAY, GLO, SAVE, OCT, EDIT + PLAY / SEQ), the MASTER pot. The FM-1's screens as PPMs into OUTDIR.
 *   tests/fm1_panel OUTDIR   (tests/run_tests.sh) */
#include <stdio.h>
#include <stdlib.h>
#define BOARD_SMK37 0
#include "panel.h"

static int fails;
static const char *dir;

static void shot(const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
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
static void check(const char *what, int ok)
{
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    fails += !ok;
}
static int hint_has(const char *s)
{
    uint32_t i, j;
    if ((int32_t)(ui.msg_until - fm1_ms) <= 0)
        return 0;
    for (i = 0; ui.msg[i]; i++) {
        for (j = 0; s[j] && ui.msg[i + j] == s[j]; j++)
            ;
        if (!s[j])
            return 1;
    }
    return 0;
}
enum { E_SELECT, E_ALGO, E_K1, E_K2, E_K3, E_K4, E_PRESET };
static void tap(uint32_t b) { panel_button(b, 1), panel_ms(60), panel_button(b, 0), panel_ms(60); }
static void turn(uint32_t e, int32_t s) { panel_enc(e, s), panel_ms(60); }
static void note(uint32_t k, uint32_t down) { panel_note(k, down), panel_ms(40); }
static uint32_t sounding(void)
{
    uint32_t v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += vs[v].stage != 0;
    return n;
}
static double peak_of(uint32_t ms)
{
    static float l[4410];
    double pk = 0;
    uint32_t i, n;
    for (; ms; ms -= n * 10u / 441u) {
        n = ms * 441u / 10u > 4410u ? 4410u : ms * 441u / 10u;
        panel_render(l, 0, n);
        for (i = 0; i < n; i++)
            pk = l[i] > pk ? l[i] : -l[i] > pk ? -l[i] : pk;
        if (n * 10u / 441u == 0u)
            break;
    }
    return pk;
}

int main(int argc, char **argv)
{
    int32_t v;
    uint32_t k, ok;
    dir = argc > 1 ? argv[1] : ".";
    panel_init();
    fm1_in.frames = 400;
    panel_ms(600);
    shot("fm1_home");
    check("switched on: nothing selected, the knobs are the orbit, page 1", ui.sel == 0u && ui.layer == L_ORBIT && fp.page == 0u);

    /* ALGORITHM: none, 1, 2, 3, 4 */
    turn(E_ALGO, 1);
    check("ALGORITHM +1: planet 1, its knobs", ui.sel == 1u && ui.layer == L_MAIN && hint_has("PLANET 1"));
    turn(E_ALGO, 5);
    check("... +5: planet 4 (the end)", ui.sel == 8u);
    turn(E_ALGO, -4);
    check("... -4: none, the knobs are the orbit", ui.sel == 0u && ui.layer == L_ORBIT && hint_has("NONE"));
    turn(E_ALGO, 1);

    /* page 1: KNOB 1..4 = K1..K4 */
    v = pt.c[0][CP_G1];
    turn(E_K1, 1);
    check("page 1, KNOB 1: GEN 1 of planet 1", pt.c[0][CP_G1] == v + 1 && sc.kb == 1u);
    v = pt.c[0][CP_BLEND];
    turn(E_K4, 3);
    panel_ms(200);
    shot("fm1_page1_blend");
    check("page 1, KNOB 4: BLEND", pt.c[0][CP_BLEND] == v + 3 && sc.kb == 4u);
    /* page 2: K5..K8 */
    tap(B_SEL);
    check("SEL: page 2, said, its light", fp.page == 1u && hint_has("PAGE 2") && (btn_leds >> B_SEL & 1u));
    v = pt.c[0][CP_CUT];
    turn(E_K2, -5);
    panel_ms(200);
    shot("fm1_page2_cutoff");
    check("page 2, KNOB 2: CUTOFF (K6)", pt.c[0][CP_CUT] == v - 5 && sc.kb == 6u && panel_krow() == 1);
    v = pt.c[0][CP_LEVEL];
    turn(E_K4, -4);
    check("page 2, KNOB 4: LEVEL (K8)", pt.c[0][CP_LEVEL] == v - 4);
    turn(E_SELECT, -1);
    check("SELECT -1: page 1", fp.page == 0u);
    turn(E_SELECT, 2);
    check("SELECT +2: page 3, the path", fp.page == 2u && panel_krow() < 0 && hint_has("PATH"));
    /* page 3: the path */
    v = pt.g[G_SIZE];
    turn(E_K2, -10);
    panel_ms(100);
    shot("fm1_path_size");
    check("page 3, KNOB 2: SIZE (the SMK's fader 2), the central box", pt.g[G_SIZE] == v - 10 && sc.cbox == CB_FADER &&
          hint_has("F2 SIZE"));
    v = pt.g[G_SHAPE];
    turn(E_K1, 3);
    panel_ms(100);
    shot("fm1_path_family");
    check("page 3, KNOB 1: FAMILY, the families' list", pt.g[G_SHAPE] == v + 6 && sc.cbox == CB_FAM);
    v = pt.g[G_RATE];
    turn(E_K3, -1);
    check("page 3, KNOB 3: SPEED", pt.g[G_RATE] == v - 1);
    v = pt.g[G_MORPH];
    turn(E_K4, 4);
    check("page 3, KNOB 4: MORPH", pt.g[G_MORPH] == v + 4);
    tap(B_HOME);
    check("HOME: page 1", fp.page == 0u && !(btn_leds >> B_SEL & 1u));

    /* the orbit's knobs: page 2 KNOB 4 = MOON SPEED (no mod wheel) */
    turn(E_ALGO, -4);
    tap(B_SEL);
    v = pt.g[G_SUBSPD];
    turn(E_K4, 6);
    panel_ms(150);
    shot("fm1_orbit_moon_speed");
    check("no planet, page 2, KNOB 4: MOON SPEED; the central box says what it does",
          pt.g[G_SUBSPD] == v + 6 && sc.cbox == CB_LAYER && sc.gk == 8u && (sc.shown & SH_HINT));
    v = pt.g[G_WIDTH];
    turn(E_K3, -5);
    check("... KNOB 3: WIDTH", pt.g[G_WIDTH] == v - 5);
    tap(B_HOME);

    /* PRESETS */
    turn(E_PRESET, 1);
    check("PRESETS +1: the next sound, named", ui.preset == 1u && hint_has("ORBIT BASS"));
    turn(E_PRESET, -2);
    check("PRESETS -2: back past the first", ui.preset == NPRESET - 1u);
    turn(E_PRESET, 1);

    /* held sets */
    panel_button(B_FX, 1), panel_ms(300);
    v = pt.g[G_RVMIX];
    turn(E_K1, 4);
    panel_ms(100);
    shot("fm1_hold_fx");
    check("FX held: the MIXER set, KNOB 1 = REVERB, its light, the central box", ui.layer == L_FX && pt.g[G_RVMIX] == v + 4 &&
          (btn_leds >> B_FX & 1u) && sc.cbox == CB_LAYER);
    tap(B_SEL);
    v = pt.g[G_VOLUME];
    turn(E_K4, -3);
    check("... SEL while held: page 2, KNOB 4 = VOLUME", pt.g[G_VOLUME] == v - 3);
    panel_button(B_FX, 0), panel_ms(100);
    check("... let go: the orbit's knobs (held and turned: not left open)", ui.layer == L_ORBIT && ui.latch == 0u);
    tap(B_HOME);
    tap(B_FX);
    check("FX tapped: the MIXER set stays open, its light", ui.layer == L_FX && ui.latch == L_FX && (btn_leds >> B_FX & 1u));
    tap(B_FX);
    check("... tapped again: closed", ui.layer == L_ORBIT && !(btn_leds >> B_FX & 1u));
    v = pt.g[G_POLY];
    pt.g[G_POLY] = PM_POLY;
    tap(B_CHORD);
    tap(B_SEL);
    turn(E_K3, 1);
    check("ENV tapped: the VOICE set open; page 2, KNOB 3 VOICES: POLY -> DUAL", ui.layer == L_VOICE &&
          pt.g[G_POLY] == PM_DUAL && (btn_leds >> B_CHORD & 1u));
    tap(B_HOME);
    pt.g[G_POLY] = (int16_t)v;
    tap(B_FADERBANK);
    check("LFO tapped: the MORE set instead, ENV's light off", ui.layer == L_CORNER && (btn_leds >> B_FADERBANK & 1u) &&
          !(btn_leds >> B_CHORD & 1u));
    tap(B_FADERBANK);
    tap(B_ARP);
    turn(E_K1, 1);
    check("ARP tapped: the ARP set, KNOB 1 the arpeggiator: on", ui.layer == L_ARP && pt.g[G_ARP] != ARP_OFF &&
          (btn_leds >> B_ARP & 1u));
    v = pt.g[G_TEMPO];
    turn(E_K2, 5);
    check("... KNOB 2: BPM", pt.g[G_TEMPO] == v + 5);
    turn(E_K1, -1);
    tap(B_ARP);
    check("... ARP again: closed, the arpeggiator off", ui.layer == L_ORBIT && pt.g[G_ARP] == ARP_OFF);

    /* EDIT + the keys: the 16 pads */
    turn(E_ALGO, -4);
    panel_button(B_EDIT, 1), panel_ms(60);
    note(0, 1);
    check("EDIT + key 1: SELECT planet 1", ui.sel == 1u && sounding() <= 1u);
    note(1, 1);
    check("... key 2 while key 1 is held: planet 2 added", ui.sel == 3u);
    note(1, 0), note(0, 0);
    ok = led_lvl[14] == 2u && led_lvl[15] == 2u && led_lvl[16] == 1u && led_lvl[17] == 1u;
    check("... the SELECT keys' LEDs: selected lit, the others the glow", ok);
    v = pt.c[1][CP_BMODE];
    note(5, 1), note(5, 0);
    check("EDIT + key 6: MODE of planet 2, steps", pt.c[1][CP_BMODE] == (v + 1) % BM_N && sc.ov == PG_MODE + 1u);
    panel_ms(100);
    shot("fm1_edit_mode");
    v = pt.c[0][CP_FTYPE];
    note(8, 1), note(8, 0);
    check("EDIT + key 9: FILTER of planet 1, steps", pt.c[0][CP_FTYPE] == (v + 1) % FT_N);
    note(15, 1), note(15, 0);
    check("EDIT + key 16: MUTE planet 4; its LED the glow, planet 1's lit", pt.c[3][CP_MUTE] == 1 && led_lvl[14 + 15] == 1u &&
          led_lvl[14 + 12] == 2u);
    for (ok = 1, k = 16; k < 27u; k++)
        ok &= led_lvl[14 + k] == 0u;
    check("... the keys above the pads dark, EDIT lit", ok && (btn_leds >> B_EDIT & 1u));
    note(15, 1), note(15, 0);
    panel_button(B_EDIT, 0), panel_ms(60);
    check("... EDIT let go: no hint (it was used), no key played a note", !hint_has("HOLD EDIT") && fp.note_keys == 0u);
    tap(B_EDIT);
    check("EDIT tap alone: says what holding does", hint_has("HOLD EDIT"));
    check("... the key LEDs back to the notes (none)", led_lvl[14] == 0u);

    /* the keys */
    turn(E_ALGO, -4);
    panel_ms(1600);
    note(0, 1);
    panel_ms(100);
    check("key 1: F3 (MIDI 53) at velocity 100, its LED", sounding() >= 1u && vs[syn.newest].note == 53u &&
          vs[syn.newest].vel == 100u && led_lvl[14] == 2u);
    note(0, 0);
    panel_ms(100);
    tap(B_OCTUP);
    note(26, 1);
    panel_ms(50);
    check("OCT+, key 27: G5 + 12 (MIDI 91)", vs[syn.newest].note == 91u && (btn_leds >> B_OCTUP & 1u));
    tap(B_OCTDN);
    note(26, 0);
    panel_ms(50);
    check("... let go after OCT-: its note ends", vs[syn.newest].on == 0u);

    /* buttons */
    v = pt.g[G_RETRIG];
    panel_button(B_EDIT, 1), panel_ms(40);
    tap(B_PLAY);
    panel_button(B_EDIT, 0), panel_ms(40);
    check("EDIT + PLAY: RESTART on / off", pt.g[G_RETRIG] == !v && hint_has("RESTART"));
    note(3, 1);
    panel_ms(100);
    panel_button(B_EDIT, 1), panel_ms(40);
    tap(B_SEQ);
    panel_button(B_EDIT, 0), panel_ms(40);
    check("EDIT + SEQ: all notes off, no FREEZE", !freeze && hint_has("ALL NOTES OFF"));
    note(3, 0);
    tap(B_SEQ);
    check("SEQ: FREEZE, its light", freeze && (btn_leds >> B_SEQ & 1u));
    tap(B_SEQ);
    tap(B_REC);
    check("REC: LATCH", latch == 1u && (btn_leds >> B_REC & 1u));
    tap(B_REC);
    tap(B_PLAY), panel_ms(380), tap(B_PLAY);
    check("PLAY twice, 0.5 s: BPM 120", pt.g[G_TEMPO] >= 115 && pt.g[G_TEMPO] <= 125);
    tap(B_GLOBE);
    check("GLO: FOCUS planet 1", focus == 1u && (btn_leds >> B_GLOBE & 1u));
    panel_button(B_GLOBE, 1), panel_ms(600), panel_button(B_GLOBE, 0), panel_ms(60);
    check("GLO held: FOCUS off", focus == 0u);
    tap(B_PARA);
    check("SAVE: RANDOMIZE, undo ready (its light)", ui.has_undo && (btn_leds >> B_PARA & 1u));
    v = pt.c[0][CP_G1];
    panel_button(B_PARA, 1), panel_ms(700), panel_button(B_PARA, 0), panel_ms(60);
    check("SAVE held: UNDO", hint_has("UNDO"));
    (void)v;

    /* page 4: the WAVE set (the selected planet's wavetables; the SMK-37's SCALE) */
    preset_load(8);                                /* WAVE ORBIT */
    panel_ms(200);
    turn(E_ALGO, 2);
    turn(E_SELECT, 3);
    v = pt.c[1][CP_WP1];
    turn(E_K1, 5);
    panel_ms(150);
    shot("fm1_page4_wave");
    check("page 4: the WAVE set, KNOB 1 = WT POS 1 of planet 2, its bubble", fp.page == 3u && ui.layer == L_WAVE &&
          pt.c[1][CP_WP1] == v + 5 && sc.ov == OV_WAVE);
    v = pt.c[1][CP_WO1];
    turn(E_K2, -3);
    check("... KNOB 2 = WT ORBIT 1", pt.c[1][CP_WO1] == v - 3);
    tap(B_HOME);
    check("HOME: page 1, the planet's knobs", fp.page == 0u && ui.layer == L_MAIN);
    turn(E_ALGO, -4);

    /* MASTER */
    preset_load(0);
    panel_ms(100);
    note(7, 1);
    {
        double full, half, off;
        panel_ms(1500);
        full = peak_of(500);
        panel_pot = 512, panel_ms(800);
        half = peak_of(500);
        panel_pot = 0, panel_ms(800);
        off = peak_of(300);
        printf("  info MASTER: full %.3f, half way %.3f, down %.4f\n", full, half, off);
        check("MASTER: the level (pot^2: half way a quarter), down silent, said", full > 0.05 && half < full * 0.4 &&
              half > full * 0.1 && off < 0.001 && hint_has("MASTER"));
        panel_pot = 1023, panel_ms(200);
    }
    note(7, 0);
    panel_ms(400);
    turn(E_ALGO, 2);
    panel_button(B_EDIT, 1), panel_ms(60);
    note(4, 1), note(4, 0);
    panel_ms(300);
    shot("fm1_edit_pads");
    panel_button(B_EDIT, 0), panel_ms(60);
    printf(fails ? "fm1_panel: %d FAILED\n" : "fm1_panel: all ok\n", fails);
    return fails != 0;
}
