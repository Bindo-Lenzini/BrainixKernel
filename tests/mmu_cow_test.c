#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mmu.h"
#include "physmem.h"

#define TEST_PAGE_LIMIT 128
#define PTE_VALID       (1ull << 0)
#define PTE_AP_RDONLY   (1ull << 7)
#define PTE_SW_COW      (1ull << 55)
#define PTE_PA_MASK     0x0000FFFFFFFFF000ULL

typedef struct {
    uint8_t *address;
    uint16_t refs;
} test_page_t;

static test_page_t pages[TEST_PAGE_LIMIT];
static int fail_after = -1;

void *phys_alloc_page(void) {
    if (fail_after == 0) {
        fail_after = -1;
        return NULL;
    }
    if (fail_after > 0) fail_after--;

    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address) continue;
        void *page = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
        if (!page) return NULL;
        memset(page, 0, PAGE_SIZE);
        pages[i].address = page;
        pages[i].refs = 1;
        return page;
    }
    return NULL;
}

void phys_free_page(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address != address) continue;
        if (pages[i].refs > 1) {
            pages[i].refs--;
        } else {
            free(pages[i].address);
            pages[i].address = NULL;
            pages[i].refs = 0;
        }
        return;
    }
}

void phys_inc_ref(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address == address && pages[i].refs != UINT16_MAX) {
            pages[i].refs++;
            return;
        }
    }
}

uint16_t phys_get_ref(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address == address) return pages[i].refs;
    }
    return 0;
}

static int active_pages(void) {
    int count = 0;
    for (int i = 0; i < TEST_PAGE_LIMIT; i++)
        if (pages[i].address) count++;
    return count;
}

static uint64_t *leaf_entry(uint64_t l0_pa, uint64_t va) {
    uint64_t *table = PA_TO_KVA(l0_pa);
    for (int level = 0; level < 3; level++) {
        uint64_t index = (va >> (39 - 9 * level)) & 0x1FF;
        if (!(table[index] & PTE_VALID)) return NULL;
        table = PA_TO_KVA(table[index] & PTE_PA_MASK);
    }
    uint64_t index = (va >> 12) & 0x1FF;
    return &table[index];
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
    const uint64_t va = USER_CODE_BASE;
    uint64_t parent_l0 = mmu_create_user_as();
    uint8_t *original_page = phys_alloc_page();
    int passed = 1;

    if (!parent_l0 || !original_page) {
        fprintf(stderr, "FAIL: test fixture allocation\n");
        return 1;
    }
    original_page[0] = 0x5A;
    if (!mmu_map_user_page_prot(parent_l0, va, KVA_TO_PA(original_page),
                                VM_READ | VM_WRITE, 1)) {
        fprintf(stderr, "FAIL: map writable source page\n");
        return 1;
    }

    uint64_t *parent_entry = leaf_entry(parent_l0, va);
    if (!parent_entry) {
        fprintf(stderr, "FAIL: source page table lookup\n");
        return 1;
    }
    uint64_t original_entry = *parent_entry;
    int baseline_pages = active_pages();
    passed &= check(phys_get_ref(original_page) == 1,
                    "source page starts with one reference");

    for (int successful_allocations = 0; successful_allocations < 3;
         successful_allocations++) {
        uint64_t child_l0 = mmu_create_user_as();
        if (!child_l0) {
            fprintf(stderr, "FAIL: child root allocation\n");
            return 1;
        }
        fail_after = successful_allocations;
        int cloned = mmu_clone_user_as_cow(parent_l0, 1, child_l0, 2);
        fail_after = -1;
        passed &= check(!cloned, "COW clone reports intermediate allocation failure");
        passed &= check(active_pages() == baseline_pages,
                        "failed COW clone releases all partial tables");
        passed &= check(*parent_entry == original_entry &&
                        phys_get_ref(original_page) == 1,
                        "failed COW clone leaves parent mappings unchanged");
    }

    uint64_t child_l0 = mmu_create_user_as();
    if (!child_l0) {
        fprintf(stderr, "FAIL: saturation child root allocation\n");
        return 1;
    }
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address == original_page) pages[i].refs = UINT16_MAX;
    }
    int saturated_clone = mmu_clone_user_as_cow(parent_l0, 1, child_l0, 2);
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address == original_page) pages[i].refs = 1;
    }
    passed &= check(!saturated_clone && active_pages() == baseline_pages,
                    "COW clone rejects saturated refcount without leaking tables");
    passed &= check(*parent_entry == original_entry,
                    "refcount overflow rejection leaves parent writable");

    child_l0 = mmu_create_user_as();
    if (!child_l0 ||
        !mmu_clone_user_as_cow(parent_l0, 1, child_l0, 2)) {
        fprintf(stderr, "FAIL: successful COW clone setup\n");
        return 1;
    }
    uint64_t shared_entry = *parent_entry;
    passed &= check((shared_entry & (PTE_AP_RDONLY | PTE_SW_COW)) ==
                    (PTE_AP_RDONLY | PTE_SW_COW) &&
                    phys_get_ref(original_page) == 2,
                    "successful COW clone shares and protects writable page");

    int shared_pages = active_pages();
    fail_after = 0;
    int fault_handled = mmu_cow_fault(parent_l0, 1, va);
    fail_after = -1;
    passed &= check(!fault_handled && active_pages() == shared_pages,
                    "COW fault reports copy allocation failure without leaking");
    passed &= check(*parent_entry == shared_entry &&
                    phys_get_ref(original_page) == 2 &&
                    original_page[0] == 0x5A,
                    "failed COW fault preserves shared mapping and contents");

    passed &= check(mmu_cow_fault(parent_l0, 1, va),
                    "COW fault succeeds when allocation becomes available");
    uint64_t copied_entry = *parent_entry;
    uint8_t *copied_page = PA_TO_KVA(copied_entry & PTE_PA_MASK);
    passed &= check((copied_entry & PTE_SW_COW) == 0 &&
                    copied_page != original_page && copied_page[0] == 0x5A &&
                    phys_get_ref(original_page) == 1,
                    "successful COW fault copies data and drops shared reference");

    mmu_destroy_user_as(child_l0, 2);
    mmu_destroy_user_as(parent_l0, 1);
    passed &= check(active_pages() == 0,
                    "COW test teardown releases all address-space pages");
    return passed ? 0 : 1;
}
