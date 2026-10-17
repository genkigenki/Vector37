/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* A simulated SMK-37 Pro for the host: the firmware's engine, UI and screen (the same sources) against a fake
 * panel (pads, keys, knobs, faders, wheels, buttons, LEDs, a 240 x 240 frame buffer). Used by tests/screen.c,
 * tools/anim.c and the web simulator (web/sim/sim.c). The device's clock runs with the audio: panel_render()
 * renders samples, and every 15 ms of them the main loop runs (input, LEDs, screen).
 * With BOARD_SMK37 0 (tests/fm1_panel.c): a simulated FM-1 instead (27 keys, 14 buttons, 7 encoders, the MASTER
 * pot, the key and button LEDs with their glow; the panel firmware/src/panel_fm1.c). */
#include <stdint.h>
#include <string.h>
#ifndef BOARD_SMK37
#define BOARD_SMK37 1                          /* (the SMK-37: ui.c, draw.c; tests/panel_fm1.h: the FM-1) */
#endif
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
#if BOARD_SMK37
#define FM1_NENC 8u
#define FM1_NBTN 18u
#define FM1_OCT_BOTH 0x30u
static struct { uint8_t fader[4]; uint32_t frames, buttons; uint64_t keys; uint16_t wheel[2]; uint8_t key_vel[37]; } fm1_in;
#else
#define FM1_NENC 7u
#define FM1_NBTN 14u
#define FM1_OCT_BOTH 0x3u
#define FM1_BTN_OCT_DOWN 0
static struct { uint32_t frames, buttons, notes; } fm1_in;
static uint8_t led_lvl[41];                    /* each LED (buttons 0..13, keys 14..40): 0 dark, 1 the glow, 2 lit */
#define FP_LED(id, lvl) (led_lvl[(id) % 41u] = (uint8_t)(lvl))
enum { FM1_ADC_MASTER = 4 };
static int32_t panel_pot = 1023;               /* the MASTER pot, 0..1023 */
static int32_t fm1_adc_read(uint32_t ch) { (void)ch; return panel_pot; }
#define PANEL_MASTER 1
static volatile int32_t panel_master_q12 = 4096;
#endif
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
#if !BOARD_SMK37
    if (id < 41u)
        led_lvl[id] = on ? 2u : 0u;
#endif
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
/* tests/fps.c times the device: PANEL_SPI_SYNC() waits for the SPI, PANEL_SPI_SEND(bytes) starts a transfer (after
 * the last one), PANEL_SPI_DONE() keeps the simulated copy out of the drawing's time */
#ifndef PANEL_SPI_SYNC
#define PANEL_SPI_SYNC() ((void)0)
#define PANEL_SPI_SEND(n) ((void)(n))
#define PANEL_SPI_DONE() ((void)0)
#endif
static const uint8_t *panel_spi_src;           /* (the transfer in flight: its source, as lcd.c keeps it) */
static uint32_t panel_spi_n;
static void lcd_sync(void) { PANEL_SPI_SYNC(); panel_spi_src = 0; }
static void lcd_sync_buf(const void *p, uint32_t n)   /* (as lcd.c: waits only if the transfer reads p .. p + n) */
{
    const uint8_t *a = (const uint8_t *)p;
    if (panel_spi_src && a < panel_spi_src + panel_spi_n && panel_spi_src < a + n)
        lcd_sync();
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    lcd_sync();                                /* (the SPI: one transfer at a time) */
    PANEL_SPI_SEND(w * h * 2u);
    panel_spi_src = (const uint8_t *)px, panel_spi_n = w * h * 2u;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fb_draws++;
    fb_pixels += w * h;
    PANEL_SPI_DONE();
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    lcd_sync();
    PANEL_SPI_SEND(w * h * 2u);
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fb_draws++;
    fb_pixels += w * h;
    lcd_sync();                                /* (lcd.c: a fill waits for its last transfer: the buffer is reused) */
    PANEL_SPI_DONE();
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
#if BOARD_SMK37
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
#else                                          /* (the FM-1: no TRS MIDI OUT; as vector37.c) */
static uint8_t trs_out;
static struct { uint8_t dead; } smo;
static void smo_put(uint32_t pkt) { (void)pkt; }
static void smo_tick(void) {}
static void smo_on(uint32_t on) { (void)on; }
#endif
/* the store (storage.c, scenes.c) on a simulated NOR: erase to 0xFF, programming only clears bits */
static uint8_t panel_nor[0x100000];
static uint32_t panel_erases;                  /* sector erases (a SAVE: one) */
static uint8_t flash_ok;
static int flash_probe(void) { return 1; }
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, panel_nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (off < 0x9D000u || off >= 0xBB000u || (off & 0xFFFu))   /* (FL_DATA only: the SMK store) */
        return -8;
    memset(panel_nor + off, 0xFF, 4096);
    panel_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off < 0x9D000u || off + n > 0xBB000u)
        return -8;
    for (i = 0; i < n; i++)
        panel_nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#define FELUCCA_DATA_FAMILY "VECTOR"
#define FELUCCA_DATA_UNTAGGED_OK 0
#include "../firmware/src/storage.c"
#include "../firmware/src/scenes.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/draw.c"

/* ---- the clock ---- */
static int32_t pblk[2 * CTL];
static uint32_t pblk_pos = CTL, sub_us, loop_ms;
#ifndef PANEL_LOOP_DUE
#define PANEL_LOOP_DUE() (++loop_ms >= UI_LOOP_MS)
#define PANEL_STEP_BEGIN() ((void)0)
#define PANEL_STEP_END() ((void)0)
#endif
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
    static uint8_t nor_ready;
    if (!nor_ready)                            /* (a new part: erased) */
        memset(panel_nor, 0xFF, sizeof panel_nor), nor_ready = 1;
    persist_boot();                            /* (as the boot: the store, the settings, your sounds) */
    palette_set(2);
    synth_init();
    ui_init();
#if BOARD_SMK37
    fm1_in.wheel[0] = 540;                     /* (the wheels at rest) */
    fm1_in.wheel[1] = 615;
    for (i = 0; i < 4u; i++)                   /* (as the page's faders start) */
        fm1_in.fader[i] = (uint8_t)(i == 0u ? 60 : i == 1u ? 20 : i == 2u ? 40 : 100);
#else
    (void)i;
    panel_master_init();
#endif
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
            if (PANEL_LOOP_DUE()) {            /* (every 15 ms; tests/fps.c: when the device would be ready) */
                loop_ms = 0;
                PANEL_STEP_BEGIN();
                panel_step();
                PANEL_STEP_END();
            }
        }
    }
}
static void panel_ms(uint32_t ms) { panel_render(0, 0, ms * 441u / 10u); }

/* ---- touching it ---- */
#if !BOARD_SMK37
static void panel_note(uint32_t k, uint32_t down)     /* the FM-1's key 0..26 (F3..G5) */
{
    if (k < 27u)
        fm1_in.notes = down ? fm1_in.notes | 1u << k : fm1_in.notes & ~(1u << k);
}
#endif
static void panel_pad(uint32_t pad, uint32_t vel)     /* vel 0 = release */
{
    if (pad < 16u)
        padq[padq_w++ % PADQ] = (uint16_t)(pad << 8 | (vel > 127u ? 127u : vel));
}
#if BOARD_SMK37
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
#endif
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
#if BOARD_SMK37
static void panel_fader(uint32_t i, uint32_t v) { if (i < 4u) fm1_in.fader[i] = (uint8_t)(v > 127u ? 127u : v); }
static void panel_wheel(uint32_t i, uint32_t raw) { if (i < 2u) fm1_in.wheel[i] = (uint16_t)raw; }
#endif
static void panel_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >> 4;
    midi_in_q[mi_w++ % MQ] = cin | st << 8 | d1 << 16 | d2 << 24;
}
