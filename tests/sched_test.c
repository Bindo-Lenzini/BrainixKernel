#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include "sched.h"
#include "mmu.h"

volatile int g_in_syscall;

static jmp_buf scheduler_start;
static task_t *destroyed_tasks[8];
static unsigned int destroyed_task_count;

void task_destroy(task_t *task) {
    if (destroyed_task_count < sizeof(destroyed_tasks) / sizeof(destroyed_tasks[0]))
        destroyed_tasks[destroyed_task_count++] = task;
}

void mmu_switch_ttbr0(uint64_t l0_pa, uint16_t asid) {
    (void)l0_pa;
    (void)asid;
}

uint64_t mmu_get_empty_l0_pa(void) {
    return 0;
}

void jump_to_frame(uint64_t sp) {
    (void)sp;
    longjmp(scheduler_start, 1);
}

static int task_is_scheduled(task_t *task) {
    task_t *first = sched_first();
    if (!first) return 0;

    task_t *current = first;
    do {
        if (current == task) return 1;
        current = current->next;
    } while (current != first);
    return 0;
}

static int check(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        return 0;
    }
    printf("PASS: %s\n", description);
    return 1;
}

int main(void) {
    task_t init = {.id = 1, .state = TASK_READY};
    task_t parent = {.id = 2, .state = TASK_READY};
    task_t child = {.id = 3, .state = TASK_READY, .parent_id = 2};
    task_t outsider = {.id = 4, .state = TASK_READY};
    task_t dead = {.id = 5, .state = TASK_DEAD};
    int passed = 1;

    sched_add(&init);
    sched_add(&parent);
    sched_add(&child);

    passed &= check(!sched_set_reaper(&outsider),
                    "reaper registration rejects tasks outside the scheduler");
    passed &= check(!sched_set_reaper(&dead),
                    "reaper registration rejects dead tasks");
    passed &= check(sched_set_reaper(&init),
                    "live scheduled task can become the reaper");

    sched_reparent_children(parent.id);
    passed &= check(child.parent_id == init.id,
                    "orphan is adopted by the registered reaper");

    sched_reparent_children(init.id);
    passed &= check(child.parent_id == 0,
                    "orphan falls back to kernel ownership when reaper exits");

    child.parent_id = parent.id;
    sched_reparent_children(parent.id);
    passed &= check(child.parent_id == 0,
                    "orphan falls back to kernel ownership without a reaper");

    task_t orphan = {.id = 10, .state = TASK_DEAD};
    task_t adopted = {.id = 11, .state = TASK_READY, .parent_id = 10};
    sched_add(&orphan);
    sched_add(&adopted);
    passed &= check(sched_set_reaper(&init),
                    "live task can be restored as the orphan reaper");

    if (setjmp(scheduler_start) == 0) sched_start();
    passed &= check(sched_current() == &init,
                    "scheduler starts with the first runnable task");

    (void)sched_yield_from_frame(0x1000);
    passed &= check(task_is_scheduled(&orphan) && destroyed_task_count == 0,
                    "dead orphan is retained outside syscall scheduling");

    g_in_syscall = 1;
    (void)sched_yield_from_frame(0x2000);
    g_in_syscall = 0;
    passed &= check(!task_is_scheduled(&orphan) &&
                    destroyed_task_count == 1 &&
                    destroyed_tasks[0] == &orphan,
                    "dead orphan is removed and destroyed at syscall scheduling");
    passed &= check(adopted.parent_id == init.id,
                    "dead orphan's child is adopted before teardown");
    passed &= check(sched_current() == &child &&
                    child.state == TASK_RUNNING,
                    "scheduler switches to a runnable task after orphan teardown");

    uint64_t blocked_frame[0x120 / sizeof(uint64_t)] = {0};
    blocked_frame[0] = 3;
    blocked_frame[0x100 / sizeof(uint64_t)] = 0x1004;
    uint64_t blocked_sp = (uint64_t)blocked_frame;
    uint64_t next_sp = sched_block_retry(blocked_sp);
    passed &= check(next_sp != blocked_sp &&
                    blocked_frame[0x100 / sizeof(uint64_t)] == 0x1000 &&
                    blocked_frame[0] == 3 &&
                    child.state == TASK_BLOCKED &&
                    sched_current() != &child,
                    "blocking syscall saves a retry frame and switches tasks");

    sched_wake_blocked();
    passed &= check(child.state == TASK_READY,
                    "event wake makes blocked syscall task runnable");

    task_t *current = sched_current();
    for (task_t *task = sched_first(); task; ) {
        task->state = task == current ? TASK_RUNNING : TASK_BLOCKED;
        task = task->next;
        if (task == sched_first()) break;
    }
    uint64_t idle_frame[0x120 / sizeof(uint64_t)] = {0};
    idle_frame[0x100 / sizeof(uint64_t)] = 0x2004;
    uint64_t idle_sp = (uint64_t)idle_frame;
    passed &= check(sched_block_retry(idle_sp) == idle_sp &&
                    idle_frame[0x100 / sizeof(uint64_t)] == 0x2004 &&
                    current->state == TASK_RUNNING,
                    "blocking falls back without corrupting frame when no peer is runnable");

    return passed ? 0 : 1;
}
