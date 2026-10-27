// BrainixKernel — blk.h
// virtio-blk: чтение/запись секторов по 512 байт.
// Поддерживает до BLK_MAX_DEV устройств (0 — основной, 1 — медиа).
#ifndef BRAINIX_BLK_H
#define BRAINIX_BLK_H

#include <stdint.h>

#define BLK_MAX_DEV 2

// Инициализация основного диска (устройство 0). Если есть второй
// virtio-blk, он автоматически инициализируется как устройство 1.
int  blk_init(uint64_t ecam_base);

// Инициализация N-го virtio-blk (skip = 0, 1, ...). Возвращает 1/0.
int  blk_init_nth(uint64_t ecam_base, int skip);

// Операции с указанным устройством.
int      blk_read_sector_n (int dev, uint64_t sector, void *buf);
int      blk_write_sector_n(int dev, uint64_t sector, const void *buf);
uint64_t blk_capacity_n(int dev);

// ---- Обратная совместимость (работает с устройством 0) ----
int      blk_read_sector (uint64_t sector, void *buf);
int      blk_write_sector(uint64_t sector, const void *buf);
uint64_t blk_capacity(void);

#endif