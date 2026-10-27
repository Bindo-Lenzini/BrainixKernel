// BrainixKernel — initrd.c
// Разбор вкомпилированного образа. Формат — см. scripts/mkinitrd.py.

#include <stdint.h>
#include "initrd.h"

extern const uint8_t        initrd_data[];
extern const unsigned long  initrd_size;

static initrd_file_t g_files[INITRD_MAX_FILES];
static int           g_count = 0;

static uint32_t le32(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == '\0' && *b == '\0';
}

int initrd_init(void) {
    g_count = 0;

    if (initrd_size < 8) return 0;
    if (initrd_data[0] != 'B' || initrd_data[1] != 'K' ||
        initrd_data[2] != 'R' || initrd_data[3] != 'D') return 0;

    uint32_t num = le32(initrd_data + 4);
    if (num > INITRD_MAX_FILES) num = INITRD_MAX_FILES;

    const uint8_t *p   = initrd_data + 8;
    const uint8_t *end = initrd_data + initrd_size;

    for (uint32_t i = 0; i < num; i++) {
        if (p + 36 > end) break;

        for (int j = 0; j < 32; j++) g_files[i].name[j] = (char)p[j];
        g_files[i].name[31] = 0;

        uint32_t sz = le32(p + 32);
        p += 36;

        if (p + sz > end) break;

        g_files[i].size = sz;
        g_files[i].data = p;
        p += sz;

        g_count++;
    }

    return g_count;
}

int initrd_count(void) { return g_count; }

const initrd_file_t *initrd_get(int idx) {
    if (idx < 0 || idx >= g_count) return 0;
    return &g_files[idx];
}

const initrd_file_t *initrd_find(const char *name) {
    for (int i = 0; i < g_count; i++) {
        if (str_eq(g_files[i].name, name)) return &g_files[i];
    }
    return 0;
}