/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Controls: one screen, everything at hand (docs/CONTROLS.md is the table this implements).
 *   Pads: four groups of 2 x 2 (columns 1-2, 3-4, 5-6, 7-8); in each group the four pads are the four corners,
 *   placed as on the screen. One press, one step: SELECT picks the corner K1-K5 edit (pressed while another is
 *   held: both), MODE and FILTER step through their lists (wrapping), MUTE switches on / off. Steady colours.
 *   K1-K8: the selected planets: GEN 1, GEN 2, TUNE 2, BLEND, MODE, CUTOFF, RESO, LEVEL (source, filter, level).
 *   Faders: the path: FAMILY, SIZE, SPEED, MORPH (a new sound brings its own; a moved fader's value glides to it,
 *   then follows). Wheels: bend, suborbit size. SEQ: FREEZE (held: the ORBIT knobs).
 *   Buttons: < > the sounds; a set's button (CHORD VOICE, FX MIXER, FADER BANK MORE, ARP) tapped opens its knob
 *   set and it stays open (its light lit) until the button is tapped again or another set's is; held while a knob
 *   turns, the set is lent only while held. KNOB BANK opens and closes the guide (K1 scrolls it).
 *   BT: the SETUP set (MIDI channels, pad light, the sound at power-on). PATCH held: SAVE the sound in its place
 *   (let go), or with PARA: FACTORY (scenes.c).
 * Every touch is noted (ui.touch) for the screen: it shows the touched thing first, and a hint line. */
#if BOARD_SMK37
enum { B_KNOBBANK, B_FADERBANK, B_LEFT, B_RIGHT, B_OCTDN, B_OCTUP, B_ARP, B_REPEAT, B_SCALE, B_CHORD, B_GLOBE,
       B_BT, B_PATCH, B_PARA, B_FX, B_SEQ, B_PLAY, B_REC };
#define PANEL_KEYS() fm1_in.keys
#else   /* the FM-1's buttons (its matrix ids), named by the job they take over (panel_fm1.c): ENV = CHORD (VOICE), LFO =
         * FADER BANK (MORE), SAVE = PARA (RANDOM / UNDO), GLO = GLOBE (FOCUS) */
enum { B_OCTDN, B_OCTUP, B_FX, B_SEL, B_CHORD, B_FADERBANK, B_EDIT, B_GLOBE, B_HOME, B_PARA, B_ARP, B_SEQ, B_PLAY, B_REC };
#define B_SCALE 0xFF                               /* (no button: the WAVE set is page 4, panel_fm1.c) */
#define PANEL_KEYS() fm1_in.notes
#endif

/* the knob sets: the default (the corner), and one per button held */
enum { L_MAIN, L_ORBIT, L_VOICE, L_FX, L_CORNER, L_ARP, L_WAVE, L_SETUP, L_N };
typedef struct { uint8_t corner; uint8_t id; } kdest_t;    /* corner: 1 a corner parameter (of the selected), 2 a
                                                              setting (not the sound's: SDESC), 3 VOICES (the voice
                                                              mode and the stack in one: kd_voices) */
typedef struct { const char *name; uint8_t btn; kdest_t k[8]; } layer_t;
#define KC(x) {1, x}
#define KS(x) {2, x}
#define KV {3, 0}
static const char *const N_VOICES[2 + NVOICE] = {"POLY", "DUAL", "MONO 1", "MONO 2", "MONO 3", "MONO 4", "MONO 5", "MONO 6",
                                                 "MONO 7", "MONO 8"};
static const pdesc_t VDESC = {"VOICES", 0, 1 + NVOICE, 0, F_ENUM, N_VOICES};   /* POLY, DUAL, MONO x the stack */
enum { S_TRS, S_MIDIIN, S_BRIGHT, S_START, S_N };   /* the settings on a knob (scenes.c keeps them) */
static const pdesc_t SDESC[S_N] = {
    [S_TRS] = {"TRS MIDI OUT", 0, 16, 1, F_NUM, 0},    /* the 3.5 mm jack: OFF, or the channel (smk_midi_out.h) */
    [S_MIDIIN] = {"MIDI IN", 0, 16, 0, F_NUM, 0},      /* USB MIDI in: OMNI, or that channel only */
    [S_BRIGHT] = {"PAD LIGHT", 1, 4, 3, F_NUM, 0},     /* 25, 50, 75, 100 % (scenes.c keeps it as 2, 4, 6, 8) */
    [S_START] = {"START SOUND", 0, 3 * NPRESET - 1, 0, F_NUM, 0},   /* the sound at power-on: FACTORY 1..16, USER,
                                                                       FACTORY 2 17..32 (scenes.c) */
};
#define LAYER_HELD(l) ((l) > L_ORBIT)              /* a button held lends the knobs its set */
#define KG(x) {0, x}
static const layer_t LAYERS[L_N] = {
    [L_MAIN] = {"PLANET", 0xFF, {KC(CP_G1), KC(CP_G2), KC(CP_T2), KC(CP_BLEND), KC(CP_BMODE), KC(CP_CUT), KC(CP_RES), KC(CP_LEVEL)}},
#if BOARD_SMK37
    [L_ORBIT] = {"ORBIT", 0xFF, {KG(G_WARPX), KG(G_WARPY), KG(G_ROT), KG(G_SHARP), KG(G_SUBSZ), KG(G_SPIN), KG(G_WIDTH), KG(G_TEMPO)}},   /* (no planet selected; RESTART: REPEAT) */
#else   /* (the FM-1: no mod wheel, MOON SPEED here; BPM: the ARP set, PLAY) */
    [L_ORBIT] = {"ORBIT", 0xFF, {KG(G_WARPX), KG(G_WARPY), KG(G_ROT), KG(G_SHARP), KG(G_SUBSZ), KG(G_SPIN), KG(G_WIDTH), KG(G_SUBSPD)}},
#endif
    [L_VOICE] = {"VOICE", B_CHORD, {KG(G_AA), KG(G_AD), KG(G_AS), KG(G_AR), KG(G_DETUNE), KG(G_GLIDE), KV, KG(G_STEREO)}},
    [L_FX] = {"MIXER", B_FX, {KG(G_RVMIX), KG(G_DLMIX), KG(G_CHMIX), KG(G_DRIVE), KG(G_RVSIZE), KG(G_DLTIME), KG(G_DLFB), KG(G_VOLUME)}},
    [L_CORNER] = {"MORE", B_FADERBANK, {KC(CP_T1), KC(CP_TRACK), KC(CP_FTYPE), KG(G_DCOL), KG(G_CHDEP), KG(G_CHSPD), KG(G_RVCOL), KG(G_AUDITION)}},
#if BOARD_SMK37
    [L_ARP] = {"ARP", B_ARP, {KG(G_ARP), KG(G_TEMPO), KG(G_OA), KG(G_OD), KG(G_OS), KG(G_OR), KG(G_BEND), KS(S_TRS)}},
#else   /* (no TRS MIDI OUT on the FM-1) */
    [L_ARP] = {"ARP", B_ARP, {KG(G_ARP), KG(G_TEMPO), KG(G_OA), KG(G_OD), KG(G_OS), KG(G_OR), KG(G_BEND), {0xFF, 0}}},
#endif
    /* the selected planets' wavetables: where each generator reads, how far the moon moves it (SCALE held) */
    [L_WAVE] = {"WAVE", B_SCALE, {KC(CP_WP1), KC(CP_WO1), KC(CP_WP2), KC(CP_WO2), {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}}},
#if BOARD_SMK37   /* the settings, not the sound's (kept in the store a few seconds after the last change) */
    [L_SETUP] = {"SETUP", B_BT, {KS(S_TRS), KS(S_MIDIIN), KS(S_BRIGHT), KS(S_START), {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}}},
#else   /* (the FM-1: no store, no set) */
    [L_SETUP] = {"SETUP", 0xFF, {{0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}}},
#endif
};
static const uint8_t FADER_G[4] = {G_SHAPE, G_SIZE, G_RATE, G_MORPH};   /* the faders: the path */
enum { PG_SELECT, PG_MODE, PG_FILTER, PG_MUTE };   /* the pad groups, left to right */
/* each knob's colour: its label below the orbit, its satellite at the selected planet */
static const uint8_t KCOL[8][3] = {{255, 90, 90}, {255, 160, 60}, {240, 225, 70}, {100, 225, 110},
                                   {60, 210, 230}, {90, 140, 255}, {190, 110, 255}, {255, 110, 200}};
#define MANUAL_H 1250                              /* the guide's height, pixels (draw.c) */
enum { TK_NONE, TK_CKNOB, TK_GKNOB, TK_PAD, TK_FADER, TK_WHEEL };   /* what was touched last (the screen shows it) */

static struct {
    uint8_t sel;                           /* the corners K1-K5 edit (bits) */
    uint8_t preset;                        /* the sound's slot in its bank */
    uint8_t bank;                          /* its bank: BK_F1 FACTORY, BK_USER, BK_F2 FACTORY 2 */
    uint8_t list;                          /* the sound list is open (PATCH) */
    uint8_t lbank;                         /* ... showing this bank */
    uint32_t list_t;                       /* ... touched last (it closes 10 s after) */
    uint32_t patch_tap;                    /* PATCH pressed at (ms | 1): again within 350 ms: ALL NOTES OFF */
    uint16_t lpad, lsaved;                 /* pads pressed in the list; ... held long enough to SAVE (done) */
    int8_t oct;
    uint8_t layer;                         /* the knob set now (a set open, or its button held) */
    uint8_t latch;                         /* the set left open by a tap of its button (0: none) */
    struct { uint8_t kind, k, cmask, grp, layer; uint32_t until; } touch;   /* (layer: the knob set then) */
    uint8_t arp_last;                      /* the arpeggiator's mode when on */
    uint8_t dirty;
    char msg[28];
    uint32_t msg_until;
    uint8_t kturn;                         /* the knob turned last (+1), shown bright until kturn_t */
    uint32_t kturn_t;
    uint32_t enc_t[8];                     /* (a knob of the set: 0..7) */
    uint32_t btn_t[FM1_NBTN];              /* held since (ms | 1), 0 = up */
    uint8_t btn_used[FM1_NBTN];            /* a knob turned while it was held: no tap on release */
    uint32_t pad_t[16];                    /* pads held since (ms | 1) */
    uint64_t keys;
    uint8_t fad_last[4], fad_ready;        /* the faders as last taken; ready after the first readings */
    uint8_t fad_mode[4];                   /* 0: the sound's value; 1: gliding to the fader (from, since); 2: following */
    int16_t fad_from[4];
    uint32_t fad_t0[4];
    int8_t fad_dir[4];                     /* the last move's direction (the deadband lets a slow move through) */
    uint32_t fad_t[4];
    int16_t mw_last;                       /* the mod wheel (MOON SPEED) as last taken, 0..127 (-1: not yet) */
    int16_t mw_ref;                        /* (where it was when last shown) */
    int32_t mw_f;                          /* (its reading, smoothed, x 8) */
    uint8_t mw_mode;                       /* as fad_mode: 0 the sound's value, 1 gliding, 2 following */
    int8_t mw_dir;
    int16_t mw_from;
    uint32_t mw_t0, mw_t;
    uint32_t mute_t[NC];                   /* a planet switched on / off at (ms | 1): its animation */
    uint8_t sel_pend, sel_used;            /* SELECT pads held: a selected one's tap takes it out (unless another
                                              was pressed while it was held: then that one was added) */
    uint32_t moon_t, path_t;               /* the moon, the path changed (by a knob or the wheel): they light up */
    uint8_t help;                          /* the guide is open */
    int16_t mscroll;                       /* its scroll position, pixels (K1) */
    uint32_t tap_t;
    struct { uint8_t on, corner, note; uint32_t t, rel; } aud;   /* AUDITION: a planet heard alone (since t; released at rel) */
    patch_t undo;                          /* before the last randomize */
    uint8_t has_undo;
    uint32_t set_t;                        /* a setting changed at (ms | 1): saved a while after (settings_tick) */
} ui = {.sel = 0u, .arp_last = ARP_UP, .mw_last = -1};

static void ui_say(const char *a, const char *b)   /* the hint line */
{
    str_cpy(ui.msg, a, sizeof ui.msg);
    if (b)
        str_cat(ui.msg, b, sizeof ui.msg);
    ui.msg_until = fm1_ms + 2000u;
    ui.dirty = 1;
}
static void touched(uint32_t kind, uint32_t k, uint32_t cmask, uint32_t grp)
{
    ui.touch.kind = (uint8_t)kind, ui.touch.k = (uint8_t)k, ui.touch.cmask = (uint8_t)cmask, ui.touch.grp = (uint8_t)grp;
    ui.touch.layer = ui.layer;
    ui.touch.until = fm1_ms + 1500u;
    ui.dirty = 1;
}

/* ------------------------------------------------------------- TRS MIDI OUT --- */
/* the 3.5 mm jack (smk_midi_out.h): the keys as played (note on with the key's velocity, its note off: the note it
 * was sent with, whatever the octave since), the pitch wheel (pitch bend, centre at rest) and the mod wheel (CC 1),
 * on the channel set (SETUP K1 or ARP K8: OFF, CH1 .. CH16; channel 1 unless changed); not the arpeggiator, the latch or
 * the sound. Only to the jack: USB MIDI out stays quiet (a computer sending it back would play the notes twice). */
#define TRS_CH ((uint32_t)settings.trs_ch & 15u)
#define TRS_GAP 4u                                 /* ms between two bends / two mod values (the line's pace) */
#define TRS_PKT(cin, st, d1, d2) ((uint32_t)(cin) | (uint32_t)(st) << 8 | (uint32_t)(d1) << 16 | (uint32_t)(d2) << 24)
static struct {
    uint64_t on;                                   /* keys whose note on went out */
    uint8_t note[37];                              /* ... with this note */
    int32_t bend, modw;                            /* as last sent (modw -1: not yet) */
    uint32_t bend_t, modw_t;
} trs = {.bend = 8192, .modw = -1};
static void trs_note(uint32_t k, uint32_t n, uint32_t vel)   /* vel 0: the key let go */
{
    if (vel) {
        trs.on |= 1ull << k;
        trs.note[k] = (uint8_t)n;
        smo_put(TRS_PKT(9u, 0x90u | TRS_CH, n, vel));
    } else if ((trs.on >> k) & 1u) {
        trs.on &= ~(1ull << k);
        smo_put(TRS_PKT(8u, 0x80u | TRS_CH, trs.note[k], 0u));
    }
}
static void trs_bend(int32_t x)                    /* the pitch wheel, -32767..32767 */
{
    int32_t v = clamp(8192 + x / 4, 0, 16383);
    if (v == trs.bend || (v != 8192 && fm1_ms - trs.bend_t < TRS_GAP))
        return;
    trs.bend = v, trs.bend_t = fm1_ms;
    smo_put(TRS_PKT(14u, 0xE0u | TRS_CH, (uint32_t)v & 127u, (uint32_t)v >> 7));
}
static void trs_modw(int32_t y)                    /* the mod wheel, 0..32767 (smoothed) */
{
    int32_t v, d;
    y = clamp(y, 0, 32767);
    v = y * 127 / 32767, d = v - trs.modw;
    if (v == trs.modw || fm1_ms - trs.modw_t < TRS_GAP || (trs.modw >= 0 && d < 2 && d > -2 && y && y != 32767))
        return;                                    /* (a wobble of one step at rest: not sent; the ends always) */
    trs.modw = v, trs.modw_t = fm1_ms;
    smo_put(TRS_PKT(11u, 0xB0u | TRS_CH, 1u, (uint32_t)v));
}
static void trs_all_off(void)                      /* before TRS MIDI OUT goes OFF: nothing left hanging */
{
    uint32_t k;
    for (k = 0; k < 37u; k++)
        trs_note(k, 0, 0);
    if (trs.bend != 8192)
        trs.bend_t = 0, trs_bend(0);
}

/* ------------------------------------------------------------- values --- */
static const pdesc_t *kd_desc(const kdest_t *k)
{
    return k->corner == 0xFF ? 0 : k->corner == 3u ? &VDESC : k->corner == 2u ? &SDESC[k->id] : k->corner ? &CDESC[k->id] : &GDESC[k->id];
}
static uint32_t sel_first(void)                    /* the first selected corner (shown) */
{
    uint32_t c;
    for (c = 0; c < NC; c++)
        if ((ui.sel >> c) & 1u)
            return c;
    return 0;
}
static int32_t set_get(uint32_t id)
{
    switch (id) {
    case S_TRS:
        return trs_out ? 1 + (int32_t)TRS_CH : 0;
    case S_MIDIIN:
        return settings.midi_in;
    case S_BRIGHT:
        return (settings.bright + 1) / 2;
    default:
        return settings.preset;
    }
}
static int32_t kd_voices(void) { return pt.g[G_POLY] == PM_MONO ? 1 + clamp(pt.g[G_STACK], 1, NVOICE) : pt.g[G_POLY] % PM_N; }
static int32_t kd_get(const kdest_t *k)
{
    if (k->corner == 3u)
        return kd_voices();
    if (k->corner == 2u)                           /* the settings */
        return set_get(k->id);
    return k->corner == 0xFF ? 0 : k->corner ? pt.c[sel_first()][k->id] : pt.g[k->id];
}

static void val_text(const pdesc_t *d, int32_t v, uint32_t id, int corner, char *b, uint32_t n)
{
    if (!d) {
        b[0] = 0;
        return;
    }
    if (!corner && id == G_SHAPE) {                /* FAMILY: the nearest family's name */
        str_cpy(b, N_SHAPE[FAM_ORDER[clamp((v + FAM_STEP / 2) / FAM_STEP, 0, SH_N - 1)]], n);
        return;
    }
    if (corner && id == CP_BLEND) {                /* left: added, centre: G1, right: the mode */
        if (v == 0)
            str_cpy(b, "G1", n);
        else {
            b[0] = v < 0 ? '+' : 'x';
            fmt_int(b + 1, v < 0 ? -v : v);
        }
        return;
    }
    switch (d->fmt) {
    case F_ENUM:
        str_cpy(b, d->names[clamp(v, d->min, d->max) - d->min], n);
        break;
    case F_ONOFF:
        str_cpy(b, v ? "ON" : "OFF", n);
        break;
    case F_SIGNED:
        if (v > 0) {
            b[0] = '+';
            fmt_int(b + 1, v);
        } else
            fmt_int(b, v);
        break;
    default:
        fmt_int(b, v);
    }
}
/* a knob's label and value in the layer now (for the screen) */
static void slot_label(char *b, uint32_t n, uint32_t bank, uint32_t slot);
static void list_knob(uint32_t k, int32_t s);
static void knob_text(uint32_t k, const char **label, char *val, uint32_t n)
{
    const kdest_t *d = &LAYERS[ui.layer].k[k];
    const pdesc_t *p = kd_desc(d);
    *label = p ? p->label : "";
    if (d->corner == 2u) {                         /* the settings (TRS MIDI OUT: the UART never finished a byte: NOT SENDING;
                                                      a turn up tries again) */
        int32_t v = set_get(d->id);
        char nb[8] = "CH";
        if (d->id == S_START)
            slot_label(val, n, (uint32_t)v / NPRESET, (uint32_t)v % NPRESET);   /* (USER: "U3 ...", or EMPTY: then
                                                                                    the factory sound at power-on) */
        else if (d->id == S_BRIGHT) {
            static const char *const PL_TXT[4] = {"25%", "50%", "75%", "100%"};
            str_cpy(val, PL_TXT[clamp(v, 1, 4) - 1], n);
        }
        else {
            fmt_int(nb + 2, v);
            str_cpy(val, !v ? (d->id == S_TRS ? "OFF" : "OMNI") : d->id == S_TRS && smo.dead ? "NOT SENDING" : nb, n);
        }
        return;
    }
    val_text(p, kd_get(d), d->id, d->corner == 1u, val, n);
}

static int32_t accel(uint32_t e, int32_t s, int32_t span)
{
    uint32_t dt = fm1_ms - ui.enc_t[e];
    ui.enc_t[e] = fm1_ms;
    if (span > 64 && dt < 40u)
        return s * 3;
    return s;
}

/* ------------------------------------------------------------- sounds --- */
/* three banks of sixteen: FACTORY (sounds 1..16) and FACTORY 2 (17..32), never changed, and USER (yours: SAVE, a pad
 * held in the sound list; scenes.c keeps them). The sound now: ui.bank and its slot ui.preset. The banks in turn
 * (the list's K1, < >): FACTORY, USER, FACTORY 2 (as SETUP's START SOUND and the store number them) */
#define BK_F1 0u
#define BK_USER 1u
#define BK_F2 2u
#define NBANK 3u
static uint32_t bank_step(uint32_t b, int32_t d) { return (b + (d < 0 ? NBANK - 1u : 1u)) % NBANK; }
static uint32_t bank_first(uint32_t b) { return b == BK_F2 ? NPRESET : 0u; }   /* (its first factory sound) */
static uint32_t sound_fact(void)                   /* the factory sound the sound now is (a USER slot: it came from) */
{
    return ui.bank == BK_USER ? usr.src[ui.preset] % NFACT : bank_first(ui.bank) + ui.preset;
}
static void faders_release(void);
static void sound_reset(void)                      /* a new sound: as it comes */
{
    ui.sel = 0;                                    /* (nothing selected, the knobs are the orbit) */
    faders_release();                              /* (the faders take over the new path softly) */
    ui.has_undo = 0;
    focus = 0;
    latch = 0;
    ui.dirty = 1;
}
static void preset_load(uint32_t n)                /* factory sound n (0..31: FACTORY 1..16, FACTORY 2 17..32) */
{
    n %= NFACT;
    ev_push(EV_ALLOFF, 0, 0);
    preset_make(n, &pt);
    ui.bank = (uint8_t)(n >= NPRESET ? BK_F2 : BK_F1);
    ui.preset = (uint8_t)(n % NPRESET);
    sound_reset();
}
static int user_load(uint32_t n)                   /* USER slot n (empty: nothing changes, -1) */
{
    if (!usr_has(n))
        return -1;
    ev_push(EV_ALLOFF, 0, 0);
    pt = usr.p[n];
    ui.bank = BK_USER;
    ui.preset = (uint8_t)n;
    sound_reset();
    return 0;
}
static void sound_start(uint32_t s)                /* the sound at power-on (SETUP K4: FACTORY 1..16, USER 1..16, FACTORY 2
                                                      17..32; an empty USER slot: the factory sound of its number) */
{
    if (s >= 2u * NPRESET)
        preset_load(NPRESET + s % NPRESET);
    else if (s < NPRESET || user_load(s - NPRESET))
        preset_load(s % NPRESET);
}
static void slot_label(char *b, uint32_t n, uint32_t bank, uint32_t slot)   /* "3 GLASS BELLS", USER "U3 GLASS BELLS",
                                                                              FACTORY 2 "17 WARM STRINGS" */
{
    uint32_t user = bank == BK_USER;
    b[0] = 0;
    if (user)
        str_cpy(b, "U", n);
    fmt_int(b + (user ? 1 : 0), (user ? 0 : bank_first(bank)) + slot + 1);
    str_cat(b, " ", n);
    if (user && !usr_has(slot))
        str_cat(b, "EMPTY", n);
    else
        str_cat(b, PRESET_NAME[user ? usr.src[slot] % NFACT : (bank_first(bank) + slot) % NFACT], n);
}
static void preset_label(char *b, uint32_t n) { slot_label(b, n, ui.bank, ui.preset); }   /* the sound now */
static void sound_step(int32_t d)                  /* < >: the previous / next sound of its bank (USER: the saved ones) */
{
    uint32_t i, s = ui.preset;
    if (ui.bank != BK_USER) {
        preset_load(bank_first(ui.bank) + (ui.preset + NPRESET + (d > 0 ? 1u : NPRESET - 1u)) % NPRESET);
        return;
    }
    for (i = 0; i < USR_SLOTS; i++) {
        s = (s + (d > 0 ? 1u : USR_SLOTS - 1u)) % USR_SLOTS;
        if (usr_has(s)) {
            user_load(s);
            return;
        }
    }
}
/* SAVE: the sound as it is now into USER slot n (named after the factory sound it came from); then that slot plays */
static void sound_save(uint32_t n)
{
    char b[28];
    uint32_t src = sound_fact();
    if (usr_save(n, &pt, src)) {
        ui_say("NOT SAVED", 0);
        return;
    }
    ui.bank = BK_USER;
    ui.preset = (uint8_t)n;
    str_cpy(b, "SAVED ", sizeof b);
    slot_label(b + 6, sizeof b - 6, BK_USER, n);
    ui_say(b, 0);
}
static void notes_off(void)                        /* ALL NOTES OFF: every voice, the latch (PATCH twice quickly) */
{
    ev_push(EV_ALLOFF, 0, 0);
    latch = 0;
    ui_say("ALL NOTES OFF", 0);
}

/* RANDOMIZE: new sounds in the selected corners (the patterns, the blend, the filter); the old ones kept for undo */
static void randomize(void)
{
    static uint32_t seed = 0x6A09E667u;
    uint32_t c;
    seed ^= fm1_ms * 2654435761u;
    ui.undo = pt;
    ui.has_undo = 1;
    for (c = 0; c < NC; c++) {
        int16_t *q = pt.c[c];
        uint32_t r;
        if (ui.sel && !((ui.sel >> c) & 1u))
            continue;
        r = xorshift(&seed);
        q[CP_G1] = (int16_t)(r % 13u < 12u ? r % 6u : PAT_N);
        q[CP_G2] = (int16_t)((r >> 8) % 6u);
        q[CP_BLEND] = (int16_t)((int32_t)((r >> 12) % 128u) - 64);
        q[CP_BMODE] = (int16_t)((r >> 20) % BM_N);
        r = xorshift(&seed);
        q[CP_FTYPE] = (int16_t)(r % 8u < 5u ? FT_LP12 + (int32_t)(r % 2u) : (int32_t)(r >> 3) % (FT_N - 1));
        q[CP_CUT] = (int16_t)(30 + (r >> 8) % 80u);
        q[CP_RES] = (int16_t)((r >> 16) % 100u);
        q[CP_T2] = (int16_t)((r >> 24) % 4u == 0u ? 1 + (r >> 26) % 4u : 0);
    }
    ui_say(ui.sel ? "RANDOM: THE SELECTED PLANETS" : "RANDOM: ALL FOUR PLANETS", 0);
    touched(TK_PAD, 0, ui.sel ? ui.sel : 15u, PG_SELECT);
}

/* ------------------------------------------------------------- AUDITION --- */
/* a planet changed (a knob, a pad) and no key held: that planet alone (its corner soloed: through its blend and
 * filter) on C3, held while it keeps changing, let go 0.5 s after the last change; a key ends it at once */
static void audition(uint32_t c)
{
    if (!pt.g[G_AUDITION] || ui.keys)
        return;
    solo = (uint8_t)(1u << c);
    ui.aud.corner = (uint8_t)c, ui.aud.t = fm1_ms, ui.aud.rel = 0;
    if (!ui.aud.on) {
        ui.aud.on = 1;
        ui.aud.note = (uint8_t)clamp(48 + 12 * ui.oct, 0, 127);
        ev_push(EV_AUD_ON, ui.aud.note, 80);
    }
}
static void audition_tick(void)
{
    if (ui.aud.on && (ui.keys || fm1_ms - ui.aud.t > 500u)) {
        ev_push(EV_AUD_OFF, ui.aud.note, 0);
        ui.aud.on = 0;
        ui.aud.rel = fm1_ms | 1u;
        if (ui.keys)                               /* (a key: the whole sound again at once) */
            solo = 0, ui.aud.rel = 0;
    }
    if (ui.aud.rel && fm1_ms - ui.aud.rel > 1500u) /* (the release rang out: all corners again) */
        solo = 0, ui.aud.rel = 0;
}

/* ------------------------------------------------------------- settings --- */
static void set_turn(uint32_t id, int32_t s)
{
    const pdesc_t *p = &SDESC[id];
    int32_t was = set_get(id), v = clamp(was + s, p->min, p->max);
    if (id == S_TRS) {                             /* OFF, CH1 .. CH16 (a turn up on NOT SENDING tries the UART again) */
        if (v == was && !(s > 0 && smo.dead))
            return;
        if (trs_out)
            trs_all_off();                         /* (the notes held let go on the jack first, on their channel) */
        if (v)
            settings.trs_ch = (uint8_t)(v - 1);
        if (!v || !trs_out || smo.dead)
            smo_on(v != 0);
        settings.trs_off = (uint8_t)!v;
    } else {
        if (v == was)
            return;
        if (id == S_MIDIIN)
            settings.midi_in = (uint8_t)v, ev_push(EV_ALLOFF, 0, 0);   /* (notes of the old channel: off) */
        else if (id == S_BRIGHT)
            settings.bright = (uint8_t)(2 * v);
        else
            settings.preset = (uint8_t)v;
    }
    ui.set_t = fm1_ms | 1u;
}
static void settings_tick(void)                    /* a setting changed: kept 3 s after the last change, no key held */
{
    if (ui.set_t && (int32_t)(fm1_ms - ui.set_t) >= 3000 && !ui.keys && !ui.aud.on) {
        ui.set_t = 0;
        settings_save();
    }
}

/* ------------------------------------------------------------- knobs --- */
static void knob_turn(uint32_t k, int32_t s)
{
    const kdest_t *d = &LAYERS[ui.layer].k[k];
    const pdesc_t *p = kd_desc(d);
    uint32_t c;
    if (!p)
        return;
    if (d->corner == 1u && !ui.sel) {              /* (a planet's knob with none selected: the WAVE set) */
        ui_say("SELECT A PLANET FIRST", 0);
        return;
    }
    s = accel(k, s, p->max - p->min);
    if (d->corner == 2u) {                         /* a setting */
        if (!s)
            return;
        set_turn(d->id, s);
        ui.kturn = (uint8_t)(k + 1u);
        ui.kturn_t = fm1_ms + 1500u;
        touched(TK_GKNOB, k, 0u, 0);
        return;
    }
    if (d->corner == 3u) {                         /* VOICES: POLY, DUAL, MONO 1 .. 8 (the stack) */
        int32_t v = clamp(kd_voices() + (s > 0 ? 1 : -1), 0, 1 + NVOICE), was = pt.g[G_POLY];
        pt.g[G_POLY] = (int16_t)(v < 2 ? v : PM_MONO);
        if (v >= 2)
            pt.g[G_STACK] = (int16_t)(v - 1);
        if (pt.g[G_POLY] != was)
            ev_push(EV_ALLOFF, 0, 0);              /* (another voice mode: from silence) */
        ui.kturn = (uint8_t)(k + 1u);
        ui.kturn_t = fm1_ms + 1500u;
        touched(TK_GKNOB, k, 0u, 0);
        return;
    }
    if (d->corner) {                               /* every selected corner moves by the same step, from its own value */
        for (c = 0; c < NC; c++)
            if ((ui.sel >> c) & 1u)
                pt.c[c][d->id] = (int16_t)clamp(pt.c[c][d->id] + s, p->min, p->max);
    } else {
        pt.g[d->id] = (int16_t)clamp(pt.g[d->id] + s, p->min, p->max);
        if (d->id == G_ARP && pt.g[G_ARP])
            ui.arp_last = (uint8_t)pt.g[G_ARP];
        if (d->id == G_SUBSPD)                     /* (the moon's speed: the wheel glides from here on its next move) */
            ui.mw_mode = 0;
        if (d->id == G_SUBSPD || d->id == G_SUBSZ)
            ui.moon_t = fm1_ms | 1u;
        if (d->id == G_WARPX || d->id == G_WARPY || d->id == G_ROT || d->id == G_SHARP || d->id == G_SPIN)
            ui.path_t = fm1_ms | 1u;
    }
    ui.kturn = (uint8_t)(k + 1u);
    ui.kturn_t = fm1_ms + 1500u;
    touched(d->corner ? TK_CKNOB : TK_GKNOB, k, d->corner ? ui.sel : 0u, 0);
    if (d->corner) {
        audition(sel_first());
        if (ui.aud.on)
            solo = ui.sel;                         /* (several selected: heard together) */
    }
}
#if BOARD_SMK37
static void knobs_poll(void)
{
    uint32_t k, b;
    int32_t s;
    for (k = 0; k < 8u; k++) {
        if ((s = fm1_enc_take(k)) == 0)
            continue;
        if (ui.list) {                             /* the sound list: K1 the bank, K2 the sounds */
            list_knob(k, s);
            continue;
        }
        if (ui.help) {                             /* the guide: K1 scrolls it */
            if (k == 0u)
                ui.mscroll = (int16_t)clamp(ui.mscroll + s * 24, 0, MANUAL_H - 222);
            ui.dirty = 1;
            continue;
        }
        for (b = 0; b < FM1_NBTN; b++)             /* (a turn while a button is held: that button's set) */
            if (ui.btn_t[b])
                ui.btn_used[b] = 1;
        knob_turn(k, s);
    }
}
#endif

/* ------------------------------------------------------------- pads --- */
static const char *const CORNER_NAME[NC] = {"1", "2", "3", "4"};
static const char *const N_MUTE[2] = {"ON", "MUTE"};
/* the sound list (PATCH): the bank shown (< > or K1: FACTORY, USER, FACTORY 2), sixteen slots, one a pad (top row 1-8, bottom
 * row 9-16): a tap plays that sound, a pad held 1 s SAVEs the sound now into that USER slot; K2 steps through the
 * bank; PATCH again (or 10 s untouched) closes it; PATCH twice quickly: ALL NOTES OFF */
#define LIST_SAVE_MS 1000u
static void list_open(uint32_t on)
{
    ui.list = (uint8_t)on;
    ui.lbank = ui.bank;
    ui.lpad = ui.lsaved = 0;
    ui.list_t = fm1_ms;
    ui.msg_until = fm1_ms;                         /* (the top line: the list's own) */
    ui.dirty = 1;
}
static void list_pick(uint32_t bank, uint32_t slot)   /* a slot tapped: its sound */
{
    char b[28];
    if (bank == BK_USER ? user_load(slot) : (preset_load(bank_first(bank) + slot), 0)) {
        ui_say("EMPTY: HOLD THE PAD TO SAVE", 0);
        return;
    }
    preset_label(b, sizeof b);
    ui_say(b, 0);
}
static void list_pad_press(uint32_t pad)
{
    ui.list_t = fm1_ms;
    ui.pad_t[pad] = fm1_ms | 1u;
    if (pad < NPRESET)
        ui.lpad |= (uint16_t)(1u << pad);
    ui.dirty = 1;
}
static void list_pad_release(uint32_t pad)
{
    ui.pad_t[pad] = 0;
    if (!((ui.lpad >> pad) & 1u))
        return;
    ui.lpad &= (uint16_t)~(1u << pad);
    if (!((ui.lsaved >> pad) & 1u))
        list_pick(ui.lbank, pad);
    ui.lsaved &= (uint16_t)~(1u << pad);
    ui.list_t = fm1_ms;
    ui.dirty = 1;
}
static void list_tick(void)                       /* a pad held 1 s: SAVE there; untouched 10 s: closed */
{
    uint32_t pad;
    if (!ui.list)
        return;
    for (pad = 0; pad < NPRESET; pad++)
        if ((ui.lpad >> pad) & 1u && !((ui.lsaved >> pad) & 1u) && fm1_ms - (ui.pad_t[pad] & ~1u) >= LIST_SAVE_MS) {
            ui.lsaved |= (uint16_t)(1u << pad);
            sound_save(pad);
            ui.lbank = BK_USER;                    /* (the USER bank in view: the slot just saved) */
            ui.list_t = fm1_ms;
            ui.dirty = 1;
        }
    if (!ui.lpad && fm1_ms - ui.list_t >= 10000u)
        list_open(0);
}
static void list_knob(uint32_t k, int32_t s)      /* K1: the bank; K2: the previous / next sound of the bank shown */
{
    ui.list_t = fm1_ms;
    if (k == 0u)
        ui.lbank = (uint8_t)bank_step(ui.lbank, s);
    else if (k == 1u) {
        if (ui.lbank != ui.bank) {                 /* (the other bank: from its first slot) */
            uint32_t i;
            for (i = 0; i < NPRESET; i++)
                if (ui.lbank != BK_USER || usr_has(i)) {
                    list_pick(ui.lbank, i);
                    break;
                }
        } else {
            char b[28];
            sound_step(s);
            preset_label(b, sizeof b);
            ui_say(b, 0);
        }
    }
    ui.dirty = 1;
}

static void pad_press(uint32_t pad)               /* one press, one step */
{
    uint32_t grp = (pad & 7u) >> 1, c = (pad & 1u) | (pad >= 8u ? 2u : 0u), p, others = 0;
    int16_t *q = pt.c[c];
    char b[28];
    if (ui.list) {
        list_pad_press(pad);
        return;
    }
    ui.pad_t[pad] = fm1_ms | 1u;
    switch (grp) {
    case PG_SELECT:                                /* tap: this planet alone (a selected one: out, at the release);
                                                      pressed while another SELECT pad is held: added */
        for (p = 0; p < 16u; p++)
            if (p != pad && ui.pad_t[p] && ((p & 7u) >> 1) == PG_SELECT)
                others = 1;
        if (others)
            ui.sel |= (uint8_t)(1u << c), ui.sel_used = 0xF;
        else if ((ui.sel >> c) & 1u)
            ui.sel_pend |= (uint8_t)(1u << c), ui.sel_used &= (uint8_t)~(1u << c);
        else
            ui.sel = (uint8_t)(1u << c);
        if (focus && focus <= 4u)
            focus = (uint8_t)(c + 1u);             /* (focused: the orbit goes to it) */
        str_cpy(b, "PAD SELECT: PLANET ", sizeof b);
        str_cat(b, CORNER_NAME[c], sizeof b);
        break;
    case PG_MODE:
        q[CP_BMODE] = (int16_t)((q[CP_BMODE] + 1) % BM_N);
        str_cpy(b, "PAD MODE ", sizeof b);
        str_cat(b, CORNER_NAME[c], sizeof b);
        str_cat(b, ": ", sizeof b);
        str_cat(b, N_BMODE[q[CP_BMODE]], sizeof b);
        break;
    case PG_FILTER:
        q[CP_FTYPE] = (int16_t)((q[CP_FTYPE] + 1) % FT_N);
        str_cpy(b, "PAD FILTER ", sizeof b);
        str_cat(b, CORNER_NAME[c], sizeof b);
        str_cat(b, ": ", sizeof b);
        str_cat(b, N_FTYPE[q[CP_FTYPE]], sizeof b);
        break;
    default:
        q[CP_MUTE] ^= 1;
        ui.mute_t[c] = fm1_ms | 1u;                /* (the planet: flash and collapse, or ignite) */
        str_cpy(b, "PAD MUTE ", sizeof b);
        str_cat(b, CORNER_NAME[c], sizeof b);
        str_cat(b, ": ", sizeof b);
        str_cat(b, N_MUTE[q[CP_MUTE] & 1], sizeof b);
    }
    ui_say(b, 0);
    touched(TK_PAD, 0, 1u << c, grp);
    if (grp != PG_MUTE && !(grp == PG_SELECT && ((ui.sel_pend >> c) & 1u)))   /* (not a planet about to go out) */
        audition(c);
}
static void pad_release(uint32_t pad)
{
    uint32_t c = (pad & 1u) | (pad >= 8u ? 2u : 0u);
    if (ui.list || ((ui.lpad >> pad) & 1u)) {
        list_pad_release(pad);
        return;
    }
    ui.pad_t[pad] = 0;
    if (((pad & 7u) >> 1) == PG_SELECT && ((ui.sel_pend >> c) & 1u)) {
        ui.sel_pend &= (uint8_t)~(1u << c);
        if (!((ui.sel_used >> c) & 1u)) {         /* a tap on a selected planet: out of the selection */
            ui.sel &= (uint8_t)~(1u << c);
            ui_say("SELECT: ", ui.sel ? "PLANET OUT" : "NONE, KNOBS = ORBIT");
            touched(TK_PAD, 0, ui.sel, PG_SELECT);
        }
    }
}
#if BOARD_SMK37
static void pads_poll(void)
{
    int32_t e;
    while ((e = smk_pad_take()) >= 0) {
        uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 255u;
        if (vel)
            pad_press(pad);
        else
            pad_release(pad);
    }
}
#endif

/* ------------------------------------------------------------- buttons --- */
static void tap_tempo(void)
{
    uint32_t dt = fm1_ms - ui.tap_t;
    char b[8];
    ui.tap_t = fm1_ms;
    if (dt >= 250u && dt <= 1500u) {
        pt.g[G_TEMPO] = (int16_t)clamp((int32_t)(60000u / dt), 40, 240);
        fmt_int(b, pt.g[G_TEMPO]);
        ui_say("BPM ", b);
    } else
        ui_say("TAP", 0);
}
/* the knob sets: a set's button tapped leaves its set open (tapped again: closed; another set's: that one); held
 * while a knob turns, the set only while held. The open set shows in the central box (or its planet's bubble) and by
 * its button's light */
static uint32_t layer_of_btn(uint32_t b)
{
    uint32_t l;
    for (l = L_ORBIT + 1u; l < L_N; l++)
        if (LAYERS[l].btn == b)
            return l;
    return 0;
}
static void latch_tap(uint32_t b)                  /* a set's button tapped */
{
    uint32_t l = layer_of_btn(b);
    ui.latch = (uint8_t)(ui.latch == l ? 0u : l);
    ui.dirty = 1;
}
static uint32_t layer_now(void)                    /* the set the knobs play now */
{
    uint32_t l = ui.sel ? L_MAIN : L_ORBIT, b;
    if (ui.latch)
        l = ui.latch;
    for (b = L_ORBIT + 1u; b < L_N; b++)           /* (a set's button held: its set while held) */
        if (LAYERS[b].btn != 0xFF && ui.btn_t[LAYERS[b].btn] &&
            ((int32_t)(fm1_ms - ui.btn_t[LAYERS[b].btn]) >= 200 || ui.btn_used[LAYERS[b].btn]))
            l = b;
    if (ui.help)
        l = ui.sel ? L_MAIN : L_ORBIT;
    return l;
}
#if BOARD_SMK37
static void button_tap(uint32_t b)                 /* a button let go without a knob turned */
{
    switch (b) {
    case B_SEQ:                                    /* FREEZE: the orbit stands still; again: on */
        freeze ^= 1u;
        ui_say(freeze ? "FREEZE: THE ORBIT STANDS" : "FREEZE OFF: IT MOVES ON", 0);
        break;
    case B_CHORD:                                  /* the sets: open / closed (VOICES, POLY .. MONO: VOICE K7; */
    case B_ARP:                                    /* the arpeggiator: ARP K1) */
    case B_FX:
    case B_FADERBANK:
    case B_SCALE:
    case B_BT:                                     /* SETUP */
        latch_tap(b);
        break;
    case B_PARA:                                   /* randomize (held: undo, below) */
        randomize();
        break;
    default:
        break;
    }
    ui.dirty = 1;
}
static void button_press(uint32_t b)
{
    switch (b) {
    case B_LEFT:
    case B_RIGHT: {
        char nb[24];
        if (ui.list) {                             /* the sound list: the other banks in turn */
            ui.lbank = (uint8_t)bank_step(ui.lbank, b == B_RIGHT ? 1 : -1);
            ui.list_t = fm1_ms;
            break;
        }
        sound_step(b == B_RIGHT ? 1 : -1);
        preset_label(nb, sizeof nb);
        ui_say(nb, 0);
        break;
    }
    case B_OCTDN:
    case B_OCTUP: {
        char ob[4] = {0};
        ui.oct = (int8_t)clamp(ui.oct + (b == B_OCTUP ? 1 : -1), -3, 3);
        ob[0] = ui.oct < 0 ? '-' : '+';
        ob[1] = (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct));
        ui_say("OCTAVE ", ob);
        break;
    }
    case B_REPEAT:
        pt.g[G_RETRIG] ^= 1;
        ui_say("RESTART ", pt.g[G_RETRIG] ? "ON" : "OFF");
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
        tap_tempo();
        break;
    case B_KNOBBANK:
        break;                                     /* (the guide: buttons_poll) */
    case B_PATCH:                                  /* the sound list, open / closed; twice quickly: ALL NOTES OFF */
        if (ui.patch_tap && fm1_ms - ui.patch_tap < 350u) {
            ui.patch_tap = 0;
            list_open(0);
            notes_off();
        } else {
            ui.patch_tap = fm1_ms | 1u;
            list_open(!ui.list);
        }
        break;
    default:
        break;
    }
    ui.dirty = 1;
}
static void buttons_poll(void)
{
    uint32_t rel, pr = fm1_input_edges(&rel), b, l;
    for (b = 0; b < FM1_NBTN; b++) {
        if ((pr >> b) & 1u) {
            ui.btn_t[b] = fm1_ms | 1u;
            ui.btn_used[b] = 0;
            button_press(b);
        }
        if ((rel >> b) & 1u && ui.btn_t[b]) {
            int32_t held = (int32_t)(fm1_ms - ui.btn_t[b]);
            if (!ui.btn_used[b] && (held < 500 || layer_of_btn(b)))   /* (a set's button: no knob turned, open) */
                button_tap(b);
            ui.btn_t[b] = 0;
        }
    }
    /* holds: GLOBE 0.5 s: focus off; PARA 0.6 s: undo; KNOB BANK: help */
    if (ui.btn_t[B_GLOBE] && (int32_t)(fm1_ms - ui.btn_t[B_GLOBE]) >= 500 && focus) {
        focus = 0;
        ui_say("FOCUS OFF", 0);
    }
    if (ui.btn_t[B_PARA] && (int32_t)(fm1_ms - ui.btn_t[B_PARA]) >= 600 && !ui.btn_used[B_PARA]) {
        ui.btn_used[B_PARA] = 1;
        if (ui.has_undo) {
            patch_t t = pt;
            pt = ui.undo;
            ui.undo = t;                           /* (again: redo) */
            ui_say("UNDO", 0);
        } else
            ui_say("NOTHING TO UNDO", 0);
    }
    if ((pr >> B_KNOBBANK) & 1u) {                 /* KNOB BANK: the guide, open / closed */
        ui.help ^= 1u;
        ui.dirty = 1;
    }
    l = layer_now();                               /* the knob set: the planets or the orbit; an open set; a held one */
    if (l != ui.layer)
        ui.layer = (uint8_t)l, ui.dirty = 1;
}
#endif

/* ------------------------------------------------------------- wheels, faders, keys --- */
/* the mod wheel: MOON SPEED (the suborbit's speed). It stays where it is put: its place is the value; a new sound
 * brings its own, and the wheel's first deliberate move glides the value there (as the faders); a deadband */
static void mod_say(void)
{
    char b[28] = "MOD MOON SPEED ", v[8];
    fmt_int(v, pt.g[G_SUBSPD]);
    str_cat(b, v, sizeof b);
    ui_say(b, 0);
}
static void mod_glide(void)
{
    if (ui.mw_mode == 1u) {
        uint32_t t = fm1_ms - ui.mw_t0;
        if (t >= 400u)
            pt.g[G_SUBSPD] = ui.mw_last, ui.mw_mode = 2;
        else
            pt.g[G_SUBSPD] = (int16_t)(ui.mw_from + (ui.mw_last - ui.mw_from) * (int32_t)t / 400);
        ui.dirty = 1;
    }
}
#define WHEEL_FRESH 5                              /* (a new move of the wheel: at least 5 steps; then step by step) */
#define FADER_FRESH 5                              /* (of a fader: the same) */
static void mod_in(int32_t v)                       /* the wheel (or MIDI CC 1), Q15 0..32767 */
{
    int32_t raw = clamp(v, 0, 32767) * 127 / 32767, d = raw - ui.mw_last;
    int8_t dir = (int8_t)(d > 0 ? 1 : -1);
    if (ui.mw_last < 0) {                          /* (the first reading: where it is) */
        ui.mw_last = ui.mw_ref = (int16_t)raw;
        return;
    }
    if (!d || (d < WHEEL_FRESH && d > -WHEEL_FRESH && !(dir == ui.mw_dir && fm1_ms - ui.mw_t < 400u)))
        return;                                    /* (a wobble at rest: ignored; a move goes on step by step) */
    ui.mw_last = (int16_t)raw, ui.mw_dir = dir, ui.mw_t = fm1_ms;
    if (ui.mw_mode == 0u)
        ui.mw_mode = 1, ui.mw_from = pt.g[G_SUBSPD], ui.mw_t0 = fm1_ms;
    else if (ui.mw_mode == 2u)
        pt.g[G_SUBSPD] = (int16_t)raw;
    mod_glide();
    {   /* shown (the central box, the ring lit) for a move: more than 5 from where it was when last shown */
        int32_t showing = ui.touch.kind == TK_WHEEL && (int32_t)(ui.touch.until - fm1_ms) > 0, dr = raw - ui.mw_ref;
        if (showing || dr > 5 || dr < -5) {
            ui.mw_ref = (int16_t)raw, ui.moon_t = fm1_ms | 1u;
            mod_say();
            touched(TK_WHEEL, 0, 0, 0);
        }
    }
}
#if BOARD_SMK37
/* the pitch and mod wheels, self-calibrating (SLOOP's smk_wheels): X springs back (bend), Y 0..32767 */
static void wheels_poll(void)
{
    enum { DEAD = 12, SPAN0 = 80, MOD_LO = 485, MOD_HI = 615 };
    static int32_t pc, plo, phi, mlo, mhi, rest;
    int32_t p = fm1_in.wheel[0], m = fm1_in.wheel[1], d, x;
    if (fm1_in.frames < 300u)
        return;
    if (!pc) {
        pc = p;
        plo = p - SPAN0;
        phi = p + SPAN0;
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
    }
    plo = p < plo ? p : plo;
    phi = p > phi ? p : phi;
    mlo = m < mlo ? m : mlo;
    mhi = m > mhi ? m : mhi;
    d = p - pc;
    if (d > -DEAD && d < DEAD) {
        x = 0;
        if (++rest > 200) {
            pc += d > 0 ? 1 : d < 0 ? -1 : 0;
            rest = 0;
        }
    } else {
        rest = 0;
        x = d > 0 ? (d - DEAD) * 32767 / (phi - pc - DEAD > 1 ? phi - pc - DEAD : 1)
                  : (d + DEAD) * 32767 / (pc - plo - DEAD > 1 ? pc - plo - DEAD : 1);
    }
    x = clamp(x, -32767, 32767);
    if (x && !joy_x)
        ui_say("PITCH BEND", 0);
    joy_x = x;
    trs_bend(x);                                   /* the jack: pitch bend */
    x = mhi - m <= DEAD ? 0 : clamp((mhi - m - DEAD) * 32767 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1), 0, 32767);
    ui.mw_f += (x * 8 - ui.mw_f) >> 3;             /* (smoothed: the reading is noisy) */
    x = (ui.mw_f + 4) >> 3;
    trs_modw(x);                                   /* the jack: CC 1 */
    if (x != joy_y || ui.mw_last < 0)
        mod_in(x);
    joy_y = x;
}
#endif

/* the faders: the path (FAMILY, SIZE, SPEED, MORPH). A new sound brings its own path values; the first deliberate
 * move of a fader makes its value glide to where the fader is (~0.4 s: the path morphs), then it follows the fader.
 * A deadband: a change of one step counts only as part of a move (the same way, soon after); a fader at rest never
 * starts a glide. */
#define GLIDE_MS 400u
/* F1 FAMILY has detents: the fader in five equal slots, one per family; the middle 70 % of a slot is that family,
 * exact (a plateau), the 15 % at each edge morph half-way to the neighbour (continuous across the slots) */
#define FAM_EDGE 38                                 /* (of 256: a slot's morphing edge) */
static int32_t family_of_raw(uint32_t raw)          /* raw 0..127 -> FAMILY 0..124 */
{
    int32_t p = (int32_t)raw * 5 * 256 / 128, sl = p >> 8, fr = p & 255, f = sl * 256;
    if (fr < FAM_EDGE)
        f -= (FAM_EDGE - fr) * 128 / FAM_EDGE;
    else if (fr > 255 - FAM_EDGE)
        f += (fr - (255 - FAM_EDGE)) * 128 / FAM_EDGE;
    return clamp(f, 0, 4 * 256) * FAM_STEP / 256;
}
static int32_t fader_value(uint32_t i, uint32_t raw)  /* raw 0..127 over the parameter's range */
{
    const pdesc_t *d = &GDESC[FADER_G[i]];
    if (FADER_G[i] == G_SHAPE)
        return family_of_raw(raw);
    return clamp(d->min + (int32_t)raw * (d->max - d->min + 1) / 128, d->min, d->max);
}
static void faders_release(void)                    /* a new sound: its values, until a fader moves */
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        ui.fad_mode[i] = 0;
    ui.mw_mode = 0;
}
static void fader_say(uint32_t i)                   /* the top line: "F2 SIZE 80" */
{
    char b[28] = {'F', (char)('1' + i), ' ', 0}, v[12];
    const pdesc_t *d = &GDESC[FADER_G[i]];
    str_cat(b, d->label, sizeof b);
    str_cat(b, " ", sizeof b);
    val_text(d, pt.g[FADER_G[i]], FADER_G[i], 0, v, sizeof v);
    str_cat(b, v, sizeof b);
    ui_say(b, 0);
}
static void faders_glide(void)                      /* (every pass of the main loop) */
{
    uint32_t i;
    mod_glide();
    for (i = 0; i < 4u; i++)
        if (ui.fad_mode[i] == 1u) {
            uint32_t t = fm1_ms - ui.fad_t0[i];
            int32_t to = fader_value(i, ui.fad_last[i]);
            if (t >= GLIDE_MS)
                pt.g[FADER_G[i]] = (int16_t)to, ui.fad_mode[i] = 2;
            else
                pt.g[FADER_G[i]] = (int16_t)(ui.fad_from[i] + (to - ui.fad_from[i]) * (int32_t)t / (int32_t)GLIDE_MS);
            ui.dirty = 1;
        }
}
#if BOARD_SMK37
static void faders_poll(void)
{
    uint32_t i;
    if (fm1_in.frames < 200u)                      /* (the first readings settle) */
        return;
    if (!ui.fad_ready) {
        ui.fad_ready = 1;
        for (i = 0; i < 4u; i++)
            ui.fad_last[i] = fm1_in.fader[i];
        faders_release();
        return;
    }
    for (i = 0; i < 4u; i++) {
        int32_t raw = fm1_in.fader[i], d = raw - ui.fad_last[i];
        int8_t dir = (int8_t)(d > 0 ? 1 : -1);
        if (!d)
            continue;
        if (d < FADER_FRESH && d > -FADER_FRESH && !(dir == ui.fad_dir[i] && fm1_ms - ui.fad_t[i] < 400u))
            continue;                              /* (a wobble at rest: ignored; a move goes on step by step) */
        ui.fad_last[i] = (uint8_t)raw, ui.fad_dir[i] = dir, ui.fad_t[i] = fm1_ms;
        if (ui.fad_mode[i] == 0u)                  /* the first move: glide from the sound's value */
            ui.fad_mode[i] = 1, ui.fad_from[i] = pt.g[FADER_G[i]], ui.fad_t0[i] = fm1_ms;
        else if (ui.fad_mode[i] == 2u)
            pt.g[FADER_G[i]] = (int16_t)fader_value(i, (uint32_t)raw);
        faders_glide();
        fader_say(i);
        touched(TK_FADER, i, 0, 0);
    }
    faders_glide();
}

static void keys_poll(void)
{
    uint64_t now = fm1_in.keys, ch = now ^ ui.keys;
    uint32_t k;
    ui.keys = now;
    if (fm1_in.frames < 100u)                      /* (the first readings settle) */
        return;
    for (k = 0; ch && k < 37u; k++)
        if ((ch >> k) & 1u) {
            int32_t n = clamp(48 + (int32_t)k + 12 * ui.oct, 0, 127);   /* the bottom key: C3 */
            uint32_t vel = fm1_in.key_vel[k];
            vel = vel ? (vel > 127u ? 127u : vel) : 100u;
            if ((now >> k) & 1u)
                ev_push(EV_ON, (uint32_t)n, vel);
            else
                ev_push(EV_OFF, (uint32_t)n, 0);
            trs_note(k, (uint32_t)n, (now >> k) & 1u ? vel : 0u);   /* the jack: as played */
        }
}
#endif

/* USB MIDI in: notes, pitch bend, the mod wheel (CC 1), all notes off (CC 123); any channel, or the one set (SETUP K2) */
static void midi_poll(void)
{
    while (mi_r != mi_w) {
        uint32_t pkt, cin, d1, d2;
        RING_PUBLISH();
        pkt = midi_in_q[mi_r % MQ];
        RING_PUBLISH();
        mi_r++;
        cin = pkt & 15u, d1 = (pkt >> 16) & 127u, d2 = (pkt >> 24) & 127u;
        if (settings.midi_in && cin >= 8u && cin <= 14u && ((pkt >> 8) & 15u) != settings.midi_in - 1u)
            continue;                              /* (MIDI IN on one channel: the others pass by) */
        if (cin == 9u && d2)
            ev_push(EV_ON, d1, d2);
        else if (cin == 8u || cin == 9u)
            ev_push(EV_OFF, d1, 0);
        else if (cin == 0xEu)
            joy_x = clamp(((int32_t)(d2 << 7 | d1) - 8192) * 4, -32767, 32767);
        else if (cin == 0xBu && d1 == 1u)
            mod_in((int32_t)d2 * 258);
        else if (cin == 0xBu && d1 == 123u)
            ev_push(EV_ALLOFF, 0, 0);
    }
}

/* ------------------------------------------------------------- LEDs --- */
/* steady, saturated, one colour per group (no flicker): SELECT blue, the selected corner bright; MODE orange; FILTER
 * purple; MUTE green, a muted corner red */
static const uint8_t GCOL[4][3] = {{40, 110, 255}, {255, 120, 0}, {170, 40, 255}, {40, 255, 80}};
static const uint8_t MUTED_COL[3] = {255, 0, 0};
/* the pads' levels (of 256 of the colour, at 100 % ~0.39 of the LEDs' full drive): a selected corner, the sound now,
 * a pad held in the list; the MODE, FILTER and MUTE groups; a muted corner (red); an unselected corner, the other
 * sounds; an empty USER slot (pale, still lit) */
enum { PK_FULL = 256, PK_MID = 150, PK_MUTED = 110, PK_DIM = 60, PK_EMPTY = 32 };
#define PK_SC 100u                                 /* (the 0.3.1 PAD LIGHT 8: the LEDs at most ~39 %) */
#if BOARD_SMK37

/* PAD LIGHT (SETUP): 25, 50, 75 or 100 %. The bright states follow it, the dim ones keep most of their light, so at
 * every step each state stays visible and apart from the next: a colour's brightest component v is scaled by
 * s + (1 - s) * K / (v + K), s the step, K = PL_KNEE the dim states' level at 100 % (an unselected corner). At 25 %
 * a pad at K keeps 62.5 % of its light, a full one ~39 %; at 100 % nothing changes. A lit component stays lit. */
#define PL_KNEE 23u
static uint32_t pl_c(uint32_t c, uint32_t gain)
{
    uint32_t o = (c * gain + 128u) >> 8;
    return c && !o ? 1u : o;
}
static void pad_light(uint32_t pad, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t s = (uint32_t)(settings.bright + 1u) / 2u * 64u, v = r > g ? r : g, gain;
    v = v > b ? v : b;
    if (v && s < 256u) {
        gain = s + (256u - s) * PL_KNEE / (v + PL_KNEE);
        r = pl_c(r, gain), g = pl_c(g, gain), b = pl_c(b, gain);
    }
    padled_set(pad, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static void pad_k(uint32_t pad, const uint8_t *col, uint32_t k)   /* a pad: its colour at level k (of 256) */
{
    pad_light(pad, col[0] * k * PK_SC >> 16, col[1] * k * PK_SC >> 16, col[2] * k * PK_SC >> 16);
}
static void ui_leds(void)
{
    uint32_t pad;
    for (pad = 0; pad < 16u; pad++) {
        uint32_t grp = (pad & 7u) >> 1, c = (pad & 1u) | (pad >= 8u ? 2u : 0u), k = PK_MID;
        const uint8_t *col = GCOL[grp];
        if (ui.list) {                             /* the sound list: a slot per pad (the sound now bright; USER: the
                                                      saved ones lit, empty ones pale) */
            static const uint8_t LCOL[NBANK][3] = {{255, 200, 90}, {80, 200, 255}, {255, 120, 200}};   /* (by bank) */
            col = LCOL[ui.lbank % NBANK];
            k = pad >= NPRESET ? 0u : ui.lbank == ui.bank && pad == ui.preset ? PK_FULL
                : ui.lbank == BK_USER && !usr_has(pad) ? PK_EMPTY : PK_DIM;
            if (pad < NPRESET && ((ui.lpad >> pad) & 1u))
                k = PK_FULL;
            pad_k(pad, col, k);
            continue;
        }
        if (grp == PG_SELECT)
            k = (ui.sel >> c) & 1u ? PK_FULL : PK_DIM;
        else if (grp == PG_MUTE && pt.c[c][CP_MUTE])
            col = MUTED_COL, k = PK_MUTED;
        pad_k(pad, col, k);
    }
    fm1_led_key(B_ARP, ui.layer == L_ARP || (pt.g[G_ARP] && fm1_ms % 1000u < 500u));   /* (its set lit; running: blinks) */
    fm1_led_key(B_REPEAT, pt.g[G_RETRIG] != 0);
    fm1_led_key(B_REC, latch);
    fm1_led_key(B_GLOBE, focus != 0);
    fm1_led_key(B_FX, ui.layer == L_FX);
    fm1_led_key(B_FADERBANK, ui.layer == L_CORNER);
    fm1_led_key(B_SCALE, ui.layer == L_WAVE);
    fm1_led_key(B_BT, ui.layer == L_SETUP);
    fm1_led_key(B_SEQ, freeze);
    fm1_led_key(B_CHORD, ui.layer == L_VOICE);
    fm1_led_key(B_KNOBBANK, ui.help);
    fm1_led_key(B_PATCH, ui.list);
    fm1_led_key(B_PARA, ui.has_undo);
    fm1_led_key(B_PLAY, fm1_ms % (60000u / (uint32_t)pt.g[G_TEMPO]) < 60u);   /* the beat */
    fm1_led_key(B_OCTDN, ui.oct < 0);
    fm1_led_key(B_OCTUP, ui.oct > 0);
}
#endif

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        ui.fad_last[i] = 0xFF;
    sound_start(settings.preset);
}

#if BOARD_SMK37
static void ui_input(void)
{
    pads_poll();
    keys_poll();
    wheels_poll();
    buttons_poll();
    knobs_poll();
    faders_poll();
    audition_tick();
    settings_tick();
    list_tick();
}
#else
#include "panel_fm1.c"                          /* the FM-1's panel: the knobs, buttons, keys, LEDs */
#endif
