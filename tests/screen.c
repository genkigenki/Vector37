/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Host render of the screen (the firmware's ui.c / draw.c on a simulated panel, tests/panel.h) to PPM files,
 * one per control of docs/CONTROLS.md, each checked: the touched thing shows on the screen.
 *   tests/screen OUTDIR        (tests/run_tests.sh converts them to PNG) */
#include <stdio.h>
#include <stdlib.h>
#include "panel.h"

static int fails;
static const char *dir;
static uint16_t before[240 * 240];

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
static void mark(void) { memcpy(before, fb, sizeof fb); }
static uint32_t changed(int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* pixels changed since mark() */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += fb[y * 240 + x] != before[y * 240 + x];
    return n;
}
static int hint_has(const char *s)                  /* the hint line says it */
{
    const char *m = ui.msg, *p;
    if ((int32_t)(ui.msg_until - fm1_ms) <= 0)
        return 0;
    for (; *m; m++) {
        for (p = s; *p && m[p - s] == *p; p++)
            ;
        if (!*p)
            return 1;
    }
    return 0;
}
static void tap(uint32_t b) { panel_button(b, 1); panel_ms(40); panel_button(b, 0); panel_ms(40); }
static void pad(uint32_t p) { panel_pad(p, 100); panel_ms(40); panel_pad(p, 0); panel_ms(40); }
static uint32_t count_col(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t col)   /* pixels of exactly col */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < 240; y++)
        for (x = x0 < 0 ? 0 : x0; x < x1 && x < 240; x++)
            n += fb[y * 240 + x] == col;
    return n;
}
static uint32_t ring_red(uint32_t c)                /* the red ring's pixels round planet c (of 64 places) */
{
    int32_t x, y;
    uint32_t red = 0, k;
    corner_xy(c, &x, &y);
    for (k = 0; k < 64u; k++) {
        uint32_t a = k << 26, j, hit = 0;
        for (j = 0; j < 2u; j++) {                  /* (radius 20, or a pixel in) */
            int32_t r = 20 - (int32_t)j;
            uint16_t px = fb[(y + ((cosq(a) * r) >> 15)) * 240 + x + ((sinq(a) * r) >> 15)];
            hit |= ((px >> 11) & 31) > 24 && ((px >> 5) & 63) < 16 && (px & 31) < 9;
        }
        red += hit;
    }
    return red;
}
static uint32_t path_bright(void)                   /* the path's mean brightness on the screen (its points) */
{
    uint32_t i, sum = 0;
    for (i = 0; i < sc.ntj; i++) {
        uint16_t px = fb[(sc.ty[i] >> 4) * 240 + (sc.tx[i] >> 4)];
        sum += ((px >> 11) & 31) * 2 + ((px >> 5) & 63) + (px & 31) * 2;
    }
    return sc.ntj ? sum * 10u / sc.ntj : 0u;
}
static uint32_t frame_bright(void)                  /* the orbit square's frame, its left edge */
{
    uint32_t sum = 0;
    int32_t y;
    for (y = FY0 + 20; y < FY0 + FW - 20; y += 2) {
        uint16_t px = fb[y * 240 + FX0];
        sum += ((px >> 11) & 31) * 2 + ((px >> 5) & 63) + (px & 31) * 2;
    }
    return sum;
}
static uint32_t pad_lum(uint32_t p) { uint32_t c = pad_rgb[p]; return (c >> 16 & 255) + (c >> 8 & 255) + (c & 255); }
/* the hardware pad of corner c (0..3) in pad group g (0..3) */
static uint32_t padof(uint32_t g, uint32_t c) { return g * 2u + (c & 1u) + (c & 2u ? 8u : 0u); }

int main(int argc, char **argv)
{
    uint32_t t, c, k;
    dir = argc > 1 ? argv[1] : ".";
    for (t = 0; t <= BOOT_MS; t += 400u) {          /* the boot */
        char n[32];
        boot_frame(t);
        snprintf(n, sizeof n, "boot_%04u", t);
        shot(n);
    }
    boot_frame(BOOT_MS);
    {   /* at its end the planets are where the orbit screen has them (the hand-over) */
        uint32_t ok = 1;
        int32_t x, y;
        for (c = 0; c < NC; c++) {
            corner_xy(c, &x, &y);
            ok &= count_col(x - 4, y - 4, x + 5, y + 5, RGB(2, 3, 12)) < 20u;
        }
        check("boot: it ends with the four planets in their corners", ok);
    }
    {   /* the boot's frames: only the changed tiles sent; at ~30 ms a frame, what the SPI (~375000 pixels a second) carries */
        uint32_t p0, n = 0, worst = 0, sum = 0, i;
        for (i = 0; i < 900u; i++)
            boot_th[i] = 0;
        boot_frame(0);
        for (t = 30; t <= BOOT_MS; t += 30u) {
            p0 = fb_pixels;
            boot_frame(t);
            sum += fb_pixels - p0, n++;
            worst = fb_pixels - p0 > worst ? fb_pixels - p0 : worst;
        }
        printf("  info boot: %u pixels a frame on average, %u at most (a whole screen 57600): ~%u frames a second on the SPI, "
               "%u at the slowest\n", sum / n, worst, 375000u / (sum / n), 375000u / worst);
        check("boot: frames small enough for 25 a second on average (the SPI), never below 10", 375000u / (sum / n) >= 25u &&
              375000u / worst >= 10u);
    }
    panel_init();
    fm1_in.frames = 400;
    panel_ms(600);
    shot("orbit_idle");
    check("idle: the orbit moves (the sun is drawn anew)", (mark(), panel_ms(400), changed(FX0, FY0, FX0 + FW, FY0 + FW) > 20));
    check("switched on: nothing selected, the knobs are the orbit", ui.sel == 0u && ui.layer == L_ORBIT);
    uint32_t pb_orbit = path_bright(), fr_orbit = frame_bright();
    {   /* no planet selected: K1 WARP (the path lights up, the central box shows it large) */
        int32_t w0 = pt.g[G_WARPX], g0 = pt.c[0][CP_G1];
        panel_enc(0, 6);
        panel_ms(150);
        shot("orbit_knob");
        check("K1, no planet selected: WARP; the path lit; the central box, WARP large; no planet changed",
              pt.g[G_WARPX] == w0 + 6 && pt.c[0][CP_G1] == g0 && sc.cbox == CB_LAYER && sc.gk == 1u && path_lit() > 0 &&
              (sc.shown & SH_CBOX) && sc.ov == 0u && sc.kb == 0u && (sc.shown & SH_HINT));
        panel_ms(1000);
        check("... the path dim again", path_lit() == 0);
        panel_enc(0, -6);
    }
    pad(padof(PG_SELECT, 0));                         /* SELECT: planet 1, its knobs */
    panel_ms(1600);
    check("SELECT pad: planet 1 selected, the knobs are its own", ui.sel == 1u && ui.layer == L_MAIN);
    {
        uint32_t pb = path_bright(), fr = frame_bright();
        printf("  info path brightness: orbit in charge %u, a planet selected %u; frame %u / %u\n", pb_orbit, pb, fr_orbit, fr);
        check("no planet selected: the path brighter than with a planet selected",
              pb_orbit * 10u > pb * 13u && sc.om == 0);
        check("no square drawn round the orbit (its edge: the sky), in either state", fr_orbit < 1200u && fr < 1200u);
    }
    panel_key(12, 1, 100), panel_key(16, 1, 90), panel_key(19, 1, 80);   /* a chord */
    panel_ms(1200);
    shot("orbit_playing");
    check("keys: voices sound, the newest is the big sun", vs[syn.newest].stage != 0);
    {   /* the pads: steady colours (no flicker while the orbit moves), the selected SELECT pad brighter */
        uint32_t a[16], same = 1, p;
        for (p = 0; p < 16u; p++) a[p] = pad_rgb[p];
        panel_ms(700);
        for (p = 0; p < 16u; p++) same &= a[p] == pad_rgb[p];
        check("pads: steady while the orbit moves", same);
        check("pads: the selected corner's SELECT pad brightest", pad_lum(padof(PG_SELECT, 0)) > 2u * pad_lum(padof(PG_SELECT, 1)));
        check("pads: four groups, four colours", pad_rgb[0] != pad_rgb[2] && pad_rgb[2] != pad_rgb[4] && pad_rgb[4] != pad_rgb[6]);
    }
    {   /* the selected planet: larger, a red ring */
        int32_t x, y;
        corner_xy(0, &x, &y);
        uint32_t red = 0, k2;
        for (k2 = 0; k2 < 64u; k2++) {               /* (round the planet, at the ring's radius) */
            uint32_t a = k2 << 26;
            uint16_t px = fb[(y + ((cosq(a) * 20) >> 15)) * 240 + x + ((sinq(a) * 20) >> 15)];
            red += ((px >> 11) & 31) > 24 && ((px >> 5) & 63) < 24;
        }
        check("selected planet: a red ring (its inward half: the field)", red > 20u);
    }
    mark();
    panel_enc(5, 5);                                  /* K6: cutoff of corner 1 */
    panel_ms(120);
    shot("knob_cutoff");
    check("K6: planet 1 pulses; its bubble: the overview, CUTOFF enlarged; the top line does not name it",
          !hint_has("CUTOFF") && sc.pulse == 1u && sc.kb == 6u && (sc.shown & SH_OVERVIEW) && (sc.shown & SH_BIG) &&
          sc.big == 6u && changed(FX0, FY0, FX0 + 70, FY0 + 60) > 40);
    {   /* the overview: the eight labels in their knobs' colours, inside the bubble */
        int32_t bx, by, ok = 1;
        bubble_box(0, OV_W, OV_H, &bx, &by);
        for (k = 0; k < 8u; k++)
            ok &= count_col(bx, by + 54, bx + OV_W, by + OV_H, rgbk(KCOL[k], 256)) > 8u || count_col(bx, by + 54, bx + OV_W, by + OV_H, rgbk(KCOL[k], 200)) > 8u;
        check("... all eight knobs in the overview (labels in the knobs' colours)", ok);
        check("... the enlarged item's name in K6's colour (blue) in the header", count_col(bx, by, bx + 90, by + 20, rgbk(KCOL[5], 256)) > 20u);
    }
    panel_pad(padof(PG_SELECT, 0), 100);              /* select 1 and 4: pressed together */
    panel_ms(30);
    panel_pad(padof(PG_SELECT, 3), 100);
    panel_ms(30);
    panel_pad(padof(PG_SELECT, 0), 0), panel_pad(padof(PG_SELECT, 3), 0);
    panel_ms(60);
    {
        int32_t b1 = pt.c[0][CP_BLEND], b4 = pt.c[3][CP_BLEND];
        panel_enc(3, 20);                             /* K4: blend to the right */
        panel_ms(150);
        shot("knob_blend_multi");
        check("SELECT pressed together: planets 1 + 4, K4 moves both; one bubble, a tail to each; each one's BLEND",
              ui.sel == 9u && pt.c[0][CP_BLEND] > b1 && pt.c[3][CP_BLEND] > b4 && sc.kb == 4u && (sc.shown & SH_MULTI) &&
              (sc.shown & SH_PERPLANET) && sc.tails == 9u && !hint_has("BLEND"));
    }
    {   /* several selected: each from its own value, the differences kept */
        int32_t bx = FX0 + (FW - OV_W) / 2, by = FY0 + (FW - OVM_H) / 2, ok = 1;
        uint32_t c2;
        pt.c[0][CP_CUT] = 40, pt.c[3][CP_CUT] = 80, pt.c[0][CP_G1] = 1, pt.c[3][CP_G1] = 3;
        panel_ms(300);
        panel_enc(5, 10);                             /* K6 +10 */
        panel_ms(100);
        panel_enc(0, 1);                              /* K1 one step */
        panel_ms(150);
        shot("multi_turn");
        check("several selected: K6 moves each from its own value (40, 80: 50, 90); K1 steps each (B, D: C, E)",
              pt.c[0][CP_CUT] == 50 && pt.c[3][CP_CUT] == 90 && pt.c[0][CP_G1] == 2 && pt.c[3][CP_G1] == 4);
        for (c2 = 0; c2 < NC; c2 += 3u) {             /* the tails: in each planet's colour, between it and the box */
            int32_t cx = c2 & 1u ? bx + OV_W - 1 : bx, cy = c2 & 2u ? by + OVM_H - 1 : by, px, py, n;
            corner_xy(c2, &px, &py);
            n = (int32_t)count_col(cx - 26, cy - 26, cx + 27, cy + 27, rgbk(CCOL[c2], 256));
            ok &= n > 20;
        }
        check("... a tail to each selected planet, in its colour", ok);
        panel_enc(5, -10);
    }
    panel_ms(1600);
    {
        int32_t m = pt.c[1][CP_BMODE];
        pad(padof(PG_MODE, 1));                       /* MODE: corner 2 */
        shot("pad_mode");
        check("MODE pad: corner 2's next mode; its list over the orbit", pt.c[1][CP_BMODE] == (m + 1) % BM_N &&
              hint_has("PAD MODE 2") && sc.ov == PG_MODE + 1u && sc.ovc == 1u);
    }
    {
        int32_t f = pt.c[2][CP_FTYPE], k;
        pad(padof(PG_FILTER, 2));                     /* FILTER: corner 3 */
        shot("pad_filter");
        check("FILTER pad: corner 3's next type; its list", pt.c[2][CP_FTYPE] == (f + 1) % FT_N && hint_has("PAD FILTER 3") &&
              sc.ov == PG_FILTER + 1u);
        for (k = 0; k < FT_N - 1; k++)
            pad(padof(PG_FILTER, 2));
        check("... six presses: round to where it was (the list wraps)", pt.c[2][CP_FTYPE] == f);
    }
    panel_ms(1600);
    {   /* MUTE: corner 4, no bubble: the planet flashes and collapses to a dark outline; again: it ignites */
        int32_t x, y;
        uint32_t lit1;
        corner_xy(3, &x, &y);
        panel_pad(padof(PG_MUTE, 3), 100);
        panel_ms(40);
        shot("mute_flash");
        lit1 = (uint32_t)(sc.ov != 0u);
        panel_pad(padof(PG_MUTE, 3), 0);
        panel_ms(200);
        shot("mute_collapse");
        panel_ms(500);
        shot("pad_mute");
        check("MUTE pad: corner 4 off, its pad red; no bubble, no list", pt.c[3][CP_MUTE] == 1 && hint_has("PAD MUTE 4: MUTE") &&
              (pad_rgb[padof(PG_MUTE, 3)] >> 16) > 0 && ((pad_rgb[padof(PG_MUTE, 3)] >> 8) & 255) == 0 && !lit1 &&
              sc.ov == 0u && !(sc.shown & SH_LIST));
        {   /* the planet: dark now (its body far dimmer than before) */
            uint32_t sum = 0;
            int32_t i, j;
            for (j = -3; j <= 3; j++)
                for (i = -3; i <= 3; i++) {
                    uint16_t px = fb[(y + j) * 240 + x + i];
                    sum += ((px >> 11) & 31) + ((px >> 5) & 63) / 2 + (px & 31);
                }
            printf("  info muted planet: brightness %u\n", sum);
            check("... the planet collapsed to a dark outline", sum < 49u * 12u);
        }
        panel_pad(padof(PG_MUTE, 3), 100);
        panel_ms(80);
        shot("mute_ignite");
        panel_pad(padof(PG_MUTE, 3), 0);
        check("MUTE pad again: on, igniting (no bubble)", pt.c[3][CP_MUTE] == 0 && hint_has("PAD MUTE 4: ON") && sc.ov == 0u);
        panel_ms(600);
    }
    pad(padof(PG_SELECT, 1));                         /* SELECT: corner 2, its overview (no list of corners) */
    panel_ms(200);
    shot("pad_select");
    check("SELECT pad: the planet's bubble shows its eight settings (the overview), no corner list",
          sc.ov == PG_SELECT + 1u && sc.ovc == 1u && (sc.shown & SH_OVERVIEW) && !(sc.shown & (SH_LIST | SH_BIG)));
    {
        int32_t bx, by, ok = 1;
        bubble_box(1, OV_W, OV_H, &bx, &by);
        for (k = 0; k < 8u; k++)
            ok &= count_col(bx, by + 54, bx + OV_W, by + OV_H, rgbk(KCOL[k], 256)) > 8u;
        check("... its eight labels, in the knobs' colours", ok);
    }
    pad(padof(PG_SELECT, 0));
    panel_ms(1600);
    mark();
    {   /* the mod wheel: MOON SPEED, gliding there on its first move, then following; its place is the value */
        int32_t s0 = pt.g[G_SUBSPD], z0 = pt.g[G_SUBSZ], r0 = sc.mr, k2;
        for (k2 = 0; k2 < 100; k2++) {              /* (resting, a count of wobble: nothing) */
            panel_wheel(1, 615u - (uint32_t)(k2 & 1));
            panel_ms(10);
        }
        check("mod wheel at rest, wobbling: the moon's speed stays the sound's", pt.g[G_SUBSPD] == s0 && ui.mw_mode == 0u);
        panel_wheel(1, 590), panel_ms(20), panel_wheel(1, 540);
        panel_ms(150);
        shot("modwheel_moon");
        check("mod wheel moved: MOON SPEED glides to it; the central box; the moon's ring lit", ui.mw_mode == 1u &&
              sc.cbox == CB_MOON && moon_lit() == 256 && hint_has("MOON SPEED"));
        panel_ms(400);
        check("... arrived: the moon's speed is the wheel's place; the moon's distance unchanged",
              ui.mw_mode == 2u && pt.g[G_SUBSPD] == ui.mw_last && ui.mw_last > s0 && pt.g[G_SUBSZ] == z0 && sc.mr == r0);
        panel_ms(1500);
        check("... the wheel left there: the value stays (no spring back), the ring dim again", pt.g[G_SUBSPD] == ui.mw_last && moon_lit() == 0);
        {
            int32_t v0 = pt.g[G_SUBSPD];
            panel_wheel(1, 560);
            panel_ms(100);
            check("... and follows the wheel directly", pt.g[G_SUBSPD] < v0 && pt.g[G_SUBSPD] == ui.mw_last);
        }
        {   /* the wheel at rest, its reading jittering +-2 for 5 s: nothing changes, nothing shows */
            uint32_t k3, seed = 12345u, popped = 0;
            int16_t v0;
            panel_ms(2500);                          /* (the box gone) */
            v0 = pt.g[G_SUBSPD];
            for (k3 = 0; k3 < 500u; k3++) {
                seed = seed * 1103515245u + 12345u;
                panel_wheel(1, 560u + (seed >> 16) % 5u - 2u);
                panel_ms(10);
                popped |= sc.cbox == CB_MOON || moon_lit() != 0 || (ui.touch.kind == TK_WHEEL && (int32_t)(ui.touch.until - fm1_ms) > 0);
            }
            check("the mod wheel at rest, its reading jittering +-2 for 5 s: MOON SPEED unchanged, no box, no lit ring",
                  pt.g[G_SUBSPD] == v0 && !popped);
            panel_wheel(1, 547);                     /* (a real move: 13 steps) */
            panel_ms(100);
            check("... a real move of 10 or more: it changes and shows", pt.g[G_SUBSPD] != v0 && sc.cbox == CB_MOON && moon_lit() > 0);
            panel_wheel(1, 560);
            panel_ms(2500);
        }
        {   /* a fader at rest jittering +-2 for 5 s: no glide, no box */
            uint32_t k3, seed = 777u, popped = 0;
            int16_t v0;
            panel_fader(2, 64);
            panel_ms(2500);
            v0 = pt.g[G_RATE];
            for (k3 = 0; k3 < 500u; k3++) {
                seed = seed * 1103515245u + 12345u;
                panel_fader(2, 64u + (seed >> 16) % 5u - 2u);
                panel_ms(10);
                popped |= sc.fader != 0u;
            }
            check("a fader at rest jittering +-2 for 5 s: its value unchanged, no box", pt.g[G_RATE] == v0 && !popped);
        }
    }
    {   /* the selected planet tapped again: none, the knobs are the orbit; K5 the moon's distance, K6 its speed */
        int32_t z0 = pt.g[G_SUBSZ], s0 = pt.g[G_SUBSPD], g0 = pt.c[0][CP_G1];
        pad(padof(PG_SELECT, 0));
        check("... the orbit lighting up, gliding (~0.2 s)", sc.om > 0 && sc.om < 256);
        panel_ms(100);
        check("tap the selected planet's SELECT pad: none selected, the knobs are the orbit", ui.sel == 0u &&
              ui.layer == L_ORBIT && hint_has("ORBIT"));
        panel_enc(4, 6);
        panel_ms(150);
        shot("orbit_moon_dist");
        check("... K5: MOON DIST, the moon's ring lit, the central box saying what it does; no planet changed",
              (sc.shown & SH_HINT) && pt.g[G_SUBSZ] == z0 + 6 &&
              pt.c[0][CP_G1] == g0 && moon_lit() == 256 && sc.cbox == CB_LAYER && sc.gk == 5u && pt.g[G_SUBSPD] == s0);
        {   /* K6: SPIN, the whole path turning on and on (the moon's speed: the wheel's alone) */
            uint32_t sp0, h0;
            panel_enc(5, 20);
            panel_ms(200);
            sp0 = ost.spin, h0 = sc.thash;
            panel_ms(600);
            shot("orbit_spin");
            check("... K6: SPIN, the path turns on and on (drawn anew), the moon's speed untouched; says what it does",
                  pt.g[G_SPIN] == 20 && ost.spin != sp0 && sc.thash != h0 && pt.g[G_SUBSPD] == s0 && (sc.shown & SH_HINT) &&
                  sc.gk == 6u);
            {
                uint32_t p0 = fb_pixels, n0 = fstat_n, per, fps;
                panel_key(12, 1, 100), panel_key(19, 1, 100);
                panel_ms(3000);
                per = (fb_pixels - p0) / (fstat_n - n0 ? fstat_n - n0 : 1u), fps = 375000u / (per ? per : 1u);
                printf("  info SPIN while playing: %u pixels a second, %u field frames, the SPI carries ~%u a second\n",
                       (fb_pixels - p0) / 3u, fstat_n - n0, fps > 33u ? 33u : fps);
                check("... SPIN while playing: 25 frames a second or more", fps >= 25u && fstat_n - n0 >= 75u);
                panel_key(12, 0, 0), panel_key(19, 0, 0);
            }
            panel_enc(5, -20);
            panel_ms(300);
            check("... SPIN at the centre: the path stands", pt.g[G_SPIN] == 0 && (sp0 = ost.spin, panel_ms(300), ost.spin == sp0));
        }
        panel_ms(1600);
        pad(padof(PG_SELECT, 0));
        panel_ms(1600);
        check("... SELECT again: planet 1, its knobs", ui.sel == 1u && ui.layer == L_MAIN);
    }
    {   /* a fader at rest, its reading wobbling by a step: no glide starts, the path stays the sound's */
        uint32_t k, h0, moved = 0;
        int32_t s0 = pt.g[G_SIZE], o0;
        panel_ms(400);
        h0 = sc.thash, o0 = ost.p.size;
        for (k = 0; k < 200u; k++) {
            panel_fader(1, 19u + (k * 7u) % 3u);    /* 19, 20, 21 */
            panel_ms(10);
            moved |= sc.thash != h0 || ost.p.size != o0;
        }
        check("a resting fader wobbling a step: no glide, the path stays still", !moved && pt.g[G_SIZE] == s0 && ui.fad_mode[1] == 0u);
    }
    {   /* fader 2 (SIZE) moved: the value glides to the fader (~0.4 s), then follows it */
        int32_t s0 = pt.g[G_SIZE], mid;
        panel_fader(1, 60), panel_ms(20), panel_fader(1, 90);
        panel_ms(180);
        mid = pt.g[G_SIZE];
        shot("fader_glide");
        check("F2 moved: SIZE glides towards the fader", ui.fad_mode[1] == 1u && mid < s0 && mid > 90);
        panel_ms(320);
        shot("fader_size");
        check("... within 0.5 s it has arrived; it follows from now on", pt.g[G_SIZE] == 90 && ui.fad_mode[1] == 2u && hint_has("F2 SIZE"));
        panel_fader(1, 60);
        panel_ms(60);
        check("... and follows the fader directly", pt.g[G_SIZE] == 60);
    }
    {   /* fader 4 (MORPH): the first move glides there too */
        panel_fader(3, 60), panel_ms(20), panel_fader(3, 40);
        panel_ms(600);
        shot("fader_morph");
        check("F4 MORPH: glided to the fader, the path drawn anew", ui.fad_mode[3] == 2u && pt.g[G_MORPH] == -24 && hint_has("F4 MORPH"));
    }
    {   /* F1's detents: a plateau per family (exact), short morphs between them */
        static const uint8_t MID[5] = {13, 38, 64, 90, 115};
        uint32_t f, r, ok = 1, mono = 1;
        int32_t prev = -1, maxstep = 0;
        for (f = 0; f < 5u; f++)
            for (r = MID[f] - 7u; r <= MID[f] + 7u; r++)
                ok &= family_of_raw(r) == (int32_t)f * FAM_STEP;
        for (r = 0; r < 128u; r++) {
            int32_t v = family_of_raw(r);
            if (prev >= 0) {
                mono &= v >= prev;
                maxstep = v - prev > maxstep ? v - prev : maxstep;
            }
            prev = v;
        }
        check("F1 detents: each family a plateau of 15 fader steps or more, the figure exactly that family", ok);
        check("... between them a continuous morph (rising, at most 6 of 124 a fader step)", mono && maxstep <= 6 &&
              family_of_raw(0) == 0 && family_of_raw(127) == 124);
    }
    {   /* fader 1 (FAMILY): slowly through every family, each a picture */
        static const uint8_t AT[5] = {0, 32, 64, 96, 127};
        uint32_t f, r = 60;
        panel_fader(0, 58), panel_ms(20), panel_fader(0, 56);
        panel_ms(500);
        for (f = 0; f < 5u; f++) {
            char n[24];
            while (r != AT[f]) {
                r = r < AT[f] ? r + 2u > AT[f] ? AT[f] : r + 2u : r < AT[f] + 2u ? AT[f] : r - 2u;
                panel_fader(0, r);
                panel_ms(30);
            }
            if (f == 2u) {
                check("F1 moving: the central box, the families as a scrolling list; on STAR's plateau: exactly STAR",
                      sc.cbox == CB_FAM && (sc.shown & SH_FAMLIST) && pt.g[G_SHAPE] == 2 * FAM_STEP);
                shot("family_list");
            }
            panel_ms(400);
            snprintf(n, sizeof n, "shape_%u", (unsigned)f);
            shot(n);
        }
        check("F1 FAMILY: Lissajous .. ADSR by the fader", pt.g[G_SHAPE] == 124 && ui.fad_mode[0] == 2u && hint_has("F1 FAMILY ADSR"));
        panel_fader(0, 0);
        panel_ms(300);
    }
    panel_ms(1600);
    {   /* SEQ: FREEZE, the orbit stands; again: it moves on */
        int32_t x0, y0;
        tap(B_SEQ);
        panel_ms(100);
        x0 = sc.sx, y0 = sc.sy;
        panel_ms(500);
        shot("freeze");
        check("SEQ tap: FREEZE, the sun stands still", freeze == 1u && sc.sx == x0 && sc.sy == y0 && hint_has("FREEZE"));
        tap(B_SEQ);
        panel_ms(500);
        check("SEQ again: it moves on", freeze == 0u && (sc.sx != x0 || sc.sy != y0));
    }
    {   /* held buttons: their knob set, shown below */
        static const struct { uint8_t b, layer; const char *name; } H[] = {
            {B_CHORD, L_VOICE, "hold_chord_voice"}, {B_FX, L_FX, "hold_fx_effects"},
            {B_FADERBANK, L_CORNER, "hold_faderbank_corner"}, {B_ARP, L_ARP, "hold_arp"}};
        for (k = 0; k < sizeof H / sizeof H[0]; k++) {
            uint32_t shape = (uint32_t)pt.g[G_SHAPE], poly = (uint32_t)pt.g[G_POLY], arp = (uint32_t)pt.g[G_ARP];
            char w[64];
            panel_button(H[k].b, 1);
            panel_ms(300);
            panel_enc(1, 1);                          /* (a knob turned while held: no tap action on release) */
            panel_ms(100);
            shot(H[k].name);
            snprintf(w, sizeof w, "%s held: knob set %s", H[k].name, LAYERS[H[k].layer].name);
            check(w, ui.layer == H[k].layer && sc.cbox == CB_LAYER && (sc.shown & SH_CBOX) && sc.gk == 2u);
            panel_button(H[k].b, 0);
            panel_ms(100);
            check("... let go: the corner knobs again, no tap action", ui.layer == L_MAIN && shape == (uint32_t)pt.g[G_SHAPE] &&
                  poly == (uint32_t)pt.g[G_POLY] && arp == (uint32_t)pt.g[G_ARP]);
        }
    }
    tap(B_CHORD);
    check("CHORD tap: POLY -> DUAL", pt.g[G_POLY] == PM_DUAL && hint_has("DUAL"));
    tap(B_CHORD), tap(B_CHORD);
    tap(B_GLOBE);
    panel_ms(300);
    tap(B_GLOBE);
    panel_ms(400);
    shot("focus_corner2");
    check("GLOBE: focus on corner 2, the sun on its planet", focus == 2u && sc.sx > FX0 + FW - 20 && sc.sy < FY0 + 20);
    panel_button(B_GLOBE, 1);
    panel_ms(700);
    panel_button(B_GLOBE, 0);
    panel_ms(60);
    check("GLOBE held: focus off", focus == 0u);
    {   /* PARA: randomize, held: undo */
        int16_t g1 = pt.c[sel_first()][CP_G1], cut = pt.c[sel_first()][CP_CUT];
        pad(padof(PG_SELECT, 1));
        g1 = pt.c[1][CP_G1], cut = pt.c[1][CP_CUT];
        tap(B_PARA);
        panel_ms(200);
        shot("random");
        check("PARA: the selected corner new", hint_has("RANDOM") && (pt.c[1][CP_G1] != g1 || pt.c[1][CP_CUT] != cut));
        panel_button(B_PARA, 1);
        panel_ms(800);
        panel_button(B_PARA, 0);
        panel_ms(60);
        check("PARA held: undo", pt.c[1][CP_G1] == g1 && pt.c[1][CP_CUT] == cut && hint_has("UNDO"));
    }
    tap(B_REPEAT);
    check("REPEAT: retrigger on, its light", pt.g[G_RETRIG] == 1 && (btn_leds >> B_REPEAT & 1u));
    tap(B_REC);
    check("REC: latch on", latch == 1u);
    tap(B_REC);
    tap(B_ARP);
    check("ARP tap: the arpeggiator on", pt.g[G_ARP] != 0 && hint_has("ARP"));
    tap(B_ARP);
    tap(B_PLAY), panel_ms(420), tap(B_PLAY);
    check("PLAY: two taps 0.5 s apart: about 120 BPM", pt.g[G_TEMPO] > 100 && pt.g[G_TEMPO] < 140);
    tap(B_OCTUP);
    check("OCT+: an octave up", ui.oct == 1 && hint_has("OCTAVE"));
    tap(B_OCTDN);
    panel_key(12, 0, 0), panel_key(16, 0, 0), panel_key(19, 0, 0);
    tap(B_PATCH);
    check("PATCH: all notes off", hint_has("ALL NOTES OFF"));
    tap(B_KNOBBANK);                                  /* the guide: K1 scrolls it */
    shot("guide_0");
    check("KNOB BANK: the guide opens, the top line says K1 scrolls", ui.help == 1u && ui.mscroll == 0);
    for (k = 1; k <= 5u; k++) {
        char n[24];
        panel_enc(0, 9);
        panel_ms(100);
        snprintf(n, sizeof n, "guide_%u", k);
        shot(n);
    }
    check("K1: the guide scrolls", ui.mscroll > 800);
    panel_enc(0, -200);
    panel_ms(100);
    check("... back to the top, no further", ui.mscroll == 0);
    tap(B_KNOBBANK);
    panel_ms(100);
    check("KNOB BANK again: closed", ui.help == 0u);
    for (k = 0; k < NPRESET; k++) {                   /* < >: every sound, playing */
        char n[40];
        tap(B_RIGHT);
        panel_key(12, 1, 100), panel_key(19, 1, 90), panel_key(24, 1, 90);
        panel_ms(1500);
        snprintf(n, sizeof n, "preset_%u", (unsigned)(ui.preset + 1u));
        shot(n);
        panel_key(12, 0, 0), panel_key(19, 0, 0), panel_key(24, 0, 0);
        panel_ms(300);
    }
    check("> through all sounds: back to the first", ui.preset == 0u);
    {   /* planet editing: the zoom, the bubble, the audition */
        uint32_t v, sounding = 0, k;
        panel_key(12, 0, 0), panel_key(16, 0, 0), panel_key(19, 0, 0), panel_key(23, 0, 0);
        panel_ms(1500);
        panel_pad(padof(PG_SELECT, 3), 100);
        panel_ms(45);
        check("SELECT pad: the planet zooms up (animating)", sc.zoom[3] > 0 && sc.zoom[3] < 256);
        panel_pad(padof(PG_SELECT, 3), 0);
        panel_ms(400);
        check("... zoomed, the other one small again", sc.zoom[3] == 256 && sc.zoom[1] == 0 && ui.sel == 8u);
        panel_ms(1500);                            /* (the SELECT pad's own audition ends) */
        for (k = 0; k < 6u; k++) {                 /* K6 turned, no key held: planet 4 alone, held while it turns */
            panel_enc(5, 2);
            panel_ms(80);
        }
        for (v = 0; v < NVOICE; v++)
            sounding |= vs[v].stage && vs[v].note == 48u;
        shot("audition");
        check("K6, no key held: AUDITION, planet 4 alone on C3; its value in its bubble", ui.aud.on && solo == 8u && sounding && sc.kb == 6u);
        {
            int32_t ax, ay;
            spk_xy(3, &ax, &ay);
            check("... a speaker beside planet 4 (no AUDITION text)", (sc.shown & SH_SPEAKER) &&
                  count_col(ax, ay, ax + 11, ay + 14, rgbk(CCOL[3], 256)) > 15u);
        }
        panel_ms(700);
        check("... released 0.5 s after the last change", !ui.aud.on);
        panel_ms(1600);
        {
            int32_t ax, ay;
            spk_xy(3, &ax, &ay);
            check("... the speaker gone, its place the sky again", !(sc.shown & SH_SPEAKER) &&
                  count_col(ax, ay, ax + 11, ay + 14, rgbk(CCOL[3], 256)) == 0u);
        }
        check("... then all four corners again", solo == 0u);
        panel_key(12, 1, 100);
        panel_ms(100);
        panel_enc(5, 2);
        panel_ms(100);
        check("a key held: no audition", !ui.aud.on && solo == 0u);
        panel_key(12, 0, 0);
        panel_ms(800);
        panel_button(B_FADERBANK, 1);
        panel_ms(300);
        panel_enc(7, -1);                          /* (MORE: K8 AUDITION off) */
        panel_ms(100);
        panel_button(B_FADERBANK, 0);
        panel_ms(100);
        panel_enc(5, 2);
        panel_ms(100);
        check("AUDITION off (FADER BANK held, K8): changes are silent", pt.g[G_AUDITION] == 0 && !ui.aud.on);
        panel_button(B_FADERBANK, 1);
        panel_ms(300);
        panel_enc(7, 1);
        panel_ms(100);
        panel_button(B_FADERBANK, 0);
        panel_ms(600);
        panel_key(12, 1, 100), panel_key(16, 1, 100), panel_key(19, 1, 100), panel_key(23, 1, 100);
    }
    {   /* the planets reach over the square's edge, drawn whole; shrinking, their place is the sky again */
        uint32_t whole = 1, gone = 1, c2;
        for (c2 = 0; c2 < NC; c2++) {
            int32_t x, y, out = 0, k2;
            pad(padof(PG_SELECT, c2));
            panel_ms(2000);                          /* (the bubble gone: the planet alone) */
            printf("  info planet %u: ring %u of 64\n", (unsigned)c2, ring_red(c2));
            whole &= ring_red(c2) >= 48u;          /* (clipped at the square: under 40; the moon may cross it) */
            corner_xy(c2, &x, &y);
            for (k2 = 0; k2 < 6; k2++) {             /* outwards, beyond the square's edge: the ring, the glow */
                int32_t px = c2 & 1u ? x + 15 + k2 : x - 15 - k2;
                out |= px < FX0 || px >= FX0 + FW ? fb[y * 240 + px] != C_BG : 0;
            }
            whole &= out;
            if (c2 == 3u)
                shot("planet_edge");
        }
        check("every planet zoomed: its red ring whole, over the square's edge (not clipped)", whole);
        pad(padof(PG_SELECT, 0));
        panel_ms(2000);
        for (c2 = 1; c2 < NC; c2++)
            gone &= ring_red(c2) == 0u;
        check("... picked another: the old ring gone (the margin drawn again)", gone && ring_red(0) >= 48u);
    }
    {   /* the moon's trail: its real places every frame, joined; a turn long, at least 0.5 s */
        int16_t s0 = pt.g[G_SUBSPD], z0 = pt.g[G_SUBSZ], r0 = pt.g[G_RATE];
        uint32_t k2, k, span[2], lit[2];
        pt.g[G_RATE] = 1;                              /* (the sun calm: 16 beats a round) */
        int32_t maxseg[2];
        pt.g[G_SUBSZ] = 90;
        for (k2 = 0; k2 < 2u; k2++) {
            uint32_t g = 0;
            pt.g[G_SUBSPD] = k2 ? 127 : 8;
            panel_ms(k2 ? 2000 : 9000);
            shot(k2 ? "moon_fast" : "moon_slow");
            span[k2] = mt_n ? MT_AGE(mt_n - 1u) : 0u;
            maxseg[k2] = 0;
            for (k = 1; k < mt_n; k++) {               /* (the longest piece between two places, 1/16 pixels) */
                int32_t dx = mt[mt_idx(k)].x - mt[mt_idx(k - 1u)].x, dy = mt[mt_idx(k)].y - mt[mt_idx(k - 1u)].y;
                dx = dx < 0 ? -dx : dx, dy = dy < 0 ? -dy : dy;
                if (dx > 640 || dy > 640 || MT_AGE(k) > 8000u)
                    continue;                          /* (a jump of the path itself: not drawn; before the sun was calm) */
                maxseg[k2] = dx > maxseg[k2] ? dx : dy > maxseg[k2] ? dy : maxseg[k2];
            }
            for (k = 2; k < 8u && k < mt_n; k++) {     /* (just behind the moon: drawn, green) */
                int32_t x = mt[mt_idx(k)].x >> 4, y = mt[mt_idx(k)].y >> 4, dx, dy;
                uint32_t best = 0;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++) {
                        uint32_t gg = (fb[(y + dy) * 240 + x + dx] >> 5) & 63;
                        best = gg > best ? gg : best;
                    }
                g += best;
            }
            lit[k2] = g;
        }
        printf("  info the moon's trail: slow %u ms back, longest piece %d/16 px, green %u; fast %u ms back, green %u\n",
               span[0], maxseg[0], lit[0], span[1], lit[1]);
        check("the moon's trail, slow: smooth (no piece longer than 4 pixels), lit right behind the moon",
              (sc.shown & SH_TRAIL) && maxseg[0] <= 64 && lit[0] > 6u * 25u);
        check("... fast: at least 0.5 s of its way, lit right behind it", span[1] >= 480u && lit[1] > 6u * 25u);
        pt.g[G_SUBSPD] = s0, pt.g[G_SUBSZ] = z0, pt.g[G_RATE] = r0;
        panel_ms(500);
    }
    {   /* the moon and its trail reach past the square: drawn, not cut at its edge */
        int32_t mx0 = sc.mx, my0 = sc.my, sx0 = sc.sx, sy0 = sc.sy;
        uint16_t px;
        sc.mx = FX0 - 5, sc.my = FY0 + FW / 2, sc.sx = FX0 + 10, sc.sy = FY0 + FW / 2;
        field_rect(SX0, SY0, SX1, SY1);
        px = fb[sc.my * 240 + sc.mx];
        check("the moon outside the square's edge: drawn whole (the sky clips, not the square)", ((px >> 5) & 63) > 40);
        sc.mx = mx0, sc.my = my0, sc.sx = sx0, sc.sy = sy0;
        panel_ms(100);
    }
    {   /* the screen's cost: pixels sent per second while playing (12 MHz SPI: ~375000 a second) */
        uint32_t p0 = fb_pixels;
        fstat_n = fstat_full = fstat_area = 0;
        panel_key(12, 1, 100), panel_key(16, 1, 100), panel_key(19, 1, 100), panel_key(23, 1, 100);
        panel_ms(3000);
        printf("  info pixels sent while playing: %u a second (the SPI carries ~375000); field frames %u (%u whole), "
               "%u pixels each\n", (fb_pixels - p0) / 3u, fstat_n, fstat_full, fstat_area / (fstat_n ? fstat_n : 1u));
        check("partial redraws: ~33 field frames a second, the SPI able to carry 30 or more (not over 90 % of it)",
              fstat_n >= 85u && (fb_pixels - p0) / 3u < 337500u && 375000u / ((fb_pixels - p0) / (fstat_n ? fstat_n : 1u)) >= 30u);
    }
    {   /* a knob morphing the path the whole time (the path drawn anew every frame, where it moved) */
        uint32_t p0, k;
        panel_button(B_SEQ, 1);
        panel_ms(300);
        p0 = fb_pixels;
        fstat_n = fstat_full = fstat_area = 0;
        for (k = 0; k < 30u; k++) {                   /* (fader 4: MORPH up and down) */
            panel_fader(3, k < 15u ? 40u + k * 5u : 110u - (k - 15u) * 5u);
            panel_ms(100);
        }
        panel_button(B_SEQ, 0);
        printf("  info while MORPH turns: %u pixels a second; field frames %u (%u whole), %u pixels each\n", (fb_pixels - p0) / 3u,
               fstat_n, fstat_full, fstat_area / (fstat_n ? fstat_n : 1u));
        {   /* (the device: frames as fast as the SPI carries them, at most ~33 a second) */
            uint32_t per = (fb_pixels - p0) / (fstat_n ? fstat_n : 1u), fps = 375000u / (per ? per : 1u);
            printf("  info while MORPH turns: about %u frames a second on the SPI\n", fps > 33u ? 33u : fps);
            check("a fader morphing the whole path: 16 frames a second or more", fps >= 16u);
        }
    }
    {   /* TRS MIDI OUT: the keys as played (the note off: the note sent, whatever the octave since), the wheels; ARP held,
         * K8: OFF / ON */
        uint32_t i, n, bends = 0, last_bend = 0, cc1 = 0;
        for (i = 0; i < 37u; i++)
            panel_key(i, 0, 0);
        panel_wheel(0, 540), panel_wheel(1, 615);
        panel_ms(2500);                               /* (the mod wheel's smoothed reading settles at 0) */
        n = (uint32_t)clamp(60 + 12 * ui.oct, 0, 127);
        panel_trs_n = 0;
        panel_key(12, 1, 99);
        panel_ms(60);
        check("TRS: a key: note on, channel 1, its velocity", panel_trs_n == 3u && panel_trs[0] == 0x90u &&
              panel_trs[1] == n && panel_trs[2] == 99u);
        tap(B_OCTUP);
        panel_key(12, 0, 0);
        panel_ms(60);
        check("TRS: let go after OCT+: the note off of the note sent", panel_trs_n == 6u && panel_trs[3] == 0x80u &&
              panel_trs[4] == n && panel_trs[5] == 0u);
        tap(B_OCTDN);
        panel_trs_n = 0;
        panel_wheel(0, 640);
        panel_ms(200);
        panel_wheel(0, 540);
        panel_ms(200);
        for (i = 0; i + 2u < panel_trs_n && i + 2u < sizeof panel_trs; i += 3u)
            if (panel_trs[i] == 0xE0u)
                bends++, last_bend = panel_trs[i + 1] | panel_trs[i + 2] << 7;
        check("TRS: the pitch wheel: pitch bend, back to the centre at rest", bends >= 2u && last_bend == 8192u);
        panel_trs_n = 0;
        panel_wheel(1, 500);
        panel_ms(400);
        for (i = 0; i + 2u < panel_trs_n && i + 2u < sizeof panel_trs; i += 3u)
            cc1 += panel_trs[i] == 0xB0u && panel_trs[i + 1] == 1u;
        check("TRS: the mod wheel: CC 1", cc1 >= 1u && panel_trs_n == cc1 * 3u);
        panel_wheel(1, 615);
        panel_ms(400);
        panel_button(B_ARP, 1);
        panel_ms(300);
        panel_enc(7, -1);
        panel_ms(100);
        check("ARP held, K8: TRS MIDI OUT OFF (shown)", !trs_out && ui.layer == L_ARP && sc.gk == 8u);
        shot("hold_arp_trs");
        panel_button(B_ARP, 0);
        panel_ms(100);
        panel_trs_n = 0;
        panel_key(12, 1, 99), panel_ms(60), panel_key(12, 0, 0), panel_ms(60);
        check("TRS MIDI OUT OFF: nothing sent", panel_trs_n == 0u);
        panel_button(B_ARP, 1);
        panel_ms(300);
        panel_enc(7, 1);
        panel_ms(100);
        panel_button(B_ARP, 0);
        panel_ms(100);
        panel_key(12, 1, 99), panel_ms(60), panel_key(12, 0, 0), panel_ms(60);
        check("... ON again: sent", trs_out && panel_trs_n == 6u);
    }
    printf(fails ? "screen: %d FAILED\n" : "screen: all ok\n", fails);
    return fails != 0;
}
