/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Vector37: Copyright (C) 2026 M. Geisler (makkuro) */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;
    uint8_t pad;               /* bitmap starts this many pixels left of the pen */
    uint8_t first, last;
    uint8_t sh;                /* scale: > 1, another font's bitmaps drawn enlarged (L = S at 2x, XL at 3x) */
    const uint8_t *adv;        /* advance per glyph */
    const uint8_t *bw;         /* bitmap width per glyph (starts FONT_PAD left of the pen) */
    const uint16_t *off;       /* byte offset of each glyph */
    const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool"), aligned(4)));
static uint16_t *cv_buf = cv_px; /* the canvas drawn into: cv_px, or (cv_begin2) one of its halves */
static uint32_t cv_w, cv_h;
static int32_t cv_ox, cv_oy;     /* canvas origin: a drawing at (x, y) lands at canvas (x + cv_ox, y + cv_oy) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* The screen is five steps of one colour, darkest to brightest, plus white.
 * Palettes are picked in the HOME-hold menu (COLOR). */
typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;
static const palette_t PALETTES[] = {
    {"GREEN", {RGB(0, 40, 12), RGB(0, 84, 30), RGB(16, 140, 54), RGB(56, 200, 92), RGB(120, 255, 146)}},
    {"AMBER", {RGB(60, 26, 0), RGB(110, 50, 0), RGB(170, 82, 0), RGB(225, 120, 8), RGB(255, 166, 40)}},
    {"CYAN", {RGB(0, 30, 50), RGB(0, 62, 96), RGB(16, 112, 160), RGB(56, 172, 222), RGB(140, 222, 255)}},
    {"RED", {RGB(52, 8, 8), RGB(100, 18, 14), RGB(170, 36, 26), RGB(226, 64, 48), RGB(255, 112, 92)}},
    {"MONO", {RGB(40, 40, 40), RGB(80, 80, 80), RGB(130, 130, 130), RGB(186, 186, 186), RGB(226, 226, 226)}},
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
#define C_LINE pal[0]                /* 1 rules, separators */
#define C_DIM pal[1]                 /* 2 inactive, empty steps, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text */
#define C_HI pal[4]                  /* 5 values, curves */

static void palette_set(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        pal[k] = PALETTES[i % NPALETTES].c[k];
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_clear(uint16_t bg)
{
    uint32_t n = cv_w * cv_h, i, v = swap16(bg);
    typedef uint32_t __attribute__((may_alias)) word_t;
    word_t *d = (word_t *)(void *)cv_buf;       /* (two pixels a word: every canvas starts on a word) */
    v |= v << 16;
    for (i = 0; i < n / 2u; i++)
        d[i] = v;
    if (n & 1u)
        cv_buf[n - 1u] = (uint16_t)v;
}

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    cv_buf = cv_px;
    lcd_sync_buf(cv_buf, w * h * 2u);  /* the last blit may still read it */
    cv_w = w;
    cv_h = h;
    cv_clear(bg);
}

/* the same in one half of cv_px, the other half than last time: while the SPI sends one strip, the next is drawn
 * (lcd.c waits only for a transfer that still reads the half drawn into). At most CV_HALF pixels. */
#define CV_HALF (CV_MAX / 2u)
static void cv_begin2(uint32_t w, uint32_t h, uint16_t bg)
{
    static uint8_t half;
    if (w * h > CV_HALF)
        h = CV_HALF / w;
    half ^= 1u;
    cv_buf = cv_px + (half ? CV_HALF : 0u);
    lcd_sync_buf(cv_buf, w * h * 2u);
    cv_w = w;
    cv_h = h;
    cv_clear(bg);
}

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_buf); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_buf + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    x += cv_ox;
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_buf[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w + cv_ox, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    x += cv_ox;
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_buf[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    if ((x0 < x1 ? x1 : x0) + cv_ox < 0 || (x0 < x1 ? x0 : x1) + cv_ox >= (int32_t)cv_w ||   /* (off the canvas: */
        (y0 < y1 ? y1 : y0) + cv_oy < 0 || (y0 < y1 ? y0 : y1) + cv_oy >= (int32_t)cv_h)     /* nothing to draw) */
        return;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* text, alpha-blended onto black with colour c; returns the end x */
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    int32_t cy0 = -cv_oy - y, cy1 = (int32_t)cv_h - cv_oy - y, cx1 = (int32_t)cv_w - cv_ox;   /* (the canvas' rows, */
    uint32_t gy0 = cy0 > 0 ? (uint32_t)cy0 : 0u, gy1 = cy1 < (int32_t)f->h ? (cy1 > 0 ? (uint32_t)cy1 : 0u) : f->h;  /* glyph rows) */
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bpr;
        int32_t gl = x - f->pad;
        const uint8_t *gd;
        w = f->bw[gi];
        if (gl >= cx1 || gl + (int32_t)w <= -cv_ox) {   /* (a glyph off the canvas: only its advance) */
            x += f->adv[gi];
            continue;
        }
        bpr = ((w / f->sh) + 1u) / 2u;
        gd = f->data + f->off[gi];
        for (gy = gy0; gy < gy1; gy++) {
            const uint8_t *row = gd + (gy / f->sh) * bpr;
            for (gx = 0; gx < w; gx++) {
                uint32_t sx = gx / f->sh, v = row[sx / 2u];
                v = (sx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(gl + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        }
        x += f->adv[gi];
    }
    return x;
}

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w;
}

/* one-shot: text in a box, cleared to black, blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
