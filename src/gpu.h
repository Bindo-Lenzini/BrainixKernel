// BrainixKernel — gpu.h
#ifndef BRAINIX_GPU_H
#define BRAINIX_GPU_H

#include <stdint.h>
#include "virtio.h"

#define VIRTIO_GPU_CTRL_HDR_SIZE 24u

static inline int virtio_gpu_completion_valid(uint16_t last_used,
                                              uint16_t used_idx,
                                              uint16_t queue_size,
                                              uint32_t id, uint32_t len,
                                              uint32_t response_capacity) {
    uint16_t pending = (uint16_t)(used_idx - last_used);
    return queue_size >= 3 && queue_size <= 64 && pending == 1 &&
           id == 0 && response_capacity >= VIRTIO_GPU_CTRL_HDR_SIZE &&
           len == VIRTIO_GPU_CTRL_HDR_SIZE;
}

static inline int gpu_framebuffer_layout(uint32_t width, uint32_t height,
                                         uint32_t *framebuffer_pages,
                                         uint32_t *entries_pages) {
    if (!width || !height || !framebuffer_pages || !entries_pages)
        return 0;
    if (width > INT32_MAX || height > INT32_MAX) return 0;
    uint64_t pixels = (uint64_t)width * height;
    if (pixels > UINT64_MAX / 4) return 0;
    uint64_t bytes = pixels * 4;
    if (bytes > UINT64_MAX - 4095) return 0;
    uint64_t fb_pages = (bytes + 4095) / 4096;
    if (fb_pages == 0 || fb_pages > UINT32_MAX / 16) return 0;
    uint64_t entries_bytes = fb_pages * 16;
    uint64_t backing_pages = (entries_bytes + 4095) / 4096;
    if (backing_pages == 0 || backing_pages > UINT32_MAX) return 0;
    *framebuffer_pages = (uint32_t)fb_pages;
    *entries_pages = (uint32_t)backing_pages;
    return 1;
}

int gpu_init(uint64_t ecam_base);
uint32_t *gpu_create_framebuffer(uint32_t w, uint32_t h);

// Полный flush. Частичный, к сожалению, QEMU обрабатывает неверно.
void gpu_flush(void);
void gpu_flush_rect(int x, int y, int w, int h);

#endif