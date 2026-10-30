// BrainixKernel — task.c

#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "heap.h"
#include "physmem.h"
#include "mmu.h"
#include "elf.h"
#include "pipe.h"

#define FRAME_SIZE 0x120

static uint64_t g_next_id = 1;

extern void uart_puts(const char *s);

static void copy_name(task_t *t, const char *name) {
    int i = 0;
    if (name) {
        while (name[i] && i < TASK_NAME_MAX - 1) {
            t->name[i] = name[i];
            i++;
        }
    }
    t->name[i] = '\0';
}

static void fds_init(task_fd_t *fds) {
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        fds[i].used       = 0;
        fds[i].src        = TFD_UNUSED;
        fds[i].flags      = 0;
        fds[i].offset     = 0;
        fds[i].file       = 0;
        fds[i].bkfs_inode = -1;
        fds[i].pipe_idx   = -1;
        fds[i].sock_idx   = -1;
        fds[i].tcp_idx    = -1;
    }
}

static task_t *alloc_task(const char *name,
                          uint8_t **kstack_out,
                          uint64_t *frame_sp_out) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return 0;

    t->cred = cred_alloc_unprivileged();
    if (!t->cred) { kfree(t); return 0; }

    uint64_t kpages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *kstack_kva = (uint8_t *)phys_alloc_pages(kpages);
    if (!kstack_kva) { cred_free(t->cred); kfree(t); return 0; }

    copy_name(t, name);
    t->id         = g_next_id++;
    t->state      = TASK_READY;
    t->next       = 0;
    t->to_window  = 0;
    t->ttbr0_pa   = 0;
    t->asid       = 0;
    t->heap_end   = 0;
    t->parent_id  = 0;
    t->exit_code  = 0;
    fds_init(t->fds);

    uint64_t kstack_top = (uint64_t)(kstack_kva + kpages * PAGE_SIZE);
    uint64_t frame_sp   = (kstack_top - FRAME_SIZE) & ~0xFull;

    uint64_t *f = (uint64_t *)frame_sp;
    for (int k = 0; k < (int)(FRAME_SIZE / 8); k++) f[k] = 0;

    *kstack_out   = kstack_kva;
    *frame_sp_out = frame_sp;
    return t;
}

task_t *task_create(const char *name, void (*fn)(void)) {
    uint8_t  *kstack;
    uint64_t  frame_sp;
    task_t *t = alloc_task(name, &kstack, &frame_sp);
    if (!t) return 0;

    uint64_t *f = (uint64_t *)frame_sp;
    f[0x100 / 8] = (uint64_t)fn;
    f[0x108 / 8] = SPSR_EL1H_UNMASKED;

    t->sp_saved = frame_sp;
    return t;
}

task_t *task_create_user(const char *name, const void *code, unsigned long code_size) {
    uint8_t  *kstack;
    uint64_t  frame_sp;
    task_t *t = alloc_task(name, &kstack, &frame_sp);
    if (!t) return 0;

    uint8_t *code_page_kva = (uint8_t *)phys_alloc_page();
    if (!code_page_kva) { cred_free(t->cred); kfree(t); return 0; }

    uint8_t *ustack_kva = (uint8_t *)phys_alloc_page();
    if (!ustack_kva) { cred_free(t->cred); kfree(t); return 0; }

    uint64_t code_pa   = KVA_TO_PA(code_page_kva);
    uint64_t ustack_pa = KVA_TO_PA(ustack_kva);

    if (code_size > PAGE_SIZE) code_size = PAGE_SIZE;
    for (unsigned long i = 0; i < code_size; i++) {
        code_page_kva[i] = ((const uint8_t *)code)[i];
    }

    uint64_t l0_pa = mmu_create_user_as();
    if (!l0_pa) { cred_free(t->cred); kfree(t); return 0; }

    uint16_t asid = mmu_alloc_asid();
    if (asid == 0) { cred_free(t->cred); kfree(t); return 0; }

    mmu_map_user_page(l0_pa, USER_CODE_BASE, code_pa, /*is_code=*/1, asid);
    mmu_map_user_page(l0_pa, USER_STACK_TOP - PAGE_SIZE, ustack_pa, /*is_code=*/0, asid);

    t->ttbr0_pa = l0_pa;
    t->asid     = asid;
    t->heap_end = USER_HEAP_BASE;

    uint64_t *f = (uint64_t *)frame_sp;
    f[0x100 / 8] = USER_CODE_BASE;
    f[0x108 / 8] = SPSR_EL0T_UNMASKED;
    f[0x110 / 8] = USER_STACK_TOP;

    t->sp_saved = frame_sp;
    return t;
}

static void *ustack_kva_for(const uint64_t *stack_pas, uint64_t stack_base,
                            uint64_t va)
{
    if (va < stack_base || va >= USER_STACK_TOP) return 0;
    uint64_t off = va - stack_base;
    uint64_t page_idx = off / PAGE_SIZE;
    uint64_t in_page = off % PAGE_SIZE;
    return (void *)PA_TO_KVA(stack_pas[page_idx] + in_page);
}

static void ustack_write(const uint64_t *stack_pas, uint64_t stack_base,
                         uint64_t va, const void *data, uint64_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    for (uint64_t i = 0; i < len; i++) {
        void *dst = ustack_kva_for(stack_pas, stack_base, va + i);
        if (dst) *(volatile uint8_t *)dst = src[i];
    }
}

static void ustack_write_u64(const uint64_t *stack_pas, uint64_t stack_base,
                             uint64_t va, uint64_t val)
{
    ustack_write(stack_pas, stack_base, va, &val, 8);
}

int task_load_elf_into(uint64_t l0_pa, uint16_t asid,
                       const void *elf, uint32_t elf_size,
                       elf_info_t *out_info)
{
    if (!elf_parse(elf, elf_size, out_info)) {
        uart_puts("[elf] parse failed: ");
        uart_puts(elf_last_error());
        uart_puts("\n");
        return 0;
    }

    const uint8_t *elf_bytes = (const uint8_t *)elf;

    for (int i = 0; i < out_info->num_segments; i++) {
        const elf_segment_t *s = &out_info->segments[i];
        uint32_t prot = VM_READ;
        if (s->flags & ELF_PF_W) prot |= VM_WRITE;
        if (s->flags & ELF_PF_X) prot |= VM_EXEC;

        uint64_t pages = (s->memsz + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t p = 0; p < pages; p++) {
            uint8_t *pg = (uint8_t *)phys_alloc_page();
            if (!pg) return 0;

            uint64_t off_in_seg = p * PAGE_SIZE;
            uint64_t copy_len = 0;
            if (off_in_seg < s->filesz) {
                copy_len = s->filesz - off_in_seg;
                if (copy_len > PAGE_SIZE) copy_len = PAGE_SIZE;
            }
            for (uint64_t j = 0; j < copy_len; j++) {
                pg[j] = elf_bytes[s->offset + off_in_seg + j];
            }
            for (uint64_t j = copy_len; j < PAGE_SIZE; j++) pg[j] = 0;

            if (!mmu_map_user_page_prot(l0_pa, s->vaddr + off_in_seg,
                                        KVA_TO_PA(pg), prot, asid)) {
                phys_free_page(pg);
                return 0;
            }
        }
    }
    return 1;
}

int task_setup_user_stack(uint64_t l0_pa, uint16_t asid,
                          int argc, const char *const *argv,
                          uint64_t *out_sp, uint64_t *out_argv)
{
    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    if (stack_pages == 0) stack_pages = 1;
    if (stack_pages > 16) stack_pages = 16;

    uint64_t stack_base = USER_STACK_TOP - stack_pages * PAGE_SIZE;
    uint64_t stack_pas[16];

    for (uint64_t p = 0; p < stack_pages; p++) {
        uint8_t *pg = (uint8_t *)phys_alloc_page();
        if (!pg) return 0;
        for (int j = 0; j < (int)PAGE_SIZE; j++) pg[j] = 0;
        stack_pas[p] = KVA_TO_PA(pg);
        mmu_map_user_page(l0_pa, stack_base + p * PAGE_SIZE,
                          stack_pas[p], 0, asid);
    }

    if (argc < 0) argc = 0;
    if (argc > TASK_MAX_ARGC) argc = TASK_MAX_ARGC;

    uint64_t sp = USER_STACK_TOP;
    uint64_t argv_user = 0;

    if (argc > 0) {
        uint64_t string_addrs[TASK_MAX_ARGC];

        for (int i = argc - 1; i >= 0; i--) {
            const char *s = (argv && argv[i]) ? argv[i] : "";
            uint64_t len = 0;
            while (s[len]) len++;
            len += 1;

            sp -= len;
            ustack_write(stack_pas, stack_base, sp, s, len);
            string_addrs[i] = sp;
        }

        sp &= ~0xFULL;
        sp -= 8;
        ustack_write_u64(stack_pas, stack_base, sp, 0);

        for (int i = argc - 1; i >= 0; i--) {
            sp -= 8;
            ustack_write_u64(stack_pas, stack_base, sp, string_addrs[i]);
        }
        argv_user = sp;
    }
    if (argc == 0) sp = USER_STACK_TOP - 16;

    *out_sp = sp;
    *out_argv = argv_user;
    return 1;
}

static task_t *create_from_elf_common(const char *name,
                                       const void *elf, uint32_t elf_size,
                                       int argc, const char *const *argv)
{
    uint8_t  *kstack;
    uint64_t  frame_sp;
    task_t *t = alloc_task(name, &kstack, &frame_sp);
    if (!t) return 0;

    uint64_t l0_pa = mmu_create_user_as();
    if (!l0_pa) { kfree(t); return 0; }

    uint16_t asid = mmu_alloc_asid();
    if (asid == 0) { kfree(t); return 0; }

    elf_info_t info;
    if (!task_load_elf_into(l0_pa, asid, elf, elf_size, &info)) {
        kfree(t);
        return 0;
    }

    uint64_t sp, argv_user;
    if (!task_setup_user_stack(l0_pa, asid, argc, argv, &sp, &argv_user)) {
        kfree(t);
        return 0;
    }

    t->ttbr0_pa = l0_pa;
    t->asid     = asid;
    t->heap_end = USER_HEAP_BASE;

    uint64_t *f = (uint64_t *)frame_sp;
    f[0x000 / 8] = (uint64_t)argc;
    f[0x008 / 8] = argv_user;
    f[0x100 / 8] = info.entry;
    f[0x108 / 8] = SPSR_EL0T_UNMASKED;
    f[0x110 / 8] = sp;

    t->sp_saved = frame_sp;
    return t;
}

task_t *task_create_from_elf(const char *name, const void *elf, uint32_t elf_size) {
    return create_from_elf_common(name, elf, elf_size, 0, 0);
}

task_t *task_create_from_elf_ex(const char *name, const void *elf, uint32_t elf_size,
                                int argc, const char *const *argv)
{
    return create_from_elf_common(name, elf, elf_size, argc, argv);
}

task_t *task_fork_clone(task_t *parent, uint64_t new_l0, uint16_t new_asid,
                       uint64_t parent_frame_sp)
{
    if (!parent) return 0;

    task_t *child = (task_t *)kmalloc(sizeof(task_t));
    if (!child) return 0;

    uint64_t kpages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *kstack = (uint8_t *)phys_alloc_pages(kpages);
    if (!kstack) { kfree(child); return 0; }

    // Копируем всё, включая fds.
    // (struct task_t имеет тривиальные поля, memcpy корректен.)
    {
        const uint8_t *s = (const uint8_t *)parent;
        uint8_t *d = (uint8_t *)child;
        for (uint64_t i = 0; i < sizeof(task_t); i++) d[i] = s[i];
    }

    child->cred = cred_copy(parent->cred);
    if (!child->cred) { kfree(child); return 0; }

    child->id         = g_next_id++;
    child->state      = TASK_READY;
    child->next       = 0;
    child->ttbr0_pa   = new_l0;
    child->asid       = new_asid;
    child->parent_id  = parent->id;
    child->exit_code  = 0;

    // Имя: parent + "c".
    int nl = 0;
    while (parent->name[nl] && nl < TASK_NAME_MAX - 2) {
        child->name[nl] = parent->name[nl];
        nl++;
    }
    child->name[nl++] = 'c';
    child->name[nl]   = 0;

    // Обход fds: для pipe'ов увеличить refcount обеих сторон.
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        task_fd_t *fd = &child->fds[i];
        if (!fd->used) continue;
        if (fd->src == TFD_PIPE_READ)  pipe_ref_read(fd->pipe_idx);
        if (fd->src == TFD_PIPE_WRITE) pipe_ref_write(fd->pipe_idx);
    }

    // Kernel-стек: копия frame'а.
    uint64_t kstack_top = (uint64_t)(kstack + kpages * PAGE_SIZE);
    uint64_t frame_sp   = (kstack_top - FRAME_SIZE) & ~0xFull;

    uint64_t *cf = (uint64_t *)frame_sp;
    uint64_t *pf = (uint64_t *)parent_frame_sp;
    for (int i = 0; i < (int)(FRAME_SIZE / 8); i++) cf[i] = pf[i];

    // Ребёнок видит fork() == 0.
    cf[0x000 / 8] = 0;

    child->sp_saved = frame_sp;
    return child;
}
