// BrainixKernel (BKOS) — inflate.c
// Распаковка zlib/deflate «с нуля», без libc и без malloc.

#include <stdint.h>
#include "inflate.h"

typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       byte_pos;
    uint32_t       bit_pos;
} bitreader_t;

static int br_read_bit(bitreader_t *br, uint32_t *out) {
    if (br->byte_pos >= br->size) return 0;
    *out = (br->data[br->byte_pos] >> br->bit_pos) & 1u;
    br->bit_pos++;
    if (br->bit_pos == 8) { br->bit_pos = 0; br->byte_pos++; }
    return 1;
}

static int br_read(bitreader_t *br, int n, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        uint32_t bit;
        if (!br_read_bit(br, &bit)) return 0;
        v |= bit << i;
    }
    *out = v;
    return 1;
}

static void br_align_byte(bitreader_t *br) {
    if (br->bit_pos != 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
    int      max_bits;
} huffman_t;

static void huff_build(huffman_t *h, const uint8_t *lengths, int n) {
    for (int i = 0; i < 16; i++) h->counts[i] = 0;
    for (int i = 0; i < n; i++) h->counts[lengths[i]]++;
    h->counts[0] = 0;

    uint16_t offs[16];
    offs[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->counts[i];

    for (int i = 0; i < n; i++) {
        if (lengths[i]) h->symbols[offs[lengths[i]]++] = (uint16_t)i;
    }

    h->max_bits = 0;
    for (int i = 15; i >= 1; i--) {
        if (h->counts[i]) { h->max_bits = i; break; }
    }
}

static int huff_decode(bitreader_t *br, const huffman_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= h->max_bits; len++) {
        uint32_t bit;
        if (!br_read_bit(br, &bit)) return -1;
        code |= (int)bit;
        int count = h->counts[len];
        if (code - first < count) {
            return h->symbols[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0
};

static const uint16_t DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int inflate_huff_block(bitreader_t *br,
                              uint8_t *dst, uint32_t dst_cap, uint32_t *out_pos,
                              const huffman_t *lit, const huffman_t *dist)
{
    for (;;) {
        int sym = huff_decode(br, lit);
        if (sym < 0) return 0;
        if (sym < 256) {
            if (*out_pos >= dst_cap) return 0;
            dst[(*out_pos)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            sym -= 257;
            if (sym >= 29) return 0;

            uint32_t extra = 0;
            if (LEN_EXTRA[sym] > 0 && !br_read(br, LEN_EXTRA[sym], &extra)) return 0;
            uint32_t length = LEN_BASE[sym] + extra;

            int dsym = huff_decode(br, dist);
            if (dsym < 0 || dsym >= 30) return 0;

            extra = 0;
            if (DIST_EXTRA[dsym] > 0 && !br_read(br, DIST_EXTRA[dsym], &extra)) return 0;
            uint32_t distance = DIST_BASE[dsym] + extra;

            if (distance > *out_pos) return 0;
            uint32_t start = *out_pos - distance;
            for (uint32_t i = 0; i < length; i++) {
                if (*out_pos >= dst_cap) return 0;
                dst[*out_pos] = dst[start + i];
                (*out_pos)++;
            }
        }
    }
}

static int inflate_stored_block(bitreader_t *br,
                                uint8_t *dst, uint32_t dst_cap, uint32_t *out_pos)
{
    br_align_byte(br);

    if (br->byte_pos + 4 > br->size) return 0;
    const uint8_t *p = br->data + br->byte_pos;
    uint32_t len  = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    uint32_t nlen = (uint32_t)p[2] | ((uint32_t)p[3] << 8);
    if ((len ^ nlen) != 0xFFFFu) return 0;
    br->byte_pos += 4;

    if (br->byte_pos + len > br->size) return 0;
    if (*out_pos + len > dst_cap) return 0;
    for (uint32_t i = 0; i < len; i++) {
        dst[(*out_pos)++] = br->data[br->byte_pos + i];
    }
    br->byte_pos += len;
    return 1;
}

static int inflate_fixed_block(bitreader_t *br,
                               uint8_t *dst, uint32_t dst_cap, uint32_t *out_pos)
{
    uint8_t lit_lens[288];
    for (int i = 0;   i <= 143; i++) lit_lens[i] = 8;
    for (int i = 144; i <= 255; i++) lit_lens[i] = 9;
    for (int i = 256; i <= 279; i++) lit_lens[i] = 7;
    for (int i = 280; i <= 287; i++) lit_lens[i] = 8;

    uint8_t dist_lens[32];
    for (int i = 0; i < 32; i++) dist_lens[i] = 5;

    huffman_t lit, dist;
    huff_build(&lit,  lit_lens,  288);
    huff_build(&dist, dist_lens, 32);
    return inflate_huff_block(br, dst, dst_cap, out_pos, &lit, &dist);
}

static int inflate_dynamic_block(bitreader_t *br,
                                 uint8_t *dst, uint32_t dst_cap, uint32_t *out_pos)
{
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!br_read(br, 5, &hlit))  return 0;
    if (!br_read(br, 5, &hdist)) return 0;
    if (!br_read(br, 4, &hclen)) return 0;
    hlit  += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 288 || hdist > 32) return 0;

    static const uint8_t ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint8_t cl_lens[19];
    for (int i = 0; i < 19; i++) cl_lens[i] = 0;
    for (uint32_t i = 0; i < hclen; i++) {
        uint32_t v = 0;
        if (!br_read(br, 3, &v)) return 0;
        cl_lens[ORDER[i]] = (uint8_t)v;
    }

    huffman_t cl;
    huff_build(&cl, cl_lens, 19);

    uint8_t lens[288 + 32];
    uint32_t total = hlit + hdist;
    uint32_t n = 0;
    while (n < total) {
        int sym = huff_decode(br, &cl);
        if (sym < 0) return 0;
        if (sym < 16) {
            lens[n++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (n == 0) return 0;
            uint32_t extra = 0;
            if (!br_read(br, 2, &extra)) return 0;
            uint8_t prev = lens[n - 1];
            for (uint32_t i = 0; i < 3 + extra && n < total; i++) lens[n++] = prev;
        } else if (sym == 17) {
            uint32_t extra = 0;
            if (!br_read(br, 3, &extra)) return 0;
            for (uint32_t i = 0; i < 3 + extra && n < total; i++) lens[n++] = 0;
        } else if (sym == 18) {
            uint32_t extra = 0;
            if (!br_read(br, 7, &extra)) return 0;
            for (uint32_t i = 0; i < 11 + extra && n < total; i++) lens[n++] = 0;
        } else {
            return 0;
        }
    }

    huffman_t lit, dist;
    huff_build(&lit,  lens,        (int)hlit);
    huff_build(&dist, lens + hlit, (int)hdist);
    return inflate_huff_block(br, dst, dst_cap, out_pos, &lit, &dist);
}

int inflate_zlib(const uint8_t *src, uint32_t src_len,
                 uint8_t *dst, uint32_t dst_cap, uint32_t *out_len)
{
    if (src_len < 2) return 0;
    uint8_t cmf = src[0];
    uint8_t flg = src[1];

    if ((cmf & 0x0F) != 8) return 0;
    if ((cmf >> 4) > 7)   return 0;
    if (flg & 0x20)       return 0;
    if ((((uint32_t)cmf * 256 + flg) % 31) != 0) return 0;

    bitreader_t br;
    br.data     = src + 2;
    br.size     = src_len - 2;
    br.byte_pos = 0;
    br.bit_pos  = 0;

    uint32_t out_pos = 0;
    int final = 0;

    while (!final) {
        uint32_t bfinal = 0, btype = 0;
        if (!br_read(&br, 1, &bfinal)) return 0;
        if (!br_read(&br, 2, &btype))  return 0;
        final = (int)bfinal;

        if (btype == 0) {
            if (!inflate_stored_block(&br, dst, dst_cap, &out_pos))  return 0;
        } else if (btype == 1) {
            if (!inflate_fixed_block(&br, dst, dst_cap, &out_pos))   return 0;
        } else if (btype == 2) {
            if (!inflate_dynamic_block(&br, dst, dst_cap, &out_pos)) return 0;
        } else {
            return 0;
        }
    }

    *out_len = out_pos;
    return 1;
}