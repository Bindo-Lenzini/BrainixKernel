#include "vfs.h"

#define VFS_MAX_OPEN_FILES 6

typedef struct {
    int endpoint;
    int file_fd;
} vfs_open_file_t;

static vfs_open_file_t open_files[VFS_MAX_OPEN_FILES];

static void close_open_file(vfs_open_file_t *file) {
    close_backend(file->endpoint);
    close_backend(file->file_fd);
    file->endpoint = -1;
    file->file_fd = -1;
}

static int send_open_reply(int endpoint, int status) {
    vfs_reply_t reply = {.status = status};
    return ipc_send_wait(endpoint, &reply, sizeof(reply)) ==
        (long)sizeof(reply);
}

static int valid_open_request(const vfs_open_request_t *request,
                              long length) {
    if (length != (long)sizeof(*request) ||
        request->magic != VFS_REQUEST_MAGIC || request->path[0] == 0) {
        return 0;
    }
    for (unsigned int i = 0; i < VFS_PATH_MAX; i++) {
        if (request->path[i] == 0) return 1;
    }
    return 0;
}

static void handle_open_request(void) {
    vfs_open_request_t request;
    int endpoint = -1;
    unsigned int rights = 0;
    long received = ipc_recv_endpoint(VFS_SERVER_FD, &request,
                                      sizeof(request), &endpoint, &rights);
    if (received <= 0 || endpoint < 0) return;

    if (!valid_open_request(&request, received) ||
        rights != (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE)) {
        send_open_reply(endpoint, -1);
        close_backend(endpoint);
        return;
    }

    int slot = -1;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (open_files[i].endpoint < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        send_open_reply(endpoint, -1);
        close_backend(endpoint);
        return;
    }

    int file_fd = open_backend(request.path, request.flags);
    if (file_fd < 0) {
        send_open_reply(endpoint, -1);
        close_backend(endpoint);
        return;
    }

    unsigned int file_rights =
        FD_RIGHT_READ | FD_RIGHT_WRITE | FD_RIGHT_DELEGATE;
    if ((request.flags & (O_WRONLY | O_RDWR)) == O_WRONLY) {
        file_rights = FD_RIGHT_WRITE | FD_RIGHT_DELEGATE;
    } else if ((request.flags & (O_WRONLY | O_RDWR)) == O_RDONLY) {
        file_rights = FD_RIGHT_READ | FD_RIGHT_DELEGATE;
    }
    if (fd_restrict(file_fd, file_rights) < 0 ||
        vfs_fd_control(endpoint, VFS_FD_REGISTER | (int)file_rights) !=
            (long)file_rights ||
        !send_open_reply(endpoint, 0)) {
        close_backend(file_fd);
        close_backend(endpoint);
        return;
    }

    open_files[slot].endpoint = endpoint;
    open_files[slot].file_fd = file_fd;
}

static void handle_file_request(vfs_open_file_t *file) {
    vfs_io_request_t request;
    long received = ipc_recv(file->endpoint, &request, sizeof(request));
    if (received == IPC_EAGAIN) return;
    if (received == 0) {
        close_open_file(file);
        return;
    }

    vfs_io_reply_t reply = {
        .magic = VFS_IO_MAGIC,
        .result = -1,
        .length = 0,
    };
    if (received < VFS_IO_HEADER_SIZE || request.magic != VFS_IO_MAGIC ||
        request.length > VFS_IO_DATA_MAX) {
        if (ipc_send_wait(file->endpoint, &reply, VFS_IO_HEADER_SIZE) < 0)
            close_open_file(file);
        return;
    }

    if (request.operation == VFS_OP_READ &&
        received == VFS_IO_HEADER_SIZE) {
        int result = (int)read_backend(file->file_fd, reply.data,
                                       request.length);
        if (result >= 0) {
            reply.result = result;
            reply.length = (uint32_t)result;
        }
    } else if (request.operation == VFS_OP_WRITE &&
               (uint32_t)received ==
                   VFS_IO_HEADER_SIZE + request.length) {
        reply.result = (int32_t)write_backend(file->file_fd, request.data,
                                              request.length);
    }

    long reply_size = VFS_IO_HEADER_SIZE + reply.length;
    if (ipc_send_wait(file->endpoint, &reply, (unsigned long)reply_size) < 0)
        close_open_file(file);
}

int main(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].endpoint = -1;
        open_files[i].file_fd = -1;
    }

    for (;;) {
        handle_open_request();
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++)
            if (open_files[i].endpoint >= 0)
                handle_file_request(&open_files[i]);
        yield();
    }
}
