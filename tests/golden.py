#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The screens at the old pace against their golden checksums: tests/golden.py DIR [--update]

DIR: tests/screen built with -DUI_FIELD_MS=28u -DUI_LOOP_MS=15u (a field frame every 30 ms, as e2ceaa0 drew them), its
.ppm / .png. tests/golden.txt: one line a screen, the SHA-256 of its RGB pixels. A screen that differs fails (the
look changed); --update writes the file anew (only when a screen is meant to change)."""
import glob
import hashlib
import os
import sys
from PIL import Image

GOLD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden.txt")


def main():
    d = sys.argv[1]
    have = {}
    for p in sorted(glob.glob(os.path.join(d, "*.ppm")) + glob.glob(os.path.join(d, "*.png"))):
        name = os.path.splitext(os.path.basename(p))[0]
        if name.startswith("fm1_"):
            continue
        have[name] = hashlib.sha256(Image.open(p).convert("RGB").tobytes()).hexdigest()
    if "--update" in sys.argv:
        with open(GOLD, "w") as f:
            for k in sorted(have):
                f.write(f"{have[k]}  {k}\n")
        print(f"  golden: {len(have)} screens written")
        return 0
    gold = {}
    for ln in open(GOLD):
        h, k = ln.split()
        gold[k] = h
    bad = [k for k in sorted(gold) if have.get(k) != gold[k]]
    new = sorted(set(have) - set(gold))
    for k in bad:
        print(f"  FAIL golden: {k} {'missing' if k not in have else 'differs'}")
    print(f"  {'ok  ' if not bad else 'FAIL'} golden: {len(gold) - len(bad)} of {len(gold)} screens as before, bit for bit"
          + (f" ({len(new)} new: {', '.join(new)})" if new else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
