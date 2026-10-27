// BrainixKernel — font8x16_aa.h
// Глифы 16×32 физических пикселей (8×16 логических при 2× scale).
// 160 glyphs: 96 ASCII + 64 Cyrillic.
#ifndef BRAINIX_FONT8X16_AA_H
#define BRAINIX_FONT8X16_AA_H

#include <stdint.h>

extern const uint8_t font8x16_aa[][32][16];
extern const int     font8x16_aa_count;

// Codepoint → glyph index, или -1.
int font_index(uint32_t cp);

#endif