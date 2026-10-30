// BrainixKernel — syscall.c
// FD-таблица теперь в task_t (per-process).

#include <stdint.h>
#include "protosw.h"
#include <stddef.h>
#include "syscall.h"
#include "task.h"
#include "cred.h"
#include "sched.h"
#include "initrd.h"
#include "bkfs.h"
#include "ksh.h"
#include "uaccess.h"
#include "mmu.h"
#include "physmem.h"
#include "elf.h"
#include "pipe.h"
#include "socket.h"
#include "net.h"
#include "tcp.h"

extern void uart_putc(char c);
extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern int  uart_rx_pop(uint8_t *out);

#define KIO_BUF 512
#define FD_BASE 3     // 0,1,2 — стандартные

// ------------------------------------------------------------------
// Доступ к fd-таблице текущей задачи.
// ------------------------------------------------------------------
static task_fd_t *fd_slot(int fd) {
    task_t *cur = sched_current();
    if (!cur) return 0;
    if (fd < 0 || fd >= TASK_MAX_FDS) return 0;
    return &cur->fds[fd];
}

static task_fd_t *fd_used(int fd) {
    task_fd_t *f = fd_slot(fd);
    if (!f) return 0;
    if (!f->used) return 0;
    return f;
}

static void fd_clear_slot(task_fd_t *f) {
    f->used       = 0;
    f->src        = TFD_UNUSED;
    f->flags      = 0;
    f->offset     = 0;
    f->file       = 0;
    f->bkfs_inode = -1;
    f->pipe_idx   = -1;
    f->sock_idx   = -1;
    f->tcp_idx    = -1;
}

// ------------------------------------------------------------------
// write
// ------------------------------------------------------------------
static uint64_t sys_write(int fd, const void *user_buf, size_t len) {
    if (!user_buf || len == 0) return 0;

    task_fd_t *f = fd_used(fd);

    // fd 1/2 без override — идут в UART или ksh.
    if (!f && (fd == 1 || fd == 2)) {
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        if (copy_from_user(kbuf, user_buf, len) < 0) return (uint64_t)-1;

        task_t *cur = sched_current();
        if (cur && cur->to_window) {
            ksh_out_write((const char *)kbuf, (uint32_t)len);
        } else {
            for (size_t i = 0; i < len; i++) uart_putc((char)kbuf[i]);
        }
        return len;
    }

    if (!f) return (uint64_t)-1;

    // BKFS-файл (offset всегда 0, перезапись целиком).
    if (f->src == TFD_BKFS) {
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        if (copy_from_user(kbuf, user_buf, len) < 0) return (uint64_t)-1;
        int w = bkfs_write_at(f->bkfs_inode, 0, kbuf, (uint32_t)len);
        if (w < 0) return (uint64_t)-1;
        f->offset = (uint32_t)w;
        return (uint64_t)w;
    }

    // Pipe write.
    if (f->src == TFD_PIPE_WRITE) {
        size_t done = 0;
        uint8_t kbuf[KIO_BUF];
        while (done < len) {
            size_t chunk = len - done;
            if (chunk > KIO_BUF) chunk = KIO_BUF;
            if (copy_from_user(kbuf, (const uint8_t *)user_buf + done, chunk) < 0)
                return done > 0 ? done : (uint64_t)-1;
            int w = pipe_write(f->pipe_idx, kbuf, (uint32_t)chunk);
            if (w < 0) return done > 0 ? done : (uint64_t)-1;
            done += w;
            if ((uint32_t)w < chunk) break;   // pipe полон, отдаём что есть
        }
        return done;
    }

    return (uint64_t)-1;
}

// ------------------------------------------------------------------
// read
// ------------------------------------------------------------------
static uint64_t sys_read(int fd, void *user_buf, size_t len) {
    if (!user_buf || len == 0) return 0;

    task_fd_t *f = fd_used(fd);

    // fd 0 без override — UART.
    if (!f && fd == 0) {
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        size_t got = 0;
        while (got < len) {
            uint8_t b;
            if (!uart_rx_pop(&b)) break;
            if (b == '\r') b = '\n';
            kbuf[got++] = b;
            if (b == '\n') break;
        }
        if (got == 0) return 0;
        if (copy_to_user(user_buf, kbuf, got) < 0) return (uint64_t)-1;
        return got;
    }

    if (!f) return (uint64_t)-1;

    if (f->src == TFD_INITRD) {
        const initrd_file_t *file = (const initrd_file_t *)f->file;
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        uint32_t avail = file->size - f->offset;
        uint32_t take = (avail > len) ? (uint32_t)len : avail;
        for (uint32_t i = 0; i < take; i++) kbuf[i] = file->data[f->offset + i];
        if (take == 0) return 0;
        if (copy_to_user(user_buf, kbuf, take) < 0) return (uint64_t)-1;
        f->offset += take;
        return take;
    }

    if (f->src == TFD_BKFS) {
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        int r = bkfs_read_at(f->bkfs_inode, f->offset, kbuf, (uint32_t)len);
        if (r < 0) return (uint64_t)-1;
        if (r == 0) return 0;
        if (copy_to_user(user_buf, kbuf, (size_t)r) < 0) return (uint64_t)-1;
        f->offset += (uint32_t)r;
        return (uint64_t)r;
    }

    if (f->src == TFD_PIPE_READ) {
        if (len > KIO_BUF) len = KIO_BUF;
        uint8_t kbuf[KIO_BUF];
        int r = pipe_read(f->pipe_idx, kbuf, (uint32_t)len);
        if (r == 0) return 0;                   // EOF
        if (r < 0) return (uint64_t)-2;         // EAGAIN
        if (copy_to_user(user_buf, kbuf, (size_t)r) < 0) return (uint64_t)-1;
        return (uint64_t)r;
    }

    return (uint64_t)-1;
}

// ------------------------------------------------------------------
// open
// ------------------------------------------------------------------
static int fd_alloc_slot(void) {
    for (int i = FD_BASE; i < TASK_MAX_FDS; i++) {
        task_fd_t *f = fd_slot(i);
        if (f && !f->used) return i;
    }
    return -1;
}

static uint64_t sys_open(const char *user_name, int flags) {
    if (!user_name) return (uint64_t)-1;

    char kname[128];
    int n = 0;
    for (; n < (int)sizeof(kname) - 1; n++) {
        char c;
        if (copy_from_user(&c, user_name + n, 1) < 0) return (uint64_t)-1;
        kname[n] = c;
        if (c == 0) break;
    }
    kname[sizeof(kname) - 1] = 0;
    if (kname[0] == 0) return (uint64_t)-1;

    int create  = (flags & BKFS_O_CREAT) ? 1 : 0;
    int trunc   = (flags & BKFS_O_TRUNC) ? 1 : 0;
    int writable = (flags & (BKFS_O_WRONLY | BKFS_O_RDWR)) ? 1 : 0;

    // --- Access control (temporary policy) ---
    // bkfs does not expose per-inode uid/gid/mode yet, so the safest
    // policy is: non-root may only open files read-only.
    task_t *cur = sched_current();
    if (!cur || !cur->cred) return (uint64_t)-1;
    if (cur->cred->euid != 0 && (create || trunc || writable)) {
        return (uint64_t)-1;
    }

    if (bkfs_is_ready()) {
        int inode = bkfs_open(kname);
        if (inode < 0 && create && writable) {
            int r = bkfs_create(kname);
            if (r == BKFS_OK || r == BKFS_EEXIST) {
                inode = bkfs_open(kname);
            }
        }

        if (inode >= 0) {
            if (trunc && writable) bkfs_truncate(inode);
            int fd = fd_alloc_slot();
            if (fd < 0) return (uint64_t)-1;
            task_fd_t *f = fd_slot(fd);
            f->used       = 1;
            f->src        = TFD_BKFS;
            f->flags      = flags;
            f->offset     = 0;
            f->file       = 0;
            f->bkfs_inode = inode;
            f->pipe_idx   = -1;
            return (uint64_t)fd;
        }
    }

    const initrd_file_t *file = initrd_find(kname);
    if (file) {
        int fd = fd_alloc_slot();
        if (fd < 0) return (uint64_t)-1;
        task_fd_t *f = fd_slot(fd);
        f->used       = 1;
        f->src        = TFD_INITRD;
        f->flags      = flags;
        f->offset     = 0;
        f->file       = file;
        f->bkfs_inode = -1;
        f->pipe_idx   = -1;
        return (uint64_t)fd;
    }

    return (uint64_t)-1;
}

static uint64_t sys_close(int fd) {
    uart_puts("[sys_close] called\n");
    uart_puts("[sys_close] called\n");
    task_fd_t *f = fd_used(fd);
    if (!f) return (uint64_t)-1;

    if (f->src == TFD_TCP || f->src == TFD_SOCKET) {
        int type = (f->src == TFD_TCP) ? SOCK_STREAM : SOCK_DGRAM;
        struct protosw *pr = pffindproto(AF_INET, type, 0);
        if (pr && pr->pr_close) {
            pr->pr_close(f);
        }
    } else if (f->src == TFD_PIPE_READ) {
        pipe_close_read(f->pipe_idx);
    } else if (f->src == TFD_PIPE_WRITE) {
        pipe_close_write(f->pipe_idx);
    }

    f->used       = 0;
    f->src        = TFD_UNUSED;
    f->bkfs_inode = -1;
    f->pipe_idx   = -1;
    f->sock_idx   = -1;
    f->tcp_idx    = -1;
    return 0;
}


// ------------------------------------------------------------------
// pipe
// ------------------------------------------------------------------
static uint64_t sys_pipe(const int *user_fds) {
    int idx = pipe_alloc();
    if (idx < 0) return (uint64_t)-1;

    // Занимаем rfd сразу, иначе fd_alloc_slot вернёт тот же слот.
    int rfd = fd_alloc_slot();
    if (rfd < 0) {
        pipe_abort(idx);
        return (uint64_t)-1;
    }

    task_fd_t *fr = fd_slot(rfd);
    fr->used       = 1;
    fr->src        = TFD_PIPE_READ;
    fr->flags      = 0;
    fr->offset     = 0;
    fr->file       = 0;
    fr->bkfs_inode = -1;
    fr->pipe_idx   = idx;
    pipe_ref_read(idx);

    int wfd = fd_alloc_slot();
    if (wfd < 0) {
        pipe_close_read(idx);
        fd_clear_slot(fr);
        return (uint64_t)-1;
    }

    task_fd_t *fw = fd_slot(wfd);
    fw->used       = 1;
    fw->src        = TFD_PIPE_WRITE;
    fw->flags      = 0;
    fw->offset     = 0;
    fw->file       = 0;
    fw->bkfs_inode = -1;
    fw->pipe_idx   = idx;
    pipe_ref_write(idx);

    int kfds[2] = { rfd, wfd };
    if (copy_to_user((void *)user_fds, kfds, sizeof(kfds)) < 0) {
        pipe_close_read(idx);
        pipe_close_write(idx);
        fd_clear_slot(fr);
        fd_clear_slot(fw);
        return (uint64_t)-1;
    }
    return 0;
}

// ------------------------------------------------------------------
// dup2
// ------------------------------------------------------------------
static uint64_t sys_dup2(int oldfd, int newfd) {
    if (newfd < 0 || newfd >= TASK_MAX_FDS) return (uint64_t)-1;
    if (oldfd < 0 || oldfd >= TASK_MAX_FDS) return (uint64_t)-1;
    if (oldfd == newfd) return (uint64_t)newfd;

    task_fd_t *fold = fd_used(oldfd);
    if (!fold) return (uint64_t)-1;

    // Закрыть newfd, если использовался.
    task_fd_t *fnew = fd_slot(newfd);
    if (fnew->used) {
        if (fnew->src == TFD_PIPE_READ)  pipe_close_read(fnew->pipe_idx);
        if (fnew->src == TFD_PIPE_WRITE) pipe_close_write(fnew->pipe_idx);
    }

    // Копия.
    *fnew = *fold;

    // refcount для pipe.
    if (fnew->src == TFD_PIPE_READ)  pipe_ref_read(fnew->pipe_idx);
    if (fnew->src == TFD_PIPE_WRITE) pipe_ref_write(fnew->pipe_idx);

    return (uint64_t)newfd;
}

// ------------------------------------------------------------------
// brk
// ------------------------------------------------------------------
static uint64_t sys_brk(uint64_t new_end) {
    task_t *cur = sched_current();
    if (!cur || !cur->ttbr0_pa) return 0;

    uint64_t old_end = cur->heap_end;
    if (old_end == 0) old_end = USER_HEAP_BASE;

    if (new_end == 0) return old_end;

    if (new_end < USER_HEAP_BASE) new_end = USER_HEAP_BASE;
    if (new_end > USER_HEAP_MAX)  new_end = USER_HEAP_MAX;

    if (new_end <= old_end) {
        cur->heap_end = new_end;
        return new_end;
    }

    uint64_t start = (old_end + 0xFFFull) & ~0xFFFull;
    uint64_t end   = (new_end + 0xFFFull) & ~0xFFFull;

    for (uint64_t va = start; va < end; va += 0x1000) {
        uint8_t *pg = (uint8_t *)phys_alloc_page();
        if (!pg) {
            cur->heap_end = va;
            return va;
        }
        for (int j = 0; j < (int)PAGE_SIZE; j++) pg[j] = 0;
        mmu_map_user_page(cur->ttbr0_pa, va, KVA_TO_PA(pg), 0, cur->asid);
    }
    cur->heap_end = new_end;
    return new_end;
}

// ------------------------------------------------------------------
// exec
// ------------------------------------------------------------------
static uint64_t sys_exec(uint64_t sp, const char *user_path,
                         const char *const *user_argv)
{
    task_t *cur = sched_current();
    if (!cur || !cur->ttbr0_pa) return (uint64_t)-1;

    char kpath[128];
    int n = 0;
    for (; n < (int)sizeof(kpath) - 1; n++) {
        char c;
        if (copy_from_user(&c, user_path + n, 1) < 0) return (uint64_t)-1;
        kpath[n] = c;
        if (c == 0) break;
    }
    kpath[sizeof(kpath) - 1] = 0;
    if (kpath[0] == 0) return (uint64_t)-1;

    const initrd_file_t *file = initrd_find(kpath);
    if (!file) return (uint64_t)-1;

    #define MAX_ARGC 16
    char argv_bufs[MAX_ARGC][128];
    const char *argv_ptrs[MAX_ARGC];
    int argc = 0;

    if (user_argv) {
        for (; argc < MAX_ARGC; argc++) {
            uint64_t uptr = 0;
            if (copy_from_user(&uptr, (const char *)user_argv + argc * 8, 8) < 0) break;
            if (uptr == 0) break;

            char *dst = argv_bufs[argc];
            int k = 0;
            for (; k < 127; k++) {
                char c;
                if (copy_from_user(&c, (const char *)uptr + k, 1) < 0) {
                    dst[k] = 0;
                    break;
                }
                dst[k] = c;
                if (c == 0) break;
            }
            dst[127] = 0;
            argv_ptrs[argc] = dst;
        }
    }

    if (argc == 0) {
        int i = 0;
        while (kpath[i] && i < 127) { argv_bufs[0][i] = kpath[i]; i++; }
        argv_bufs[0][i] = 0;
        argv_ptrs[0] = argv_bufs[0];
        argc = 1;
    }

    uint64_t new_l0 = mmu_create_user_as();
    if (!new_l0) return (uint64_t)-1;
    uint16_t new_asid = mmu_alloc_asid();
    if (new_asid == 0) return (uint64_t)-1;

    elf_info_t info;
    if (!task_load_elf_into(new_l0, new_asid, file->data, file->size, &info))
        return (uint64_t)-1;

    uint64_t new_sp, new_argv;
    if (!task_setup_user_stack(new_l0, new_asid, argc, argv_ptrs, &new_sp, &new_argv))
        return (uint64_t)-1;

    cur->ttbr0_pa = new_l0;
    cur->asid     = new_asid;
    cur->heap_end = USER_HEAP_BASE;

    mmu_switch_ttbr0(new_l0, new_asid);

    uint64_t *frame = (uint64_t *)sp;
    frame[0x000 / 8] = (uint64_t)argc;
    frame[0x008 / 8] = new_argv;
    frame[0x100 / 8] = info.entry;
    frame[0x108 / 8] = SPSR_EL0T_UNMASKED;
    frame[0x110 / 8] = new_sp;

    return sp;
}

// ------------------------------------------------------------------
// fork
// ------------------------------------------------------------------
static uint64_t sys_fork(uint64_t sp) {
    task_t *parent = sched_current();
    if (!parent || !parent->ttbr0_pa) return (uint64_t)-1;

    uint64_t child_l0 = mmu_create_user_as();
    if (!child_l0) return (uint64_t)-1;

    uint16_t child_asid = mmu_alloc_asid();
    if (child_asid == 0) return (uint64_t)-1;

    if (!mmu_clone_user_as_cow(parent->ttbr0_pa, parent->asid, child_l0, child_asid))
        return (uint64_t)-1;

    task_t *child = task_fork_clone(parent, child_l0, child_asid, sp);
    if (!child) return (uint64_t)-1;

    sched_add(child);

    ((uint64_t *)sp)[0x000 / 8] = child->id;
    return sp;
}

// ------------------------------------------------------------------
// wait
// ------------------------------------------------------------------
static uint64_t sys_wait(uint64_t sp, int pid) {
    task_t *cur = sched_current();
    uint64_t result = (uint64_t)-1;

    if (cur) {
        extern task_t *sched_first(void);
        task_t *head = sched_first();
        if (head) {
            task_t *p = head;
            int iter = 0;
            do {
                if (++iter > 64) break;
                if (p->parent_id == cur->id) {
                    if (pid == -1 || (uint64_t)pid == p->id) {
                        if (p->state == TASK_DEAD) {
                            result = p->exit_code;
                            p->state     = TASK_REAPED;
                            p->parent_id = 0;
                            break;
                        }
                        if (p->state != TASK_REAPED) {
                            result = (uint64_t)-2;
                            break;
                        }
                    }
                }
                p = p->next;
            } while (p != head);
        }
    }

    ((uint64_t *)sp)[0x000 / 8] = result;
    return sp;
}

// ------------------------------------------------------------------
// readdir
// ------------------------------------------------------------------
static uint64_t sys_readdir(uint64_t sp, uint64_t idx,
                            const char *user_buf, uint64_t max_len)
{
    (void)sp;
    if (!user_buf || max_len == 0) return (uint64_t)-1;

    char kname[64];
    uint32_t ksize = 0;
    if (!bkfs_list((int)idx, kname, sizeof(kname), &ksize)) return (uint64_t)-1;

    uint32_t n = 0;
    while (kname[n] && n < max_len - 1) n++;
    if (copy_to_user((void *)user_buf, kname, n + 1) < 0) return (uint64_t)-1;
    return n;
}

// ------------------------------------------------------------------
// exit
// ------------------------------------------------------------------
static void build_exit_msg(char *out, uint64_t code, int *len) {
    const char *pre = "[exit ";
    int i = 0;
    for (int j = 0; pre[j]; j++) out[i++] = pre[j];
    char tmp[16]; int t = 0;
    if (code == 0) tmp[t++] = '0';
    while (code) { tmp[t++] = (char)('0' + (code % 10)); code /= 10; }
    while (t > 0) out[i++] = tmp[--t];
    out[i++] = ']';
    out[i++] = '\n';
    *len = i;
}

static uint64_t do_exit(uint64_t code, uint64_t sp) {
    task_t *cur = sched_current();

    // Пишем [exit N]:
    //   * в ksh, если задача помечена to_window;
    //   * в UART, только если это kernel-задача (ttbr0_pa == 0);
    //   * user-задачи молчат — exit-код доставляется родителю через wait().
    char msg[32];
    int mlen = 0;
    build_exit_msg(msg, code, &mlen);

    if (cur && cur->to_window) {
        ksh_out_write(msg, (uint32_t)mlen);
        ksh_out_flush_partial();
    } else if (cur && cur->ttbr0_pa == 0) {
        for (int i = 0; i < mlen; i++) uart_putc(msg[i]);
    }

    if (cur) {
        cur->exit_code = code;
        cur->state     = TASK_DEAD;
    }
    return sched_yield_from_frame(sp);
}

// ------------------------------------------------------------------
// dispatch
// ------------------------------------------------------------------
// ------------------------------------------------------------------
// socket (AF_INET/SOCK_DGRAM)
// ------------------------------------------------------------------
static uint64_t sys_socket(int domain, int type, int protocol) {
    uart_puts("[sys_socket] called\n");
    uart_puts("[sys_socket] called\n");
 uart_puts("[sys_socket] called\n");
    // Ищем протокол в таблице protosw (вместо if/else)
    if (domain == 0) domain = AF_INET; if (domain == 0) domain = AF_INET; if (domain == 0) domain = AF_INET; struct protosw *pr = pffindproto(domain, type, protocol);
    if (!pr) return (uint64_t)-1; // Семейство/тип не поддерживается

    int fd = fd_alloc_slot();
    if (fd < 0) return (uint64_t)-1;

    task_fd_t *f = fd_slot(fd);
    f->used       = 1;
    f->flags      = 0;
    f->offset     = 0;
    f->file       = 0;
    f->bkfs_inode = -1;
    f->pipe_idx   = -1;
    f->sock_idx   = -1;
    f->tcp_idx    = -1;

    // Вызываем pr_attach из таблицы протоколов!
    int idx = pr->pr_attach(f); 
    if (idx < 0) { 
        f->used = 0; 
        return (uint64_t)-1; 
    }

    // Настраиваем fd в зависимости от типа
    if (type == SOCK_DGRAM) {
        f->src      = TFD_SOCKET;
        f->sock_idx = idx;
    } else if (type == SOCK_STREAM) {
        f->src     = TFD_TCP;
        f->tcp_idx = idx;
    } else {
        f->used = 0;
        return (uint64_t)-1;
    }
    
    return (uint64_t)fd;
}



// ------------------------------------------------------------------
// bind — назначить локальный порт (network byte order).
// ------------------------------------------------------------------
static uint64_t sys_bind(int fd, uint16_t port_be) {
    task_fd_t *f = fd_used(fd);
    if (!f || (f->src != TFD_SOCKET && f->src != TFD_TCP)) return (uint64_t)-1;
    
    int type = (f->src == TFD_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    struct protosw *pr = pffindproto(AF_INET, type, 0);
    if (!pr || !pr->pr_bind) return (uint64_t)-1;

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = port_be;
    sa.sin_addr = 0;
    return pr->pr_bind(f, (struct sockaddr *)&sa);
}

// ------------------------------------------------------------------
// sendto
//   a0 = fd
//   a1 = user buf
//   a2 = len
//   a3 = dst_port_be
//   a4 = user_dst_ip[4]   (по ABI — в x4, но у нас SVC передаёт только x0-x2)
//
// ВНИМАНИЕ: SVC в нашем ABI передаёт только 3 аргумента (x0,x1,x2).
// Для sendto используем структуру: buf передаём как [len, port, ip, data...]
// Проще: a0=fd, a1=упакованный аргумент. Неудобно.
//
// Компромисс: расширяем ABI до 5 аргументов (x0..x4). Просто в диспетчере
// читаем f[3], f[4].
// ------------------------------------------------------------------
static uint64_t sys_sendto(int fd, const void *user_buf, uint64_t len,
                           uint64_t dst_port_be, const uint8_t *user_dst_ip)
{
    task_fd_t *f = fd_used(fd);
    if (!f || (f->src != TFD_SOCKET && f->src != TFD_TCP)) return (uint64_t)-1;

    int type = (f->src == TFD_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    struct protosw *pr = pffindproto(AF_INET, type, 0);
    if (!pr || !pr->pr_send) return (uint64_t)-1;

    if (!user_buf || len == 0 || len > SOCK_MAX_PACKET) return (uint64_t)-1;
    
    uint8_t kbuf[SOCK_MAX_PACKET];
    if (copy_from_user(kbuf, user_buf, len) < 0) return (uint64_t)-1;
    
    uint8_t ip[4];
    if (copy_from_user(ip, user_dst_ip, 4) < 0) return (uint64_t)-1;

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = (uint16_t)dst_port_be;
    uint8_t *a = (uint8_t *)&sa.sin_addr;
    a[0] = ip[0]; a[1] = ip[1]; a[2] = ip[2]; a[3] = ip[3];

    int ret = pr->pr_send(f, kbuf, len, (struct sockaddr *)&sa);
    if (ret < 0) return (uint64_t)-1;
    return (uint64_t)ret;
}

// ------------------------------------------------------------------
// recvfrom — забрать UDP-пакет из очереди сокета.
//   a0 = fd
//   a1 = user buf
//   a2 = max len
//   a3 = user uint16_t* для src_port (host order) или 0
//   a4 = user uint8_t[4] для src_ip или 0
// Возврат: >0 — размер, 0 — нет данных, -1 — ошибка.
// ------------------------------------------------------------------
static uint64_t sys_recvfrom(int fd, void *user_buf, uint64_t max_len,
                             uint16_t *user_src_port, uint8_t *user_src_ip)
{
    task_fd_t *f = fd_used(fd);
    if (!f || (f->src != TFD_SOCKET && f->src != TFD_TCP)) return (uint64_t)-1;

    int type = (f->src == TFD_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    struct protosw *pr = pffindproto(AF_INET, type, 0);
    if (!pr || !pr->pr_recv) return (uint64_t)-1;

    if (!user_buf || max_len == 0) return (uint64_t)-1;
    if (max_len > SOCK_MAX_PACKET) max_len = SOCK_MAX_PACKET;

    uint8_t kbuf[SOCK_MAX_PACKET];
    struct sockaddr_in sa;
    int ret = pr->pr_recv(f, kbuf, max_len, (struct sockaddr *)&sa);
    if (ret <= 0) return (uint64_t)ret;

    if (copy_to_user(user_buf, kbuf, ret) < 0) return (uint64_t)-1;

    if (user_src_port) {
        if (copy_to_user(user_src_port, &sa.sin_port, 2) < 0) return (uint64_t)-1;
    }
    if (user_src_ip) {
        uint8_t *a = (uint8_t *)&sa.sin_addr;
        if (copy_to_user(user_src_ip, a, 4) < 0) return (uint64_t)-1;
    }
    return (uint64_t)ret;
}

// ------------------------------------------------------------------
// connect(fd, ip[4], port_host)
// ------------------------------------------------------------------
static uint64_t sys_connect(int fd, const uint8_t *user_ip, uint16_t port_host) {
    task_fd_t *f = fd_used(fd);
    if (!f || f->src != TFD_TCP) return (uint64_t)-1;

    struct protosw *pr = pffindproto(AF_INET, SOCK_STREAM, 0);
    if (!pr || !pr->pr_connect) return (uint64_t)-1;

    uint8_t ip[4];
    if (copy_from_user(ip, user_ip, 4) < 0) return (uint64_t)-1;

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = port_host;
    uint8_t *a = (uint8_t *)&sa.sin_addr;
    a[0] = ip[0]; a[1] = ip[1]; a[2] = ip[2]; a[3] = ip[3];

    return (uint64_t)pr->pr_connect(f, (struct sockaddr *)&sa);
}

static uint64_t sys_send(int fd, const void *user_buf, size_t len) {
    task_fd_t *f = fd_used(fd);
    if (!f || f->src != TFD_TCP) return (uint64_t)-1;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c) return (uint64_t)-1;
    if (!user_buf || len == 0) return 0;

    if (len > 1400) len = 1400;
    uint8_t kbuf[1400];
    if (copy_from_user(kbuf, user_buf, len) < 0) return (uint64_t)-1;

    int r = tcp_send_data(c, kbuf, (uint32_t)len);
    if (r <= 0) return (uint64_t)-1;
    return (uint64_t)r;
}

static uint64_t sys_recv(int fd, void *user_buf, size_t max_len) {
    task_fd_t *f = fd_used(fd);
    if (!f || f->src != TFD_TCP) return (uint64_t)-1;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c) return (uint64_t)-1;
    if (!user_buf || max_len == 0) return (uint64_t)-1;

    if (c->rx_len == 0) {
        if (c->peer_closed) return 0;
        return (uint64_t)-2;
    }

    if (max_len > 4096) max_len = 4096;
    uint8_t kbuf[4096];
    uint32_t got = tcp_recv_data(c, kbuf, (uint32_t)max_len);
    if (got == 0) return (uint64_t)-2;
    if (copy_to_user(user_buf, kbuf, got) < 0) return (uint64_t)-1;
    return (uint64_t)got;
}

static uint64_t sys_getuid(void) {
    task_t *cur = sched_current();
    if (!cur || !cur->cred) return (uint64_t)-1;
    return (uint64_t)cur->cred->uid;
}

static uint64_t sys_geteuid(void) {
    task_t *cur = sched_current();
    if (!cur || !cur->cred) return (uint64_t)-1;
    return (uint64_t)cur->cred->euid;
}

static uint64_t sys_getgid(void) {
    task_t *cur = sched_current();
    if (!cur || !cur->cred) return (uint64_t)-1;
    return (uint64_t)cur->cred->gid;
}

static uint64_t sys_getegid(void) {
    task_t *cur = sched_current();
    if (!cur || !cur->cred) return (uint64_t)-1;
    return (uint64_t)cur->cred->egid;
}

uint64_t syscall_dispatch(uint64_t sp) {
    uint64_t *f  = (uint64_t *)sp;
    uint64_t nr  = f[8];
    uint64_t a0  = f[0];
    uint64_t a1  = f[1];
    uint64_t a2  = f[2];
    uint64_t a3  = f[3];
    uint64_t a4  = f[4];

    if (nr == SYS_EXIT)    return do_exit(a0, sp);
    if (nr == SYS_YIELD)   return sched_yield_from_frame(sp);
    if (nr == SYS_FORK)    return sys_fork(sp);
    if (nr == SYS_EXEC)    return sys_exec(sp, (const char *)a0,
                                          (const char *const *)a1);
    if (nr == SYS_WAIT)    return sys_wait(sp, (int)a0);

    uint64_t ret = (uint64_t)-1;
    if (nr == SYS_WRITE)    ret = sys_write((int)a0, (const void *)a1, (size_t)a2);
    if (nr == SYS_READ)     ret = sys_read((int)a0, (void *)a1, (size_t)a2);
    if (nr == SYS_OPEN)     ret = sys_open((const char *)a0, (int)a1);
    if (nr == SYS_CLOSE)    ret = sys_close((int)a0);
    if (nr == SYS_BRK)      ret = sys_brk(a0);
    if (nr == SYS_READDIR)  ret = sys_readdir(sp, a0, (const char *)a1, a2);
    if (nr == SYS_PIPE)     ret = sys_pipe((const int *)a0);
    if (nr == SYS_DUP2)     ret = sys_dup2((int)a0, (int)a1);
    if (nr == SYS_SOCKET)   ret = sys_socket((int)a0, (int)a1, (int)a2);
    if (nr == SYS_BIND)     ret = sys_bind((int)a0, (uint16_t)a1);
    if (nr == SYS_SENDTO)   ret = sys_sendto((int)a0, (const void *)a1,
                                              a2, a3, (const uint8_t *)a4);
    if (nr == SYS_RECVFROM) ret = sys_recvfrom((int)a0, (void *)a1, a2,
                                               (uint16_t *)a3, (uint8_t *)a4);
    if (nr == SYS_CONNECT)  ret = sys_connect((int)a0, (const uint8_t *)a1,
                                              (uint16_t)a2);
    if (nr == SYS_SEND)     ret = sys_send((int)a0, (const void *)a1, (size_t)a2);
    if (nr == SYS_RECV)     ret = sys_recv((int)a0, (void *)a1, (size_t)a2);
    if (nr == SYS_GETUID)   ret = sys_getuid();
    if (nr == SYS_GETEUID)  ret = sys_geteuid();
    if (nr == SYS_GETGID)   ret = sys_getgid();
    if (nr == SYS_GETEGID)  ret = sys_getegid();

    f[0] = ret;
    return sp;
}
