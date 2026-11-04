// BrainixKernel — gpu.c
// virtio-gpu. Все буферы — KVA, дескрипторы получают PA.

#include <stdint.h>
#include "gpu.h"
#include "virtio.h"
#include "pci.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);
extern void uart_puthex(uint64_t v);
extern void uart_putu(uint64_t v);
extern void panic(const char *msg);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA              0x1100
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM       1

typedef struct __attribute__((packed)) {
    uint32_t type; uint32_t flags;
    uint64_t fence_id; uint32_t ctx_id; uint32_t padding;
} virtio_gpu_ctrl_hdr_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id; uint32_t format;
    uint32_t width; uint32_t height;
} virtio_gpu_resource_create_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id; uint32_t nr_entries;
} virtio_gpu_resource_attach_backing_t;

typedef struct __attribute__((packed)) {
    uint64_t addr; uint32_t length; uint32_t padding;
} virtio_gpu_mem_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t x, y, width, height;
} virtio_gpu_rect_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id; uint32_t resource_id;
} virtio_gpu_set_scanout_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset; uint32_t resource_id; uint32_t padding;
} virtio_gpu_transfer_to_host_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id; uint32_t padding;
} virtio_gpu_resource_flush_t;

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
static void mem_zero(void *p, uint64_t bytes) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < bytes; i++) b[i] = 0;
}

static virtio_mmio_t g_io;
static virtqueue_t   g_cq;
static int           g_ready = 0;
static uint16_t      g_last_used = 0;

static uint8_t *g_cmd_buf  = 0;
static uint8_t *g_resp_buf = 0;
static uint8_t *g_entries  = 0;

static uint32_t *g_fb           = 0;
static uint32_t  g_fb_w         = 0;
static uint32_t  g_fb_h         = 0;
static uint32_t  g_fb_pages     = 0;
static uint32_t  g_resource_id  = 1;

static int queue_state_valid(void) {
    cache_invalidate(g_cq.used, sizeof(virtq_used_t));
    if (g_cq.avail->idx == g_last_used &&
        g_cq.used->idx == g_last_used) {
        return 1;
    }
    uart_puts("  [gpu] control queue state out of sync; device disabled\n");
    g_ready = 0;
    return 0;
}

static int wait_response(void *resp, uint32_t resp_len) {
    uint64_t timeout = 20000000;
    uint16_t used_idx = g_last_used;
    while (timeout > 0) {
        cache_invalidate(g_cq.used, sizeof(virtq_used_t));
        used_idx = g_cq.used->idx;
        if (used_idx != g_last_used) break;
        timeout--;
    }
    if (timeout == 0) {
        uart_puts("  [gpu] command timeout; device disabled\n");
        g_ready = 0;
        return 0;
    }

    uint16_t slot = (uint16_t)((uint16_t)(used_idx - 1) %
                               g_cq.queue_size);
    uint32_t id = g_cq.used->ring[slot].id;
    uint32_t len = g_cq.used->ring[slot].len;
    if (!virtio_gpu_completion_valid(g_last_used, used_idx,
                                     g_cq.queue_size, id, len, resp_len)) {
        uart_puts("  [gpu] invalid command completion; device disabled\n");
        g_ready = 0;
        return 0;
    }
    g_last_used = used_idx;
    cache_invalidate(resp, resp_len);
    return 1;
}

static int send2(void *cmd, uint32_t cmd_len, void *resp, uint32_t resp_len) {
    if (!g_ready || !cmd || !resp || cmd_len == 0 || cmd_len > PAGE_SIZE ||
        resp_len < VIRTIO_GPU_CTRL_HDR_SIZE || resp_len > PAGE_SIZE ||
        !queue_state_valid()) return 0;

    cache_clean(cmd, cmd_len);
    g_cq.desc[0].addr  = KVA_TO_PA(cmd);     g_cq.desc[0].len = cmd_len;
    g_cq.desc[0].flags = VIRTQ_DESC_F_NEXT;  g_cq.desc[0].next = 1;
    g_cq.desc[1].addr  = KVA_TO_PA(resp);    g_cq.desc[1].len = resp_len;
    g_cq.desc[1].flags = VIRTQ_DESC_F_WRITE; g_cq.desc[1].next = 0;
    cache_clean(g_cq.desc, sizeof(virtq_desc_t) * g_cq.queue_size);

    uint16_t aidx = g_cq.avail->idx;
    g_cq.avail->ring[aidx % g_cq.queue_size] = 0;
    dsb_sy();
    g_cq.avail->idx = aidx + 1;
    cache_clean(g_cq.avail, sizeof(virtq_avail_t));

    virtio_queue_notify(&g_io, &g_cq);

    return wait_response(resp, resp_len);
}

static int send3(void *cmd, uint32_t cmd_len,
                 void *payload, uint32_t payload_len,
                 void *resp, uint32_t resp_len) {
    if (!g_ready || !cmd || !payload || !resp || cmd_len == 0 ||
        cmd_len > PAGE_SIZE || payload_len == 0 ||
        resp_len < VIRTIO_GPU_CTRL_HDR_SIZE || resp_len > PAGE_SIZE ||
        g_cq.queue_size < 3 ||
        !queue_state_valid()) return 0;

    cache_clean(cmd, cmd_len);
    cache_clean(payload, payload_len);
    g_cq.desc[0].addr  = KVA_TO_PA(cmd);     g_cq.desc[0].len = cmd_len;
    g_cq.desc[0].flags = VIRTQ_DESC_F_NEXT;  g_cq.desc[0].next = 1;
    g_cq.desc[1].addr  = KVA_TO_PA(payload); g_cq.desc[1].len = payload_len;
    g_cq.desc[1].flags = VIRTQ_DESC_F_NEXT;  g_cq.desc[1].next = 2;
    g_cq.desc[2].addr  = KVA_TO_PA(resp);    g_cq.desc[2].len = resp_len;
    g_cq.desc[2].flags = VIRTQ_DESC_F_WRITE; g_cq.desc[2].next = 0;
    cache_clean(g_cq.desc, sizeof(virtq_desc_t) * g_cq.queue_size);

    uint16_t aidx = g_cq.avail->idx;
    g_cq.avail->ring[aidx % g_cq.queue_size] = 0;
    dsb_sy();
    g_cq.avail->idx = aidx + 1;
    cache_clean(g_cq.avail, sizeof(virtq_avail_t));

    virtio_queue_notify(&g_io, &g_cq);

    return wait_response(resp, resp_len);
}

static int resp_ok(void) {
    virtio_gpu_ctrl_hdr_t *hdr = (virtio_gpu_ctrl_hdr_t *)g_resp_buf;
    return hdr->type == VIRTIO_GPU_RESP_OK_NODATA;
}

int gpu_init(uint64_t ecam_base) {
    g_ready = 0;

    int b, d, f;
    if (!pci_find_device(ecam_base, 0x1AF4, 0x1050, &b, &d, &f)) return 0;

    uint32_t bar4 = pci_cfg_read32(ecam_base, b, d, f, 0x10 + 4 * 4);
    if ((bar4 & ~0xFull) == 0) {
        pci_assign_bar64(ecam_base, b, d, f, 4, 0x10000000ull);
        dsb_sy();
    }

    if (!pci_parse_virtio_caps(ecam_base, b, d, f, &g_io)) return 0;

    virtio_state_t st;
    virtio_transport_init(&g_io, &st);
    if (!virtio_set_features(&g_io, VIRTIO_F_VERSION_1, &st)) return 0;
    if (!virtio_setup_queue(&g_io, 0, &g_cq)) return 0;
    if (g_cq.queue_size < 3) return 0;
    cache_invalidate(g_cq.used, sizeof(virtq_used_t));
    g_last_used = g_cq.used->idx;
    if (!virtio_activate(&g_io, &st)) return 0;

    g_ready = 1;
    return 1;
}

uint32_t *gpu_create_framebuffer(uint32_t w, uint32_t h) {
    uint32_t fb_pages;
    uint32_t entries_pages;
    if (!g_ready || !gpu_framebuffer_layout(w, h, &fb_pages,
                                             &entries_pages)) return 0;

    g_fb_w = w;
    g_fb_h = h;
    g_fb_pages = fb_pages;

    g_fb = (uint32_t *)phys_alloc_pages(g_fb_pages);
    if (!g_fb) return 0;

    g_cmd_buf  = (uint8_t *)phys_alloc_page();
    g_resp_buf = (uint8_t *)phys_alloc_page();
    g_entries  = (uint8_t *)phys_alloc_pages(entries_pages);
    if (!g_cmd_buf || !g_resp_buf || !g_entries) return 0;

    mem_zero(g_cmd_buf, 4096);
    mem_zero(g_resp_buf, 4096);

    uart_puts("  [gpu] fb at "); uart_puthex((uint64_t)g_fb);
    uart_puts(" pages="); uart_putu(g_fb_pages); uart_puts("\n");

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_resource_create_2d_t *rc = (virtio_gpu_resource_create_2d_t *)g_cmd_buf;
    rc->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    rc->resource_id = g_resource_id;
    rc->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    rc->width = w; rc->height = h;
    if (!send2(rc, sizeof(*rc), g_resp_buf, 24)) return 0;
    if (!resp_ok()) return 0;
    uart_puts("  [gpu] RESOURCE_CREATE_2D OK\n");

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_resource_attach_backing_t *ab = (virtio_gpu_resource_attach_backing_t *)g_cmd_buf;
    ab->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    ab->resource_id = g_resource_id;
    ab->nr_entries = g_fb_pages;

    virtio_gpu_mem_entry_t *me = (virtio_gpu_mem_entry_t *)g_entries;
    uint64_t fb_pa = KVA_TO_PA(g_fb);
    for (uint32_t i = 0; i < g_fb_pages; i++) {
        me[i].addr = fb_pa + i * 4096;
        me[i].length = 4096;
        me[i].padding = 0;
    }
    if (!send3(ab, sizeof(*ab), g_entries, g_fb_pages * 16, g_resp_buf, 24)) return 0;
    if (!resp_ok()) return 0;
    uart_puts("  [gpu] ATTACH_BACKING OK\n");

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_set_scanout_t *ss = (virtio_gpu_set_scanout_t *)g_cmd_buf;
    ss->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    ss->r.x = 0; ss->r.y = 0; ss->r.width = w; ss->r.height = h;
    ss->scanout_id = 0;
    ss->resource_id = g_resource_id;
    if (!send2(ss, sizeof(*ss), g_resp_buf, 24)) return 0;
    if (!resp_ok()) return 0;
    uart_puts("  [gpu] SET_SCANOUT OK\n");

    return g_fb;
}

void gpu_flush_rect(int x, int y, int w, int h) {
    if (!g_ready || !g_fb || w <= 0 || h <= 0) return;
    int64_t left = x;
    int64_t top = y;
    int64_t width = w;
    int64_t height = h;
    if (left < 0) { width += left; left = 0; }
    if (top < 0) { height += top; top = 0; }
    if (left >= g_fb_w || top >= g_fb_h ||
        width <= 0 || height <= 0) return;
    if (left + width > g_fb_w) width = g_fb_w - left;
    if (top + height > g_fb_h) height = g_fb_h - top;
    if (width <= 0 || height <= 0) return;

    uint32_t rect_x = (uint32_t)left;
    uint32_t rect_y = (uint32_t)top;
    uint32_t rect_w = (uint32_t)width;
    uint32_t rect_h = (uint32_t)height;

    for (uint32_t i = 0; i < rect_h; i++) {
        uint64_t row_offset =
            ((uint64_t)(rect_y + i) * g_fb_w + rect_x) * 4;
        uint8_t *row = (uint8_t *)g_fb + row_offset;
        cache_clean(row, (uint64_t)rect_w * 4);
    }

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_transfer_to_host_2d_t *tr = (virtio_gpu_transfer_to_host_2d_t *)g_cmd_buf;
    tr->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    tr->r.x = rect_x; tr->r.y = rect_y;
    tr->r.width = rect_w; tr->r.height = rect_h;
    tr->offset = ((uint64_t)rect_y * g_fb_w + rect_x) * 4;
    tr->resource_id = g_resource_id;
    if (!send2(tr, sizeof(*tr), g_resp_buf, 24)) return;

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_resource_flush_t *fl = (virtio_gpu_resource_flush_t *)g_cmd_buf;
    fl->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    fl->r.x = rect_x; fl->r.y = rect_y;
    fl->r.width = rect_w; fl->r.height = rect_h;
    fl->resource_id = g_resource_id;
    send2(fl, sizeof(*fl), g_resp_buf, 24);
}

void gpu_flush(void) {
    if (!g_ready || !g_fb) return;

    cache_clean(g_fb, (uint64_t)g_fb_pages * 4096);

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_transfer_to_host_2d_t *tr = (virtio_gpu_transfer_to_host_2d_t *)g_cmd_buf;
    tr->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    tr->r.x = 0; tr->r.y = 0;
    tr->r.width = g_fb_w; tr->r.height = g_fb_h;
    tr->offset = 0;
    tr->resource_id = g_resource_id;
    if (!send2(tr, sizeof(*tr), g_resp_buf, 24)) return;

    mem_zero(g_cmd_buf, 4096);
    virtio_gpu_resource_flush_t *fl = (virtio_gpu_resource_flush_t *)g_cmd_buf;
    fl->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    fl->r.x = 0; fl->r.y = 0;
    fl->r.width = g_fb_w; fl->r.height = g_fb_h;
    fl->resource_id = g_resource_id;
    send2(fl, sizeof(*fl), g_resp_buf, 24);
}