// BrainixKernel — inflate.h
// Декомпрессия zlib-потока (RFC 1950), внутри — raw deflate (RFC 1951).
#ifndef BRAINIX_INFLATE_H
#define BRAINIX_INFLATE_H

#include <stdint.h>

int inflate_zlib(const uint8_t *src, uint32_t src_len,
                 uint8_t *dst, uint32_t dst_cap, uint32_t *out_len);

#endif