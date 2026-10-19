// BrainixKernel (BKOS) — mmu.c
//
// TTBR1 (верхняя половина): linear map kernel-адресов. KVA = PA + offset.
// TTBR0 (нижняя половина): per-task. Каждой user-задаче выдаётся
// уникальный ASID, хранимый в TTBR0_EL1[63:48]. Переключение задач
// не требует полного TLB flush — при условии, что user-страницы
// помечены nG=1 (иначе TLB-записи глобальные и не тегируются ASID).

#include <stdint.h>
#include "mmu.h"
#include "physmem.h"

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void)    { __asm__ volatile("isb"    ::: "memory"); }

// ---------- TCR_EL1 ----------
#define TCR_T0SZ_16     (16ull)
#define TCR_T1SZ_16     (16ull << 16)
#define TCR_IRGN0_WBWA  (1ull << 8)
#define TCR_ORGN0_WBWA  (1ull << 10)
#define TCR_SH0_IS      (3ull << 12)
#define TCR_TG0_4K      (0ull << 14)
#define TCR_IRGN1_WBWA  (1ull << 24)
#define TCR_ORGN1_WBWA  (1ull << 26)
#define TCR_SH1_IS      (3ull << 28)
#define TCR_TG1_4K      (2ull << 30)
#define TCR_IPS_40BIT   (2ull << 32)

// A1=0 → ASID берётся из TTBR0_EL1[63:48]. AS=0 → 8-битный ASID.
#define TCR_VALUE ( \
    TCR_T0SZ_16 | TCR_T1SZ_16 | \
    TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_IS | TCR_TG0_4K | \
    TCR_IRGN1_WBWA | TCR_ORGN1_WBWA | TCR_SH1_IS | TCR_TG1_4K | \
    TCR_IPS_40BIT )

#define MAIR_VALUE 0xFFull

// ---------- Descriptor bits ----------
#define PTE_VALID       (1ull << 0)
#define PTE_BIT1        (1ull << 1)
#define PTE_ATTR_IDX(n) ((uint64_t)(n) << 2)
#define PTE_AP_RDONLY   (1ull << 7)    // AP[2]: 1 = read-only
#define PTE_SH_IS       (3ull << 8)
#define PTE_AF          (1ull << 10)
#define PTE_NG          (1ull << 11)   // not-global: TLB-entry тегируется ASID
#define PTE_PXN         (1ull << 53)
#define PTE_UXN         (1ull << 54)

#define PTE_AP_KERNEL_RW   (0ull << 6)
#define PTE_AP_USER_RW     (1ull << 6)
#define PTE_AP_KERNEL_RO   (2ull << 6)
#define PTE_AP_USER_RO     (3ull << 6)

// Маска «полезных» флагов листа: биты 0..11 (valid..nG) + 52..54
// (Contiguous, PXN, UXN). Используется при COW, чтобы не терять
// W^X-биты. PA сюда не входит — её берём отдельной маской.
#define PTE_LEAF_FLAGS_MASK  (0xFFFull | (7ull << 52))
#define PTE_LEAF_PA_MASK     0x0000FFFFFFFFF000ULL

#define DESC_TABLE       (PTE_VALID | PTE_BIT1)

#define DESC_BLOCK_DEVICE \
    (PTE_VALID | PTE_AF | PTE_AP_KERNEL_RW | \
     PTE_ATTR_IDX(1) | PTE_PXN | PTE_UXN)

#define DESC_BLOCK_KERNEL_RW \
    (PTE_VALID | PTE_AF | PTE_SH_IS | PTE_AP_KERNEL_RW | \
     PTE_ATTR_IDX(0) | PTE_PXN | PTE_UXN)

#define DESC_PAGE_DEFAULT \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_AP_KERNEL_RW | \
     PTE_ATTR_IDX(0) | PTE_PXN | PTE_UXN)

#define DESC_PAGE_KERNEL_RX \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_AP_KERNEL_RO | \
     PTE_ATTR_IDX(0) | PTE_UXN)

#define DESC_PAGE_KERNEL_RO \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_AP_KERNEL_RO | \
     PTE_ATTR_IDX(0) | PTE_PXN | PTE_UXN)

#define DESC_PAGE_KERNEL_RW \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_AP_KERNEL_RW | \
     PTE_ATTR_IDX(0) | PTE_PXN | PTE_UXN)

// nG=1 обязателен: иначе TLB-entry глобальная и живёт для всех ASID.
#define DESC_PAGE_USER_CODE \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_ATTR_IDX(0) | \
     PTE_AP_USER_RO | PTE_PXN | PTE_NG)

#define DESC_PAGE_USER_DATA \
    (PTE_VALID | PTE_BIT1 | PTE_AF | PTE_SH_IS | PTE_ATTR_IDX(0) | \
     PTE_AP_USER_RW | PTE_PXN | PTE_UXN | PTE_NG)

#define L3_PA_BASE  0x40000000ull
#define L3_PA_END   0x44000000ull
#define L3_COUNT    32

// ---------- TTBR1 ----------
static uint64_t l0_hi[512]  __attribute__((aligned(4096)));
static uint64_t l1_hi[512]  __attribute__((aligned(4096)));
static uint64_t l2_hi_low[512]  __attribute__((aligned(4096)));
static uint64_t l2_hi_ram[512]  __attribute__((aligned(4096)));
static uint64_t l2_hi_pcie[512] __attribute__((aligned(4096)));
static uint64_t l3_hi_tables[L3_COUNT][512] __attribute__((aligned(4096)));

// ---------- TTBR0 по умолчанию (пустая) ----------
static uint64_t l0_empty[512] __attribute__((aligned(4096)));

// ---------- ASID-счётчик ----------
static uint16_t g_next_asid = ASID_USER_FIRST;

extern char __text_start,   __text_end;
extern char __rodata_start, __rodata_end;
extern char __data_start,   __data_end, __bss_end;
extern char __stack_guard_start, __stack_guard_end, __stack_top;

static inline uint64_t tbl_pa(void *p) { return KVA_TO_PA(p); }

static void mem_zero(void *p, uint64_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}

static void build_ttbr1(void) {
    l0_hi[0] = tbl_pa(l1_hi) | DESC_TABLE;

    l1_hi[0]   = tbl_pa(l2_hi_low)  | DESC_TABLE;
    l1_hi[1]   = tbl_pa(l2_hi_ram)  | DESC_TABLE;
    l1_hi[256] = tbl_pa(l2_hi_pcie) | DESC_TABLE;

    for (uint64_t i = 0; i < 512; i++) {
        l2_hi_low[i] = (i * 0x200000ull) | DESC_BLOCK_DEVICE;
    }
    for (uint64_t i = 0; i < L3_COUNT; i++) {
        l2_hi_ram[i] = tbl_pa(&l3_hi_tables[i][0]) | DESC_TABLE;
    }
    for (uint64_t i = L3_COUNT; i < 512; i++) {
        l2_hi_ram[i] = (0x40000000ull + i * 0x200000ull) | DESC_BLOCK_KERNEL_RW;
    }
    for (uint64_t t = 0; t < L3_COUNT; t++) {
        for (uint64_t i = 0; i < 512; i++) {
            uint64_t pa = L3_PA_BASE + t * 0x200000ull + i * 0x1000ull;
            l3_hi_tables[t][i] = pa | DESC_PAGE_DEFAULT;
        }
    }
    for (uint64_t i = 0; i < 512; i++) {
        l2_hi_pcie[i] = (0x4000000000ull + i * 0x200000ull) | DESC_BLOCK_DEVICE;
    }
}

static void apply_kernel_page_range(uint64_t kva_start, uint64_t kva_end,
                                    uint64_t flags) {
    if (kva_start >= kva_end) return;
    kva_start &= ~0xFFFull;
    kva_end    = (kva_end + 0xFFFull) & ~0xFFFull;

    uint64_t kva_base = L3_PA_BASE + KERNEL_VA_OFFSET;
    uint64_t kva_lim  = L3_PA_END  + KERNEL_VA_OFFSET;

    for (uint64_t kva = kva_start; kva < kva_end; kva += 0x1000) {
        if (kva < kva_base || kva >= kva_lim) continue;
        uint64_t pa  = kva - KERNEL_VA_OFFSET;
        uint64_t off = pa - L3_PA_BASE;
        uint64_t t   = off / 0x200000ull;
        uint64_t i   = (off % 0x200000ull) / 0x1000ull;
        if (t >= L3_COUNT) continue;
        l3_hi_tables[t][i] = (flags == 0) ? 0 : (pa | flags);
    }
}

static void apply_kernel_protections(void) {
    if ((uint64_t)&__stack_top >= L3_PA_END + KERNEL_VA_OFFSET) return;

    apply_kernel_page_range((uint64_t)&__text_start,
                            (uint64_t)&__text_end,
                            DESC_PAGE_KERNEL_RX);
    apply_kernel_page_range((uint64_t)&__rodata_start,
                            (uint64_t)&__rodata_end,
                            DESC_PAGE_KERNEL_RO);
    apply_kernel_page_range((uint64_t)&__data_start,
                            (uint64_t)&__bss_end,
                            DESC_PAGE_KERNEL_RW);
    apply_kernel_page_range((uint64_t)&__stack_guard_start,
                            (uint64_t)&__stack_guard_end,
                            0);
}

void mmu_init(void) {
    build_ttbr1();
    apply_kernel_protections();

    mem_zero(l0_empty, 4096);

    dsb_sy();
    isb();

    __asm__ volatile("msr mair_el1, %0" :: "r"(MAIR_VALUE));
    isb();
    __asm__ volatile("msr tcr_el1, %0" :: "r"(TCR_VALUE));
    isb();

    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(tbl_pa(l0_empty)));
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(tbl_pa(l0_hi)));
    dsb_sy();
    isb();

    __asm__ volatile("tlbi vmalle1" ::: "memory");
    dsb_sy();
    isb();
    __asm__ volatile("ic iallu" ::: "memory");
    dsb_sy();
    isb();
}

uint64_t mmu_get_empty_l0_pa(void) {
    return tbl_pa(l0_empty);
}

// ============================================================
// ASID.
// ============================================================

uint16_t mmu_alloc_asid(void) {
    if (g_next_asid > ASID_MAX) return 0;
    return g_next_asid++;
}

void mmu_switch_ttbr0(uint64_t l0_pa, uint16_t asid) {
    // TTBR0_EL1[63:48] = ASID, [47:0] = физический адрес L0.
    uint64_t ttbr0 = ((uint64_t)asid << 48) |
                     (l0_pa & 0x0000FFFFFFFFFFFFULL);
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0));
    dsb_sy();
    isb();
}

void mmu_flush_asid(uint16_t asid) {
    // tlbi aside1, Xt — invalidate TLB entries for one ASID.
    uint64_t a = asid;
    __asm__ volatile("tlbi aside1, %0" :: "r"(a) : "memory");
    dsb_sy();
    isb();
}

// ============================================================
// Per-task user AS.
// ============================================================

uint64_t mmu_create_user_as(void) {
    uint8_t *l0_kva = (uint8_t *)phys_alloc_page();
    if (!l0_kva) return 0;
    mem_zero(l0_kva, 4096);
    return KVA_TO_PA(l0_kva);
}

static inline uint64_t idx_l0(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t idx_l1(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t idx_l2(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline uint64_t idx_l3(uint64_t va) { return (va >> 12) & 0x1FF; }

static uint64_t *walk_or_alloc(uint64_t *parent, uint64_t idx) {
    if (!(parent[idx] & PTE_VALID)) {
        uint8_t *child_kva = (uint8_t *)phys_alloc_page();
        if (!child_kva) return 0;
        mem_zero(child_kva, 4096);
        parent[idx] = KVA_TO_PA(child_kva) | DESC_TABLE;
        dsb_sy();
        return (uint64_t *)child_kva;
    }
    uint64_t child_pa = parent[idx] & PTE_LEAF_PA_MASK;
    return (uint64_t *)PA_TO_KVA(child_pa);
}

void mmu_map_user_page(uint64_t l0_pa, uint64_t va, uint64_t pa,
                       int is_code, uint16_t asid) {
    if (!l0_pa) return;

    uint64_t *l0 = (uint64_t *)PA_TO_KVA(l0_pa);

    uint64_t *l1 = walk_or_alloc(l0, idx_l0(va));
    if (!l1) return;
    uint64_t *l2 = walk_or_alloc(l1, idx_l1(va));
    if (!l2) return;
    uint64_t *l3 = walk_or_alloc(l2, idx_l2(va));
    if (!l3) return;

    uint64_t flags = is_code ? DESC_PAGE_USER_CODE : DESC_PAGE_USER_DATA;
    l3[idx_l3(va)] = (pa & ~0xFFFull) | flags;

    dsb_sy();

    if (asid != 0) {
        mmu_flush_asid(asid);
    } else {
        __asm__ volatile("tlbi vmalle1" ::: "memory");
        dsb_sy();
        isb();
    }
}

// ============================================================
// Клонирование user AS.
// ============================================================

int mmu_clone_user_as(uint64_t src_l0_pa, uint64_t dst_l0_pa, uint16_t dst_asid) {
    if (!src_l0_pa || !dst_l0_pa) return 0;

    uint64_t *src_l0 = (uint64_t *)PA_TO_KVA(src_l0_pa);

    for (uint64_t i0 = 0; i0 < 512; i0++) {
        if (!(src_l0[i0] & PTE_VALID)) continue;

        uint64_t *src_l1 = (uint64_t *)PA_TO_KVA(src_l0[i0] & PTE_LEAF_PA_MASK);

        for (uint64_t i1 = 0; i1 < 512; i1++) {
            if (!(src_l1[i1] & PTE_VALID)) continue;

            uint64_t *src_l2 = (uint64_t *)PA_TO_KVA(src_l1[i1] & PTE_LEAF_PA_MASK);

            for (uint64_t i2 = 0; i2 < 512; i2++) {
                if (!(src_l2[i2] & PTE_VALID)) continue;

                uint64_t *src_l3 = (uint64_t *)PA_TO_KVA(src_l2[i2] & PTE_LEAF_PA_MASK);

                for (uint64_t i3 = 0; i3 < 512; i3++) {
                    if (!(src_l3[i3] & PTE_VALID)) continue;

                    uint64_t src_pa = src_l3[i3] & PTE_LEAF_PA_MASK;
                    uint64_t orig_flags = src_l3[i3];

                    // Определить is_code: если UXN=0 → исполняемая.
                    int is_code = !(orig_flags & PTE_UXN);

                    // VA из индексов.
                    uint64_t va = (i0 << 39) | (i1 << 30) | (i2 << 21) | (i3 << 12);

                    // Копируем страницу.
                    uint8_t *new_pg = (uint8_t *)phys_alloc_page();
                    if (!new_pg) return 0;
                    const uint8_t *old_pg = (const uint8_t *)PA_TO_KVA(src_pa);
                    for (int k = 0; k < (int)PAGE_SIZE; k++) new_pg[k] = old_pg[k];

                    mmu_map_user_page(dst_l0_pa, va, KVA_TO_PA(new_pg),
                                      is_code, dst_asid);
                }
            }
        }
    }
    return 1;
}

// ============================================================
// COW-клон AS.
// ============================================================

// Проходим по таблицам src, копируем промежуточные, листья — шарим.
// Возврат 1/0.
static int cow_walk(uint64_t *src, uint64_t *dst, int level) {
    for (int i = 0; i < 512; i++) {
        uint64_t entry = src[i];
        if (!(entry & PTE_VALID)) continue;

        if (level == 3) {
            // Leaf page. Share PA, mark RO.
            uint64_t pa = entry & PTE_LEAF_PA_MASK;
            // Сохраняем ВСЕ флаги, включая PXN/UXN — иначе COW
            // снимает W^X и делает все страницы исполняемыми для EL0.
            uint64_t flags = entry & PTE_LEAF_FLAGS_MASK;

            uint64_t new_entry = pa | flags | PTE_AP_RDONLY;

            src[i] = new_entry;   // родитель тоже RO
            dst[i] = new_entry;   // ребёнок видит ту же PA

            phys_inc_ref((void *)PA_TO_KVA(pa));
        } else {
            // Промежуточная таблица. Копируем (deep), чтобы у ребёнка
            // были свои L1/L2/L3 — иначе мутация одной таблицы
            // затронет обоих.
            uint8_t *new_tbl = (uint8_t *)phys_alloc_page();
            if (!new_tbl) return 0;
            for (int k = 0; k < (int)PAGE_SIZE; k++) new_tbl[k] = 0;

            uint64_t new_pa = KVA_TO_PA(new_tbl);
            dst[i] = new_pa | DESC_TABLE;

            uint64_t *src_child = (uint64_t *)PA_TO_KVA(entry & PTE_LEAF_PA_MASK);
            uint64_t *dst_child = (uint64_t *)new_tbl;

            if (!cow_walk(src_child, dst_child, level + 1)) return 0;
        }
    }
    return 1;
}

int mmu_clone_user_as_cow(uint64_t src_l0_pa, uint16_t src_asid,
                          uint64_t dst_l0_pa, uint16_t dst_asid)
{
    if (!src_l0_pa || !dst_l0_pa) return 0;

    uint64_t *src_l0 = (uint64_t *)PA_TO_KVA(src_l0_pa);
    uint64_t *dst_l0 = (uint64_t *)PA_TO_KVA(dst_l0_pa);

    int ok = cow_walk(src_l0, dst_l0, 0);

    dsb_sy();

    // cow_walk мутирует PTE родителя (меняет на RO). Даже если ok==0,
    // часть страниц уже могла стать RO — надо флашить оба ASID.
    // Без флаша родителя его TLB помнит RW → запись пройдёт без
    // COW-fault'а → оба процесса пишут в одну физическую страницу.
    mmu_flush_asid(src_asid);
    mmu_flush_asid(dst_asid);

    return ok;
}

// ============================================================
// COW fault.
// ============================================================

int mmu_cow_fault(uint64_t ttbr0_pa, uint16_t asid, uint64_t va) {
    if (!ttbr0_pa) return 0;

    uint64_t *l0 = (uint64_t *)PA_TO_KVA(ttbr0_pa);
    uint64_t i0 = idx_l0(va);

    if (!(l0[i0] & PTE_VALID)) return 0;
    uint64_t *l1 = (uint64_t *)PA_TO_KVA(l0[i0] & PTE_LEAF_PA_MASK);
    uint64_t i1 = idx_l1(va);

    if (!(l1[i1] & PTE_VALID)) return 0;
    uint64_t *l2 = (uint64_t *)PA_TO_KVA(l1[i1] & PTE_LEAF_PA_MASK);
    uint64_t i2 = idx_l2(va);

    if (!(l2[i2] & PTE_VALID)) return 0;
    uint64_t *l3 = (uint64_t *)PA_TO_KVA(l2[i2] & PTE_LEAF_PA_MASK);
    uint64_t i3 = idx_l3(va);

    uint64_t entry = l3[i3];
    if (!(entry & PTE_VALID)) return 0;

    uint64_t pa = entry & PTE_LEAF_PA_MASK;
    uint64_t flags = entry & PTE_LEAF_FLAGS_MASK;

    // Если RO, но refcount == 1 → просто восстановить RW.
    uint16_t ref = phys_get_ref((void *)PA_TO_KVA(pa));

    if (ref <= 1) {
        // Не shared. Снимаем RO.
        uint64_t new_entry = pa | (flags & ~PTE_AP_RDONLY);
        l3[i3] = new_entry;
        dsb_sy();
        mmu_flush_asid(asid);
        return 1;
    }

    // Shared. Копируем страницу.
    uint8_t *new_pg = (uint8_t *)phys_alloc_page();
    if (!new_pg) return 0;

    const uint8_t *old_pg = (const uint8_t *)PA_TO_KVA(pa);
    for (int k = 0; k < (int)PAGE_SIZE; k++) new_pg[k] = old_pg[k];

    uint64_t new_pa = KVA_TO_PA(new_pg);

    // Декремент старого refcount.
    phys_free_page((void *)PA_TO_KVA(pa));

    // Обновляем PTE: новая PA, RW.
    uint64_t new_entry = new_pa | (flags & ~PTE_AP_RDONLY);
    l3[i3] = new_entry;

    dsb_sy();
    mmu_flush_asid(asid);
    return 1;
}
