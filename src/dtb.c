// BrainixKernel (BKOS) — dtb.c
// FDT-парсер. g_dtb — KVA. Все reads через KVA.
// RAM banks отдаются как PA (используются physmem как PA).
// PCIe ECAM отдаётся как KVA (callers используют как memory).

#include <stdint.h>
#include <stddef.h>
#include "dtb.h"
#include "mmu.h"

#define FDT_MAGIC      0xD00DFEEDu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u

static const void *g_dtb = 0;

void dtb_set(void *ptr) { g_dtb = ptr; }
const void *dtb_get_ptr(void) { return g_dtb; }

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static inline uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}
static int name_is(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return (*s == '\0' || *s == '@');
}
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == '\0' && *b == '\0';
}
static int compat_contains(const uint8_t *val, uint32_t len, const char *s) {
    uint32_t off = 0;
    while (off < len) {
        const char *cur = (const char *)(val + off);
        uint32_t l = 0;
        while (off + l < len && cur[l]) l++;
        if (str_eq(cur, s)) return 1;
        off += l + 1;
    }
    return 0;
}

int dtb_get_ram_banks(ram_bank_t *out, int max) {
    if (!g_dtb || max <= 0) return 0;

    const uint8_t *base = (const uint8_t *)g_dtb;
    if (be32(base) != FDT_MAGIC) return 0;

    uint32_t off_struct  = be32(base + 8);
    uint32_t off_strings = be32(base + 12);

    const uint8_t *p    = base + off_struct;
    const char    *strs = (const char *)(base + off_strings);

    int depth = 0;
    int in_memory = 0;
    int count = 0;

    for (;;) {
        uint32_t tok = be32(p);
        p += 4;

        if (tok == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t len = 0;
            while (name[len]) len++;
            p += (len + 4) & ~(size_t)3;
            depth++;
            if (depth == 2 && name_is(name, "memory")) in_memory = 1;
        } else if (tok == FDT_END_NODE) {
            if (depth == 2 && in_memory) in_memory = 0;
            depth--;
        } else if (tok == FDT_PROP) {
            uint32_t len     = be32(p);
            uint32_t nameoff = be32(p + 4);
            p += 8;
            const uint8_t *val = p;
            p += (len + 3) & ~3u;
            if (in_memory) {
                const char *pname = strs + nameoff;
                if (str_eq(pname, "reg")) {
                    uint32_t n = len / 16;
                    for (uint32_t i = 0; i < n && count < max; i++) {
                        out[count].base = be64(val + i * 16);
                        out[count].size = be64(val + i * 16 + 8);
                        count++;
                    }
                }
            }
        } else if (tok == FDT_NOP) {
        } else if (tok == FDT_END) {
            break;
        } else break;
    }
    return count;
}

int dtb_get_framebuffer(fb_info_t *out) {
    (void)out;
    return 0;   // не используется
}

int dtb_get_pci_ecam(pci_ecam_t *out) {
    if (!g_dtb || !out) return 0;

    const uint8_t *base = (const uint8_t *)g_dtb;
    if (be32(base) != FDT_MAGIC) return 0;

    uint32_t off_struct  = be32(base + 8);
    uint32_t off_strings = be32(base + 12);

    const uint8_t *p    = base + off_struct;
    const char    *strs = (const char *)(base + off_strings);

    out->ecam_base = 0;
    out->ecam_size = 0;

    int      in_pci = 0;
    int      has_ecam = 0;
    uint64_t reg_base = 0, reg_size = 0;

    for (;;) {
        uint32_t tok = be32(p);
        p += 4;

        if (tok == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t len = 0;
            while (name[len]) len++;
            p += (len + 4) & ~(size_t)3;
            (void)name;
            in_pci = 0;
            has_ecam = 0;
            reg_base = 0;
            reg_size = 0;
        } else if (tok == FDT_END_NODE) {
            if (in_pci && has_ecam && reg_base && reg_size) {
                // Отдаём KVA. Физический = reg_base; VA = PA + offset.
                out->ecam_base = reg_base + KERNEL_VA_OFFSET;
                out->ecam_size = reg_size;
                return 1;
            }
            in_pci = 0;
        } else if (tok == FDT_PROP) {
            uint32_t len     = be32(p);
            uint32_t nameoff = be32(p + 4);
            p += 8;
            const uint8_t *val = p;
            p += (len + 3) & ~3u;

            const char *pname = strs + nameoff;
            if (str_eq(pname, "compatible")) {
                if (compat_contains(val, len, "pci-host-ecam-generic")) {
                    in_pci = 1;
                    has_ecam = 1;
                }
            } else if (str_eq(pname, "reg") && len >= 16) {
                reg_base = be64(val);
                reg_size = be64(val + 8);
            }
        } else if (tok == FDT_NOP) {
        } else if (tok == FDT_END) {
            break;
        } else break;
    }

    return 0;
}