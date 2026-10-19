// BrainixKernel (BKOS) — initrd.h
// Чтение файлов, вкомпилированных в образ ядра.
#ifndef BKOS_INITRD_H
#define BKOS_INITRD_H

#include <stdint.h>

#define INITRD_MAX_FILES 16

typedef struct {
    char           name[32];
    uint32_t       size;
    const uint8_t *data;
} initrd_file_t;

int initrd_init(void);
int initrd_count(void);
const initrd_file_t *initrd_get(int idx);
const initrd_file_t *initrd_find(const char *name);

#endif
