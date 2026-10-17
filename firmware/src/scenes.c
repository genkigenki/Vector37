/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The SMK data area (storage.c, owner SMK37DATAVECTOR): the settings (OBJ_SETTINGS) and your sounds (OBJ_SCENE0:
 * one record, the USER bank: a copy of each sound you saved, in its slot). Written only when asked: SAVE (a pad held
 * in the sound list), and
 * the settings a while after the last change with no key held (an erase silences the audio for a moment). The FM-1
 * build keeps no store (Felucca's data there stays untouched): no saving there. */
#define SET_MAGIC 0x56335331u              /* "V3S1" */
typedef struct {
    uint32_t magic;
    uint8_t bright;                        /* pad LEDs 1..8 */
    uint8_t console;                       /* USB serial console (CDC) */
    uint8_t preset;                        /* the sound at power-on: 0..15 FACTORY 1..16, 16..31 USER 1..16, 32..47
                                              FACTORY 2 17..32 (an older record: 0..31, the same) */
    uint8_t trs_ch;                        /* TRS MIDI OUT channel - 1 (0..15); (0.2: reserved, 0) */
    uint8_t trs_off;                       /* TRS MIDI OUT OFF (0: ON, as at power-on before) */
    uint8_t midi_in;                       /* USB MIDI IN: 0 OMNI, 1..16 that channel only */
    uint8_t rsv[6];
} settings_t;
_Static_assert(sizeof(settings_t) == 16u, "settings: the 0.2 record's size");
static settings_t settings = {SET_MAGIC, 5, 1, 0, 0, 0, 0, {0}};

static int flash_probe(void);              /* the JEDEC id (vector37.c; the host tests: a simulated part) */
static uint8_t store_on;                   /* the SMK store is ours (boot): saving possible */

static void settings_save(void)
{
    if (flash_ok && store_on)
        st_save(OBJ_SETTINGS, &settings, sizeof settings);
}

/* your sounds, the USER bank: a header, then a sound per slot; a slot's bit set: saved. The sizes are in the header,
 * so a later version with more parameters reads these (the new ones at their defaults). After the sounds: each slot's
 * name (the factory sound it came from); a record without them (0.3's first build: a sound saved in its factory
 * sound's place) names slot n after factory sound n */
#define USR_MAGIC 0x56335531u              /* "V3U1" */
typedef struct {
    uint32_t magic;
    uint16_t nslot, nc, ncp, ng;           /* slots, corners, corner parameters, global parameters */
    uint32_t mask;                         /* the slots saved */
} usr_hdr_t;
#define USR_SLOTS NPRESET
static struct {
    usr_hdr_t h;
    patch_t p[USR_SLOTS];
    uint8_t src[USR_SLOTS];                /* the slot's name: the factory sound it came from (0..31) */
} usr;
_Static_assert(sizeof(patch_t) == ((uint32_t)NC * CP_N + G_COUNT) * 2u, "your sounds: a sound as the record has it");
_Static_assert(sizeof usr <= ST_PAYLOAD_MAX, "your sounds: one store record");

static int usr_has(uint32_t n) { return n < USR_SLOTS && ((usr.h.mask >> n) & 1u); }
static void usr_load_all(void)
{
    uint32_t n, k, c, sz;
    const usr_hdr_t *h = (const usr_hdr_t *)st_buf;
    st_hdr_t sh;
    usr.h.mask = 0;
    if (st_current(OBJ_SCENE0, &sh) < 0 || sh.len < sizeof *h || h->magic != USR_MAGIC || h->nc != NC || !h->ncp || !h->ng)
        return;
    sz = ((uint32_t)h->nc * h->ncp + h->ng) * 2u;
    if (sizeof *h + h->nslot * sz > sh.len)
        return;
    for (n = 0; n < USR_SLOTS && n < h->nslot; n++) {
        const uint8_t *q = st_buf + sizeof *h + n * sz;
        uint32_t at = sizeof *h + h->nslot * sz + n;    /* (its name, if the record has them) */
        int16_t v;
        if (!((h->mask >> n) & 1u))
            continue;
        usr.src[n] = (uint8_t)(at < sh.len && st_buf[at] < NFACT ? st_buf[at] : n % NPRESET);
        patch_init(&usr.p[n]);                 /* (parameters this record does not have: their defaults) */
        for (c = 0; c < NC; c++)
            for (k = 0; k < h->ncp; k++) {
                memcpy(&v, q + (c * h->ncp + k) * 2u, 2);
                if (k < CP_N)
                    usr.p[n].c[c][k] = v;
            }
        for (k = 0; k < h->ng; k++) {
            memcpy(&v, q + ((uint32_t)NC * h->ncp + k) * 2u, 2);
            if (k < G_COUNT)
                usr.p[n].g[k] = v;
        }
        patch_sanitize(&usr.p[n]);
        usr.h.mask |= 1u << n;
    }
}
static int usr_write(void)
{
    if (!flash_ok || !store_on)
        return -1;
    usr.h.magic = USR_MAGIC, usr.h.nslot = USR_SLOTS, usr.h.nc = NC, usr.h.ncp = CP_N, usr.h.ng = G_COUNT;
    return st_save(OBJ_SCENE0, &usr, sizeof usr);
}
static int usr_save(uint32_t n, const patch_t *p, uint32_t src)   /* SAVE: this sound into USER slot n, named after
                                                                    factory sound src; 0: saved (read back) */
{
    uint32_t was = usr.h.mask;
    if (n >= USR_SLOTS)
        return -1;
    usr.p[n] = *p;
    usr.src[n] = (uint8_t)(src % NFACT);
    usr.h.mask |= 1u << n;
    if (usr_write()) {
        usr.h.mask = was;
        return -1;
    }
    return 0;
}
static void persist_boot(void)
{
    settings_t s;
    flash_ok = (uint8_t)flash_probe();
    if (!flash_ok)
        return;
#if !BOARD_SMK37
    return;                                 /* (the FM-1: no store of ours; Felucca's data there stays untouched) */
#endif
    st_owner_check();                       /* another firmware's store: cleared (storage.c) */
    store_on = 1;
    if (st_load(OBJ_SETTINGS, &s, sizeof s) == (int)sizeof s && s.magic == SET_MAGIC) {
        settings = s;
        if (settings.bright < 1u || settings.bright > 8u)
            settings.bright = 5;
        if (settings.preset >= 3u * NPRESET)
            settings.preset = 0;
        settings.trs_ch &= 15u;
        settings.trs_off = settings.trs_off == 1u;
        if (settings.midi_in > 16u)
            settings.midi_in = 0;
    }
    trs_out = (uint8_t)!settings.trs_off;
    usr_load_all();
}
