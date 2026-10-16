// BrainixKernel (BKOS) — jpeg.h
// Baseline + Progressive JPEG в ARGB8888.
#ifndef BKOS_JPEG_H
#define BKOS_JPEG_H

#include <stdint.h>
#include "png.h"

// Декодирует baseline (SOF0/SOF1) или progressive (SOF2) JPEG.
// Заполняет out (pixels через phys_alloc_pages).
int jpeg_decode(const uint8_t *data, uint32_t len, image_t *out);

// Причина последнего провала jpeg_decode(). Пустая = успех.
const char *jpeg_last_error(void);

#endif