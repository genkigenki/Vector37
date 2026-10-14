/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Controls: one screen, everything at hand (docs/CONTROLS.md is the table this implements).
 *   Pads: four groups of 2 x 2 (columns 1-2, 3-4, 5-6, 7-8); in each group the four pads are the four corners,
 *   placed as on the screen. One press, one step: SELECT picks the corner K1-K5 edit (pressed while another is
 *   held: both), MODE and FILTER step through their lists (wrapping), MUTE switches on / off. Steady colours.
 *   K1-K8: the selected planets: GEN 1, GEN 2, TUNE 2, BLEND, MODE, CUTOFF, RESO, LEVEL (source, filter, level).
 *   Faders: the path: FAMILY, SIZE, SPEED, MORPH (a new sound brings its own; a moved fader's value glides to it,
 *   then follows). Wheels: bend, suborbit size. SEQ: FREEZE (held: the ORBIT knobs).
 *   Buttons: < > the sounds; a held button lends the knobs another set (ORBIT, VOICE, EFFECTS, CORNER+, ARP) while
 *   held, its tap an action (next shape, voice mode, ...); KNOB BANK opens and closes the guide (K1 scrolls it).
 * Every touch is noted (ui.touch) for the screen: it shows the touched thing first, and a hint line. */
enum { B_KNOBBANK, B_FADERBANK, B_LEFT, B_RIGHT, B_OCTDN, B_OCTUP, B_ARP, B_REPEAT, B_SCALE, B_CHORD, B_GLOBE,
       B_BT, B_PATCH, B_PARA, B_FX, B_SEQ, B_PLAY, B_REC };

/* the knob sets: the default (the corner), and one per button held */
enum { L_MAIN, L_ORBIT, L_VOICE, L_FX, L_CORNER, L_ARP, L_N };
typedef struct { uint8_t corner; uint8_t id; } kdest_t;    /* corner: 1 a corner parameter (of the selected), 2 a
                                                              setting (not the sound's: SDESC) */
typedef struct { const char *name; uint8_t btn; kdest_t k[8]; } layer_t;
#define KC(x) {1, x}
#define KS(x) {2, x}
enum { S_TRS, S_N };                               /* the settings on a knob */
static const pdesc_t SDESC[S_N] = {
    [S_TRS] = {"TRS MIDI OUT", 0, 1, 1, F_ONOFF, 0},   /* the 3.5 mm jack: the keys and wheels as played (smk_midi_out.h) */
};
#define LAYER_HELD(l) ((l) > L_ORBIT)              /* a button held lends the knobs its set */
#define KG(x) {0, x}
static const layer_t LAYERS[L_N] = {
    [L_MAIN] = {"PLANET", 0xFF, {KC(CP_G1), KC(CP_G2), KC(CP_T2), KC(CP_BLEND), KC(CP_BMODE), KC(CP_CUT), KC(CP_RES), KC(CP_LEVEL)}},
    [L_ORBIT] = {"ORBIT", 0xFF, {KG(G_WARPX), KG(G_WARPY), KG(G_ROT), KG(G_SHARP), KG(G_SUBSZ), KG(G_SPIN), KG(G_RETRIG), KG(G_TEMPO)}},   /* (no planet selected) */
    [L_VOICE] = {"VOICE", B_CHORD, {KG(G_AA), KG(G_AD), KG(G_AS), KG(G_AR), KG(G_DETUNE), KG(G_GLIDE), KG(G_STACK), KG(G_STEREO)}},
    [L_FX] = {"MIXER", B_FX, {KG(G_RVMIX), KG(G_DLMIX), KG(G_CHMIX), KG(G_DRIVE), KG(G_RVSIZE), KG(G_DLTIME), KG(G_DLFB), KG(G_VOLUME)}},
    [L_CORNER] = {"MORE", B_FADERBANK, {KC(CP_T1), KC(CP_TRACK), KC(CP_FTYPE), KG(G_DCOL), KG(G_CHDEP), KG(G_CHSPD), KG(G_RVCOL), KG(G_AUDITION)}},
    [L_ARP] = {"ARP", B_ARP, {KG(G_ARP), KG(G_TEMPO), KG(G_OA), KG(G_OD), KG(G_OS), KG(G_OR), KG(G_BEND), KS(S_TRS)}},
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
    uint8_t preset;
    int8_t oct;
    uint8_t layer;                         /* the knob set now (a button held) */
    struct { uint8_t kind, k, cmask, grp, layer; uint32_t until; } touch;   /* (layer: the knob set then) */
    uint8_t arp_last;                      /* the arpeggiator's mode when on */
    uint8_t dirty;
    char msg[28];
    uint32_t msg_until;
    uint8_t kturn;                         /* the knob turned last (+1), shown bright until kturn_t */
    uint32_t kturn_t;
    uint32_t enc_t[FM1_NENC];
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
 * on MIDI channel 1; not the arpeggiator, the latch or the sound. Only to the jack: USB MIDI out stays quiet (a
 * computer sending it back would play the notes twice). ARP held, K8: ON / OFF (ON at power-on). */
#define TRS_CH 0u                                  /* channel 1 */
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
    return k->corner == 0xFF ? 0 : k->corner == 2u ? &SDESC[k->id] : k->corner ? &CDESC[k->id] : &GDESC[k->id];
}
static uint32_t sel_first(void)                    /* the first selected corner (shown) */
{
    uint32_t c;
    for (c = 0; c < NC; c++)
        if ((ui.sel >> c) & 1u)
            return c;
    return 0;
}
static int32_t kd_get(const kdest_t *k)
{
    if (k->corner == 2u)                           /* (S_TRS, the only setting; 2: ON but not sending) */
        return trs_out ? 1 + smo.dead : 0;
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
static void knob_text(uint32_t k, const char **label, char *val, uint32_t n)
{
    const kdest_t *d = &LAYERS[ui.layer].k[k];
    const pdesc_t *p = kd_desc(d);
    *label = p ? p->label : "";
    if (d->corner == 2u) {                         /* (TRS MIDI OUT; the UART never finished a byte: turning it ON tries again) */
        str_cpy(val, !trs_out ? "OFF" : smo.dead ? "NOT SENDING" : "ON", n);
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
static void faders_release(void);
static void preset_load(uint32_t n)
{
    n %= NPRESET;
    ev_push(EV_ALLOFF, 0, 0);
    preset_make(n, &pt);
    ui.sel = 0;                                    /* (a new sound: nothing selected, the knobs are the orbit) */
    faders_release();                              /* (the faders take over the new path softly) */
    ui.preset = (uint8_t)n;
    ui.has_undo = 0;
    focus = 0;
    latch = 0;
    ui.dirty = 1;
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

/* ------------------------------------------------------------- knobs --- */
static void knob_turn(uint32_t k, int32_t s)
{
    const kdest_t *d = &LAYERS[ui.layer].k[k];
    const pdesc_t *p = kd_desc(d);
    uint32_t c;
    if (!p)
        return;
    s = accel(k, s, p->max - p->min);
    if (d->corner == 2u) {                         /* TRS MIDI OUT: right ON, left OFF (ON tries the UART again) */
        if (!s)
            return;
        if (s < 0 && trs_out)
            trs_all_off();                         /* (the notes held let go on the jack first) */
        smo_on(s > 0);
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
static void knobs_poll(void)
{
    uint32_t k, b;
    int32_t s;
    for (k = 0; k < 8u; k++) {
        if ((s = fm1_enc_take(k)) == 0)
            continue;
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

/* ------------------------------------------------------------- pads --- */
static const char *const CORNER_NAME[NC] = {"1", "2", "3", "4"};
static const char *const N_MUTE[2] = {"ON", "MUTE"};
static void pad_press(uint32_t pad)               /* one press, one step */
{
    uint32_t grp = (pad & 7u) >> 1, c = (pad & 1u) | (pad >= 8u ? 2u : 0u), p, others = 0;
    int16_t *q = pt.c[c];
    char b[28];
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
static void button_tap(uint32_t b)                 /* a button let go without a knob turned */
{
    switch (b) {
    case B_SEQ:                                    /* FREEZE: the orbit stands still; again: on */
        freeze ^= 1u;
        ui_say(freeze ? "FREEZE: THE ORBIT STANDS" : "FREEZE OFF: IT MOVES ON", 0);
        break;
    case B_CHORD:                                  /* POLY -> DUAL -> MONO */
        ev_push(EV_ALLOFF, 0, 0);
        pt.g[G_POLY] = (int16_t)((pt.g[G_POLY] + 1) % PM_N);
        ui_say(N_POLY[pt.g[G_POLY]], 0);
        break;
    case B_ARP:
        pt.g[G_ARP] = (int16_t)(pt.g[G_ARP] ? ARP_OFF : ui.arp_last);
        ui_say("ARP ", N_ARP[pt.g[G_ARP]]);
        break;
    case B_FX:                                     /* (a tap: say what holding does) */
        ui_say("HOLD FX: MIXER KNOBS", 0);
        break;
    case B_FADERBANK:
        ui_say("HOLD FADER BANK: MORE KNOBS", 0);
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
        preset_load(ui.preset + NPRESET + (b == B_RIGHT ? 1u : NPRESET - 1u));
        fmt_int(nb, ui.preset + 1);
        str_cat(nb, " ", sizeof nb);
        str_cat(nb, PRESET_NAME[ui.preset], sizeof nb);
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
    case B_GLOBE:                                  /* FOCUS: corner 1 2 3 4, the centre, ...; held: off */
        focus = (uint8_t)(focus % 5u + 1u);
        if (focus <= 4u)
            ui.sel = (uint8_t)(1u << (focus - 1u));
        ui_say("FOCUS ", focus == 5u ? "CENTRE" : CORNER_NAME[focus - 1u]);
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
    case B_PATCH:
        ev_push(EV_ALLOFF, 0, 0);
        latch = 0;
        ui_say("ALL NOTES OFF", 0);
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
            if (!ui.btn_used[b] && held < 500)
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
    l = ui.sel ? L_MAIN : L_ORBIT;                 /* the knob set: the planets or the orbit; a held button's */
    for (b = 1; b < L_N; b++)
        if (LAYERS[b].btn != 0xFF && ui.btn_t[LAYERS[b].btn] && ((int32_t)(fm1_ms - ui.btn_t[LAYERS[b].btn]) >= 200 || ui.btn_used[LAYERS[b].btn]))
            l = b;
    if (ui.help)
        l = ui.sel ? L_MAIN : L_ORBIT;
    if (l != ui.layer)
        ui.layer = (uint8_t)l, ui.dirty = 1;
}

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

/* USB MIDI in: notes, pitch bend, the mod wheel (CC 1), all notes off (CC 123) */
static void midi_poll(void)
{
    while (mi_r != mi_w) {
        uint32_t pkt, cin, d1, d2;
        RING_PUBLISH();
        pkt = midi_in_q[mi_r % MQ];
        RING_PUBLISH();
        mi_r++;
        cin = pkt & 15u, d1 = (pkt >> 16) & 127u, d2 = (pkt >> 24) & 127u;
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
static void ui_leds(void)
{
    uint32_t pad, sc = 4u + settings.bright * 12u;  /* pad brightness of 256 */
    for (pad = 0; pad < 16u; pad++) {
        uint32_t grp = (pad & 7u) >> 1, c = (pad & 1u) | (pad >= 8u ? 2u : 0u), k = 150;
        const uint8_t *col = GCOL[grp];
        if (grp == PG_SELECT)
            k = (ui.sel >> c) & 1u ? 256u : 60u;
        else if (grp == PG_MUTE && pt.c[c][CP_MUTE])
            col = MUTED_COL, k = 110;
        padled_set(pad, (uint8_t)(col[0] * k * sc >> 16), (uint8_t)(col[1] * k * sc >> 16), (uint8_t)(col[2] * k * sc >> 16));
    }
    fm1_led_key(B_ARP, pt.g[G_ARP] != 0);
    fm1_led_key(B_REPEAT, pt.g[G_RETRIG] != 0);
    fm1_led_key(B_REC, latch);
    fm1_led_key(B_GLOBE, focus != 0);
    fm1_led_key(B_FX, ui.layer == L_FX);
    fm1_led_key(B_FADERBANK, ui.layer == L_CORNER);
    fm1_led_key(B_SEQ, freeze);
    fm1_led_key(B_CHORD, ui.layer == L_VOICE);
    fm1_led_key(B_KNOBBANK, ui.help);
    fm1_led_key(B_PARA, ui.has_undo);
    fm1_led_key(B_PLAY, fm1_ms % (60000u / (uint32_t)pt.g[G_TEMPO]) < 60u);   /* the beat */
    fm1_led_key(B_OCTDN, ui.oct < 0);
    fm1_led_key(B_OCTUP, ui.oct > 0);
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        ui.fad_last[i] = 0xFF;
    preset_load(settings.preset);
}

static void ui_input(void)
{
    pads_poll();
    keys_poll();
    wheels_poll();
    buttons_poll();
    knobs_poll();
    faders_poll();
    audition_tick();
}
