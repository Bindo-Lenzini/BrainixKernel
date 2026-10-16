// BrainixKernel (BKOS) — icons.h
// Все иконки рисуются в боксе 48×48 (верхний-левый угол (x, y)).
#ifndef BKOS_ICONS_H
#define BKOS_ICONS_H

#include <stdint.h>

#define ICON_SIZE 48

void icon_draw_ksh    (int x, int y, int selected);
void icon_draw_files  (int x, int y, int selected);
void icon_draw_editor (int x, int y, int selected);
void icon_draw_brainix(int x, int y, int selected);

void icon_draw_computer(int x, int y, int selected);
void icon_draw_recycle (int x, int y, int selected);

// Иконка "Персонализация" — шестерёнка.
void icon_draw_settings(int x, int y, int selected);

#endif