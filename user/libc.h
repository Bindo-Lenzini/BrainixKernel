// BrainixKernel (BKOS) — libc.h
// Минимальная libc для user-программ.
// Реализация — user/libc.c. Никакой аллокации: всё на статике.

#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>

// ---------- string.h ----------
unsigned long strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, unsigned long n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, unsigned long n);
char *strcat(char *dst, const char *src);

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
int   memcmp(const void *a, const void *b, unsigned long n);

// ---------- stdlib.h ----------
int   atoi(const char *s);
void  itoa(int v, char *buf);          // buf >= 12 байт
void  utoa_hex(uint64_t v, char *buf); // buf >= 17 байт, "0x..."
uint16_t htons(uint16_t v);
uint16_t ntohs(uint16_t v);
uint32_t htonl(uint32_t v);
uint32_t ntohl(uint32_t v);
void *malloc(unsigned long size);
void  free(void *ptr);

// ---------- stdio.h ----------
void  putchar(int c);
void  puts(const char *s);

// Подмножество printf: %s %d %u %x %c %% %p.
// %p — печатает 16 hex-цифр без "0x".
void  printf(const char *fmt, ...);

#endif
