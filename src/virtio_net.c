// BrainixKernel (BKOS) — virtio_net.c
// virtio-net через PCI (modern, VIRTIO_F_VERSION_1).
//
// ВАЖНО: в modern режиме virtio-net header — 12 байт, не 10!
// Device ID modern virtio-net-pci = 0x1041.
// В Makefile: -device virtio-net-pci,netdev=net0,disable-legacy=on
//
// desc.addr в дескрипторах — PA, буферы (g_tx_buf, g_rx_bufs) — KVA.

#include <stdint.h>
#include "virtio_net.h"
#include "virtio.h"
#include "pci.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);
extern void uart_puthex(uint64_t v);
extern void uart_putu(uint64_t v);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

#define VIRTIO_NET_PCI_MODERN_ID  0x1041u

// virtio-net header для modern: 12 байт.
typedef struct __attribute__((packed)) {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} vnet_hdr_t;

#define VNET_HDR_SIZE  12
#define VNET_BUF_SIZE  2048

#define C_Q_SELECT   0x16
#define C_Q_SIZE     0x18
#define C_Q_MSIX     0x1A
#define C_Q_ENABLE   0x1C
#define C_Q_NOFF     0x1E
#define C_Q_DESC     0x20
#define C_Q_DRIVER   0x28
#define C_Q_DEVICE   0x30

static virtio_mmio_t g_io;
static virtqueue_t   g_rxq;
static virtqueue_t   g_txq;
static int           g_ready = 0;

static uint8_t  *g_rx_bufs[VNET_RX_BUFS];
static uint8_t  *g_tx_buf = 0;

static uint16_t  g_rx_last_used = 0;
static uint16_t  g_tx_last_used = 0;

static uint8_t   g_mac[VNET_MAC_LEN];

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
static void mem_zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}
static void print_hex_byte(uint8_t b) {
    static const char hex[] = "0123456789abcdef";
    char s[3] = { hex[(b >> 4) & 0xF], hex[b & 0xF], 0 };
    uart_puts(s);
}

static int pci_setup_queue(uint16_t qidx, virtqueue_t *vq) {
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
    vq->desc_phys   = KVA_TO_PA(desc);
    vq->avail_phys  = KVA_TO_PA(avail);
    vq->used_phys   = KVA_TO_PA(used);

    virtio_common_write16(&g_io, C_Q_SELECT, qidx);

    uint16_t max_size = virtio_common_read16(&g_io, C_Q_SIZE);
    if (max_size == 0) return 0;
    if (max_size < vq->queue_size) vq->queue_size = max_size;

    virtio_common_write16(&g_io, C_Q_SIZE, vq->queue_size);
    virtio_common_write16(&g_io, C_Q_MSIX, 0);

    virtio_common_write32(&g_io, C_Q_DESC,    (uint32_t)vq->desc_phys);
    virtio_common_write32(&g_io, C_Q_DESC+4,  (uint32_t)(vq->desc_phys >> 32));
    virtio_common_write32(&g_io, C_Q_DRIVER,  (uint32_t)vq->avail_phys);
    virtio_common_write32(&g_io, C_Q_DRIVER+4,(uint32_t)(vq->avail_phys >> 32));
    virtio_common_write32(&g_io, C_Q_DEVICE,  (uint32_t)vq->used_phys);
    virtio_common_write32(&g_io, C_Q_DEVICE+4,(uint32_t)(vq->used_phys >> 32));

    vq->notify_off = virtio_common_read16(&g_io, C_Q_NOFF);

    virtio_common_write16(&g_io, C_Q_ENABLE, 1);
    return 1;
}

static void rx_refill_one(uint16_t id) {
    if (id >= g_rxq.queue_size) return;
    g_rxq.desc[id].addr  = KVA_TO_PA(g_rx_bufs[id]);
    g_rxq.desc[id].len   = VNET_BUF_SIZE;
    g_rxq.desc[id].flags = VIRTQ_DESC_F_WRITE;
    g_rxq.desc[id].next  = 0;
}

static void rx_push_avail(uint16_t id) {
    uint16_t aidx = g_rxq.avail->idx;
    g_rxq.avail->ring[aidx % g_rxq.queue_size] = id;
    dsb_sy();
    g_rxq.avail->idx = aidx + 1;
}

int vnet_init(uint64_t ecam_base) {
    g_ready = 0;

    int b, d, f;
    if (!pci_find_device(ecam_base, 0x1AF4, VIRTIO_NET_PCI_MODERN_ID,
                         &b, &d, &f)) {
        uart_puts("  [net] virtio-net-pci (0x1041) not found\n");
        return 0;
    }
    uart_puts("  [net] found virtio-net-pci at bus=");
    uart_putu((uint64_t)b);
    uart_puts(" dev=");
    uart_putu((uint64_t)d);
    uart_puts(" fn=");
    uart_putu((uint64_t)f);
    uart_puts("\n");

    uint32_t bar4 = pci_cfg_read32(ecam_base, b, d, f, 0x10 + 4 * 4);
    if ((bar4 & ~0xFull) == 0) {
        pci_assign_bar64(ecam_base, b, d, f, 4, 0x10500000ull);
        dsb_sy();
    }

    if (!pci_parse_virtio_caps(ecam_base, b, d, f, &g_io)) {
        uart_puts("  [net] parse virtio caps failed\n");
        return 0;
    }

    virtio_state_t st;
    virtio_transport_init(&g_io, &st);

    uint64_t dev_feat = st.device_features;
    uart_puts("  [net] dev features hi=");
    uart_puthex(dev_feat >> 32);
    uart_puts(" lo=");
    uart_puthex(dev_feat & 0xFFFFFFFFull);
    uart_puts("\n");

    uint64_t wanted   = VIRTIO_F_VERSION_1 | (1ull << 5);
    uint64_t accepted = wanted & dev_feat;

    if (!(accepted & VIRTIO_F_VERSION_1)) {
        uart_puts("  [net] VERSION_1 not offered\n");
        return 0;
    }

    if (!virtio_set_features(&g_io, wanted, &st)) {
        uart_puts("  [net] set_features failed\n");
        return 0;
    }
    uart_puts("  [net] features set\n");

    if (!pci_setup_queue(0, &g_rxq)) {
        uart_puts("  [net] RX queue setup failed\n");
        return 0;
    }
    if (!pci_setup_queue(1, &g_txq)) {
        uart_puts("  [net] TX queue setup failed\n");
        return 0;
    }
    uart_puts("  [net] queues: RX=");
    uart_putu(g_rxq.queue_size);
    uart_puts(" TX=");
    uart_putu(g_txq.queue_size);
    uart_puts("\n");

    if (!virtio_activate(&g_io, &st)) {
        uart_puts("  [net] activate failed\n");
        return 0;
    }

    volatile uint8_t *cfg = (volatile uint8_t *)g_io.device;
    for (int i = 0; i < VNET_MAC_LEN; i++) g_mac[i] = cfg[i];
    uart_puts("  [net] MAC = ");
    for (int i = 0; i < VNET_MAC_LEN; i++) {
        if (i) uart_puts(":");
        print_hex_byte(g_mac[i]);
    }
    uart_puts("\n");

    uint32_t n_rx = g_rxq.queue_size;
    if (n_rx > VNET_RX_BUFS) n_rx = VNET_RX_BUFS;
    for (uint32_t i = 0; i < n_rx; i++) {
        g_rx_bufs[i] = (uint8_t *)phys_alloc_page();
        if (!g_rx_bufs[i]) return 0;
        mem_zero(g_rx_bufs[i], VNET_BUF_SIZE);
        rx_refill_one((uint16_t)i);
    }

    g_tx_buf = (uint8_t *)phys_alloc_page();
    if (!g_tx_buf) return 0;
    mem_zero(g_tx_buf, VNET_BUF_SIZE);

    g_rx_last_used = g_rxq.used->idx;
    g_tx_last_used = g_txq.used->idx;

    for (uint32_t i = 0; i < n_rx; i++) rx_push_avail((uint16_t)i);

    cache_clean(g_rxq.desc,  sizeof(virtq_desc_t) * g_rxq.queue_size);
    cache_clean(g_rxq.avail, sizeof(virtq_avail_t));
    dsb_sy();

    virtio_queue_notify(&g_io, &g_rxq);

    uart_puts("  [net] RX avail=");
    uart_putu(g_rxq.avail->idx);
    uart_puts(" used=");
    uart_putu(g_rxq.used->idx);
    uart_puts("\n");

    uart_puts("  [net] poll test (200ms)...\n");
    uint64_t freq = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t now = 0;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    uint64_t end = now + freq / 5;
    uint16_t last_used = g_rx_last_used;
    int changes = 0;
    while (now < end) {
        cache_invalidate(g_rxq.used, sizeof(virtq_used_t));
        if (g_rxq.used->idx != last_used) {
            changes++;
            uart_puts("  [net] used.idx -> ");
            uart_putu(g_rxq.used->idx);
            uart_puts("\n");
            last_used = g_rxq.used->idx;
        }
        __asm__ volatile("wfe");
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    }
    uart_puts("  [net] poll done, changes=");
    uart_putu((uint64_t)changes);
    uart_puts(" final_used=");
    uart_putu(g_rxq.used->idx);
    uart_puts("\n");

    g_ready = 1;
    return 1;
}

int vnet_ready(void) { return g_ready; }

void vnet_get_mac(uint8_t *out) {
    for (int i = 0; i < VNET_MAC_LEN; i++) out[i] = g_mac[i];
}

int vnet_send(const void *frame, uint32_t len) {
    if (!g_ready) return 0;
    if (len == 0 || len > VNET_MAX_FRAME) return 0;

    vnet_hdr_t *hdr = (vnet_hdr_t *)g_tx_buf;
    mem_zero(hdr, VNET_HDR_SIZE);
    hdr->num_buffers = 0;

    uint8_t *dst = g_tx_buf + VNET_HDR_SIZE;
    const uint8_t *src = (const uint8_t *)frame;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];

    uint32_t total = VNET_HDR_SIZE + len;
    cache_clean(g_tx_buf, total);

    g_txq.desc[0].addr  = KVA_TO_PA(g_tx_buf);
    g_txq.desc[0].len   = total;
    g_txq.desc[0].flags = 0;
    g_txq.desc[0].next  = 0;
    cache_clean(g_txq.desc, sizeof(virtq_desc_t));

    uint16_t aidx = g_txq.avail->idx;
    g_txq.avail->ring[aidx % g_txq.queue_size] = 0;
    dsb_sy();
    g_txq.avail->idx = aidx + 1;
    cache_clean(g_txq.avail, sizeof(virtq_avail_t));

    virtio_queue_notify(&g_io, &g_txq);

    uint64_t t = 40000000;
    while (t > 0) {
        cache_invalidate(g_txq.used, sizeof(virtq_used_t));
        if (g_txq.used->idx != g_tx_last_used) break;
        t--;
    }
    if (t == 0) {
        uart_puts("  [net] TX timeout\n");
        return 0;
    }
    g_tx_last_used = g_txq.used->idx;
    return 1;
}

int vnet_recv(void *frame, uint32_t max, uint32_t *out_len) {
    if (!g_ready) return 0;

    cache_invalidate(g_rxq.used, sizeof(virtq_used_t));
    uint16_t cur = g_rxq.used->idx;
    if (cur == g_rx_last_used) return 0;

    uint32_t got = 0;
    while (g_rx_last_used != cur) {
        uint32_t id  = g_rxq.used->ring[g_rx_last_used % g_rxq.queue_size].id;
        uint32_t len = g_rxq.used->ring[g_rx_last_used % g_rxq.queue_size].len;
        g_rx_last_used++;

        if (id >= VNET_RX_BUFS) continue;
        if (len <= VNET_HDR_SIZE) { rx_push_avail((uint16_t)id); continue; }

        uint8_t *buf = g_rx_bufs[id];
        cache_invalidate(buf, VNET_BUF_SIZE);

        uint32_t data_len = len - VNET_HDR_SIZE;
        if (data_len > max) data_len = max;
        if (data_len > VNET_MAX_FRAME) data_len = VNET_MAX_FRAME;

        const uint8_t *srcp = buf + VNET_HDR_SIZE;
        uint8_t *dstp = (uint8_t *)frame;
        for (uint32_t i = 0; i < data_len; i++) dstp[i] = srcp[i];

        if (out_len) *out_len = data_len;
        got = 1;

        rx_refill_one((uint16_t)id);
        rx_push_avail((uint16_t)id);
        cache_clean(g_rxq.desc,  sizeof(virtq_desc_t) * g_rxq.queue_size);
        cache_clean(g_rxq.avail, sizeof(virtq_avail_t));
        virtio_queue_notify(&g_io, &g_rxq);
        break;
    }
    return got;
}