// BrainixKernel — signal.h
// Минимальная POSIX-подобная реализация сигналов.
#ifndef BRAINIX_SIGNAL_H
#define BRAINIX_SIGNAL_H

#include <stdint.h>
#include "task.h"
#include "mmu.h"

#define NSIG 32

// Номера сигналов (совместимы с Linux там, где есть).
#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGABRT    6
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19

#define SIG_DFL ((sig_handler_t)0)
#define SIG_IGN ((sig_handler_t)1)

// Для sigprocmask.
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

// Фиксированный VA trampoline-страницы в каждом user-AS.
// Ниже стека (стек: [0x7FFFB000, 0x7FFFF000)), вне зоны кода/heap.
#define SIGTRAMP_VA  0x0000000007FFF000ULL

#define USER_STACK_LIMIT (USER_STACK_TOP - USER_STACK_SIZE)

// Магия сигнального фрейма (проверка в sigreturn).
#define SIG_FRAME_MAGIC 0xB4A1D00D5A17CAFEULL

struct sigaction {
    sig_handler_t sa_handler;   // SIG_DFL / SIG_IGN / функция
    uint64_t      sa_mask;      // маскируемые на время обработки
};

void     signal_init(void);                                  // один раз в kernel_main
void     signal_map_tramp(uint64_t l0_pa, uint16_t asid);    // при create/fork/exec
int      signal_send(task_t *t, int sig);                    // пометить pending
uint64_t signal_deliver(uint64_t sp);                        // на пути в EL0
task_t  *task_by_id(uint64_t id);                            // для SIGCHLD

#endif