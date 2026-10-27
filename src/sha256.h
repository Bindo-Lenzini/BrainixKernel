// BrainixKernel — sha256.h
// SHA-256 (FIPS 180-4) + HMAC-SHA256 (RFC 2104).
// Используется в TLS 1.3: HKDF, transcript hash, Finished MAC.
#ifndef BRAINIX_SHA256_H
#define BRAINIX_SHA256_H

#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;      // всего обработано бит
    uint8_t  buf[SHA256_BLOCK_SIZE];
    uint32_t buflen;      // байт в буфере (0..63)
} sha256_ctx_t;

void sha256_init  (sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, uint32_t len);
void sha256_final (sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_SIZE]);

// One-shot
void sha256(const void *data, uint32_t len, uint8_t out[SHA256_DIGEST_SIZE]);

// HMAC-SHA256
void hmac_sha256(const void *key, uint32_t key_len,
                 const void *data, uint32_t data_len,
                 uint8_t out[SHA256_DIGEST_SIZE]);

// Self-test: три стандартных вектора. Печатает результат в UART.
// Возвращает 1 если всё совпало, 0 иначе.
int  sha256_selftest(void);

#endif
