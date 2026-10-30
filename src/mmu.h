// BrainixKernel — mmu.h
#ifndef BRAINIX_MMU_H
#define BRAINIX_MMU_H

#include <stdint.h>

// Смещение ядра: KVA = PA + KERNEL_VA_OFFSET (верхняя половина VA).
#define KERNEL_VA_OFFSET  0xFFFF000000000000ULL

#define PA_TO_KVA(pa)  ((void *)((uint64_t)(pa) + KERNEL_VA_OFFSET))
#define KVA_TO_PA(kva) ((uint64_t)(kva) - KERNEL_VA_OFFSET)

// User VA layout (нижняя половина, TTBR0).
#define USER_CODE_BASE    0x0000000000010000ULL
#define USER_STACK_TOP    0x000000007FFFF000ULL
#define USER_STACK_SIZE   0x0000000000004000ULL   // 16 KB (4 страницы)

// Верхняя граница user VA. Всё, что выше — kernel VA (TTBR1).
#define USER_VA_LIMIT     0x0000800000000000ULL

// Heap user-задачи.
#define USER_HEAP_BASE    0x0000000001000000ULL   // 16 MB
#define USER_HEAP_MAX     0x0000000040000000ULL   // 1 GB (stack выше)

#define VM_READ  1u
#define VM_WRITE 2u
#define VM_EXEC  4u

// ---------- ASID (Address Space ID) ----------
// 8-битный ASID (TCR_EL1.AS = 0 по умолчанию).
// Значение 0 зарезервировано для kernel (TTBR0 не используется).
#define ASID_USER_FIRST  1
#define ASID_MAX         255

// Выдать новый ASID. Возвращает 0, если исчерпаны.
uint16_t mmu_alloc_asid(void);

// Установить TTBR0 для задачи с заданным ASID. Без TLB flush:
// записи TLB, помеченные этим ASID, остаются валидными.
void mmu_switch_ttbr0(uint64_t l0_pa, uint16_t asid);

// Сбросить TLB только для одного ASID.
void mmu_flush_asid(uint16_t asid);

// Инициализация MMU.
void mmu_init(void);

// Создаёт пустую L0 для user-задачи. Возвращает PA.
uint64_t mmu_create_user_as(void);

// Мапит одну 4 KB страницу в TTBR0 user-задачи.
// is_code=1 → RX (UXN=0, writable=0). is_code=0 → RW+UXN.
void mmu_map_user_page(uint64_t l0_pa, uint64_t va, uint64_t pa,
                       int is_code, uint16_t asid);

// prot: VM_READ | VM_WRITE | VM_EXEC. W^X: EXEC снимает WRITE.
int mmu_map_user_page_prot(uint64_t l0_pa, uint64_t va, uint64_t pa,
                           uint32_t prot, uint16_t asid);

int  mmu_pan_supported(void);
void mmu_pan_enable(void);
void mmu_pan_off(void);
void mmu_pan_on(void);

// Клонирует user AS: все валидные страницы из src_l0_pa копируются
// в новые физические страницы и мапятся в dst_l0_pa. Используется fork.
// Возвращает 1/0.
int mmu_clone_user_as(uint64_t src_l0_pa, uint64_t dst_l0_pa, uint16_t dst_asid);

// COW-клон: листовые страницы шарим (refcount++), промежуточные
// таблицы копируем. Всё шаримое помечается read-only.
//
// src_asid и dst_asid нужны, чтобы инвалидировать TLB **обоих**
// процессов: после клона PTE родителя тоже стали RO, но его TLB
// ещё помнит RW — без flush родитель писал бы в общую страницу
// без fault'а.
int mmu_clone_user_as_cow(uint64_t src_l0_pa, uint16_t src_asid,
                          uint64_t dst_l0_pa, uint16_t dst_asid);

// Обработчик Data Abort (write на shared RO page).
//   ttbr0_pa — AS задачи
//   asid     — ASID задачи
//   va       — fault-адрес (FAR_EL1)
// Возврат 1, если страница разрешена (можно retry). 0 — не COW.
int mmu_cow_fault(uint64_t ttbr0_pa, uint16_t asid, uint64_t va);

uint64_t mmu_get_empty_l0_pa(void);

#endif