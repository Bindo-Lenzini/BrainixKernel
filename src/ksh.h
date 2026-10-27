// BrainixKernel — ksh.h
// KernelShell: буфер вывода user-программ.
#ifndef BRAINIX_KSH_H
#define BRAINIX_KSH_H

#include <stdint.h>

void ksh_init(void);

// Записать данные от user-программы в буфер.
void ksh_out_write(const char *data, uint32_t len);

// Извлечь очередную полную строку. Возвращает 1, если строка была,
// 0 — если в буфере нет полной строки (или он пуст).
int  ksh_out_pop_line(char *out, int max);

// Принудительно завершить текущую неполную строку (для выхода).
void ksh_out_flush_partial(void);

#endif