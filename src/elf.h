// BrainixKernel (BKOS) — elf.h
// Парсер ELF64 EXEC для AArch64. Только для user-программ:
//   * Class: ELF64
//   * Data: little-endian
//   * Type: ET_EXEC (не PIE/DYN)
//   * Machine: EM_AARCH64
//
// Program headers не обязаны иметь секции .data/.bss — работаем
// только с PT_LOAD сегментами.

#ifndef BKOS_ELF_H
#define BKOS_ELF_H

#include <stdint.h>

// Максимум PT_LOAD сегментов в одном ELF.
// Наши простые программы укладываются в 1-2, но с запасом.
#define ELF_MAX_SEGMENTS 8

// Флаги p_flags (совпадают с ELF spec).
#define ELF_PF_X 0x1
#define ELF_PF_W 0x2
#define ELF_PF_R 0x4

typedef struct {
    uint64_t vaddr;      // user virtual address (нижняя половина)
    uint64_t memsz;      // размер в памяти (>= filesz)
    uint64_t filesz;     // сколько байт копировать из файла
    uint64_t offset;     // смещение в ELF-файле
    uint32_t flags;      // ELF_PF_R | ELF_PF_W | ELF_PF_X
} elf_segment_t;

typedef struct {
    uint64_t      entry;                        // e_entry
    int           num_segments;
    elf_segment_t segments[ELF_MAX_SEGMENTS];
} elf_info_t;

// Разбирает ELF64 EXEC для AArch64. Возвращает 1 при успехе.
// out->num_segments установлен, segments заполнены (только PT_LOAD).
// При ошибке возвращает 0 и пишет причину в elf_last_error().
int elf_parse(const uint8_t *data, uint32_t len, elf_info_t *out);

// Причина последнего провала elf_parse(). Пустая = успех.
const char *elf_last_error(void);

#endif
