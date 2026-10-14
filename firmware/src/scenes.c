/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Settings in the SMK data area (storage.c, owner SMK37DATAVECTOR). Vector37 0.1 keeps no sounds of its own
 * yet: the store holds its settings only, written by nothing in normal play (an erase would pause the audio). */
#define SET_MAGIC 0x56335331u              /* "V3S1" */
typedef struct {
    uint32_t magic;
    uint8_t bright;                        /* pad LEDs 1..8 */
    uint8_t console;                       /* USB serial console (CDC) */
    uint8_t preset;                        /* the sound at power-on (0..NPRESET - 1) */
    uint8_t rsv[9];
} settings_t;
static settings_t settings = {SET_MAGIC, 5, 1, 0, {0}};

static void settings_save(void)
{
    if (flash_ok)
        st_save(OBJ_SETTINGS, &settings, sizeof settings);
}

static void persist_boot(void)
{
    settings_t s;
    uint32_t f = irq_save();
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());
    irq_restore(f);
    if (!flash_ok)
        return;
    st_owner_check();                       /* another firmware's store: cleared (storage.c) */
    if (st_load(OBJ_SETTINGS, &s, sizeof s) == (int)sizeof s && s.magic == SET_MAGIC) {
        settings = s;
        if (settings.bright < 1u || settings.bright > 8u)
            settings.bright = 5;
        if (settings.preset >= NPRESET)
            settings.preset = 0;
    }
}
