// BrainixKernel — wm.c

#include <stdint.h>
#include "wm.h"
#include "fb.h"

extern void uart_puts(const char *s);

#define TITLEBAR_H 28
#define BORDER_W   2
#define RESIZE_SIZE 14
#define MIN_W      200
#define MIN_H      120
#define RADIUS     8
#define TITLE_R    6

#define CLR_BORDER      0xFF5AA0F0
#define CLR_BORDER_NF   0xFF3A5570
#define CLR_TITLE_TOP   0xFF3B6FB8
#define CLR_TITLE_BOT   0xFF1F3F6F
#define CLR_TITLE_NF    0xFF2A3F55
#define CLR_TITLE_FG    0xFFF0F0F0
#define CLR_BODY        0xFF1A2E4A
#define CLR_HANDLE      0xFF7AB8FF

static wnd_t g_wnd[WM_MAX];
static int   g_count = 0;

static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void wm_init(void) {
    for (int i = 0; i < WM_MAX; i++) {
        g_wnd[i].type = WND_NONE;
        g_wnd[i].z = 0;
        g_wnd[i].visible = 0;
        g_wnd[i].focused = 0;
        g_wnd[i].minimized = 0;
        g_wnd[i].maximized = 0;
        g_wnd[i].saved_x = g_wnd[i].saved_y = 0;
        g_wnd[i].saved_w = g_wnd[i].saved_h = 0;
    }
    g_count = 0;
}

int wm_create(wnd_type_t type, int x, int y, int w, int h, const char *title) {
    if (g_count >= WM_MAX) return -1;
    int idx = g_count++;
    g_wnd[idx].type = type;
    g_wnd[idx].x = x; g_wnd[idx].y = y;
    g_wnd[idx].w = w; g_wnd[idx].h = h;
    g_wnd[idx].z = idx;
    g_wnd[idx].visible = 1;
    g_wnd[idx].minimized = 0;
    g_wnd[idx].maximized = 0;
    g_wnd[idx].focused = 0;
    g_wnd[idx].saved_x = x; g_wnd[idx].saved_y = y;
    g_wnd[idx].saved_w = w; g_wnd[idx].saved_h = h;
    str_copy_n(g_wnd[idx].title, title, 40);
    return idx;
}

void wm_show(int idx, int show) {
    if (idx < 0 || idx >= g_count) return;
    g_wnd[idx].visible = show;
    if (show) g_wnd[idx].minimized = 0;
}
void wm_set_title(int idx, const char *title) {
    if (idx < 0 || idx >= g_count) return;
    str_copy_n(g_wnd[idx].title, title, 40);
}
void wm_raise(int idx) {
    if (idx < 0 || idx >= g_count) return;
    for (int i = 0; i < g_count; i++) g_wnd[i].focused = 0;
    int my_z = g_wnd[idx].z;
    for (int i = 0; i < g_count; i++) {
        if (i == idx) continue;
        if (g_wnd[i].z > my_z) g_wnd[i].z--;
    }
    g_wnd[idx].z = g_count - 1;
    g_wnd[idx].focused = 1;
    g_wnd[idx].visible = 1;
    g_wnd[idx].minimized = 0;
}
wnd_t *wm_focused(void) {
    for (int i = 0; i < g_count; i++)
        if (g_wnd[i].focused && g_wnd[i].visible) return &g_wnd[i];
    return 0;
}
wnd_t *wm_get(int idx) {
    if (idx < 0 || idx >= g_count) return 0;
    return &g_wnd[idx];
}
int wm_find_type(wnd_type_t type) {
    for (int i = 0; i < g_count; i++)
        if (g_wnd[i].type == type) return i;
    return -1;
}
int wm_find_type_visible(wnd_type_t type) {
    for (int i = 0; i < g_count; i++) {
        if (g_wnd[i].type != type) continue;
        if (g_wnd[i].visible || g_wnd[i].minimized) return i;
    }
    return -1;
}
int wm_is_minimized_any(wnd_type_t type) {
    for (int i = 0; i < g_count; i++)
        if (g_wnd[i].type == type && g_wnd[i].minimized && !g_wnd[i].visible)
            return 1;
    return 0;
}
void wm_set_size(int idx, int w, int h) {
    if (idx < 0 || idx >= g_count) return;
    if (g_wnd[idx].maximized) return;
    if (w < MIN_W) w = MIN_W;
    if (h < MIN_H) h = MIN_H;
    int max_w = 1280 - g_wnd[idx].x;
    int max_h = 800  - g_wnd[idx].y - 2;
    if (w > max_w) w = max_w;
    if (h > max_h) h = max_h;
    g_wnd[idx].w = w;
    g_wnd[idx].h = h;
}
void wm_minimize(int idx) {
    if (idx < 0 || idx >= g_count) return;
    g_wnd[idx].visible = 0;
    g_wnd[idx].minimized = 1;
    g_wnd[idx].focused = 0;
}
void wm_maximize_toggle(int idx) {
    if (idx < 0 || idx >= g_count) return;
    wnd_t *w = &g_wnd[idx];
    if (!w->maximized) {
        w->saved_x = w->x; w->saved_y = w->y;
        w->saved_w = w->w; w->saved_h = w->h;
        w->x = 0; w->y = 52;
        w->w = 1280;
        w->h = 800 - 52 - 56 - 12;
        w->maximized = 1;
    } else {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->maximized = 0;
    }
}

// ============================================================
// Мягкая тень окна: 6 слоёв с плавным затуханием.
// Рисуется как расширяющиеся прямоугольники от внутреннего
// (самый тёмный, ближе к окну) к внешнему (самый светлый).
// ============================================================
static void draw_soft_shadow(int x, int y, int w, int h, int focused) {
    // Unfocused окно — тень слабее (визуально «выше в воздухе» focused).
    uint32_t base = focused ? 255 : 200;
    fb_darken_rect(x + 12, y + 12, w, h, 255 - (20 * base) / 255);
    fb_darken_rect(x + 10, y + 10, w, h, 255 - (35 * base) / 255);
    fb_darken_rect(x + 8,  y + 8,  w, h, 255 - (55 * base) / 255);
    fb_darken_rect(x + 6,  y + 6,  w, h, 255 - (80 * base) / 255);
    fb_darken_rect(x + 4,  y + 4,  w, h, 255 - (110 * base) / 255);
    fb_darken_rect(x + 2,  y + 2,  w, h, 255 - (140 * base) / 255);
}

static void draw_window_frame(wnd_t *w) {
    uint32_t bcol = w->focused ? CLR_BORDER : CLR_BORDER_NF;

    draw_soft_shadow(w->x, w->y, w->w, w->h, w->focused);

    fb_fill_round_rect_aa(w->x, w->y, w->w, w->h, RADIUS, bcol);

    int bx = w->x + BORDER_W;
    int by = w->y + BORDER_W;
    int bw = w->w - 2*BORDER_W;
    int bh = w->h - 2*BORDER_W;

    if (w->focused) {
        // Gradient titlebar
        for (int i = 0; i < TITLEBAR_H; i++) {
            uint32_t t = (uint32_t)i * 255 / (TITLEBAR_H - 1);
            uint32_t inv = 255 - t;
            uint32_t r = (((CLR_TITLE_TOP >> 16) & 0xFF) * inv + ((CLR_TITLE_BOT >> 16) & 0xFF) * t) / 255;
            uint32_t g = (((CLR_TITLE_TOP >> 8)  & 0xFF) * inv + ((CLR_TITLE_BOT >> 8)  & 0xFF) * t) / 255;
            uint32_t b = (( CLR_TITLE_TOP        & 0xFF) * inv + ( CLR_TITLE_BOT        & 0xFF) * t) / 255;
            uint32_t c = 0xFF000000u | (r << 16) | (g << 8) | b;
            if (i == 0) { fb_fill_round_rect_top_aa(bx, by, bw, TITLEBAR_H, TITLE_R, c); break; }
        }
        for (int i = TITLE_R; i < TITLEBAR_H; i++) {
            uint32_t t = (uint32_t)i * 255 / (TITLEBAR_H - 1);
            uint32_t inv = 255 - t;
            uint32_t r = (((CLR_TITLE_TOP >> 16) & 0xFF) * inv + ((CLR_TITLE_BOT >> 16) & 0xFF) * t) / 255;
            uint32_t g = (((CLR_TITLE_TOP >> 8)  & 0xFF) * inv + ((CLR_TITLE_BOT >> 8)  & 0xFF) * t) / 255;
            uint32_t b = (( CLR_TITLE_TOP        & 0xFF) * inv + ( CLR_TITLE_BOT        & 0xFF) * t) / 255;
            uint32_t c = 0xFF000000u | (r << 16) | (g << 8) | b;
            fb_fill_rect(bx + TITLE_R, by + i, bw - 2*TITLE_R, 1, c);
        }
        // Верхний блик
        fb_blend_rect(bx + TITLE_R, by, bw - 2*TITLE_R, 1, 0xFFFFFFFF, 50);
    } else {
        fb_fill_round_rect_top_aa(bx, by, bw, TITLEBAR_H, TITLE_R, CLR_TITLE_NF);
    }

    fb_fill_round_rect_bot_aa(bx, by + TITLEBAR_H,
                              bw, bh - TITLEBAR_H,
                              TITLE_R, CLR_BODY);

    fb_draw_string_aa(w->x + 12, w->y + 6, w->title, CLR_TITLE_FG,
                      w->focused ? CLR_TITLE_TOP : CLR_TITLE_NF);

    int by_ = w->y + 10;
    int cx_close = w->x + w->w - 20;
    int cx_min   = cx_close - 16;
    int cx_max   = cx_close - 32;

    fb_fill_circle_aa(cx_close + 5, by_ + 5, 5, 0xFFFF5F57);
    fb_fill_circle_aa(cx_min   + 5, by_ + 5, 5, 0xFFFFBD2E);
    fb_fill_circle_aa(cx_max   + 5, by_ + 5, 5, 0xFF28C840);
}

static int z_order_next(int prev_z, int *out_idx) {
    int best_z = 1000;
    int best_i = -1;
    for (int i = 0; i < g_count; i++) {
        if (!g_wnd[i].visible) continue;
        if (g_wnd[i].z <= prev_z) continue;
        if (g_wnd[i].z < best_z) { best_z = g_wnd[i].z; best_i = i; }
    }
    *out_idx = best_i;
    return best_z;
}

void wm_draw_all(wm_draw_content_fn draw_content) {
    int prev_z = -1, idx = -1;
    for (int k = 0; k < g_count; k++) {
        int z = z_order_next(prev_z, &idx);
        if (idx < 0) return;
        prev_z = z;
        wnd_t *w = &g_wnd[idx];
        if (!w->visible) continue;
        draw_window_frame(w);
        int cx = w->x + 4;
        int cy = w->y + TITLEBAR_H + 4;
        int cw = w->w - 8;
        int ch = w->h - TITLEBAR_H - 8;
        if (draw_content) draw_content(w->type, cx, cy, cw, ch);
        if (!w->maximized) {
            int hx = w->x + w->w - RESIZE_SIZE;
            int hy = w->y + w->h - RESIZE_SIZE;
            uint32_t hcol = w->focused ? CLR_HANDLE : 0xFF6A7A8A;
            for (int i = 0; i < 3; i++) {
                int off = 2 + i * 4;
                for (int j = 0; j < 4; j++)
                    fb_putpixel(hx + RESIZE_SIZE - 1 - off - j,
                                hy + RESIZE_SIZE - 1 - j, hcol);
            }
        }
    }
}

void wm_draw_window_only(int idx, wm_draw_content_fn draw_content) {
    if (idx < 0 || idx >= g_count) return;
    wnd_t *w = &g_wnd[idx];
    if (!w->visible) return;
    draw_window_frame(w);
    int cx = w->x + 4;
    int cy = w->y + TITLEBAR_H + 4;
    int cw = w->w - 8;
    int ch = w->h - TITLEBAR_H - 8;
    if (draw_content) draw_content(w->type, cx, cy, cw, ch);
    if (!w->maximized) {
        int hx = w->x + w->w - RESIZE_SIZE;
        int hy = w->y + w->h - RESIZE_SIZE;
        uint32_t hcol = w->focused ? CLR_HANDLE : 0xFF6A7A8A;
        for (int i = 0; i < 3; i++) {
            int off = 2 + i * 4;
            for (int j = 0; j < 4; j++)
                fb_putpixel(hx + RESIZE_SIZE - 1 - off - j,
                            hy + RESIZE_SIZE - 1 - j, hcol);
        }
    }
}

int wm_hit_test(int x, int y) {
    int best_z = -1, best_i = -1;
    for (int i = 0; i < g_count; i++) {
        wnd_t *w = &g_wnd[i];
        if (!w->visible || w->minimized) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            if (w->z > best_z) { best_z = w->z; best_i = i; }
    }
    return best_i;
}
int wm_hit_title(int x, int y) {
    int i = wm_hit_test(x, y);
    if (i < 0) return -1;
    wnd_t *w = &g_wnd[i];
    if (y >= w->y && y < w->y + TITLEBAR_H) return i;
    return -1;
}
int wm_hit_close(int x, int y) {
    int i = wm_hit_title(x, y);
    if (i < 0) return -1;
    wnd_t *w = &g_wnd[i];
    int bx = w->x + w->w - 20, by = w->y + 10;
    if (x >= bx && x < bx + 10 && y >= by && y < by + 10) return i;
    return -1;
}
int wm_hit_minimize(int x, int y) {
    int i = wm_hit_title(x, y);
    if (i < 0) return -1;
    wnd_t *w = &g_wnd[i];
    int bx = w->x + w->w - 20 - 16, by = w->y + 10;
    if (x >= bx && x < bx + 10 && y >= by && y < by + 10) return i;
    return -1;
}
int wm_hit_maximize(int x, int y) {
    int i = wm_hit_title(x, y);
    if (i < 0) return -1;
    wnd_t *w = &g_wnd[i];
    int bx = w->x + w->w - 20 - 32, by = w->y + 10;
    if (x >= bx && x < bx + 10 && y >= by && y < by + 10) return i;
    return -1;
}
int wm_hit_resize(int x, int y) {
    int best_z = -1, best_i = -1;
    for (int i = 0; i < g_count; i++) {
        wnd_t *w = &g_wnd[i];
        if (!w->visible || w->minimized || w->maximized) continue;
        int rx = w->x + w->w - RESIZE_SIZE;
        int ry = w->y + w->h - RESIZE_SIZE;
        if (x >= rx && x < w->x + w->w && y >= ry && y < w->y + w->h)
            if (w->z > best_z) { best_z = w->z; best_i = i; }
    }
    return best_i;
}
int wm_hit_content(int x, int y) {
    int i = wm_hit_test(x, y);
    if (i < 0) return -1;
    wnd_t *w = &g_wnd[i];
    int cx = w->x + 4;
    int cy = w->y + TITLEBAR_H + 4;
    int cw = w->w - 8;
    int ch = w->h - TITLEBAR_H - 8;
    if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) return i;
    return -1;
}