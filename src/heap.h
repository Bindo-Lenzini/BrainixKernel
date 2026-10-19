// BrainixKernel (BKOS) — heap.h
#ifndef BKOS_HEAP_H
#define BKOS_HEAP_H

#include <stddef.h>

// Инициализирует heap. Вызывать один раз после physmem_init().
void  heap_init(void);

// Возвращает указатель на >= size байт (16-байтовое выравнивание),
// либо NULL, если size == 0 или size > HEAP_MAX_ALLOC.
//
// Диапазоны:
//   size ≤ 4096       → slab-бакет (быстро, страница режется на слоты)
//   4096 < size ≤ 64K → page-based (аллокация целых страниц)
void *kmalloc(size_t size);

// Освобождает блок, полученный из kmalloc().
// Для больших блоков возвращает страницы в physmem.
void  kfree(void *ptr);

// Максимум одной аллокации через kmalloc.
// Больше — только phys_alloc_pages() напрямую.
#define HEAP_MAX_ALLOC 65536u

#endif
