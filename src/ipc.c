// BrainixKernel — ipc.c

#include "ipc.h"
#include "bkfs.h"
#include "physmem.h"

typedef struct {
    uint16_t len;
    int transfer_idx;
    uint8_t transfer_endpoint;
    uint8_t transfer_rights;
    uint8_t transfer_type;
    uintptr_t transfer_file;
    int transfer_inode;
    uint32_t transfer_offset;
    uint8_t data[IPC_MESSAGE_MAX];
} ipc_message_t;

typedef struct {
    uint8_t head;
    uint8_t count;
    ipc_message_t messages[IPC_QUEUE_DEPTH];
} ipc_queue_t;

typedef struct {
    int used;
    uint16_t refs[2];
    uint8_t vfs_proxy_rights[2];
    ipc_queue_t inbox[2];
    void *shared_page;
} ipc_channel_t;

static ipc_channel_t g_channels[IPC_MAX];

static void release_channel(ipc_channel_t *channel) {
    int released_idx = (int)(channel - g_channels);
    int transfers[IPC_QUEUE_DEPTH * 2];
    uint8_t endpoints[IPC_QUEUE_DEPTH * 2];
    int transfer_count = 0;
    int inodes[IPC_QUEUE_DEPTH * 2];
    int inode_count = 0;
    for (int q = 0; q < 2; q++) {
        ipc_queue_t *queue = &channel->inbox[q];
        for (uint8_t i = 0; i < queue->count; i++) {
            uint8_t slot = (uint8_t)((queue->head + i) % IPC_QUEUE_DEPTH);
            ipc_message_t *message = &queue->messages[slot];
            if (message->transfer_type == IPC_TRANSFER_ENDPOINT ||
                message->transfer_type == IPC_TRANSFER_VFS) {
                transfers[transfer_count] = message->transfer_idx;
                endpoints[transfer_count++] = message->transfer_endpoint;
            } else if (message->transfer_type == IPC_TRANSFER_BKFS) {
                inodes[inode_count++] = message->transfer_inode;
            }
            message->transfer_idx = -1;
            message->transfer_rights = 0;
            message->transfer_type = IPC_TRANSFER_NONE;
            message->transfer_file = 0;
            message->transfer_inode = -1;
            message->transfer_offset = 0;
        }
    }
    if (channel->shared_page) phys_free_page(channel->shared_page);
    channel->shared_page = 0;
    channel->used = 0;
    channel->refs[0] = 0;
    channel->refs[1] = 0;
    channel->vfs_proxy_rights[0] = 0;
    channel->vfs_proxy_rights[1] = 0;
    for (int i = 0; i < transfer_count; i++) {
        if (transfers[i] != released_idx)
            ipc_close(transfers[i], endpoints[i]);
    }
    for (int i = 0; i < inode_count; i++)
        bkfs_inode_release(inodes[i]);
}

static int valid_endpoint(int idx, uint8_t endpoint) {
    return idx >= 0 && idx < IPC_MAX && endpoint < 2 &&
           g_channels[idx].used && g_channels[idx].refs[endpoint] != 0;
}

static int has_transfer_path(int from, int target) {
    int stack[IPC_MAX];
    uint8_t seen[IPC_MAX] = {0};
    int stack_size = 0;
    stack[stack_size++] = from;
    seen[from] = 1;

    while (stack_size > 0) {
        int current = stack[--stack_size];
        if (current == target) return 1;
        ipc_channel_t *channel = &g_channels[current];
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            ipc_queue_t *queue = &channel->inbox[endpoint];
            for (uint8_t i = 0; i < queue->count; i++) {
                uint8_t slot = (uint8_t)((queue->head + i) % IPC_QUEUE_DEPTH);
                int next = queue->messages[slot].transfer_idx;
                if (next < 0 || seen[next]) continue;
                seen[next] = 1;
                stack[stack_size++] = next;
            }
        }
    }
    return 0;
}

int ipc_alloc(void) {
    for (int i = 0; i < IPC_MAX; i++) {
        ipc_channel_t *channel = &g_channels[i];
        if (channel->used) continue;

        void *shared_page = phys_alloc_page();
        if (!shared_page) return -1;
        uint8_t *bytes = (uint8_t *)shared_page;
        for (uint32_t j = 0; j < PAGE_SIZE; j++) bytes[j] = 0;

        channel->used = 1;
        channel->refs[0] = 0;
        channel->refs[1] = 0;
        channel->vfs_proxy_rights[0] = 0;
        channel->vfs_proxy_rights[1] = 0;
        channel->shared_page = shared_page;
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            channel->inbox[endpoint].head = 0;
            channel->inbox[endpoint].count = 0;
            for (int slot = 0; slot < IPC_QUEUE_DEPTH; slot++) {
                channel->inbox[endpoint].messages[slot].transfer_idx = -1;
                channel->inbox[endpoint].messages[slot].transfer_type =
                    IPC_TRANSFER_NONE;
                channel->inbox[endpoint].messages[slot].transfer_endpoint = 0;
            }
        }
        return i;
    }
    return -1;
}

void ipc_abort(int idx) {
    if (idx < 0 || idx >= IPC_MAX) return;
    ipc_channel_t *channel = &g_channels[idx];
    if (channel->refs[0] == 0 && channel->refs[1] == 0)
        release_channel(channel);
}

void ipc_ref(int idx, uint8_t endpoint) {
    if (idx < 0 || idx >= IPC_MAX || endpoint >= 2 ||
        !g_channels[idx].used) return;
    if (g_channels[idx].refs[endpoint] != UINT16_MAX)
        g_channels[idx].refs[endpoint]++;
}

void ipc_close(int idx, uint8_t endpoint) {
    if (idx < 0 || idx >= IPC_MAX || endpoint >= 2) return;
    ipc_channel_t *channel = &g_channels[idx];
    if (!channel->used || channel->refs[endpoint] == 0) return;

    channel->refs[endpoint]--;
    if (channel->refs[0] == 0 && channel->refs[1] == 0)
        release_channel(channel);
}

int ipc_register_vfs_proxy(int idx, uint8_t endpoint, uint8_t rights) {
    if (!valid_endpoint(idx, endpoint) || rights == 0 ||
        (rights & ~(IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE |
                    IPC_FILE_RIGHT_DELEGATE)) != 0) {
        return -1;
    }

    ipc_channel_t *channel = &g_channels[idx];
    uint8_t peer = (uint8_t)(endpoint ^ 1u);
    if (channel->vfs_proxy_rights[peer] != 0 &&
        channel->vfs_proxy_rights[peer] != rights) {
        return -1;
    }
    channel->vfs_proxy_rights[peer] = rights;
    return 0;
}

int ipc_vfs_proxy_rights(int idx, uint8_t endpoint) {
    if (!valid_endpoint(idx, endpoint)) return -1;
    return g_channels[idx].vfs_proxy_rights[endpoint];
}

static int send_message(int idx, uint8_t endpoint, const void *message,
                        uint32_t len, int transfer_idx,
                        uint8_t transfer_endpoint, uint8_t transfer_rights,
                        uint8_t transfer_type, uintptr_t transfer_file,
                        int transfer_inode, uint32_t transfer_offset) {
    if (!valid_endpoint(idx, endpoint) || !message ||
        len == 0 || len > IPC_MESSAGE_MAX) {
        return -1;
    }
    if (transfer_type == IPC_TRANSFER_ENDPOINT ||
        transfer_type == IPC_TRANSFER_VFS) {
        if (transfer_idx < 0 || transfer_idx == idx ||
            transfer_endpoint >= 2 ||
            !valid_endpoint(transfer_idx, transfer_endpoint) ||
            transfer_rights == 0 ||
            (transfer_type == IPC_TRANSFER_ENDPOINT &&
             (transfer_rights & ~IPC_RIGHT_ALL) != 0) ||
            (transfer_type == IPC_TRANSFER_VFS &&
             (transfer_rights & ~(IPC_FILE_RIGHT_READ |
                                  IPC_FILE_RIGHT_WRITE |
                                  IPC_FILE_RIGHT_DELEGATE)) != 0) ||
            has_transfer_path(transfer_idx, idx)) return -1;
    } else if (transfer_type == IPC_TRANSFER_INITRD) {
        if (transfer_file == 0 || transfer_rights == 0 ||
            (transfer_rights & ~IPC_FILE_RIGHT_ALL) != 0) return -1;
    } else if (transfer_type == IPC_TRANSFER_BKFS) {
        if (transfer_inode < 0 || transfer_rights == 0 ||
            (transfer_rights & ~IPC_FILE_RIGHT_ALL) != 0) return -1;
    } else if (transfer_type != IPC_TRANSFER_NONE || transfer_idx >= 0) {
        return -1;
    }

    ipc_channel_t *channel = &g_channels[idx];
    uint8_t peer = (uint8_t)(endpoint ^ 1u);
    if (channel->refs[peer] == 0) return -1;

    ipc_queue_t *queue = &channel->inbox[peer];
    if (queue->count == IPC_QUEUE_DEPTH) return IPC_EAGAIN;
    if ((transfer_type == IPC_TRANSFER_ENDPOINT ||
         transfer_type == IPC_TRANSFER_VFS) &&
        g_channels[transfer_idx].refs[transfer_endpoint] == UINT16_MAX)
        return -1;
    if (transfer_type == IPC_TRANSFER_BKFS &&
        bkfs_inode_retain(transfer_inode) != BKFS_OK) return -1;

    uint8_t slot = (uint8_t)((queue->head + queue->count) % IPC_QUEUE_DEPTH);
    ipc_message_t *entry = &queue->messages[slot];
    const uint8_t *src = (const uint8_t *)message;
    for (uint32_t i = 0; i < len; i++) entry->data[i] = src[i];
    entry->len = (uint16_t)len;
    entry->transfer_idx = transfer_idx;
    entry->transfer_endpoint = transfer_endpoint;
    entry->transfer_rights = transfer_type != IPC_TRANSFER_NONE
        ? transfer_rights : 0;
    entry->transfer_type = transfer_type;
    entry->transfer_file = transfer_file;
    entry->transfer_inode = transfer_inode;
    entry->transfer_offset = transfer_offset;
    if (transfer_type == IPC_TRANSFER_ENDPOINT ||
        transfer_type == IPC_TRANSFER_VFS)
        ipc_ref(transfer_idx, transfer_endpoint);
    queue->count++;
    return (int)len;
}

int ipc_send(int idx, uint8_t endpoint, const void *message, uint32_t len) {
    return send_message(idx, endpoint, message, len, -1, 0, 0,
                        IPC_TRANSFER_NONE, 0, -1, 0);
}

int ipc_send_endpoint(int idx, uint8_t endpoint, const void *message,
                      uint32_t len, int transfer_idx,
                      uint8_t transfer_endpoint, uint8_t transfer_rights) {
    if (transfer_idx < 0) return -1;
    return send_message(idx, endpoint, message, len, transfer_idx,
                        transfer_endpoint, transfer_rights,
                        IPC_TRANSFER_ENDPOINT, 0, -1, 0);
}

int ipc_send_vfs(int idx, uint8_t endpoint, const void *message,
                 uint32_t len, int transfer_idx,
                 uint8_t transfer_endpoint, uint8_t transfer_rights) {
    if (transfer_idx < 0) return -1;
    return send_message(idx, endpoint, message, len, transfer_idx,
                        transfer_endpoint, transfer_rights,
                        IPC_TRANSFER_VFS, 0, -1, 0);
}

int ipc_send_initrd(int idx, uint8_t endpoint, const void *message,
                    uint32_t len, uintptr_t file, uint32_t offset,
                    uint8_t transfer_rights) {
    return send_message(idx, endpoint, message, len, -1, 0, transfer_rights,
                        IPC_TRANSFER_INITRD, file, -1, offset);
}

int ipc_send_bkfs(int idx, uint8_t endpoint, const void *message,
                  uint32_t len, int inode, uint32_t offset,
                  uint8_t transfer_rights) {
    return send_message(idx, endpoint, message, len, -1, 0, transfer_rights,
                        IPC_TRANSFER_BKFS, 0, inode, offset);
}

int ipc_peek_endpoint(int idx, uint8_t endpoint, void *message,
                      uint32_t capacity, int *transfer_idx,
                      uint8_t *transfer_endpoint, uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint) || !message) return -1;

    ipc_channel_t *channel = &g_channels[idx];
    ipc_queue_t *queue = &channel->inbox[endpoint];
    if (queue->count == 0) {
        return channel->refs[endpoint ^ 1u] == 0 ? 0 : IPC_EAGAIN;
    }

    ipc_message_t *entry = &queue->messages[queue->head];
    if (entry->transfer_type == IPC_TRANSFER_INITRD ||
        entry->transfer_type == IPC_TRANSFER_VFS ||
        entry->transfer_type == IPC_TRANSFER_BKFS) return -1;
    if (capacity < entry->len) return -1;
    uint8_t *dst = (uint8_t *)message;
    for (uint16_t i = 0; i < entry->len; i++) dst[i] = entry->data[i];
    if (transfer_idx)
        *transfer_idx = entry->transfer_type == IPC_TRANSFER_ENDPOINT
            ? entry->transfer_idx : -1;
    if (transfer_endpoint)
        *transfer_endpoint = entry->transfer_endpoint;
    if (transfer_rights)
        *transfer_rights = entry->transfer_type == IPC_TRANSFER_ENDPOINT
            ? entry->transfer_rights : 0;
    return entry->len;
}

int ipc_peek_vfs(int idx, uint8_t endpoint, void *message, uint32_t capacity,
                 int *transfer_idx, uint8_t *transfer_endpoint,
                 uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint) || !message || !transfer_idx ||
        !transfer_endpoint || !transfer_rights) return -1;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) {
        return g_channels[idx].refs[endpoint ^ 1u] == 0 ? 0 : IPC_EAGAIN;
    }

    ipc_message_t *entry = &queue->messages[queue->head];
    if (entry->transfer_type != IPC_TRANSFER_VFS ||
        capacity < entry->len) return -1;
    for (uint16_t i = 0; i < entry->len; i++)
        ((uint8_t *)message)[i] = entry->data[i];
    *transfer_idx = entry->transfer_idx;
    *transfer_endpoint = entry->transfer_endpoint;
    *transfer_rights = entry->transfer_rights;
    return entry->len;
}

int ipc_peek_initrd(int idx, uint8_t endpoint, void *message,
                    uint32_t capacity, uintptr_t *file, uint32_t *offset,
                    uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint) || !message) return -1;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) {
        return g_channels[idx].refs[endpoint ^ 1u] == 0 ? 0 : IPC_EAGAIN;
    }

    ipc_message_t *entry = &queue->messages[queue->head];
    if (entry->transfer_type == IPC_TRANSFER_ENDPOINT ||
        entry->transfer_type == IPC_TRANSFER_VFS ||
        entry->transfer_type == IPC_TRANSFER_BKFS ||
        capacity < entry->len) return -1;
    uint8_t *dst = (uint8_t *)message;
    for (uint16_t i = 0; i < entry->len; i++) dst[i] = entry->data[i];
    if (file) *file = entry->transfer_type == IPC_TRANSFER_INITRD
        ? entry->transfer_file : 0;
    if (offset) *offset = entry->transfer_offset;
    if (transfer_rights)
        *transfer_rights = entry->transfer_type == IPC_TRANSFER_INITRD
            ? entry->transfer_rights : 0;
    return entry->len;
}

int ipc_peek_bkfs(int idx, uint8_t endpoint, void *message,
                  uint32_t capacity, int *inode, uint32_t *offset,
                  uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint) || !message) return -1;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) {
        return g_channels[idx].refs[endpoint ^ 1u] == 0 ? 0 : IPC_EAGAIN;
    }

    ipc_message_t *entry = &queue->messages[queue->head];
    if (entry->transfer_type == IPC_TRANSFER_ENDPOINT ||
        entry->transfer_type == IPC_TRANSFER_VFS ||
        entry->transfer_type == IPC_TRANSFER_INITRD ||
        capacity < entry->len) return -1;
    uint8_t *dst = (uint8_t *)message;
    for (uint16_t i = 0; i < entry->len; i++) dst[i] = entry->data[i];
    if (inode) *inode = entry->transfer_type == IPC_TRANSFER_BKFS
        ? entry->transfer_inode : -1;
    if (offset) *offset = entry->transfer_offset;
    if (transfer_rights)
        *transfer_rights = entry->transfer_type == IPC_TRANSFER_BKFS
            ? entry->transfer_rights : 0;
    return entry->len;
}

int ipc_peek(int idx, uint8_t endpoint, void *message, uint32_t capacity) {
    int transfer_idx = -1;
    int result = ipc_peek_endpoint(idx, endpoint, message, capacity,
                                   &transfer_idx, 0, 0);
    return result > 0 && transfer_idx >= 0 ? -1 : result;
}

int ipc_consume(int idx, uint8_t endpoint) {
    int transfer_idx = -1;
    uint8_t transfer_endpoint = 0;
    int result = ipc_consume_endpoint(idx, endpoint, &transfer_idx,
                                      &transfer_endpoint, 0);
    if (result && transfer_idx >= 0)
        ipc_close(transfer_idx, transfer_endpoint);
    return result;
}

int ipc_consume_initrd(int idx, uint8_t endpoint, uintptr_t *file,
                       uint32_t *offset, uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint)) return 0;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) return 0;
    ipc_message_t *message = &queue->messages[queue->head];
    if (message->transfer_type == IPC_TRANSFER_ENDPOINT ||
        message->transfer_type == IPC_TRANSFER_VFS ||
        message->transfer_type == IPC_TRANSFER_BKFS) return 0;
    if (file) *file = message->transfer_type == IPC_TRANSFER_INITRD
        ? message->transfer_file : 0;
    if (offset) *offset = message->transfer_offset;
    if (transfer_rights)
        *transfer_rights = message->transfer_type == IPC_TRANSFER_INITRD
            ? message->transfer_rights : 0;
    message->transfer_idx = -1;
    message->transfer_endpoint = 0;
    message->transfer_rights = 0;
    message->transfer_type = IPC_TRANSFER_NONE;
    message->transfer_file = 0;
    message->transfer_inode = -1;
    message->transfer_offset = 0;
    queue->head = (uint8_t)((queue->head + 1) % IPC_QUEUE_DEPTH);
    queue->count--;
    return 1;
}

int ipc_consume_bkfs(int idx, uint8_t endpoint, int *inode,
                     uint32_t *offset, uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint)) return 0;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) return 0;
    ipc_message_t *message = &queue->messages[queue->head];
    if (message->transfer_type == IPC_TRANSFER_ENDPOINT ||
        message->transfer_type == IPC_TRANSFER_VFS ||
        message->transfer_type == IPC_TRANSFER_INITRD) return 0;
    int transferred_inode = message->transfer_type == IPC_TRANSFER_BKFS
        ? message->transfer_inode : -1;
    if (inode) *inode = transferred_inode;
    if (offset) *offset = message->transfer_offset;
    if (transfer_rights)
        *transfer_rights = message->transfer_type == IPC_TRANSFER_BKFS
            ? message->transfer_rights : 0;
    message->transfer_idx = -1;
    message->transfer_endpoint = 0;
    message->transfer_rights = 0;
    message->transfer_type = IPC_TRANSFER_NONE;
    message->transfer_file = 0;
    message->transfer_inode = -1;
    message->transfer_offset = 0;
    queue->head = (uint8_t)((queue->head + 1) % IPC_QUEUE_DEPTH);
    queue->count--;
    if (transferred_inode >= 0) bkfs_inode_release(transferred_inode);
    return 1;
}

int ipc_consume_endpoint(int idx, uint8_t endpoint, int *transfer_idx,
                         uint8_t *transfer_endpoint,
                         uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint)) return 0;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) return 0;

    ipc_message_t *message = &queue->messages[queue->head];
    if (message->transfer_type == IPC_TRANSFER_INITRD ||
        message->transfer_type == IPC_TRANSFER_VFS ||
        message->transfer_type == IPC_TRANSFER_BKFS) return 0;
    if (message->transfer_type == IPC_TRANSFER_ENDPOINT &&
        (!transfer_idx || !transfer_endpoint)) return 0;
    if (transfer_idx)
        *transfer_idx = message->transfer_type == IPC_TRANSFER_ENDPOINT
            ? message->transfer_idx : -1;
    if (transfer_endpoint)
        *transfer_endpoint = message->transfer_endpoint;
    if (transfer_rights)
        *transfer_rights = message->transfer_type == IPC_TRANSFER_ENDPOINT
            ? message->transfer_rights : 0;
    message->transfer_idx = -1;
    message->transfer_endpoint = 0;
    message->transfer_rights = 0;
    message->transfer_type = IPC_TRANSFER_NONE;
    message->transfer_file = 0;
    message->transfer_inode = -1;
    message->transfer_offset = 0;
    queue->head = (uint8_t)((queue->head + 1) % IPC_QUEUE_DEPTH);
    queue->count--;
    return 1;
}

int ipc_consume_vfs(int idx, uint8_t endpoint, int *transfer_idx,
                    uint8_t *transfer_endpoint, uint8_t *transfer_rights) {
    if (!valid_endpoint(idx, endpoint) || !transfer_idx ||
        !transfer_endpoint || !transfer_rights) return 0;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) return 0;

    ipc_message_t *message = &queue->messages[queue->head];
    if (message->transfer_type != IPC_TRANSFER_VFS) return 0;
    *transfer_idx = message->transfer_idx;
    *transfer_endpoint = message->transfer_endpoint;
    *transfer_rights = message->transfer_rights;
    message->transfer_idx = -1;
    message->transfer_endpoint = 0;
    message->transfer_rights = 0;
    message->transfer_type = IPC_TRANSFER_NONE;
    message->transfer_file = 0;
    message->transfer_inode = -1;
    message->transfer_offset = 0;
    queue->head = (uint8_t)((queue->head + 1) % IPC_QUEUE_DEPTH);
    queue->count--;
    return 1;
}

void *ipc_shared_page(int idx) {
    if (idx < 0 || idx >= IPC_MAX || !g_channels[idx].used) return 0;
    return g_channels[idx].shared_page;
}
