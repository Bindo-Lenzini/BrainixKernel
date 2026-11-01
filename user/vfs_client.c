#include "vfs.h"
#include "libc.h"

int vfs_open(const char *path, int flags) {
    if (!path) return -1;
    unsigned long path_len = strlen(path);
    if (path_len == 0 || path_len >= VFS_PATH_MAX) return -1;

    int file_fds[2] = {-1, -1};
    if (ipc_pair_rights(file_fds,
                        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE |
                            IPC_RIGHT_DELEGATE,
                        IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE) < 0) {
        return -1;
    }

    vfs_open_request_t request;
    memset(&request, 0, sizeof(request));
    request.magic = VFS_REQUEST_MAGIC;
    request.flags = flags;
    memcpy(request.path, path, path_len + 1);

    long sent;
    do {
        sent = ipc_send_endpoint(VFS_CLIENT_FD, &request, sizeof(request),
                                 file_fds[0],
                                 IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE);
        if (sent == IPC_EAGAIN) yield();
    } while (sent == IPC_EAGAIN);
    close(file_fds[0]);
    if (sent != (long)sizeof(request)) {
        close(file_fds[1]);
        return -1;
    }

    vfs_reply_t reply = {-1};
    long received = ipc_recv_wait(file_fds[1], &reply, sizeof(reply));
    int access_mode = flags & (O_WRONLY | O_RDWR);
    unsigned int rights = access_mode == O_RDONLY ? FD_RIGHT_READ :
        access_mode == O_WRONLY ? FD_RIGHT_WRITE :
        access_mode == O_RDWR ? FD_RIGHT_READ | FD_RIGHT_WRITE : 0;
    if (rights != 0) rights |= FD_RIGHT_DELEGATE;
    if (received != (long)sizeof(reply) || reply.status != 0 ||
        rights == 0 || vfs_fd_control(file_fds[1], (int)rights) !=
            (long)rights) {
        close(file_fds[1]);
        return -1;
    }
    return file_fds[1];
}

int open(const char *path, int flags) {
    return vfs_open(path, flags);
}
