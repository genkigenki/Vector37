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
    for (n = 0; n < NFACT; n++) {
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

static int cmpd(const void *a, const void *b);
/* the loudness: K-weighted (ITU-R BS.1770: the shelf and the high-pass), the loudest 0.4 s window (every 0.1 s),
 * LUFS; of pcm[0 .. nframes) */
static double loud_max(void)
{
    static double kl[MAXF], kr[MAXF];
    double best = -120, w0, A = pow(10, 4.0 / 40), al, cw, b[2][3], a[2][3];
    uint32_t i, s, ch, st, win = FS * 4 / 10;
    w0 = 2 * M_PI * 1500 / FS, cw = cos(w0), al = sin(w0) / (2 * sqrt(0.5));      /* the shelf: +4 dB, 1.5 kHz */
    b[0][0] = A * ((A + 1) + (A - 1) * cw + 2 * sqrt(A) * al), b[0][1] = -2 * A * ((A - 1) + (A + 1) * cw);
    b[0][2] = A * ((A + 1) + (A - 1) * cw - 2 * sqrt(A) * al), a[0][0] = (A + 1) - (A - 1) * cw + 2 * sqrt(A) * al;
    a[0][1] = 2 * ((A - 1) - (A + 1) * cw), a[0][2] = (A + 1) - (A - 1) * cw - 2 * sqrt(A) * al;
    w0 = 2 * M_PI * 38 / FS, cw = cos(w0), al = sin(w0) / (2 * 0.5);              /* the high-pass: 38 Hz */
    b[1][0] = (1 + cw) / 2, b[1][1] = -(1 + cw), b[1][2] = (1 + cw) / 2;
    a[1][0] = 1 + al, a[1][1] = -2 * cw, a[1][2] = 1 - al;
    for (ch = 0; ch < 2; ch++) {
        double *o = ch ? kr : kl;
        for (i = 0; i < nframes; i++)
            o[i] = pcm[2 * i + ch] / 32768.0;
        for (st = 0; st < 2; st++) {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (i = 0; i < nframes; i++) {
                double x = o[i], y = (b[st][0] * x + b[st][1] * x1 + b[st][2] * x2 - a[st][1] * y1 - a[st][2] * y2) / a[st][0];
                x2 = x1, x1 = x, y2 = y1, y1 = y, o[i] = y;
            }
        }
    }
    for (s = 0; s + win <= nframes; s += FS / 10) {
        double e = 0;
        for (i = s; i < s + win; i++)
            e += kl[i] * kl[i] + kr[i] * kr[i];
        e /= win;
        if (e > 0)
            best = fmax(best, -0.691 + 10 * log10(e));
    }
    return best;
}

/* the demos: each factory sound played as it is meant (a chord, a bass line, a melody, held keys for an arpeggio),
 * to OUTDIR/demo_NN_NAME.wav; the loudest 0.4 s of each (K-weighted): FACTORY 2 within 3 dB of the factory sounds'
 * median, none silent, none clipped (the master's limiter: no more than a few samples) */
enum { DM_CHORDS, DM_BASS, DM_LEAD, DM_HOLD, DM_KEYS };
static const uint8_t DEMO[NFACT] = {DM_CHORDS, DM_BASS, DM_KEYS, DM_LEAD, DM_CHORDS, DM_HOLD, DM_LEAD, DM_CHORDS,
                                    DM_CHORDS, DM_KEYS, DM_CHORDS, DM_BASS, DM_KEYS, DM_LEAD, DM_CHORDS, DM_HOLD,
                                    DM_CHORDS, DM_CHORDS, DM_CHORDS, DM_CHORDS, DM_KEYS, DM_KEYS, DM_KEYS, DM_KEYS,
                                    DM_BASS, DM_BASS, DM_HOLD, DM_HOLD, DM_CHORDS, DM_LEAD, DM_CHORDS, DM_HOLD};
static uint32_t demo_kind;
static void demo_script(uint32_t b)
{
    static const uint8_t CH[4][3] = {{48, 55, 64}, {45, 52, 60}, {41, 48, 57}, {43, 50, 59}};   /* C Am F G */
    static const uint8_t BL[16] = {36, 36, 48, 36, 43, 36, 46, 48, 33, 33, 45, 33, 41, 43, 45, 47};
    static const uint8_t ML[16] = {72, 74, 76, 79, 76, 74, 72, 69, 67, 69, 72, 74, 72, 0, 67, 72};
    static const uint8_t KY[8] = {60, 64, 67, 72, 71, 67, 64, 62};
    uint32_t t = b * CTL, k, step, e;
    switch (demo_kind) {
    case DM_CHORDS:                                /* four chords, two seconds each */
        step = FS * 2;
        for (k = 0; k < 4; k++) {
            uint32_t on = k * step, off = on + step - FS / 10;
            if (t <= on && on < t + CTL)
                for (e = 0; e < 3; e++) ev_push(EV_ON, CH[k][e], 96);
            if (t <= off && off < t + CTL)
                for (e = 0; e < 3; e++) ev_push(EV_OFF, CH[k][e], 0);
        }
        break;
    case DM_BASS:                                  /* eighths at 120 BPM */
    case DM_LEAD:                                  /* a melody, quarters at 120 */
        step = demo_kind == DM_BASS ? FS / 4 : FS / 2;
        for (k = 0; k < 16; k++) {
            uint32_t on = k * step, off = on + step * 4 / 5, n = demo_kind == DM_BASS ? BL[k] : ML[k];
            if (!n)
                continue;
            if (t <= on && on < t + CTL) ev_push(EV_ON, n, 100);
            if (t <= off && off < t + CTL) ev_push(EV_OFF, n, 0);
        }
        break;
    case DM_KEYS:                                  /* a broken chord, then the chord */
        for (k = 0; k < 8; k++) {
            uint32_t on = k * (FS / 3), off = on + FS / 3;
            if (t <= on && on < t + CTL) ev_push(EV_ON, KY[k], 100);
            if (t <= off && off < t + CTL) ev_push(EV_OFF, KY[k], 0);
        }
        if (t <= FS * 3 && FS * 3 < t + CTL)
            for (e = 0; e < 3; e++) ev_push(EV_ON, CH[0][e], 100);
        if (t <= FS * 6 && FS * 6 < t + CTL)
            for (e = 0; e < 3; e++) ev_push(EV_OFF, CH[0][e], 0);
        break;
    default:                                       /* held keys (its arpeggio runs): a chord, then another */
        for (k = 0; k < 2; k++) {
            uint32_t on = k * FS * 4, off = on + FS * 4 - FS / 10;
            if (t <= on && on < t + CTL)
                for (e = 0; e < 3; e++) ev_push(EV_ON, CH[k * 2][e], 100);
            if (t <= off && off < t + CTL)
                for (e = 0; e < 3; e++) ev_push(EV_OFF, CH[k * 2][e], 0);
        }
    }
}
static void test_demos(void)
{
    uint32_t n, overs;
    double lu[NFACT], med, t[NFACT];
    printf(" the demos (each sound played as meant, WAV): the loudest 0.4 s, K-weighted (LUFS)\n");
    for (n = 0; n < NFACT; n++) {
        patch_t p;
        char name[48];
        preset_make(n, &p);
        reset(&p);
        demo_kind = DEMO[n];
        syn.overs = 0;
        render(9.0, demo_script);
        overs = syn.overs;
        lu[n] = loud_max();
        snprintf(name, sizeof name, "demo_%02u_%s", n + 1, PRESET_NAME[n]);
        for (char *c = name; *c; c++) if (*c == ' ' || *c == '.') *c = '_';
        wav(name, pcm, nframes);
        t[n] = lu[n];
        printf("  info %2u %-14s %6.1f LUFS  peak %5.1f dBFS  limited %4.1f %% of the time\n", n + 1, PRESET_NAME[n], lu[n],
               20 * log10(peak / 32768 + 1e-9), 100.0 * overs / nframes);
        if (peak > 31000 || lu[n] < -40 || (n >= NPRESET && overs > nframes / 4u)) {   /* (FACTORY 2: the master's limiter
                                                      in gain at most a quarter of the time) */
            printf("  FAIL %u %s: %s\n", n + 1, PRESET_NAME[n], lu[n] < -40 ? "silent" : "too loud (near clipping, or limited)");
            fails++;
        }
    }
    qsort(t, NPRESET, sizeof *t, cmpd);           /* (the reference: FACTORY 1's median) */
    med = (t[NPRESET / 2 - 1] + t[NPRESET / 2]) / 2;
    {
        double lo = 1e9, hi = -1e9;
        for (n = NPRESET; n < NFACT; n++)
            lo = fmin(lo, lu[n] - med), hi = fmax(hi, lu[n] - med);
        check("FACTORY 2: within 3 dB of FACTORY 1's median", lo >= -3 && hi <= 3, "%+.1f .. %+.1f dB", lo, hi);
        for (lo = 1e9, hi = -1e9, n = 0; n < NPRESET; n++)
            lo = fmin(lo, lu[n] - med), hi = fmax(hi, lu[n] - med);
        printf("  info FACTORY 1: %+.1f .. %+.1f dB of the median (%.1f LUFS)\n", lo, hi, med);
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
    double emax = 0;
    for (s = 0; s + 2048 <= to; s += 1024) {       /* (the loudest window of the whole: what is 40 dB below it, a
                                                      decay's tail in the noise floor, is not counted) */
        double e = 0;
        for (i = 0; i < 2048; i++)
            e += mono[s + i] * mono[s + i];
        emax = fmax(emax, e);
    }
    for (s = from; s + 2048 <= to && k < MAXW; s += 1024) {
        double e = 0, ec = 0, eb[NBAND] = {0}, et = 0;
        for (i = 0; i < 2048; i++)
            et += mono[s + i] * mono[s + i];
        if (et < emax * 1e-4)
            continue;
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
    if (!n)
        return 0;                                  /* (nothing heard) */
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
    for (k = 0; k < NFACT; k++) {
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
    for (k = 0; k < NFACT; k++) {
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
            if (n < 4u)
                n = spectrum_stats(0, nframes);    /* (a drum: gone by then; from its start) */
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

/* stereo: WIDTH 0 (the voices' spread 0, effects off): L == R; WIDTH full: the balance follows the orbit's side
 * (the planets 1, 3 left; 2, 4 right; the moon's x), the gains constant power (L + R never cancels); a demo WAV */
static int32_t opan_w[MAXF / 1024];
static void opan_note(uint32_t b)
{
    hold_note(b);
    if ((b * CTL) % 1024u == 0u && b * CTL / 1024u < MAXF / 1024)
        opan_w[b * CTL / 1024u] = vs[0].opan;
}
static void test_stereo(void)
{
    patch_t p;
    uint32_t i, k, nw, diff = 0;
    double sxy = 0, sx = 0, sy = 0, sxx = 0, syy = 0, lo = 1e9, hi = -1e9, corr, worst = 1e9;
    preset_make(0, &p);
    p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0, p.g[G_DRIVE] = 0, p.g[G_STEREO] = 0;
    p.g[G_WIDTH] = 0;
    reset(&p);
    memset(mst.dc, 0, sizeof mst.dc), memset(mst.dce, 0, sizeof mst.dce);   /* (the DC blocker: as from cold) */
    on_note = 48;
    render(4.0, hold_note);
    for (i = 0; i < nframes; i++)
        diff += pcm[2 * i] != pcm[2 * i + 1];
    check("WIDTH 0: L == R (mono, as before)", diff == 0, "%.0f samples differ%.0s", diff, 0);
    p.g[G_WIDTH] = 127;
    reset(&p);
    render(8.0, opan_note);
    nw = nframes / 1024 - 1;
    for (k = 2; k < nw; k++) {                         /* each 1024-sample window: balance against the orbit's side */
        double el = 0, er = 0, em = 0, bal, x;
        for (i = k * 1024; i < k * 1024 + 1024; i++) {
            double l = pcm[2 * i], r = pcm[2 * i + 1];
            el += l * l, er += r * r, em += (l + r) * (l + r);
        }
        bal = (sqrt(er) - sqrt(el)) / (sqrt(er) + sqrt(el) + 1e-9);
        x = (opan_w[k] + opan_w[k + 1]) / 2.0;
        sxy += x * bal, sx += x, sy += bal, sxx += x * x, syy += bal * bal;
        lo = fmin(lo, bal), hi = fmax(hi, bal);
        worst = fmin(worst, sqrt(em) / (sqrt(el) + sqrt(er) + 1e-9));
    }
    nw -= 2;
    corr = (sxy - sx * sy / nw) / sqrt((sxx - sx * sx / nw) * (syy - sy * sy / nw) + 1e-30);
    wav("stereo_width_full", pcm, nframes);
    check("WIDTH full: the balance follows the orbit", corr > 0.8 && hi - lo > 0.6, "correlation %.2f, balance swing %.2f",
          corr, hi - lo);
    check("... mono sum: L + R never cancels", worst > 0.95, "lowest |L+R| / (|L|+|R|) %.3f%.0s", worst, 0);
    preset_make(0, &p);                                /* the demo: NEBULA PAD as it is, then WIDTH full */
    p.g[G_WIDTH] = 127;
    reset(&p);
    render(9.0, preset_script);
    wav("stereo_demo_nebula_width_full", pcm, nframes);
}

/* the wavetables (W1..W8): every bank sounds; band-limited at high notes at its first, middle and last frame (as the
 * overtone patterns); the position morphs the timbre without clicks (a knob sweeping it; the orbit moving it); the
 * frames come from the flash only when the position crosses into another frame */
static patch_t wt_patch(uint32_t bank, int32_t pos, int32_t orbit)
{
    patch_t p = solo_patch((int32_t)(PAT_W1 + bank), 0, 0, 0);
    uint32_t c;
    for (c = 0; c < NC; c++)
        p.c[c][CP_WP1] = (int16_t)pos, p.c[c][CP_WO1] = (int16_t)orbit;
    return p;
}
static int32_t wt_sweep_pos;
static void wt_sweep(uint32_t b)                   /* WT POS 0 .. 127 over 4 s, a note held */
{
    hold_note(b);
    pt.c[0][CP_WP1] = (int16_t)clamp((int32_t)(b * CTL * 127u / (FS * 4u)), 0, 127);
    wt_sweep_pos = pt.c[0][CP_WP1];
}
static void test_wavetables(void)
{
    static double re[16384], im[16384];
    uint32_t bank, fr, note, i, n;
    double worst = 0, hiw = -200;
    printf(" wavetables (W1..W8 %s .. %s): band-limiting, morph, the flash\n", N_WT[0], N_WT[WT_NB - 1]);
    for (bank = 0; bank < WT_NB; bank++) {         /* each bank sounds */
        patch_t p = wt_patch(bank, 64, 0);
        char what[48];
        reset(&p);
        focus = 1, on_note = 48;
        render(0.6, hold_note);
        snprintf(what, sizeof what, "W%u %s sounds (C3, frame 16)", bank + 1, N_WT[bank]);
        check(what, rms_ > 1000 && peak < 32700, "rms %.0f peak %.0f", rms_, peak);
    }
    for (bank = 0; bank < WT_NB; bank++)           /* aliasing: C4 .. C7 at the first, middle, last frame */
        for (fr = 0; fr < 3; fr++)
            for (note = 60; note <= 96; note += 12) {
                patch_t p = wt_patch(bank, (int32_t)(fr * 127 / 2), 0);
                double f0 = 440 * pow(2, ((int)note - 69) / 12.0), on = 0, off = 0, hi = 0, r;
                reset(&p);
                focus = 1, on_note = note;
                render(0.8, hold_note);
                for (i = 0; i < 16384; i++) {
                    double w = 0.5 - 0.5 * cos(2 * M_PI * i / 16384);
                    re[i] = mono[FS / 4 + i] * w, im[i] = 0;
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
                r = off / (on + 1e-30);
                worst = fmax(worst, r);
                hiw = fmax(hiw, 10 * log10(hi / (on + off) + 1e-30));
            }
    check("wavetables band-limited: 8 banks x 3 frames, C4..C7", worst < 1e-3,
          "worst off-harmonic energy %.1f dB, above 16k %.1f dB", 10 * log10(worst + 1e-30), hiw);
    {   /* a knob sweeping WT POS (SCAN, the resonance up the harmonics): the timbre moves, no clicks */
        patch_t p = wt_patch(0, 0, 0);
        double c10, c90, base = 0, sweep = 0;
        reset(&p);
        focus = 1, on_note = 48;
        render(1.0, hold_note);
        for (i = FS / 2; i < nframes; i++) base = fmax(base, fabs(mono[i] - 2 * mono[i - 1] + mono[i - 2]));
        reset(&p);
        focus = 1, on_note = 48;
        wt_loads = 0;
        render(4.0, wt_sweep);
        wav("wt_scan_sweep", pcm, nframes);
        for (i = FS / 2; i < nframes; i++) sweep = fmax(sweep, fabs(mono[i] - 2 * mono[i - 1] + mono[i - 2]));
        n = spectrum_stats(FS / 4, nframes);
        c10 = pct(cen_w, n, 0.1), c90 = pct(cen_w, n, 0.9);
        check("WT POS swept (SCAN): the timbre moves", c90 > c10 * 3, "centroid p10..p90 %.0f..%.0f Hz", c10, c90);
        /* (the SCAN frames' resonance itself has steep slopes: compare with a still sound of its brightest frame) */
        {
            patch_t q = wt_patch(0, 127, 0);
            double b2 = 0;
            reset(&q);
            focus = 1, on_note = 48;
            render(1.0, hold_note);
            for (i = FS / 2; i < nframes; i++) b2 = fmax(b2, fabs(mono[i] - 2 * mono[i - 1] + mono[i - 2]));
            base = fmax(base, b2);
        }
        check("... no clicks (the 2nd difference as a still frame's)", sweep < base * 1.5, "max 2nd difference %.0f vs %.0f",
              sweep, base);
        printf("  info frames loaded from the flash while sweeping 4 s: %u (2 per frame crossed; a voice, a generator)\n",
               wt_loads);
        check("... the flash read only when a frame is crossed", wt_loads <= 2u * 40u, "%.0f frame loads in 4 s%.0s",
              (double)wt_loads, 0);
    }
    {   /* the orbit moving it: WT ORBIT 63, the moon round the sun on a big circle: the position follows the moon */
        patch_t p = wt_patch(2, 0, 63);
        int32_t lo = 1 << 30, hi = -1;
        uint32_t b;
        int32_t out[2 * CTL];
        p.g[G_SIZE] = 127, p.g[G_RATE] = 6, p.g[G_SUBSZ] = 0, p.g[G_SHARP] = 1;
        reset(&p);
        ev_push(EV_ON, 48, 100);
        for (b = 0; b < FS * 3 / CTL; b++) {
            synth_block(out, CTL);
            if (b > FS / CTL / 2)
                lo = vs[0].wpos[0][0] < lo ? vs[0].wpos[0][0] : lo, hi = vs[0].wpos[0][0] > hi ? vs[0].wpos[0][0] : hi;
        }
        check("WT ORBIT: the moon nearing the planet moves the position (VOWEL)", hi - lo > 8 << 16,
              "position %.1f .. %.1f frames", lo / 65536.0, hi / 65536.0);
    }
    {   /* the demo: WAVE ORBIT as it is, and before: its wavetables replaced by the classic pattern C (a saw's even half) */
        patch_t p;
        uint32_t c;
        preset_make(8, &p);
        reset(&p);
        render(9.0, preset_script);
        wav("wt_after_wave_orbit", pcm, nframes);
        for (c = 0; c < NC; c++) {
            if (PAT_IS_WT(p.c[c][CP_G1]))
                p.c[c][CP_G1] = PAT_C;
            if (PAT_IS_WT(p.c[c][CP_G2]))
                p.c[c][CP_G2] = PAT_C;
        }
        reset(&p);
        render(9.0, preset_script);
        wav("wt_before_classic", pcm, nframes);
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
    {   /* the same with both generators of every corner a wavetable (the orbit moving them): the CPU budget */
        double nsw, best8 = 1e30, bestw = 1e30;
        int rep;
        for (rep = 0; rep < 3; rep++) {            /* (the best of three: the host's timing is noisy) */
            p.g[G_RVMIX] = p.g[G_DLMIX] = p.g[G_CHMIX] = 0, p.g[G_DRIVE] = 0;
            for (c = 0; c < NC; c++)
                p.c[c][CP_G1] = PAT_C, p.c[c][CP_G2] = PAT_D;
            reset(&p);
            focus = 5;
            render(1.0, chord10);
            best8 = fmin(best8, render(1.5, 0));
            for (c = 0; c < NC; c++)
                p.c[c][CP_G1] = (int16_t)(PAT_W1 + c), p.c[c][CP_G2] = (int16_t)(PAT_W1 + 4 + c), p.c[c][CP_WO1] = 63, p.c[c][CP_WO2] = -50;
            reset(&p);
            focus = 0;
            render(1.0, chord10);
            bestw = fmin(bestw, render(1.5, 0));
        }
        nsw = bestw;
        printf("  cost (host): 8 voices, every generator a wavetable: %.0f ns/block vs %.0f classic (x %.2f), %.1f %% of real time\n",
               nsw, best8, nsw / best8, nsw / (CTL * 1e9 / FS) * 100);
        check("CPU budget: wavetables at most 1.6 x the classic patterns", nsw < best8 * 1.6, "x %.2f%.0s", nsw / best8, 0);
    }
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
    test_demos();
    test_stereo();
    test_wavetables();
    test_cost();
    printf(fails ? "host: %d FAILED\n" : "host: all ok\n", fails);
    return fails != 0;
}
