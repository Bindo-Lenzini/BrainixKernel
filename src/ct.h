// BrainixKernel — ct.h
// Constant-time утилиты. НЕ оптимизировать без понимания.
#ifndef BRAINIX_CT_H
#define BRAINIX_CT_H

#include <stdint.h>

// Возвращает 0 если равны, !=0 иначе.
// Время работы зависит только от n, не от содержимого.
int ct_memcmp(const void *a, const void *b, uint32_t n);

// 1 если все n байт нулевые, 0 иначе.
int ct_is_zero(const void *a, uint32_t n);

#endif
