#include "vfs.h"
#include "libc.h"

int vfs_open(const char *path, int flags) {
    if (!path) return -1;
    unsigned long path_len = strlen(path);
    if (path_len == 0 || path_len >= VFS_PATH_MAX) return -1;

    int reply_fds[2] = {-1, -1};
    if (ipc_pair_rights(reply_fds, IPC_RIGHT_RECEIVE,
                        IPC_RIGHT_SEND | IPC_RIGHT_DELEGATE) < 0) {
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
                                 reply_fds[1],
                                 IPC_RIGHT_SEND | IPC_RIGHT_DELEGATE);
        if (sent == IPC_EAGAIN) yield();
    } while (sent == IPC_EAGAIN);
    close(reply_fds[1]);
    if (sent != (long)sizeof(request)) {
        close(reply_fds[0]);
        return -1;
    }

    for (unsigned int attempt = 0; attempt < 100000; attempt++) {
        vfs_reply_t reply = {-1};
        int received_fd = -1;
        unsigned int received_rights = 0;
        long received = ipc_recv_file(reply_fds[0], &reply, sizeof(reply),
                                      &received_fd, &received_rights);
        if (received == IPC_EAGAIN) {
            yield();
            continue;
        }

        close(reply_fds[0]);
        if (received != (long)sizeof(reply) || reply.status != 0 ||
            received_fd < 0) {
            if (received_fd >= 0) close(received_fd);
            return -1;
        }
        return received_fd;
    }

    close(reply_fds[0]);
    return -1;
}
