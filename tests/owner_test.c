/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* The SMK store owner (storage.c st_owner_check) as Vector37 builds it: tag SMK37DATAVECTOR at 0xAF000;
 * another firmware's store (or an untagged one) is cleared, ours is kept, nothing outside FL_DATA touched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_flash_map.h"
static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (!FL_STORE_OK(off, n))
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#define FELUCCA_DATA_FAMILY "VECTOR"
#define FELUCCA_DATA_UNTAGGED_OK 0
#include "../firmware/src/storage.c"

static int bad;
static void check(const char *what, int ok) { printf("owner: %-64s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static int outside_kept(void)
{
    uint32_t i;
    for (i = 0; i < sizeof nor; i++)
        if ((i < 0x9D000u || i >= 0xB0000u) && nor[i] != (uint8_t)(i * 7u))
            return 0;
    return 1;
}
static void fill(void)
{
    uint32_t i;
    for (i = 0; i < sizeof nor; i++)
        nor[i] = (uint8_t)(i * 7u);
}

int main(void)
{
    static const char TAG[16] = "SMK37DATAVECTOR";
    uint8_t got[8];
    fill();
    memcpy(nor + 0xAF000u, "SMK37DATASLOOP\0", 16);
    st_owner_check();
    check("SLOOP's store: cleared, tagged ours", !memcmp(nor + 0xAF000u, TAG, 16) && nor[0x9D000u] == 0xFF &&
                                                     nor[0xAEFFFu] == 0xFF && st_owner_cleared);
    check("nothing outside the store and its tag touched", outside_kept());
    st_save(OBJ_SCENE0 + 3, "scene4", 6);
    st_owner_cleared = 0;
    st_owner_check();
    check("our store: kept", !st_owner_cleared && st_load(OBJ_SCENE0 + 3, got, 6) == 6 && !memcmp(got, "scene4", 6));
    fill();
    memcpy(nor + 0xAF000u, "SMK37DATAMAKKURO", 16);
    st_owner_cleared = 0;
    st_owner_check();
    check("Makkuro37's store: cleared, tagged ours", !memcmp(nor + 0xAF000u, TAG, 16) && nor[0x9D000u] == 0xFF &&
                                                         st_owner_cleared);
    check("... nothing outside touched", outside_kept());
    memset(nor + 0xAF000u, 0xFF, 0x1000u);
    st_save(OBJ_SCENE0 + 3, "scene4", 6);
    st_owner_check();
    check("untagged store: cleared (FELUCCA_DATA_UNTAGGED_OK 0)", st_owner_cleared && st_load(OBJ_SCENE0 + 3, got, 6) < 0);
    printf("%s\n", bad ? "OWNER TEST FAILED" : "owner test passed");
    return bad != 0;
}
