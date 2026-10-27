// BrainixKernel — signal.c
#include <stdint.h>
#include "signal.h"
#include "sched.h"
#include "uaccess.h"

// ============================================================
// Trampoline-страница (mini-vDSO).
// EL0-код, в который «возвращается» signal handler:
//   movz x8, #22      ; SYS_SIGRETURN
//   svc  #0
//   ; сюда не должны попасть; если попали — exit(127)
//   movz x8, #1       ; SYS_EXIT
//   movz x0, #127
//   svc  #0
//   b    .
// ============================================================

static uint8_t g_sigtramp_page[4096] __attribute__((aligned(4096)));

void signal_init(void) {
    uint32_t *p = (uint32_t *)g_sigtramp_page;
    p[0] = 0xD28002C8;  // movz x8, #22
    p[1] = 0xD4000001;  // svc  #0
    p[2] = 0xD2800028;  // movz x8, #1
    p[3] = 0xD2800FE0;  // movz x0, #127
    p[4] = 0xD4000001;  // svc  #0
    p[5] = 0x14000000;  // b .
}

void signal_map_tramp(uint64_t l0_pa, uint16_t asid) {
    if (!l0_pa) return;
    mmu_map_user_page(l0_pa, SIGTRAMP_VA, KVA_TO_PA(g_sigtramp_page), 1, asid);
}

// ============================================================
// Ядро сигнальной логики.
// ============================================================

task_t *task_by_id(uint64_t id) {
    task_t *head = sched_first();
    if (!head) return 0;
    task_t *p = head;
    do {
        if (p->id == id) return p;
        p = p->next;
    } while (p != head);
    return 0;
}

int signal_send(task_t *t, int sig) {
    if (!t) return -1;
    if (sig <= 0 || sig >= NSIG) return -1;
    if (t->state == TASK_DEAD || t->state == TASK_REAPED) return -1;
    // Игнорируемые сигналы не копим (кроме неубиваемых).
    if (t->sig_handler[sig] == SIG_IGN && sig != SIGKILL && sig != SIGSTOP)
        return 0;
    t->sig_pending |= (1ULL << sig);
    return 0;
}

// Убийство по сигналу: конвенция Unix — exit code = 128 + sig.
static uint64_t signal_terminate(uint64_t sp, int sig) {
    task_t *t = sched_current();
    if (t) {
        t->exit_code = 128 + (uint64_t)sig;
        t->state     = TASK_DEAD;
    }
    return sched_yield_from_frame(sp);
}

// Доставка старшего незамаскированного pending-сигнала.
// Вызывается на пути возврата в EL0 (из SVC-обработчика и из IRQ).
// Может переключить задачу (действие по умолчанию = terminate).
uint64_t signal_deliver(uint64_t sp) {
    task_t *t = sched_current();
    if (!t || !t->ttbr0_pa) return sp;                 // kernel-задачи — мимо
    if (t->state == TASK_DEAD || t->state == TASK_REAPED) return sp;

    for (;;) {
        uint64_t ready = t->sig_pending & ~t->sig_mask;
        if (t->sig_pending & (1ULL << SIGKILL))        // SIGKILL не маскируется
            ready |= (1ULL << SIGKILL);
        if (!ready) return sp;

        int sig = __builtin_ctzll(ready);
        t->sig_pending &= ~(1ULL << sig);

        sig_handler_t h = t->sig_handler[sig];
        if (h == SIG_IGN) continue;
        if (h == SIG_DFL) {
            if (sig == SIGCHLD || sig == SIGCONT || sig == SIGSTOP)
                continue;                              // default: ignore
            return signal_terminate(sp, sig);
        }

        // ---- Доставка: сигнальный фрейм на user-стеке ----
        uint64_t *f = (uint64_t *)sp;
        uint64_t sp_el0 = f[0x110 / 8];

        // Лейаут фрейма в юзер-памяти (0x120 байт):
        //   +0x00..+0xF7  x0..x30
        //   +0xF8         elr (куда вернуться после handler'а)
        //   +0x100        spsr
        //   +0x108        saved sig_mask
        //   +0x110        SIG_FRAME_MAGIC
        uint64_t uf[36];
        for (int i = 0; i <= 30; i++) uf[i] = f[i];    // x0..x30
        uf[31] = f[0x100 / 8];                         // elr
        uf[32] = f[0x108 / 8];                         // spsr
        uf[33] = t->sig_mask;                          // saved mask
        uf[34] = SIG_FRAME_MAGIC;
        uf[35] = 0;

        uint64_t nsp = (sp_el0 - 0x120) & ~0xFULL;
        if (nsp < USER_STACK_LIMIT || nsp + 0x120 > USER_STACK_TOP)
            return signal_terminate(sp, SIGSEGV);      // переполнение стека

        if (copy_to_user((void *)nsp, uf, 0x120) < 0)
            return signal_terminate(sp, SIGSEGV);

        f[0x110 / 8] = nsp;                // user SP → на наш фрейм
        f[0x100 / 8] = SIGTRAMP_VA;        // ELR → trampoline
        f[0x108 / 8] = SPSR_EL0T_UNMASKED; // SPSR → EL0
        f[0x0F0 / 8] = SIGTRAMP_VA;        // x30 → trampoline (ret из handler'а)
        f[0x000 / 8] = (uint64_t)sig;      // x0 = номер сигнала

        t->sig_mask = t->sig_hmask[sig] | (1ULL << sig);
        return sp;
    }
}