/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The FM-1 build's flash guard (firmware/hal/fm1_flash_map.h without BOARD_SMK37): the app may write the update
 * staging (0xE0000..0xE4FFF) and nothing else: not the head, Felucca's store and settings, its autosave and FM6 bank,
 * the stock OTA's VM backup / BTIF / USR (0xE8000..0xFBFFF), key_mac; no store of its own (FL_STORE_OK never).
 *   cc -Ifirmware/hal -o fm1_flash_test tests/fm1_flash_test.c && ./fm1_flash_test */
#include <stdint.h>
#include <stdio.h>
#include "fm1_flash_map.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    fails += !ok;
}

int main(void)
{
    uint32_t off, allowed = 0, bad = 0;
    for (off = 0; off < 0x100000u; off += 0x1000u) {       /* every sector: only the staging */
        int g = FL_GUARD(off, 0x1000u) != 0, want = off >= 0xE0000u && off < 0xE5000u;
        allowed += (uint32_t)g;
        bad += (uint32_t)(g != want);
    }
    check("FL_GUARD: the 5 staging sectors 0xE0000..0xE4FFF, no other sector", allowed == 5u && bad == 0u);
    check("... a write over the staging's end refused", !FL_GUARD(0xE4F00u, 0x200u));
    check("... a wrapping length refused", !FL_GUARD(0xE0000u, 0xFFFFFFFFu));
    for (bad = 0, off = 0; off < 0x100000u; off += 0x1000u)
        bad += FL_STORE_OK(off, 0x1000u) != 0;
    check("no store of ours on the FM-1 (FL_STORE_OK refuses every sector)", bad == 0u);
    check("the protected ranges: the head, 0xE8000..0xFBFFF, key_mac", FL_HEAD_HI == 0x4000u && FL_BAK_LO == 0xE8000u &&
          FL_BAK_HI == 0xFC000u && FL_KEYMAC_LO == 0xFF000u && !FL_GUARD(0xE8F00u, 0x100u) && !FL_GUARD(0xFF000u, 16u));
    check("the FM-1's flash part only (JEDEC 0x856014)", FL_JEDEC_OK(0x856014u) && !FL_JEDEC_OK(0xEB6014u));
    printf(fails ? "fm1_flash_test: %d FAILED\n" : "fm1_flash_test: all ok\n", fails);
    return fails != 0;
}
