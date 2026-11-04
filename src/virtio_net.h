// BrainixKernel — virtio_net.h
#ifndef BRAINIX_VIRTIO_NET_H
#define BRAINIX_VIRTIO_NET_H

#include <stdint.h>

#define VNET_MAC_LEN       6
#define VNET_MAX_FRAME     1514
#define VNET_RX_BUFS       64      // было 32, теперь == queue_size
#define VNET_MAX_QUEUES    3       // RX, TX, control
#define VNET_RX_HDR_SIZE   12
#define VNET_RX_BUF_SIZE   2048

static inline uint16_t vnet_used_pending(uint16_t last, uint16_t current) {
    return (uint16_t)(current - last);
}

static inline int vnet_rx_completion_valid(uint16_t pending,
                                           uint16_t queue_size,
                                           uint32_t id, uint32_t len,
                                           int in_flight, int seen) {
    return queue_size != 0 && queue_size <= VNET_RX_BUFS &&
           pending != 0 && pending <= queue_size &&
           id < queue_size && in_flight && !seen &&
           len <= VNET_RX_BUF_SIZE;
}

static inline int vnet_rx_frame_length_valid(uint32_t len,
                                              uint32_t max_frame) {
    return len > VNET_RX_HDR_SIZE &&
           len - VNET_RX_HDR_SIZE <= max_frame;
}

int  vnet_init(uint64_t ecam_base);
int  vnet_ready(void);
void vnet_get_mac(uint8_t *out);
int  vnet_send(const void *frame, uint32_t len);
int  vnet_recv(void *frame, uint32_t max, uint32_t *out_len);

#endif