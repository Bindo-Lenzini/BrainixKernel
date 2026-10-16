// BrainixKernel (BKOS) — startmenu.c
// Меню «Пуск» с живыми кнопками: 3D-иконки, hover, подписи, стрелки.

#include <stdint.h>
#include "startmenu.h"
#include "fb.h"

extern void uart_puts(const char *s);

#define MENU_W 240
#define ITEM_H 48
#define HEADER_H 28
#define MENU_H (HEADER_H + STARTMENU_ITEM_COUNT * ITEM_H + 6)

#define MENU_X 8
#define MENU_Y 552       // над иконкой B в доке

static int g_open  = 0;
static int g_hover = -1;

static const char *g_labels[STARTMENU_ITEM_COUNT] = {
    "Shutdown",
    "Reboot",
    "Sleep",
    "Hibernate",
};

static const char *g_sub[STARTMENU_ITEM_COUNT] = {
    "power off",
    "restart",
    "low power",
    "to disk",
};

static const uint32_t g_icon_top[STARTMENU_ITEM_COUNT] = {
    0xFFFF7A72,   // яркая верхушка
    0xFFFFD050,
    0xFF6AB0FF,
    0xFFB878D8,
};

static const uint32_t g_icon_bot[STARTMENU_ITEM_COUNT] = {
    0xFFB03030,   // тёмный низ
    0xFFB08020,
    0xFF2A6088,
    0xFF5A2A70,
};

void startmenu_toggle(void) { g_open = !g_open; g_hover = -1; }
void startmenu_close(void)  { g_open = 0; g_hover = -1; }
int  startmenu_is_open(void) { return g_open; }

int startmenu_hit(int x, int y) {
    if (!g_open) return -1;
    if (x < MENU_X || x >= MENU_X + MENU_W) return -1;
    int top = MENU_Y + HEADER_H;
    int bot = top + STARTMENU_ITEM_COUNT * ITEM_H;
    if (y < top || y >= bot) return -1;
    int idx = (y - top) / ITEM_H;
    if (idx < 0 || idx >= STARTMENU_ITEM_COUNT) return -1;
    return idx;
}

// Кружок с 3D-эффектом: верхняя половина светлее, нижняя темнее.
static void draw_circle_3d(int cx, int cy, int r, uint32_t top, uint32_t bot) {
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            int d2 = i*i + j*j;
            if (d2 > r*r) continue;
            uint32_t col = (j < 0) ? top : bot;
            fb_putpixel(cx + i, cy + j, col);
        }
    }
    // Чёрный контур по краю.
    for (int j = -r - 1; j <= r + 1; j++) {
        for (int i = -r - 1; i <= r + 1; i++) {
            int inside  = (i*i + j*j) <= r*r;
            int inside2 = ((i+1)*(i+1) + j*j) <= r*r &&
                          ((i-1)*(i-1) + j*j) <= r*r &&
                          (i*i + (j+1)*(j+1)) <= r*r &&
                          (i*i + (j-1)*(j-1)) <= r*r;
            if (inside2 && !inside) {
                fb_putpixel(cx + i, cy + j, 0xFF101010);
            }
        }
    }
    // Блик.
    fb_putpixel(cx - r/2, cy - r/2, 0xFFFFFFFF);
    fb_putpixel(cx - r/2 + 1, cy - r/2, 0xFFFFFFFF);
    fb_putpixel(cx - r/2, cy - r/2 + 1, 0xFFEEEEEE);
}

// Стрелка ">" справа.
static void draw_arrow(int x, int y, uint32_t col) {
    for (int i = 0; i < 5; i++) {
        fb_putpixel(x + i, y - i, col);
        fb_putpixel(x + i, y + i, col);
    }
    fb_fill_rect(x, y, 5, 1, col);
}

void startmenu_draw(int mx, int my) {
    if (!g_open) return;

    // Тень.
    fb_fill_rect(MENU_X + 4, MENU_Y + 4, MENU_W, MENU_H, 0x40000000);
    fb_fill_rect(MENU_X + 2, MENU_Y + 2, MENU_W, MENU_H, 0x80000000);

    // Тело.
    fb_fill_rect(MENU_X, MENU_Y, MENU_W, MENU_H, 0xFF142844);

    // Обводка.
    fb_fill_rect(MENU_X, MENU_Y, MENU_W, 1, 0xFF4A90E2);
    fb_fill_rect(MENU_X, MENU_Y + MENU_H - 1, MENU_W, 1, 0xFF4A90E2);
    fb_fill_rect(MENU_X, MENU_Y, 1, MENU_H, 0xFF4A90E2);
    fb_fill_rect(MENU_X + MENU_W - 1, MENU_Y, 1, MENU_H, 0xFF4A90E2);

    // Заголовок.
    fb_fill_rect(MENU_X + 1, MENU_Y + 1, MENU_W - 2, HEADER_H, 0xFF2C4A73);
    fb_draw_string(MENU_X + 12, MENU_Y + 8, "Brainix", 0xFFF0F0F0, 0xFF2C4A73);
    fb_draw_string(MENU_X + 76, MENU_Y + 8, "Menu", 0xFF7AB8FF, 0xFF2C4A73);
    fb_fill_rect(MENU_X + 1, MENU_Y + HEADER_H - 1, MENU_W - 2, 1, 0xFF4A90E2);

    // Hover.
    g_hover = startmenu_hit(mx, my);

    for (int i = 0; i < STARTMENU_ITEM_COUNT; i++) {
        int iy = MENU_Y + HEADER_H + i * ITEM_H;

        if (i == STARTMENU_ITEM_SLEEP) {
            fb_fill_rect(MENU_X + 12, iy, MENU_W - 24, 1, 0xFF0D1F33);
        }

        int is_hov = (i == g_hover);
        uint32_t bg;
        if (is_hov) {
            bg = 0xFF3A5A80;
            fb_fill_rect(MENU_X + 1, iy, MENU_W - 2, ITEM_H, bg);
            fb_fill_rect(MENU_X + 1, iy, 3, ITEM_H, 0xFF4A90E2);
        } else {
            bg = 0xFF142844;
        }

        // 3D-иконка.
        int ic_x = MENU_X + 28;
        int ic_y = iy + ITEM_H / 2;
        draw_circle_3d(ic_x, ic_y, 9, g_icon_top[i], g_icon_bot[i]);

        // Основной текст.
        uint32_t fg = is_hov ? 0xFFFFFFFF : 0xFFD8D8D8;
        fb_draw_string(MENU_X + 52, iy + 10, g_labels[i], fg, bg);

        // Мелкая подпись ниже.
        uint32_t fg2 = is_hov ? 0xFFD0E5FF : 0xFF7A8A9A;
        fb_draw_string(MENU_X + 52, iy + 26, g_sub[i], fg2, bg);

        // Стрелка при hover.
        if (is_hov) {
            draw_arrow(MENU_X + MENU_W - 24, iy + ITEM_H / 2, 0xFF7AB8FF);
        }
    }
}

// ---- Действия ----

void system_shutdown(void) {
    uart_puts("[power] shutdown requested\n");
    register uint64_t x0 __asm__("x0") = 0x84000008;
    __asm__ volatile("hvc #0" : "+r"(x0));
    for (;;) __asm__ volatile("wfe");
}

void system_reboot(void) {
    uart_puts("[power] reboot requested\n");
    register uint64_t x0 __asm__("x0") = 0x84000009;
    __asm__ volatile("hvc #0" : "+r"(x0));
    for (;;) __asm__ volatile("wfe");
}