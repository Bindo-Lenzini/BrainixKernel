// BrainixKernel — personalize.c
// Окно персонализации: список обоев (пресеты + initrd + медиа),
// большое превью выбранного, кнопка Apply.
//
// Layout:
//   +-----------------+--------------------------------+
//   | Presets         |                                |
//   |  default        |  Большое превью выбранного     |
//   |  night          |  (cover, 500×~400)             |
//   |  ...            |                                |
//   | Images (initrd) |                                |
//   |  sunset.png     |                                |
//   | Media disk      |                                |
//   |  linux2.jpeg    |                                |
//   +-----------------+--------------------------------+
//   | current: default                  [Set as wallpaper] |
//   +-----------------------------------------------------+

#include <stdint.h>
#include "personalize.h"
#include "fb.h"
#include "wallpaper.h"
#include "initrd.h"
#include "media.h"
#include "physmem.h"

#define CLR_BODY      0xFF101C2E
#define CLR_FG        0xFFE0E0E0
#define CLR_FG_DIM    0xFF9DA9B8
#define CLR_LIST_BG   0xFF0B1524
#define CLR_SELECT    0xFF4A90E2
#define CLR_SELECT_FG 0xFFFFFFFF
#define CLR_STATUS    0xFF2C4A73
#define CLR_STATUS_FG 0xFFF0F0F0
#define CLR_OK        0xFF6EE7A0
#define CLR_ERR       0xFFFF6B6B
#define CLR_PREVIEW_BG 0xFF05080F
#define CLR_PREVIEW_BORDER 0xFF2A4A75
#define CLR_BTN       0xFF4A90E2
#define CLR_BTN_HOVER 0xFF5FA8F0
#define CLR_BTN_TEXT  0xFFFFFFFF

#define LIST_W     220
#define STATUS_H   32
#define ROW_H      22

// ---- Строковые хелперы ----
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == 0 && *b == 0;
}
static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int is_image_ext(const char *name) {
    int len = str_len(name);
    if (len < 4) return 0;
    const char *e4 = name + len - 4;
    if (str_eq(e4, ".png") || str_eq(e4, ".PNG")) return 1;
    if (str_eq(e4, ".bmp") || str_eq(e4, ".BMP")) return 1;
    if (str_eq(e4, ".jpg") || str_eq(e4, ".JPG")) return 1;
    if (str_eq(e4, ".jpe") || str_eq(e4, ".JPE")) return 1;
    if (len >= 5) {
        const char *e5 = name + len - 5;
        if (str_eq(e5, ".jpeg") || str_eq(e5, ".JPEG")) return 1;
    }
    return 0;
}

// ============================================================
// Сборка списка
// ============================================================

static void add_item(pers_t *p, const char *name, pers_src_t src, int preset_idx) {
    if (p->count >= PERS_MAX_ITEMS) return;
    pers_item_t *it = &p->items[p->count];
    str_copy_n(it->name, name, 63);
    it->src = src;
    it->preset_idx = preset_idx;
    p->count++;
}

void pers_init(pers_t *p) {
    p->count = 0;
    p->selected = 0;
    p->scroll = 0;
    p->status_msg = 0;
    p->cursor_visible = 1;
    p->preview_for = -1;
    p->preview_is_gradient = 0;
    p->preview.ok = 0;
    p->preview.pixels = 0;
    p->preview.width = 0;
    p->preview.height = 0;

    // 1. Пресеты
    int np = wallpaper_count();
    for (int i = 0; i < np; i++) {
        add_item(p, wallpaper_name(i), PERS_PRESET, i);
    }

    // 2. Initrd картинки
    int nf = initrd_count();
    for (int i = 0; i < nf; i++) {
        const initrd_file_t *f = initrd_get(i);
        if (!f) continue;
        if (!is_image_ext(f->name)) continue;
        add_item(p, f->name, PERS_INITRD, -1);
    }

    // 3. Медиа картинки
    if (media_is_ready()) {
        int nm = media_count();
        for (int i = 0; i < nm; i++) {
            char mname[MEDIA_MAX_NAME];
            uint32_t msize;
            if (!media_list(i, mname, sizeof(mname), &msize)) continue;
            if (!is_image_ext(mname)) continue;
            add_item(p, mname, PERS_MEDIA, -1);
        }
    }
}

void pers_close(pers_t *p) {
    if (p->preview.ok && p->preview.pixels) {
        image_free(&p->preview);
    }
    p->preview.ok = 0;
    p->preview.pixels = 0;
    p->preview_for = -1;
    p->preview_is_gradient = 0;
}

// ============================================================
// Загрузка превью
// ============================================================

static void load_preview(pers_t *p) {
    // Уже загружено?
    if (p->preview_for == p->selected) return;

    // Освободить предыдущее
    if (p->preview.ok && p->preview.pixels) image_free(&p->preview);
    p->preview.ok = 0;
    p->preview.pixels = 0;
    p->preview_is_gradient = 0;

    if (p->selected < 0 || p->selected >= p->count) {
        p->preview_for = -1;
        return;
    }

    pers_item_t *it = &p->items[p->selected];

    if (it->src == PERS_PRESET) {
        p->preview_is_gradient = 1;
        p->preview_for = p->selected;
        return;
    }

    if (it->src == PERS_INITRD) {
        const initrd_file_t *f = initrd_find(it->name);
        if (!f) { p->preview_for = -1; return; }
        image_t img;
        if (!image_decode(f->data, f->size, &img)) {
            p->preview_for = -1;
            return;
        }
        p->preview = img;
        p->preview_for = p->selected;
        return;
    }

    if (it->src == PERS_MEDIA) {
        int sz = media_size(it->name);
        if (sz <= 0) { p->preview_for = -1; return; }

        uint64_t pages = ((uint64_t)sz + PAGE_SIZE - 1) / PAGE_SIZE;
        uint8_t *tmp = (uint8_t *)phys_alloc_pages(pages);
        if (!tmp) { p->preview_for = -1; return; }

        int got = media_read(it->name, tmp, (uint32_t)sz);
        if (got <= 0) {
            for (uint64_t i = 0; i < pages; i++)
                phys_free_page(tmp + i * PAGE_SIZE);
            p->preview_for = -1;
            return;
        }

        image_t img;
        int ok = image_decode(tmp, (uint32_t)got, &img);
        for (uint64_t i = 0; i < pages; i++)
            phys_free_page(tmp + i * PAGE_SIZE);

        if (!ok) { p->preview_for = -1; return; }
        p->preview = img;
        p->preview_for = p->selected;
    }
}

// ============================================================
// Применить
// ============================================================

static void pers_apply(pers_t *p) {
    if (p->selected < 0 || p->selected >= p->count) {
        p->status_msg = 2;
        return;
    }
    pers_item_t *it = &p->items[p->selected];
    if (wallpaper_set_by_name(it->name)) {
        p->status_msg = 1;
    } else {
        p->status_msg = 2;
    }
}

// ============================================================
// Отрисовка
// ============================================================

// Иконка типа для пресета/initrd/media.
static void draw_type_icon(int x, int y, pers_src_t src, int preset_idx) {
    if (src == PERS_PRESET) {
        uint32_t top = 0xFF4A90E2, bot = 0xFF2A4A75;
        wallpaper_preset_colors(preset_idx, &top, &bot);
        // Мини-градиент 12×12
        for (int j = 0; j < 12; j++) {
            uint32_t t = (uint32_t)j * 255 / 11;
            uint32_t inv = 255 - t;
            uint32_t r = (((top >> 16) & 0xFF) * inv + ((bot >> 16) & 0xFF) * t) / 255;
            uint32_t g = (((top >> 8)  & 0xFF) * inv + ((bot >> 8)  & 0xFF) * t) / 255;
            uint32_t b = (( top        & 0xFF) * inv + ( bot        & 0xFF) * t) / 255;
            uint32_t c = 0xFF000000u | (r << 16) | (g << 8) | b;
            fb_fill_rect(x, y + j, 12, 1, c);
        }
    } else if (src == PERS_INITRD) {
        // Зелёная "P"
        fb_fill_rect(x, y, 12, 12, 0xFF1A4A22);
        fb_draw_char(x + 2, y + 2, 'P', 0xFFB0FFB0, 0xFF1A4A22);
    } else if (src == PERS_MEDIA) {
        // Жёлтый диск
        fb_fill_rect(x, y, 12, 12, 0xFF4A3A0A);
        fb_draw_char(x + 2, y + 2, 'M', 0xFFFFE080, 0xFF4A3A0A);
    }
}

static void draw_list(pers_t *p, int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, CLR_LIST_BG);
    fb_fill_rect(x + w - 1, y, 1, h, 0xFF0A1018);

    int visible_rows = h / ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    // Скролл — не даём уехать в пустоту
    if (p->scroll > p->count - visible_rows) {
        p->scroll = p->count - visible_rows;
    }
    if (p->scroll < 0) p->scroll = 0;

    for (int i = 0; i < visible_rows; i++) {
        int idx = p->scroll + i;
        if (idx >= p->count) break;

        int ry = y + i * ROW_H;
        int is_sel = (idx == p->selected);

        if (is_sel) {
            fb_fill_rect(x + 2, ry, w - 4, ROW_H, CLR_SELECT);
        }

        pers_item_t *it = &p->items[idx];
        uint32_t fg = is_sel ? CLR_SELECT_FG : CLR_FG;

        draw_type_icon(x + 6, ry + 5, it->src, it->preset_idx);

        // Имя, обрезаем до ширины
        int max_chars = (w - 26) / 8;
        char buf[64];
        int n = 0;
        while (it->name[n] && n < max_chars && n < 63) {
            buf[n] = it->name[n];
            n++;
        }
        buf[n] = 0;
        fb_draw_string(x + 22, ry + 7, buf, fg, is_sel ? CLR_SELECT : CLR_LIST_BG);
    }

    // Скроллбар справа (если нужен)
    if (p->count > visible_rows) {
        int bar_x = x + w - 4;
        int bar_h = h;
        int thumb_h = (bar_h * visible_rows) / p->count;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = y + (bar_h - thumb_h) * p->scroll / (p->count - visible_rows);
        fb_fill_rect(bar_x, y, 3, bar_h, 0xFF0A1018);
        fb_fill_rect(bar_x, thumb_y, 3, thumb_h, 0xFF4A90E2);
    }
}

// "Cover" вписывание: заполняем прямоугольник с сохранением пропорций,
// излишек обрезается по центру.
static void blit_cover(int x, int y, int w, int h, const image_t *img) {
    if (!img->ok || !img->pixels || img->width == 0 || img->height == 0) return;

    uint64_t lhs = (uint64_t)w * img->height;
    uint64_t rhs = (uint64_t)h * img->width;

    int scaled_w, scaled_h;
    if (lhs >= rhs) {
        scaled_w = w;
        scaled_h = (int)(((uint64_t)img->height * w) / img->width);
    } else {
        scaled_h = h;
        scaled_w = (int)(((uint64_t)img->width * h) / img->height);
    }
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;

    int off_x = x + (w - scaled_w) / 2;
    int off_y = y + (h - scaled_h) / 2;

    // Рисуем с масштабированием, но с обрезкой по границам [x..x+w, y..y+h]
    for (int j = 0; j < h; j++) {
        int py = y + j;
        int sy = (j - (off_y - y)) * img->height / scaled_h;
        if (sy < 0 || sy >= (int)img->height) continue;
        const uint32_t *src_row = img->pixels + (uint64_t)sy * img->width;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            int sx = (i - (off_x - x)) * img->width / scaled_w;
            if (sx < 0 || sx >= (int)img->width) continue;
            fb_putpixel(px, py, src_row[sx] | 0xFF000000u);
        }
    }
}

static void draw_preview_area(pers_t *p, int x, int y, int w, int h) {
    // Фон + рамка
    fb_fill_rect(x, y, w, h, CLR_PREVIEW_BG);
    // Рамка 2 px
    fb_fill_rect(x, y, w, 2, CLR_PREVIEW_BORDER);
    fb_fill_rect(x, y + h - 2, w, 2, CLR_PREVIEW_BORDER);
    fb_fill_rect(x, y, 2, h, CLR_PREVIEW_BORDER);
    fb_fill_rect(x + w - 2, y, 2, h, CLR_PREVIEW_BORDER);

    if (p->selected < 0 || p->selected >= p->count) {
        fb_draw_string(x + 12, y + 12, "no selection", CLR_FG_DIM, CLR_PREVIEW_BG);
        return;
    }

    pers_item_t *it = &p->items[p->selected];

    int inner_x = x + 4;
    int inner_y = y + 4;
    int inner_w = w - 8;
    int inner_h = h - 8;

    if (it->src == PERS_PRESET) {
        uint32_t top = 0xFF4A90E2, bot = 0xFF2A4A75;
        wallpaper_preset_colors(it->preset_idx, &top, &bot);
        fb_fill_gradient_v(inner_x, inner_y, inner_w, inner_h, top, bot);

        // Имя пресета внизу
        int nl = str_len(it->name);
        int nx = inner_x + (inner_w - nl * 8) / 2;
        int ny = inner_y + inner_h - 22;
        fb_fill_rect(nx - 6, ny - 4, nl * 8 + 12, 20, 0x80000000);
        fb_draw_string_aa(nx, ny, it->name, 0xFFFFFFFF, 0);
        return;
    }

    if (p->preview_for != p->selected || !p->preview.ok) {
        // Превью не загружено
        fb_draw_string(x + 12, y + 12, "loading preview...", CLR_FG_DIM, CLR_PREVIEW_BG);
        // Всё равно покажем имя
        fb_draw_string(x + 12, y + 28, it->name, CLR_FG, CLR_PREVIEW_BG);
        return;
    }

    blit_cover(inner_x, inner_y, inner_w, inner_h, &p->preview);

    // Имя файла внизу полупрозрачно
    int nl = str_len(it->name);
    int nx = inner_x + (inner_w - nl * 8) / 2;
    int ny = inner_y + inner_h - 22;
    fb_fill_rect(nx - 6, ny - 4, nl * 8 + 12, 20, 0x80000000);
    fb_draw_string_aa(nx, ny, it->name, 0xFFFFFFFF, 0);
}

static void draw_status(pers_t *p, int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, CLR_STATUS);

    // "current: <name>"
    char buf[96];
    int i = 0;
    const char *pre = "current: ";
    for (int j = 0; pre[j]; j++) buf[i++] = pre[j];
    const char *cur = wallpaper_current_name();
    for (int j = 0; cur && cur[j] && i < 80; j++) buf[i++] = cur[j];
    buf[i] = 0;
    fb_draw_string(x + 8, y + 9, buf, CLR_STATUS_FG, CLR_STATUS);

    // Кнопка "Set as wallpaper"
    int btn_w = 190;
    int btn_h = 22;
    int bx = x + w - btn_w - 8;
    int by = y + (h - btn_h) / 2;
    fb_fill_round_rect_aa(bx, by, btn_w, btn_h, 6, CLR_BTN);
    const char *label = "Set as wallpaper";
    int lw = str_len(label) * 8;
    fb_draw_string(bx + (btn_w - lw) / 2, by + 6, label, CLR_BTN_TEXT, CLR_BTN);

    // Статус справа от кнопки (перед ней)
    if (p->status_msg == 1) {
        fb_draw_string(bx - 80, y + 9, "applied", CLR_OK, CLR_STATUS);
    } else if (p->status_msg == 2) {
        fb_draw_string(bx - 70, y + 9, "error", CLR_ERR, CLR_STATUS);
    }
}

void pers_draw_content(pers_t *p, int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, CLR_BODY);

    int list_h = ch - STATUS_H;
    if (list_h < 40) list_h = 40;

    draw_list(p, cx, cy, LIST_W, list_h);
    draw_preview_area(p, cx + LIST_W + 4, cy + 4,
                      cw - LIST_W - 8, list_h - 8);
    draw_status(p, cx, cy + ch - STATUS_H, cw, STATUS_H);
}

// ============================================================
// Ввод
// ============================================================

void pers_key(pers_t *p, char c) {
    if (p->count == 0) return;

    if (c == 0x11) {   // UP
        if (p->selected > 0) {
            p->selected--;
            p->status_msg = 0;
            load_preview(p);
        }
        return;
    }
    if (c == 0x12) {   // DOWN
        if (p->selected < p->count - 1) {
            p->selected++;
            p->status_msg = 0;
            load_preview(p);
        }
        return;
    }
    if (c == 0x15) {   // HOME
        p->selected = 0;
        p->status_msg = 0;
        load_preview(p);
        return;
    }
    if (c == 0x16) {   // END
        p->selected = p->count - 1;
        p->status_msg = 0;
        load_preview(p);
        return;
    }
    if (c == '\n') {
        pers_apply(p);
        return;
    }
}

int pers_click(pers_t *p, int mx, int my, int cx, int cy, int cw, int ch) {
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return 0;

    int list_h = ch - STATUS_H;
    if (list_h < 40) list_h = 40;

    // Клик по списку
    if (mx < cx + LIST_W && my < cy + list_h) {
        int row = (my - cy) / ROW_H;
        int idx = p->scroll + row;
        if (idx >= 0 && idx < p->count) {
            if (p->selected == idx) {
                pers_apply(p);
            } else {
                p->selected = idx;
                p->status_msg = 0;
                load_preview(p);
            }
            return 1;
        }
        return 0;
    }

    // Клик по кнопке "Set as wallpaper"
    int status_y = cy + ch - STATUS_H;
    if (my >= status_y && my < cy + ch) {
        int btn_w = 190;
        int btn_h = 22;
        int bx = cx + cw - btn_w - 8;
        int by = status_y + (STATUS_H - btn_h) / 2;
        if (mx >= bx && mx < bx + btn_w && my >= by && my < by + btn_h) {
            pers_apply(p);
            return 1;
        }
    }

    return 0;
}