// BrainixKernel — backdrop.h
// Liquid Glass: размытие обоев + рисование «матовых» панелей.
#ifndef BRAINIX_BACKDROP_H
#define BRAINIX_BACKDROP_H

#include <stdint.h>

// Инициализация (выделяет буферы). Вызвать один раз после fb_init.
void backdrop_init(void);

// Снять обои с framebuffer и размыть. Вызывать при старте и после смены обоев.
void backdrop_capture(void);

// Пометить, что нужно переснять (после смены обоев).
void backdrop_invalidate(void);

// Нарисовать матовую (frosted glass) панель со скруглением.
// tint — цвет, alpha — сила тонировки (0..255). Чем меньше alpha, тем
// сильнее просвечивает размытый фон.
void backdrop_draw_frosted(int x, int y, int w, int h, int r,
                           uint32_t tint, uint32_t alpha);

// То же без скругления — для полос на всю ширину.
void backdrop_draw_frosted_rect(int x, int y, int w, int h,
                                uint32_t tint, uint32_t alpha);

#endif