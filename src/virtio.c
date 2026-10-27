// BrainixKernel — virtio.c
// Транспорт virtio-pci (modern).
// desc/avail/used — KVA. Device получает PA через KVA_TO_PA.

#include <stdint.h>
#include "virtio.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);
extern void uart_puthex(uint64_t v);
extern void uart_putu(uint64_t v);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

#define VIRTIO_COMMON_DFSELECT        0x00
#define VIRTIO_COMMON_DF              0x04
#define VIRTIO_COMMON_DRVSELECT       0x08
#define VIRTIO_COMMON_DRV             0x0C
#define VIRTIO_COMMON_MSIX            0x10
#define VIRTIO_COMMON_NUMQ            0x12
#define VIRTIO_COMMON_STATUS          0x14
#define VIRTIO_COMMON_CFGGEN          0x15
#define VIRTIO_COMMON_Q_SELECT        0x16
#define VIRTIO_COMMON_Q_SIZE          0x18
#define VIRTIO_COMMON_Q_MSIX          0x1A
#define VIRTIO_COMMON_Q_ENABLE        0x1C
#define VIRTIO_COMMON_Q_NOFF          0x1E
#define VIRTIO_COMMON_Q_DESC          0x20
#define VIRTIO_COMMON_Q_DRIVER        0x28
#define VIRTIO_COMMON_Q_DEVICE        0x30

#define VIRTIO_STATUS_ACK        0x01
#define VIRTIO_STATUS_DRIVER     0x02
#define VIRTIO_STATUS_DRIVER_OK  0x04
#define VIRTIO_STATUS_FEAT_OK    0x08

uint8_t virtio_common_read8(const virtio_mmio_t *io, uint32_t off) {
    return *(volatile uint8_t *)(io->common + off);
}
uint16_t virtio_common_read16(const virtio_mmio_t *io, uint32_t off) {
    return *(volatile uint16_t *)(io->common + off);
}
uint32_t virtio_common_read32(const virtio_mmio_t *io, uint32_t off) {
    return *(volatile uint32_t *)(io->common + off);
}
void virtio_common_write8(const virtio_mmio_t *io, uint32_t off, uint8_t v) {
    *(volatile uint8_t *)(io->common + off) = v;
    dsb_sy();
}
void virtio_common_write16(const virtio_mmio_t *io, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(io->common + off) = v;
    dsb_sy();
}
void virtio_common_write32(const virtio_mmio_t *io, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(io->common + off) = v;
    dsb_sy();
}

static uint64_t virtio_read_features(const virtio_mmio_t *io) {
    virtio_common_write32(io, VIRTIO_COMMON_DFSELECT, 0);
    uint32_t lo = virtio_common_read32(io, VIRTIO_COMMON_DF);
    virtio_common_write32(io, VIRTIO_COMMON_DFSELECT, 1);
    uint32_t hi = virtio_common_read32(io, VIRTIO_COMMON_DF);
    return ((uint64_t)hi << 32) | lo;
}

static void mem_zero(void *p, unsigned long bytes) {
    uint8_t *b = (uint8_t *)p;
    for (unsigned long i = 0; i < bytes; i++) b[i] = 0;
}

int virtio_transport_init(const virtio_mmio_t *io, virtio_state_t *out) {
    out->ok = 0;
    out->device_features = 0;
    out->driver_features = 0;
    out->num_queues = 0;
    out->status = 0;

    virtio_common_write8(io, VIRTIO_COMMON_STATUS, 0);

    uint8_t s = VIRTIO_STATUS_ACK;
    virtio_common_write8(io, VIRTIO_COMMON_STATUS, s);

    s |= VIRTIO_STATUS_DRIVER;
    virtio_common_write8(io, VIRTIO_COMMON_STATUS, s);

    out->device_features = virtio_read_features(io);
    out->num_queues = virtio_common_read16(io, VIRTIO_COMMON_NUMQ);
    out->status = virtio_common_read8(io, VIRTIO_COMMON_STATUS);
    out->ok = 1;
    return 1;
}

int virtio_set_features(const virtio_mmio_t *io, uint64_t features, virtio_state_t *out) {
    uint64_t dev = virtio_read_features(io);

    uint64_t accepted = features & dev;
    if (!(accepted & VIRTIO_F_VERSION_1)) return 0;

    out->driver_features = accepted;
    out->device_features = dev;

    virtio_common_write32(io, VIRTIO_COMMON_DRVSELECT, 0);
    virtio_common_write32(io, VIRTIO_COMMON_DRV, (uint32_t)(accepted & 0xFFFFFFFF));
    virtio_common_write32(io, VIRTIO_COMMON_DRVSELECT, 1);
    virtio_common_write32(io, VIRTIO_COMMON_DRV, (uint32_t)(accepted >> 32));

    uint8_t s = virtio_common_read8(io, VIRTIO_COMMON_STATUS);
    s |= VIRTIO_STATUS_FEAT_OK;
    virtio_common_write8(io, VIRTIO_COMMON_STATUS, s);

    uint8_t check = virtio_common_read8(io, VIRTIO_COMMON_STATUS);
    out->status = check;
    if (!(check & VIRTIO_STATUS_FEAT_OK)) return 0;

    out->ok = 1;
    return 1;
}

int virtio_setup_queue(const virtio_mmio_t *io, uint16_t qidx, virtqueue_t *vq) {
    virtq_desc_t  *desc  = (virtq_desc_t *)phys_alloc_page();
    virtq_avail_t *avail = (virtq_avail_t *)phys_alloc_page();
    virtq_used_t  *used  = (virtq_used_t *)phys_alloc_page();
    if (!desc || !avail || !used) return 0;

    mem_zero(desc,  sizeof(virtq_desc_t)  * VIRTQ_SIZE);
    mem_zero(avail, sizeof(virtq_avail_t));
    mem_zero(used,  sizeof(virtq_used_t));

    vq->queue_index = qidx;
    vq->queue_size  = VIRTQ_SIZE;
    vq->desc        = desc;
    vq->avail       = avail;
    vq->used        = used;
    // desc/avail/used — KVA. Device видит PA.
    vq->desc_phys   = KVA_TO_PA(desc);
    vq->avail_phys  = KVA_TO_PA(avail);
    vq->used_phys   = KVA_TO_PA(used);

    virtio_common_write16(io, VIRTIO_COMMON_Q_SELECT, qidx);

    uint16_t max_size = virtio_common_read16(io, VIRTIO_COMMON_Q_SIZE);
    if (max_size == 0) return 0;
    if (max_size < VIRTQ_SIZE) {
        vq->queue_size = max_size;
    }

    virtio_common_write16(io, VIRTIO_COMMON_Q_SIZE, vq->queue_size);
    virtio_common_write16(io, VIRTIO_COMMON_Q_MSIX, 0);

    virtio_common_write32(io, VIRTIO_COMMON_Q_DESC,   (uint32_t)vq->desc_phys);
    virtio_common_write32(io, VIRTIO_COMMON_Q_DESC+4, (uint32_t)(vq->desc_phys >> 32));
    virtio_common_write32(io, VIRTIO_COMMON_Q_DRIVER, (uint32_t)vq->avail_phys);
    virtio_common_write32(io, VIRTIO_COMMON_Q_DRIVER+4,(uint32_t)(vq->avail_phys >> 32));
    virtio_common_write32(io, VIRTIO_COMMON_Q_DEVICE, (uint32_t)vq->used_phys);
    virtio_common_write32(io, VIRTIO_COMMON_Q_DEVICE+4,(uint32_t)(vq->used_phys >> 32));

    vq->notify_off = virtio_common_read16(io, VIRTIO_COMMON_Q_NOFF);

    virtio_common_write16(io, VIRTIO_COMMON_Q_ENABLE, 1);
    return 1;
}

int virtio_activate(const virtio_mmio_t *io, virtio_state_t *st) {
    uint8_t s = virtio_common_read8(io, VIRTIO_COMMON_STATUS);
    s |= VIRTIO_STATUS_DRIVER_OK;
    virtio_common_write8(io, VIRTIO_COMMON_STATUS, s);
    st->status = virtio_common_read8(io, VIRTIO_COMMON_STATUS);
    return (st->status & VIRTIO_STATUS_DRIVER_OK) ? 1 : 0;
}

void virtio_queue_notify(const virtio_mmio_t *io, const virtqueue_t *vq) {
    uint64_t addr = io->notify + (uint64_t)vq->notify_off * io->notify_mult;
    *(volatile uint16_t *)addr = vq->queue_index;
    dsb_sy();
}