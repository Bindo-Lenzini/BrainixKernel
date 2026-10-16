// BrainixKernel (BKOS) — virtio_net.h
#ifndef BKOS_VIRTIO_NET_H
#define BKOS_VIRTIO_NET_H

#include <stdint.h>

#define VNET_MAC_LEN       6
#define VNET_MAX_FRAME     1514
#define VNET_RX_BUFS       64      // было 32, теперь == queue_size
#define VNET_MAX_QUEUES    3       // RX, TX, control

int  vnet_init(uint64_t ecam_base);
int  vnet_ready(void);
void vnet_get_mac(uint8_t *out);
int  vnet_send(const void *frame, uint32_t len);
int  vnet_recv(void *frame, uint32_t max, uint32_t *out_len);

#endif