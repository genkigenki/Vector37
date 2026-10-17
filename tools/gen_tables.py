#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (makkuro)
"""Fixed-point tables for Vector37 (no float on the target).  gen_tables.py OUT.h"""
import math
import sys

FS = 44100
CTL = 32                        # control block: samples per control tick
AMP = 10000                     # a generator's fundamental, Q15 units

# The six overtone patterns: which overtones n >= 2 sound, and their sign (amplitude +-1/n). The fundamental
# is always in. A..C even overtones, D..F odd ones; C (all even, +) and D (all odd, +) together: a sawtooth.
PATTERNS = {
    "A": lambda n: -1 if n % 8 == 2 else 0,
    "B": lambda n: -1 if n % 4 == 2 else 0,
    "C": lambda n: 1 if n % 2 == 0 else 0,
    "D": lambda n: 1 if n % 2 == 1 else 0,
    "E": lambda n: -1 if n % 4 == 3 else 0,
    "F": lambda n: -1 if n % 8 == 3 else 0,
}
# band b holds the increments up to 2^(20 + b) (top frequency 10.77 Hz x 2^b); bands 0..1 use band 2
BANDS = range(2, 12)
TOP_HZ = 25000.0                # the highest overtone at the band's top pitch (its alias lands above 19 kHz)


def arr(name, ctype, vals, per=12):
    out = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def expmap(v, lo, hi, n=127):
    return lo * (hi / lo) ** (v / n)


def wavetables():
    """per pattern, per band: one cycle (length 64..1024, a power of two, >= 4x the overtones) plus a guard
    sample; the fundamental included. Returns the flat table, the offsets [pattern][band] and the shifts."""
    flat, off, shift = [], [], []
    for b in BANDS:
        ftop = 2 ** (20 + b) * FS / 2 ** 32
        nmax = min(255, int(TOP_HZ / ftop))
        size = 64
        while size < 4 * nmax and size < 1024:
            size *= 2
        shift.append(32 - int(math.log2(size)))
    for name, sign in PATTERNS.items():
        row = []
        for bi, b in enumerate(BANDS):
            ftop = 2 ** (20 + b) * FS / 2 ** 32
            nmax = min(255, int(TOP_HZ / ftop))
            size = 2 ** (32 - shift[bi])
            parts = [(1, 1.0)] + [(n, sign(n) / n) for n in range(2, nmax + 1) if sign(n)]
            t = [round(AMP * sum(a * math.sin(2 * math.pi * n * i / size) for n, a in parts)) for i in range(size)]
            row.append(len(flat))
            flat += t + [t[0]]
        off.append(row)
    return flat, off, shift


# ---- the wavetable banks (pattern W1..W8): 8 banks of 32 frames, each frame one cycle in six band-limited levels
# (mip levels by octave band, as the overtone tables: band b (increments up to 2^(20 + b)) reads level max(0, b - 6);
# level 0 holds 32 harmonics in 128 samples (b 2..6: 32 x 689 Hz = 22 kHz), level 1 16 in 128 (b 7), 2 8 in 64, 3 4 in
# 32, 4 2 in 32, 5 the fundamental in 16: four samples per highest harmonic in level 0 (its top half faded), eight
# and more above, the highest at most ~22 kHz at the band's top pitch: read linearly, the interpolation's mirror
# images stay ~31 dB or more below every frame's spectrum, C4..C7, tests/host.c). Every bank is made here from a recipe (additive spectra, or a waveform in time whose harmonics a
# DFT takes): original, nothing sampled. A frame is normalized (RMS 8000, peak at most 28000); the engine morphs
# between two neighbouring frames per sample.
WT_NF = 32
WT_H = 32                                   # harmonics in level 0
WT_LVL = [(128, 32), (128, 16), (64, 8), (32, 4), (32, 2), (16, 1)]   # (samples, harmonics) per level
WT_NAMES = ["SCAN", "PULSE", "VOWEL", "ORGAN", "SYNC", "FM", "STEPS", "WARP"]
WT_DFT_N = 1024


def _dft(fn):
    """harmonics 1..WT_H of one period fn(x), x in [0, 1): (sin, cos) coefficients"""
    xs = [fn(i / WT_DFT_N) for i in range(WT_DFT_N)]
    out = []
    for n in range(1, WT_H + 1):
        a = b = 0.0
        for i, v in enumerate(xs):
            ph = 2 * math.pi * n * i / WT_DFT_N
            a += v * math.sin(ph)
            b += v * math.cos(ph)
        out.append((2 * a / WT_DFT_N, 2 * b / WT_DFT_N))
    return out


def _bank_spectra(k):
    """bank k: 32 frames, each [(sin, cos)] for harmonics 1..WT_H"""
    frames = []
    for f in range(WT_NF):
        t = f / (WT_NF - 1)
        if k == 0:      # SCAN: a resonance sliding up the harmonics of a soft saw
            n0 = 1.0 * (24.0 ** t)
            sp = [(n ** -1.1 * (0.25 + 4.0 * math.exp(-((n - n0) / (1.0 + 0.15 * n0)) ** 2)), 0.0)
                  for n in range(1, WT_H + 1)]
        elif k == 1:    # PULSE: the pulse's width 50 % .. 4 %
            w = 0.5 - 0.46 * t
            sp = [(0.0, 2 * math.sin(math.pi * n * w) / (math.pi * n)) for n in range(1, WT_H + 1)]
        elif k == 2:    # VOWEL: formants (for a 110 Hz voice) through A E I O U
            V = [(800, 1150, 2800), (400, 1700, 2600), (300, 2200, 3000), (450, 800, 2830), (325, 700, 2530)]
            u = t * (len(V) - 1)
            i0 = min(int(u), len(V) - 2)
            fr = u - i0
            fm = [V[i0][j] + (V[i0 + 1][j] - V[i0][j]) * fr for j in range(3)]
            sp = [(0.15 / n + sum(g * math.exp(-((n * 110 - F) / bw) ** 2) for F, g, bw in
                                  zip(fm, (1.0, 0.5, 0.25), (90, 120, 160))), 0.0) for n in range(1, WT_H + 1)]
        elif k == 3:    # ORGAN: drawbars (16' 8' 5 1/3' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1') through four registrations
            H = [1, 2, 3, 4, 6, 8, 10, 12, 16]
            R = [(8, 8, 8, 0, 0, 0, 0, 0, 0), (8, 8, 6, 8, 0, 4, 0, 0, 0), (6, 8, 8, 6, 4, 6, 4, 2, 4),
                 (8, 2, 6, 2, 8, 2, 8, 6, 8)]
            u = t * (len(R) - 1)
            i0 = min(int(u), len(R) - 2)
            fr = u - i0
            amp = {h: (R[i0][j] + (R[i0 + 1][j] - R[i0][j]) * fr) / 8.0 for j, h in enumerate(H)}
            sp = [(amp.get(n, 0.0), 0.0) for n in range(1, WT_H + 1)]
        elif k == 4:    # SYNC: a saw hard-synced to the cycle, its own rate 1 .. 6 times
            r = 1.0 + 5.0 * t
            sp = _dft(lambda x: 2.0 * ((x * r) % 1.0) - 1.0)
        elif k == 5:    # FM: a sine whose phase a sine at twice the rate moves, the index 0 .. 5
            ix = 5.0 * t
            sp = _dft(lambda x: math.sin(2 * math.pi * x + ix * math.sin(4 * math.pi * x)))
        elif k == 6:    # STEPS: a sine in fewer and fewer levels (16 .. 2), a coarse digital stair
            lv = 16.0 - 14.0 * t
            sp = _dft(lambda x: math.floor(math.sin(2 * math.pi * x) * lv / 2 + 0.5) / (lv / 2))
        else:           # WARP: a cosine whose phase bends at a knee (phase distortion), the knee 50 % .. 3 %
            d = 0.5 - 0.47 * t
            sp = _dft(lambda x: -math.cos(math.pi * (x / d if x < d else 1.0 + (x - d) / (1.0 - d))))
        frames.append(sp)
    return frames


def _taper(n, nh):
    """a level's top harmonics faded (cos^2 over its upper half): the table read with linear interpolation at four
    samples per highest harmonic mirrors those around the table's rate; faded, the mirrors stay far below"""
    h = nh / 2.0
    return 1.0 if n <= h else math.cos(math.pi / 2 * (n - h) / (h + 1)) ** 2


def _level(sp, size, nh):
    return [sum(_taper(n, nh) * (a * math.sin(2 * math.pi * n * i / size) + b * math.cos(2 * math.pi * n * i / size))
                for n, (a, b) in enumerate(sp[:nh], 1)) for i in range(size)]


def wavetable_banks():
    """WT_DATA: [bank][frame][level 0..5 samples], no guard samples (the engine's frame cache adds one)"""
    data = []
    for k in range(len(WT_NAMES)):
        for sp in _bank_spectra(k):
            l0 = _level(sp, *WT_LVL[0])
            rms = math.sqrt(sum(v * v for v in l0) / len(l0)) or 1.0
            pk = max(abs(v) for v in l0) or 1.0
            g = min(8000.0 / rms, 28000.0 / pk)
            for li, (size, nh) in enumerate(WT_LVL):
                data += [max(-32767, min(32767, round(g * v))) for v in (l0 if li == 0 else _level(sp, size, nh))]
    return data


def main(path):
    L = ["/* generated by tools/gen_tables.py */", "#pragma once", "#include <stdint.h>",
         f"#define FS {FS}", f"#define CTL {CTL}", f"#define GEN_AMP {AMP}",
         # phase steps per control tick for 1 BPM: one beat / 8 (the orbit, x beats8), a sixteenth (the arpeggiator)
         f"#define BEAT8_K {round(2 ** 32 * 8 * CTL / (60 * FS))}", f"#define SIXTEENTH_K {round(2 ** 32 * 4 * CTL / (60 * FS))}", ""]
    L += arr("SINE", "int16_t", [round(32767 * math.sin(2 * math.pi * i / 1024)) for i in range(1025)])
    # phase increment (2^32 = FS) of the top octave, cents 12000..13199 above MIDI 0 (8.1758 Hz)
    L += arr("INC_TOP", "uint32_t",
             [round(440 * 2 ** ((12000 + c) / 1200 - 69 / 12) / FS * 2 ** 32) for c in range(1200)], 8)
    # envelopes: attack step per control tick (Q24 full scale), decay / release factor per tick (Q16);
    # times 2 ms .. 12 s
    t = [expmap(v, 0.002, 12.0) for v in range(128)]
    L += arr("ENV_STEP", "uint32_t", [max(1, round((1 << 24) * CTL / (s * FS))) for s in t], 8)
    L += arr("ENV_FALL", "uint16_t", [min(65535, round(65536 * math.exp(-CTL * 5.0 / (s * FS)))) for s in t])
    # the suborbit's speed: phase step per control tick, 0.03 .. 24 Hz
    L += arr("SUB_RATE", "uint32_t", [round(expmap(v, 0.03, 24.0) * CTL / FS * 2 ** 32) for v in range(128)], 8)
    # TPT state-variable filter: g = tan(pi fc / FS), Q16; fc 30 Hz .. 16 kHz over 0..1023
    L += arr("SVF_G", "uint32_t",
             [round(65536 * math.tan(math.pi * expmap(i, 30.0, 16000.0, 1023) / FS)) for i in range(1024)], 8)
    # where a pitch sits on that curve: index = (cents - SVF_C0) * SVF_KC >> 16
    c0 = 1200 * math.log2(30.0 / 8.1757989)
    span = 1200 * math.log2(16000.0 / 30.0)
    L += [f"#define SVF_C0 {round(c0)}", f"#define SVF_KC {round(1023 * 65536 / span)}"]
    # formant filter: the vowels U E O I A, two formants each, as indices on the SVF curve
    vow = [(300, 870), (530, 1840), (570, 840), (270, 2290), (730, 1090)]
    idx = lambda f: round(1023 * math.log(f / 30.0) / math.log(16000.0 / 30.0))
    L += arr("VOWEL", "int16_t", [idx(f) for p in vow for f in p], 10)
    # tanh soft clip over x = -4..4 (513 points), Q15 out
    L += arr("TANH", "int16_t", [round(32767 * math.tanh((i - 256) / 64.0)) for i in range(513)])
    # a tanh knee over 0..2 (Q15 in, x 32 a step), Q15 out
    L += arr("SAT_Q15", "int16_t", [int(32767 * math.tanh(i / 2048 * 2)) for i in range(2049)])
    flat, off, shift = wavetables()
    L += [f"#define OVT_NB {len(BANDS)}", "#define OVT_B0 2"]
    L += arr("OVT_SRC", "int16_t", flat, 16)
    L += arr("OVT_OFF", "uint16_t", [o for row in off for o in row], 10)
    L += arr("OVT_SH", "uint8_t", shift, 10)
    wt = wavetable_banks()
    offs, o = [], 0
    for size, _ in WT_LVL:
        offs.append(o)
        o += size
    L += [f"#define WT_NB {len(WT_NAMES)}", f"#define WT_NF {WT_NF}", f"#define WT_FR {o}", f"#define WT_NLVL {len(WT_LVL)}"]
    L += arr("WT_LVL_OFF", "uint16_t", offs, 6)
    L += arr("WT_LVL_N", "uint16_t", [z for z, _ in WT_LVL], 6)
    L += arr("WT_LVL_SH", "uint8_t", [32 - int(math.log2(z)) for z, _ in WT_LVL], 6)
    L += ["/* the banks (W1..W8): " + ", ".join(WT_NAMES) + " */"]
    L += arr("WT_DATA", "int16_t", wt, 16)
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")
    print(f"tables: {path} ({len(flat)} overtone table samples, {len(wt)} wavetable samples: "
          f"{len(WT_NAMES)} banks x {WT_NF} frames, {len(wt) * 2 // 1024} KB in flash)")


if __name__ == "__main__":
    main(sys.argv[1])
