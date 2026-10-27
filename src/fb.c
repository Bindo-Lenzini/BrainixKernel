// BrainixKernel — fb.c
// 2× retina + SSAA (2×2 oversampling на каждый физический пиксель).
// Все AA-примитивы (rounded rect, circle, blend) считают alpha в
// физических пикселях — никакой мозаики 2×2.

#include <stdint.h>
#include "fb.h"
#include "gpu.h"
#include "font8x8.h"
#include "font8x16_aa.h"

static uint32_t *g_base        = 0;
static uint32_t  g_w_phys      = 0;
static uint32_t  g_h_phys      = 0;
static uint32_t  g_stride_phys = 0;
static uint32_t  g_w_log       = 0;
static uint32_t  g_h_log       = 0;

static int       g_con_ready   = 0;
static int       g_con_enabled = 1;
static int       g_con_x = 0;
static int       g_con_y = 0;
static uint32_t  g_con_fg = 0xFFE0E0E0;
static uint32_t  g_con_bg = 0xFF101828;

#define LINE_H 10
static int g_dirty = 0;
static int g_dirty_x0 = 9999, g_dirty_y0 = 9999;
static int g_dirty_x1 = -1, g_dirty_y1 = -1;

static inline uint32_t mix_u32(uint32_t fg, uint32_t bg, uint32_t a) {
    uint32_t inv = 255 - a;
    uint32_t fr  = (fg >> 16) & 0xFF, fgg = (fg >> 8) & 0xFF, fb_ = fg & 0xFF;
    uint32_t br  = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bb_ = bg & 0xFF;
    uint32_t r = (fr * a + br * inv) / 255;
    uint32_t g = (fgg * a + bgg * inv) / 255;
    uint32_t b = (fb_ * a + bb_ * inv) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void fb_mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    g_dirty = 1;
    if (x < g_dirty_x0) g_dirty_x0 = x;
    if (y < g_dirty_y0) g_dirty_y0 = y;
    if (x + w > g_dirty_x1) g_dirty_x1 = x + w;
    if (y + h > g_dirty_y1) g_dirty_y1 = y + h;
}

void fb_init(uint32_t *base, uint32_t w_phys, uint32_t h_phys,
             uint32_t stride_phys) {
    g_base        = base;
    g_w_phys      = w_phys;
    g_h_phys      = h_phys;
    g_stride_phys = stride_phys ? stride_phys : w_phys;
    g_w_log       = w_phys / FB_SCALE;
    g_h_log       = h_phys / FB_SCALE;
    g_con_x = 0; g_con_y = 0;
    g_con_ready = 1; g_con_enabled = 1;
    g_dirty = 0;
}

uint32_t fb_width(void)  { return g_w_log; }
uint32_t fb_height(void) { return g_h_log; }

void fb_putpixel(int x, int y, uint32_t color) {
    fb_mark_dirty(x, y, 1, 1);
    if (!g_base) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= g_w_log || (uint32_t)y >= g_h_log) return;
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    for (int j = 0; j < FB_SCALE; j++) {
        uint32_t *row = g_base + (uint32_t)(py + j) * g_stride_phys;
        for (int i = 0; i < FB_SCALE; i++) row[px + i] = color;
    }
    g_dirty = 1;
}

uint32_t fb_getpixel(int x, int y) {
    if (!g_base) return 0;
    if (x < 0 || y < 0) return 0;
    if ((uint32_t)x >= g_w_log || (uint32_t)y >= g_h_log) return 0;
    return g_base[(uint32_t)(y * FB_SCALE) * g_stride_phys +
                  (uint32_t)(x * FB_SCALE)];
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    fb_mark_dirty(x, y, w, h);
    if (!g_base || w <= 0 || h <= 0) return;
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    int pw = w * FB_SCALE;
    int ph = h * FB_SCALE;
    int x0 = px, y0 = py, x1 = px + pw, y1 = py + ph;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_w_phys) x1 = (int)g_w_phys;
    if (y1 > (int)g_h_phys) y1 = (int)g_h_phys;
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = g_base + (uint32_t)yy * g_stride_phys;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
    g_dirty = 1;
}

void fb_fill_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    if (h <= 0) return;
    int denom = (h > 1) ? (h - 1) : 1;
    for (int j = 0; j < h; j++) {
        uint32_t t = (uint32_t)j * 255 / denom;
        uint32_t inv = 255 - t;
        uint32_t r = (((top >> 16) & 0xFF) * inv + ((bot >> 16) & 0xFF) * t) / 255;
        uint32_t g = (((top >> 8)  & 0xFF) * inv + ((bot >> 8)  & 0xFF) * t) / 255;
        uint32_t b = (( top        & 0xFF) * inv + ( bot        & 0xFF) * t) / 255;
        fb_fill_rect(x, y + j, w, 1, 0xFF000000u | (r << 16) | (g << 8) | b);
    }
}

void fb_darken_rect(int x, int y, int w, int h, uint32_t factor) {
    fb_mark_dirty(x, y, w, h);
    if (!g_base) return;
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    int pw = w * FB_SCALE;
    int ph = h * FB_SCALE;
    int x0 = px, y0 = py, x1 = px + pw, y1 = py + ph;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_w_phys) x1 = (int)g_w_phys;
    if (y1 > (int)g_h_phys) y1 = (int)g_h_phys;
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = g_base + (uint32_t)yy * g_stride_phys;
        for (int xx = x0; xx < x1; xx++) {
            uint32_t c = row[xx];
            uint32_t r = (((c >> 16) & 0xFF) * factor) / 255;
            uint32_t g = (((c >> 8)  & 0xFF) * factor) / 255;
            uint32_t b = (( c        & 0xFF) * factor) / 255;
            row[xx] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    g_dirty = 1;
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!g_base) return;
    unsigned ch = (unsigned char)c;
    if (ch < 0x20 || ch > 0x7F) ch = '?';
    const uint8_t *glyph = font8x8_basic[ch - 0x20];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
            fb_putpixel(x + col, y + row, (bits & (1u << col)) ? fg : bg);
    }
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        if (*s == '\n') { x = 0; y += LINE_H; s++; continue; }
        fb_draw_char(x, y, *s, fg, bg);
        x += 8; s++;
    }
}

int font_index(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7F) return (int)(cp - 0x20);
    if (cp >= 0x0410 && cp <= 0x044F) {
        int idx = 96 + (int)(cp - 0x0410);
        if (idx < font8x16_aa_count) return idx;
    }
    if (cp == 0x00D7) return (int)('x' - 0x20);
    if (cp == 0x2014 || cp == 0x2013) return (int)('-' - 0x20);
    if (cp == 0x00AB) return (int)('<' - 0x20);
    if (cp == 0x00BB) return (int)('>' - 0x20);
    return -1;
}

void fb_draw_cp_aa(int x, int y, uint32_t cp, uint32_t fg, uint32_t bg) {
    fb_mark_dirty(x, y, 8, 16);
    if (!g_base) return;
    int idx = font_index(cp);
    if (idx < 0) return;
    const uint8_t (*glyph)[16] = font8x16_aa[idx];

    int px0 = x * FB_SCALE;
    int py0 = y * FB_SCALE;

    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 16; col++) {
            uint32_t a = glyph[row][col];
            int px = px0 + col;
            int py = py0 + row;
            if (px < 0 || py < 0) continue;
            if ((uint32_t)px >= g_w_phys || (uint32_t)py >= g_h_phys) continue;

            uint32_t color;
            if (a == 0) {
                if (bg == 0) continue;
                color = bg;
            } else if (bg == 0) {
                uint32_t dst = g_base[(uint32_t)py * g_stride_phys + (uint32_t)px];
                color = mix_u32(fg, dst, a);
            } else {
                color = mix_u32(fg, bg, a);
            }
            g_base[(uint32_t)py * g_stride_phys + (uint32_t)px] = color;
        }
    }
    g_dirty = 1;
}

void fb_draw_char_aa(int x, int y, char c, uint32_t fg, uint32_t bg) {
    fb_draw_cp_aa(x, y, (uint32_t)(uint8_t)c, fg, bg);
}

static int u8_decode(const char *s, uint32_t *out) {
    uint8_t b0 = (uint8_t)s[0];
    if (b0 < 0x80) { *out = b0; return 1; }
    if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = (uint8_t)s[1];
        if ((b1 & 0xC0) != 0x80) { *out = '?'; return 1; }
        *out = ((uint32_t)(b0 & 0x1F) << 6) | (b1 & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = (uint8_t)s[1], b2 = (uint8_t)s[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { *out = '?'; return 1; }
        *out = ((uint32_t)(b0 & 0x0F) << 12) |
               ((uint32_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0) {
        uint8_t b1 = (uint8_t)s[1], b2 = (uint8_t)s[2], b3 = (uint8_t)s[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
            *out = '?'; return 1;
        }
        *out = ((uint32_t)(b0 & 0x07) << 18) |
               ((uint32_t)(b1 & 0x3F) << 12) |
               ((uint32_t)(b2 & 0x3F) << 6) | (b3 & 0x3F);
        return 4;
    }
    *out = '?';
    return 1;
}

void fb_draw_string_aa(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += 16; s++; continue; }
        uint32_t cp;
        int n = u8_decode(s, &cp);
        if (n <= 0) { s++; continue; }
        fb_draw_cp_aa(cx, y, cp, fg, bg);
        cx += 8;
        s += n;
    }
}

// ============================================================
// SSAA-ядро для rounded-rect.
// sub_inside_rrect: проверка субпикселя (в 4× масштабе).
// shape: 0 = все углы, 1 = только верхние, 2 = только нижние
// ============================================================
static int sub_inside_rrect(int sx4, int sy4, int W4, int H4, int R4, int shape) {
    if (sx4 < 0 || sy4 < 0 || sx4 >= W4 || sy4 >= H4) return 0;

    int cxl = R4;
    int cxr = W4 - R4;
    int cyt = R4;
    int cyb = H4 - R4;
    int dx, dy;

    if (shape != 2 && sx4 < cxl && sy4 < cyt) {
        dx = sx4 - cxl; dy = sy4 - cyt;
        return dx*dx + dy*dy <= R4*R4;
    }
    if (shape != 2 && sx4 >= cxr && sy4 < cyt) {
        dx = sx4 - cxr; dy = sy4 - cyt;
        return dx*dx + dy*dy <= R4*R4;
    }
    if (shape != 1 && sx4 < cxl && sy4 >= cyb) {
        dx = sx4 - cxl; dy = sy4 - cyb;
        return dx*dx + dy*dy <= R4*R4;
    }
    if (shape != 1 && sx4 >= cxr && sy4 >= cyb) {
        dx = sx4 - cxr; dy = sy4 - cyb;
        return dx*dx + dy*dy <= R4*R4;
    }
    return 1;
}

// Основная функция: рисует rounded rect в ФИЗИЧЕСКИХ координатах
// с градиентом top→bot, используя 2×2 SSAA для каждого физ. пикселя.
// Если alpha < 255 — дополнительно смешивает с фоном (blend).
static void ssaa_rrect(int X, int Y, int W, int H, int R,
                       uint32_t top, uint32_t bot, int shape,
                       uint32_t alpha)
{
    if (W <= 0 || H <= 0 || alpha == 0) return;
    if (R < 0) R = 0;
    if (R > W/2) R = W/2;
    if (R > H/2) R = H/2;

    int W4 = W * 4;
    int H4 = H * 4;
    int R4 = R * 4;
    int denom = (H > 1) ? (H - 1) : 1;

    int cxl = R4, cxr = W4 - R4;
    int top_corner_zone = R;
    int bot_corner_zone = H - R;

    for (int py = 0; py < H; py++) {
        int phys_y = Y + py;
        if (phys_y < 0 || (uint32_t)phys_y >= g_h_phys) continue;
        uint32_t *row = g_base + (uint32_t)phys_y * g_stride_phys;

        // Цвет для строки
        uint32_t t = (uint32_t)py * 255 / denom;
        uint32_t inv = 255 - t;
        uint32_t rr = (((top >> 16) & 0xFF) * inv + ((bot >> 16) & 0xFF) * t) / 255;
        uint32_t gg = (((top >> 8)  & 0xFF) * inv + ((bot >> 8)  & 0xFF) * t) / 255;
        uint32_t bb = (( top        & 0xFF) * inv + ( bot        & 0xFF) * t) / 255;
        uint32_t c = 0xFF000000u | (rr << 16) | (gg << 8) | bb;

        // Определяем, есть ли в этой строке угловые зоны
        int has_top_corner = (shape != 2) && (py < top_corner_zone);
        int has_bot_corner = (shape != 1) && (py >= bot_corner_zone);
        int in_corner_row = has_top_corner || has_bot_corner;

        int py4_top = py * 4 + 1;
        int py4_bot = py * 4 + 3;

        for (int px = 0; px < W; px++) {
            int phys_x = X + px;
            if (phys_x < 0 || (uint32_t)phys_x >= g_w_phys) continue;

            int inside = 4;   // полное покрытие по умолчанию
            if (in_corner_row) {
                int px4_l = px * 4 + 1;
                int px4_r = px * 4 + 3;

                int in_corner = 0;
                if (has_top_corner && px4_l < cxl) in_corner = 1;
                if (has_top_corner && px4_r >= cxr) in_corner = 1;
                if (has_bot_corner && px4_l < cxl) in_corner = 1;
                if (has_bot_corner && px4_r >= cxr) in_corner = 1;

                if (in_corner) {
                    inside = 0;
                    if (sub_inside_rrect(px4_l, py4_top, W4, H4, R4, shape)) inside++;
                    if (sub_inside_rrect(px4_r, py4_top, W4, H4, R4, shape)) inside++;
                    if (sub_inside_rrect(px4_l, py4_bot, W4, H4, R4, shape)) inside++;
                    if (sub_inside_rrect(px4_r, py4_bot, W4, H4, R4, shape)) inside++;
                }
            }

            if (inside == 0) continue;

            uint32_t dst = row[phys_x];
            uint32_t final_a;
            if (inside == 4) final_a = alpha;
            else             final_a = (alpha * (uint32_t)inside) / 4;

            if (final_a >= 255)      row[phys_x] = c;
            else if (final_a == 0)   continue;
            else                     row[phys_x] = mix_u32(c, dst, final_a);
        }
    }
    g_dirty = 1;
}

// ============================================================
// Публичные AA-функции
// ============================================================

void fb_fill_round_rect_aa(int x, int y, int w, int h, int r, uint32_t color) {
    ssaa_rrect(x * FB_SCALE, y * FB_SCALE,
               w * FB_SCALE, h * FB_SCALE, r * FB_SCALE,
               color, color, 0, 255);
}

void fb_fill_round_rect_top_aa(int x, int y, int w, int h, int r, uint32_t color) {
    ssaa_rrect(x * FB_SCALE, y * FB_SCALE,
               w * FB_SCALE, h * FB_SCALE, r * FB_SCALE,
               color, color, 1, 255);
}

void fb_fill_round_rect_bot_aa(int x, int y, int w, int h, int r, uint32_t color) {
    ssaa_rrect(x * FB_SCALE, y * FB_SCALE,
               w * FB_SCALE, h * FB_SCALE, r * FB_SCALE,
               color, color, 2, 255);
}

void fb_fill_round_rect_v_grad_aa(int x, int y, int w, int h, int r,
                                  uint32_t top, uint32_t bot) {
    ssaa_rrect(x * FB_SCALE, y * FB_SCALE,
               w * FB_SCALE, h * FB_SCALE, r * FB_SCALE,
               top, bot, 0, 255);
}

void fb_blend_round_rect_aa(int x, int y, int w, int h, int r,
                            uint32_t color, uint32_t alpha) {
    ssaa_rrect(x * FB_SCALE, y * FB_SCALE,
               w * FB_SCALE, h * FB_SCALE, r * FB_SCALE,
               color, color, 0, alpha);
}

// Круг = rounded rect с R = W/2.
void fb_fill_circle_aa(int cx, int cy, int r, uint32_t color) {
    if (r <= 0) return;
    int X = (cx - r) * FB_SCALE;
    int Y = (cy - r) * FB_SCALE;
    int D = (2 * r) * FB_SCALE;
    ssaa_rrect(X, Y, D, D, D / 2, color, color, 0, 255);
}

// ============================================================
// Обычные (не-AA) заливки
// ============================================================
void fb_blend_rect(int x, int y, int w, int h, uint32_t color, uint32_t alpha) {
    if (!g_base || alpha == 0 || w <= 0 || h <= 0) return;
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    int pw = w * FB_SCALE;
    int ph = h * FB_SCALE;
    int x0 = px, y0 = py, x1 = px + pw, y1 = py + ph;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_w_phys) x1 = (int)g_w_phys;
    if (y1 > (int)g_h_phys) y1 = (int)g_h_phys;
    if (x0 >= x1 || y0 >= y1) return;

    if (alpha >= 255) {
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = g_base + (uint32_t)yy * g_stride_phys;
            for (int xx = x0; xx < x1; xx++) row[xx] = color;
        }
    } else {
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = g_base + (uint32_t)yy * g_stride_phys;
            for (int xx = x0; xx < x1; xx++)
                row[xx] = mix_u32(color, row[xx], alpha);
        }
    }
    g_dirty = 1;
}

__attribute__((unused))
static void plot_blend(int x, int y, uint32_t color, uint32_t alpha) {
    if (alpha == 0) return;
    if (alpha >= 255) { fb_putpixel(x, y, color); return; }
    uint32_t dst = fb_getpixel(x, y);
    fb_putpixel(x, y, mix_u32(color, dst, alpha));
}

void fb_draw_line_aa(int x0, int y0, int x1, int y1,
                     uint32_t color, uint32_t alpha) {
    // Рисуем линию в физических координатах с AA.
    int X0 = x0 * FB_SCALE;
    int Y0 = y0 * FB_SCALE;
    int X1 = x1 * FB_SCALE;
    int Y1 = y1 * FB_SCALE;
    int dx = X1 - X0, dy = Y1 - Y0;
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;
    dx = (dx < 0) ? -dx : dx;
    dy = (dy < 0) ? -dy : dy;

    int steps = (dx > dy) ? dx : dy;
    if (steps == 0) {
        for (int j = 0; j < FB_SCALE; j++)
            for (int i = 0; i < FB_SCALE; i++) {
                int px = X0 + i, py = Y0 + j;
                if (px < 0 || py < 0) continue;
                if ((uint32_t)px >= g_w_phys || (uint32_t)py >= g_h_phys) continue;
                uint32_t *r = g_base + (uint32_t)py * g_stride_phys;
                r[px] = (alpha >= 255) ? color : mix_u32(color, r[px], alpha);
            }
        return;
    }

    for (int s = 0; s <= steps; s++) {
        int px = X0 + (dx ? (s * dx / steps) * sx : 0);
        int py = Y0 + (dy ? (s * dy / steps) * sy : 0);
        for (int j = 0; j < FB_SCALE; j++) {
            for (int i = 0; i < FB_SCALE; i++) {
                int fx = px + i, fy = py + j;
                if (fx < 0 || fy < 0) continue;
                if ((uint32_t)fx >= g_w_phys || (uint32_t)fy >= g_h_phys) continue;
                uint32_t *r = g_base + (uint32_t)fy * g_stride_phys;
                r[fx] = (alpha >= 255) ? color : mix_u32(color, r[fx], alpha);
            }
        }
    }
    g_dirty = 1;
}

static inline uint32_t blend_argb(uint32_t src, uint32_t dst) {
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)   return dst;
    if (sa == 255) return src | 0xFF000000u;
    return mix_u32(src & 0x00FFFFFF, dst, sa);
}

void fb_blit_argb(int x, int y, int w, int h, const uint32_t *pixels) {
    if (!g_base) return;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= (int)g_h_log) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < 0 || px >= (int)g_w_log) continue;
            uint32_t src = pixels[j * w + i];
            if (((src >> 24) & 0xFF) == 0) continue;
            uint32_t dst = fb_getpixel(px, py);
            fb_putpixel(px, py, blend_argb(src, dst));
        }
    }
    g_dirty = 1;
}

void fb_blit_argb_scaled(int x, int y, int dw, int dh,
                         int sw, int sh, const uint32_t *pixels) {
    if (!g_base || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int j = 0; j < dh; j++) {
        int py = y + j;
        if (py < 0 || py >= (int)g_h_log) continue;
        int sy = j * sh / dh;
        for (int i = 0; i < dw; i++) {
            int px = x + i;
            if (px < 0 || px >= (int)g_w_log) continue;
            int sx = i * sw / dw;
            uint32_t src = pixels[sy * sw + sx];
            if (((src >> 24) & 0xFF) == 0) continue;
            uint32_t dst = fb_getpixel(px, py);
            fb_putpixel(px, py, blend_argb(src, dst));
        }
    }
    g_dirty = 1;
}

int fb_console_is_ready(void) { return g_con_ready && g_con_enabled; }
void fb_console_clear(void) {
    if (!g_con_ready) return;
    fb_fill_rect(0, 0, (int)g_w_log, (int)g_h_log, g_con_bg);
    g_con_x = 0; g_con_y = 0;
}
static void wipe_rest_of_line(void) {
    if (g_con_y + 8 > (int)g_h_log || g_con_x >= (int)g_w_log) return;
    fb_fill_rect(g_con_x, g_con_y, (int)g_w_log - g_con_x, 8, g_con_bg);
}
static void next_line(void) {
    g_con_x = 0;
    g_con_y += LINE_H;
    if (g_con_y + 8 > (int)g_h_log) { fb_console_clear(); return; }
    fb_fill_rect(0, g_con_y, (int)g_w_log, 8, g_con_bg);
}
void fb_console_putc(char c) {
    if (!g_con_ready || !g_con_enabled) return;
    if (c == '\r') { wipe_rest_of_line(); g_con_x = 0; return; }
    if (c == '\n') { wipe_rest_of_line(); next_line(); return; }
    if ((unsigned char)c < 0x20) return;
    if (g_con_x + 8 > (int)g_w_log) next_line();
    fb_draw_char(g_con_x, g_con_y, c, g_con_fg, g_con_bg);
    g_con_x += 8;
}


void fb_get_pixels(int x, int y, int w, int h, uint32_t *out) {
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    int pw = w * FB_SCALE;
    int ph = h * FB_SCALE;
    if (px < 0 || py < 0 || px + pw > (int)g_w_phys || py + ph > (int)g_h_phys) return;
    for (int j = 0; j < ph; j++) {
        uint32_t *src_row = g_base + (uint32_t)(py + j) * g_stride_phys + px;
        uint32_t *dst_row = out + (uint64_t)j * pw;
        for (int i = 0; i < pw; i++) dst_row[i] = src_row[i];
    }
}
void fb_put_pixels(int x, int y, int w, int h, const uint32_t *in) {
    int px = x * FB_SCALE;
    int py = y * FB_SCALE;
    int pw = w * FB_SCALE;
    int ph = h * FB_SCALE;
    if (px < 0 || py < 0 || px + pw > (int)g_w_phys || py + ph > (int)g_h_phys) return;
    for (int j = 0; j < ph; j++) {
        uint32_t *dst_row = g_base + (uint32_t)(py + j) * g_stride_phys + px;
        const uint32_t *src_row = in + (uint64_t)j * pw;
        for (int i = 0; i < pw; i++) dst_row[i] = src_row[i];
    }
    fb_mark_dirty(x, y, w, h);
}

void fb_flush(void) {
    if (!g_con_ready) return;
    g_con_enabled = 0;
    gpu_flush();
    g_con_enabled = 1;
    g_dirty = 0;
}
void fb_flush_dirty(void) {
    if (!g_con_ready || !g_dirty) return;

    g_con_enabled = 0;
    
    int px = g_dirty_x0 * FB_SCALE;
    int py = g_dirty_y0 * FB_SCALE;
    int pw = (g_dirty_x1 - g_dirty_x0) * FB_SCALE;
    int ph = (g_dirty_y1 - g_dirty_y0) * FB_SCALE;
    
    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > (int)g_w_phys) pw = g_w_phys - px;
    if (py + ph > (int)g_h_phys) ph = g_h_phys - py;

    if (pw > 0 && ph > 0) {
        gpu_flush_rect(px, py, pw, ph);
    }
    
    g_con_enabled = 1;
    
    g_dirty = 0;
    g_dirty_x0 = 9999; g_dirty_y0 = 9999;
    g_dirty_x1 = -1; g_dirty_y1 = -1;
}