// BrainixKernel — personalize.h
// Окно "Personalization": список обоев + превью + Apply.
#ifndef BRAINIX_PERSONALIZE_H
#define BRAINIX_PERSONALIZE_H

#include <stdint.h>
#include "png.h"

#define PERS_MAX_ITEMS 64

typedef enum {
    PERS_PRESET = 0,
    PERS_INITRD = 1,
    PERS_MEDIA  = 2,
} pers_src_t;

typedef struct {
    char       name[64];
    pers_src_t src;
    int        preset_idx;
} pers_item_t;

typedef struct {
    pers_item_t items[PERS_MAX_ITEMS];
    int         count;
    int         selected;
    int         scroll;
    int         status_msg;    // 0 = none, 1 = applied, 2 = error
    int         cursor_visible;

    int         preview_for;   // для какого items[i] сейчас превью
    int         preview_is_gradient;
    image_t     preview;
} pers_t;

void pers_init(pers_t *p);
void pers_close(pers_t *p);
void pers_draw_content(pers_t *p, int cx, int cy, int cw, int ch);
void pers_key(pers_t *p, char c);
// Возвращает 1, если клик был обработан (значит, нужен redraw).
int  pers_click(pers_t *p, int mx, int my, int cx, int cy, int cw, int ch);

#endif