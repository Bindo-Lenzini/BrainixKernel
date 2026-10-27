// BrainixKernel — about.h
// Окно "About This System": системная информация в стиле macOS.
#ifndef BRAINIX_ABOUT_H
#define BRAINIX_ABOUT_H

void about_draw_content(int cx, int cy, int cw, int ch);

// Попадает ли клик (mx, my) в кнопку "System Report".
// Возвращает 1, если да — тогда kernel.c откроет ksh с отчётом.
int  about_hit_report(int mx, int my, int cx, int cy, int cw, int ch);

#endif