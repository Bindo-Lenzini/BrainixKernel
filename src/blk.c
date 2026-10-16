// BrainixKernel (BKOS) — blk.c
// virtio-blk через PCI. Буферы KVA, дескрипторы получают PA.

#include <stdint.h>
#include "blk.h"
#include "virtio.h"
#include "pci.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);
extern void uart_puthex(uint64_t v);
extern void uart_putu(uint64_t v);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_req_t;

#define VIRTIO_BLK_T_IN    0
#define VIRTIO_BLK_T_OUT   1

static void cache_invalidate(void *addr, uint64_t len) {
    uint64_t a = (uint64_t)addr & ~63ull;
    uint64_t end = ((uint64_t)addr + len + 63) & ~63ull;
    while (a < end) { __asm__ volatile("dc ivac, %0" :: "r"(a)); a += 64; }
    dsb_sy();
}
static void cache_clean(void *addr, uint64_t len) {
    uint64_t a = (uint64_t)addr & ~63ull;
    uint64_t end = ((uint64_t)addr + len + 63) & ~63ull;
    while (a < end) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
    dsb_sy();
}

typedef struct {
    virtio_mmio_t io;
    virtqueue_t   vq;
    int           ready;
    uint64_t      capacity;
    uint8_t      *hdr_buf;
    uint8_t      *sts_buf;
    uint8_t      *dat_buf;
} blk_dev_t;

static blk_dev_t g_devs[BLK_MAX_DEV];
static int       g_dev_count = 0;

int blk_init_nth(uint64_t ecam_base, int skip) {
    if (g_dev_count >= BLK_MAX_DEV) return 0;

    int b, d, f;
    if (!pci_find_device_nth(ecam_base, 0x1AF4, 0x1042, skip, &b, &d, &f)) return 0;

    blk_dev_t *dev = &g_devs[g_dev_count];

    uint32_t bar4 = pci_cfg_read32(ecam_base, b, d, f, 0x10 + 4 * 4);
    if ((bar4 & ~0xFull) == 0) {
        uint64_t bar_addr = (skip == 0) ? 0x10300000ull : 0x10400000ull;
        pci_assign_bar64(ecam_base, b, d, f, 4, bar_addr);
        dsb_sy();
    }

    if (!pci_parse_virtio_caps(ecam_base, b, d, f, &dev->io)) return 0;

    virtio_state_t st;
    virtio_transport_init(&dev->io, &st);
    if (!virtio_set_features(&dev->io, VIRTIO_F_VERSION_1, &st)) return 0;
    if (!virtio_setup_queue(&dev->io, 0, &dev->vq)) return 0;
    if (!virtio_activate(&dev->io, &st)) return 0;

    dev->hdr_buf = (uint8_t *)phys_alloc_page();
    dev->sts_buf = (uint8_t *)phys_alloc_page();
    dev->dat_buf = (uint8_t *)phys_alloc_page();
    if (!dev->hdr_buf || !dev->sts_buf || !dev->dat_buf) return 0;

    dev->capacity = *(volatile uint64_t *)(dev->io.device + 0);
    dev->ready = 1;

    g_dev_count++;
    return 1;
}

int blk_init(uint64_t ecam_base) {
    if (!blk_init_nth(ecam_base, 0)) return 0;
    blk_init_nth(ecam_base, 1);
    return 1;
}

uint64_t blk_capacity_n(int dev) {
    if (dev < 0 || dev >= g_dev_count) return 0;
    return g_devs[dev].capacity;
}

uint64_t blk_capacity(void) { return blk_capacity_n(0); }

static int blk_do(int dev_idx, uint32_t type, uint64_t sector,
                  void *buf, int is_write)
{
    if (dev_idx < 0 || dev_idx >= g_dev_count) return 0;
    blk_dev_t *dev = &g_devs[dev_idx];
    if (!dev->ready) return 0;

    virtio_blk_req_t *hdr = (virtio_blk_req_t *)dev->hdr_buf;
    hdr->type     = type;
    hdr->reserved = 0;
    hdr->sector   = sector;

    *dev->sts_buf = 0xFF;

    if (is_write) {
        const uint8_t *src = (const uint8_t *)buf;
        for (int i = 0; i < 512; i++) dev->dat_buf[i] = src[i];
    }

    cache_clean(dev->hdr_buf, 16);
    if (is_write) cache_clean(dev->dat_buf, 512);
    cache_clean(dev->sts_buf, 1);

    dev->vq.desc[0].addr  = KVA_TO_PA(dev->hdr_buf); dev->vq.desc[0].len = 16;
    dev->vq.desc[0].flags = VIRTQ_DESC_F_NEXT;       dev->vq.desc[0].next = 1;

    dev->vq.desc[1].addr  = KVA_TO_PA(dev->dat_buf); dev->vq.desc[1].len = 512;
    dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    dev->vq.desc[1].next  = 2;

    dev->vq.desc[2].addr  = KVA_TO_PA(dev->sts_buf); dev->vq.desc[2].len = 1;
    dev->vq.desc[2].flags = VIRTQ_DESC_F_WRITE;      dev->vq.desc[2].next = 0;

    cache_clean(dev->vq.desc, sizeof(virtq_desc_t) * 3);

    uint16_t aidx = dev->vq.avail->idx;
    dev->vq.avail->ring[aidx % dev->vq.queue_size] = 0;
    dsb_sy();
    dev->vq.avail->idx = aidx + 1;
    cache_clean(dev->vq.avail, sizeof(virtq_avail_t));

    virtio_queue_notify(&dev->io, &dev->vq);

    uint64_t t = 20000000;
    while (t > 0) {
        cache_invalidate(dev->vq.used, sizeof(virtq_used_t));
        if (dev->vq.used->idx == aidx + 1) break;
        t--;
    }
    if (t == 0) return 0;

    cache_invalidate(dev->sts_buf, 1);
    if (*dev->sts_buf != 0) return 0;

    if (!is_write) {
        cache_invalidate(dev->dat_buf, 512);
        uint8_t *dst = (uint8_t *)buf;
        for (int i = 0; i < 512; i++) dst[i] = dev->dat_buf[i];
    }

    return 1;
}

int blk_read_sector_n(int dev, uint64_t sector, void *buf) {
    return blk_do(dev, VIRTIO_BLK_T_IN, sector, buf, 0);
}

int blk_write_sector_n(int dev, uint64_t sector, const void *buf) {
    return blk_do(dev, VIRTIO_BLK_T_OUT, sector, (void *)buf, 1);
}

int blk_read_sector(uint64_t sector, void *buf) {
    return blk_read_sector_n(0, sector, buf);
}

int blk_write_sector(uint64_t sector, const void *buf) {
    return blk_write_sector_n(0, sector, buf);
}