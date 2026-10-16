// BrainixKernel (BKOS) — dtb.h
#ifndef BKOS_DTB_H
#define BKOS_DTB_H

#include <stdint.h>

#define DTB_MAX_RAM_BANKS 4

typedef struct {
    uint64_t base;
    uint64_t size;
} ram_bank_t;

typedef struct {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    char     format[16];
    int      valid;
} fb_info_t;

typedef struct {
    uint64_t ecam_base;
    uint64_t ecam_size;
} pci_ecam_t;

void dtb_set(void *ptr);
const void *dtb_get_ptr(void);
int dtb_get_ram_banks(ram_bank_t *out, int max);
int dtb_get_framebuffer(fb_info_t *out);
int dtb_get_pci_ecam(pci_ecam_t *out);

#endif