#include "vfs.h"
#include "user.h"

long vfs_file_read(int fd, void *buffer, unsigned long length) {
    if (!buffer && length != 0) return -1;
    if (length == 0) return 0;
    if (length > VFS_IO_DATA_MAX) length = VFS_IO_DATA_MAX;

    vfs_io_request_t request = {
        .magic = VFS_IO_MAGIC,
        .operation = VFS_OP_READ,
        .length = (uint32_t)length,
    };
    if (ipc_send_wait(fd, &request, VFS_IO_HEADER_SIZE) !=
        VFS_IO_HEADER_SIZE) return -1;

    vfs_io_reply_t reply;
    long received = ipc_recv_wait(fd, &reply, sizeof(reply));
    if (received < VFS_IO_HEADER_SIZE || reply.magic != VFS_IO_MAGIC ||
        reply.length > VFS_IO_DATA_MAX ||
        (uint32_t)received != VFS_IO_HEADER_SIZE + reply.length ||
        reply.result < 0 || (uint32_t)reply.result != reply.length) {
        return -1;
    }
    for (uint32_t i = 0; i < reply.length; i++)
        ((uint8_t *)buffer)[i] = reply.data[i];
    return reply.result;
}

long vfs_file_write(int fd, const void *buffer, unsigned long length) {
    if (!buffer && length != 0) return -1;
    if (length == 0) return 0;
    if (length > VFS_IO_DATA_MAX) length = VFS_IO_DATA_MAX;

    vfs_io_request_t request = {
        .magic = VFS_IO_MAGIC,
        .operation = VFS_OP_WRITE,
        .length = (uint32_t)length,
    };
    for (uint32_t i = 0; i < request.length; i++)
        request.data[i] = ((const uint8_t *)buffer)[i];
    if (ipc_send_wait(fd, &request, VFS_IO_HEADER_SIZE + request.length) !=
        (long)(VFS_IO_HEADER_SIZE + request.length)) return -1;

    vfs_io_reply_t reply;
    long received = ipc_recv_wait(fd, &reply, sizeof(reply));
    if (received != VFS_IO_HEADER_SIZE || reply.magic != VFS_IO_MAGIC ||
        reply.length != 0 || reply.result < 0) {
        return -1;
    }
    return reply.result;
}

long read(int fd, void *buffer, unsigned long length) {
    long rights = vfs_fd_control(fd, 0);
    if (rights >= 0) {
        if (!(rights & FD_RIGHT_READ)) return -1;
        return vfs_file_read(fd, buffer, length);
    }
    return read_backend(fd, buffer, length);
}

long write(int fd, const void *buffer, unsigned long length) {
    long rights = vfs_fd_control(fd, 0);
    if (rights >= 0) {
        if (!(rights & FD_RIGHT_WRITE)) return -1;
        return vfs_file_write(fd, buffer, length);
    }
    return write_backend(fd, buffer, length);
}
