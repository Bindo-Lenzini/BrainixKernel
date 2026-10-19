// BrainixKernel (BKOS) — pci.h
#ifndef BKOS_PCI_H
#define BKOS_PCI_H

#include <stdint.h>
#include "virtio.h"

// Найти N-е устройство по vendor/device (skip=0 — первое).
int pci_find_device_nth(uint64_t ecam_base, uint16_t vendor, uint16_t device,
                        int skip, int *out_bus, int *out_dev, int *out_fn);

// Обёртка: ищет первое совпадение.
int pci_find_device(uint64_t ecam_base, uint16_t vendor, uint16_t device,
                    int *out_bus, int *out_dev, int *out_fn);

uint32_t pci_cfg_read32(uint64_t ecam, int b, int d, int f, uint32_t off);
uint16_t pci_cfg_read16(uint64_t ecam, int b, int d, int f, uint32_t off);
uint8_t  pci_cfg_read8 (uint64_t ecam, int b, int d, int f, uint32_t off);
void     pci_cfg_write32(uint64_t ecam, int b, int d, int f, uint32_t off, uint32_t v);
void     pci_cfg_write16(uint64_t ecam, int b, int d, int f, uint32_t off, uint16_t v);

void     pci_assign_bar64(uint64_t ecam, int b, int d, int f, int bar_idx, uint64_t addr);
uint64_t pci_bar_base(uint64_t ecam, int b, int d, int f, int bar_idx);

int      pci_parse_virtio_caps(uint64_t ecam, int b, int d, int f, virtio_mmio_t *out);

#endif
