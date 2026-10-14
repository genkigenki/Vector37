/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (makkuro) */
/* Host checks of the Vector37 engine (the firmware's sources): the overtone patterns, the blends, aliasing,
 * clicks, the voice modes, every preset rendered to WAV (level, clip, DC), the cost per voice.
 *   tests/host OUTDIR   (tests/run_tests.sh) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "mk_tables.h"
#define RING_PUBLISH() ((void)0)
#include "../firmware/src/synth.h"
#include "../firmware/src/orbit.c"
#include "../firmware/src/engine.c"
#include "../firmware/src/analog.c"
#include "../firmware/src/fx.c"
#include "../firmware/src/mix.c"
#include "../firmware/src/params.c"
#include "../firmware/src/presets.c"

static int fails;
static const char *dir;
#define MAXF (FS * 12)
static int16_t pcm[2 * MAXF];
static double mono[MAXF];

static void check(const char *what, int ok, const char *fmt, double a, double b)
{
    char d[128];
    snprintf(d, sizeof d, fmt, a, b);
    printf("  %-4s %-34s %s\n", ok ? "ok" : "FAIL", what, d);
    fails += !ok;
}

static void wav(const char *name, const int16_t *s, uint32_t frames)
{
    char path[512];
    FILE *f;
    uint32_t v, sr = FS, br = FS * 4;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb"))) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

/* the engine from cold with sound p */
static void reset(const patch_t *p)
{
    memset(vs, 0, sizeof vs);
    memset(&down, 0, sizeof down);
    memset(&lat, 0, sizeof lat);
    memset(dl_buf, 0, sizeof dl_buf);
    memset(pl_mem, 0, sizeof pl_mem);
    memset(ch_buf, 0, sizeof ch_buf);
    memset(&pls, 0, sizeof pls);
    memset(&dls, 0, sizeof dls);
    ev_r = ev_w = 0;
    syn.oph = syn.sph = 0;
    syn.arp_note = -1, syn.arp_run = 0, syn.latched = 0;
    focus = solo = latch = 0;
    joy_x = joy_y = 0;
    vmax = NVOICE;
    pt = *p;
}

typedef void (*script_t)(uint32_t block);
static double peak, rms_, dc_;
static uint32_t nframes;
/* secs of sound into pcm / mono; script(block) before each block */
static double render(double secs, script_t script)
{
    uint32_t nb = (uint32_t)(secs * FS / CTL), b, i;
    int32_t out[2 * CTL];
    double sum = 0, sq = 0;
    struct timespec t0, t1;
    peak = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (b = 0; b < nb; b++) {
        if (script)
            script(b);
        synth_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t l = out[2 * i], r = out[2 * i + 1];
            if (l > 32767 || l < -32768 || r > 32767 || r < -32768) {
                printf("  FAIL sample out of range %d %d\n", l, r);
                fails++;
            }
            pcm[2 * (b * CTL + i)] = (int16_t)clamp(l, -32768, 32767);
            pcm[2 * (b * CTL + i) + 1] = (int16_t)clamp(r, -32768, 32767);
            mono[b * CTL + i] = (l + r) * 0.5;
            sum += l + r;
            sq += (double)l * l + (double)r * r;
            if (abs(l) > peak) peak = abs(l);
            if (abs(r) > peak) peak = abs(r);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    nframes = nb * CTL;
    rms_ = sqrt(sq / (2.0 * nframes));
    dc_ = sum / (2.0 * nframes);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / nb;   /* ns a block */
}

/* the amplitude of frequency hz in mono[from .. from + n) (Hann window) */
static double tone_amp(uint32_t from, uint32_t n, double hz)
{
    double re = 0, im = 0, w, sw = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += mono[from + i] * w * cos(2 * M_PI * hz * i / FS);
        im += mono[from + i] * w * sin(2 * M_PI * hz * i / FS);
        sw += w;
    }
    return 2 * sqrt(re * re + im * im) / sw;
}

/* in-place FFT (n a power of two) */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (m = 2; m <= n; m <<= 1)
        for (i = 0; i < n; i += m)
            for (k = 0; k < m / 2; k++) {
                double a = -2 * M_PI * k / m, c = cos(a), s = sin(a);
                double xr = re[i + k + m / 2] * c - im[i + k + m / 2] * s, xi = re[i + k + m / 2] * s + im[i + k + m / 2] * c;
                re[i + k + m / 2] = re[i + k] - xr, im[i + k + m / 2] = im[i + k] - xi;
                re[i + k] += xr, im[i + k] += xi;
            }
}

/* one corner alone (focus on it, nothing after it): a clean look at the generators */
static patch_t solo_patch(int32_t g1, int32_t g2, int32_t blend, int32_t mode)
{
    patch_t p;
    uint32_t c;
    patch_init(&p);
    for (c = 0; c < NC; c++) {
        p.c[c][CP_G1] = (int16_t)g1, p.c[c][CP_G2] = (int16_t)g2, p.c[c][CP_BLEND] = (int16_t)blend;
        p.c[c][CP_BMODE] = (int16_t)mode, p.c[c][CP_FTYPE] = FT_OFF;
    }
    p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0;
    p.g[G_AA] = 0, p.g[G_AS] = 127, p.g[G_VOLUME] = 90;
    return p;
}
static uint32_t on_note;
static void hold_note(uint32_t b) { if (b == 0) ev_push(EV_ON, on_note, 100); }

static void test_patterns(void)
{
    static const char *NAME = "ABCDEF";
    uint32_t pat, n;
    printf(" overtone patterns (one corner, C3 = 130.8 Hz, filter off)\n");
    for (pat = 0; pat < 6; pat++) {
        patch_t p = solo_patch((int32_t)pat, 0, 0, 0);
        double f0 = 440 * pow(2, (48 - 69) / 12.0), a1, ok = 1;
        char what[64], got[160] = "";
        reset(&p);
        focus = 1;
        on_note = 48;
        render(1.0, hold_note);
        a1 = tone_amp(FS / 2, 16384, f0);
        for (n = 2; n <= 8; n++) {                     /* each overtone: 1/n where the pattern has it, else ~0 */
            int s = (pat == 0 && n % 8 == 2) || (pat == 1 && n % 4 == 2) || (pat == 2 && n % 2 == 0) ||
                    (pat == 3 && n % 2 == 1) || (pat == 4 && n % 4 == 3) || (pat == 5 && n % 8 == 3);
            double r = tone_amp(FS / 2, 16384, f0 * n) / a1, want = s ? 1.0 / n : 0;
            char b[16];
            snprintf(b, sizeof b, " %.2f", r);
            strcat(got, b);
            if (fabs(r - want) > 0.03)
                ok = 0;
        }
        snprintf(what, sizeof what, "pattern %c: overtones 2..8 / 1", NAME[pat]);
        printf("  %-4s %-34s%s\n", ok ? "ok" : "FAIL", what, got);
        fails += !ok;
    }
    {   /* C, D added: a sawtooth (every overtone at 1/n) */
        patch_t p = solo_patch(PAT_C, PAT_D, -64, 0);
        double f0 = 440 * pow(2, (48 - 69) / 12.0), a1, worst = 0;
        reset(&p);
        focus = 1;
        on_note = 48;
        render(1.0, hold_note);
        wav("pattern_c_plus_d", pcm, nframes);
        a1 = tone_amp(FS / 2, 16384, f0);
        for (n = 2; n <= 12; n++) {
            double e = fabs(tone_amp(FS / 2, 16384, f0 * n) / a1 - 1.0 / n);
            if (e > worst) worst = e;
        }
        check("C + D (blend left) = sawtooth", worst < 0.02, "worst |a_n - 1/n| %.4f%.0s", worst, 0);
    }
    {   /* C, B added: B's minus cancels C's 2, 6, 10 */
        patch_t p = solo_patch(PAT_C, PAT_B, -64, 0);
        double f0 = 440 * pow(2, (48 - 69) / 12.0), a1, r2, r4;
        reset(&p);
        focus = 1;
        on_note = 48;
        render(1.0, hold_note);
        a1 = tone_amp(FS / 2, 16384, f0);
        r2 = tone_amp(FS / 2, 16384, f0 * 2) / a1, r4 = tone_amp(FS / 2, 16384, f0 * 4) / a1;
        check("C + B: overtone 2 cancelled, 4 kept", r2 < 0.01 && fabs(r4 - 0.25) < 0.02, "a2 %.3f  a4 %.3f", r2, r4);
    }
}

static void test_noise(void)
{
    patch_t p = solo_patch(PAT_N, 0, 0, 0);
    reset(&p);
    focus = 1;
    on_note = 60;
    render(1.0, hold_note);
    wav("pattern_n", pcm, nframes);
    check("pattern N: noise sounds", rms_ > 300 && peak < 32000, "rms %.0f peak %.0f", rms_, peak);
}

static void test_blends(void)
{
    static const char *const NM[BM_N] = {"AM", "PM", "PM+AM", "PD+AM", "rPD+AM"};
    uint32_t m;
    printf(" blend modes (C x F, blend right 40, C3)\n");
    for (m = 0; m < BM_N; m++) {
        patch_t p = solo_patch(PAT_C, PAT_F, 40, (int32_t)m), p0 = solo_patch(PAT_C, PAT_F, 0, 0);
        double f0 = 440 * pow(2, (48 - 69) / 12.0), diff = 0, r0;
        char name[32];
        uint32_t n;
        reset(&p0);
        focus = 1, on_note = 48;
        render(0.5, hold_note);
        r0 = rms_;
        {   /* the spectrum differs from G1 alone */
            double a[9];
            for (n = 1; n <= 8; n++) a[n] = tone_amp(FS / 4, 8192, f0 * n);
            reset(&p);
            focus = 1, on_note = 48;
            render(0.5, hold_note);
            for (n = 1; n <= 8; n++) diff += fabs(tone_amp(FS / 4, 8192, f0 * n) - a[n]);
        }
        snprintf(name, sizeof name, "blend_%s", NM[m]);
        wav(name, pcm, nframes);
        snprintf(name, sizeof name, "blend %s: changes the tone", NM[m]);
        check(name, diff > 300 && rms_ > r0 * 0.25 && peak < 32700, "spectral change %.0f, rms %.0f", diff, rms_);
    }
}

/* aliasing: a high note's spectrum, the energy away from its harmonics */
static void test_alias(void)
{
    static double re[16384], im[16384];
    uint32_t note, i;
    for (note = 84; note <= 96; note += 12) {
        patch_t p = solo_patch(PAT_C, PAT_D, -64, 0);
        double f0 = 440 * pow(2, (note - 69) / 12.0), on = 0, off = 0, hi = 0;
        char what[48];
        reset(&p);
        focus = 1, on_note = note;
        render(1.0, hold_note);
        for (i = 0; i < 16384; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / 16384);
            re[i] = mono[FS / 2 + i] * w, im[i] = 0;
        }
        fft(re, im, 16384);
        for (i = 4; i < 8192; i++) {
            double hz = i * (double)FS / 16384, e = re[i] * re[i] + im[i] * im[i], h = hz / f0;
            if (fabs(h - floor(h + 0.5)) * f0 < 25)
                on += e;
            else
                off += e;
            if (hz > 16000)
                hi += e;
        }
        snprintf(what, sizeof what, "aliasing, saw at MIDI %u", note);
        check(what, off / on < 1e-3, "off-harmonic energy %.1f dB, above 16k %.1f dB",
              10 * log10(off / on + 1e-30), 10 * log10(hi / (on + off) + 1e-30));
    }
}

/* clicks: the orbit at its fastest, the corners far apart: the second difference stays near a still orbit's */
static void test_clicks(void)
{
    patch_t p;
    double base, fast;
    uint32_t i;
    preset_make(0, &p);
    p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0;
    p.g[G_SUBSZ] = 0;
    reset(&p);
    focus = 5, on_note = 48;
    render(1.0, hold_note);
    for (base = 0, i = FS / 2; i < nframes; i++) base = fmax(base, fabs(mono[i] - 2 * mono[i - 1] + mono[i - 2]));
    p.g[G_RATE] = 8, p.g[G_TEMPO] = 240, p.g[G_SUBSZ] = 127, p.g[G_SUBSPD] = 127;
    reset(&p);
    on_note = 48;
    render(1.0, hold_note);
    wav("clicks_fast_orbit", pcm, nframes);
    for (fast = 0, i = FS / 2; i < nframes; i++) fast = fmax(fast, fabs(mono[i] - 2 * mono[i - 1] + mono[i - 2]));
    check("no clicks: fast orbit vs still", fast < base * 2.5, "max 2nd difference %.0f vs %.0f", fast, base);
}

/* the voice modes: how many voices sound */
static uint32_t sounding(void)
{
    uint32_t v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += vs[v].stage != 0;
    return n;
}
static void chord10(uint32_t b) { if (b < 10) ev_push(EV_ON, 48 + b * 2, 100); }
static void test_modes(void)
{
    patch_t p;
    uint32_t n;
    patch_init(&p);
    reset(&p);
    render(0.3, chord10);
    check("POLY: ten keys, eight voices", sounding() == 8, "%.0f sounding%.0s", sounding(), 0);
    p.g[G_POLY] = PM_DUAL;
    reset(&p);
    render(0.3, chord10);
    n = sounding();
    check("DUAL: pairs (main + follower)", n == 8 && vs[1].follower && !vs[0].follower, "%.0f sounding%.0s", n, 0);
    p.g[G_POLY] = PM_MONO, p.g[G_STACK] = 5;
    reset(&p);
    render(0.3, chord10);
    n = sounding();
    check("MONO: a stack of five, the last key", n == 5 && vs[0].note == 66 && vs[4].note == 66, "%.0f sounding, note %.0f", n,
          vs[0].note);
    p.g[G_POLY] = PM_POLY, p.g[G_ARP] = ARP_UP;
    reset(&p);
    ev_push(EV_ON, 48, 100), ev_push(EV_ON, 52, 100), ev_push(EV_ON, 55, 100);
    {
        uint32_t seen = 0, b, last = 0;
        int32_t out[2 * CTL];
        for (b = 0; b < FS * 2 / CTL; b++) {
            synth_block(out, CTL);
            if (syn.arp_note >= 0 && (uint32_t)syn.arp_note != last)
                last = (uint32_t)syn.arp_note, seen |= 1u << (last - 48);
        }
        check("ARP UP: three keys, two octaves", seen == (1u | 1u << 4 | 1u << 7 | 1u << 12 | 1u << 16 | 1u << 19),
              "notes seen %.0f%.0s", (double)seen, 0);
    }
    p.g[G_ARP] = ARP_OFF;
    reset(&p);
    latch = 1;
    ev_push(EV_ON, 60, 100), ev_push(EV_ON, 64, 100), ev_push(EV_OFF, 60, 0), ev_push(EV_OFF, 64, 0);
    render(0.2, 0);
    n = sounding();
    ev_push(EV_ON, 67, 100);
    render(1.5, 0);
    check("LATCH: notes stay; a new chord replaces them", n == 2 && sounding() == 1, "%.0f then %.0f", n, sounding());
}

static void preset_script(uint32_t b)              /* a chord, held 3 s, then released; a second one */
{
    static const uint8_t CH[2][3] = {{48, 55, 63}, {53, 60, 69}};
    uint32_t k, t = b * CTL;
    for (k = 0; k < 3; k++) {
        if (t < (uint32_t)FS * 3 / 1 && t + CTL > (uint32_t)FS * 3 / 1) ev_push(EV_OFF, CH[0][k], 0);
        if (b == 0) ev_push(EV_ON, CH[0][k], 90 + k * 10);
        if (t < (uint32_t)FS * 4 && t + CTL >= (uint32_t)FS * 4) ev_push(EV_ON, CH[1][k], 100);
        if (t < (uint32_t)FS * 6 && t + CTL >= (uint32_t)FS * 6) ev_push(EV_OFF, CH[1][k], 0);
    }
}
static void test_presets(void)
{
    uint32_t n;
    printf(" presets (a chord 3 s, released, another; WAV)\n");
    for (n = 0; n < NPRESET; n++) {
        patch_t p;
        char name[48], what[48];
        double ns;
        preset_make(n, &p);
        reset(&p);
        ns = render(9.0, preset_script);
        snprintf(name, sizeof name, "preset_%u_%s", n + 1, PRESET_NAME[n]);
        for (char *c = name; *c; c++) if (*c == ' ') *c = '_';
        wav(name, pcm, nframes);
        snprintf(what, sizeof what, "%u %s", n + 1, PRESET_NAME[n]);
        check(what, peak > 3000 && peak <= 32767 && fabs(dc_) < 300,
              "peak %5.1f dBFS, rms %.0f", 20 * log10(peak / 32768), rms_);
        (void)ns;
    }
}

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
#define NBAND 7                                    /* octave bands 125 Hz .. 8 kHz */
#define MAXW 600
static double cen_w[MAXW], band_w[NBAND][MAXW];
/* the spectrum every 1024 samples over mono[from .. to): the centroid and the octave bands (dB of the total) */
static uint32_t spectrum_stats(uint32_t from, uint32_t to)
{
    static double re[2048], im[2048];
    uint32_t s, i, k = 0, b;
    for (s = from; s + 2048 <= to && k < MAXW; s += 1024) {
        double e = 0, ec = 0, eb[NBAND] = {0};
        for (i = 0; i < 2048; i++)
            re[i] = mono[s + i] * (0.5 - 0.5 * cos(2 * M_PI * i / 2048)), im[i] = 0;
        fft(re, im, 2048);
        for (i = 1; i < 1024; i++) {
            double hz = i * (double)FS / 2048, p = re[i] * re[i] + im[i] * im[i];
            int ob = (int)floor(log2(hz / 88.4));      /* 88..177 Hz: the 125 Hz band */
            e += p, ec += p * hz;
            if (ob >= 0 && ob < NBAND)
                eb[ob] += p;
        }
        if (e < 1e3)                               /* (silence: not counted) */
            continue;
        cen_w[k] = ec / e;
        for (b = 0; b < NBAND; b++)
            band_w[b][k] = 10 * log10(eb[b] / e + 1e-7);
        k++;
    }
    return k;
}
static double pct(double *v, uint32_t n, double p)
{
    static double t[MAXW];
    memcpy(t, v, n * sizeof *v);
    qsort(t, n, sizeof *t, cmpd);
    return t[(uint32_t)(p * (n - 1))];
}
/* the paths: each sound's (and any SIZE, any rotation) whole inside the square: never squashed on an edge; how far
 * they reach towards the planets */
static void test_paths(void)
{
    uint32_t k, sz, rot, i;
    printf(" the paths (SIZE 127 fitted): the farthest reach (1.0 = the square's edge)%c", 10);
    for (k = 0; k < NPRESET; k++) {
        patch_t p;
        int32_t worst = 0, reach = 0;
        char what[48];
        preset_make(k, &p);
        for (sz = 40; sz <= 127; sz += 29)
            for (rot = 0; rot < 128; rot += 16) {
                p.g[G_SIZE] = (int16_t)sz, p.g[G_ROT] = (int16_t)rot;
                orbit_follow(p.g, 1);
                for (i = 0; i < 512; i++) {
                    int32_t x, y;
                    orbit_path(i * (0xFFFFFFFFu / 512u), i * (0xFFFFFFFFu / 512u), &x, &y);
                    orbit_place(&x, &y);
                    worst = abs(x) > worst ? abs(x) : worst, worst = abs(y) > worst ? abs(y) : worst;
                }
            }
        preset_make(k, &p);
        orbit_follow(p.g, 1);
        for (i = 0; i < 512; i++) {
            int32_t x, y, d;
            orbit_path(i * (0xFFFFFFFFu / 512u), i * (0xFFFFFFFFu / 512u), &x, &y);
            orbit_place(&x, &y);
            d = abs(x) > abs(y) ? abs(x) : abs(y);
            reach = d > reach ? d : reach;
        }
        snprintf(what, sizeof what, "%u %s", k + 1, PRESET_NAME[k]);
        check(what, worst < 32700 && reach > 29000, "reach %.2f, never past %.3f", reach / 32768.0, worst / 32768.0);
    }
    {   /* a knob turned: the path glides, no step larger than a little at a time */
        patch_t p;
        int32_t px = 0, py = 0, jump = 0, t;
        preset_make(0, &p);
        orbit_follow(p.g, 1);
        for (t = 0; t < 2000; t++) {
            int32_t x, y;
            if (t % 40 == 0 && p.g[G_MORPH] < 63)
                p.g[G_MORPH]++;            /* (a knob turning slowly: MORPH 1:1 .. 1:5) */
            orbit_follow(p.g, 0);
            orbit_path(0x30000000u, 0x30000000u, &x, &y);
            orbit_place(&x, &y);
            if (t)
                jump = abs(x - px) > jump ? abs(x - px) : jump, jump = abs(y - py) > jump ? abs(y - py) : jump;
            px = x, py = y;
        }
        check("a knob turning: the path morphs, never jumps", jump < 400, "largest step a tick %.4f%.0s", jump / 32768.0, 0);
    }
    {   /* fader 1 moving slowly through every family: the sun never jumps (each point of the path, a tick) */
        patch_t p;
        int32_t jump = 0, t, k;
        int32_t px[8], py[8];
        preset_make(0, &p);
        p.g[G_SHAPE] = 0;
        orbit_follow(p.g, 1);
        for (t = 0; t < 124 * 30; t++) {
            if (t % 30 == 0 && p.g[G_SHAPE] < 124)
                p.g[G_SHAPE]++;            /* (FAMILY, a step every 30 ticks: about 3 s through all five) */
            orbit_follow(p.g, 0);
            for (k = 0; k < 8; k++) {
                int32_t x, y;
                orbit_path((uint32_t)k << 29, (uint32_t)k << 29, &x, &y);
                orbit_place(&x, &y);
                if (t)
                    jump = abs(x - px[k]) > jump ? abs(x - px[k]) : jump, jump = abs(y - py[k]) > jump ? abs(y - py[k]) : jump;
                px[k] = x, py[k] = y;
            }
        }
        check("FAMILY moving slowly: the path morphs, never jumps", jump < 600, "largest step a tick %.4f%.0s", jump / 32768.0, 0);
    }
}

/* WARP Y at its left end (as the Lissajous sounds set it): every family still its figure, never a plain circle (the
 * radius varies by more than 10 %), but Lissajous at 1:1 (MORPH -64) */
static void test_not_circle(void)
{
    static const int16_t MO[3] = {-64, 0, 63};
    uint32_t f, m, i, bad = 0;
    double worst = 1;
    for (f = 0; f < SH_N; f++)
        for (m = 0; m < 3; m++) {
            patch_t p;
            double lo = 1e9, hi = 0;
            patch_init(&p);
            p.g[G_SHAPE] = (int16_t)(f * FAM_STEP), p.g[G_WARPY] = -64, p.g[G_WARPX] = 0, p.g[G_MORPH] = MO[m];
            orbit_follow(p.g, 1);
            for (i = 0; i < 512; i++) {
                int32_t x, y;
                double r;
                orbit_path(i * (0xFFFFFFFFu / 512u), i * (0xFFFFFFFFu / 512u), &x, &y);
                r = sqrt((double)x * x + (double)y * y);
                lo = fmin(lo, r), hi = fmax(hi, r);
            }
            if (f == 0 && m == 0)
                continue;                              /* (Lissajous 1:1: the circle) */
            worst = fmin(worst, (hi - lo) / hi);
            bad += (hi - lo) / hi <= 0.10;
        }
    check("WARP Y -64: no family a circle (but 1:1)", bad == 0, "least radius variation %.2f, %.0f circles", worst, bad);
}

static void retrig_note(uint32_t b)               /* (decaying sounds: the note again every 0.75 s) */
{
    if (b % (FS * 3 / 4 / CTL) == 0) {
        ev_push(EV_OFF, on_note, 0);
        ev_push(EV_ON, on_note, 100);
    }
}
/* the orbit heard: one note held (effects off), the spectrum every 23 ms: how far the centroid and the octave bands
 * move with the orbit; then each corner alone (focus): its centroid, its bands, its level. Pass: the spectrum moves
 * by 12 dB or more in some band, the corners' spectra differ by 6 dB a band on average (the most different pair),
 * and no corner is more than 12 dB quieter than the loudest. */
static void test_motion(void)
{
    uint32_t n, k, c, b, d;
    printf(" the orbit heard (C3 held 6 s, dry): centroid p10..p90, the largest octave-band swing p10..p90;\n"
           " each corner alone (focus): centroid, the most different pair's mean band difference, the level spread\n");
    for (k = 0; k < NPRESET; k++) {
        patch_t p;
        double c10, c90, swing = 0, cc[NC], cb[NC][NBAND], rl[NC], mn = 1e9, mx = 0, rmax = -1e9, rmin = 1e9, dist = 0;
        char what[64];
        int ok;
        preset_make(k, &p);
        p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0, p.g[G_ARP] = ARP_OFF;
        reset(&p);
        on_note = 48;
        render(6.0, p.g[G_AS] < 40 ? retrig_note : hold_note);
        snprintf(what, sizeof what, "motion_%u", k + 1);
        wav(what, pcm, nframes);
        n = spectrum_stats(FS / 2, nframes);
        c10 = pct(cen_w, n, 0.1), c90 = pct(cen_w, n, 0.9);
        for (b = 0; b < NBAND; b++)
            swing = fmax(swing, pct(band_w[b], n, 0.9) - pct(band_w[b], n, 0.1));
        for (c = 0; c < NC; c++) {
            reset(&p);
            focus = (uint8_t)(c + 1u);
            render(0.8, hold_note);
            rl[c] = 20 * log10(rms_ + 1e-9);
            n = spectrum_stats(FS / 10, nframes);   /* (the first 0.7 s: the bells still ring) */
            cc[c] = pct(cen_w, n, 0.5);
            for (b = 0; b < NBAND; b++)
                cb[c][b] = pct(band_w[b], n, 0.5);
            mn = fmin(mn, cc[c]), mx = fmax(mx, cc[c]);
            rmax = fmax(rmax, rl[c]), rmin = fmin(rmin, rl[c]);
        }
        for (c = 0; c < NC; c++)
            for (d = c + 1; d < NC; d++) {
                double s = 0;
                for (b = 0; b < NBAND; b++)
                    s += fabs(cb[c][b] - cb[d][b]);
                dist = fmax(dist, s / NBAND);
            }
        ok = swing >= 12 && dist >= 6 && rmax - rmin <= 12;
        snprintf(what, sizeof what, "%u %s", k + 1, PRESET_NAME[k]);
        printf("  %-4s %-14s centroid %5.0f..%5.0f Hz  band swing %4.1f dB | corners %5.0f %5.0f %5.0f %5.0f Hz (%.1f oct), "
               "apart %4.1f dB/band, levels within %4.1f dB (%.0f %.0f %.0f %.0f)\n", ok ? "ok" : "FAIL", what, c10, c90,
               swing, cc[0], cc[1], cc[2], cc[3], log2(mx / mn), dist, rmax - rmin, rl[0] - rmax, rl[1] - rmax, rl[2] - rmax,
               rl[3] - rmax);
        fails += !ok;
    }
}

/* the cost: eight voices, every corner busy (focus centre, LP24 / VOX, PM+AM / PD+AM), effects on */
static void test_cost(void)
{
    patch_t p;
    double ns1, ns8, nsfx;
    uint32_t c;
    preset_make(0, &p);
    for (c = 0; c < NC; c++) {
        p.c[c][CP_FTYPE] = c & 1 ? FT_VOX : FT_LP24;
        p.c[c][CP_BMODE] = c & 1 ? BM_PMAM : BM_PDAM;
        p.c[c][CP_BLEND] = 40;
    }
    p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0, p.g[G_DRIVE] = 0;
    reset(&p);
    focus = 5;
    render(0.2, 0);
    nsfx = render(2.0, 0);                         /* (nothing playing: the bare tick) */
    on_note = 48;
    reset(&p);
    focus = 5;
    ns1 = render(2.0, hold_note);
    reset(&p);
    focus = 5;
    render(2.0, chord10);
    ns8 = render(2.0, 0);
    p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 60, p.g[G_DRIVE] = 40;
    pt = p;
    nsfx = render(2.0, 0) - ns8;
    printf("  cost (host): 1 voice %.0f ns/block, 8 voices %.0f ns/block (%.0f per voice), effects %.0f ns/block\n",
           ns1, ns8, (ns8 - ns1) / 7, nsfx);
    printf("  cost (host): 8 voices + effects = %.1f %% of real time\n", (ns8 + nsfx) / (CTL * 1e9 / FS) * 100);
}

int main(int argc, char **argv)
{
    dir = argc > 1 ? argv[1] : ".";
    synth_init();
    {
        patch_t p;
        patch_init(&p);
        reset(&p);
        render(0.5, 0);
        check("silent with no keys", peak == 0, "peak %.0f%.0s", peak, 0);
    }
    test_patterns();
    test_noise();
    test_blends();
    test_alias();
    test_clicks();
    test_modes();
    test_presets();
    test_paths();
    test_not_circle();
    test_motion();
    test_cost();
    printf(fails ? "host: %d FAILED\n" : "host: all ok\n", fails);
    return fails != 0;
}
