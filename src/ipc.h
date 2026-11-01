// BrainixKernel — ipc.h
#ifndef BRAINIX_IPC_H
#define BRAINIX_IPC_H

#include <stdint.h>

#define IPC_MAX             16
#define IPC_QUEUE_DEPTH     8
#define IPC_MESSAGE_MAX     256

#define IPC_EAGAIN          (-2)

#define IPC_RIGHT_SEND      0x01
#define IPC_RIGHT_RECEIVE   0x02
#define IPC_RIGHT_MAP       0x04
#define IPC_RIGHT_DELEGATE  0x08
#define IPC_RIGHT_ALL       (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | \
                             IPC_RIGHT_MAP | IPC_RIGHT_DELEGATE)

#define IPC_TRANSFER_NONE     0
#define IPC_TRANSFER_ENDPOINT 1
#define IPC_TRANSFER_INITRD   2
#define IPC_TRANSFER_BKFS     3
#define IPC_TRANSFER_VFS      4

#define IPC_FILE_RIGHT_READ     0x01
#define IPC_FILE_RIGHT_WRITE    0x02
#define IPC_FILE_RIGHT_DELEGATE 0x04
#define IPC_FILE_RIGHT_ALL      0x07

int ipc_alloc(void);
void ipc_abort(int idx);
void ipc_ref(int idx, uint8_t endpoint);
void ipc_close(int idx, uint8_t endpoint);
int ipc_register_vfs_proxy(int idx, uint8_t endpoint, uint8_t rights);
int ipc_vfs_proxy_rights(int idx, uint8_t endpoint);
int ipc_send(int idx, uint8_t endpoint, const void *message, uint32_t len);
int ipc_send_endpoint(int idx, uint8_t endpoint, const void *message,
                      uint32_t len, int transfer_idx,
                      uint8_t transfer_endpoint, uint8_t transfer_rights);
int ipc_send_vfs(int idx, uint8_t endpoint, const void *message,
                 uint32_t len, int transfer_idx,
                 uint8_t transfer_endpoint, uint8_t transfer_rights);
int ipc_send_initrd(int idx, uint8_t endpoint, const void *message,
                    uint32_t len, uintptr_t file, uint32_t offset,
                    uint8_t transfer_rights);
int ipc_send_bkfs(int idx, uint8_t endpoint, const void *message,
                  uint32_t len, int inode, uint32_t offset,
                  uint8_t transfer_rights);
int ipc_peek(int idx, uint8_t endpoint, void *message, uint32_t capacity);
int ipc_peek_endpoint(int idx, uint8_t endpoint, void *message,
                      uint32_t capacity, int *transfer_idx,
                      uint8_t *transfer_endpoint, uint8_t *transfer_rights);
int ipc_peek_vfs(int idx, uint8_t endpoint, void *message, uint32_t capacity,
                 int *transfer_idx, uint8_t *transfer_endpoint,
                 uint8_t *transfer_rights);
int ipc_peek_initrd(int idx, uint8_t endpoint, void *message,
                    uint32_t capacity, uintptr_t *file, uint32_t *offset,
                    uint8_t *transfer_rights);
int ipc_peek_bkfs(int idx, uint8_t endpoint, void *message,
                  uint32_t capacity, int *inode, uint32_t *offset,
                  uint8_t *transfer_rights);
int ipc_consume(int idx, uint8_t endpoint);
int ipc_consume_endpoint(int idx, uint8_t endpoint, int *transfer_idx,
                         uint8_t *transfer_endpoint,
                         uint8_t *transfer_rights);
int ipc_consume_vfs(int idx, uint8_t endpoint, int *transfer_idx,
                    uint8_t *transfer_endpoint, uint8_t *transfer_rights);
int ipc_consume_initrd(int idx, uint8_t endpoint, uintptr_t *file,
                       uint32_t *offset, uint8_t *transfer_rights);
int ipc_consume_bkfs(int idx, uint8_t endpoint, int *inode,
                     uint32_t *offset, uint8_t *transfer_rights);
void *ipc_shared_page(int idx);

#endif
