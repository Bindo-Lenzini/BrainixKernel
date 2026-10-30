// BrainixKernel — sched.h
#ifndef BRAINIX_SCHED_H
#define BRAINIX_SCHED_H

#include "task.h"

void sched_add(task_t *t);
int sched_remove(task_t *t);
int sched_set_reaper(task_t *t);
void sched_reparent_children(uint64_t parent_id);
void sched_start(void);
uint64_t sched_yield_from_frame(uint64_t sp);
task_t *sched_current(void);
task_t *sched_first(void);
void jump_to_frame(uint64_t sp) __attribute__((noreturn));

// Разбудить все TASK_BLOCKED → TASK_READY.
void sched_wake_blocked(void);

#endif