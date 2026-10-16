// BrainixKernel (BKOS) — kbd.h
#ifndef BKOS_KBD_H
#define BKOS_KBD_H

#include <stdint.h>

// Внутренние коды (0x01..0x1F):
//   0x11 UP, 0x12 DOWN, 0x13 LEFT, 0x14 RIGHT, 0x15 HOME, 0x16 END, 0x17 DEL
//   0x18 CTRL_S, 0x19 CTRL_Q, 0x1A CTRL_C, 0x1B CTRL_V
// Прочие — печатные ASCII / UTF-8-байты.

int  kbd_init(uint64_t ecam_base);
void kbd_set_handler(void (*handler)(char c));
void kbd_poll(void);

// Текущее состояние (для UI).
int  kbd_layout_ru(void);
const char *kbd_layout_name(void);

#endif