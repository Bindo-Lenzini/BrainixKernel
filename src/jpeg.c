// BrainixKernel (BKOS) — jpeg.c
// Baseline + Progressive JPEG. С graceful-degradation:
// если какой-то прогрессивный скан падает — пропускаем его,
// но продолжаем с остальными. В конце всегда IDCT.

#include <stdint.h>
#include "jpeg.h"
#include "png.h"
#include "physmem.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);

#define JPEG_MAX_DIM    4096u
#define JPEG_MAX_PIXELS (2048u * 1152u)
#define MAX_COMPS       4

static char g_jpeg_err[100] = {0};

static void jerr(const char *s) {
    int i = 0;
    while (s[i] && i < 99) { g_jpeg_err[i] = s[i]; i++; }
    g_jpeg_err[i] = 0;
    uart_puts("[jpeg] "); uart_puts(s); uart_puts("\n");
}

static void jlog(const char *s) { uart_puts("[jpeg] "); uart_puts(s); uart_puts("\n"); }

const char *jpeg_last_error(void) { return g_jpeg_err; }

// ============================================================
// Память
// ============================================================
static void *page_alloc_bytes(uint64_t bytes) {
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    return phys_alloc_pages(pages);
}
static void page_free_bytes(void *p, uint64_t bytes) {
    if (!p) return;
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < pages; i++) phys_free_page(b + i * PAGE_SIZE);
}

// ============================================================
// Bit reader
// ============================================================
typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       pos;
    uint32_t       bits;
    int            nbits;
} jb_t;

static int jb_next_byte(jb_t *br) {
    if (br->pos >= br->size) return -1;
    uint8_t b = br->data[br->pos++];
    if (b == 0xFF) {
        if (br->pos >= br->size) { br->pos--; return -1; }
        uint8_t n = br->data[br->pos];
        if (n == 0x00) { br->pos++; return 0xFF; }
        br->pos--;
        return -1;
    }
    return b;
}

static int jb_get_bits(jb_t *br, int n) {
    int v = 0;
    while (n > 0) {
        if (br->nbits == 0) {
            int b = jb_next_byte(br);
            if (b < 0) return -1;
            br->bits = (uint32_t)b;
            br->nbits = 8;
        }
        br->nbits--;
        v = (v << 1) | ((br->bits >> br->nbits) & 1);
        n--;
    }
    return v;
}

static int jb_extend(int v, int n) {
    if (n == 0) return 0;
    if (v < (1 << (n - 1))) return v - (1 << n) + 1;
    return v;
}

static int jb_skip_restart(jb_t *br) {
    br->nbits = 0;
    while (br->pos + 1 < br->size &&
           br->data[br->pos] == 0xFF && br->data[br->pos + 1] == 0x00)
        br->pos += 2;
    if (br->pos + 2 > br->size) return 0;
    if (br->data[br->pos] != 0xFF) return 0;
    uint8_t m = br->data[br->pos + 1];
    if (m < 0xD0 || m > 0xD7) return 0;
    br->pos += 2;
    return 1;
}

// ============================================================
// Хаффман
// ============================================================
typedef struct {
    int32_t maxcode[18];
    int32_t mincode[18];
    int32_t valptr[18];
    uint8_t huffval[256];
} jhuff_t;

static void jh_build(jhuff_t *h, const uint8_t *counts, const uint8_t *vals) {
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        int cnt = counts[len - 1];
        h->valptr[len]  = k;
        h->mincode[len] = code;
        for (int i = 0; i < cnt; i++) { h->huffval[k] = vals[k]; k++; }
        code += cnt;
        h->maxcode[len] = (cnt > 0) ? (code - 1) : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->valptr[17]  = 0;
    h->mincode[17] = 0;
}

static int jh_decode(jb_t *br, const jhuff_t *h) {
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        int bit = jb_get_bits(br, 1);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        if (code <= h->maxcode[len]) {
            int idx = h->valptr[len] + code - h->mincode[len];
            return h->huffval[idx];
        }
    }
    return -1;
}

// ============================================================
// Таблицы
// ============================================================
static const uint8_t g_zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

static const int32_t g_idct_tab[8][8] = {
    { 11585, 16069, 15137, 13623, 11585,  9102,  6270,  3196 },
    { 11585, 13623,  6270, -3196,-11585,-16069,-15137, -9102 },
    { 11585,  9102, -6270,-16069,-11585,  3196, 15137, 13623 },
    { 11585,  3196,-15137, -9102, 11585, 13623, -6270,-16069 },
    { 11585, -3196,-15137,  9102, 11585,-13623, -6270, 16069 },
    { 11585, -9102, -6270, 16069,-11585, -3196, 15137,-13623 },
    { 11585,-13623,  6270,  3196,-11585, 16069,-15137,  9102 },
    { 11585,-16069, 15137,-13623, 11585, -9102,  6270, -3196 }
};

static void idct_block(int32_t *block) {
    int64_t tmp[64];
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int64_t s = 0;
            for (int v = 0; v < 8; v++)
                s += (int64_t)g_idct_tab[y][v] * block[v * 8 + x];
            tmp[y * 8 + x] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int64_t s = 0;
            for (int u = 0; u < 8; u++)
                s += (int64_t)g_idct_tab[x][u] * tmp[y * 8 + u];
            int32_t val = (int32_t)((s + (1LL << 29)) >> 30);
            val += 128;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            block[y * 8 + x] = val;
        }
}

// ============================================================
// Компонент
// ============================================================
typedef struct {
    int      id;
    int      h, v;
    int      quant_id;
    int      dc_table;
    int      ac_table;
    int      bw, bh;
    int16_t *coeffs;
} jcomp_t;

static int16_t *block_of(jcomp_t *c, int bx, int by) {
    return c->coeffs + ((uint64_t)by * c->bw + bx) * 64;
}

// ============================================================
// Helper: дамп первых 16 байт (для отладки)
// ============================================================
static void dump_bytes(const char *prefix, const uint8_t *p, int n) {
    static const char hex[] = "0123456789abcdef";
    uart_puts("[jpeg] "); uart_puts(prefix); uart_puts(": ");
    for (int i = 0; i < n; i++) {
        char b[3] = { hex[p[i] >> 4], hex[p[i] & 0xF], 0 };
        uart_puts(b);
        uart_puts(" ");
    }
    uart_puts("\n");
}

// ============================================================
// Baseline scan
// ============================================================
static int scan_baseline(jb_t *br, jcomp_t **comps, int ncomp,
                         const jhuff_t *hdc_tab, const jhuff_t *hac_tab,
                         const uint8_t (*qt)[64],
                         int mcus_x, int mcus_y, int non_interleaved,
                         int restart_interval)
{
    int16_t dc_pred[MAX_COMPS] = {0};

    if (non_interleaved) {
        jcomp_t *c = comps[0];
        const jhuff_t *hd = &hdc_tab[c->dc_table];
        const jhuff_t *ha = &hac_tab[c->ac_table];
        int qid = c->quant_id;
        int idx = 0;
        for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
            if (restart_interval > 0 && idx > 0 && (idx % restart_interval) == 0) {
                if (!jb_skip_restart(br)) return 0;
                dc_pred[0] = 0;
            }
            int16_t *blk = block_of(c, bx, by);
            int sym = jh_decode(br, hd); if (sym < 0) return 0;
            int diff = 0;
            if (sym > 0) { int vb = jb_get_bits(br, sym); if (vb < 0) return 0; diff = jb_extend(vb, sym); }
            dc_pred[0] += diff;
            blk[0] = (int16_t)(dc_pred[0] * qt[qid][0]);
            int k = 1;
            while (k < 64) {
                int ac_sym = jh_decode(br, ha); if (ac_sym < 0) return 0;
                int run = ac_sym >> 4, sz = ac_sym & 0x0F;
                if (sz == 0) { if (run == 15) { k += 16; continue; } break; }
                k += run; if (k >= 64) return 0;
                int vb = jb_get_bits(br, sz); if (vb < 0) return 0;
                blk[g_zigzag[k]] = (int16_t)(jb_extend(vb, sz) * qt[qid][k]);
                k++;
            }
            idx++;
        }
    } else {
        int mcu_idx = 0;
        for (int my = 0; my < mcus_y; my++) for (int mx = 0; mx < mcus_x; mx++) {
            if (restart_interval > 0 && mcu_idx > 0 && (mcu_idx % restart_interval) == 0) {
                if (!jb_skip_restart(br)) return 0;
                for (int i = 0; i < MAX_COMPS; i++) dc_pred[i] = 0;
            }
            for (int ci = 0; ci < ncomp; ci++) {
                jcomp_t *c = comps[ci];
                const jhuff_t *hd = &hdc_tab[c->dc_table];
                const jhuff_t *ha = &hac_tab[c->ac_table];
                int qid = c->quant_id;
                for (int by = 0; by < c->v; by++) for (int bx = 0; bx < c->h; bx++) {
                    int block_x = mx * c->h + bx, block_y = my * c->v + by;
                    if (block_x >= c->bw || block_y >= c->bh) continue;
                    int16_t *blk = block_of(c, block_x, block_y);
                    int sym = jh_decode(br, hd); if (sym < 0) return 0;
                    int diff = 0;
                    if (sym > 0) { int vb = jb_get_bits(br, sym); if (vb < 0) return 0; diff = jb_extend(vb, sym); }
                    dc_pred[ci] += diff;
                    blk[0] = (int16_t)(dc_pred[ci] * qt[qid][0]);
                    int k = 1;
                    while (k < 64) {
                        int ac_sym = jh_decode(br, ha); if (ac_sym < 0) return 0;
                        int run = ac_sym >> 4, sz = ac_sym & 0x0F;
                        if (sz == 0) { if (run == 15) { k += 16; continue; } break; }
                        k += run; if (k >= 64) return 0;
                        int vb = jb_get_bits(br, sz); if (vb < 0) return 0;
                        blk[g_zigzag[k]] = (int16_t)(jb_extend(vb, sz) * qt[qid][k]);
                        k++;
                    }
                }
            }
            mcu_idx++;
        }
    }
    return 1;
}

// ============================================================
// DC first — non-interleaved
// ============================================================
static int scan_dc_first_ni(jb_t *br, jcomp_t *c, const jhuff_t *hdc_tab,
                            const uint8_t *qt, int al, int restart_interval)
{
    const jhuff_t *hd = &hdc_tab[c->dc_table];
    int p1 = 1 << al;
    int16_t pred = 0;
    int idx = 0;
    for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
        if (restart_interval > 0 && idx > 0 && (idx % restart_interval) == 0) {
            if (!jb_skip_restart(br)) return 0;
            pred = 0;
        }
        int sym = jh_decode(br, hd); if (sym < 0) return 0;
        int diff = 0;
        if (sym > 0) { int vb = jb_get_bits(br, sym); if (vb < 0) return 0; diff = jb_extend(vb, sym); }
        pred += diff;
        block_of(c, bx, by)[0] = (int16_t)(pred * p1 * qt[0]);
        idx++;
    }
    return 1;
}

// ============================================================
// DC first — interleaved
// ============================================================
static int scan_dc_first_int(jb_t *br, jcomp_t **comps, int ncomp,
                             int16_t *dc_pred, const jhuff_t *hdc_tab,
                             const uint8_t (*qt)[64],
                             int mcus_x, int mcus_y, int al, int restart_interval)
{
    int p1 = 1 << al;
    int mcu_idx = 0;
    for (int my = 0; my < mcus_y; my++) for (int mx = 0; mx < mcus_x; mx++) {
        if (restart_interval > 0 && mcu_idx > 0 && (mcu_idx % restart_interval) == 0) {
            if (!jb_skip_restart(br)) return 0;
            for (int i = 0; i < MAX_COMPS; i++) dc_pred[i] = 0;
        }
        for (int ci = 0; ci < ncomp; ci++) {
            jcomp_t *c = comps[ci];
            const jhuff_t *hd = &hdc_tab[c->dc_table];
            int qid = c->quant_id;
            for (int by = 0; by < c->v; by++) for (int bx = 0; bx < c->h; bx++) {
                int block_x = mx * c->h + bx, block_y = my * c->v + by;
                if (block_x >= c->bw || block_y >= c->bh) continue;
                int sym = jh_decode(br, hd); if (sym < 0) return 0;
                int diff = 0;
                if (sym > 0) { int vb = jb_get_bits(br, sym); if (vb < 0) return 0; diff = jb_extend(vb, sym); }
                dc_pred[ci] += diff;
                block_of(c, block_x, block_y)[0] = (int16_t)(dc_pred[ci] * p1 * qt[qid][0]);
            }
        }
        mcu_idx++;
    }
    return 1;
}

// ============================================================
// DC refine
// ============================================================
static int scan_dc_refine(jb_t *br, jcomp_t **comps, int ncomp, int al,
                          int non_interleaved, int restart_interval)
{
    (void)non_interleaved;
    for (int ci = 0; ci < ncomp; ci++) {
        jcomp_t *c = comps[ci];
        int idx = 0;
        for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
            if (restart_interval > 0 && idx > 0 && (idx % restart_interval) == 0) {
                if (!jb_skip_restart(br)) return 0;
            }
            int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
            if (bit) {
                int16_t *blk = block_of(c, bx, by);
                int16_t cur = blk[0];
                if ((cur & (1 << al)) == 0) {
                    if (cur >= 0) blk[0] = (int16_t)(cur + (1 << al));
                    else          blk[0] = (int16_t)(cur - (1 << al));
                }
            }
            idx++;
        }
    }
    return 1;
}

static int scan_dc_refine_int(jb_t *br, jcomp_t **comps, int ncomp, int al,
                              int mcus_x, int mcus_y, int restart_interval)
{
    int mcu_idx = 0;
    for (int my = 0; my < mcus_y; my++) for (int mx = 0; mx < mcus_x; mx++) {
        if (restart_interval > 0 && mcu_idx > 0 && (mcu_idx % restart_interval) == 0) {
            if (!jb_skip_restart(br)) return 0;
        }
        for (int ci = 0; ci < ncomp; ci++) {
            jcomp_t *c = comps[ci];
            for (int by = 0; by < c->v; by++) for (int bx = 0; bx < c->h; bx++) {
                int block_x = mx * c->h + bx, block_y = my * c->v + by;
                if (block_x >= c->bw || block_y >= c->bh) continue;
                int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
                if (bit) {
                    int16_t *blk = block_of(c, block_x, block_y);
                    int16_t cur = blk[0];
                    if ((cur & (1 << al)) == 0) {
                        if (cur >= 0) blk[0] = (int16_t)(cur + (1 << al));
                        else          blk[0] = (int16_t)(cur - (1 << al));
                    }
                }
            }
        }
        mcu_idx++;
    }
    return 1;
}

// ============================================================
// AC first
// ============================================================
static int scan_ac_first(jb_t *br, jcomp_t *c, const jhuff_t *hac_tab,
                         const uint8_t *qt, int ss, int se, int al,
                         int restart_interval)
{
    const jhuff_t *ha = &hac_tab[c->ac_table];
    int p1 = 1 << al;
    int idx = 0;
    for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
        if (restart_interval > 0 && idx > 0 && (idx % restart_interval) == 0) {
            if (!jb_skip_restart(br)) return 0;
        }
        int16_t *blk = block_of(c, bx, by);
        int k = ss;
        while (k <= se) {
            int sym = jh_decode(br, ha);
            if (sym < 0) {
                uart_puts("[jpeg] AC1 huff fail idx="); uart_putu((uint64_t)idx);
                uart_puts(" k="); uart_putu((uint64_t)k);
                uart_puts(" pos="); uart_putu((uint64_t)br->pos);
                uart_puts("\n");
                return 0;
            }
            int run = sym >> 4, size = sym & 0x0F;
            if (size == 0) { if (run == 15) { k += 16; continue; } break; }
            k += run;
            if (k > 63) return 0;
            int vb = jb_get_bits(br, size); if (vb < 0) return 0;
            blk[g_zigzag[k]] = (int16_t)(jb_extend(vb, size) * p1 * qt[k]);
            k++;
        }
        idx++;
    }
    return 1;
}

// ============================================================
// AC refine
// ============================================================
static int scan_ac_refine(jb_t *br, jcomp_t *c, const jhuff_t *hac_tab,
                          int ss, int se, int al, int restart_interval)
{
    const jhuff_t *ha = &hac_tab[c->ac_table];
    int p1 = 1 << al, m1 = -(1 << al);
    int eobrun = 0;
    int idx = 0;

    for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
        if (restart_interval > 0 && idx > 0 && (idx % restart_interval) == 0) {
            if (!jb_skip_restart(br)) return 0;
            eobrun = 0;
        }
        int16_t *blk = block_of(c, bx, by);
        int k = ss;

        if (eobrun > 0) {
            for (; k <= se; k++) {
                int rm = g_zigzag[k];
                if (blk[rm] != 0) {
                    int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
                    if (bit && (blk[rm] & p1) == 0) {
                        if (blk[rm] > 0) blk[rm] = (int16_t)(blk[rm] + p1);
                        else             blk[rm] = (int16_t)(blk[rm] + m1);
                    }
                }
            }
            eobrun--; idx++; continue;
        }

        while (k <= se) {
            int sym = jh_decode(br, ha); if (sym < 0) return 0;
            int r = sym >> 4, s = sym & 0x0F;
            if (s == 0) {
                if (r < 15) {
                    eobrun = 1 << r;
                    if (r > 0) { int extra = jb_get_bits(br, r); if (extra < 0) return 0; eobrun += extra; }
                    eobrun--;
                    for (; k <= se; k++) {
                        int rm = g_zigzag[k];
                        if (blk[rm] != 0) {
                            int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
                            if (bit && (blk[rm] & p1) == 0) {
                                if (blk[rm] > 0) blk[rm] = (int16_t)(blk[rm] + p1);
                                else             blk[rm] = (int16_t)(blk[rm] + m1);
                            }
                        }
                    }
                    break;
                } else {
                    int zs = 16;
                    while (k <= se && zs > 0) {
                        int rm = g_zigzag[k];
                        if (blk[rm] != 0) {
                            int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
                            if (bit && (blk[rm] & p1) == 0) {
                                if (blk[rm] > 0) blk[rm] = (int16_t)(blk[rm] + p1);
                                else             blk[rm] = (int16_t)(blk[rm] + m1);
                            }
                        } else zs--;
                        k++;
                    }
                    continue;
                }
            }
            if (s != 1) return 0;
            int bit = jb_get_bits(br, 1); if (bit < 0) return 0;
            int16_t newval = bit ? (int16_t)p1 : (int16_t)m1;
            while (k <= se && r > 0) {
                int rm = g_zigzag[k];
                if (blk[rm] != 0) {
                    int cb = jb_get_bits(br, 1); if (cb < 0) return 0;
                    if (cb && (blk[rm] & p1) == 0) {
                        if (blk[rm] > 0) blk[rm] = (int16_t)(blk[rm] + p1);
                        else             blk[rm] = (int16_t)(blk[rm] + m1);
                    }
                } else r--;
                k++;
            }
            while (k <= se) {
                int rm = g_zigzag[k];
                if (blk[rm] == 0) break;
                int cb = jb_get_bits(br, 1); if (cb < 0) return 0;
                if (cb && (blk[rm] & p1) == 0) {
                    if (blk[rm] > 0) blk[rm] = (int16_t)(blk[rm] + p1);
                    else             blk[rm] = (int16_t)(blk[rm] - p1);
                }
                k++;
            }
            if (k <= se) { blk[g_zigzag[k]] = newval; k++; }
        }
        idx++;
    }
    return 1;
}

// ============================================================
// main
// ============================================================
int jpeg_decode(const uint8_t *data, uint32_t len, image_t *out) {
    g_jpeg_err[0] = 0;

    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        jerr("not JPEG (no SOI)");
        return 0;
    }

    uint32_t pos = 2;

    uint8_t  qt[4][64];
    int      qt_valid[4] = {0};
    jhuff_t  hdc[4], hac[4];
    int      hdc_valid[4] = {0}, hac_valid[4] = {0};

    int      width = 0, height = 0;
    int      num_comp = 0;
    int      is_progressive = 0;
    int      hmax = 1, vmax = 1;
    int      restart_interval = 0;

    jcomp_t  comps[MAX_COMPS];
    for (int i = 0; i < MAX_COMPS; i++) {
        comps[i].id = 0; comps[i].h = comps[i].v = 0;
        comps[i].quant_id = 0;
        comps[i].dc_table = comps[i].ac_table = 0;
        comps[i].bw = comps[i].bh = 0;
        comps[i].coeffs = 0;
    }
    uint64_t coeffs_bytes[MAX_COMPS] = {0};
    int coeffs_allocated = 0;
    int got_sof = 0;
    int scan_count = 0;

    while (pos + 1 < len) {
        if (data[pos] != 0xFF) { jerr("expected marker"); goto fail; }
        while (pos < len && data[pos] == 0xFF) pos++;
        if (pos >= len) break;
        uint8_t marker = data[pos++];

        if (marker == 0xD8) continue;
        if (marker == 0xD9) break;

        if (pos + 2 > len) { jerr("truncated segment"); goto fail; }
        uint16_t seglen = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (seglen < 2 || pos + seglen - 2 > len) { jerr("bad segment length"); goto fail; }
        uint32_t seg_end = pos + seglen - 2;

        if (marker == 0xDB) {
            while (pos < seg_end) {
                int pq_tq = data[pos++]; int pq = pq_tq >> 4; int tq = pq_tq & 0x0F;
                if (tq > 3) { jerr("DQT id>3"); goto fail; }
                for (int i = 0; i < 64; i++) {
                    if (pq == 0) qt[tq][i] = data[pos++];
                    else { qt[tq][i] = data[pos]; pos += 2; }
                }
                qt_valid[tq] = 1;
            }
        } else if (marker == 0xC4) {
            while (pos < seg_end) {
                int tc_th = data[pos++]; int tc = tc_th >> 4; int th = tc_th & 0x0F;
                if (th > 3 || tc > 1) { jerr("DHT bad id"); goto fail; }
                uint8_t counts[16]; int total = 0;
                for (int i = 0; i < 16; i++) { counts[i] = data[pos++]; total += counts[i]; }
                uint8_t vals[256];
                for (int i = 0; i < total; i++) vals[i] = data[pos++];
                if (tc == 0) { jh_build(&hdc[th], counts, vals); hdc_valid[th] = 1; }
                else         { jh_build(&hac[th], counts, vals); hac_valid[th] = 1; }
            }
        } else if (marker == 0xDD) {
            if (seglen != 4) { jerr("DRI bad len"); goto fail; }
            restart_interval = ((int)data[pos] << 8) | data[pos + 1];
            uart_puts("[jpeg] DRI interval="); uart_putu((uint64_t)restart_interval); uart_puts("\n");
        } else if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (got_sof) { jerr("multiple SOF"); goto fail; }
            got_sof = 1;
            if (marker == 0xC2) is_progressive = 1;

            int prec = data[pos++];
            if (prec != 8) { jerr("only 8-bit"); goto fail; }
            height = ((int)data[pos] << 8) | data[pos + 1]; pos += 2;
            width  = ((int)data[pos] << 8) | data[pos + 1]; pos += 2;
            num_comp = data[pos++];
            if (num_comp != 1 && num_comp != 3) { jerr("only 1 or 3 comps"); goto fail; }
            for (int i = 0; i < num_comp; i++) {
                comps[i].id = data[pos++];
                int hv = data[pos++];
                comps[i].h = hv >> 4; comps[i].v = hv & 0x0F;
                comps[i].quant_id = data[pos++];
                if (comps[i].h < 1 || comps[i].h > 4) { jerr("bad H"); goto fail; }
                if (comps[i].v < 1 || comps[i].v > 4) { jerr("bad V"); goto fail; }
            }
            if (width <= 0 || height <= 0) { jerr("bad dims"); goto fail; }
            if ((uint32_t)width > JPEG_MAX_DIM || (uint32_t)height > JPEG_MAX_DIM) { jerr("too big"); goto fail; }
            if ((uint64_t)width * height > JPEG_MAX_PIXELS) { jerr("too many pixels"); goto fail; }
            for (int i = 0; i < num_comp; i++) {
                int qid = comps[i].quant_id;
                if (qid > 3 || !qt_valid[qid]) { jerr("no quant table"); goto fail; }
            }

            hmax = 1; vmax = 1;
            for (int i = 0; i < num_comp; i++) {
                if (comps[i].h > hmax) hmax = comps[i].h;
                if (comps[i].v > vmax) vmax = comps[i].v;
            }

            uart_puts("[jpeg] SOF prog="); uart_putu((uint64_t)is_progressive);
            uart_puts(" "); uart_putu((uint64_t)width); uart_puts("x"); uart_putu((uint64_t)height);
            uart_puts(" comps="); uart_putu((uint64_t)num_comp);
            uart_puts(" hmax="); uart_putu((uint64_t)hmax);
            uart_puts(" vmax="); uart_putu((uint64_t)vmax);
            uart_puts("\n");
            for (int i = 0; i < num_comp; i++) {
                uart_puts("[jpeg]   comp id="); uart_putu((uint64_t)comps[i].id);
                uart_puts(" h="); uart_putu((uint64_t)comps[i].h);
                uart_puts(" v="); uart_putu((uint64_t)comps[i].v);
                uart_puts(" q="); uart_putu((uint64_t)comps[i].quant_id);
                uart_puts("\n");
            }

            int mcu_w = hmax * 8, mcu_h = vmax * 8;
            int mcus_x = (width + mcu_w - 1) / mcu_w;
            int mcus_y = (height + mcu_h - 1) / mcu_h;
            for (int i = 0; i < num_comp; i++) {
                comps[i].bw = mcus_x * comps[i].h;
                comps[i].bh = mcus_y * comps[i].v;
                coeffs_bytes[i] = (uint64_t)comps[i].bw * comps[i].bh * 64 * sizeof(int16_t);
                comps[i].coeffs = (int16_t *)page_alloc_bytes(coeffs_bytes[i]);
                if (!comps[i].coeffs) { jerr("coeffs alloc"); goto fail; }
                for (uint64_t k = 0; k < (uint64_t)comps[i].bw * comps[i].bh * 64; k++) comps[i].coeffs[k] = 0;
                uart_puts("[jpeg]   bw="); uart_putu((uint64_t)comps[i].bw);
                uart_puts(" bh="); uart_putu((uint64_t)comps[i].bh);
                uart_puts("\n");
            }
            coeffs_allocated = 1;

        } else if (marker == 0xDA) {
            if (!got_sof) { jerr("SOS before SOF"); goto fail; }
            if (!coeffs_allocated) { jerr("coeffs not allocated"); goto fail; }

            int ns = data[pos++];
            if (ns < 1 || ns > num_comp) { jerr("bad NS"); goto fail; }
            jcomp_t *scan_comps[MAX_COMPS];
            int ns_valid = 0;
            for (int i = 0; i < ns; i++) {
                int cs = data[pos++], tdta = data[pos++];
                int found = -1;
                for (int j = 0; j < num_comp; j++) if (comps[j].id == cs) { found = j; break; }
                if (found < 0) { jerr("SOS comp id unknown"); goto fail; }
                comps[found].dc_table = tdta >> 4;
                comps[found].ac_table = tdta & 0x0F;
                scan_comps[ns_valid++] = &comps[found];
            }
            int ss = data[pos++], se = data[pos++], ahal = data[pos++];
            int ah = ahal >> 4, al = ahal & 0x0F;
            scan_count++;

            uart_puts("[jpeg] scan #"); uart_putu((uint64_t)scan_count);
            uart_puts(" Ss="); uart_putu((uint64_t)ss);
            uart_puts(" Se="); uart_putu((uint64_t)se);
            uart_puts(" Ah="); uart_putu((uint64_t)ah);
            uart_puts(" Al="); uart_putu((uint64_t)al);
            uart_puts(" ns="); uart_putu((uint64_t)ns);
            uart_puts(" pos="); uart_putu((uint64_t)pos);
            uart_puts("\n");
            for (int i = 0; i < ns_valid; i++) {
                uart_puts("[jpeg]   scan comp id=");
                uart_putu((uint64_t)scan_comps[i]->id);
                uart_puts(" dc="); uart_putu((uint64_t)scan_comps[i]->dc_table);
                uart_puts(" ac="); uart_putu((uint64_t)scan_comps[i]->ac_table);
                uart_puts("\n");
            }
            dump_bytes("first 16 bytes", data + pos, 16);

            jb_t br;
            br.data = data; br.size = len; br.pos = pos;
            br.bits = 0; br.nbits = 0;

            int mcus_x = (width + hmax * 8 - 1) / (hmax * 8);
            int mcus_y = (height + vmax * 8 - 1) / (vmax * 8);
            int ni = (ns == 1);

            int ok = 1;
            if (!is_progressive) {
                ok = scan_baseline(&br, scan_comps, ns, hdc, hac, qt,
                                   mcus_x, mcus_y, ni, restart_interval);
                if (!ok) { jerr("baseline scan failed"); goto fail; }
            } else {
                if (ss == 0 && se == 0 && ah == 0) {
                    if (ni) {
                        if (!scan_dc_first_ni(&br, scan_comps[0], hdc,
                                              qt[scan_comps[0]->quant_id], al,
                                              restart_interval)) {
                            jlog("DC first ni FAILED (continuing)");
                            ok = 0;
                        }
                    } else {
                        int16_t dc_pred[MAX_COMPS] = {0};
                        if (!scan_dc_first_int(&br, scan_comps, ns, dc_pred,
                                               hdc, qt, mcus_x, mcus_y, al,
                                               restart_interval)) {
                            jlog("DC first int FAILED (continuing)");
                            ok = 0;
                        }
                    }
                } else if (ss == 0 && se == 0 && ah > 0) {
                    if (ni) {
                        if (!scan_dc_refine(&br, scan_comps, ns, al, 1, restart_interval)) {
                            jlog("DC refine FAILED (continuing)"); ok = 0;
                        }
                    } else {
                        if (!scan_dc_refine_int(&br, scan_comps, ns, al, mcus_x, mcus_y,
                                                restart_interval)) {
                            jlog("DC refine int FAILED (continuing)"); ok = 0;
                        }
                    }
                } else if (ss > 0 && ah == 0) {
                    if (!ni) { jlog("AC first must be 1 comp (continuing)"); ok = 0; }
                    else if (!scan_ac_first(&br, scan_comps[0], hac, qt[scan_comps[0]->quant_id],
                                            ss, se, al, restart_interval)) {
                        jlog("AC first FAILED (continuing)"); ok = 0;
                    }
                } else {
                    if (!ni) { jlog("AC refine must be 1 comp (continuing)"); ok = 0; }
                    else if (!scan_ac_refine(&br, scan_comps[0], hac, ss, se, al,
                                             restart_interval)) {
                        jlog("AC refine FAILED (continuing)"); ok = 0;
                    }
                }
            }
            (void)ok;

            // После скана — ищем следующий маркер
            pos = br.pos;
            while (pos + 1 < len) {
                if (data[pos] == 0xFF && data[pos + 1] != 0x00) break;
                pos++;
            }
            continue;

        } else if (marker >= 0xC3 && marker <= 0xCF) {
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) { /* уже обработано */ }
            else if (marker == 0xC4 || marker == 0xC8 || marker == 0xCC) { /* уже обработано */ }
            else {
                uart_puts("[jpeg] unsupported SOF marker 0x");
                static const char hex[] = "0123456789ABCDEF";
                uart_puts((char[]){hex[(marker>>4)&0xF], hex[marker&0xF], 0});
                uart_puts("\n");
                jerr("unsupported JPEG SOF type");
                goto fail;
            }
        }

        pos = seg_end;
    }

    if (!got_sof) { jerr("no SOF"); goto fail; }
    if (!coeffs_allocated) { jerr("no SOS"); goto fail; }

    // ============================================================
    // IDCT + YCbCr (всегда, даже если часть сканов упала)
    // ============================================================
    uint8_t *comp8[MAX_COMPS] = {0};
    uint64_t comp8_bytes[MAX_COMPS] = {0};

    for (int i = 0; i < num_comp; i++) {
        jcomp_t *c = &comps[i];
        int full_w = c->bw * 8, full_h = c->bh * 8;
        comp8_bytes[i] = (uint64_t)full_w * full_h;
        comp8[i] = (uint8_t *)page_alloc_bytes(comp8_bytes[i]);
        if (!comp8[i]) { jerr("idct buf alloc"); goto fail; }
        for (int by = 0; by < c->bh; by++) for (int bx = 0; bx < c->bw; bx++) {
            int16_t *coef = block_of(c, bx, by);
            int32_t blk[64];
            for (int k = 0; k < 64; k++) blk[k] = coef[k];
            idct_block(blk);
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
                comp8[i][(uint64_t)(by*8+y) * full_w + (bx*8+x)] = (uint8_t)blk[y*8+x];
        }
    }

    uint32_t *out_pix = (uint32_t *)page_alloc_bytes((uint64_t)width * height * 4);
    if (!out_pix) { jerr("output alloc"); goto fail; }

    if (num_comp == 1) {
        uint8_t *Y = comp8[0];
        int Ys = comps[0].bw * 8;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            uint8_t g = Y[(uint64_t)y * Ys + x];
            out_pix[(uint64_t)y * width + x] = 0xFF000000u | ((uint32_t)g << 16) | ((uint32_t)g << 8) | g;
        }
    } else {
        uint8_t *Y = comp8[0], *Cb = comp8[1], *Cr = comp8[2];
        int Ys = comps[0].bw * 8, Cbs = comps[1].bw * 8, Crs = comps[2].bw * 8;
        int hY = comps[0].h, vY = comps[0].v;
        int hCb = comps[1].h, vCb = comps[1].v;
        int hCr = comps[2].h, vCr = comps[2].v;
        for (int y = 0; y < height; y++) {
            int cby = (y * vCb) / vY, cry = (y * vCr) / vY;
            for (int x = 0; x < width; x++) {
                int cbx = (x * hCb) / hY, crx = (x * hCr) / hY;
                int Yv = Y[(uint64_t)y*Ys + x];
                int Cbv = Cb[(uint64_t)cby*Cbs + cbx] - 128;
                int Crv = Cr[(uint64_t)cry*Crs + crx] - 128;
                int r = Yv + (359*Crv)/256;
                int g = Yv - (88*Cbv)/256 - (183*Crv)/256;
                int b = Yv + (454*Cbv)/256;
                if (r < 0) r = 0; if (r > 255) r = 255;
                if (g < 0) g = 0; if (g > 255) g = 255;
                if (b < 0) b = 0; if (b > 255) b = 255;
                out_pix[(uint64_t)y * width + x] =
                    0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    for (int i = 0; i < num_comp; i++) {
        if (comps[i].coeffs) page_free_bytes(comps[i].coeffs, coeffs_bytes[i]);
        if (comp8[i]) page_free_bytes(comp8[i], comp8_bytes[i]);
    }

    out->width = (uint32_t)width; out->height = (uint32_t)height;
    out->pixels = out_pix; out->ok = 1;
    g_jpeg_err[0] = 0;
    return 1;

fail:
    for (int i = 0; i < num_comp; i++)
        if (comps[i].coeffs) page_free_bytes(comps[i].coeffs, coeffs_bytes[i]);
    return 0;
}