// BrainixKernel (BKOS) — win.h
#ifndef BKOS_WIN_H
#define BKOS_WIN_H

#include <stdint.h>

#define WIN_HISTORY_MAX   26
#define WIN_LINE_MAX      96
#define WIN_INPUT_MAX     80
#define WIN_CMD_HISTORY   16

typedef struct {
    char history[WIN_HISTORY_MAX][WIN_LINE_MAX];
    int  history_count;

    char input[WIN_INPUT_MAX + 1];
    int  input_len;
    int  cursor_pos;

    char cmd_history[WIN_CMD_HISTORY][WIN_INPUT_MAX + 1];
    int  cmd_history_count;
    int  cmd_history_idx;

    int  cursor_visible;
} win_t;

void win_init(win_t *w);
void win_draw_content(win_t *w, int cx, int cy, int cw, int ch);
void win_puts(win_t *w, const char *s);
void win_key(win_t *w, char c);

#endif