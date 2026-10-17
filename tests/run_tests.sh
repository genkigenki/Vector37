#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (makkuro)
# Host tests (no hardware), from the repo root:  tests/run_tests.sh [OUTDIR]
#   host    the engine: patterns, blends, aliasing, clicks, voice modes, presets to OUTDIR/*.wav, cost
#   screen  every control of docs/CONTROLS.md on the screen, to OUTDIR/*.png
#   board   the flash store and its owner tag, update loader, OTA entry, recovery (board layer tests)
#   fm1     the FM-1 build (experimental): its panel and screens, its flash guard, its package (BOARD=fm1 sh build.sh)
set -e
cd "$(dirname "$0")/.."
OUT=build/host
DIR="${1:-$OUT}"
mkdir -p "$OUT" build/gen "$DIR"
python3 tools/gen_tables.py build/gen/mk_tables.h >/dev/null
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
CC="${CC:-cc} -O2 -Wall -Wno-unused-function"
fail=0
$CC -Ibuild/gen -o "$OUT/host" tests/host.c -lm
"$OUT/host" "$DIR" || fail=1
$CC -w -Ibuild/gen -o "$OUT/screen" tests/screen.c
"$OUT/screen" "$DIR" || fail=1
# the old pace (a field frame every 30 ms): the screens as e2ceaa0 drew them, bit for bit (tests/golden.py compares)
$CC -w -DUI_FIELD_MS=28u -DUI_LOOP_MS=15u -Ibuild/gen -o "$OUT/screen_pace30" tests/screen.c &&
    mkdir -p "$OUT/pace30" && { "$OUT/screen_pace30" "$OUT/pace30" >"$OUT/screen_pace30.log" 2>&1; true; } &&
    python3 tests/golden.py "$OUT/pace30" || { echo "  FAIL screen at the old pace"; fail=1; }
# the frame rate: the main loop timed as the device would run it (tests/fps.c: the model, the floors)
$CC -DFPS_FLOOR=1 -Ibuild/gen -o "$OUT/fps" tests/fps.c && "$OUT/fps" || fail=1
# the FM-1 build (experimental): its panel and its screens (fm1_*.ppm)
$CC -Ibuild/gen -o "$OUT/fm1_panel" tests/fm1_panel.c &&
    "$OUT/fm1_panel" "$DIR" >"$OUT/fm1_panel.log" 2>&1 && echo "  ok   fm1_panel (the FM-1's panel, its screens)" ||
    { echo "  FAIL fm1_panel (build/host/fm1_panel.log)"; fail=1; }
python3 - "$DIR" <<'EOF' || fail=1
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
print("  screens: %d PNG" % len(glob.glob(os.path.join(sys.argv[1], "*.png"))))
EOF
$CC -o "$OUT/smk_midi_out_test" tests/smk_midi_out_test.c &&   # TRS MIDI OUT: the driver, as SLOOP's
    "$OUT/smk_midi_out_test" >"$OUT/smk_midi_out_test.log" 2>&1 && echo "  ok   smk_midi_out_test" ||
    { echo "  FAIL smk_midi_out_test (build/host/smk_midi_out_test.log)"; fail=1; }
PKG=build/vector37.fwsc
[ -f $PKG ] || { echo "run ./build.sh first"; exit 1; }
for t in "storage_test" "smk_flash_test" "owner_test" "recovery_test" "ota_test $PKG" "ldr_test ${OLD_PKG:-$PKG} $PKG"; do
    set -- $t
    $CC -w -DBOARD_SMK37=1 -Ifirmware/hal -Ifirmware/src -Ibuild/gen -o "$OUT/$1" "tests/$1.c" &&
        "$OUT/$1" $2 $3 >"$OUT/$1.log" 2>&1 && echo "  ok   $1" || { echo "  FAIL $1 (build/host/$1.log)"; fail=1; }
done
for m in 1 2; do                         # ota.c of the MODEL=elite / mke builds: their identity and update record
    $CC -w -DBOARD_SMK37=1 -DT_MODEL=$m -Ifirmware/hal -Ifirmware/src -Ibuild/gen -o "$OUT/ota_test_m$m" tests/ota_test.c &&
        "$OUT/ota_test_m$m" $PKG >"$OUT/ota_test_m$m.log" 2>&1 && echo "  ok   ota_test (model $m)" ||
        { echo "  FAIL ota_test model $m (build/host/ota_test_m$m.log)"; fail=1; }
done
# the FM-1 build: its flash guard; its package if built (BOARD=fm1)
$CC -Ifirmware/hal -o "$OUT/fm1_flash_test" tests/fm1_flash_test.c &&
    "$OUT/fm1_flash_test" >"$OUT/fm1_flash_test.log" 2>&1 && echo "  ok   fm1_flash_test" ||
    { echo "  FAIL fm1_flash_test (build/host/fm1_flash_test.log)"; fail=1; }
FPKG=build/vector37-fm1.fwsc
if [ -f $FPKG ]; then
    $CC -w '-DT_FM1_ID="FM-1_700"' -Ifirmware/hal -Ifirmware/src -Ibuild/gen -o "$OUT/ota_test_fm1" tests/ota_test.c &&
        "$OUT/ota_test_fm1" $FPKG >"$OUT/ota_test_fm1.log" 2>&1 && echo "  ok   ota_test (FM-1)" ||
        { echo "  FAIL ota_test FM-1 (build/host/ota_test_fm1.log)"; fail=1; }
    $CC -w -Ifirmware/hal -Ifirmware/src -Ibuild/gen -o "$OUT/ldr_test_fm1" tests/ldr_test.c &&
        "$OUT/ldr_test_fm1" $FPKG $FPKG >"$OUT/ldr_test_fm1.log" 2>&1 && echo "  ok   ldr_test (FM-1)" ||
        { echo "  FAIL ldr_test FM-1 (build/host/ldr_test_fm1.log)"; fail=1; }
fi
[ $fail = 0 ] && echo "all tests passed" || { echo "TESTS FAILED"; exit 1; }
