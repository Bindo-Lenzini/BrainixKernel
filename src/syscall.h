// BrainixKernel — syscall.h
#ifndef BRAINIX_SYSCALL_H
#define BRAINIX_SYSCALL_H

#include <stdint.h>

#define SYS_WRITE    0
#define SYS_EXIT     1
#define SYS_YIELD    2
#define SYS_READ     3
#define SYS_OPEN     4
#define SYS_CLOSE    5
#define SYS_BRK      6
#define SYS_EXEC     7
#define SYS_FORK     8
#define SYS_WAIT     9
#define SYS_READDIR  10
#define SYS_PIPE     11
#define SYS_DUP2     12
#define SYS_SOCKET   13
#define SYS_SENDTO   14
#define SYS_RECVFROM 15
#define SYS_BIND     16
#define SYS_CONNECT  17
#define SYS_SEND     18
#define SYS_RECV     19
#define SYS_GETUID   23
#define SYS_GETEUID  24
#define SYS_GETGID   25
#define SYS_GETEGID  26

uint64_t syscall_dispatch(uint64_t sp);

#endif
