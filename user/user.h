// BrainixKernel — user.h
#ifndef USER_H
#define USER_H

#include <stdint.h>

#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_YIELD   2
#define SYS_READ    3
#define SYS_OPEN    4
#define SYS_CLOSE   5
#define SYS_BRK     6
#define SYS_EXEC    7
#define SYS_FORK    8
#define SYS_WAIT    9
#define SYS_READDIR 10
#define SYS_PIPE    11
#define SYS_DUP2    12
#define SYS_SOCKET   13
#define SYS_SENDTO   14
#define SYS_RECVFROM 15
#define SYS_BIND     16
#define SYS_CONNECT  17
#define SYS_GETUID   23
#define SYS_GETEUID  24
#define SYS_GETGID   25
#define SYS_GETEGID  26

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREAT   0x40
#define O_TRUNC   0x80

// EAGAIN — из read/write на pipe.
#define EAGAIN    (-2)

long write(int fd, const void *buf, unsigned long n);
long read (int fd, void *buf, unsigned long n);
void exit  (int code);
int  open  (const char *name, int flags);
int  close (int fd);
void yield (void);
void *brk  (void *new_end);
int  exec  (const char *path, char *const argv[]);
int  fork  (void);
int  wait  (int pid);
int  readdir(int idx, char *buf, int max);
int  pipe  (int fds[2]);
int  dup2  (int oldfd, int newfd);
int  socket(int domain, int type, int protocol);
int  bind  (int fd, unsigned short port_be);
int  connect(int fd, const unsigned char ip[4], unsigned short port_host);
long send   (int fd, const void *buf, unsigned long len);
long recv   (int fd, void *buf, unsigned long len);
long sendto(int fd, const void *buf, unsigned long len,
            unsigned short dst_port_be, const unsigned char dst_ip[4]);
long recvfrom(int fd, void *buf, unsigned long len,
              unsigned short *src_port_be, unsigned char src_ip[4]);

// Credentials (uid/gid) — inline stubs.
static inline long __bk_syscall0(long nr) {
    register long x0 __asm__("x0");
    register long x8 __asm__("x8") = nr;
    __asm__ volatile ("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}
static inline long getuid(void)  { return __bk_syscall0(SYS_GETUID); }
static inline long geteuid(void) { return __bk_syscall0(SYS_GETEUID); }
static inline long getgid(void)  { return __bk_syscall0(SYS_GETGID); }
static inline long getegid(void) { return __bk_syscall0(SYS_GETEGID); }

#define AF_INET    2
#define SOCK_DGRAM 2
#define SOCK_STREAM 1

#endif
