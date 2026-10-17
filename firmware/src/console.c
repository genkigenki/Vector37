/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* Serial console on the CDC-ACM function: diagnostics and test hooks (notes, parameters, presets).
 * Runs in the main loop (cdc_task); usb_poll moves the bytes. The baud rate is ignored. Flash is
 * never written from here; `uboot` does what the SysEx soft key does. */
#define CON_LINE 64u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
#if BOARD_SMK37
    if (!ok || a < 0x9D000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x9D000..0xFFFFF)\r\n");
#else
    if (!ok || a < 0x93000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x93000..0xFFFFF)\r\n");
#endif
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

static int32_t con_db10(int32_t peak)                   /* dBFS x 10 of a Q15 peak (rough) */
{
    int32_t d = 0;
    if (peak <= 0)
        return -999;
    while (peak < 16384)
        peak <<= 1, d -= 60;
    return d - (32767 - peak) * 60 / 16384;
}

static void con_status(void)
{
    uint32_t v, act = 0;
    for (v = 0; v < NVOICE; v++)
        act += vs[v].stage != 0u;
    con_puts("vector37 " MK_VERSION "\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(cpu_q8 * 100u / 256u));
    con_kv("cpu_peak_pct", (int32_t)(cpu_peak_q8 * 100u / 256u));
    con_kv("audio_max_us", (int32_t)mk_dbg.max_us);
    con_kv("audio_late", (int32_t)mk_dbg.late);
    con_kv("voices_on", (int32_t)act);
    con_kv("voices_max", (int32_t)vmax);
    con_kv("peak_db10", con_db10(syn.peak));
    con_kv("limiter", (int32_t)syn.overs);
    con_kv("preset", settings.preset);
    con_kv("flash", flash_ok);
    con_kv("boots", (int32_t)mk_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("uac_underruns", (int32_t)uac.underruns);
}

static void con_cpu(void)                               /* the load since the last `cpu` */
{
    con_kv("cpu_pct", (int32_t)(cpu_q8 * 100u / 256u));
    con_kv("cpu_peak_pct", (int32_t)(cpu_peak_q8 * 100u / 256u));
    con_kv("audio_max_us", (int32_t)mk_dbg.max_us);
    con_kv("audio_late", (int32_t)mk_dbg.late);
    con_kv("voices_max", (int32_t)vmax);
    con_kv("peak_db10", con_db10(syn.peak));
    con_kv("limiter", (int32_t)syn.overs);
    cpu_peak_q8 = 0;
    mk_dbg.max_us = 0;
    syn.peak = 0;
    syn.overs = 0;
}

static void con_voices(void)
{
    uint32_t v;
    for (v = 0; v < NVOICE; v++) {
        con_puts("v");
        con_dec((int32_t)v + 1);
        con_puts(" on ");
        con_dec(vs[v].on);
        con_puts(" stage ");
        con_dec(vs[v].stage);
        con_puts(" note ");
        con_dec(vs[v].note);
        con_puts(" amp ");
        con_dec(vs[v].amp);
        con_puts(" w ");
        con_dec(vs[v].w[0] >> 7), con_putc(' '), con_dec(vs[v].w[1] >> 7), con_putc(' ');
        con_dec(vs[v].w[2] >> 7), con_putc(' '), con_dec(vs[v].w[3] >> 7);
        con_puts("\r\n");
    }
}

/* note N [VEL] | note off N | note panic: as a key */
static void con_note(const char *p)
{
    int ok, off = con_word(&p, "off");
    uint32_t n, vel;
    if (con_word(&p, "panic")) {
        ev_push(EV_ALLOFF, 0, 0);
        con_puts("ok\r\n");
        return;
    }
    n = con_num(&p, &ok);
    if (!ok || n > 127u) {
        con_puts("usage: note N [VEL] | note off N | note panic\r\n");
        return;
    }
    vel = con_num(&p, &ok);
    ev_push(off ? EV_OFF : EV_ON, n, ok && vel <= 127u ? vel : 100u);
    con_puts("ok\r\n");
}

/* set cN K VALUE | set g K VALUE (parameter index, clamped) */
static void con_set(const char *p)
{
    int ok1 = 0, ok2 = 0, glob = con_word(&p, "g");
    uint32_t c = 0, k;
    int32_t x;
    if (!glob) {
        while (*p == ' ')
            p++;
        if (*p != 'c') {
            con_puts("usage: set cN K VALUE | set g K VALUE\r\n");
            return;
        }
        p++;
        c = con_num(&p, &ok1) - 1u;
        if (!ok1 || c >= NC) {
            con_puts("corner 1..4\r\n");
            return;
        }
    }
    k = con_num(&p, &ok1);
    while (*p == ' ')
        p++;
    if (*p == '-') {
        p++;
        x = -(int32_t)con_num(&p, &ok2);
    } else
        x = (int32_t)con_num(&p, &ok2);
    if (!ok1 || !ok2 || k >= (glob ? (uint32_t)G_COUNT : (uint32_t)CP_N)) {
        con_puts("bad index or value\r\n");
        return;
    }
    if (glob)
        pt.g[k] = (int16_t)clamp(x, GDESC[k].min, GDESC[k].max);
    else
        pt.c[c][k] = (int16_t)clamp(x, CDESC[k].min, CDESC[k].max);
    ui.dirty = 1;
    con_kv("=", glob ? pt.g[k] : pt.c[c][k]);
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&mk_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late", "timer_irqs",
                                        "ui_frames", "last_us", "cpu_q8", "boots", "stage", "prev_stage", "prev_rst"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof mk_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
}

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
}

static void con_params(void)
{
    uint32_t c, k;
    for (c = 0; c < NC; c++) {
        con_puts("c");
        con_dec((int32_t)c + 1);
        for (k = 0; k < CP_N; k++) {
            con_putc(' ');
            con_dec(pt.c[c][k]);
        }
        con_puts("\r\n");
    }
    con_puts("g");
    for (k = 0; k < G_COUNT; k++) {
        con_putc(' ');
        con_dec(pt.g[k]);
    }
    con_puts("\r\n");
}

static void con_preset(const char *p)                   /* preset N: a factory sound */
{
    int ok;
    uint32_t n = con_num(&p, &ok);
    if (!ok || n < 1u || n > NFACT) {
        con_puts("usage: preset 1..32\r\n");
        return;
    }
    preset_load(n - 1u);
    con_kv("loaded", (int32_t)n);
}

#if BOARD_SMK37
static void con_smk(void)                               /* the SMK-37 Pro inputs (smk_input.h) */
{
    uint32_t i;
    con_kv("adc_ok", (int32_t)fm1_in.adc_ok);
    con_kv("adc_miss", (int32_t)fm1_in.adc_miss);
    con_kv("frames", (int32_t)fm1_in.frames);
    con_puts("pad raw ");
    for (i = 0; i < SMK_NPAD; i++) { con_dec(fm1_in.pad_raw[i]); con_putc(' '); }
    con_puts("\r\npad base ");
    for (i = 0; i < SMK_NPAD; i++) { con_dec(fm1_in.pad_base[i]); con_putc(' '); }
    con_puts("\r\npad st ");
    for (i = 0; i < SMK_NPAD; i++) { con_dec(fm1_in.pad_st[i]); con_putc(' '); }
    con_puts("\r\nfader raw ");
    for (i = 0; i < 4u; i++) { con_dec(fm1_in.fad_raw[i]); con_putc(' '); }
    con_puts("\r\nfader ");
    for (i = 0; i < 4u; i++) { con_dec(fm1_in.fader[i]); con_putc(' '); }
    con_puts("\r\n");
    con_kv("wheel0", fm1_in.wheel[0]);
    con_kv("wheel1", fm1_in.wheel[1]);
    con_kv("joy_x", joy_x);
    con_kv("joy_y", joy_y);
    con_kv("pedal", fm1_in.pedal);
    con_kv("batt", fm1_in.batt);
    con_kx("buttons", fm1_in.buttons);
    con_kx("keys_lo", (uint32_t)fm1_in.keys);
    con_kx("keys_hi", (uint32_t)(fm1_in.keys >> 32));
    for (i = 0; i < FM1_NENC; i++) {
        con_puts("enc");
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(fm1_in.enc_steps[i]);
        con_putc(i + 1u == FM1_NENC ? '\r' : ' ');
    }
    con_puts("\r\n");
}

/* what the firmware sends the pads: GRB words by hardware pad (0..7 top row, 8..15 bottom row) */
static void con_leds(void)
{
    uint32_t i;
    for (i = 0; i < SMK_NPAD; i++) {
        con_puts("pad");
        con_dec((int32_t)i);
        con_puts(" grb ");
        con_hex(padled_rgb[i], 6);
        con_puts(i % 4u == 3u ? "\r\n" : "  ");
    }
}

/* the pad LEDs' colour order: hardware pad 8 (bottom left) red, 9 green, 10 blue, 11 white, for 20 s */
static void con_padtest(void)
{
    uint32_t i;
    for (i = 0; i < SMK_NPAD; i++)
        padled_set(i, 0, 0, 0);
    padled_set(8, 40, 0, 0);
    padled_set(9, 0, 40, 0);
    padled_set(10, 0, 0, 40);
    padled_set(11, 40, 40, 40);
    padled_hold_until = fm1_ms + 20000u;
    con_puts("top row from the left: red green blue white\r\n");
}

#else
static void con_smk(void)                               /* the FM-1's inputs (fm1_input.h), MASTER */
{
    uint32_t i;
    con_kv("frames", (int32_t)fm1_in.frames);
    con_kx("buttons", fm1_in.buttons);
    con_kx("notes", fm1_in.notes);
    con_kv("master", fm1_adc_read(FM1_ADC_MASTER));
    for (i = 0; i < FM1_NENC; i++) {
        con_puts("enc");
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(fm1_in.enc_steps[i]);
        con_putc(i + 1u == FM1_NENC ? '\r' : ' ');
    }
    con_puts("\r\n");
}
static void con_leds(void) { con_puts("the FM-1 has no pads\r\n"); }
static void con_padtest(void) { con_leds(); }

#endif
static void con_exec(const char *p)
{
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("status  cpu  voices  in  leds  padtest  note N|off N|panic  set cN|g K V  params\r\n"
                 "preset N  dbg  crash  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "cpu"))
        con_cpu();
    else if (con_word(&p, "voices"))
        con_voices();
    else if (con_word(&p, "note"))
        con_note(p);
    else if (con_word(&p, "set"))
        con_set(p);
    else if (con_word(&p, "preset"))
        con_preset(p);
    else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "in"))
        con_smk();
    else if (con_word(&p, "padtest"))
        con_padtest();
    else if (con_word(&p, "leds"))
        con_leds();
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nVector37 " MK_VERSION " console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
