// BrainixKernel (BKOS) — pci.c
// Общие PCI helpers.
//
// ECAM адреса (доступ к configuration space) приходят из dtb.c как KVA.
// BAR адреса (доступ к MMIO-регистрам устройств) возвращаются драйверам
// как KVA — потому что kernel обращается к ним напрямую.

#include <stdint.h>
#include "pci.h"
#include "mmu.h"

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static uint64_t cfg_addr(uint64_t ecam_kva, int bus, int dev, int fn, uint32_t off) {
    return ecam_kva
        + ((uint64_t)bus << 20)
        + ((uint64_t)dev << 15)
        + ((uint64_t)fn  << 12)
        + off;
}

uint32_t pci_cfg_read32(uint64_t ecam, int b, int d, int f, uint32_t off) {
    return *(volatile uint32_t *)cfg_addr(ecam, b, d, f, off);
}
uint16_t pci_cfg_read16(uint64_t ecam, int b, int d, int f, uint32_t off) {
    return *(volatile uint16_t *)cfg_addr(ecam, b, d, f, off);
}
uint8_t pci_cfg_read8(uint64_t ecam, int b, int d, int f, uint32_t off) {
    return *(volatile uint8_t *)cfg_addr(ecam, b, d, f, off);
}
void pci_cfg_write32(uint64_t ecam, int b, int d, int f, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)cfg_addr(ecam, b, d, f, off) = v;
    dsb_sy();
}
void pci_cfg_write16(uint64_t ecam, int b, int d, int f, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)cfg_addr(ecam, b, d, f, off) = v;
    dsb_sy();
}

int pci_find_device_nth(uint64_t ecam, uint16_t vendor, uint16_t device,
                        int skip, int *out_bus, int *out_dev, int *out_fn)
{
    int seen = 0;
    for (int bus = 0; bus < 4; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_cfg_read32(ecam, bus, dev, fn, 0x00);
                if (id == 0 || id == 0xFFFFFFFFu) continue;
                uint16_t v = id & 0xFFFF;
                uint16_t d = (id >> 16) & 0xFFFF;
                if (v == vendor && d == device) {
                    if (seen == skip) {
                        *out_bus = bus; *out_dev = dev; *out_fn = fn;
                        return 1;
                    }
                    seen++;
                }
                uint8_t ht = pci_cfg_read8(ecam, bus, dev, fn, 0x0E);
                if (!(ht & 0x80)) break;
            }
        }
    }
    return 0;
}

int pci_find_device(uint64_t ecam, uint16_t vendor, uint16_t device,
                    int *out_bus, int *out_dev, int *out_fn)
{
    return pci_find_device_nth(ecam, vendor, device, 0, out_bus, out_dev, out_fn);
}

void pci_assign_bar64(uint64_t ecam, int b, int d, int f,
                      int bar_idx, uint64_t addr) {
    uint32_t old_lo = pci_cfg_read32(ecam, b, d, f, 0x10 + bar_idx * 4);
    uint32_t type_bits = old_lo & 0xF;
    uint32_t new_lo = ((uint32_t)addr & ~0xFull) | type_bits;
    uint32_t new_hi = (uint32_t)(addr >> 32);
    pci_cfg_write32(ecam, b, d, f, 0x10 + bar_idx * 4, new_lo);
    pci_cfg_write32(ecam, b, d, f, 0x10 + (bar_idx + 1) * 4, new_hi);
}

// Возвращает ФИЗИЧЕСКИЙ адрес начала BAR (без KERNEL_VA_OFFSET).
uint64_t pci_bar_base(uint64_t ecam, int b, int d, int f, int bar_idx) {
    uint32_t bar = pci_cfg_read32(ecam, b, d, f, 0x10 + bar_idx * 4);
    if (bar & 0x1) return (uint64_t)(bar & ~0x3u);
    uint32_t type = (bar >> 1) & 0x3;
    if (type == 0x2) {
        uint32_t bar_hi = pci_cfg_read32(ecam, b, d, f, 0x10 + (bar_idx + 1) * 4);
        return ((uint64_t)bar_hi << 32) | (bar & ~0xFull);
    }
    return (uint64_t)(bar & ~0xFull);
}

int pci_parse_virtio_caps(uint64_t ecam, int b, int d, int f, virtio_mmio_t *out) {
    out->common = 0; out->notify = 0; out->isr = 0; out->device = 0;
    out->notify_mult = 0;

    uint16_t cmd = pci_cfg_read16(ecam, b, d, f, 0x04);
    cmd |= 0x2 | 0x4;
    pci_cfg_write16(ecam, b, d, f, 0x04, cmd);

    uint8_t cap_ptr = pci_cfg_read8(ecam, b, d, f, 0x34) & 0xFC;

    while (cap_ptr != 0) {
        uint8_t cap_id = pci_cfg_read8(ecam, b, d, f, cap_ptr);
        if (cap_id == 0x09) {
            uint8_t cfg_type = pci_cfg_read8(ecam, b, d, f, cap_ptr + 3);
            uint8_t bar_idx  = pci_cfg_read8(ecam, b, d, f, cap_ptr + 4);
            uint32_t offset  = pci_cfg_read32(ecam, b, d, f, cap_ptr + 8);

            if (bar_idx > 5) {
                cap_ptr = pci_cfg_read8(ecam, b, d, f, cap_ptr + 1) & 0xFC;
                continue;
            }

            // BAR → PA, потом в KVA. Kernel обращается к MMIO через
            // TTBR1, поэтому все адреса должны быть в high VA.
            uint64_t abs_kva = pci_bar_base(ecam, b, d, f, bar_idx)
                             + offset
                             + KERNEL_VA_OFFSET;

            if (cfg_type == 1) out->common = abs_kva;
            if (cfg_type == 2) {
                out->notify = abs_kva;
                out->notify_mult = pci_cfg_read32(ecam, b, d, f, cap_ptr + 16);
            }
            if (cfg_type == 3) out->isr = abs_kva;
            if (cfg_type == 4) out->device = abs_kva;
        }
        cap_ptr = pci_cfg_read8(ecam, b, d, f, cap_ptr + 1) & 0xFC;
    }

    return (out->common && out->notify && out->isr && out->device);
}
