#ifndef USER_VFS_H
#define USER_VFS_H

#include "user.h"
#include <vfs_protocol.h>

#define VFS_SERVER_FD 3
#define VFS_CLIENT_FD 4
#define VFS_PATH_MAX 64
#define VFS_REQUEST_MAGIC 0x31534656u

typedef struct {
    uint32_t magic;
    int32_t flags;
    char path[VFS_PATH_MAX];
} vfs_open_request_t;

typedef struct {
    int32_t status;
} vfs_reply_t;

int vfs_open(const char *path, int flags);
int open_backend(const char *path, int flags);
long read_backend(int fd, void *buffer, unsigned long length);
long write_backend(int fd, const void *buffer, unsigned long length);
long close_backend(int fd);
long vfs_fd_control(int fd, int rights);
long vfs_file_read(int fd, void *buffer, unsigned long length);
long vfs_file_write(int fd, const void *buffer, unsigned long length);

#endif
