// BrainixKernel (BKOS) — heap.c
//
// Двухуровневый аллокатор поверх physmem:
//
//   1) Slab для size ≤ 4096:
//      страница режется на слоты фиксированного размера, свободные
//      объекты хранят указатель next на месте payload.
//
//   2) Page-based для 4096 < size ≤ 64 KB:
//      запрашиваем N подряд идущих страниц у physmem, в начале
//      кладём заголовок, возвращаем payload.
//
// Заголовок выделения (16 байт) — общий для обоих уровней.
// Ключевое: magic лежит по одному и тому же смещению от payload
// (ptr - 8), поэтому kfree определяет тип за одну загрузку.
//
// Заголовок не пересекается с payload; для slab он остаётся целым
// даже после того, как payload вернулся в free-list (там первые
// 8 байт payload используются как next-указатель).
//
// Большие аллокации (> HEAP_MAX_ALLOC) не поддерживаются — для них
// используется phys_alloc_pages() напрямую.

#include <stdint.h>
#include <stddef.h>
#include "heap.h"
#include "physmem.h"

#define HEAP_MAGIC  0xB1055EEDu

#define KIND_SLAB   0u
#define KIND_LARGE  1u

// Универсальный заголовок.
//   slab : extra = индекс бакета (0..NUM_BUCKETS-1)
//   large: extra = число страниц
typedef struct alloc_hdr {
    uint32_t magic;
    uint32_t kind;
    uint64_t extra;
} alloc_hdr_t;

// Размер = 16, layout:
//   offset 0: magic (4)
//   offset 4: kind  (4)
//   offset 8: extra (8)
// → ptr - 8 == &magic, что и использует kfree для диспетчеризации.

#define NUM_BUCKETS 9
static const size_t bucket_sizes[NUM_BUCKETS] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

// Свободный объект в slab-бакете: первые 8 байт payload хранят next.
typedef struct free_obj {
    struct free_obj *next;
} free_obj_t;

static free_obj_t *buckets[NUM_BUCKETS];

// Размер slab-слота: заголовок + payload, выровнено на 16.
static size_t slot_size(int i) {
    size_t raw = sizeof(alloc_hdr_t) + bucket_sizes[i];
    return (raw + 15) & ~(size_t)15;
}

// Взять страницу из physmem, нарезать на слоты, все положить в free-list.
static int refill(int i) {
    uint8_t *page = (uint8_t *)phys_alloc_page();
    if (!page) return -1;

    size_t ss = slot_size(i);
    size_t n  = PAGE_SIZE / ss;
    if (n == 0) return -1;

    for (size_t k = 0; k < n; k++) {
        uint8_t *slot = page + k * ss;
        // payload лежит после заголовка
        free_obj_t *fo = (free_obj_t *)(slot + sizeof(alloc_hdr_t));
        fo->next = buckets[i];
        buckets[i] = fo;
    }
    return 0;
}

void heap_init(void) {
    for (int i = 0; i < NUM_BUCKETS; i++) buckets[i] = 0;
}

// Найти минимальный бакет, вмещающий size.
static int find_bucket(size_t size) {
    for (int k = 0; k < NUM_BUCKETS; k++) {
        if (size <= bucket_sizes[k]) return k;
    }
    return -1;
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;

    // ---- Slab path ----
    if (size <= bucket_sizes[NUM_BUCKETS - 1]) {
        int i = find_bucket(size);
        if (i < 0) return 0;
        if (!buckets[i]) {
            if (refill(i) < 0) return 0;
        }
        free_obj_t *fo = buckets[i];
        buckets[i] = fo->next;

        alloc_hdr_t *hdr = (alloc_hdr_t *)((uint8_t *)fo - sizeof(alloc_hdr_t));
        hdr->magic = HEAP_MAGIC;
        hdr->kind  = KIND_SLAB;
        hdr->extra = (uint64_t)i;
        return (void *)fo;
    }

    // ---- Large path ----
    if (size > HEAP_MAX_ALLOC) return 0;

    uint64_t data_bytes = ((uint64_t)size + 15) & ~15ull;
    uint64_t total      = sizeof(alloc_hdr_t) + data_bytes;
    uint64_t pages      = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    uint8_t *p = (uint8_t *)phys_alloc_pages(pages);
    if (!p) return 0;

    alloc_hdr_t *hdr = (alloc_hdr_t *)p;
    hdr->magic = HEAP_MAGIC;
    hdr->kind  = KIND_LARGE;
    hdr->extra = pages;

    return p + sizeof(alloc_hdr_t);
}

void kfree(void *ptr) {
    if (!ptr) return;

    // magic всегда по адресу ptr - 8 в обоих вариантах заголовка.
    alloc_hdr_t *hdr = (alloc_hdr_t *)((uint8_t *)ptr - sizeof(alloc_hdr_t));
    if (hdr->magic != HEAP_MAGIC) return;   // мусорный указатель — игнорируем

    if (hdr->kind == KIND_SLAB) {
        uint32_t b = (uint32_t)hdr->extra;
        if (b >= NUM_BUCKETS) return;
        free_obj_t *fo = (free_obj_t *)ptr;
        fo->next = buckets[b];
        buckets[b] = fo;
        return;
    }

    if (hdr->kind == KIND_LARGE) {
        uint64_t pages = hdr->extra;
        if (pages == 0) return;
        // phys_free_page работает с одной страницей — освобождаем в цикле.
        for (uint64_t i = 0; i < pages; i++) {
            phys_free_page((uint8_t *)hdr + i * PAGE_SIZE);
        }
        return;
    }
    // Неизвестный kind — не трогаем память.
}
