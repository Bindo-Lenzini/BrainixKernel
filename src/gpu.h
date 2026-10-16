// BrainixKernel (BKOS) — gpu.h
#ifndef BKOS_GPU_H
#define BKOS_GPU_H

#include <stdint.h>

int gpu_init(uint64_t ecam_base);
uint32_t *gpu_create_framebuffer(uint32_t w, uint32_t h);

// Полный flush. Частичный, к сожалению, QEMU обрабатывает неверно.
void gpu_flush(void);

#endif