/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* A simulated SMK-37 Pro for the host: the firmware's engine, UI and screen (the same sources) against a fake
 * panel (pads, keys, knobs, faders, wheels, buttons, LEDs, a 240 x 240 frame buffer). Used by tests/screen.c,
 * tools/anim.c and the web simulator (web/sim/sim.c). The device's clock runs with the audio: panel_render()
 * renders samples, and every 15 ms of them the main loop runs (input, LEDs, screen). */
#include <stdint.h>
#include <string.h>
#define memset mk_memset
#define memcpy mk_memcpy
#define memcmp mk_memcmp
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "mk_tables.h"
#ifndef MK_VERSION
#define MK_VERSION "host"
#endif
#define BRAND_NAME "VECTOR37"
#define RING_PUBLISH() ((void)0)
static uint32_t fm1_ms;

/* ---- the panel ---- */
#define FM1_NENC 8u
#define FM1_NBTN 18u
#define FM1_OCT_BOTH 0x30u
static struct { uint8_t fader[4]; uint32_t frames, buttons; uint64_t keys; uint16_t wheel[2]; uint8_t key_vel[37]; } fm1_in;
static uint32_t btn_pressed, btn_released;
static int16_t enc_steps[8];
#define PADQ 64u
static uint16_t padq[PADQ];
static uint32_t padq_w, padq_r;
static int32_t smk_pad_take(void) { return padq_r == padq_w ? -1 : padq[padq_r++ % PADQ]; }
static uint32_t fm1_input_edges(uint32_t *r)
{
    uint32_t p = btn_pressed;
    *r = btn_released;
    btn_pressed = btn_released = 0;
    return p;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = enc_steps[e & 7u];
    enc_steps[e & 7u] = 0;
    return s;
}
static uint32_t btn_leds;
static void fm1_led_key(uint32_t id, int on)
{
    if (id < 32u)
        btn_leds = on ? btn_leds | 1u << id : btn_leds & ~(1u << id);
}
static uint32_t pad_rgb[16];
static void padled_set(uint32_t p, uint8_t r, uint8_t g, uint8_t b)
{
    if (p < 16u)
        pad_rgb[p] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(uint32_t ms) { fm1_ms += ms; }
#define MQ 64u
static uint32_t midi_in_q[MQ], mi_w, mi_r;

/* ---- the screen: RGB565 ---- */
static uint16_t fb[240 * 240];
static uint32_t fb_draws, fb_pixels;           /* (pixels sent: the cost on the SPI) */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fb_draws++;
    fb_pixels += w * h;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fb_draws++;
    fb_pixels += w * h;
}

#include "../firmware/src/gfx.c"
#include "../firmware/src/synth.h"
#include "../firmware/src/orbit.c"
#include "../firmware/src/engine.c"
#include "../firmware/src/analog.c"
#include "../firmware/src/fx.c"
#include "../firmware/src/mix.c"
#include "../firmware/src/params.c"
#include "../firmware/src/presets.c"
/* the TRS MIDI OUT jack: a UART that takes a byte at once; the bytes in panel_trs (tests/screen.c) */
static uint8_t panel_trs[4096];
static uint32_t panel_trs_n;
static void panel_trs_put(uint32_t b)
{
    if (panel_trs_n < sizeof panel_trs)
        panel_trs[panel_trs_n] = (uint8_t)b;
    panel_trs_n++;
}
#define SMO_TX_DONE() 1u
#define SMO_TX_PUT(b) panel_trs_put(b)
#include "../firmware/src/smk_midi_out.h"
static struct { uint8_t bright, console, preset; } settings = {5, 0, 0};
static void settings_save(void) {}
#include "../firmware/src/ui.c"
#include "../firmware/src/draw.c"

/* ---- the clock ---- */
static int32_t pblk[2 * CTL];
static uint32_t pblk_pos = CTL, sub_us, loop_ms;
static void panel_step(void)                   /* one pass of the firmware's main loop */
{
    fm1_in.frames += 10u;
    ui_input();
    midi_poll();
    ui_leds();
    draw_frame();
}
static void panel_init(void)
{
    uint32_t i;
    palette_set(2);
    synth_init();
    ui_init();
    fm1_in.wheel[0] = 540;                     /* (the wheels at rest) */
    fm1_in.wheel[1] = 615;
    for (i = 0; i < 4u; i++)                   /* (as the page's faders start) */
        fm1_in.fader[i] = (uint8_t)(i == 0u ? 60 : i == 1u ? 20 : i == 2u ? 40 : 100);
    lcd_fill(0, 0, 240, 240, C_BG);
    panel_step();
}
/* n frames of audio into l, r (may be 0); the main loop as the time passes */
static void panel_render(float *l, float *r, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i++) {
        if (pblk_pos >= CTL) {
            synth_block(pblk, CTL);
            pblk_pos = 0;
        }
        if (l)
            l[i] = (float)pblk[2u * pblk_pos] * (1.0f / 32768.0f);
        if (r)
            r[i] = (float)pblk[2u * pblk_pos + 1u] * (1.0f / 32768.0f);
        pblk_pos++;
        if ((sub_us += 22676u) >= 1000000u) {  /* 1 / 44100 s in ns (x 1000) */
            sub_us -= 1000000u;
            fm1_ms++;
            for (j = 0; j < 10u; j++)          /* (the TIMER5 ticks: the jack's bytes) */
                smo_tick();
            if (++loop_ms >= 15u) {
                loop_ms = 0;
                panel_step();
            }
        }
    }
}
static void panel_ms(uint32_t ms) { panel_render(0, 0, ms * 441u / 10u); }

/* ---- touching it ---- */
static void panel_pad(uint32_t pad, uint32_t vel)     /* vel 0 = release */
{
    if (pad < 16u)
        padq[padq_w++ % PADQ] = (uint16_t)(pad << 8 | (vel > 127u ? 127u : vel));
}
static void panel_key(uint32_t k, uint32_t down, uint32_t vel)   /* keybed key 0..36 */
{
    if (k >= 37u)
        return;
    fm1_in.key_vel[k] = (uint8_t)vel;
    if (down)
        fm1_in.keys |= (uint64_t)1u << k;
    else
        fm1_in.keys &= ~((uint64_t)1u << k);
}
static void panel_button(uint32_t b, uint32_t down)
{
    if (b >= FM1_NBTN)
        return;
    if (down) {
        fm1_in.buttons |= 1u << b;
        btn_pressed |= 1u << b;
    } else {
        fm1_in.buttons &= ~(1u << b);
        btn_released |= 1u << b;
    }
}
static void panel_enc(uint32_t e, int32_t steps) { enc_steps[e & 7u] = (int16_t)(enc_steps[e & 7u] + steps); }
static void panel_fader(uint32_t i, uint32_t v) { if (i < 4u) fm1_in.fader[i] = (uint8_t)(v > 127u ? 127u : v); }
static void panel_wheel(uint32_t i, uint32_t raw) { if (i < 2u) fm1_in.wheel[i] = (uint16_t)raw; }
static void panel_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >> 4;
    midi_in_q[mi_w++ % MQ] = cin | st << 8 | d1 << 16 | d2 << 24;
}
