// BrainixKernel — ipc.c

#include "ipc.h"
#include "physmem.h"

typedef struct {
    uint16_t len;
    int transfer_idx;
    uint8_t transfer_endpoint;
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
    ipc_queue_t inbox[2];
    void *shared_page;
} ipc_channel_t;

static ipc_channel_t g_channels[IPC_MAX];

static void release_channel(ipc_channel_t *channel) {
    int released_idx = (int)(channel - g_channels);
    int transfers[IPC_QUEUE_DEPTH * 2];
    uint8_t endpoints[IPC_QUEUE_DEPTH * 2];
    int transfer_count = 0;
    for (int q = 0; q < 2; q++) {
        ipc_queue_t *queue = &channel->inbox[q];
        for (uint8_t i = 0; i < queue->count; i++) {
            uint8_t slot = (uint8_t)((queue->head + i) % IPC_QUEUE_DEPTH);
            ipc_message_t *message = &queue->messages[slot];
            if (message->transfer_idx >= 0) {
                transfers[transfer_count] = message->transfer_idx;
                endpoints[transfer_count++] = message->transfer_endpoint;
                message->transfer_idx = -1;
            }
        }
    }
    if (channel->shared_page) phys_free_page(channel->shared_page);
    channel->shared_page = 0;
    channel->used = 0;
    channel->refs[0] = 0;
    channel->refs[1] = 0;
    for (int i = 0; i < transfer_count; i++) {
        if (transfers[i] != released_idx)
            ipc_close(transfers[i], endpoints[i]);
    }
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
        channel->shared_page = shared_page;
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            channel->inbox[endpoint].head = 0;
            channel->inbox[endpoint].count = 0;
            for (int slot = 0; slot < IPC_QUEUE_DEPTH; slot++)
                channel->inbox[endpoint].messages[slot].transfer_idx = -1;
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

static int send_message(int idx, uint8_t endpoint, const void *message,
                        uint32_t len, int transfer_idx,
                        uint8_t transfer_endpoint) {
    if (!valid_endpoint(idx, endpoint) || !message ||
        len == 0 || len > IPC_MESSAGE_MAX) {
        return -1;
    }
    if (transfer_idx >= 0 &&
        (transfer_idx == idx || transfer_endpoint >= 2 ||
         !valid_endpoint(transfer_idx, transfer_endpoint) ||
         has_transfer_path(transfer_idx, idx))) {
        return -1;
    }

    ipc_channel_t *channel = &g_channels[idx];
    uint8_t peer = (uint8_t)(endpoint ^ 1u);
    if (channel->refs[peer] == 0) return -1;

    ipc_queue_t *queue = &channel->inbox[peer];
    if (queue->count == IPC_QUEUE_DEPTH) return IPC_EAGAIN;
    if (transfer_idx >= 0 &&
        g_channels[transfer_idx].refs[transfer_endpoint] == UINT16_MAX)
        return -1;

    uint8_t slot = (uint8_t)((queue->head + queue->count) % IPC_QUEUE_DEPTH);
    ipc_message_t *entry = &queue->messages[slot];
    const uint8_t *src = (const uint8_t *)message;
    for (uint32_t i = 0; i < len; i++) entry->data[i] = src[i];
    entry->len = (uint16_t)len;
    entry->transfer_idx = transfer_idx;
    entry->transfer_endpoint = transfer_endpoint;
    if (transfer_idx >= 0) ipc_ref(transfer_idx, transfer_endpoint);
    queue->count++;
    return (int)len;
}

int ipc_send(int idx, uint8_t endpoint, const void *message, uint32_t len) {
    return send_message(idx, endpoint, message, len, -1, 0);
}

int ipc_send_endpoint(int idx, uint8_t endpoint, const void *message,
                      uint32_t len, int transfer_idx,
                      uint8_t transfer_endpoint) {
    if (transfer_idx < 0) return -1;
    return send_message(idx, endpoint, message, len, transfer_idx,
                        transfer_endpoint);
}

int ipc_peek_endpoint(int idx, uint8_t endpoint, void *message,
                      uint32_t capacity, int *transfer_idx,
                      uint8_t *transfer_endpoint) {
    if (!valid_endpoint(idx, endpoint) || !message) return -1;

    ipc_channel_t *channel = &g_channels[idx];
    ipc_queue_t *queue = &channel->inbox[endpoint];
    if (queue->count == 0) {
        return channel->refs[endpoint ^ 1u] == 0 ? 0 : IPC_EAGAIN;
    }

    ipc_message_t *entry = &queue->messages[queue->head];
    if (capacity < entry->len) return -1;
    uint8_t *dst = (uint8_t *)message;
    for (uint16_t i = 0; i < entry->len; i++) dst[i] = entry->data[i];
    if (transfer_idx) *transfer_idx = entry->transfer_idx;
    if (transfer_endpoint)
        *transfer_endpoint = entry->transfer_endpoint;
    return entry->len;
}

int ipc_peek(int idx, uint8_t endpoint, void *message, uint32_t capacity) {
    int transfer_idx = -1;
    int result = ipc_peek_endpoint(idx, endpoint, message, capacity,
                                   &transfer_idx, 0);
    return result > 0 && transfer_idx >= 0 ? -1 : result;
}

int ipc_consume(int idx, uint8_t endpoint) {
    int transfer_idx = -1;
    uint8_t transfer_endpoint = 0;
    int result = ipc_consume_endpoint(idx, endpoint, &transfer_idx,
                                      &transfer_endpoint);
    if (result && transfer_idx >= 0)
        ipc_close(transfer_idx, transfer_endpoint);
    return result;
}

int ipc_consume_endpoint(int idx, uint8_t endpoint, int *transfer_idx,
                         uint8_t *transfer_endpoint) {
    if (!valid_endpoint(idx, endpoint)) return 0;
    ipc_queue_t *queue = &g_channels[idx].inbox[endpoint];
    if (queue->count == 0) return 0;

    ipc_message_t *message = &queue->messages[queue->head];
    if (message->transfer_idx >= 0 &&
        (!transfer_idx || !transfer_endpoint)) return 0;
    if (transfer_idx) *transfer_idx = message->transfer_idx;
    if (transfer_endpoint)
        *transfer_endpoint = message->transfer_endpoint;
    message->transfer_idx = -1;
    queue->head = (uint8_t)((queue->head + 1) % IPC_QUEUE_DEPTH);
    queue->count--;
    return 1;
}

void *ipc_shared_page(int idx) {
    if (idx < 0 || idx >= IPC_MAX || !g_channels[idx].used) return 0;
    return g_channels[idx].shared_page;
}
