// BrainixKernel (BKOS) — sha256_selftest.c
// Kernel-only self-test для SHA-256 и HMAC-SHA256.
// Отделён от sha256.c, чтобы sha256.c можно было включать в user-space
// без зависимости от uart_puts.
#include <stdint.h>
#include "sha256.h"

extern void uart_puts(const char *s);

static int hex_eq(const uint8_t *digest, const char *hex) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        char hi = H[(digest[i] >> 4) & 0xF];
        char lo = H[digest[i] & 0xF];
        if (hi != hex[i*2] || lo != hex[i*2 + 1]) return 0;
    }
    return 1;
}

int sha256_selftest(void) {
    uint8_t d[32];
    int ok = 1;

    // SHA TC1 (RFC 6234): SHA-256("")
    sha256("", 0, d);
    if (!hex_eq(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
        uart_puts("[sha256] FAIL: empty\n"); ok = 0;
    }

    // SHA TC2 (RFC 6234): SHA-256("abc")
    sha256("abc", 3, d);
    if (!hex_eq(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        uart_puts("[sha256] FAIL: abc\n"); ok = 0;
    }

    // SHA TC3 (RFC 6234 Test Case 2): ровно 56 байт.
    // Boundary: (56 + 1 + padding) переходит во второй блок.
    {
        static const char msg[] =
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        sha256(msg, 56, d);
        if (!hex_eq(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")) {
            uart_puts("[sha256] FAIL: 56-byte boundary\n"); ok = 0;
        }
    }

    // SHA TC4 (RFC 6234): 10^6 * 'a'. Медленно на QEMU, держим выключенным.
    // Раскомментировать для разовой проверки.
    /*
    {
        static uint8_t a[1000];
        for (int i = 0; i < 1000; i++) a[i] = 'a';
        sha256_ctx_t c; sha256_init(&c);
        for (int i = 0; i < 1000; i++) sha256_update(&c, a, 1000);
        sha256_final(&c, d);
        if (!hex_eq(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) {
            uart_puts("[sha256] FAIL: million-a\n"); ok = 0;
        }
    }
    */

    // HMAC TC1 (RFC 4231 Test Case 1): короткий ключ.
    {
        uint8_t key[20];
        for (int i = 0; i < 20; i++) key[i] = 0x0b;
        hmac_sha256(key, 20, "Hi There", 8, d);
        if (!hex_eq(d, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")) {
            uart_puts("[sha256] FAIL: hmac short\n"); ok = 0;
        }
    }

    // HMAC TC2 (RFC 4231 Test Case 2): "Jefe".
    {
        hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, d);
        if (!hex_eq(d, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")) {
            uart_puts("[sha256] FAIL: hmac jefe\n"); ok = 0;
        }
    }

    // HMAC TC3 (RFC 4231 Test Case 6): ключ длиной 131 байт.
    // Единственный тест, покрывающий ветку key_len > SHA256_BLOCK_SIZE.
    {
        uint8_t key[131];
        for (int i = 0; i < 131; i++) key[i] = 0xaa;
        static const char msg[] =
            "Test Using Larger Than Block-Size Key - Hash Key First";
        hmac_sha256(key, 131, msg, 54, d);
        if (!hex_eq(d, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54")) {
            uart_puts("[sha256] FAIL: hmac long key\n"); ok = 0;
        }
    }

    if (ok) uart_puts("[sha256] selftest OK\n");
    return ok;
}
