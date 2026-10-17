/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The screen's frame rate: the firmware's ui.c / draw.c on the simulated panel (tests/panel.h), its main loop timed as
 * the device would run it. tests/fps [OUTDIR]  (run by tests/run_tests.sh; prints a table, checks the floors)
 *
 * The device model (core 0: the audio interrupt and the UI loop; core 1 is not started):
 *   - drawing: the host's time for the same code x K, K the ratio of the device to the host on the drawing's inner
 *     work (glow: 18 + 66 instructions a pixel in build/vector37.dis, two divisions; one instruction a cycle, 320 MHz,
 *     as docs/DESIGN.md assumes), measured here; stretched by 1 / (1 - the audio's share) (the audio interrupt
 *     outranks the loop);
 *   - the audio's share: (380 a voice + 620) instructions a sample at 44.1 kHz (docs/DESIGN.md, build/vector37.dis);
 *   - the SPI: 12 MHz (lcd.c LCD_BAUD 4): 0.667 us a byte, 10 us of commands a window; one transfer at a time; the
 *     drawing waits where lcd.c waits (lcd_sync: a buffer the DMA still reads, the next window);
 *   - the loop: again 15 ms after it began (main.c), or when it is done if that is later.
 * The host's own speed: the field frames a second the host draws (the drawing alone). */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static void fp_sync(void);
static void fp_send(uint32_t n);
static void fp_done(void);
static void fp_begin(void);
static void fp_end(void);
static uint32_t fp_next_ms;
#define PANEL_SPI_SYNC() fp_sync()
#define PANEL_SPI_SEND(n) fp_send(n)
#define PANEL_SPI_DONE() fp_done()
#define PANEL_LOOP_DUE() (fm1_ms >= fp_next_ms)
#define PANEL_STEP_BEGIN() fp_begin()
#define PANEL_STEP_END() fp_end()
#include "panel.h"
#ifndef FPS_MS
#define FPS_MS 3000
#endif
#ifndef UI_LOOP_MS
#define UI_LOOP_MS 15u                          /* (before: a loop every 15 ms) */
#endif
static double fp_t_cpu, fp_spi_end, fp_k = 1.0, fp_audio, fp_draw_us, fp_spi_us, fp_wait_us, fp_host_ns, fp_frame_max;
static double fp_host_last, fp_step_t0;
static uint32_t fp_on, fp_in;
static double fp_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}
static void fp_advance(void)                       /* the drawing since the last event, in device microseconds */
{
    double n = fp_now(), d = (n - fp_host_last) * 1e-3 * fp_k / (1.0 - fp_audio);
    fp_host_ns += n - fp_host_last;
    fp_t_cpu += d, fp_draw_us += d;
    fp_host_last = n;
}
static void fp_sync(void)
{
    if (!fp_on || !fp_in)
        return;
    fp_advance();
    if (fp_t_cpu < fp_spi_end)
        fp_wait_us += fp_spi_end - fp_t_cpu, fp_t_cpu = fp_spi_end;
    fp_host_last = fp_now();
}
static void fp_send(uint32_t n)
{
    if (!fp_on || !fp_in)
        return;
    fp_advance();
    fp_t_cpu += 10.0;                              /* (the window: CASET, RASET, RAMWR) */
    fp_spi_end = fp_t_cpu + n * (8.0 / 12.0);
    fp_spi_us += n * (8.0 / 12.0);
}
static void fp_done(void) { if (fp_on && fp_in) fp_host_last = fp_now(); }
static void fp_begin(void)
{
    if (fp_t_cpu < fm1_ms * 1000.0)
        fp_t_cpu = fm1_ms * 1000.0;
    fp_step_t0 = fp_t_cpu;
    fp_host_last = fp_now();
    fp_in = 1;
}
static void fp_end(void)
{
    double ready;
    fp_advance();
    fp_in = 0;
    if (fp_t_cpu - fp_step_t0 > fp_frame_max)
        fp_frame_max = fp_t_cpu - fp_step_t0;
    ready = fp_step_t0 + UI_LOOP_MS * 1000.0 > fp_t_cpu ? fp_step_t0 + UI_LOOP_MS * 1000.0 : fp_t_cpu;
    fp_next_ms = (uint32_t)(ready / 1000.0 + 0.999);
}

static int fails;
static void check(const char *what, int ok)
{
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    fails += !ok;
}
static void pad(uint32_t p) { panel_pad(p, 100); panel_ms(40); panel_pad(p, 0); panel_ms(40); }
static uint32_t padof(uint32_t g, uint32_t c) { return g * 2u + (c & 1u) + (c & 2u ? 8u : 0u); }
static double audio_share(uint32_t voices) { return (voices * 380.0 + 620.0) * 44100.0 / 320e6; }

typedef struct { double fps, draw_ms, spi_ms, frame_ms, ui_pct, spi_pct, audio_pct, host_fps; } fp_res_t;
static fp_res_t measure(const char *name, uint32_t voices, uint32_t ms, void (*act)(uint32_t))
{
    fp_res_t r;
    uint32_t n0 = fstat_n, t;
    double w0;
    fp_audio = audio_share(voices);
    fp_draw_us = fp_spi_us = fp_wait_us = fp_host_ns = fp_frame_max = 0;
    w0 = fp_t_cpu;
    for (t = 0; t < ms; t += 50u) {
        if (act)
            act(t);
        panel_ms(50);
    }
    {
        uint32_t n = fstat_n - n0 ? fstat_n - n0 : 1u;
        r.fps = (fstat_n - n0) * 1000.0 / ms;
        r.draw_ms = fp_draw_us * (1.0 - fp_audio) / 1000.0 / n;   /* (the CPU's own time a frame) */
        r.spi_ms = fp_spi_us / 1000.0 / n;
        r.frame_ms = fp_frame_max / 1000.0;
        r.ui_pct = 100.0 * fp_draw_us * (1.0 - fp_audio) / (ms * 1000.0);
        r.spi_pct = 100.0 * fp_spi_us / (ms * 1000.0);
        r.audio_pct = 100.0 * fp_audio;
        r.host_fps = (fstat_n - n0) / (fp_host_ns * 1e-9);
    }
    (void)w0;
    printf("  %-22s %5.1f fps  draw %5.2f ms  spi %5.2f ms  slowest loop %5.1f ms  core0: audio %4.1f %% + ui %4.1f %%"
           "  spi %4.1f %%  host %6.0f fps\n", name, r.fps, r.draw_ms, r.spi_ms, r.frame_ms, r.audio_pct, r.ui_pct,
           r.spi_pct, r.host_fps);
    return r;
}
static void act_morph(uint32_t t) { panel_fader(3, (t / 50u) % 30u < 15u ? 40u + (t / 50u) % 30u * 5u : 110u - ((t / 50u) % 30u - 15u) * 5u); }
static void act_knob(uint32_t t) { panel_enc(5, (t / 50u) % 20u < 10u ? 2 : -2); }

/* K: the device's time for the host's, on a fixed piece of the drawing: the earlier glow and cv_add (kept here as they were,
 * so K does not move when draw.c gets faster): a pixel 18 + 66 instructions and two divisions in build/vector37.dis of
 * e2ceaa0 (~4 cycles a division assumed), at 320 MHz */
static uint16_t kc_px[120 * 120];
static volatile uint32_t kc_sink;
static void kc_add(int32_t x, int32_t y, const uint8_t *c, int32_t k)
{
    uint32_t i, p, r, g, b;
    if (!in_clip(x, y))
        return;
    if ((uint32_t)x >= 120u || (uint32_t)y >= 120u || k <= 0)
        return;
    i = (uint32_t)y * 120u + (uint32_t)x;
    p = swap16(kc_px[i]);
    r = ((p >> 11) << 3) + (uint32_t)(c[0] * k >> 8);
    g = (((p >> 5) & 63u) << 2) + (uint32_t)(c[1] * k >> 8);
    b = ((p & 31u) << 3) + (uint32_t)(c[2] * k >> 8);
    kc_px[i] = swap16(RGB(r > 255u ? 255u : r, g > 255u ? 255u : g, b > 255u ? 255u : b));
}
static __attribute__((noinline)) void kc_glow(int32_t cx, int32_t cy, int32_t r, const uint8_t *c, int32_t k)
{
    int32_t x, y, r2 = r * r;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y;
            if (d < r2)
                kc_add(cx + x, cy + y, c, k * (r2 - d) / r2 * (r2 - d) / r2);
        }
}
static double calib(void)
{
    double t0, t1, host_us_px, best = 1e9, dev_us_px = (18.0 + 66.0 + 2.0 * 4.0) / 320.0;
    uint32_t i, px = 0, rep;
    int32_t x, y;
    for (y = -24; y <= 24; y++)
        for (x = -24; x <= 24; x++)
            px += x * x + y * y < 24 * 24;
    for (rep = 0; rep < 5u; rep++) {               /* (the fastest of five: the host undisturbed) */
        t0 = fp_now();
        for (i = 0; i < 2000u; i++) {
            kc_glow(60, 60, 24, SUN, 200 + (int32_t)(i & 7u));
            kc_glow(40 + (int32_t)(i & 15u), 70, 24, MOON, 100);
        }
        t1 = fp_now();
        kc_sink += kc_px[i % 14400u];
        host_us_px = (t1 - t0) * 1e-3 / (2000.0 * 2.0 * px);
        best = host_us_px < best ? host_us_px : best;
    }
    printf("  info calibration: host %.2f ns a glow pixel (the earlier code), the device ~%.0f ns (K = %.0f)\n", best * 1e3,
           dev_us_px * 1e3, dev_us_px / best);
    return dev_us_px / best;
}

int main(int argc, char **argv)
{
    fp_res_t idle, play, sel, morph, knob, spin;
    (void)argc, (void)argv;
    fp_k = calib();
    panel_init();
    fm1_in.frames = 400;
    fp_on = 1;
    panel_ms(1000);
    printf("  info the device (model, see tests/fps.c): field frames a second; per frame the drawing (CPU) and the SPI\n");
    idle = measure("idle orbit", 0, FPS_MS, 0);
    panel_key(12, 1, 100), panel_key(16, 1, 100), panel_key(19, 1, 100), panel_key(23, 1, 100);
    panel_ms(300);
    play = measure("four notes", 4, FPS_MS, 0);
    morph = measure("four notes, MORPH", 4, FPS_MS, act_morph);
    pad(padof(0, 0));
    panel_ms(500);
    sel = measure("planet selected", 4, FPS_MS, 0);
    knob = measure("planet, K6 turning", 4, FPS_MS, act_knob);
    pad(padof(0, 0));
    panel_ms(500);
    panel_enc(5, 20);                              /* (no planet: K6 SPIN) */
    panel_ms(300);
    spin = measure("four notes, SPIN", 4, FPS_MS, 0);
    panel_enc(5, -20);
    panel_ms(300);
#ifndef FPS_FLOOR
#define FPS_FLOOR 0
#endif
#if FPS_FLOOR
    check("fps: idle orbit 50 or more field frames a second", idle.fps >= 50.0);
    check("fps: four notes 50 or more", play.fps >= 50.0);
    check("fps: a planet selected, playing: 45 or more", sel.fps >= 45.0);
    check("fps: a knob turning (its bubble drawn anew): 25 or more", knob.fps >= 25.0);
    check("fps: a fader morphing the whole path: 20 or more", morph.fps >= 20.0);
    check("fps: SPIN while playing: 45 or more", spin.fps >= 45.0);
    check("fps: the UI never holds the loop over 150 ms (a whole sky: ~60 ms)", idle.frame_ms < 150 && play.frame_ms < 150 && sel.frame_ms < 150 &&
          knob.frame_ms < 150 && morph.frame_ms < 150 && spin.frame_ms < 150);
#endif
    (void)idle, (void)play, (void)sel, (void)morph, (void)knob, (void)spin;
    return fails ? 1 : 0;
}
