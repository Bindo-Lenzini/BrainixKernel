// BrainixKernel (BKOS) — clipboard.h
// Глобальный буфер обмена (для Ctrl+C / Ctrl+V).
#ifndef BKOS_CLIPBOARD_H
#define BKOS_CLIPBOARD_H

#include <stdint.h>

#define CLIP_MAX 2048

void        clip_set(const char *data, int len);
const char *clip_get(int *out_len);
void        clip_clear(void);
int         clip_len(void);

#endif