// BrainixKernel — elf.c
// Разбор ELF64 EXEC для AArch64.

#include <stdint.h>
#include "elf.h"
#include "mmu.h"

#define EI_NIDENT 16

// ELF64 header (без libc: pack'ed вручную, без x86-специфики).
typedef struct __attribute__((packed)) {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

// ELF64 program header.
typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

// Значения полей.
#define ET_EXEC          2
#define EM_AARCH64       183
#define PT_LOAD          1
#define ELFCLASS64       2
#define ELFDATA2LSB      1

// Последняя ошибка.
static char g_elf_err[100] = {0};

static void set_err(const char *s) {
    int i = 0;
    while (s[i] && i < 99) { g_elf_err[i] = s[i]; i++; }
    g_elf_err[i] = 0;
}

const char *elf_last_error(void) { return g_elf_err; }

int elf_parse(const uint8_t *data, uint32_t len, elf_info_t *out) {
    g_elf_err[0] = 0;

    if (!data || !out) {
        set_err("null pointer");
        return 0;
    }
    if (len < sizeof(elf64_ehdr_t)) {
        set_err("file too small");
        return 0;
    }

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)data;

    // Магия.
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        set_err("not ELF (bad magic)");
        return 0;
    }
    if (eh->e_ident[4] != ELFCLASS64) {
        set_err("not ELF64");
        return 0;
    }
    if (eh->e_ident[5] != ELFDATA2LSB) {
        set_err("not little-endian");
        return 0;
    }
    if (eh->e_ident[6] != 1 || eh->e_version != 1 ||
        eh->e_ehsize < sizeof(elf64_ehdr_t)) {
        set_err("unsupported ELF version or header size");
        return 0;
    }
    if (eh->e_type != ET_EXEC) {
        set_err("not ET_EXEC (PIE/DYN not supported)");
        return 0;
    }
    if (eh->e_machine != EM_AARCH64) {
        set_err("not AArch64");
        return 0;
    }
    if (eh->e_phentsize != sizeof(elf64_phdr_t)) {
        set_err("bad e_phentsize");
        return 0;
    }
    if (eh->e_phnum > ELF_MAX_SEGMENTS) {
        set_err("too many program headers");
        return 0;
    }
    if (eh->e_phoff > len ||
        eh->e_phnum > (len - eh->e_phoff) / eh->e_phentsize) {
        set_err("e_phoff out of range");
        return 0;
    }

    out->entry = eh->e_entry;
    out->num_segments = 0;

    for (int i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)
            (data + eh->e_phoff + (uint64_t)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        // Проверки сегмента.
        if (ph->p_filesz > ph->p_memsz) {
            set_err("segment: filesz > memsz");
            return 0;
        }
        if (ph->p_offset > len || ph->p_filesz > len - ph->p_offset) {
            set_err("segment: out of file");
            return 0;
        }
        if (ph->p_memsz == 0) continue;

        // Сегмент должен лежать в user VA (нижняя половина).
        if (ph->p_vaddr < USER_CODE_BASE || ph->p_vaddr >= USER_VA_LIMIT) {
            set_err("segment: vaddr in kernel range");
            return 0;
        }
        if (ph->p_memsz > USER_VA_LIMIT - ph->p_vaddr) {
            set_err("segment: crosses user VA limit");
            return 0;
        }

        if ((ph->p_flags & ~(ELF_PF_R | ELF_PF_W | ELF_PF_X)) ||
            !(ph->p_flags & ELF_PF_R)) {
            set_err("segment: unsupported permission flags");
            return 0;
        }
        if ((ph->p_flags & (ELF_PF_W | ELF_PF_X)) ==
            (ELF_PF_W | ELF_PF_X)) {
            set_err("segment: writable and executable");
            return 0;
        }

        if (ph->p_align > 1 &&
            ((ph->p_align & (ph->p_align - 1)) ||
             (ph->p_vaddr & (ph->p_align - 1)) !=
             (ph->p_offset & (ph->p_align - 1)))) {
            set_err("segment: invalid alignment");
            return 0;
        }

        // vaddr должен быть выровнен на 4 KB (мы мапим постранично).
        if (ph->p_vaddr & 0xFFFull) {
            set_err("segment: vaddr not page-aligned");
            return 0;
        }

        uint64_t segment_end = ph->p_vaddr + ph->p_memsz;
        uint64_t page_end = (segment_end + 0xFFF) & ~0xFFFull;
        uint64_t page_start = ph->p_vaddr;
        if ((page_start < USER_HEAP_MAX && page_end > USER_HEAP_BASE) ||
            (page_start < USER_STACK_TOP &&
             page_end > USER_STACK_TOP - USER_STACK_SIZE)) {
            set_err("segment: overlaps reserved heap or stack");
            return 0;
        }

        for (int j = 0; j < out->num_segments; j++) {
            const elf_segment_t *prior = &out->segments[j];
            uint64_t prior_end = prior->vaddr + prior->memsz;
            uint64_t prior_page_end = (prior_end + 0xFFF) & ~0xFFFull;
            if (page_start < prior_page_end && prior->vaddr < page_end) {
                set_err("segments: overlapping pages");
                return 0;
            }
        }

        if (out->num_segments >= ELF_MAX_SEGMENTS) {
            set_err("too many PT_LOAD segments");
            return 0;
        }

        elf_segment_t *s = &out->segments[out->num_segments++];
        s->vaddr  = ph->p_vaddr;
        s->memsz  = ph->p_memsz;
        s->filesz = ph->p_filesz;
        s->offset = ph->p_offset;
        s->flags  = ph->p_flags;
    }

    if (out->num_segments == 0) {
        set_err("no PT_LOAD segments");
        return 0;
    }

    int entry_is_executable = 0;
    for (int i = 0; i < out->num_segments; i++) {
        const elf_segment_t *s = &out->segments[i];
        if ((s->flags & ELF_PF_X) && out->entry >= s->vaddr &&
            out->entry - s->vaddr < s->memsz) {
            entry_is_executable = 1;
            break;
        }
    }
    if (!entry_is_executable) {
        set_err("entry is not inside an executable segment");
        return 0;
    }

    return 1;
}