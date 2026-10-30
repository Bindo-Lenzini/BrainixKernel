// BrainixKernel — bkfs.c
// Реализация BKFS v3: 13 прямых + 1 косвенный блок = до 72 KB на файл.
// Bitmap свободных блоков в памяти (пересобирается при init).
// Без libc.

#include <stdint.h>
#include "bkfs.h"
#include "blk.h"
#include "cred.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);

#define BKFS_MAGIC        0x53464B42u  // 'BKFS'
#define BKFS_VERSION_LEGACY 2u
#define BKFS_VERSION      3u
#define BKFS_MIGRATING_VERSION 0x80000003u
#define BKFS_METADATA_MAGIC 0xB35Au
#define BKFS_BLOCK_SIZE   512u
#define BKFS_INODE_COUNT  256u
#define BKFS_INODE_START  1u
#define BKFS_INODE_SECTORS ((BKFS_INODE_COUNT * 128u) / BKFS_BLOCK_SIZE)  // 64
#define BKFS_DATA_START   (BKFS_INODE_START + BKFS_INODE_SECTORS)         // 65

// Раскладка inode и блочных указателей:
//   blocks[0..12]  — 13 прямых блоков (6.5 KB)
//   blocks[13]     — косвенный блок (128 указателей = 64 KB)
//   UID/GID/mode   — метаданные доступа в бывших reserved bytes
#define BKFS_DIRECT_COUNT       13
#define BKFS_INDIRECT_INDEX     13
#define BKFS_INDIRECT_PTRS      (BKFS_BLOCK_SIZE / 4)  // 128
#define BKFS_MAX_BLOCKS         (BKFS_DIRECT_COUNT + BKFS_INDIRECT_PTRS)  // 141

#define MAX_DISK_SECTORS  32768u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t  reserved[512 - 7 * 4];
} bkfs_super_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    char     name[59];
    uint32_t size;
    uint32_t blocks[14];
    uint16_t uid;
    uint16_t gid;
    uint16_t mode;
    uint16_t metadata_magic;
} bkfs_inode_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    char     name[59];
    uint32_t size;
    uint32_t blocks[15];
    uint8_t  pad[4];
} bkfs_inode_v2_t;

static uint8_t g_sec[512];
static bkfs_super_t g_super;
static int      g_ready = 0;
static uint32_t g_inode_cache_sector = 0xFFFFFFFFu;

// Bitmap свободных/занятых блоков (1 = занят).
static uint8_t  g_block_bitmap[MAX_DISK_SECTORS / 8];
static uint32_t g_total_blocks = 0;

_Static_assert(sizeof(bkfs_inode_t) == 128, "BKFS v3 inode size");
_Static_assert(sizeof(bkfs_inode_v2_t) == 128, "BKFS v2 inode size");

// ---------- Утилиты ----------
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == 0 && *b == 0;
}
static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static void bytes_copy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void bytes_zero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}
static int sector_is_zero(const uint8_t *sector) {
    for (uint32_t i = 0; i < BKFS_BLOCK_SIZE; i++) {
        if (sector[i] != 0) return 0;
    }
    return 1;
}

static int disk_is_zero(uint32_t total_blocks) {
    uint8_t sector[BKFS_BLOCK_SIZE];
    for (uint32_t s = 1; s < total_blocks; s++) {
        if (!blk_read_sector(s, sector) || !sector_is_zero(sector)) return 0;
    }
    return 1;
}

// Побайтовая работа с 32-битными указателями в блоке (без выравнивания).
static uint32_t ptr_get(const uint8_t *buf, int idx) {
    const uint8_t *p = buf + idx * 4;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void ptr_set(uint8_t *buf, int idx, uint32_t val) {
    uint8_t *p = buf + idx * 4;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

// ---------- Inode cache ----------
static int inode_load_sector(uint32_t inode_idx) {
    uint32_t off_bytes = inode_idx * 128u;
    uint32_t sector = BKFS_INODE_START + off_bytes / 512u;
    if (sector == g_inode_cache_sector) return 1;
    if (!blk_read_sector(sector, g_sec)) return 0;
    g_inode_cache_sector = sector;
    return 1;
}
static bkfs_inode_t *inode_ptr(uint32_t inode_idx) {
    uint32_t off = (inode_idx * 128u) % 512u;
    return (bkfs_inode_t *)(g_sec + off);
}
static int inode_load(uint32_t idx, bkfs_inode_t *out) {
    if (idx >= BKFS_INODE_COUNT) return 0;
    if (!inode_load_sector(idx)) return 0;
    bytes_copy(out, inode_ptr(idx), sizeof(bkfs_inode_t));
    return 1;
}
static int inode_store(uint32_t idx, const bkfs_inode_t *in) {
    if (idx >= BKFS_INODE_COUNT) return 0;
    if (!inode_load_sector(idx)) return 0;
    bytes_copy(inode_ptr(idx), in, sizeof(bkfs_inode_t));
    uint32_t sector = BKFS_INODE_START + (idx * 128u) / 512u;
    if (!blk_write_sector(sector, g_sec)) return 0;
    g_inode_cache_sector = 0xFFFFFFFFu;
    return 1;
}
static int inode_find(const char *name, uint32_t *out_idx) {
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        if (!inode_load_sector(i)) return 0;
        bkfs_inode_t *p = inode_ptr(i);
        if (p->type == 0) continue;
        if (str_eq(p->name, name)) { *out_idx = i; return 1; }
    }
    return 0;
}
static int inode_alloc(uint32_t *out_idx) {
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        if (!inode_load_sector(i)) return 0;
        bkfs_inode_t *p = inode_ptr(i);
        if (p->type == 0) { *out_idx = i; return 1; }
    }
    return 0;
}

// ---------- Bitmap ----------
static void bm_set(uint32_t s)   { g_block_bitmap[s / 8] |=  (uint8_t)(1u << (s % 8)); }
static void bm_clear(uint32_t s) { g_block_bitmap[s / 8] &= (uint8_t)~(1u << (s % 8)); }
static int  bm_test(uint32_t s)  { return (g_block_bitmap[s / 8] >> (s % 8)) & 1; }

static int alloc_block(uint32_t *out) {
    for (uint32_t s = BKFS_DATA_START; s < g_total_blocks; s++) {
        if (!bm_test(s)) {
            bm_set(s);
            *out = s;
            return 1;
        }
    }
    return 0;
}
static void free_block(uint32_t s) {
    if (s >= BKFS_DATA_START && s < g_total_blocks) bm_clear(s);
}

// Полная пересборка bitmap: сканирует все inode (direct + indirect).
static void bitmap_init(void) {
    for (uint32_t i = 0; i < sizeof(g_block_bitmap); i++) g_block_bitmap[i] = 0;
    bm_set(0);
    for (uint32_t s = BKFS_INODE_START; s < BKFS_DATA_START; s++) bm_set(s);
}

static int bitmap_claim_data_block(uint32_t sector) {
    if (sector < BKFS_DATA_START || sector >= g_total_blocks ||
        sector >= MAX_DISK_SECTORS || bm_test(sector)) {
        return 0;
    }
    bm_set(sector);
    return 1;
}

static int bitmap_claim_inode_blocks(const uint32_t *blocks, uint32_t size) {
    if (size > BKFS_MAX_FILE_SIZE) return 0;

    for (int b = 0; b < BKFS_DIRECT_COUNT; b++) {
        if (blocks[b] && !bitmap_claim_data_block(blocks[b])) return 0;
    }

    uint32_t ind = blocks[BKFS_INDIRECT_INDEX];
    if (ind) {
        if (!bitmap_claim_data_block(ind)) return 0;
        uint8_t buf[BKFS_BLOCK_SIZE];
        if (!blk_read_sector(ind, buf)) return 0;
        for (int j = 0; j < (int)BKFS_INDIRECT_PTRS; j++) {
            uint32_t sector = ptr_get(buf, j);
            if (sector && !bitmap_claim_data_block(sector)) return 0;
        }
    }
    return 1;
}

static int bitmap_rebuild(void) {
    bitmap_init();
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        bkfs_inode_t in;
        if (!inode_load(i, &in)) return 0;
        if (in.type == 0) continue;
        if (!bitmap_claim_inode_blocks(in.blocks, in.size)) return 0;
    }
    return 1;
}

static int bitmap_validate_v2(void) {
    bitmap_init();
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        uint32_t off_bytes = i * 128u;
        uint32_t sector = BKFS_INODE_START + off_bytes / BKFS_BLOCK_SIZE;
        if (sector != g_inode_cache_sector &&
            !blk_read_sector(sector, g_sec)) {
            g_inode_cache_sector = 0xFFFFFFFFu;
            return 0;
        }
        g_inode_cache_sector = sector;
        bkfs_inode_v2_t *legacy =
            (bkfs_inode_v2_t *)(g_sec + (off_bytes % BKFS_BLOCK_SIZE));
        if (legacy->type == 0) continue;
        bkfs_inode_t *current = (bkfs_inode_t *)legacy;
        if (current->metadata_magic == BKFS_METADATA_MAGIC) {
            if (!bitmap_claim_inode_blocks(current->blocks, current->size)) {
                g_inode_cache_sector = 0xFFFFFFFFu;
                return 0;
            }
            continue;
        }
        if (legacy->blocks[14] != 0 ||
            legacy->pad[0] != 0 || legacy->pad[1] != 0 ||
            legacy->pad[2] != 0 || legacy->pad[3] != 0 ||
            !bitmap_claim_inode_blocks(legacy->blocks, legacy->size)) {
            g_inode_cache_sector = 0xFFFFFFFFu;
            return 0;
        }
    }
    g_inode_cache_sector = 0xFFFFFFFFu;
    return 1;
}

// ---------- Доступ к блокам файла ----------
// Возвращает sector для block_idx, или 0 если не выделен.
static uint32_t get_block(const bkfs_inode_t *in, int block_idx) {
    if (block_idx < 0 || block_idx >= (int)BKFS_MAX_BLOCKS) return 0;
    if (block_idx < BKFS_DIRECT_COUNT) return in->blocks[block_idx];

    uint32_t ind = in->blocks[BKFS_INDIRECT_INDEX];
    if (ind == 0) return 0;
    int ptr_idx = block_idx - BKFS_DIRECT_COUNT;
    uint8_t buf[512];
    if (!blk_read_sector(ind, buf)) return 0;
    return ptr_get(buf, ptr_idx);
}

// Устанавливает block_idx → sector. Аллоцирует indirect при необходимости.
static int set_block(bkfs_inode_t *in, int block_idx, uint32_t sector) {
    if (block_idx < 0 || block_idx >= (int)BKFS_MAX_BLOCKS) return 0;

    if (block_idx < BKFS_DIRECT_COUNT) {
        in->blocks[block_idx] = sector;
        return 1;
    }
    if (in->blocks[BKFS_INDIRECT_INDEX] == 0) {
        uint32_t s;
        if (!alloc_block(&s)) return 0;
        uint8_t z[512];
        bytes_zero(z, 512);
        if (!blk_write_sector(s, z)) return 0;
        in->blocks[BKFS_INDIRECT_INDEX] = s;
    }
    uint8_t buf[512];
    if (!blk_read_sector(in->blocks[BKFS_INDIRECT_INDEX], buf)) return 0;
    ptr_set(buf, block_idx - BKFS_DIRECT_COUNT, sector);
    if (!blk_write_sector(in->blocks[BKFS_INDIRECT_INDEX], buf)) return 0;
    return 1;
}

// Освобождает все блоки файла (direct + indirect + indirect blocks).
static void free_all_blocks(bkfs_inode_t *in) {
    for (int b = 0; b < BKFS_DIRECT_COUNT; b++) {
        if (in->blocks[b]) free_block(in->blocks[b]);
        in->blocks[b] = 0;
    }
    uint32_t ind = in->blocks[BKFS_INDIRECT_INDEX];
    if (ind) {
        uint8_t buf[512];
        if (blk_read_sector(ind, buf)) {
            for (int j = 0; j < (int)BKFS_INDIRECT_PTRS; j++) {
                uint32_t s = ptr_get(buf, j);
                if (s) free_block(s);
            }
        }
        free_block(ind);
        in->blocks[BKFS_INDIRECT_INDEX] = 0;
    }
    in->size = 0;
}

static int migrate_v2(void) {
    uint8_t sector[512];
    for (uint32_t s = 0; s < BKFS_INODE_SECTORS; s++) {
        if (!blk_read_sector(BKFS_INODE_START + s, sector)) return 0;
        bkfs_inode_v2_t *legacy = (bkfs_inode_v2_t *)sector;
        bkfs_inode_t *current = (bkfs_inode_t *)sector;
        for (uint32_t i = 0; i < 4; i++) {
            if (current[i].type == 0 ||
                current[i].metadata_magic == BKFS_METADATA_MAGIC) {
                continue;
            }
            if (legacy[i].blocks[14] != 0 ||
                legacy[i].pad[0] != 0 || legacy[i].pad[1] != 0 ||
                legacy[i].pad[2] != 0 || legacy[i].pad[3] != 0) {
                return 0;
            }
            current[i].uid = ROOT_UID;
            current[i].gid = ROOT_GID;
            current[i].mode = BKFS_DEFAULT_MODE;
            current[i].metadata_magic = BKFS_METADATA_MAGIC;
        }
        if (!blk_write_sector(BKFS_INODE_START + s, sector)) return 0;
    }

    if (!blk_read_sector(0, g_sec)) return -1;
    ((bkfs_super_t *)g_sec)->version = BKFS_VERSION;
    if (!blk_write_sector(0, g_sec)) return 0;
    g_inode_cache_sector = 0xFFFFFFFFu;
    return 1;
}

static int metadata_validate(void) {
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        bkfs_inode_t in;
        if (!inode_load(i, &in)) return 0;
        if (in.type != 0 &&
            (in.metadata_magic != BKFS_METADATA_MAGIC || in.mode > 07777u)) {
            return 0;
        }
    }
    return 1;
}

// ============================================================
// Public API
// ============================================================

int bkfs_init(void) {
    g_ready = 0;
    g_inode_cache_sector = 0xFFFFFFFFu;

    if (!blk_read_sector(0, g_sec)) return -1;

    bkfs_super_t *sp = (bkfs_super_t *)g_sec;
    if (sp->magic != BKFS_MAGIC) {
        if (!sector_is_zero(g_sec)) return -1;
        uint64_t capacity = blk_capacity();
        if (capacity > MAX_DISK_SECTORS) capacity = MAX_DISK_SECTORS;
        return disk_is_zero((uint32_t)capacity) ? 0 : -1;
    }
    if (sp->block_size != BKFS_BLOCK_SIZE ||
        sp->inode_count != BKFS_INODE_COUNT ||
        sp->inode_start != BKFS_INODE_START ||
        sp->data_start != BKFS_DATA_START ||
        sp->total_blocks < BKFS_DATA_START ||
        sp->total_blocks > blk_capacity()) {
        return -1;
    }

    g_total_blocks = sp->total_blocks;
    if (sp->version == BKFS_VERSION_LEGACY) {
        if (!bitmap_validate_v2()) return -1;
        if (!blk_read_sector(0, g_sec)) return -1;
        sp = (bkfs_super_t *)g_sec;
        sp->version = BKFS_MIGRATING_VERSION;
        if (!blk_write_sector(0, g_sec) || !migrate_v2()) return -1;
        if (!blk_read_sector(0, g_sec)) return -1;
        sp = (bkfs_super_t *)g_sec;
    } else if (sp->version == BKFS_MIGRATING_VERSION) {
        if (!bitmap_validate_v2()) return -1;
        if (!migrate_v2()) return -1;
        if (!blk_read_sector(0, g_sec)) return -1;
        sp = (bkfs_super_t *)g_sec;
    } else if (sp->version != BKFS_VERSION) {
        return -1;
    }

    bytes_copy(&g_super, sp, sizeof(bkfs_super_t));
    g_total_blocks = sp->total_blocks;
    if (g_total_blocks > MAX_DISK_SECTORS) g_total_blocks = MAX_DISK_SECTORS;

    g_inode_cache_sector = 0xFFFFFFFFu;
    if (!metadata_validate()) return -1;
    if (!bitmap_rebuild()) return -1;
    g_ready = 1;
    return 1;
}

int bkfs_is_ready(void) { return g_ready; }

int bkfs_format(void) {
    uint64_t total = blk_capacity();
    if (total < BKFS_DATA_START) return BKFS_EIO;
    if (total > MAX_DISK_SECTORS) total = MAX_DISK_SECTORS;

    bytes_zero(g_sec, 512);
    bkfs_super_t *sp = (bkfs_super_t *)g_sec;
    sp->magic        = BKFS_MAGIC;
    sp->version      = BKFS_VERSION;
    sp->block_size   = BKFS_BLOCK_SIZE;
    sp->total_blocks = (uint32_t)total;
    sp->inode_count  = BKFS_INODE_COUNT;
    sp->inode_start  = BKFS_INODE_START;
    sp->data_start   = BKFS_DATA_START;

    uint8_t zero[512];
    bytes_zero(zero, 512);
    for (uint32_t s = 0; s < BKFS_INODE_SECTORS; s++) {
        if (!blk_write_sector(BKFS_INODE_START + s, zero)) return BKFS_EIO;
    }
    if (!blk_write_sector(0, g_sec)) return BKFS_EIO;

    bytes_copy(&g_super, sp, sizeof(bkfs_super_t));
    g_total_blocks = (uint32_t)total;
    g_ready = 1;
    g_inode_cache_sector = 0xFFFFFFFFu;

    // Все блоки данных свободны.
    for (uint32_t i = 0; i < sizeof(g_block_bitmap); i++) g_block_bitmap[i] = 0;
    bm_set(0);
    for (uint32_t s = BKFS_INODE_START; s < BKFS_DATA_START; s++) bm_set(s);

    return BKFS_OK;
}

int bkfs_format_for(const struct cred *cred) {
    if (!cred || !cred_has_cap(cred, CAP_SYS_ADMIN)) return BKFS_EINVAL;
    return bkfs_format();
}

int bkfs_create(const char *name) {
    return bkfs_create_for(name, cred_ui(), BKFS_DEFAULT_MODE);
}

static int inode_create_as(const char *name, uint32_t uid, uint32_t gid,
                           uint32_t mode) {
    if (!g_ready) return BKFS_EIO;
    if (!name || !name[0]) return BKFS_EIO;
    if (uid > BKFS_MAX_ID || gid > BKFS_MAX_ID || mode > 07777u)
        return BKFS_EINVAL;

    uint32_t existing;
    if (inode_find(name, &existing)) return BKFS_EEXIST;

    uint32_t idx;
    if (!inode_alloc(&idx)) return BKFS_ENOSPC;

    bkfs_inode_t in;
    bytes_zero(&in, sizeof(in));
    in.type = 1;
    str_copy_n(in.name, name, 59);
    in.size = 0;
    in.uid = (uint16_t)uid;
    in.gid = (uint16_t)gid;
    in.mode = (uint16_t)mode;
    in.metadata_magic = BKFS_METADATA_MAGIC;

    if (!inode_store(idx, &in)) return BKFS_EIO;
    return BKFS_OK;
}

int bkfs_create_for(const char *name, const struct cred *cred, uint32_t mode) {
    if (!cred) return BKFS_EINVAL;
    if (mode > 07777u || cred->euid > BKFS_MAX_ID ||
        cred->egid > BKFS_MAX_ID) {
        return BKFS_EINVAL;
    }
    return inode_create_as(name, cred->euid, cred->egid, mode);
}

int bkfs_open(const char *name) {
    if (!g_ready) return BKFS_EIO;
    if (!name || !name[0]) return BKFS_ENOENT;

    uint32_t idx;
    if (!inode_find(name, &idx)) return BKFS_ENOENT;
    return (int)idx;
}

int bkfs_get_metadata(int inode, uint32_t *uid, uint32_t *gid,
                      uint32_t *mode) {
    if (!g_ready) return BKFS_EIO;
    if (inode < 0 || inode >= (int)BKFS_INODE_COUNT) return BKFS_ENOENT;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;
    if (in.metadata_magic != BKFS_METADATA_MAGIC || in.mode > 07777u)
        return BKFS_EIO;

    if (uid) *uid = in.uid;
    if (gid) *gid = in.gid;
    if (mode) *mode = in.mode;
    return BKFS_OK;
}

static int inode_access_for(const struct cred *cred, int inode, int want,
                            uint32_t *uid_out, uint32_t *gid_out,
                            uint32_t *mode_out) {
    uint32_t uid, gid, mode;
    if (!cred || bkfs_get_metadata(inode, &uid, &gid, &mode) != BKFS_OK)
        return 0;
    if (uid_out) *uid_out = uid;
    if (gid_out) *gid_out = gid;
    if (mode_out) *mode_out = mode;
    return cred_may_access(cred, mode, uid, gid, want);
}

int bkfs_chmod_for(const char *name, uint32_t mode, const struct cred *cred) {
    if (!cred || mode > 07777u) return BKFS_EINVAL;
    int inode = bkfs_open(name);
    if (inode < 0) return inode;
    uint32_t uid, gid, old_mode;
    if (bkfs_get_metadata(inode, &uid, &gid, &old_mode) != BKFS_OK)
        return BKFS_EIO;
    if (cred->euid != uid && !cred_has_cap(cred, CAP_FOWNER))
        return BKFS_EINVAL;
    if (!cred_has_cap(cred, CAP_FSETID) && !cred_is_group_member(cred, gid))
        mode &= ~02000u;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    in.mode = (uint16_t)mode;
    return inode_store((uint32_t)inode, &in) ? BKFS_OK : BKFS_EIO;
}

int bkfs_chown_for(const char *name, uint32_t uid, uint32_t gid,
                   const struct cred *cred) {
    if (!cred || (uid != UINT32_MAX && uid > BKFS_MAX_ID) ||
        (gid != UINT32_MAX && gid > BKFS_MAX_ID)) {
        return BKFS_EINVAL;
    }
    int inode = bkfs_open(name);
    if (inode < 0) return inode;
    uint32_t old_uid, old_gid, mode;
    if (bkfs_get_metadata(inode, &old_uid, &old_gid, &mode) != BKFS_OK)
        return BKFS_EIO;

    int privileged = cred_has_cap(cred, CAP_CHOWN);
    if (!privileged) {
        if (cred->euid != old_uid ||
            (uid != UINT32_MAX && uid != old_uid) ||
            (gid != UINT32_MAX &&
             !cred_is_group_member(cred, (gid_t)gid))) {
            return BKFS_EINVAL;
        }
    }

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (uid != UINT32_MAX) in.uid = (uint16_t)uid;
    if (gid != UINT32_MAX) in.gid = (uint16_t)gid;
    if (uid != UINT32_MAX || gid != UINT32_MAX)
        in.mode &= (uint16_t)~06000u;
    return inode_store((uint32_t)inode, &in) ? BKFS_OK : BKFS_EIO;
}

int bkfs_size(int inode) {
    if (!g_ready) return BKFS_EIO;
    if (inode < 0 || inode >= (int)BKFS_INODE_COUNT) return BKFS_ENOENT;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;
    return (int)in.size;
}

int bkfs_truncate(int inode) {
    if (!g_ready) return BKFS_EIO;
    if (inode < 0 || inode >= (int)BKFS_INODE_COUNT) return BKFS_ENOENT;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;

    free_all_blocks(&in);
    if (!inode_store((uint32_t)inode, &in)) return BKFS_EIO;
    return BKFS_OK;
}

int bkfs_truncate_for(int inode, const struct cred *cred) {
    if (!inode_access_for(cred, inode, CRED_W, 0, 0, 0)) return BKFS_EINVAL;
    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;
    if (!cred_has_cap(cred, CAP_FSETID)) in.mode &= (uint16_t)~06000u;
    free_all_blocks(&in);
    return inode_store((uint32_t)inode, &in) ? BKFS_OK : BKFS_EIO;
}

int bkfs_read_at(int inode, uint32_t offset, void *buf, uint32_t len) {
    if (!g_ready) return BKFS_EIO;
    if (inode < 0 || inode >= (int)BKFS_INODE_COUNT) return BKFS_ENOENT;
    if (!buf || len == 0) return 0;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;

    if (offset >= in.size) return 0;
    uint32_t avail = in.size - offset;
    if (len > avail) len = avail;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t got = 0;
    uint8_t sec[512];

    while (got < len) {
        uint32_t cur = offset + got;
        int blk_idx = (int)(cur / BKFS_BLOCK_SIZE);
        uint32_t in_blk = cur % BKFS_BLOCK_SIZE;

        uint32_t s = get_block(&in, blk_idx);
        if (s == 0) break;
        if (!blk_read_sector(s, sec)) return BKFS_EIO;

        uint32_t chunk = BKFS_BLOCK_SIZE - in_blk;
        if (chunk > len - got) chunk = len - got;
        for (uint32_t i = 0; i < chunk; i++) dst[got + i] = sec[in_blk + i];
        got += chunk;
    }
    return (int)got;
}

int bkfs_write_at(int inode, uint32_t offset, const void *buf, uint32_t len) {
    if (!g_ready) return BKFS_EIO;
    if (inode < 0 || inode >= (int)BKFS_INODE_COUNT) return BKFS_ENOENT;
    if (!buf) return 0;
    if (len == 0) return 0;

    bkfs_inode_t in;
    if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
    if (in.type == 0) return BKFS_ENOENT;

    if (offset + len > BKFS_MAX_FILE_SIZE) {
        if (offset >= BKFS_MAX_FILE_SIZE) return BKFS_ENOSPC;
        len = BKFS_MAX_FILE_SIZE - offset;
    }

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t written = 0;
    uint8_t sec[512];

    while (written < len) {
        uint32_t cur = offset + written;
        int blk_idx = (int)(cur / BKFS_BLOCK_SIZE);
        uint32_t in_blk = cur % BKFS_BLOCK_SIZE;

        uint32_t chunk = BKFS_BLOCK_SIZE - in_blk;
        if (chunk > len - written) chunk = len - written;

        uint32_t s = get_block(&in, blk_idx);
        if (s == 0) {
            if (!alloc_block(&s)) {
                inode_store((uint32_t)inode, &in);
                return written > 0 ? (int)written : BKFS_ENOSPC;
            }
            if (!set_block(&in, blk_idx, s)) {
                free_block(s);
                inode_store((uint32_t)inode, &in);
                return written > 0 ? (int)written : BKFS_EIO;
            }
            bytes_zero(sec, 512);
        } else {
            if (!blk_read_sector(s, sec)) {
                inode_store((uint32_t)inode, &in);
                return written > 0 ? (int)written : BKFS_EIO;
            }
        }

        for (uint32_t i = 0; i < chunk; i++) sec[in_blk + i] = src[written + i];
        if (!blk_write_sector(s, sec)) {
            inode_store((uint32_t)inode, &in);
            return written > 0 ? (int)written : BKFS_EIO;
        }
        written += chunk;
    }

    uint32_t new_end = offset + written;
    if (new_end > in.size) in.size = new_end;

    if (!inode_store((uint32_t)inode, &in)) return BKFS_EIO;
    return (int)written;
}

int bkfs_write_at_for(int inode, uint32_t offset, const void *buf,
                      uint32_t len, const struct cred *cred) {
    if (!inode_access_for(cred, inode, CRED_W, 0, 0, 0)) return BKFS_EINVAL;
    if (len == 0) return 0;
    if (!buf) return BKFS_EINVAL;
    if (!cred_has_cap(cred, CAP_FSETID)) {
        bkfs_inode_t in;
        if (!inode_load((uint32_t)inode, &in)) return BKFS_EIO;
        if (in.type == 0) return BKFS_ENOENT;
        if (in.mode & 06000u) {
            in.mode &= (uint16_t)~06000u;
            if (!inode_store((uint32_t)inode, &in)) return BKFS_EIO;
        }
    }
    return bkfs_write_at(inode, offset, buf, len);
}

// ---------- High-level API ----------

static int inode_delete(const char *name) {
    if (!g_ready) return BKFS_EIO;

    uint32_t idx;
    if (!inode_find(name, &idx)) return BKFS_ENOENT;

    bkfs_inode_t in;
    if (!inode_load(idx, &in)) return BKFS_EIO;

    free_all_blocks(&in);
    in.type = 0;

    if (!inode_store(idx, &in)) return BKFS_EIO;
    return BKFS_OK;
}

int bkfs_write(const char *name, const void *data, uint32_t len) {
    return bkfs_write_for(name, data, len, cred_ui());
}

int bkfs_read(const char *name, void *buf, uint32_t max) {
    return bkfs_read_for(name, buf, max, cred_ui());
}

int bkfs_delete(const char *name) {
    return bkfs_delete_for(name, cred_ui());
}

int bkfs_read_for(const char *name, void *buf, uint32_t max,
                  const struct cred *cred) {
    int inode = bkfs_open(name);
    if (inode < 0) return inode;
    if (!inode_access_for(cred, inode, CRED_R, 0, 0, 0)) return BKFS_EINVAL;
    return bkfs_read_at(inode, 0, buf, max);
}

int bkfs_write_for(const char *name, const void *data, uint32_t len,
                   const struct cred *cred) {
    int inode = bkfs_open(name);
    if (inode < 0) return inode;
    if (!inode_access_for(cred, inode, CRED_W, 0, 0, 0)) return BKFS_EINVAL;
    if (bkfs_truncate_for(inode, cred) != BKFS_OK) return BKFS_EIO;
    return bkfs_write_at_for(inode, 0, data, len, cred);
}

int bkfs_delete_for(const char *name, const struct cred *cred) {
    if (!cred) return BKFS_EINVAL;
    int inode = bkfs_open(name);
    if (inode < 0) return inode;
    uint32_t uid;
    if (bkfs_get_metadata(inode, &uid, 0, 0) != BKFS_OK) return BKFS_EIO;
    if (cred->euid != uid && !cred_has_cap(cred, CAP_FOWNER))
        return BKFS_EINVAL;
    return inode_delete(name);
}

int bkfs_list(int idx, char *name_out, uint32_t name_max, uint32_t *size_out) {
    return bkfs_list_for(idx, name_out, name_max, size_out, cred_ui());
}

int bkfs_list_for(int idx, char *name_out, uint32_t name_max,
                  uint32_t *size_out, const struct cred *cred) {
    if (!g_ready || idx < 0 || !name_out || name_max == 0 || !cred) return 0;
    int visible = 0;
    for (uint32_t i = 0; i < BKFS_INODE_COUNT; i++) {
        if (!inode_load_sector(i)) return 0;
        bkfs_inode_t *p = inode_ptr(i);
        if (p->type == 0) continue;
        if (!cred_may_access(cred, p->mode, p->uid, p->gid, CRED_R)) continue;
        if (visible++ != idx) continue;
        str_copy_n(name_out, p->name, (int)name_max);
        if (size_out) *size_out = p->size;
        return 1;
    }
    return 0;
}
