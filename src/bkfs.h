// BrainixKernel — bkfs.h
// Простая файловая система на секторах virtio-blk.
// Версия 2: 13 прямых + 1 косвенный блок = до 72 KB на файл.
#ifndef BRAINIX_BKFS_H
#define BRAINIX_BKFS_H

#include <stdint.h>

#define BKFS_OK       0
#define BKFS_ENOENT  -1
#define BKFS_ENOSPC  -2
#define BKFS_EEXIST  -3
#define BKFS_EIO     -4

// Open flags (совпадают с флагами user-программ).
#define BKFS_O_RDONLY  0x00
#define BKFS_O_WRONLY  0x01
#define BKFS_O_RDWR    0x02
#define BKFS_O_CREAT   0x40
#define BKFS_O_TRUNC   0x80

// Максимум файла: 13 direct + 128 indirect = 141 блок × 512 = 72192 байт.
#define BKFS_MAX_FILE_SIZE 72192u

int  bkfs_init(void);
int  bkfs_format(void);
int  bkfs_is_ready(void);

// High-level API (файл целиком).
int  bkfs_create(const char *name);
int  bkfs_write (const char *name, const void *data, uint32_t len);
int  bkfs_read  (const char *name, void *buf, uint32_t max);
int  bkfs_delete(const char *name);
int  bkfs_list(int idx, char *name_out, uint32_t name_max, uint32_t *size_out);

// Low-level API (для syscalls).
int  bkfs_open(const char *name);
int  bkfs_size(int inode);
int  bkfs_truncate(int inode);
int  bkfs_read_at(int inode, uint32_t offset, void *buf, uint32_t len);
int  bkfs_write_at(int inode, uint32_t offset, const void *buf, uint32_t len);

#endif
