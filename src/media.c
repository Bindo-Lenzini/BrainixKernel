// BrainixKernel — media.c
// Драйвер медиа-диска (2-й virtio-blk). Lazy-init: проверяем наличие
// диска и читаем суперблок при первом обращении.

#include <stdint.h>
#include "media.h"
#include "blk.h"

#define MEDIA_DEV 1   // второй blk-диск в системе

typedef struct __attribute__((packed)) {
    uint8_t  magic[4];       // "MEDI"
    uint32_t version;        // = 1
    uint32_t num_files;
    uint32_t data_start;     // = 16 (сектор)
    uint8_t  reserved[500];
} media_super_t;

typedef struct __attribute__((packed)) {
    char     name[48];
    uint32_t size;
    uint32_t start_sector;
    uint8_t  reserved[8];
} media_entry_t;

static int      g_ready     = 0;
static int      g_tried     = 0;
static uint32_t g_num_files = 0;

static uint8_t  g_sec[512];

static void bytes_copy_n(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void media_try_init(void) {
    if (g_tried) return;
    g_tried = 1;

    if (blk_capacity_n(MEDIA_DEV) == 0) return;
    if (!blk_read_sector_n(MEDIA_DEV, 0, g_sec)) return;

    const media_super_t *s = (const media_super_t *)g_sec;
    if (s->magic[0] != 'M' || s->magic[1] != 'E' ||
        s->magic[2] != 'D' || s->magic[3] != 'I') return;
    if (s->version != 1) return;
    if (s->num_files > MEDIA_MAX_FILES) return;

    g_num_files = s->num_files;
    g_ready = 1;
}

int media_is_ready(void) {
    media_try_init();
    return g_ready;
}

int media_count(void) {
    if (!media_is_ready()) return 0;
    return (int)g_num_files;
}

static int load_entry(uint32_t idx, media_entry_t *out) {
    if (idx >= g_num_files) return 0;
    uint32_t off     = idx * 64u;
    uint32_t sect    = 1u + (off / 512u);
    uint32_t in_sect = off % 512u;
    if (!blk_read_sector_n(MEDIA_DEV, sect, g_sec)) return 0;
    bytes_copy_n(out, g_sec + in_sect, 64);
    return 1;
}

static int entry_name_eq(const char *fname, const char *name) {
    for (int i = 0; i < 48; i++) {
        if (fname[i] == 0 && name[i] == 0) return 1;
        if (fname[i] != name[i]) return 0;
    }
    return 0;
}

int media_list(int idx, char *name_out, uint32_t name_max, uint32_t *size_out) {
    if (!media_is_ready()) return 0;
    media_entry_t e;
    if (!load_entry((uint32_t)idx, &e)) return 0;
    uint32_t n = 0;
    while (n < 47 && e.name[n] && n + 1 < name_max) {
        name_out[n] = e.name[n];
        n++;
    }
    name_out[n] = 0;
    if (size_out) *size_out = e.size;
    return 1;
}

static int find_entry(const char *name, media_entry_t *out) {
    if (!media_is_ready()) return 0;
    for (uint32_t i = 0; i < g_num_files; i++) {
        media_entry_t e;
        if (!load_entry(i, &e)) return 0;
        if (entry_name_eq(e.name, name)) {
            *out = e;
            return 1;
        }
    }
    return 0;
}

int media_size(const char *name) {
    media_entry_t e;
    if (!find_entry(name, &e)) return -1;
    return (int)e.size;
}

int media_read(const char *name, void *buf, uint32_t max) {
    media_entry_t e;
    if (!find_entry(name, &e)) return -1;

    uint32_t to_read = e.size;
    if (to_read > max) to_read = max;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t got = 0;
    uint64_t sector = e.start_sector;

    while (got < to_read) {
        if (!blk_read_sector_n(MEDIA_DEV, sector, g_sec)) return -1;
        uint32_t chunk = to_read - got;
        if (chunk > 512) chunk = 512;
        for (uint32_t k = 0; k < chunk; k++) dst[got + k] = g_sec[k];
        got += chunk;
        sector++;
    }
    return (int)got;
}