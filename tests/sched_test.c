#include <stdint.h>
#include <stdio.h>
#include "sched.h"
#include "mmu.h"

volatile int g_in_syscall;

void task_destroy(task_t *task) {
    (void)task;
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
    __builtin_trap();
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

    return passed ? 0 : 1;
}
