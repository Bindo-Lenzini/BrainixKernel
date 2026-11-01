#ifndef BRAINIX_VFS_PROTOCOL_H
#define BRAINIX_VFS_PROTOCOL_H

#include <stdint.h>

#define VFS_IO_MAGIC 0x314F4956u
#define VFS_IO_DATA_MAX 240u
#define VFS_IO_HEADER_SIZE 12u
#define VFS_FD_REGISTER 0x100

#define VFS_OP_READ  1u
#define VFS_OP_WRITE 2u

typedef struct {
    uint32_t magic;
    uint32_t operation;
    uint32_t length;
    uint8_t data[VFS_IO_DATA_MAX];
} vfs_io_request_t;

typedef struct {
    uint32_t magic;
    int32_t result;
    uint32_t length;
    uint8_t data[VFS_IO_DATA_MAX];
} vfs_io_reply_t;

#endif
