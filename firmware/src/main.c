/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* Boot and main loop. Boot order: WDT first, boot-loop guard, fatal vectors, guards; then LCD,
 * input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* TIMER5 outranks ALNK0: the scan keeps its pace while a half buffer renders. Nested in the render it
 * scans, counts ms and serves the USB audio stream (uac_service); usb_poll waits for the first tick
 * after the render. The time spent nested is handed to the audio ISR (its load figures stay render-only). */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    mk_dbg.timer_irqs++;
    fm1_input_tick();
    smo_tick();                                 /* TRS MIDI OUT: a byte when the last has gone (also nested) */
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
    if (++sub == 10u)
        sub = 0;
    if (mk_dbg.in_audio) {
        mk_dbg.nested++;
        if (usb_due)
            uac_service();
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
    owed = 0;
}
extern void isr_timer5(void);

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, BRAND_CRASH, C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, RGB(80, 120, 255), 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                      /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

static void mk_main(void)
{
    uint32_t healthy_since, t_oct = 0, shown = 0;
    persist_boot();
    if (flash_ok)
        ota_boot_cleanup();                     /* staging area left by an update */
    lcd_init();
    palette_set(2);
    splash();
    if (mk_dbg.magic != DBG_MAGIC) {
        memset(&mk_dbg, 0, sizeof mk_dbg);
        mk_dbg.magic = DBG_MAGIC;
    }
    mk_dbg.boots++;
    mk_dbg.max_us = 0;
    mk_dbg.in_audio = 0;                        /* .noinit: a reset inside the audio ISR left it set */
    mk_dbg.prev_stage = mk_dbg.stage;
    mk_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    if (flash_ok)                               /* the factory key / pad velocity curves (read only) */
        smk_trim_load(fm1_xip_ptr(SMK_TRIM_FLASH));
    synth_init();                               /* the wavetables into RAM */
    ui_init();                                  /* the first sound */
    audio_init();
    usb_cdc_on = settings.console;              /* before USB starts: the host sees one device */
    usb_start();
    smk_uart1_tx_init();                        /* TRS MIDI OUT (PORTC, IOMAP RMW: before TIMER5) */
    fm1_timer5_start(isr_timer5, 4);            /* above ALNK0 (3): the scan may nest into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    boot_anim();                                /* (draw.c: ~2 s, a key ends it) */

    healthy_since = fm1_ms;
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if ((uint32_t)(m - healthy_since) >= 30000u && bootguard.pending)
            bootguard_clear(&bootguard);        /* 30 s of this boot */
        {   /* OCT- + OCT+ held 5 s: UBOOT (debug / update); a countdown from 2 s, letting go cancels it */
            if ((fm1_in.buttons & FM1_OCT_BOTH) != FM1_OCT_BOTH) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t_oct = fm1_ms;
            } else if (fm1_ms - t_oct > 2000u && fm1_ms - t_oct <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t_oct) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t_oct > 5000u)
                enter_uboot("UBOOT");
        }
        ota_service();                          /* M-UPGRADE handshake */
        if (usb.ota_req) {                      /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                  /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BG);
            ui.dirty = 1;
        }
        if (usb.uboot_req)                      /* SysEx F0 22 24 35 7D F7 from the host */
            enter_uboot("UBOOT (USB)");
        cdc_task();
        midi_poll();
        mk_dbg.ui_frames++;
        mk_dbg.stage = 1;
        ui_input();
        mk_dbg.stage = 2;
        ui_leds();
        padled_flush();                         /* the pads' colours, between LCD transfers */
        mk_dbg.stage = 3;
        draw_frame();
        mk_dbg.stage = 4;
        while (fm1_ms - m < 15u) {              /* ~60 loops / s at most */
            ui_input();
            midi_poll();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);           /* a power-on: no failed boot to count */
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();
    mk_main();
    for (;;)
        ;
}
