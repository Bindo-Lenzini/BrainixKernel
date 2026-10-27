// BrainixKernel — sched.c
//
// Round-robin. При переключении на user-задачу — TTBR0 с её ASID.
// TLB не сбрасываем: записи для каждого ASID сосуществуют.

#include "sched.h"
#include "task.h"
#include "mmu.h"


static task_t *g_head    = 0;
static task_t *g_current = 0;

void sched_add(task_t *t) {
    if (!t) return;
    if (!g_head) { g_head = t; t->next = t; return; }

    task_t *p = g_head;
    while (p->next != g_head) p = p->next;
    p->next = t;
    t->next = g_head;
}

static task_t *pick_next(void) {
    if (!g_current) return g_head;

    task_t *p = g_current->next;
    while (p != g_current &&
           (p->state == TASK_DEAD || p->state == TASK_BLOCKED ||
            p->state == TASK_REAPED)) {
        p = p->next;
    }
    return p;
}

uint64_t sched_yield_from_frame(uint64_t sp) {
    task_t *prev = g_current;
    task_t *next = pick_next();
    if (!next || next == prev) return sp;

    if (prev) prev->sp_saved = sp;
    g_current = next;


    // Переключение TTBR0 с ASID. Без TLB flush: записи предыдущей
    // задачи остаются в TLB, помеченные её ASID.
    if (next->ttbr0_pa) {
        mmu_switch_ttbr0(next->ttbr0_pa, next->asid);
    } else {
        mmu_switch_ttbr0(mmu_get_empty_l0_pa(), 0);
    }

    if (prev && prev->state != TASK_DEAD && prev->state != TASK_BLOCKED)
        prev->state = TASK_READY;
    if (next->state != TASK_DEAD && next->state != TASK_BLOCKED)
        next->state = TASK_RUNNING;

    return next->sp_saved;
}

void sched_wake_blocked(void) {
    if (!g_head) return;
    task_t *p = g_head;
    do {
        if (p->state == TASK_BLOCKED) p->state = TASK_READY;
        p = p->next;
    } while (p != g_head);
}

void sched_start(void) {
    if (!g_head) return;
    g_current = g_head;
    g_current->state = TASK_RUNNING;
    if (g_current->ttbr0_pa) mmu_switch_ttbr0(g_current->ttbr0_pa, g_current->asid);
    jump_to_frame(g_current->sp_saved);
}

task_t *sched_current(void) { return g_current; }

task_t *sched_first(void) { return g_head; }
