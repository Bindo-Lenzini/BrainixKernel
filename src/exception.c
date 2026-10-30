// BrainixKernel — exception.c
// Обработчик синхронных исключений: SVC, Data/Instruction Abort,
// recovery для uaccess. Диагностика EL0-fault'ов пишется в UART
// и (если задача помечена to_window) в ksh-окно.

#include <stdint.h>
#include "task.h"
#include "sched.h"
#include "syscall.h"
#include "uaccess.h"
#include "mmu.h"
#include "ksh.h"

// Флаг: мы внутри syscall_dispatch. Устанавливается в
// exception_sync_handler на время обработки SVC, чтобы timer IRQ
// не переключал задачу через sched_yield_from_frame
// (иначе вложенные frames путаются).
volatile int g_in_syscall = 0;

extern void uart_puts(const char *s);
extern void uart_puthex(uint64_t v);

// ============================================================
// Форматирование диагностики.
// ============================================================

static int fmt_hex(char *dst, uint64_t v) {
    static const char hx[] = "0123456789ABCDEF";
    dst[0] = '0'; dst[1] = 'x';
    for (int i = 0; i < 16; i++)
        dst[2 + i] = hx[(v >> (60 - i * 4)) & 0xF];
    return 18;
}

static int fmt_str(char *dst, const char *s) {
    int n = 0;
    while (s[n]) { dst[n] = s[n]; n++; }
    return n;
}

// Decode FSC (DFSC/IFSC) в человекочитаемую строку.
static const char *fsc_str(uint32_t fsc) {
    switch (fsc) {
        case 0x01: return "alignment";
        case 0x04: return "translation L0";
        case 0x05: return "translation L1";
        case 0x06: return "translation L2";
        case 0x07: return "translation L3";
        case 0x08: return "access-flag L0";
        case 0x09: return "access-flag L1";
        case 0x0A: return "access-flag L2";
        case 0x0B: return "access-flag L3";
        case 0x0C: return "permission L0";
        case 0x0D: return "permission L1";
        case 0x0E: return "permission L2";
        case 0x0F: return "permission L3";
        case 0x10: return "synchronous-external";
        case 0x11: return "sync-ext tblwalk L0";
        case 0x13: return "sync-ext tblwalk L2";
        case 0x15: return "sync-ext tblwalk L1";
        case 0x17: return "sync-ext tblwalk L3";
        case 0x21: return "alignment (prefetch)";
        default:   return "?";
    }
}

// [fault] task=NAME kind=W/R far=0x... fsc=... elr=0x...
// Пишем в UART; если задача to_window — ещё и в ksh-окно.
static void fault_report(const char *kind, int is_write,
                         uint64_t esr, uint64_t far, uint64_t elr)
{
    char buf[256];
    int n = 0;

    n += fmt_str(buf + n, "\n[fault] ");

    task_t *cur = sched_current();
    if (cur) {
        n += fmt_str(buf + n, "task=");
        n += fmt_str(buf + n, cur->name);
        n += fmt_str(buf + n, " ");
    }

    n += fmt_str(buf + n, kind);
    n += fmt_str(buf + n, is_write ? " W" : " R");

    n += fmt_str(buf + n, " far="); n += fmt_hex(buf + n, far);
    n += fmt_str(buf + n, " fsc=");
    n += fmt_str(buf + n, fsc_str((uint32_t)(esr & 0x3F)));
    n += fmt_str(buf + n, " elr="); n += fmt_hex(buf + n, elr);
    n += fmt_str(buf + n, "\n");
    buf[n] = 0;

    uart_puts(buf);

    if (cur && cur->to_window) {
        ksh_out_write(buf, (uint32_t)n);
        ksh_out_flush_partial();
    }
}

// ============================================================
// Main handler.
// ============================================================

uint64_t exception_sync_handler(uint64_t sp) {
    uint64_t esr, elr, spsr, far = 0;
    __asm__ volatile("mrs %0, esr_el1"  : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1"  : "=r"(elr));
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
    __asm__ volatile("mrs %0, far_el1"  : "=r"(far));

    uint32_t ec = (uint32_t)((esr >> 26) & 0x3F);
    int from_el0 = ((spsr & 0xF) == 0);

    // ---------- SVC (syscall) ----------
    if (ec == 0x15) {
        if (from_el0) {
            g_in_syscall = 1;
            uint64_t r = syscall_dispatch(sp);
            g_in_syscall = 0;
            return r;
        }
        // SVC из EL1 (kernel_yield).
        return sched_yield_from_frame(sp);
    }

    // ---------- Data Abort из EL1 во время uaccess — recovery ----------
    if (ec == 0x25 && !from_el0 && __uaccess_active) {
        if (far >= __uaccess_start && far < __uaccess_end) {
            __uaccess_active = 0;
            uint64_t *f = (uint64_t *)sp;
            f[0x100 / 8] = __uaccess_recovery_elr;
            return sp;
        }
    }

    // ---------- Data Abort из EL0 ----------
    if (ec == 0x24 && from_el0) {
        uint32_t fsc = (uint32_t)(esr & 0x3F);
        int is_write = (int)((esr >> 6) & 1);

        // COW — только на write-fault уровня permission L3 (FSC=0x0F).
        // На любом другом FSC не пытаемся: mmu_cow_fault может вернуть 1
        // без реального изменения PTE, и мы уйдём в бесконечный retry.
        if (is_write && fsc == 0x0F) {
            task_t *cur = sched_current();
            if (cur && mmu_cow_fault(cur->ttbr0_pa, cur->asid, far)) {
                return sp;   // retry инструкции
            }
        }

        fault_report("Data", is_write, esr, far, elr);
        task_t *cur = sched_current();
        if (cur) {
            cur->exit_code = 0xDEAD0000ULL | fsc;
            cur->state     = TASK_REAPED;
        }
        return sched_yield_from_frame(sp);
    }

    // ---------- Instruction Abort из EL0 ----------
    if (ec == 0x20 && from_el0) {
        fault_report("Instr", 0, esr, far, elr);
        task_t *cur = sched_current();
        if (cur) {
            cur->exit_code = 0xDEAD0000ULL | (esr & 0x3F);
            cur->state     = TASK_REAPED;
        }
        return sched_yield_from_frame(sp);
    }

    // ---------- Прочее из EL0 — тоже убиваем, но логируем ----------
    if (from_el0) {
        fault_report("Other", 0, esr, far, elr);
        task_t *cur = sched_current();
        if (cur) {
            cur->exit_code = 0xDEAD0000ULL | ec;
            cur->state     = TASK_REAPED;
        }
        return sched_yield_from_frame(sp);
    }

    // ---------- Fault из ядра — panic ----------
    {
        char buf[256];
        int n = 0;
        n += fmt_str(buf + n, "\n[kernel fault] EC="); n += fmt_hex(buf + n, ec);
        n += fmt_str(buf + n, " ESR="); n += fmt_hex(buf + n, esr);
        n += fmt_str(buf + n, " ELR="); n += fmt_hex(buf + n, elr);
        n += fmt_str(buf + n, " FAR="); n += fmt_hex(buf + n, far);
        n += fmt_str(buf + n, "\n[halted]\n");
        buf[n] = 0;
        uart_puts(buf);
    }
    for (;;) __asm__ volatile("wfe");
}