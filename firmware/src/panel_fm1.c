/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Vector37's panel on the FM-1 (experimental, untested on a unit; build.py BOARD=fm1). The FM-1 has no pads, faders
 * or wheels and four knobs instead of eight: their jobs move to pages, buttons and the keys. The shared actions
 * (knob_turn, pad_press, preset_load, randomize, audition, ...) and the screen are the SMK-37's (ui.c, draw.c).
 *   KNOB 1..4      the knob set's K1..K4 (page 1) or K5..K8 (page 2), or the PATH (page 3: FAMILY, SIZE, SPEED,
 *                  MORPH, the SMK's faders); SEL: the next page, SELECT turns them, HOME: page 1
 *   ALGORITHM      the planet selected: none (the knobs are the orbit), 1, 2, 3, 4
 *   PRESETS        the sounds (the SMK's < >)
 *   EDIT held      the lowest 16 keys are the 16 pads, in four groups of four (SELECT 1-4, MODE 1-4, FILTER 1-4,
 *                  MUTE 1-4, left to right); their LEDs show the pads' state (SELECT: lit = selected; MUTE: lit = on,
 *                  the glow = muted; MODE, FILTER: the glow). EDIT + PLAY: RESTART on / off; EDIT + SEQ: all notes off
 *   ENV, FX, LFO, ARP   the VOICE, MIXER, MORE, ARP sets: a tap opens the set and it stays (its light lit; tapped
 *                  again or another set's: closed / that one); held while a knob turns: only while held. VOICE
 *                  K7 VOICES: POLY, DUAL, MONO 1..8; ARP K1: the arpeggiator's mode (OFF, ...)
 *   page 4         WAVE: the selected planet's wavetables (WT POS 1, WT ORBIT 1, WT POS 2, WT ORBIT 2; the SMK-37's
 *                  SCALE set)
 *   SEQ            FREEZE     REC  LATCH     PLAY  tap the BPM     GLO  FOCUS (1-4, centre, off)
 *   SAVE           RANDOMIZE the selected planets (held: UNDO)      OCT- / OCT+  the keys' octave
 *   MASTER         the output level (a pot)
 *   ORBIT set (no planet selected) K8: MOON SPEED (the SMK's mod wheel; BPM: the ARP set, PLAY)
 * The 27 keys (F3..G5) play at velocity 100; their LEDs light the notes held. OCT- + OCT+ held 5 s: the update
 * mode (main.c), OCT- held at power-on: the rescue mode. No TRS MIDI OUT (the FM-1's jack is MIDI IN), no store.
 * The ids: the FM-1's matrix (ui.c: B_OCTDN 0, B_OCTUP 1, B_FX 2, B_SEL 3, ENV 4 = B_CHORD, LFO 5 = B_FADERBANK,
 * B_EDIT 6, GLO 7 = B_GLOBE, B_HOME 8, SAVE 9 = B_PARA, B_ARP 10, B_SEQ 11, B_PLAY 12, B_REC 13); encoders SELECT 0,
 * ALGORITHM 1, KNOB 1..4 2..5, PRESETS 6, clockwise + */
enum { FP_E_SELECT = 0, FP_E_ALGO = 1, FP_E_K1 = 2, FP_E_PRESET = 6 };
enum { FP_PAGE1, FP_PAGE2, FP_PATH, FP_WAVE, FP_NPAGE };
#define FP_KEY0 14u                                /* (LED ids: the note keys from 14) */
#define FP_NOTE0 53                                /* the lowest key: F3 (the SMK's sixth key) */
static const char *const FP_PAGE_NAME[FP_NPAGE] = {"PAGE 1: KNOBS 1-4", "PAGE 2: KNOBS 5-8", "PAGE 3: THE PATH",
                                                   "PAGE 4: WAVETABLES"};
static struct {
    uint8_t page;
    uint32_t prev;                                 /* the keys as last seen */
    uint32_t note_keys, pad_keys;                  /* keys playing a note; keys held as pads */
    uint8_t note[27];                              /* the note a key started */
    uint32_t pot_t;
    int32_t pot_sm, pot_last;
} fp = {.pot_last = -1};

/* the row of the knob set KNOB 1..4 play (0: K1..K4, 1: K5..K8), -1: the path (draw.c marks it) */
static int32_t panel_krow(void)
{
    if (fp.page == FP_PATH)
        return LAYER_HELD(ui.layer) ? 0 : -1;
    return fp.page == FP_WAVE ? 0 : fp.page;
}

/* ------------------------------------------------------------- LEDs --- */
#ifndef FP_LED
static void fp_led(uint32_t id, uint32_t lvl)      /* 0 dark, 1 the glow, 2 lit */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                fm1_led[p] = (uint8_t)(lvl == 2u ? fm1_led[p] | 1u << r : fm1_led[p] & ~(1u << r));
                fm1_led_dim[p] = (uint8_t)(lvl == 1u ? fm1_led_dim[p] | 1u << r : fm1_led_dim[p] & ~(1u << r));
            }
}
#define FP_LED(id, lvl) fp_led(id, lvl)
#endif

/* ------------------------------------------------------------- MASTER --- */
static int32_t fp_pot(void)                        /* 0..1023, smoothed, a dead band against the last bits */
{
    int32_t pot = fm1_adc_read(FM1_ADC_MASTER);
    if (pot < 0)
        return fp.pot_last < 0 ? 1023 : fp.pot_last;
    fp.pot_sm = fp.pot_last < 0 ? pot * 16 : fp.pot_sm + (pot * 16 - fp.pot_sm) / 8;
    pot = fp.pot_sm / 16;
    if (fp.pot_last >= 0 && pot - fp.pot_last < 6 && fp.pot_last - pot < 6)
        pot = fp.pot_last;
    return clamp(pot, 0, 1023);
}
static void panel_master_init(void)                /* (before the audio starts: no jump) */
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        fp.pot_last = fp_pot();
    panel_master_q12 = fp.pot_last * fp.pot_last >> 8;
}
static void panel_master_poll(void)                /* every 20 ms: the level pot^2 */
{
    int32_t pot;
    if (fm1_ms - fp.pot_t < 20u)
        return;
    fp.pot_t = fm1_ms;
    pot = fp_pot();
    if (pot != fp.pot_last) {
        char b[8];
        fp.pot_last = pot;
        panel_master_q12 = pot * pot >> 8;
        fmt_int(b, pot * 100 / 1023);
        str_cat(b, " %", sizeof b);
        ui_say("MASTER ", b);
    }
}

/* ------------------------------------------------------------- the path (page 3) --- */
static void fp_path_turn(uint32_t i, int32_t s)    /* KNOB i: fader i's parameter */
{
    const pdesc_t *d = &GDESC[FADER_G[i]];
    int32_t r, best = 0, bd = 1 << 30;
    s = accel(4u + i, s, d->max - d->min);
    if (FADER_G[i] == G_SHAPE)
        s *= 2;                                    /* (FAMILY: 62 clicks through the five families) */
    pt.g[FADER_G[i]] = (int16_t)clamp(pt.g[FADER_G[i]] + s, d->min, d->max);
    for (r = 0; r < 128; r++) {                    /* (the bottom strip's tick: where a fader would be) */
        int32_t e = fader_value(i, (uint32_t)r) - pt.g[FADER_G[i]];
        e = e < 0 ? -e : e;
        if (e < bd)
            bd = e, best = r;
    }
    ui.fad_last[i] = (uint8_t)best, ui.fad_mode[i] = 2;
    if (FADER_G[i] == G_SHAPE || FADER_G[i] == G_MORPH)
        ui.path_t = fm1_ms | 1u;
    fader_say(i);
    touched(TK_FADER, i, 0, 0);
}
static void fp_page(int32_t p)
{
    fp.page = (uint8_t)clamp(p, 0, FP_NPAGE - 1);
    ui_say(FP_PAGE_NAME[fp.page], 0);
}

/* ------------------------------------------------------------- the encoders --- */
static void fp_knobs(void)
{
    uint32_t k, b;
    int32_t s;
    for (k = 0; k < 4u; k++) {
        if ((s = fm1_enc_take(FP_E_K1 + k)) == 0)
            continue;
        for (b = 0; b < FM1_NBTN; b++)             /* (a turn while a button is held: that button's set) */
            if (ui.btn_t[b])
                ui.btn_used[b] = 1;
        if (panel_krow() < 0)
            fp_path_turn(k, s);
        else
            knob_turn((uint32_t)panel_krow() * 4u + k, s);
    }
    if ((s = fm1_enc_take(FP_E_SELECT)) != 0)      /* SELECT: the page */
        fp_page(fp.page + s);
    if ((s = fm1_enc_take(FP_E_PRESET)) != 0) {    /* PRESETS: the sounds */
        char nb[24];
        preset_load((ui.bank == BK_USER ? 0u : sound_fact()) + NFACT * 8u + (uint32_t)s);   /* (all 32 in turn) */
        fmt_int(nb, sound_fact() + 1);
        str_cat(nb, " ", sizeof nb);
        str_cat(nb, PRESET_NAME[sound_fact()], sizeof nb);
        ui_say(nb, 0);
    }
    if ((s = fm1_enc_take(FP_E_ALGO)) != 0) {      /* ALGORITHM: none, planet 1, 2, 3, 4 */
        int32_t cur = ui.sel ? (int32_t)sel_first() + 1 : 0, n = clamp(cur + s, 0, NC);
        char b[28];
        ui.sel = (uint8_t)(n ? 1u << (n - 1) : 0u);
        if (n && focus && focus <= 4u)
            focus = (uint8_t)n;
        if (n) {
            str_cpy(b, "SELECT: PLANET ", sizeof b);
            str_cat(b, CORNER_NAME[n - 1], sizeof b);
            ui_say(b, 0);
            audition((uint32_t)n - 1u);
        } else
            ui_say("SELECT: NONE, KNOBS = ORBIT", 0);
        touched(TK_PAD, 0, ui.sel, PG_SELECT);
    }
}

/* ------------------------------------------------------------- the buttons --- */
static void fp_tap(uint32_t b)                     /* a button let go without a knob turned or a key played */
{
    switch (b) {
    case B_SEQ:
        freeze ^= 1u;
        ui_say(freeze ? "FREEZE: THE ORBIT STANDS" : "FREEZE OFF: IT MOVES ON", 0);
        break;
    case B_CHORD:                                  /* ENV, ARP, FX, LFO: their sets, open / closed */
    case B_ARP:
    case B_FX:
    case B_FADERBANK:
        latch_tap(b);
        break;
    case B_EDIT:
        ui_say("HOLD EDIT: KEYS = PADS", 0);
        break;
    case B_HOME:                                   /* page 1 */
        fp_page(FP_PAGE1);
        break;
    case B_PARA:                                   /* SAVE: randomize (held: undo, below) */
        randomize();
        break;
    default:
        break;
    }
    ui.dirty = 1;
}
static void fp_press(uint32_t b)
{
    uint32_t edit = ui.btn_t[B_EDIT] != 0u;
    switch (b) {
    case B_OCTDN:
    case B_OCTUP: {
        char ob[4] = {0};
        ui.oct = (int8_t)clamp(ui.oct + (b == B_OCTUP ? 1 : -1), -3, 3);
        ob[0] = ui.oct < 0 ? '-' : '+';
        ob[1] = (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct));
        ui_say("OCTAVE ", ob);
        break;
    }
    case B_SEL:                                    /* the next page */
        fp_page((fp.page + 1) % FP_NPAGE);
        break;
    case B_GLOBE:                                  /* FOCUS: corner 1 2 3 4, the centre, off, ...; held: off */
        focus = (uint8_t)((focus + 1u) % 6u);
        if (focus && focus <= 4u)
            ui.sel = (uint8_t)(1u << (focus - 1u));
        ui_say("FOCUS ", !focus ? "OFF" : focus == 5u ? "CENTRE" : CORNER_NAME[focus - 1u]);
        break;
    case B_REC:
        latch ^= 1u;
        ui_say("LATCH ", latch ? "ON" : "OFF");
        break;
    case B_PLAY:
        if (edit) {                                /* EDIT + PLAY: RESTART */
            ui.btn_used[B_EDIT] = 1;
            pt.g[G_RETRIG] ^= 1;
            ui_say("RESTART ", pt.g[G_RETRIG] ? "ON" : "OFF");
        } else
            tap_tempo();
        break;
    case B_SEQ:
        if (edit) {                                /* EDIT + SEQ: all notes off (not FREEZE) */
            ui.btn_used[B_EDIT] = ui.btn_used[B_SEQ] = 1;
            ev_push(EV_ALLOFF, 0, 0);
            latch = 0;
            ui_say("ALL NOTES OFF", 0);
        }
        break;
    default:
        break;
    }
    ui.dirty = 1;
}
static void fp_buttons(void)
{
    uint32_t rel, pr = fm1_input_edges(&rel), b, l;
    for (b = 0; b < FM1_NBTN; b++) {
        if ((pr >> b) & 1u) {
            ui.btn_t[b] = fm1_ms | 1u;
            ui.btn_used[b] = 0;
            fp_press(b);
        }
        if ((rel >> b) & 1u && ui.btn_t[b]) {
            int32_t held = (int32_t)(fm1_ms - ui.btn_t[b]);
            if (!ui.btn_used[b] && (held < 500 || layer_of_btn(b)))   /* (a set's button: no knob turned, open) */
                fp_tap(b);
            ui.btn_t[b] = 0;
        }
    }
    if (ui.btn_t[B_GLOBE] && (int32_t)(fm1_ms - ui.btn_t[B_GLOBE]) >= 500 && focus) {
        focus = 0;
        ui_say("FOCUS OFF", 0);
    }
    if (ui.btn_t[B_PARA] && (int32_t)(fm1_ms - ui.btn_t[B_PARA]) >= 600 && !ui.btn_used[B_PARA]) {
        ui.btn_used[B_PARA] = 1;                   /* SAVE held: UNDO (again: redo) */
        if (ui.has_undo) {
            patch_t t = pt;
            pt = ui.undo;
            ui.undo = t;
            ui_say("UNDO", 0);
        } else
            ui_say("NOTHING TO UNDO", 0);
    }
    l = layer_now();                               /* the knob set: the planets or the orbit; an open set; a held one */
    if (fp.page == FP_WAVE && l == L_MAIN)         /* (page 4, a planet selected: its wavetables) */
        l = L_WAVE;
    if (l != ui.layer)
        ui.layer = (uint8_t)l, ui.dirty = 1;
}

/* ------------------------------------------------------------- the keys --- */
static uint32_t fp_pad_of(uint32_t k) { return (k >> 2) * 2u + (k & 1u) + (k & 2u ? 8u : 0u); }   /* key 0..15: group k / 4, corner k % 4 */
static void fp_keys(void)
{
    uint32_t now = fm1_in.notes & 0x7FFFFFFu, ch = now ^ fp.prev, k, edit = ui.btn_t[B_EDIT] != 0u;
    if (fm1_in.frames < 100u)                      /* (the first readings settle) */
        return;
    fp.prev = now;
    for (k = 0; ch && k < 27u; k++) {
        uint32_t bit = 1u << k;
        if (!(ch & bit))
            continue;
        if (now & bit) {
            if (edit) {                            /* EDIT held: the lowest 16 keys are the pads */
                ui.btn_used[B_EDIT] = 1;
                if (k < 16u)
                    fp.pad_keys |= bit, pad_press(fp_pad_of(k));
            } else {
                int32_t n = clamp(FP_NOTE0 + (int32_t)k + 12 * ui.oct, 0, 127);
                fp.note[k] = (uint8_t)n;
                fp.note_keys |= bit;
                ev_push(EV_ON, (uint32_t)n, 100u);
            }
        } else if (fp.pad_keys & bit) {
            fp.pad_keys &= ~bit;
            pad_release(fp_pad_of(k));
        } else if (fp.note_keys & bit) {
            fp.note_keys &= ~bit;
            ev_push(EV_OFF, fp.note[k], 0);
        }
    }
    ui.keys = fp.note_keys;                        /* (audition: a key held) */
}

/* ------------------------------------------------------------- the LEDs --- */
static void ui_leds(void)
{
    uint32_t k;
    if (ui.btn_t[B_EDIT]) {                        /* the keys as the pads: their state */
        for (k = 0; k < 27u; k++) {
            uint32_t c = k & 3u, lvl = 0;
            if (k < 4u)
                lvl = (ui.sel >> c) & 1u ? 2u : 1u;
            else if (k < 12u)
                lvl = 1;
            else if (k < 16u)
                lvl = pt.c[c][CP_MUTE] ? 1u : 2u;
            FP_LED(FP_KEY0 + k, lvl);
        }
    } else
        for (k = 0; k < 27u; k++)
            FP_LED(FP_KEY0 + k, (fp.note_keys >> k) & 1u ? 2u : 0u);
    fm1_led_key(B_ARP, ui.layer == L_ARP || (pt.g[G_ARP] && fm1_ms % 1000u < 500u));   /* (its set lit; running: blinks) */
    fm1_led_key(B_REC, latch);
    fm1_led_key(B_GLOBE, focus != 0);
    fm1_led_key(B_FX, ui.layer == L_FX);
    fm1_led_key(B_FADERBANK, ui.layer == L_CORNER);
    fm1_led_key(B_CHORD, ui.layer == L_VOICE);
    fm1_led_key(B_SEQ, freeze);
    fm1_led_key(B_EDIT, ui.btn_t[B_EDIT] != 0u);
    fm1_led_key(B_SEL, fp.page != FP_PAGE1);
    fm1_led_key(B_PARA, ui.has_undo);
    fm1_led_key(B_PLAY, fm1_ms % (60000u / (uint32_t)pt.g[G_TEMPO]) < 60u);   /* the beat */
    fm1_led_key(B_OCTDN, ui.oct < 0);
    fm1_led_key(B_OCTUP, ui.oct > 0);
}

static void ui_input(void)
{
    fp_keys();
    fp_buttons();
    fp_knobs();
    faders_glide();
    audition_tick();
    panel_master_poll();
}
