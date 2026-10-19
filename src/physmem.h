// BrainixKernel (BKOS) — physmem.h
#ifndef BKOS_PHYSMEM_H
#define BKOS_PHYSMEM_H

#include <stdint.h>

#define PAGE_SIZE 4096ull

void physmem_init(void);

// Одна страница.
void  *phys_alloc_page(void);
void   phys_free_page(void *pa);

// N подряд идущих страниц. Возвращает базовый адрес или NULL.
// Банки RAM между собой не склеиваются — блок должен уместиться
// целиком в одном банке.
void  *phys_alloc_pages(uint64_t n);

// Ref-counting для COW.
// Каждая страница имеет счётчик ссылок. Обычная аллокация — 1.
// phys_inc_ref() — увеличить, phys_free_page() — уменьшить,
// физически освобождает при достижении 0.
void     phys_inc_ref(void *pa);
uint16_t phys_get_ref(void *pa);

uint64_t physmem_total_pages(void);
uint64_t physmem_free_pages(void);

#endif
