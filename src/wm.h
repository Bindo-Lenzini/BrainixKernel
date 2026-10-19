// BrainixKernel (BKOS) — wm.h
#ifndef BKOS_WM_H
#define BKOS_WM_H

#include <stdint.h>

#define WM_MAX 8

typedef enum {
    WND_NONE = 0,
    WND_KSH,
    WND_EDITOR,
    WND_FILES,
    WND_PERSONALIZE,
    WND_ABOUT,
    WND_VNC,
} wnd_type_t;

typedef struct {
    wnd_type_t  type;
    int         x, y, w, h;
    int         z;
    int         visible;
    int         minimized;
    int         focused;
    int         maximized;
    int         saved_x, saved_y, saved_w, saved_h;
    char        title[40];
} wnd_t;

void wm_init(void);
int  wm_create(wnd_type_t type, int x, int y, int w, int h, const char *title);
void wm_show(int idx, int show);
void wm_raise(int idx);
wnd_t *wm_focused(void);
wnd_t *wm_get(int idx);
int  wm_find_type(wnd_type_t type);
int  wm_find_type_visible(wnd_type_t type);

int  wm_hit_test(int x, int y);
int  wm_hit_title(int x, int y);
int  wm_hit_close(int x, int y);
int  wm_hit_maximize(int x, int y);
int  wm_hit_minimize(int x, int y);
int  wm_hit_resize(int x, int y);
int  wm_hit_content(int x, int y);

void wm_set_size(int idx, int w, int h);
void wm_minimize(int idx);
void wm_maximize_toggle(int idx);
int  wm_is_minimized_any(wnd_type_t type);

typedef void (*wm_draw_content_fn)(wnd_type_t type, int cx, int cy, int cw, int ch);
void wm_draw_all(wm_draw_content_fn draw_content);
void wm_draw_window_only(int idx, wm_draw_content_fn draw_content);

void wm_set_title(int idx, const char *title);

#endif