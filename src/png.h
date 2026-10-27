// BrainixKernel — png.h
// Декодер изображений PNG, BMP, JPEG в формат ARGB8888 (0xAARRGGBB).
#ifndef BRAINIX_PNG_H
#define BRAINIX_PNG_H

#include <stdint.h>

typedef struct {
    uint32_t  width;
    uint32_t  height;
    uint32_t *pixels;    // width * height, ARGB8888, top-down
    int       ok;        // 1 = успешно декодировано
} image_t;

// Определяет формат по magic-байтам и декодирует.
int  image_decode(const uint8_t *data, uint32_t len, image_t *out);

// Освобождает память.
void image_free(image_t *img);

// Причина последнего провала image_decode(). Пустая = OK.
const char *png_last_error(void);

#endif  