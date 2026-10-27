// BrainixKernel — virtio.h
#ifndef BRAINIX_VIRTIO_H
#define BRAINIX_VIRTIO_H

#include <stdint.h>

#define VIRTIO_F_VERSION_1       (1ull << 32)

// QEMU virtio-gpu control queue даёт max 64 дескриптора.
#define VIRTQ_SIZE 64

// Флаги дескрипторов virtqueue.
#define VIRTQ_DESC_F_NEXT   0x1
#define VIRTQ_DESC_F_WRITE  0x2

typedef struct {
    uint64_t common;
    uint64_t notify;
    uint64_t isr;
    uint64_t device;
    uint32_t notify_mult;
} virtio_mmio_t;

typedef struct {
    int      ok;
    uint64_t device_features;
    uint64_t driver_features;
    uint16_t num_queues;
    uint8_t  status;
} virtio_state_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    struct __attribute__((packed)) {
        uint32_t id;
        uint32_t len;
    } ring[VIRTQ_SIZE];
    uint16_t avail_event;
} virtq_used_t;

typedef struct {
    uint16_t         queue_index;
    uint16_t         queue_size;
    virtq_desc_t    *desc;
    virtq_avail_t   *avail;
    virtq_used_t    *used;
    uint64_t         desc_phys;
    uint64_t         avail_phys;
    uint64_t         used_phys;
    uint16_t         notify_off;
} virtqueue_t;

int virtio_transport_init(const virtio_mmio_t *io, virtio_state_t *out);
int virtio_set_features(const virtio_mmio_t *io, uint64_t features, virtio_state_t *out);
int virtio_setup_queue(const virtio_mmio_t *io, uint16_t qidx, virtqueue_t *vq);
int virtio_activate(const virtio_mmio_t *io, virtio_state_t *st);

uint8_t  virtio_common_read8 (const virtio_mmio_t *io, uint32_t off);
uint16_t virtio_common_read16(const virtio_mmio_t *io, uint32_t off);
uint32_t virtio_common_read32(const virtio_mmio_t *io, uint32_t off);
void     virtio_common_write8 (const virtio_mmio_t *io, uint32_t off, uint8_t  v);
void     virtio_common_write16(const virtio_mmio_t *io, uint32_t off, uint16_t v);
void     virtio_common_write32(const virtio_mmio_t *io, uint32_t off, uint32_t v);

void virtio_queue_notify(const virtio_mmio_t *io, const virtqueue_t *vq);

#endif