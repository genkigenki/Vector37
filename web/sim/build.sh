#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (makkuro)
# The Vector37 simulator: the firmware's sources to WebAssembly (Emscripten), plus the page.
#   web/sim/build.sh  ->  build/sim/{index.html, worklet.js, sim.wasm}
#   then: python3 -m http.server -d build/sim 8738, open http://localhost:8738
set -e
cd "$(dirname "$0")/../.."
[ -d "$HOME/emsdk/upstream/emscripten" ] && PATH="$HOME/emsdk/upstream/emscripten:$PATH"
command -v emcc >/dev/null 2>&1 || { echo "sim: no emcc (Emscripten: ~/emsdk)"; exit 1; }
mkdir -p build/gen build/sim
python3 tools/gen_tables.py build/gen/mk_tables.h >/dev/null
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
emcc -O2 -std=gnu11 -w -Ibuild/gen --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 \
    -sFILESYSTEM=0 -o build/sim/sim.wasm web/sim/sim.c
cp web/sim/index.html web/sim/worklet.js build/sim/
echo "sim: build/sim ($(wc -c < build/sim/sim.wasm | tr -d ' ') B wasm)"
