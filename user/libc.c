// BrainixKernel — libc.c
// Минимальная libc: string, stdlib, printf (подмножество).

#include "libc.h"
#include "user.h"
#include <stdarg.h>

// ============================================================
// string.h
// ============================================================

unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != 0) {}
    return dst;
}

char *strncpy(char *dst, const char *src, unsigned long n) {
    unsigned long i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != 0) {}
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (unsigned long i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    for (unsigned long i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, unsigned long n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

// ============================================================
// stdlib.h
// ============================================================

int atoi(const char *s) {
    int sign = 1;
    int v = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

void itoa(int v, char *buf) {
    char tmp[12];
    int t = 0;
    int neg = 0;
    unsigned int u;
    if (v < 0) { neg = 1; u = (unsigned int)(-v); }
    else       { u = (unsigned int)v; }
    if (u == 0) tmp[t++] = '0';
    while (u) { tmp[t++] = (char)('0' + (u % 10)); u /= 10; }
    int i = 0;
    if (neg) buf[i++] = '-';
    while (t > 0) buf[i++] = tmp[--t];
    buf[i] = 0;
}

static const char g_hex[] = "0123456789abcdef";

void utoa_hex(uint64_t v, char *buf) {
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = g_hex[(v >> (60 - i * 4)) & 0xF];
    }
    buf[18] = 0;
}

// ============================================================
// stdio.h
// ============================================================

void putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
}

void puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    char nl = '\n';
    write(STDOUT_FILENO, &nl, 1);
}

// Небольшой буфер вывода — уменьшает число SVC-ов для printf.
static char g_outbuf[256];
static int  g_outlen = 0;

static void out_flush(void) {
    if (g_outlen > 0) {
        write(STDOUT_FILENO, g_outbuf, (unsigned long)g_outlen);
        g_outlen = 0;
    }
}

static void out_char(char c) {
    if (g_outlen >= (int)sizeof(g_outbuf)) out_flush();
    g_outbuf[g_outlen++] = c;
}

static void out_str(const char *s) {
    while (*s) out_char(*s++);
}

static void out_uint(unsigned long v, int base, int pad, int min_digits) {
    char tmp[24];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) {
        tmp[t++] = g_hex[v % base];
        v /= base;
    }
    while (t < min_digits) tmp[t++] = pad ? '0' : ' ';
    while (t > 0) out_char(tmp[--t]);
}

static void out_int(int v) {
    if (v < 0) {
        out_char('-');
        out_uint((unsigned long)(-(long)v), 10, 1, 1);
    } else {
        out_uint((unsigned long)v, 10, 1, 1);
    }
}

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            out_char(*p);
            continue;
        }
        p++;
        if (*p == 0) { out_char('%'); break; }

        // Флаги: минимальная ширина (для %p и %08x).
        int min_digits = 1;
        int pad_zero = 0;
        if (*p == '0') {
            pad_zero = 1;
            p++;
            int w = 0;
            while (*p >= '0' && *p <= '9') { w = w * 10 + (*p - '0'); p++; }
            min_digits = w;
        }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                out_str(s ? s : "(null)");
                break;
            }
            case 'd': {
                int v = va_arg(ap, int);
                out_int(v);
                break;
            }
            case 'u': {
                unsigned int v = va_arg(ap, unsigned int);
                out_uint(v, 10, pad_zero, min_digits);
                break;
            }
            case 'x': {
                unsigned int v = va_arg(ap, unsigned int);
                out_uint(v, 16, pad_zero, min_digits);
                break;
            }
            case 'p': {
                void *v = va_arg(ap, void *);
                if (min_digits < 16) min_digits = 16;
                out_uint((unsigned long)v, 16, 1, min_digits);
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                out_char((char)c);
                break;
            }
            case '%': {
                out_char('%');
                break;
            }
            default:
                out_char('%');
                out_char(*p);
                break;
        }
    }
    out_flush();
    va_end(ap);
}


// ============================================================
// malloc — bump allocator поверх brk.
// free — no-op.
// ============================================================

static unsigned long g_heap_cur = 0;
static unsigned long g_heap_end = 0;

void *malloc(unsigned long size) {
    if (size == 0) return 0;

    // Выровнять на 16.
    size = (size + 15) & ~15UL;

    if (g_heap_cur == 0) {
        // Первый вызов: узнаём текущий break.
        g_heap_cur = (unsigned long)brk(0);
        g_heap_end = g_heap_cur;
    }

    if (g_heap_cur + size > g_heap_end) {
        // Расширяем heap через brk.
        unsigned long new_end = g_heap_cur + size;
        void *result = brk((void *)new_end);
        if (result == 0) return 0;
        g_heap_end = (unsigned long)result;
        if (g_heap_end < new_end) return 0;  // не смогли расширить
    }

    void *p = (void *)g_heap_cur;
    g_heap_cur += size;
    return p;
}

void free(void *ptr) {
    (void)ptr;
    // no-op: bump allocator без возможности освобождения.
}


// ============================================================
// Byte-order helpers.
// ============================================================

uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
uint16_t ntohs(uint16_t v) { return htons(v); }

uint32_t htonl(uint32_t v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}
uint32_t ntohl(uint32_t v) { return htonl(v); }
