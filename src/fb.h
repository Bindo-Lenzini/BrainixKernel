// BrainixKernel — fb.h
#ifndef BRAINIX_FB_H
#define BRAINIX_FB_H

#include <stdint.h>

// Множитель: 1 логический пиксель = FB_SCALE × FB_SCALE физических.
// Фреймбуфер реально 2560×1600, но весь UI работает в 1280×800.
#define FB_SCALE 2

void fb_init(uint32_t *base, uint32_t w_phys, uint32_t h_phys,
             uint32_t stride_phys);

uint32_t fb_width(void);   // логическая ширина (1280)
uint32_t fb_height(void);  // логическая высота (800)

void fb_putpixel(int x, int y, uint32_t color);
uint32_t fb_getpixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_fill_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot);
void fb_darken_rect(int x, int y, int w, int h, uint32_t factor);

void fb_blend_rect(int x, int y, int w, int h, uint32_t color, uint32_t alpha);
void fb_blend_round_rect_aa(int x, int y, int w, int h, int r,
                            uint32_t color, uint32_t alpha);

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);

void fb_draw_cp_aa(int x, int y, uint32_t cp, uint32_t fg, uint32_t bg);
void fb_draw_char_aa(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string_aa(int x, int y, const char *s, uint32_t fg, uint32_t bg);

void fb_draw_line_aa(int x0, int y0, int x1, int y1,
                     uint32_t color, uint32_t alpha);

void fb_fill_round_rect_v_grad_aa(int x, int y, int w, int h, int r,
                                  uint32_t top, uint32_t bot);

void fb_blit_argb(int x, int y, int w, int h, const uint32_t *pixels);
void fb_blit_argb_scaled(int x, int y, int dw, int dh,
                         int sw, int sh, const uint32_t *pixels);

void fb_fill_circle_aa(int cx, int cy, int r, uint32_t color);
void fb_fill_round_rect_aa(int x, int y, int w, int h, int r, uint32_t color);
void fb_fill_round_rect_top_aa(int x, int y, int w, int h, int r, uint32_t color);
void fb_fill_round_rect_bot_aa(int x, int y, int w, int h, int r, uint32_t color);

void fb_console_putc(char c);
void fb_console_clear(void);
int  fb_console_is_ready(void);

void fb_mark_dirty(int x, int y, int w, int h);
void fb_flush(void);
void fb_flush_dirty(void);
void fb_get_pixels(int x, int y, int w, int h, uint32_t *out);
void fb_put_pixels(int x, int y, int w, int h, const uint32_t *in);

#endif