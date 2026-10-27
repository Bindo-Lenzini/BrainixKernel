// BrainixKernel — mouse.c
// virtio-tablet через PCI. desc.addr — PA.

#include <stdint.h>
#include "mouse.h"
#include "virtio.h"
#include "pci.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

#define EV_SYN  0x00
#define EV_KEY  0x01
#define EV_ABS  0x03

#define ABS_X   0x00
#define ABS_Y   0x01

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

static virtio_mmio_t g_io;
static virtqueue_t   g_evq;
static int           g_ready = 0;

static uint8_t *g_event_bufs = 0;
static uint16_t g_last_used = 0;

static int g_x_raw = 0;
static int g_y_raw = 0;
static int g_btn_left = 0;
static int g_btn_left_pressed_flag = 0;

int mouse_x(void) { return g_x_raw; }
int mouse_y(void) { return g_y_raw; }
int mouse_left_pressed(void) { return g_btn_left_pressed_flag; }
int mouse_left_down(void) { return g_btn_left; }

int mouse_init(uint64_t ecam_base) {
    g_ready = 0;

    int b, d, f;
    if (!pci_find_device_nth(ecam_base, 0x1AF4, 0x1052, 1, &b, &d, &f)) {
        return 0;
    }

    uint32_t bar4 = pci_cfg_read32(ecam_base, b, d, f, 0x10 + 4 * 4);
    if ((bar4 & ~0xFull) == 0) {
        pci_assign_bar64(ecam_base, b, d, f, 4, 0x10200000ull);
        dsb_sy();
    }

    if (!pci_parse_virtio_caps(ecam_base, b, d, f, &g_io)) return 0;

    virtio_state_t st;
    virtio_transport_init(&g_io, &st);
    if (!virtio_set_features(&g_io, VIRTIO_F_VERSION_1, &st)) return 0;
    if (!virtio_setup_queue(&g_io, 0, &g_evq)) return 0;
    if (!virtio_activate(&g_io, &st)) return 0;

    g_event_bufs = (uint8_t *)phys_alloc_page();
    if (!g_event_bufs) return 0;

    for (int i = 0; i < g_evq.queue_size; i++) {
        g_evq.desc[i].addr  = KVA_TO_PA(g_event_bufs + i * 8);
        g_evq.desc[i].len   = 8;
        g_evq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_evq.desc[i].next  = 0;
    }
    {
        uint64_t a = (uint64_t)g_evq.desc & ~63ull;
        uint64_t e = ((uint64_t)g_evq.desc + sizeof(virtq_desc_t) * g_evq.queue_size + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
    }
    for (int i = 0; i < g_evq.queue_size; i++) g_evq.avail->ring[i] = (uint16_t)i;
    g_evq.avail->idx = g_evq.queue_size;
    {
        uint64_t a = (uint64_t)g_evq.avail & ~63ull;
        uint64_t e = ((uint64_t)g_evq.avail + sizeof(virtq_avail_t) + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
    }
    dsb_sy();

    virtio_queue_notify(&g_io, &g_evq);

    g_last_used = 0;
    g_ready = 1;
    return 1;
}

static int g_syncing = 0;

void mouse_poll(void) {
    if (!g_ready) return;

    g_btn_left_pressed_flag = 0;

    {
        uint64_t a = (uint64_t)g_evq.used & ~63ull;
        uint64_t e = ((uint64_t)g_evq.used + sizeof(virtq_used_t) + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc ivac, %0" :: "r"(a)); a += 64; }
    }
    dsb_sy();

    uint16_t cur = g_evq.used->idx;

    while (g_last_used != cur) {
        uint32_t id  = g_evq.used->ring[g_last_used % g_evq.queue_size].id;
        uint32_t len = g_evq.used->ring[g_last_used % g_evq.queue_size].len;

        if (id < g_evq.queue_size && len == 8) {
            uint8_t *evp = g_event_bufs + id * 8;
            uint64_t a = (uint64_t)evp & ~63ull;
            __asm__ volatile("dc ivac, %0" :: "r"(a));
            dsb_sy();

            virtio_input_event_t *ev = (virtio_input_event_t *)evp;

            if (ev->type == EV_ABS) {
                if (ev->code == ABS_X) g_x_raw = (int)ev->value;
                if (ev->code == ABS_Y) g_y_raw = (int)ev->value;
                g_syncing = 1;
            } else if (ev->type == EV_KEY) {
                if (ev->code == BTN_LEFT) {
                    if (ev->value == 1 && g_btn_left == 0) g_btn_left_pressed_flag = 1;
                    g_btn_left = (ev->value != 0);
                }
            }

            if (ev->type == EV_SYN && g_syncing) g_syncing = 0;
        }

        g_evq.avail->ring[g_evq.avail->idx % g_evq.queue_size] = (uint16_t)id;
        dsb_sy();
        g_evq.avail->idx++;
        {
            uint64_t a = (uint64_t)g_evq.avail & ~63ull;
            uint64_t e = ((uint64_t)g_evq.avail + sizeof(virtq_avail_t) + 63) & ~63ull;
            while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
        }
        dsb_sy();

        g_last_used++;
    }

    if (cur != 0) virtio_queue_notify(&g_io, &g_evq);
}