// BrainixKernel — backdrop.c
// Размытие обоев (box blur, separable) + рисование «матовых» панелей.

#include <stdint.h>
#include "backdrop.h"
#include "fb.h"
#include "physmem.h"

#define SCR_W 1280
#define SCR_H 800

// Радиус размытия. Больше = мягче и «дороже».
#define BLUR_RADIUS 14
// Сколько раз прогнать (1 = box, 2 = приближение Гаусса).
#define BLUR_PASSES 2

static uint32_t *g_src   = 0;    // снимок обоев (исходный)
static uint32_t *g_tmp   = 0;    // промежуточный буфер
static uint32_t *g_blur  = 0;    // результат (размытый)
static int       g_ready = 0;
static int       g_dirty = 1;    // нужен ли пересчёт

static inline uint32_t mix_u32(uint32_t fg, uint32_t bg, uint32_t a) {
    uint32_t inv = 255 - a;
    uint32_t fr  = (fg >> 16) & 0xFF, fgg = (fg >> 8) & 0xFF, fb_ = fg & 0xFF;
    uint32_t br  = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bb_ = bg & 0xFF;
    uint32_t r = (fr * a + br * inv) / 255;
    uint32_t g = (fgg * a + bgg * inv) / 255;
    uint32_t b = (fb_ * a + bb_ * inv) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// ----------------------------------------------------------------------------
// Box blur: горизонтальный проход (в src → в dst)
// ----------------------------------------------------------------------------
static void box_blur_h(const uint32_t *src, uint32_t *dst,
                       int w, int h, int r)
{
    int win = 2*r + 1;
    for (int y = 0; y < h; y++) {
        const uint32_t *s = src + (uint64_t)y * w;
        uint32_t *d = dst + (uint64_t)y * w;

        // Инициализация аккумулятора: сумма [0..r] с краевым дублированием
        int A = 0, R = 0, G = 0, B = 0;
        for (int i = -r; i <= r; i++) {
            int xx = i < 0 ? 0 : (i >= w ? w-1 : i);
            uint32_t c = s[xx];
            A += (c >> 24) & 0xFF;
            R += (c >> 16) & 0xFF;
            G += (c >> 8)  & 0xFF;
            B +=  c        & 0xFF;
        }

        for (int x = 0; x < w; x++) {
            d[x] = ((uint32_t)(A / win) << 24)
                 | ((uint32_t)(R / win) << 16)
                 | ((uint32_t)(G / win) << 8)
                 |  (uint32_t)(B / win);

            int xl = x - r;
            int xr = x + r + 1;
            if (xl < 0) xl = 0;
            if (xr >= w) xr = w - 1;

            uint32_t cl = s[xl];
            uint32_t cr = s[xr];

            A += (int)((cr >> 24) & 0xFF) - (int)((cl >> 24) & 0xFF);
            R += (int)((cr >> 16) & 0xFF) - (int)((cl >> 16) & 0xFF);
            G += (int)((cr >> 8)  & 0xFF) - (int)((cl >> 8)  & 0xFF);
            B += (int)( cr        & 0xFF) - (int)( cl        & 0xFF);
        }
    }
}

// ----------------------------------------------------------------------------
// Box blur: вертикальный проход
// ----------------------------------------------------------------------------
static void box_blur_v(const uint32_t *src, uint32_t *dst,
                       int w, int h, int r)
{
    int win = 2*r + 1;
    for (int x = 0; x < w; x++) {
        int A = 0, R = 0, G = 0, B = 0;
        for (int i = -r; i <= r; i++) {
            int yy = i < 0 ? 0 : (i >= h ? h-1 : i);
            uint32_t c = src[(uint64_t)yy * w + x];
            A += (c >> 24) & 0xFF;
            R += (c >> 16) & 0xFF;
            G += (c >> 8)  & 0xFF;
            B +=  c        & 0xFF;
        }
        for (int y = 0; y < h; y++) {
            dst[(uint64_t)y * w + x] =
                ((uint32_t)(A / win) << 24)
              | ((uint32_t)(R / win) << 16)
              | ((uint32_t)(G / win) << 8)
              |  (uint32_t)(B / win);

            int yl = y - r;
            int yr = y + r + 1;
            if (yl < 0) yl = 0;
            if (yr >= h) yr = h - 1;

            uint32_t cl = src[(uint64_t)yl * w + x];
            uint32_t cr = src[(uint64_t)yr * w + x];

            A += (int)((cr >> 24) & 0xFF) - (int)((cl >> 24) & 0xFF);
            R += (int)((cr >> 16) & 0xFF) - (int)((cl >> 16) & 0xFF);
            G += (int)((cr >> 8)  & 0xFF) - (int)((cl >> 8)  & 0xFF);
            B += (int)( cr        & 0xFF) - (int)( cl        & 0xFF);
        }
    }
}

// ----------------------------------------------------------------------------
void backdrop_init(void) {
    uint64_t pages = ((uint64_t)SCR_W * SCR_H * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
    g_src  = (uint32_t *)phys_alloc_pages(pages);
    g_tmp  = (uint32_t *)phys_alloc_pages(pages);
    g_blur = (uint32_t *)phys_alloc_pages(pages);
    if (g_src && g_tmp && g_blur) g_ready = 1;
    g_dirty = 1;
}

void backdrop_invalidate(void) { g_dirty = 1; }

void backdrop_capture(void) {
    if (!g_ready || !g_dirty) return;

    // 1. Снимок обоев
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            g_src[(uint64_t)y * SCR_W + x] = fb_getpixel(x, y);
        }
    }

    // 2. Размытие (2 прохода)
    uint32_t *in  = g_src;
    uint32_t *mid = g_tmp;
    uint32_t *out = g_blur;

    for (int pass = 0; pass < BLUR_PASSES; pass++) {
        box_blur_h(in,  mid, SCR_W, SCR_H, BLUR_RADIUS);
        box_blur_v(mid, out, SCR_W, SCR_H, BLUR_RADIUS);
        // Цикл: следующий проход размывает уже размытое
        in  = out;
        // swap: out становится mid для следующей итерации
        uint32_t *t = mid; mid = out; out = t;
        // После swap: in снова указывает на результат предыдущего шага.
        // Проще: результат предыдущего — либо g_blur, либо g_tmp.
        // Чтобы не путаться, всегда используем буферы явно.
        if (pass + 1 < BLUR_PASSES) {
            in = (pass % 2 == 0) ? g_blur : g_tmp;
        }
    }

    g_dirty = 0;
}

// ----------------------------------------------------------------------------
// Рисуем матовую панель
// ----------------------------------------------------------------------------
static void draw_frosted_row(int x, int y, int w,
                             uint32_t tint, uint32_t alpha)
{
    if (y < 0 || y >= SCR_H) return;
    int x0 = x;
    int x1 = x + w;
    if (x0 < 0) x0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (x0 >= x1) return;

    const uint32_t *src_row = g_blur + (uint64_t)y * SCR_W;
    for (int xx = x0; xx < x1; xx++) {
        uint32_t bg = src_row[xx];
        uint32_t c  = mix_u32(tint, bg, alpha);
        fb_putpixel(xx, y, c);
    }
}

void backdrop_draw_frosted_rect(int x, int y, int w, int h,
                                uint32_t tint, uint32_t alpha)
{
    if (!g_ready) {
        fb_blend_rect(x, y, w, h, tint, alpha);
        return;
    }
    for (int j = 0; j < h; j++) draw_frosted_row(x, y + j, w, tint, alpha);
}

void backdrop_draw_frosted(int x, int y, int w, int h, int r,
                           uint32_t tint, uint32_t alpha)
{
    if (!g_ready) {
        fb_blend_round_rect_aa(x, y, w, h, r, tint, alpha);
        return;
    }
    if (r < 0) r = 0;
    if (r > w/2) r = w/2;
    if (r > h/2) r = h/2;

    if (r == 0) {
        backdrop_draw_frosted_rect(x, y, w, h, tint, alpha);
        return;
    }

    int r2x4 = 4 * r * r;

    for (int j = 0; j < h; j++) {
        int inset = 0;
        if (j < r || j >= h - r) {
            int jj = (j < r) ? j : (h - 1 - j);
            int found = -1;
            for (int i = 0; i < r; i++) {
                int dx = 2*i + 1 - 2*r;
                int dy = 2*jj + 1 - 2*r;
                if (dx*dx + dy*dy <= r2x4) { found = i; break; }
            }
            if (found < 0) found = r;
            inset = found;
        }
        int row_w = w - 2*inset;
        if (row_w <= 0) continue;
        draw_frosted_row(x + inset, y + j, row_w, tint, alpha);
    }
}