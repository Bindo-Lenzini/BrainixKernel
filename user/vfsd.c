#include "vfs.h"

static void send_error(int endpoint) {
    vfs_reply_t reply = {-1};
    ipc_send_wait(endpoint, &reply, sizeof(reply));
}

static int valid_request(const vfs_open_request_t *request, long length) {
    if (length != (long)sizeof(*request) ||
        request->magic != VFS_REQUEST_MAGIC || request->path[0] == 0) {
        return 0;
    }
    for (unsigned int i = 0; i < VFS_PATH_MAX; i++) {
        if (request->path[i] == 0) return 1;
    }
    return 0;
}

int main(void) {
    for (;;) {
        vfs_open_request_t request;
        int reply_endpoint = -1;
        unsigned int reply_rights = 0;
        long received = ipc_recv_endpoint(VFS_SERVER_FD, &request,
                                          sizeof(request), &reply_endpoint,
                                          &reply_rights);
        if (received == IPC_EAGAIN) {
            yield();
            continue;
        }
        if (received <= 0) return 0;
        if (reply_endpoint < 0) continue;

        if (!valid_request(&request, received) ||
            reply_rights != (IPC_RIGHT_SEND | IPC_RIGHT_DELEGATE)) {
            send_error(reply_endpoint);
            close(reply_endpoint);
            continue;
        }

        int file_fd = open_backend(request.path, request.flags);
        if (file_fd < 0) {
            send_error(reply_endpoint);
            close(reply_endpoint);
            continue;
        }

        unsigned int file_rights =
            FD_RIGHT_READ | FD_RIGHT_WRITE | FD_RIGHT_DELEGATE;
        if ((request.flags & (O_WRONLY | O_RDWR)) == O_WRONLY) {
            file_rights = FD_RIGHT_WRITE | FD_RIGHT_DELEGATE;
        } else if ((request.flags & (O_WRONLY | O_RDWR)) == O_RDONLY) {
            file_rights = FD_RIGHT_READ | FD_RIGHT_DELEGATE;
        }

        vfs_reply_t reply = {0};
        long sent;
        do {
            sent = ipc_send_file(reply_endpoint, &reply, sizeof(reply),
                                 file_fd, file_rights);
            if (sent == IPC_EAGAIN) yield();
        } while (sent == IPC_EAGAIN);
        close(file_fd);
        if (sent != (long)sizeof(reply)) send_error(reply_endpoint);
        close(reply_endpoint);
    }
}
