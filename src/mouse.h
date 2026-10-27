// BrainixKernel — mouse.h
#ifndef BRAINIX_MOUSE_H
#define BRAINIX_MOUSE_H

#include <stdint.h>

int  mouse_init(uint64_t ecam_base);
void mouse_poll(void);

int  mouse_x(void);
int  mouse_y(void);
int  mouse_left_pressed(void);   // 1 = была нажата с прошлого poll
int  mouse_left_down(void);      // 1 = удерживается сейчас

#define MOUSE_W 12
#define MOUSE_H 18

#endif