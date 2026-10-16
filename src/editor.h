// BrainixKernel (BKOS) — editor.h
#ifndef BKOS_EDITOR_H
#define BKOS_EDITOR_H

#include <stdint.h>

#define EDIT_MAX 2048

typedef struct {
    char     name[64];
    char     buf[EDIT_MAX];
    uint32_t len;
    uint32_t cursor;
    int      cursor_visible;
    int      dirty;
    int      status_msg;
    int      scroll_line;    // верхняя видимая логическая строка
    int      visible_rows;   // обновляется в draw_content
} editor_t;

void editor_open (editor_t *e, const char *name);
void editor_close(editor_t *e);
int  editor_is_open(const editor_t *e);

void editor_draw_content(editor_t *e, int cx, int cy, int cw, int ch);
void editor_key(editor_t *e, char c);
int  editor_save(editor_t *e);

// Клик по content-области. Возвращает 1, если клик обработан.
int  editor_click(editor_t *e, int mx, int my,
                  int cx, int cy, int cw, int ch);

#endif