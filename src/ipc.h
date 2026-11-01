// BrainixKernel — ipc.h
#ifndef BRAINIX_IPC_H
#define BRAINIX_IPC_H

#include <stdint.h>

#define IPC_MAX             8
#define IPC_QUEUE_DEPTH     8
#define IPC_MESSAGE_MAX     256

#define IPC_EAGAIN          (-2)

int ipc_alloc(void);
void ipc_abort(int idx);
void ipc_ref(int idx, uint8_t endpoint);
void ipc_close(int idx, uint8_t endpoint);
int ipc_send(int idx, uint8_t endpoint, const void *message, uint32_t len);
int ipc_send_endpoint(int idx, uint8_t endpoint, const void *message,
                      uint32_t len, int transfer_idx,
                      uint8_t transfer_endpoint);
int ipc_peek(int idx, uint8_t endpoint, void *message, uint32_t capacity);
int ipc_peek_endpoint(int idx, uint8_t endpoint, void *message,
                      uint32_t capacity, int *transfer_idx,
                      uint8_t *transfer_endpoint);
int ipc_consume(int idx, uint8_t endpoint);
int ipc_consume_endpoint(int idx, uint8_t endpoint, int *transfer_idx,
                         uint8_t *transfer_endpoint);
void *ipc_shared_page(int idx);

#endif
