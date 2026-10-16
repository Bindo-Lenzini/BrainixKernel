// BrainixKernel (BKOS) — font8x8.h
// 8×8 растровый ASCII-шрифт, покрывает 0x20..0x7F (96 символов).
// Каждый символ — 8 байт, один байт = одна строка, MSB слева.
#ifndef BKOS_FONT8X8_H
#define BKOS_FONT8X8_H

#include <stdint.h>

extern const uint8_t font8x8_basic[96][8];

#endif