// BrainixKernel — icons.c
// macOS Big Sur-style: squircle base + solid color + white glyph.

#include <stdint.h>
#include "icons.h"
#include "fb.h"

// ============================================================
// Базовая squircle-подложка
// ============================================================
static void bg_squircle(int x, int y, uint32_t top, uint32_t bot) {
    // Мягкая тень снизу-справа
    fb_blend_round_rect_aa(x + 2, y + 3, 48, 48, 12, 0xFF000000, 55);
    fb_blend_round_rect_aa(x + 1, y + 2, 48, 48, 12, 0xFF000000, 40);

    // Градиентный squircle
    fb_fill_round_rect_v_grad_aa(x, y, 48, 48, 12, top, bot);

    // Тонкий верхний блик (едва заметный)
    fb_blend_round_rect_aa(x + 2, y + 1, 44, 3, 10, 0xFFFFFFFF, 60);
}

// ============================================================
// Computer — синий, белый монитор
// ============================================================
void icon_draw_computer(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFF6EB4FF, 0xFF2A6BC8);

    // Монитор — белый корпус
    fb_fill_round_rect_aa(x + 9, y + 12, 30, 22, 3, 0xFFFFFFFF);
    // Экран внутри — тёмно-синий
    fb_fill_round_rect_aa(x + 12, y + 15, 24, 16, 2, 0xFF2A4A78);
    // Подставка
    fb_fill_round_rect_aa(x + 21, y + 34, 6, 3, 1, 0xFFFFFFFF);
    fb_fill_round_rect_aa(x + 15, y + 37, 18, 3, 1, 0xFFFFFFFF);
}

// ============================================================
// Recycle — серебро, белая корзина
// ============================================================
void icon_draw_recycle(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFFD0D6DC, 0xFF7A8290);

    // Ручка
    fb_fill_round_rect_aa(x + 20, y + 9, 8, 4, 2, 0xFFFFFFFF);

    // Крышка
    fb_fill_round_rect_aa(x + 11, y + 14, 26, 5, 2, 0xFFFFFFFF);

    // Тело корзины
    fb_fill_round_rect_aa(x + 13, y + 21, 22, 18, 3, 0xFFFFFFFF);
    // Полоски (рёбра)
    fb_fill_rect(x + 19, y + 24, 2, 12, 0xFF9AA2B0);
    fb_fill_rect(x + 27, y + 24, 2, 12, 0xFF9AA2B0);
}

// ============================================================
// Terminal — тёмный, белый >_
// ============================================================
void icon_draw_ksh(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFF3A424E, 0xFF10141A);

    // > — толстая стрелка (из двух линий)
    for (int i = -1; i <= 1; i++) {
        fb_draw_line_aa(x + 16, y + 20 + i, x + 26, y + 27 + i, 0xFFFFFFFF, 255);
        fb_draw_line_aa(x + 26, y + 27 + i, x + 16, y + 34 + i, 0xFFFFFFFF, 255);
    }

    // _ — нижнее подчёркивание
    fb_fill_round_rect_aa(x + 27, y + 33, 10, 3, 1, 0xFFFFFFFF);
}

// ============================================================
// Files — голубой, белая папка
// ============================================================
void icon_draw_files(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFF6EB4FF, 0xFF2A6BC8);

    // Задняя часть папки (язычок)
    fb_fill_round_rect_aa(x + 10, y + 14, 14, 8, 2, 0xFFFFFFFF);
    // Передняя часть папки
    fb_fill_round_rect_aa(x + 9, y + 18, 30, 20, 3, 0xFFFFFFFF);
    // Внутренняя линия-тень под язычком
    fb_blend_rect(x + 9, y + 18, 30, 1, 0xFFB8C8DC, 220);
}

// ============================================================
// Editor — белый, серые линии (документ)
// ============================================================
void icon_draw_editor(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFFFBFBFD, 0xFFE0E2E8);

    // Тёмная полоса-заголовок сверху
    fb_fill_round_rect_top_aa(x + 9, y + 12, 30, 6, 3, 0xFF3A6EA8);

    // Тело документа
    fb_fill_round_rect_aa(x + 9, y + 12, 30, 26, 3, 0xFF303A4A);

    // Строки текста
    fb_fill_round_rect_aa(x + 13, y + 22, 22, 2, 1, 0xFFFFFFFF);
    fb_fill_round_rect_aa(x + 13, y + 27, 22, 2, 1, 0xFFFFFFFF);
    fb_fill_round_rect_aa(x + 13, y + 32, 16, 2, 1, 0xFFFFFFFF);
}

// ============================================================
// Brainix — синий, белая буква B
// ============================================================
void icon_draw_brainix(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFF4A90E2, 0xFF1E4480);

    // Буква B — вертикальная стойка
    fb_fill_round_rect_aa(x + 17, y + 13, 5, 22, 2, 0xFFFFFFFF);

    // Верхняя дуга
    fb_fill_circle_aa(x + 25, y + 19, 7, 0xFFFFFFFF);
    fb_fill_circle_aa(x + 25, y + 19, 4, 0xFF2E5AA8);

    // Нижняя дуга
    fb_fill_circle_aa(x + 25, y + 29, 7, 0xFFFFFFFF);
    fb_fill_circle_aa(x + 25, y + 29, 4, 0xFF2E5AA8);

    // Перекрываем левую часть дуг стойкой, чтобы получилась B
    fb_fill_round_rect_aa(x + 17, y + 13, 5, 22, 2, 0xFFFFFFFF);

    // Правые «щёчки» B
    fb_fill_rect(x + 32, y + 17, 1, 5, 0xFFFFFFFF);
    fb_fill_rect(x + 32, y + 27, 1, 5, 0xFFFFFFFF);
}

// ============================================================
// Settings — серебро, белая шестерёнка
// ============================================================
void icon_draw_settings(int x, int y, int selected) {
    (void)selected;
    bg_squircle(x, y, 0xFFB8BEC8, 0xFF5A6270);

    int cx = x + 24;
    int cy = y + 24;

    // 8 зубьев
    fb_fill_round_rect_aa(cx - 3, cy - 20, 6, 8, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx - 3, cy + 12, 6, 8, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx - 20, cy - 3, 8, 6, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx + 12, cy - 3, 8, 6, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx - 16, cy - 16, 7, 7, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx + 9,  cy - 16, 7, 7, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx - 16, cy + 9,  7, 7, 2, 0xFFFFFFFF);
    fb_fill_round_rect_aa(cx + 9,  cy + 9,  7, 7, 2, 0xFFFFFFFF);

    // Основной круг
    fb_fill_circle_aa(cx, cy, 14, 0xFFFFFFFF);

    // Отверстие в центре — цветом фона
    fb_fill_circle_aa(cx, cy, 5, 0xFF8A929E);
}