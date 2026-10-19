// BrainixKernel (BKOS) — uaccess.h
// Безопасные копирования между kernel и user-памятью.

#ifndef BKOS_UACCESS_H
#define BKOS_UACCESS_H

#include <stdint.h>

// Возврат: 0 = успех, -1 = fault при доступе к user-памяти.
int copy_from_user(void *dst, const void *user_src, uint64_t len);
int copy_to_user  (void *user_dst, const void *src, uint64_t len);

extern volatile uint64_t __uaccess_recovery_elr;
extern volatile uint64_t __uaccess_active;

#endif
