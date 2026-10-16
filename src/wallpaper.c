// BrainixKernel (BKOS) — wallpaper.c

#include <stdint.h>
#include "wallpaper.h"
#include "fb.h"
#include "png.h"
#include "initrd.h"
#include "media.h"
#include "physmem.h"
#include "backdrop.h"

typedef struct {
    const char *name;
    uint32_t    top;
    uint32_t    bot;
} wp_preset_t;

static const wp_preset_t g_presets[] = {
    { "default", 0xFF2A4A75, 0xFF12243E },
    { "night",   0xFF1A1A3A, 0xFF05050F },
    { "forest",  0xFF1F3A22, 0xFF08160A },
    { "sunset",  0xFF6F3A1F, 0xFF2A0A05 },
    { "ocean",   0xFF1A4568, 0xFF061828 },
    { "purple",  0xFF3A1A55, 0xFF12041E },
    { "steel",   0xFF3A3A42, 0xFF14141A },
    { "void",    0xFF0A0A0A, 0xFF000000 },
};

#define WP_COUNT ((int)(sizeof(g_presets) / sizeof(g_presets[0])))

static int      g_current         = 0;
static int      g_custom          = 0;
static image_t  g_image           = {0};
static char     g_custom_name[64] = {0};
static char     g_last_reason[100] = {0};

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == 0 && *b == 0;
}
static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int has_image_ext(const char *name) {
    int len = str_len(name);
    if (len < 4) return 0;
    const char *ext4 = name + len - 4;
    if (str_eq(ext4, ".png") || str_eq(ext4, ".PNG")) return 1;
    if (str_eq(ext4, ".bmp") || str_eq(ext4, ".BMP")) return 1;
    if (str_eq(ext4, ".jpg") || str_eq(ext4, ".JPG")) return 1;
    if (str_eq(ext4, ".jpe") || str_eq(ext4, ".JPE")) return 1;
    if (len >= 5) {
        const char *ext5 = name + len - 5;
        if (str_eq(ext5, ".jpeg") || str_eq(ext5, ".JPEG")) return 1;
    }
    return 0;
}

int wallpaper_count(void) { return WP_COUNT; }
const char *wallpaper_name(int idx) {
    if (idx < 0 || idx >= WP_COUNT) return 0;
    return g_presets[idx].name;
}
const char *wallpaper_current_name(void) {
    if (g_custom && g_custom_name[0]) return g_custom_name;
    return g_presets[g_current].name;
}
void wallpaper_preset_colors(int idx, uint32_t *out_top, uint32_t *out_bot) {
    if (idx < 0 || idx >= WP_COUNT) {
        if (out_top) *out_top = 0xFF4A90E2;
        if (out_bot) *out_bot = 0xFF2A4A75;
        return;
    }
    if (out_top) *out_top = g_presets[idx].top;
    if (out_bot) *out_bot = g_presets[idx].bot;
}
const char *wallpaper_last_reason(void) { return g_last_reason; }

static void set_reason(const char *s) {
    int i = 0;
    while (s[i] && i < 99) { g_last_reason[i] = s[i]; i++; }
    g_last_reason[i] = 0;
}

static void adopt_image(image_t *img, const char *name) {
    if (g_custom && g_image.ok) image_free(&g_image);
    g_image = *img;
    g_custom = 1;
    str_copy_n(g_custom_name, name, sizeof(g_custom_name));
    g_last_reason[0] = 0;
    backdrop_invalidate();
}

static int try_media_wallpaper(const char *name) {
    if (!media_is_ready()) { set_reason("media disk not attached"); return 0; }
    int sz = media_size(name);
    if (sz <= 0) { set_reason("file not found on media disk"); return 0; }
    uint64_t pages = ((uint64_t)sz + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *tmp = (uint8_t *)phys_alloc_pages(pages);
    if (!tmp) { set_reason("out of memory"); return 0; }
    int got = media_read(name, tmp, (uint32_t)sz);
    if (got <= 0) {
        for (uint64_t i = 0; i < pages; i++) phys_free_page(tmp + i * PAGE_SIZE);
        set_reason("media read failed"); return 0;
    }
    image_t img;
    int ok = image_decode(tmp, (uint32_t)got, &img);
    for (uint64_t i = 0; i < pages; i++) phys_free_page(tmp + i * PAGE_SIZE);
    if (!ok) { set_reason(png_last_error()); return 0; }
    adopt_image(&img, name);
    return 1;
}

int wallpaper_set_by_name(const char *name) {
    if (!name || !name[0]) { set_reason("empty name"); return 0; }
    for (int i = 0; i < WP_COUNT; i++) {
        if (str_eq(g_presets[i].name, name)) {
            if (g_custom && g_image.ok) image_free(&g_image);
            g_custom = 0;
            g_custom_name[0] = 0;
            g_current = i;
            g_last_reason[0] = 0;
            backdrop_invalidate();
            return 1;
        }
    }
    if (has_image_ext(name)) {
        const initrd_file_t *f = initrd_find(name);
        if (f) {
            image_t img;
            if (image_decode(f->data, f->size, &img)) {
                adopt_image(&img, name);
                return 1;
            }
            set_reason(png_last_error());
            return 0;
        }
        if (try_media_wallpaper(name)) return 1;
        return 0;
    }
    set_reason("unknown preset");
    return 0;
}

void wallpaper_set_by_index(int idx) {
    if (idx < 0 || idx >= WP_COUNT) return;
    if (g_custom && g_image.ok) image_free(&g_image);
    g_custom = 0;
    g_custom_name[0] = 0;
    g_current = idx;
    g_last_reason[0] = 0;
    backdrop_invalidate();
}

static void draw_image_cover(const image_t *img) {
    uint32_t sw = img->width, sh = img->height;
    if (sw == 0 || sh == 0 || !img->pixels) return;
    uint32_t dw = fb_width(), dh = fb_height();
    if (dw == 0 || dh == 0) return;
    uint64_t lhs = (uint64_t)dw * sh, rhs = (uint64_t)dh * sw;
    uint32_t scaled_w, scaled_h;
    if (lhs >= rhs) {
        scaled_w = dw;
        scaled_h = (uint32_t)(((uint64_t)sh * dw) / sw);
    } else {
        scaled_h = dh;
        scaled_w = (uint32_t)(((uint64_t)sw * dh) / sh);
    }
    if (scaled_w == 0) scaled_w = 1;
    if (scaled_h == 0) scaled_h = 1;
    int32_t off_x = ((int32_t)scaled_w - (int32_t)dw) / 2;
    int32_t off_y = ((int32_t)scaled_h - (int32_t)dh) / 2;
    for (uint32_t y = 0; y < dh; y++) {
        int32_t sy = (int32_t)y + off_y;
        if (sy < 0) sy = 0;
        if ((uint32_t)sy >= scaled_h) sy = (int32_t)scaled_h - 1;
        uint32_t src_y = (uint32_t)sy * sh / scaled_h;
        if (src_y >= sh) src_y = sh - 1;
        const uint32_t *src_row = img->pixels + (uint64_t)src_y * sw;
        for (uint32_t x = 0; x < dw; x++) {
            int32_t sx = (int32_t)x + off_x;
            if (sx < 0) sx = 0;
            if ((uint32_t)sx >= scaled_w) sx = (int32_t)scaled_w - 1;
            uint32_t src_x = (uint32_t)sx * sw / scaled_w;
            if (src_x >= sw) src_x = sw - 1;
            fb_putpixel((int)x, (int)y, src_row[src_x] | 0xFF000000u);
        }
    }
}

void wallpaper_draw(void) {
    if (g_custom && g_image.ok) {
        draw_image_cover(&g_image);
        return;
    }
    const wp_preset_t *p = &g_presets[g_current];
    fb_fill_gradient_v(0, 0, (int)fb_width(), (int)fb_height(), p->top, p->bot);
}