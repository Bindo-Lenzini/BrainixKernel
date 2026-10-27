// BrainixKernel — png.c
// Декодер PNG + BMP в формат ARGB8888 (0xAARRGGBB).
// JPEG делегируется в jpeg.c.
//
// PNG:
//   - bit_depth = 8: color_type ∈ {0, 2, 3, 4, 6}
//   - bit_depth = 1/2/4: color_type ∈ {0, 3}
//   - interlace: 0 (sequential) и 1 (Adam7)
// BMP:
//   - BI_RGB (без сжатия), 24 или 32 бита

#include <stdint.h>
#include "png.h"
#include "inflate.h"
#include "physmem.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);

extern int         jpeg_decode(const uint8_t *data, uint32_t len, image_t *out);
extern const char *jpeg_last_error(void);

#define IMG_MAX_DIM    4096u
#define IMG_MAX_PIXELS (2048u * 1152u)
#define IDAT_BUF_BYTES (8u * 1024u * 1024u)

// ---- Последняя ошибка ----
static char g_last_error[100] = {0};

static void set_err(const char *s) {
    int i = 0;
    while (s[i] && i < 99) { g_last_error[i] = s[i]; i++; }
    g_last_error[i] = 0;
    uart_puts("[png] ");
    uart_puts(s);
    uart_puts("\n");
}

const char *png_last_error(void) { return g_last_error; }

// ---- Чтение ----
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static uint32_t rd32le(const uint8_t *p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8)  |  (uint32_t)p[0];
}
static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// ---- Память ----
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

static uint32_t *alloc_pixels(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return 0;
    if (w > IMG_MAX_DIM || h > IMG_MAX_DIM) return 0;
    if ((uint64_t)w * h > IMG_MAX_PIXELS) return 0;
    uint64_t bytes = (uint64_t)w * h * 4;
    return (uint32_t *)page_alloc_bytes(bytes);
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p  = (int)a + (int)b - (int)c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int channels_of(uint8_t color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

static uint32_t filter_bpp(int channels, uint8_t bit_depth) {
    uint32_t bits = (uint32_t)channels * (uint32_t)bit_depth;
    if (bits < 8) return 1;
    return bits / 8;
}

// ============================================================
// Adam7-таблица
// ============================================================
static const int ADAM7_X0[7] = {0, 4, 0, 2, 0, 1, 0};
static const int ADAM7_Y0[7] = {0, 0, 4, 0, 2, 0, 1};
static const int ADAM7_DX[7] = {8, 8, 4, 4, 2, 2, 1};
static const int ADAM7_DY[7] = {8, 8, 8, 4, 4, 2, 2};

// ============================================================
// Общая распаковка пикселя (из «сырых» байтов строки) в ARGB.
// line — указатель на строку БЕЗ байта фильтра.
// idx — индекс пикселя в этой строке.
// ============================================================
static uint32_t decode_pixel(const uint8_t *line, uint32_t idx,
                             int ch, uint8_t bit_depth, uint8_t color_type,
                             const uint8_t *plte, const uint8_t *trns)
{
    uint32_t r = 0, g = 0, b = 0, a = 255;

    if (bit_depth == 8) {
        const uint8_t *px = line + (uint64_t)idx * ch;
        if (color_type == 0) { r = g = b = px[0]; }
        else if (color_type == 2) { r = px[0]; g = px[1]; b = px[2]; }
        else if (color_type == 3) {
            uint8_t ii = px[0];
            r = plte[ii * 3 + 0];
            g = plte[ii * 3 + 1];
            b = plte[ii * 3 + 2];
            a = trns[ii];
        }
        else if (color_type == 4) { r = g = b = px[0]; a = px[1]; }
        else if (color_type == 6) { r = px[0]; g = px[1]; b = px[2]; a = px[3]; }
    } else {
        // bit_depth 1/2/4, color_type 0 или 3
        uint32_t per_byte = 8u / bit_depth;
        uint32_t byte_idx = idx / per_byte;
        uint32_t bit_off  = idx % per_byte;
        uint32_t shift    = 8u - bit_depth * (bit_off + 1);
        uint32_t mask     = (1u << bit_depth) - 1u;
        uint8_t  val      = (line[byte_idx] >> shift) & mask;

        if (color_type == 0) {
            uint32_t maxv = (1u << bit_depth) - 1u;
            uint32_t gv   = val * 255u / maxv;
            r = g = b = gv;
        } else {
            uint8_t ii = (uint8_t)val;
            r = plte[ii * 3 + 0];
            g = plte[ii * 3 + 1];
            b = plte[ii * 3 + 2];
            a = trns[ii];
        }
    }
    return ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}

// ============================================================
// Применение фильтра к строке (in-place) с предыдущей строкой prev.
// row_bytes — сколько байт обрабатывать, bpp — фильтрационный bpp.
// ============================================================
static int unfilter_row(uint8_t *line, const uint8_t *prev,
                        uint32_t row_bytes, uint32_t bpp, uint8_t ftype)
{
    if (ftype > 4) return 0;
    for (uint32_t x = 0; x < row_bytes; x++) {
        uint8_t a = (x >= bpp) ? line[x - bpp] : 0;
        uint8_t b = prev ? prev[x] : 0;
        uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
        uint8_t v = line[x];
        switch (ftype) {
            case 0: break;
            case 1: v = (uint8_t)(v + a); break;
            case 2: v = (uint8_t)(v + b); break;
            case 3: v = (uint8_t)(v + ((uint32_t)a + (uint32_t)b) / 2u); break;
            case 4: v = (uint8_t)(v + paeth(a, b, c)); break;
        }
        line[x] = v;
    }
    return 1;
}

// ============================================================
// Sequential (interlace = 0) путь.
// idat уже собран, raw — одна большая область.
// ============================================================
static int png_decode_sequential(const uint8_t *idat, uint32_t idat_len,
                                 uint32_t w, uint32_t h,
                                 int ch, uint8_t bit_depth, uint8_t color_type,
                                 const uint8_t *plte, const uint8_t *trns,
                                 image_t *out)
{
    uint32_t bits_per_pixel = (uint32_t)ch * (uint32_t)bit_depth;
    uint32_t row_bits       = w * bits_per_pixel;
    uint32_t row_bytes      = (row_bits + 7u) / 8u;
    uint32_t stride         = 1u + row_bytes;
    uint64_t raw_size       = (uint64_t)stride * h;

    uint8_t *raw = (uint8_t *)page_alloc_bytes(raw_size);
    if (!raw) { set_err("raw buffer alloc failed"); return 0; }

    uint32_t produced = 0;
    int ok = inflate_zlib(idat, idat_len, raw, (uint32_t)raw_size, &produced);
    if (!ok) {
        set_err("inflate failed (bad zlib/deflate)");
        page_free_bytes(raw, raw_size);
        return 0;
    }
    if (produced != (uint32_t)raw_size) {
        set_err("inflate wrong size");
        page_free_bytes(raw, raw_size);
        return 0;
    }

    uint32_t bpp = filter_bpp(ch, bit_depth);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row  = raw + (uint64_t)y * stride;
        uint8_t  ftyp = row[0];
        uint8_t *line = row + 1;
        uint8_t *prev = (y > 0) ? (raw + (uint64_t)(y - 1) * stride + 1) : 0;
        if (!unfilter_row(line, prev, row_bytes, bpp, ftyp)) {
            set_err("bad PNG filter type");
            page_free_bytes(raw, raw_size);
            return 0;
        }
    }

    uint32_t *pixels = alloc_pixels(w, h);
    if (!pixels) {
        set_err("pixels alloc failed (out of memory?)");
        page_free_bytes(raw, raw_size);
        return 0;
    }

    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = raw + (uint64_t)y * stride + 1;
        for (uint32_t x = 0; x < w; x++) {
            pixels[(uint64_t)y * w + x] =
                decode_pixel(line, x, ch, bit_depth, color_type, plte, trns);
        }
    }

    page_free_bytes(raw, raw_size);
    out->width = w; out->height = h; out->pixels = pixels; out->ok = 1;
    return 1;
}

// ============================================================
// Adam7 (interlace = 1) путь.
// ============================================================
static int png_decode_interlaced(const uint8_t *idat, uint32_t idat_len,
                                 uint32_t w, uint32_t h,
                                 int ch, uint8_t bit_depth, uint8_t color_type,
                                 const uint8_t *plte, const uint8_t *trns,
                                 image_t *out)
{
    uint32_t pass_w[7], pass_h[7], pass_row_bytes[7];
    uint64_t raw_size = 0;

    for (int p = 0; p < 7; p++) {
        int sx = ADAM7_X0[p], sy = ADAM7_Y0[p];
        int dx = ADAM7_DX[p], dy = ADAM7_DY[p];

        uint32_t pw = ((int)w > sx) ? (uint32_t)(((int)w - sx + dx - 1) / dx) : 0;
        uint32_t ph = ((int)h > sy) ? (uint32_t)(((int)h - sy + dy - 1) / dy) : 0;

        pass_w[p] = pw;
        pass_h[p] = ph;

        if (pw == 0 || ph == 0) {
            pass_row_bytes[p] = 0;
            continue;
        }
        uint32_t bits = pw * (uint32_t)ch * (uint32_t)bit_depth;
        uint32_t rbytes = (bits + 7u) / 8u;
        pass_row_bytes[p] = rbytes;
        raw_size += (uint64_t)(1 + rbytes) * ph;
    }

    if (raw_size == 0) { set_err("interlaced: empty image"); return 0; }

    uint8_t *raw = (uint8_t *)page_alloc_bytes(raw_size);
    if (!raw) { set_err("interlaced raw alloc failed"); return 0; }

    uint32_t produced = 0;
    int ok = inflate_zlib(idat, idat_len, raw, (uint32_t)raw_size, &produced);
    if (!ok) {
        set_err("interlaced inflate failed");
        page_free_bytes(raw, raw_size);
        return 0;
    }
    if (produced != (uint32_t)raw_size) {
        set_err("interlaced wrong size");
        page_free_bytes(raw, raw_size);
        return 0;
    }

    uint32_t *pixels = alloc_pixels(w, h);
    if (!pixels) {
        set_err("interlaced pixels alloc failed");
        page_free_bytes(raw, raw_size);
        return 0;
    }
    // Фон — прозрачный чёрный, пока пассы не заполнят всё.
    for (uint64_t i = 0; i < (uint64_t)w * h; i++) pixels[i] = 0;

    uint32_t bpp = filter_bpp(ch, bit_depth);
    uint64_t raw_off = 0;

    for (int p = 0; p < 7; p++) {
        uint32_t pw = pass_w[p], ph = pass_h[p];
        if (pw == 0 || ph == 0) continue;

        uint32_t rbytes = pass_row_bytes[p];
        uint32_t stride = 1u + rbytes;

        // 1) Применяем фильтры (prev — предыдущая строка ТОГО ЖЕ пасса)
        for (uint32_t y = 0; y < ph; y++) {
            uint8_t *row  = raw + raw_off + (uint64_t)y * stride;
            uint8_t  ftyp = row[0];
            uint8_t *line = row + 1;
            uint8_t *prev = (y > 0) ? (raw + raw_off + (uint64_t)(y - 1) * stride + 1) : 0;
            if (!unfilter_row(line, prev, rbytes, bpp, ftyp)) {
                set_err("interlaced bad filter type");
                page_free_bytes(raw, raw_size);
                page_free_bytes(pixels, (uint64_t)w * h * 4);
                return 0;
            }
        }

        // 2) Раскладываем пиксели пасса в полную сетку
        int sx = ADAM7_X0[p], sy = ADAM7_Y0[p];
        int dx = ADAM7_DX[p], dy = ADAM7_DY[p];

        for (uint32_t py = 0; py < ph; py++) {
            uint8_t *line = raw + raw_off + (uint64_t)py * stride + 1;
            int out_y = sy + (int)py * dy;
            if (out_y < 0 || out_y >= (int)h) continue;

            for (uint32_t px = 0; px < pw; px++) {
                int out_x = sx + (int)px * dx;
                if (out_x < 0 || out_x >= (int)w) continue;

                uint32_t argb = decode_pixel(line, px, ch, bit_depth,
                                             color_type, plte, trns);
                pixels[(uint64_t)out_y * w + out_x] = argb;
            }
        }

        raw_off += (uint64_t)stride * ph;
    }

    page_free_bytes(raw, raw_size);

    out->width = w; out->height = h; out->pixels = pixels; out->ok = 1;
    return 1;
}

// ============================================================
// Парсинг PNG: собираем IHDR/PLTE/tRNS/IDAT, потом выбираем путь.
// ============================================================
static int png_decode(const uint8_t *data, uint32_t len, image_t *out) {
    static const uint8_t SIG[8] = {0x89,'P','N','G','\r','\n',0x1A,'\n'};
    if (len < 8 + 25 + 12) { set_err("file too small"); return 0; }
    for (int i = 0; i < 8; i++)
        if (data[i] != SIG[i]) { set_err("bad PNG signature"); return 0; }

    uint32_t off = 8;
    uint32_t w = 0, h = 0;
    uint8_t  bit_depth = 0, color_type = 0;
    int      interlace = 0;
    int      got_ihdr  = 0;

    uint8_t  plte[256 * 3];
    uint8_t  trns[256];
    int      plte_len = 0;
    for (int i = 0; i < 256 * 3; i++) plte[i] = 0;
    for (int i = 0; i < 256; i++) trns[i] = 255;

    uint8_t *idat = (uint8_t *)page_alloc_bytes(IDAT_BUF_BYTES);
    if (!idat) { set_err("idat buffer alloc failed"); return 0; }
    uint32_t idat_len = 0;

    while (off + 12 <= len) {
        uint32_t clen = rd32be(data + off); off += 4;
        const uint8_t *ctype = data + off; off += 4;

        if (off + clen + 4 > len) break;
        const uint8_t *cdata = data + off;
        off += clen + 4;

        if (ctype[0] == 'I' && ctype[1] == 'H' &&
            ctype[2] == 'D' && ctype[3] == 'R') {
            if (clen < 13) { set_err("IHDR too short"); page_free_bytes(idat, IDAT_BUF_BYTES); return 0; }
            w = rd32be(cdata + 0);
            h = rd32be(cdata + 4);
            bit_depth  = cdata[8];
            color_type = cdata[9];
            interlace  = cdata[12];
            got_ihdr   = 1;
        } else if (ctype[0] == 'P' && ctype[1] == 'L' &&
                   ctype[2] == 'T' && ctype[3] == 'E') {
            int n = (int)(clen / 3);
            if (n > 256) n = 256;
            for (int i = 0; i < n * 3; i++) plte[i] = cdata[i];
            plte_len = n;
        } else if (ctype[0] == 't' && ctype[1] == 'R' &&
                   ctype[2] == 'N' && ctype[3] == 'S') {
            int n = (int)clen;
            if (n > 256) n = 256;
            for (int i = 0; i < n; i++) trns[i] = cdata[i];
        } else if (ctype[0] == 'I' && ctype[1] == 'D' &&
                   ctype[2] == 'A' && ctype[3] == 'T') {
            if ((uint64_t)idat_len + clen > IDAT_BUF_BYTES) {
                set_err("IDAT too big (>8MB)");
                page_free_bytes(idat, IDAT_BUF_BYTES);
                return 0;
            }
            for (uint32_t i = 0; i < clen; i++) idat[idat_len + i] = cdata[i];
            idat_len += clen;
        } else if (ctype[0] == 'I' && ctype[1] == 'E' &&
                   ctype[2] == 'N' && ctype[3] == 'D') {
            break;
        }
    }

    if (!got_ihdr) { set_err("no IHDR chunk"); page_free_bytes(idat, IDAT_BUF_BYTES); return 0; }
    if (idat_len == 0) { set_err("no IDAT chunks"); page_free_bytes(idat, IDAT_BUF_BYTES); return 0; }

    int ch = channels_of(color_type);
    if (ch == 0) {
        set_err("unsupported color_type");
        page_free_bytes(idat, IDAT_BUF_BYTES);
        return 0;
    }
    if (bit_depth != 8) {
        if (color_type != 0 && color_type != 3) {
            set_err("bit_depth!=8 only for gray/palette");
            page_free_bytes(idat, IDAT_BUF_BYTES);
            return 0;
        }
        if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4) {
            set_err("unsupported bit_depth");
            page_free_bytes(idat, IDAT_BUF_BYTES);
            return 0;
        }
    }
    if (color_type == 3 && plte_len == 0) {
        set_err("palette PNG but no PLTE chunk");
        page_free_bytes(idat, IDAT_BUF_BYTES);
        return 0;
    }
    if (w == 0 || h == 0 || w > IMG_MAX_DIM || h > IMG_MAX_DIM ||
        (uint64_t)w * h > IMG_MAX_PIXELS) {
        set_err("bad dimensions or too big");
        page_free_bytes(idat, IDAT_BUF_BYTES);
        return 0;
    }

    int r;
    if (interlace == 0) {
        r = png_decode_sequential(idat, idat_len, w, h, ch, bit_depth, color_type,
                                  plte, trns, out);
    } else if (interlace == 1) {
        r = png_decode_interlaced(idat, idat_len, w, h, ch, bit_depth, color_type,
                                  plte, trns, out);
    } else {
        set_err("unsupported interlace method");
        r = 0;
    }

    page_free_bytes(idat, IDAT_BUF_BYTES);

    if (r) g_last_error[0] = 0;
    return r;
}

// ============================================================
// BMP
// ============================================================

static int bmp_decode(const uint8_t *data, uint32_t len, image_t *out) {
    if (len < 54) { set_err("BMP too small"); return 0; }
    if (data[0] != 'B' || data[1] != 'M') { set_err("not a BMP"); return 0; }

    uint32_t data_off = rd32le(data + 10);
    uint32_t hdr_size = rd32le(data + 14);
    if (hdr_size < 40) { set_err("BMP header too small"); return 0; }

    int32_t  w_s  = (int32_t)rd32le(data + 18);
    int32_t  h_s  = (int32_t)rd32le(data + 22);
    uint16_t planes = rd16le(data + 26);
    uint16_t bpp    = rd16le(data + 28);
    uint32_t comp   = rd32le(data + 30);

    if (planes != 1) { set_err("BMP planes!=1"); return 0; }
    if (comp != 0)   { set_err("BMP compressed (only BI_RGB)"); return 0; }
    if (bpp != 24 && bpp != 32) { set_err("BMP bpp unsupported"); return 0; }
    if (w_s <= 0 || h_s == 0) { set_err("BMP bad dims"); return 0; }

    int      flip = 1;
    uint32_t w    = (uint32_t)w_s;
    uint32_t h    = (uint32_t)(h_s < 0 ? -h_s : h_s);
    if (h_s < 0) flip = 0;

    if (w > IMG_MAX_DIM || h > IMG_MAX_DIM) { set_err("BMP too big"); return 0; }
    if ((uint64_t)w * h > IMG_MAX_PIXELS) { set_err("BMP too many pixels"); return 0; }

    uint32_t bytes_pp   = bpp / 8u;
    uint32_t row_raw    = w * bytes_pp;
    uint32_t row_padded = (row_raw + 3u) & ~3u;

    if ((uint64_t)data_off + (uint64_t)row_padded * h > (uint64_t)len) {
        set_err("BMP truncated");
        return 0;
    }

    uint32_t *pixels = alloc_pixels(w, h);
    if (!pixels) { set_err("BMP pixels alloc failed"); return 0; }

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = flip ? (h - 1u - y) : y;
        const uint8_t *row = data + data_off + (uint64_t)src_y * row_padded;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t bl = row[x * bytes_pp + 0];
            uint8_t gr = row[x * bytes_pp + 1];
            uint8_t rd = row[x * bytes_pp + 2];
            uint8_t al = (bytes_pp == 4) ? row[x * bytes_pp + 3] : 255;
            pixels[(uint64_t)y * w + x] =
                ((uint32_t)al << 24) | ((uint32_t)rd << 16) |
                ((uint32_t)gr << 8)  |  (uint32_t)bl;
        }
    }

    g_last_error[0] = 0;
    out->width  = w;
    out->height = h;
    out->pixels = pixels;
    out->ok     = 1;
    return 1;
}

// ============================================================
// Public API
// ============================================================

int image_decode(const uint8_t *data, uint32_t len, image_t *out) {
    out->width  = 0;
    out->height = 0;
    out->pixels = 0;
    out->ok     = 0;
    g_last_error[0] = 0;

    if (len >= 8 &&
        data[0] == 0x89 && data[1] == 'P' &&
        data[2] == 'N'  && data[3] == 'G') {
        return png_decode(data, len, out);
    }
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') {
        return bmp_decode(data, len, out);
    }
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        int r = jpeg_decode(data, len, out);
        if (!r) set_err(jpeg_last_error());
        return r;
    }
    set_err("not PNG, BMP, or JPEG");
    return 0;
}

void image_free(image_t *img) {
    if (!img) return;
    if (img->pixels && img->width && img->height) {
        uint64_t bytes = (uint64_t)img->width * img->height * 4;
        page_free_bytes(img->pixels, bytes);
    }
    img->pixels = 0;
    img->width  = 0;
    img->height = 0;
    img->ok     = 0;
}