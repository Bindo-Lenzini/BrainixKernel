// BrainixKernel — desktop.c
// Иконки 48×48, подписи с тенью и AA-фоном.

#include <stdint.h>
#include "desktop.h"
#include "fb.h"
#include "icons.h"

#define FIRST_X   14
#define FIRST_Y   68
#define ICON_CELL 90
#define GAP_Y     4

static int g_selected = -1;

static const char *g_labels[DI_COUNT] = {
    "Computer",
    "Recycle",
    "Terminal",
    "Files",
    "Editor",
    "Personalize",
};

static int icon_y(int idx) { return FIRST_Y + idx * (ICON_CELL + GAP_Y); }

int desktop_hit(int x, int y) {
    for (int i = 0; i < DI_COUNT; i++) {
        int ix = FIRST_X;
        int iy = icon_y(i);
        if (x >= ix && x < ix + ICON_CELL &&
            y >= iy && y < iy + ICON_CELL) return i;
    }
    return -1;
}

void desktop_select(int idx) { g_selected = idx; }
int  desktop_selected(void) { return g_selected; }
void desktop_clear_selection(void) { g_selected = -1; }

const char *desktop_label(int idx) {
    if (idx < 0 || idx >= DI_COUNT) return "";
    return g_labels[idx];
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void draw_icon_at(int idx, int ix, int iy) {
    int sel = (idx == g_selected);

    // Подсветка ячейки при выборе — мягкая AA-плашка
    if (sel) {
        fb_blend_round_rect_aa(ix + 4, iy + 2, ICON_CELL - 8, ICON_CELL - 4,
                               12, 0xFF4A90E2, 70);
        // Тонкая рамка
        fb_blend_round_rect_aa(ix + 4, iy + 2, ICON_CELL - 8, ICON_CELL - 4,
                               12, 0xFF4A90E2, 180);
        fb_blend_round_rect_aa(ix + 5, iy + 3, ICON_CELL - 10, ICON_CELL - 6,
                               11, 0x00000000, 0);  // no-op, оставляем
    }

    // Иконка 48×48 по центру ячейки
    int icon_x = ix + (ICON_CELL - ICON_SIZE) / 2;
    int icon_y_ = iy + 6;

    switch (idx) {
        case DI_COMPUTER:    icon_draw_computer  (icon_x, icon_y_, sel); break;
        case DI_RECYCLE:     icon_draw_recycle   (icon_x, icon_y_, sel); break;
        case DI_TERMINAL:    icon_draw_ksh       (icon_x, icon_y_, sel); break;
        case DI_FILES:       icon_draw_files     (icon_x, icon_y_, sel); break;
        case DI_EDITOR:      icon_draw_editor    (icon_x, icon_y_, sel); break;
        case DI_PERSONALIZE: icon_draw_settings  (icon_x, icon_y_, sel); break;
    }

    // Подпись с тенью + AA-фон
    const char *lbl = g_labels[idx];
    int lw = str_len(lbl) * 8;
    int lx = ix + (ICON_CELL - lw) / 2;
    int ly = iy + ICON_CELL - 22;

    // Тёмный полупрозрачный пилюль-фон
    fb_blend_round_rect_aa(lx - 6, ly - 4, lw + 12, 22, 8,
                           sel ? 0xFF4A90E2 : 0xFF000000,
                           sel ? 200 : 120);

    // Тень текста (смещённая)
    fb_draw_string_aa(lx + 1, ly + 1, lbl, 0xFF000000, 0);
    fb_draw_string_aa(lx, ly, lbl, 0xFFFFFFFF, 0);
}

void desktop_draw(void) {
    for (int i = 0; i < DI_COUNT; i++) {
        draw_icon_at(i, FIRST_X, icon_y(i));
    }
}