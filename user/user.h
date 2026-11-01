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
#define SYS_IPC_PAIR 20
#define SYS_IPC_SEND 21
#define SYS_IPC_RECV 22
#define SYS_IPC_MAP  31
#define SYS_IPC_SEND_WAIT 32
#define SYS_IPC_RECV_WAIT 33
#define SYS_IPC_SEND_ENDPOINT 34
#define SYS_IPC_RECV_ENDPOINT 35
#define IPC_EAGAIN   (-2)
#define IPC_MESSAGE_MAX 256
#define IPC_SHARED_PAGE_SIZE 4096
#define IPC_RIGHT_SEND     0x01
#define IPC_RIGHT_RECEIVE  0x02
#define IPC_RIGHT_MAP      0x04
#define IPC_RIGHT_DELEGATE 0x08
#define IPC_RIGHT_ALL      0x0f
#define SYS_GETUID   23
#define SYS_GETEUID  24
#define SYS_GETGID   25
#define SYS_GETEGID  26
#define SYS_GETGROUPS 27
#define SYS_SETGROUPS 28
#define SYS_CHMOD     29
#define SYS_CHOWN     30

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
int  chmod (const char *path, unsigned int mode);
int  chown (const char *path, int uid, int gid);
int  getgroups(int size, unsigned int groups[]);
int  setgroups(int count, const unsigned int groups[]);
void yield (void);
void *brk  (void *new_end);
int  exec  (const char *path, char *const argv[]);
int  fork  (void);
int  wait  (int pid);
int  readdir(int idx, char *buf, int max);
int  pipe  (int fds[2]);
int  ipc_pair(int fds[2]);
long ipc_send(int fd, const void *message, unsigned long len);
long ipc_recv(int fd, void *message, unsigned long capacity);
long ipc_send_wait(int fd, const void *message, unsigned long len);
long ipc_recv_wait(int fd, void *message, unsigned long capacity);
long ipc_send_endpoint(int fd, const void *message, unsigned long len,
                       int transfer_fd, unsigned int transfer_rights);
long ipc_recv_endpoint(int fd, void *message, unsigned long capacity,
                       int *received_fd, unsigned int *received_rights);
int  ipc_map(int fd, void *address);
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
