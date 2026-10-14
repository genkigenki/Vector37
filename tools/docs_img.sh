#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (makkuro)
# The docs' pictures: the screens of tests/screen.c (2x nearest) as docs/img/screen-*.png, and the screen in
# motion (tools/anim.c) as docs/img/orbit.webp and each sound playing as docs/img/preset-N-name.webp (1:1, 12.5
# frames a second) with a still docs/img/screen-preset-N-name.png.
#   sh tools/docs_img.sh        (needs build/gen: sh build.sh or tests/run_tests.sh first)
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
mkdir -p "$T/s" "$T/a" docs/img
cc -O2 -w -D_DEFAULT_SOURCE -Ibuild/gen -o "$T/screen" tests/screen.c
cc -O2 -w -D_DEFAULT_SOURCE -Ibuild/gen -o "$T/anim" tools/anim.c
"$T/screen" "$T/s" >/dev/null
"$T/anim" "$T/a"
for n in 0 1 2 3 4 5 6 7; do mkdir -p "$T/p$n"; "$T/anim" "$T/p$n" $n; done
mkdir -p "$T/f" "$T/g" "$T/pl"; "$T/anim" "$T/f" family; "$T/anim" "$T/g" glide; "$T/anim" "$T/pl" planet
rm -f docs/img/screen-*.png
python3 - "$T" docs/img <<'PY'
import glob, os, sys
from PIL import Image
t, out = sys.argv[1], sys.argv[2]
keep = {"orbit_playing": "orbit", "orbit_idle": "orbit-idle", "knob_cutoff": "knob-cutoff", "knob_blend_multi": "select-two",
        "pad_mode": "pad-mode", "pad_filter": "pad-filter", "pad_mute": "pad-mute", "modwheel_moon": "modwheel-moon", "orbit_knob": "orbit-knob", "orbit_moon_dist": "orbit-moon", "multi_turn": "multi", "moon_slow": "moon-slow", "moon_fast": "moon-fast", "mute_flash": "mute-flash", "mute_collapse": "mute-collapse", "mute_ignite": "mute-ignite", "fader_glide": "fader-glide", "fader_morph": "fader-morph", "freeze": "freeze", "audition": "audition", "pad_select": "select", "family_list": "family-list", "planet_edge": "planet-edge", "hold_faderbank_corner": "hold-faderbank",
        "fader_size": "fader", "hold_chord_voice": "hold-chord",
        "hold_fx_effects": "hold-fx", "focus_corner2": "focus", "boot_1600": "boot",
        "guide_0": "guide-1", "guide_1": "guide-2", "guide_2": "guide-3", "guide_3": "guide-4", "guide_4": "guide-5", "guide_5": "guide-6",
        "shape_0": "shape-lissajous", "shape_1": "shape-star", "shape_2": "shape-spiro", "shape_3": "shape-pulse",
        "shape_4": "shape-adsr",
        }
for k, v in keep.items():
    Image.open(os.path.join(t, "s", k + ".ppm")).resize((480, 480), Image.NEAREST).save(os.path.join(out, "screen-" + v + ".png"))
frames = [Image.open(p).convert("RGB") for p in sorted(glob.glob(os.path.join(t, "a", "*.ppm")))]
frames = frames[::2]
frames[0].save(os.path.join(out, "orbit.webp"), save_all=True, append_images=frames[1:], duration=80, loop=0, quality=80, method=4)
names = ["nebula-pad", "orbit-bass", "glass-bells", "talking", "deep-drone", "pluck-step", "sync-lead", "space-station"]
for n, name in enumerate(names):          # each sound playing: docs/img/preset-N-name.webp
    fr = [Image.open(p).convert("RGB") for p in sorted(glob.glob(os.path.join(t, f"p{n}", "*.ppm")))][::2]
    fr[0].save(os.path.join(out, f"preset-{n + 1}-{name}.webp"), save_all=True, append_images=fr[1:], duration=80, loop=0,
               quality=78, method=4)
    fr[len(fr) // 3].resize((480, 480), Image.NEAREST).save(os.path.join(out, f"screen-preset-{n + 1}-{name}.png"))
for d, name in (("f", "family"), ("g", "glide"), ("pl", "planet")):   # fader 1 through the families; the faders' glide after new sounds
    fr = [Image.open(p).convert("RGB") for p in sorted(glob.glob(os.path.join(t, d, "*.ppm")))][::2]
    fr[0].save(os.path.join(out, name + ".webp"), save_all=True, append_images=fr[1:], duration=80, loop=0, quality=78, method=4)
print(f"docs/img: {len(keep)} screens, {len(frames)} frames of motion, {len(names)} sounds, family, glide, planet")
PY
rm -rf "$T"
