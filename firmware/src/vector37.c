/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* Vector37, an orbiting overtone synth for the M-Vave SMK-37 Pro: one compilation unit (the HAL is
 * header-only; board layer from SLOOP / Felucca). Order matters. */
#include <stdint.h>
#define MK_VERSION "0.2"
#define BRAND_NAME "VECTOR37"
#define BRAND_CRASH "VECTOR37 CRASH"
#define BRAND_RESCUE "VECTOR37 USB RESCUE"
#define SMK_PAD_EVENTS 1             /* the pads are gate buttons, not note keys (smk_input.h) */
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "smk_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_lcd_hw.h"
#include "mk_tables.h"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;     /* milliseconds since boot (TIMER5 ISR) */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));

#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "synth.h"
#include "orbit.c"
#include "engine.c"
#include "analog.c"
#include "fx.c"
#include "mix.c"
#include "params.c"
#include "presets.c"

#define FELUCCA_OTA 1                /* M-UPGRADE update entry (ota.c) */
#define FELUCCA_OTA_DRYRUN 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "SMK-37 Pro_700"  /* package identity (build.py) */
#endif
#define FELUCCA_CDC 1                /* USB serial console (console.c) */
#define FELUCCA_UAC 1                /* USB audio input: the master output */
#define FELUCCA_UAC_TONE 0
#define FELUCCA_FLASH 1
#define HALF_FRAMES 256              /* I2S half buffer: 5.8 ms at 44.1 kHz */
#include "smk_uart_tx.h"
#include "smk_midi_out.h"            /* TRS MIDI OUT (UART1 TX, PC7): the keys and wheels as played (ui.c) */
#include "usb.c"
#include "audio.c"

#include "fm1_flash.h"
static uint8_t flash_ok;                 /* JEDEC id matched at boot */
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)          /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}
/* a sector erase with the audio silenced in the same interrupts-off window */
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    uint32_t f = irq_save();
    int rc;
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, took);
    irq_restore(f);
    return rc;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    return fl_erase4k_quiet(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#define FELUCCA_DATA_FAMILY "VECTOR"     /* owner tag SMK37DATAVECTOR */
#define FELUCCA_DATA_UNTAGGED_OK 0
#include "storage.c"
#include "scenes.c"

static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? SMK_MODEL "_000" : FELUCCA_ID)
#include "ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k_quiet(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "UPDATING..."};
    char b[24];
    if (recovery_active) return;             /* keep polled USB alive during OTA */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    if (code == 1)
        str_cpy(b, "DRY RUN OK", sizeof b);
    else {
        uint32_t k = 0, v = (uint32_t)-code;
        str_cpy(b, "FAILED  -", sizeof b);
        while (b[k])
            k++;
        if (v >= 10u)
            b[k++] = (char)('0' + v / 10u);
        b[k++] = (char)('0' + v % 10u);
        b[k] = 0;
    }
    draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard_clear(&bootguard);                        /* intentional update reset */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                             /* record into RAM, core reset (fm1_sys.h) */
}

#include "ui.c"
#include "draw.c"
#include "console.c"
#include "recovery.c"        /* early, polled USB updater; no synth or settings */
#include "main.c"
