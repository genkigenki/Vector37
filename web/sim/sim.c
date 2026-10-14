/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Vector37 in the browser: the firmware's engine, UI and screen (the same sources) on the simulated panel
 * (tests/panel.h), compiled to WebAssembly (web/sim/build.sh). The page's AudioWorklet calls sim_render(n): the
 * device clock runs with the audio, the main loop (input, LEDs, screen) every 15 ms of it. */
#define EXPORT __attribute__((used, visibility("default")))
#define MK_VERSION "sim"
#include "../../tests/panel.h"

static float out_l[1024], out_r[1024];

EXPORT void sim_init(void) { panel_init(); }
EXPORT float *sim_out_l(void) { return out_l; }
EXPORT float *sim_out_r(void) { return out_r; }
EXPORT uint32_t sim_rate(void) { return FS; }
EXPORT void sim_render(uint32_t n) { panel_render(out_l, out_r, n > 1024u ? 1024u : n); }   /* n frames */
EXPORT uint16_t *sim_fb(void) { return fb; }
EXPORT uint32_t sim_draws(void) { return fb_draws; }
EXPORT uint32_t *sim_pads(void) { return pad_rgb; }
EXPORT uint32_t sim_leds(void) { return btn_leds; }
EXPORT uint32_t sim_ui(void) { return (uint32_t)ui.layer << 16 | (uint32_t)ui.sel << 8 | ui.preset; }   /* (tests) */
EXPORT uint32_t sim_voices(void)
{
    uint32_t v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += vs[v].stage != 0;
    return n;
}
EXPORT int32_t sim_param(uint32_t corner, uint32_t k)   /* corner 1..4: its parameter k; 0: global k */
{
    return corner ? pt.c[(corner - 1u) % NC][k % CP_N] : pt.g[k % G_COUNT];
}
EXPORT void sim_pad(uint32_t pad, uint32_t vel) { panel_pad(pad, vel); }
EXPORT void sim_key(uint32_t k, uint32_t down) { panel_key(k, down, 100); }
EXPORT void sim_button(uint32_t b, uint32_t down) { panel_button(b, down); }
EXPORT void sim_enc(uint32_t e, int32_t steps) { panel_enc(e, steps); }
EXPORT void sim_fader(uint32_t i, uint32_t v) { panel_fader(i, v); }
EXPORT void sim_wheel(uint32_t i, uint32_t raw) { panel_wheel(i, raw); }
EXPORT void sim_midi(uint32_t st, uint32_t d1, uint32_t d2) { panel_midi(st, d1, d2); }
/* the controls' places for the page: fader i (0..3) where the sound's value sits (0..127, the middle of the fader
 * travel that gives it); 4: the mod wheel's slider (0..130) for MOON SPEED. The page moves its handles there while
 * they are not held, so a new sound's values show and a touch goes on from them (the firmware's glide unchanged) */
EXPORT int32_t sim_ctl(uint32_t i)
{
    if (i < 4u) {
        int32_t v = pt.g[FADER_G[i]], bd = 1 << 30, lo = 0, hi = 0, r;
        for (r = 0; r < 128; r++) {
            int32_t d = fader_value(i, (uint32_t)r) - v;
            d = d < 0 ? -d : d;
            if (d < bd)
                bd = d, lo = hi = r;
            else if (d == bd && r == hi + 1)
                hi = r;
        }
        return (lo + hi) / 2;
    }
    if (i == 4u)                                   /* (the slider: raw 615 - s; 12 steps of dead band at the bottom) */
        return 12 + (pt.g[G_SUBSPD] * 118 + 63) / 127;
    return 0;
}
