// BrainixKernel — syscall.h
#ifndef BRAINIX_SYSCALL_H
#define BRAINIX_SYSCALL_H

#include <stdint.h>

#define SYS_WRITE    0
#define SYS_EXIT     1
#define SYS_YIELD    2
#define SYS_READ     3
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
#define SYS_IPC_PAIR 20
#define SYS_IPC_SEND 21
#define SYS_IPC_RECV 22
#define SYS_GETUID   23
#define SYS_GETEUID  24
#define SYS_GETGID   25
#define SYS_GETEGID  26
#define SYS_GETGROUPS 27
#define SYS_SETGROUPS 28
#define SYS_CHMOD     29
#define SYS_CHOWN     30
#define SYS_IPC_MAP   31
#define SYS_IPC_SEND_WAIT 32
#define SYS_IPC_RECV_WAIT 33
#define SYS_IPC_SEND_ENDPOINT 34
#define SYS_IPC_RECV_ENDPOINT 35
#define SYS_IPC_PAIR_RIGHTS 36
#define SYS_FD_RESTRICT 37
#define SYS_IPC_SEND_FILE 38
#define SYS_IPC_RECV_FILE 39
#define SYS_OPEN_BACKEND  40
#define SYS_VFS_FD        41
#define SYS_IPC_SEND_VFS  42
#define SYS_IPC_RECV_VFS  43

void syscall_console_configure(int ipc_idx);
uint64_t syscall_dispatch(uint64_t sp);

#endif
