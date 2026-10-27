// BrainixKernel — pipe.h
// Простой pipe: кольцевой буфер на 4 KB + refcounting сторон.

#ifndef BRAINIX_PIPE_H
#define BRAINIX_PIPE_H

#include <stdint.h>

#define PIPE_MAX       8
#define PIPE_BUF_SIZE  4096

// Аллоцирует pipe. Возвращает индекс (0..PIPE_MAX-1) или -1.
int  pipe_alloc(void);

// Закрывает концы. Декремент refcount.
void pipe_ref_read (int idx);
void pipe_ref_write(int idx);
void pipe_close_read (int idx);
void pipe_close_write(int idx);

// Читает до max байт.
//   > 0 — прочитано
//   = 0 — EOF (нет данных и нет писателей)
//   < 0 — EAGAIN (нет данных, писатели ещё живы)
int  pipe_read(int idx, void *buf, uint32_t max);

// Пишет до len байт.
//   > 0 — записано (может быть меньше len при переполнении)
//   < 0 — нет читателей
int  pipe_write(int idx, const void *buf, uint32_t len);

#endif
