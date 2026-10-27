// BrainixKernel — ct.c
#include "ct.h"

int ct_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t diff = 0;
    for (uint32_t i = 0; i < n; i++) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff;
}

int ct_is_zero(const void *a, uint32_t n) {
    const uint8_t *x = (const uint8_t *)a;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= x[i];
    return acc == 0;
}
