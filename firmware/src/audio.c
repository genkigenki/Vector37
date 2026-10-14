/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* I2S output (ALNK0 -> external codec) and the audio ISR: each half buffer is rendered in blocks of
 * CTL samples by synth_block (synth.c), then scaled to 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets: read with the console (dbg) */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct mk_dbg {
    uint32_t magic, halves, max_us, nested;
    volatile uint32_t in_audio;           /* TIMER5 reads it to decide what may nest in the render */
    uint32_t late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, prev_stage, prev_rst;
} mk_dbg __attribute__((section(".noinit")));
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static volatile uint32_t cpu_q8, cpu_peak_q8;          /* render time / half-buffer time, x256 (avg, peak) */

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    mk_dbg.in_audio = 1;                        /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, i, us, q;
        int32_t *o = &abuf[half * HALF_WORDS];
        uac_render_start();
        for (b = 0; b < HALF_FRAMES; b += CTL) {
            int32_t *x = o + 2u * b;
            synth_block(x, CTL);
            uac_tap(x, CTL);                   /* the USB audio input: the master output */
            for (i = 0; i < 2u * CTL; i++)
                x[i] <<= OUT_SHIFT;
        }
        fm1_audio_ack_half();
        us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;
        q = (us * 256u) / (HALF_FRAMES * 1000000u / FS);
        cpu_q8 = (cpu_q8 * 15u + q) / 16u;
        cpu_guard(q);                           /* too slow: fewer voices (mix.c) */
        if (q > cpu_peak_q8)
            cpu_peak_q8 = q;
        if (fm1_audio_free_half() != half)
            mk_dbg.late++;                      /* the DMA moved on while we rendered */
        mk_dbg.halves++;
        mk_dbg.last_us = us;
        if (us > mk_dbg.max_us)
            mk_dbg.max_us = us;
        mk_dbg.cpu_q8 = cpu_q8;
    }
    mk_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
