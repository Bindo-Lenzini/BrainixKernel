#include <stdint.h>
#include <stdio.h>

#include "dtb.h"
#include "mmu.h"
#include "physmem.h"

char _end;

int dtb_get_ram_banks(ram_bank_t *out, int max) {
    if (!out || max < 1) return 0;
    uint64_t kernel_end_pa = (uint64_t)(uintptr_t)&_end -
                             KERNEL_VA_OFFSET;
    uint64_t base = (kernel_end_pa + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    out[0].base = base + PAGE_SIZE;
    out[0].size = 16 * PAGE_SIZE;
    return 1;
}

const void *dtb_get_ptr(void) {
    return 0;
}

static int check(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        return 0;
    }
    printf("PASS: %s\n", description);
    return 1;
}

int main(void) {
    physmem_init();
    int failures = 0;
    uint64_t initial_free = physmem_free_pages();

    failures += !check(initial_free == 16,
                       "host fixture initializes a bounded physical bank");
    failures += !check(phys_alloc_pages(0) == 0 &&
                       phys_alloc_pages(initial_free + 1) == 0 &&
                       phys_alloc_pages(UINT64_MAX) == 0 &&
                       physmem_free_pages() == initial_free,
                       "zero, oversized and overflowing requests fail without touching bitmap");

    void *pages = phys_alloc_pages(2);
    failures += !check(pages != 0 &&
                       physmem_free_pages() == initial_free - 2,
                       "valid contiguous allocation reserves exactly requested pages");
    if (pages) {
        phys_free_page(pages);
        phys_free_page((uint8_t *)pages + PAGE_SIZE);
    }
    failures += !check(physmem_free_pages() == initial_free,
                       "free returns contiguous pages to allocator");

    void *blocker = phys_alloc_page();
    void *run = phys_alloc_pages(3);
    failures += !check(blocker != 0 && run != 0 &&
                       (uint8_t *)run == (uint8_t *)blocker + PAGE_SIZE,
                       "linear scan finds the next contiguous run after fragmentation");
    if (run) {
        for (uint64_t i = 0; i < 3; i++)
            phys_free_page((uint8_t *)run + i * PAGE_SIZE);
    }
    if (blocker) phys_free_page(blocker);
    failures += !check(physmem_free_pages() == initial_free,
                       "fragmented allocation cleanup restores every page");

    void *mapped_page = phys_alloc_page();
    int mapping_added = mapped_page &&
        phys_mapping_add(mapped_page, 1, 0);
    failures += !check(mapping_added &&
                       !phys_mapping_add(mapped_page, 0, 1),
                       "physical mapping tracker rejects writable/executable aliases");
    if (mapped_page) {
        phys_free_page(mapped_page);
        failures += !check(phys_get_ref(mapped_page) == 1,
                           "mapped physical pages cannot be freed before unmapping");
        phys_mapping_remove(mapped_page, 1, 0);
        phys_free_page(mapped_page);
    }
    failures += !check(physmem_free_pages() == initial_free,
                       "mapping tracker cleanup restores the allocator");
    return failures != 0;
}
