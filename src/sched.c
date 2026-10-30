// BrainixKernel — sched.c
//
// Round-robin. При переключении на user-задачу — TTBR0 с её ASID.
// TLB не сбрасываем: записи для каждого ASID сосуществуют.

#include "sched.h"
#include "task.h"
#include "mmu.h"


static task_t *g_head    = 0;
static task_t *g_current = 0;
static uint64_t g_reaper_id;
extern volatile int g_in_syscall;

static task_t *find_task(uint64_t id) {
    if (!g_head || id == 0) return 0;

    task_t *p = g_head;
    do {
        if (p->id == id) return p;
        p = p->next;
    } while (p != g_head);
    return 0;
}

void sched_add(task_t *t) {
    if (!t) return;
    if (!g_head) { g_head = t; t->next = t; return; }

    task_t *p = g_head;
    while (p->next != g_head) p = p->next;
    p->next = t;
    t->next = g_head;
}

int sched_remove(task_t *t) {
    if (!t || !g_head || t == g_current) return 0;

    task_t *prev = g_head;
    while (prev->next != t && prev->next != g_head) prev = prev->next;
    if (prev->next != t) return 0;

    if (t == g_head) g_head = (t->next == t) ? 0 : t->next;
    prev->next = t->next;
    t->next = 0;
    return 1;
}

int sched_set_reaper(task_t *t) {
    if (!t || find_task(t->id) != t ||
        t->state == TASK_DEAD || t->state == TASK_REAPED) {
        return 0;
    }

    g_reaper_id = t->id;
    return 1;
}

void sched_reparent_children(uint64_t parent_id) {
    if (!g_head) return;

    if (parent_id == g_reaper_id) g_reaper_id = 0;
    task_t *reaper = find_task(g_reaper_id);
    if (!reaper || reaper->state == TASK_DEAD ||
        reaper->state == TASK_REAPED || reaper->id == parent_id) {
        reaper = 0;
    }

    task_t *p = g_head;
    do {
        if (p->parent_id == parent_id) {
            p->parent_id = reaper ? reaper->id : 0;
        }
        p = p->next;
    } while (p != g_head);
}

static void reap_dead_orphans(task_t *exclude) {
    if (!g_head) return;

    uint64_t count = 1;
    for (task_t *p = g_head->next; p != g_head; p = p->next) count++;

    task_t *p = g_head;
    for (uint64_t i = 0; i < count && g_head; i++) {
        task_t *next = p->next;
        if (p != exclude && p->state == TASK_DEAD && p->parent_id == 0) {
            sched_reparent_children(p->id);
            if (sched_remove(p)) task_destroy(p);
        }
        p = next;
    }
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
    if (!prev) return sp;

    if (g_in_syscall) reap_dead_orphans(prev);
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

    // Keep timer IRQs from switching away before the first task frame is restored.
#ifndef SCHED_HOST_TEST
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
#endif
    g_current = g_head;
    g_current->state = TASK_RUNNING;
    if (g_current->ttbr0_pa) mmu_switch_ttbr0(g_current->ttbr0_pa, g_current->asid);
    jump_to_frame(g_current->sp_saved);
}

task_t *sched_current(void) { return g_current; }

task_t *sched_first(void) { return g_head; }
