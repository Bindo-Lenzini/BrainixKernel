// BrainixKernel (BKOS) — physmem.c
// Bitmap-аллокатор. Возвращает KVA-указатели (PA + KERNEL_VA_OFFSET).

#include <stdint.h>
#include "physmem.h"
#include "dtb.h"
#include "mmu.h"

#define MAX_BANKS 4
#define MAX_PAGES (1024ull * 1024)

typedef struct {
    uint64_t base;         // PA
    uint64_t first_page;
    uint64_t num_pages;
} bank_t;

static bank_t   g_banks[MAX_BANKS];
static int      g_num_banks   = 0;
static uint64_t g_total_pages = 0;

static uint64_t bitmap[MAX_PAGES / 64];
static uint16_t g_refcount[MAX_PAGES];

extern char _end;

static inline void bm_set  (uint64_t i) { bitmap[i / 64] |=  (1ull << (i % 64)); }
static inline void bm_clear(uint64_t i) { bitmap[i / 64] &= ~(1ull << (i % 64)); }
static inline int  bm_test (uint64_t i) { return (bitmap[i / 64] >> (i % 64)) & 1; }

static int bank_of(uint64_t pa) {
    for (int i = 0; i < g_num_banks; i++) {
        uint64_t s = g_banks[i].base;
        uint64_t e = s + g_banks[i].num_pages * PAGE_SIZE;
        if (pa >= s && pa < e) return i;
    }
    return -1;
}

static void reserve_range(uint64_t pa_start, uint64_t pa_end) {
    for (int b = 0; b < g_num_banks; b++) {
        uint64_t b_start = g_banks[b].base;
        uint64_t b_end   = b_start + g_banks[b].num_pages * PAGE_SIZE;

        uint64_t s = pa_start > b_start ? pa_start : b_start;
        uint64_t e = pa_end   < b_end   ? pa_end   : b_end;
        if (s >= e) continue;

        uint64_t first = g_banks[b].first_page + (s - b_start) / PAGE_SIZE;
        uint64_t last  = g_banks[b].first_page +
                         (e - b_start + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t i = first; i < last; i++) bm_set(i);
    }
}

static uint64_t fdt_size(const void *dtb_kva) {
    if (!dtb_kva) return 0;
    const uint8_t *p = (const uint8_t *)dtb_kva;
    uint32_t magic = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
    if (magic != 0xD00DFEEDu) return 0;
    return ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

void physmem_init(void) {
    ram_bank_t dtb_banks[MAX_BANKS];
    int n = dtb_get_ram_banks(dtb_banks, MAX_BANKS);

    if (n == 0) {
        dtb_banks[0].base = 0x40000000ull;
        dtb_banks[0].size = 128ull * 1024 * 1024;
        n = 1;
    }

    g_num_banks = 0;
    g_total_pages = 0;

    for (int i = 0; i < n; i++) {
        uint64_t pages = dtb_banks[i].size / PAGE_SIZE;
        if (pages == 0) continue;
        if (g_total_pages + pages > MAX_PAGES) break;

        g_banks[g_num_banks].base       = dtb_banks[i].base;
        g_banks[g_num_banks].first_page = g_total_pages;
        g_banks[g_num_banks].num_pages  = pages;

        g_total_pages += pages;
        g_num_banks++;
    }

    for (uint64_t i = 0; i < MAX_PAGES / 64; i++) bitmap[i] = 0;
    for (uint64_t i = 0; i < MAX_PAGES; i++) g_refcount[i] = 0;

    // Kernel image занимает PA [base, _end-PA).
    // `_end` — high VA. Convert to PA.
    if (g_num_banks > 0) {
        uint64_t end_pa = (uint64_t)&_end - KERNEL_VA_OFFSET;
        reserve_range(g_banks[0].base, end_pa);
    }

    // DTB занимает PA [dtb_pa, dtb_pa + dsz).
    const void *dtb_kva = dtb_get_ptr();
    uint64_t dsz = fdt_size(dtb_kva);
    if (dsz > 0) {
        uint64_t dtb_pa = (uint64_t)dtb_kva - KERNEL_VA_OFFSET;
        reserve_range(dtb_pa, dtb_pa + dsz);
    }
}

void *phys_alloc_page(void) {
    for (uint64_t i = 0; i < g_total_pages; i++) {
        if (bm_test(i)) continue;
        bm_set(i);

        g_refcount[i] = 1;
        for (int b = 0; b < g_num_banks; b++) {
            if (i < g_banks[b].first_page + g_banks[b].num_pages) {
                uint64_t off = i - g_banks[b].first_page;
                uint64_t pa  = g_banks[b].base + off * PAGE_SIZE;
                return PA_TO_KVA(pa);
            }
        }
    }
    return 0;
}

void *phys_alloc_pages(uint64_t n) {
    if (n == 0) return 0;
    if (n == 1) return phys_alloc_page();

    for (int b = 0; b < g_num_banks; b++) {
        uint64_t first = g_banks[b].first_page;
        uint64_t count = g_banks[b].num_pages;

        uint64_t i = 0;
        while (i + n <= count) {
            int ok = 1;
            for (uint64_t k = 0; k < n; k++) {
                if (bm_test(first + i + k)) { ok = 0; break; }
            }
            if (ok) {
                for (uint64_t k = 0; k < n; k++) {
                    bm_set(first + i + k);
                    g_refcount[first + i + k] = 1;
                }
                uint64_t pa = g_banks[b].base + i * PAGE_SIZE;
                return PA_TO_KVA(pa);
            }
            i++;
        }
    }
    return 0;
}

void phys_free_page(void *kva) {
    if (!kva) return;
    uint64_t pa = (uint64_t)kva - KERNEL_VA_OFFSET;
    if (pa & (PAGE_SIZE - 1)) return;

    int b = bank_of(pa);
    if (b < 0) return;

    uint64_t off = (pa - g_banks[b].base) / PAGE_SIZE;
    uint64_t idx = g_banks[b].first_page + off;

    if (g_refcount[idx] > 1) {
        g_refcount[idx]--;
        return;
    }
    g_refcount[idx] = 0;
    bm_clear(idx);
}

// ------------------------------------------------------------------
// Ref-counting для COW.
// ------------------------------------------------------------------

static uint64_t page_index_of(uint64_t pa) {
    int b = bank_of(pa);
    if (b < 0) return (uint64_t)-1;
    uint64_t off = (pa - g_banks[b].base) / PAGE_SIZE;
    return g_banks[b].first_page + off;
}

void phys_inc_ref(void *kva) {
    if (!kva) return;
    uint64_t pa = (uint64_t)kva - KERNEL_VA_OFFSET;
    if (pa & (PAGE_SIZE - 1)) return;
    uint64_t idx = page_index_of(pa);
    if (idx == (uint64_t)-1) return;
    if (g_refcount[idx] < 0xFFFF) g_refcount[idx]++;
}

uint16_t phys_get_ref(void *kva) {
    if (!kva) return 0;
    uint64_t pa = (uint64_t)kva - KERNEL_VA_OFFSET;
    if (pa & (PAGE_SIZE - 1)) return 0;
    uint64_t idx = page_index_of(pa);
    if (idx == (uint64_t)-1) return 0;
    return g_refcount[idx];
}

uint64_t physmem_total_pages(void) { return g_total_pages; }

uint64_t physmem_free_pages(void) {
    uint64_t free = 0;
    for (uint64_t i = 0; i < g_total_pages; i++) {
        if (!bm_test(i)) free++;
    }
    return free;
}
