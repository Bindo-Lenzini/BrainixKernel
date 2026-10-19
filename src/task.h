// BrainixKernel (BKOS) — task.h
#ifndef BKOS_TASK_H
#define BKOS_TASK_H

#include <stdint.h>
#include "elf.h"

#define TASK_NAME_MAX   16
#define TASK_STACK_SIZE 8192
#define TASK_USTACK_SIZE 4096
#define TASK_MAX_ARGC   16
#define TASK_MAX_FDS    16

#define SPSR_EL1H_UNMASKED 0x5ULL
#define SPSR_EL0T_UNMASKED 0x0ULL

// Тип источника fd.
#define TFD_UNUSED      0
#define TFD_INITRD      1
#define TFD_BKFS        2
#define TFD_PIPE_READ   3
#define TFD_PIPE_WRITE  4
#define TFD_SOCKET      5
#define TFD_TCP         6

typedef struct {
    int      used;
    int      src;         // TFD_*
    int      flags;
    uint32_t offset;

    // TFD_INITRD:
    const void *file;

    // TFD_BKFS:
    int      bkfs_inode;

    // TFD_PIPE_*:
    int      pipe_idx;

    // TFD_SOCKET:
    int      sock_idx;

    // TFD_TCP:
    int      tcp_idx;
} task_fd_t;

typedef enum {
    TASK_READY   = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_DEAD    = 3,
    TASK_REAPED  = 4,
} task_state_t;

typedef struct task {
    uint64_t      sp_saved;
    uint64_t      id;
    char          name[TASK_NAME_MAX];
    task_state_t  state;
    struct task  *next;
    int           to_window;
    uint64_t      ttbr0_pa;
    uint16_t      asid;
    uint64_t      heap_end;
    uint64_t      parent_id;
    uint64_t      exit_code;
    task_fd_t     fds[TASK_MAX_FDS];
} task_t;

task_t *task_create(const char *name, void (*fn)(void));
task_t *task_create_user(const char *name, const void *code, unsigned long code_size);
task_t *task_create_from_elf(const char *name, const void *elf, uint32_t elf_size);
task_t *task_create_from_elf_ex(const char *name, const void *elf, uint32_t elf_size,
                                int argc, const char *const *argv);

int task_load_elf_into(uint64_t l0_pa, uint16_t asid,
                       const void *elf, uint32_t elf_size,
                       elf_info_t *out_info);

int task_setup_user_stack(uint64_t l0_pa, uint16_t asid,
                          int argc, const char *const *argv,
                          uint64_t *out_sp, uint64_t *out_argv);

task_t *task_fork_clone(task_t *parent, uint64_t new_l0, uint16_t new_asid,
                       uint64_t parent_frame_sp);

#endif
