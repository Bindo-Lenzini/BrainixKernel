// BrainixKernel — kbd.c
// virtio-keyboard через PCI. desc.addr — PA.

#include <stdint.h>
#include "kbd.h"
#include "virtio.h"
#include "pci.h"
#include "physmem.h"
#include "mmu.h"

extern void uart_puts(const char *s);

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

#define EV_SYN  0x00
#define EV_KEY  0x01

#define KEY_ESC        1
#define KEY_1          2
#define KEY_2          3
#define KEY_3          4
#define KEY_4          5
#define KEY_5          6
#define KEY_6          7
#define KEY_7          8
#define KEY_8          9
#define KEY_9         10
#define KEY_0         11
#define KEY_MINUS     12
#define KEY_EQUAL     13
#define KEY_BACKSPACE 14
#define KEY_TAB       15
#define KEY_Q         16
#define KEY_W         17
#define KEY_E         18
#define KEY_R         19
#define KEY_T         20
#define KEY_Y         21
#define KEY_U         22
#define KEY_I         23
#define KEY_O         24
#define KEY_P         25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER     28
#define KEY_LEFTCTRL  29
#define KEY_A         30
#define KEY_S         31
#define KEY_D         32
#define KEY_F         33
#define KEY_G         34
#define KEY_H         35
#define KEY_J         36
#define KEY_K         37
#define KEY_L         38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE     41
#define KEY_LEFTSHIFT 42
#define KEY_BACKSLASH 43
#define KEY_Z         44
#define KEY_X         45
#define KEY_C         46
#define KEY_V         47
#define KEY_B         48
#define KEY_N         49
#define KEY_M         50
#define KEY_COMMA     51
#define KEY_DOT       52
#define KEY_SLASH     53
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTALT   56
#define KEY_SPACE     57
#define KEY_CAPSLOCK  58
#define KEY_F12       88
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT  100
#define KEY_HOME      102
#define KEY_UP        103
#define KEY_LEFT      105
#define KEY_RIGHT     106
#define KEY_END       107
#define KEY_DOWN      108
#define KEY_DELETE    111
#define KEY_LEFTMETA  125
#define KEY_RIGHTMETA 126

#define K_UP      0x11
#define K_DOWN    0x12
#define K_LEFT    0x13
#define K_RIGHT   0x14
#define K_HOME    0x15
#define K_END     0x16
#define K_DEL     0x17
#define K_CTRL_S  0x18
#define K_CTRL_Q  0x19
#define K_CTRL_C  0x1A
#define K_CTRL_V  0x1B

static int g_shift = 0;
static int g_ctrl  = 0;
static int g_super = 0;
static int g_alt   = 0;
static int g_ru    = 0;
static int g_alt_shift_toggled = 0;

static virtio_mmio_t g_io;
static virtqueue_t   g_evq;
static int           g_ready = 0;
static uint8_t      *g_event_bufs = 0;
static uint16_t      g_last_used = 0;
static void        (*g_handler)(char) = 0;

int kbd_layout_ru(void) { return g_ru; }
const char *kbd_layout_name(void) { return g_ru ? "RU" : "EN"; }

void kbd_set_handler(void (*handler)(char c)) { g_handler = handler; }

static uint32_t ru_lower(uint16_t code) {
    switch (code) {
        case KEY_Q: return 0x0439;
        case KEY_W: return 0x0446;
        case KEY_E: return 0x0443;
        case KEY_R: return 0x043A;
        case KEY_T: return 0x0435;
        case KEY_Y: return 0x043D;
        case KEY_U: return 0x0433;
        case KEY_I: return 0x0448;
        case KEY_O: return 0x0449;
        case KEY_P: return 0x0437;
        case KEY_LEFTBRACE:  return 0x0445;
        case KEY_RIGHTBRACE: return 0x044A;
        case KEY_A: return 0x0444;
        case KEY_S: return 0x044B;
        case KEY_D: return 0x0432;
        case KEY_F: return 0x0430;
        case KEY_G: return 0x043F;
        case KEY_H: return 0x0440;
        case KEY_J: return 0x043E;
        case KEY_K: return 0x043B;
        case KEY_L: return 0x0434;
        case KEY_SEMICOLON:  return 0x0436;
        case KEY_APOSTROPHE: return 0x044D;
        case KEY_Z: return 0x044F;
        case KEY_X: return 0x0447;
        case KEY_C: return 0x0441;
        case KEY_V: return 0x043C;
        case KEY_B: return 0x0438;
        case KEY_N: return 0x0442;
        case KEY_M: return 0x044C;
        case KEY_COMMA: return 0x0431;
        case KEY_DOT:   return 0x044E;
        default: return 0;
    }
}

static uint32_t en_char(uint16_t code, int shift) {
    switch (code) {
        case KEY_A: return shift ? 'A' : 'a';
        case KEY_B: return shift ? 'B' : 'b';
        case KEY_C: return shift ? 'C' : 'c';
        case KEY_D: return shift ? 'D' : 'd';
        case KEY_E: return shift ? 'E' : 'e';
        case KEY_F: return shift ? 'F' : 'f';
        case KEY_G: return shift ? 'G' : 'g';
        case KEY_H: return shift ? 'H' : 'h';
        case KEY_I: return shift ? 'I' : 'i';
        case KEY_J: return shift ? 'J' : 'j';
        case KEY_K: return shift ? 'K' : 'k';
        case KEY_L: return shift ? 'L' : 'l';
        case KEY_M: return shift ? 'M' : 'm';
        case KEY_N: return shift ? 'N' : 'n';
        case KEY_O: return shift ? 'O' : 'o';
        case KEY_P: return shift ? 'P' : 'p';
        case KEY_Q: return shift ? 'Q' : 'q';
        case KEY_R: return shift ? 'R' : 'r';
        case KEY_S: return shift ? 'S' : 's';
        case KEY_T: return shift ? 'T' : 't';
        case KEY_U: return shift ? 'U' : 'u';
        case KEY_V: return shift ? 'V' : 'v';
        case KEY_W: return shift ? 'W' : 'w';
        case KEY_X: return shift ? 'X' : 'x';
        case KEY_Y: return shift ? 'Y' : 'y';
        case KEY_Z: return shift ? 'Z' : 'z';
    }
    if (!shift) {
        switch (code) {
            case KEY_1: return '1'; case KEY_2: return '2';
            case KEY_3: return '3'; case KEY_4: return '4';
            case KEY_5: return '5'; case KEY_6: return '6';
            case KEY_7: return '7'; case KEY_8: return '8';
            case KEY_9: return '9'; case KEY_0: return '0';
            case KEY_MINUS: return '-';
            case KEY_EQUAL: return '=';
            case KEY_LEFTBRACE: return '[';
            case KEY_RIGHTBRACE: return ']';
            case KEY_SEMICOLON: return ';';
            case KEY_APOSTROPHE: return '\'';
            case KEY_GRAVE: return '`';
            case KEY_BACKSLASH: return '\\';
            case KEY_COMMA: return ',';
            case KEY_DOT: return '.';
            case KEY_SLASH: return '/';
        }
    } else {
        switch (code) {
            case KEY_1: return '!'; case KEY_2: return '@';
            case KEY_3: return '#'; case KEY_4: return '$';
            case KEY_5: return '%'; case KEY_6: return '^';
            case KEY_7: return '&'; case KEY_8: return '*';
            case KEY_9: return '('; case KEY_0: return ')';
            case KEY_MINUS: return '_';
            case KEY_EQUAL: return '+';
            case KEY_LEFTBRACE: return '{';
            case KEY_RIGHTBRACE: return '}';
            case KEY_SEMICOLON: return ':';
            case KEY_APOSTROPHE: return '"';
            case KEY_GRAVE: return '~';
            case KEY_BACKSLASH: return '|';
            case KEY_COMMA: return '<';
            case KEY_DOT: return '>';
            case KEY_SLASH: return '?';
        }
    }
    switch (code) {
        case KEY_SPACE: return ' ';
        case KEY_ENTER: return '\n';
        case KEY_BACKSPACE: return '\b';
        case KEY_TAB: return '\t';
        case KEY_UP:     return K_UP;
        case KEY_DOWN:   return K_DOWN;
        case KEY_LEFT:   return K_LEFT;
        case KEY_RIGHT:  return K_RIGHT;
        case KEY_HOME:   return K_HOME;
        case KEY_END:    return K_END;
        case KEY_DELETE: return K_DEL;
    }
    return 0;
}

static void emit_cp(uint32_t cp) {
    if (!g_handler) return;
    if (cp < 0x80) { g_handler((char)cp); return; }
    if (cp < 0x800) {
        g_handler((char)(0xC0 | (cp >> 6)));
        g_handler((char)(0x80 | (cp & 0x3F)));
        return;
    }
    if (cp < 0x10000) {
        g_handler((char)(0xE0 | (cp >> 12)));
        g_handler((char)(0x80 | ((cp >> 6) & 0x3F)));
        g_handler((char)(0x80 | (cp & 0x3F)));
        return;
    }
    g_handler((char)(0xF0 | (cp >> 18)));
    g_handler((char)(0x80 | ((cp >> 12) & 0x3F)));
    g_handler((char)(0x80 | ((cp >> 6) & 0x3F)));
    g_handler((char)(0x80 | (cp & 0x3F)));
}

int kbd_init(uint64_t ecam_base) {
    g_ready = 0;
    int b, d, f;
    if (!pci_find_device(ecam_base, 0x1AF4, 0x1052, &b, &d, &f)) return 0;

    uint32_t bar4 = pci_cfg_read32(ecam_base, b, d, f, 0x10 + 4 * 4);
    if ((bar4 & ~0xFull) == 0) {
        pci_assign_bar64(ecam_base, b, d, f, 4, 0x10100000ull);
        dsb_sy();
    }
    if (!pci_parse_virtio_caps(ecam_base, b, d, f, &g_io)) return 0;

    virtio_state_t st;
    virtio_transport_init(&g_io, &st);
    if (!virtio_set_features(&g_io, VIRTIO_F_VERSION_1, &st)) return 0;
    if (!virtio_setup_queue(&g_io, 0, &g_evq)) return 0;
    if (!virtio_activate(&g_io, &st)) return 0;

    g_event_bufs = (uint8_t *)phys_alloc_page();
    if (!g_event_bufs) return 0;

    for (int i = 0; i < g_evq.queue_size; i++) {
        g_evq.desc[i].addr  = KVA_TO_PA(g_event_bufs + i * 8);
        g_evq.desc[i].len   = 8;
        g_evq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_evq.desc[i].next  = 0;
    }
    {
        uint64_t a = (uint64_t)g_evq.desc & ~63ull;
        uint64_t e = ((uint64_t)g_evq.desc + sizeof(virtq_desc_t) * g_evq.queue_size + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
    }
    for (int i = 0; i < g_evq.queue_size; i++) g_evq.avail->ring[i] = (uint16_t)i;
    g_evq.avail->idx = g_evq.queue_size;
    {
        uint64_t a = (uint64_t)g_evq.avail & ~63ull;
        uint64_t e = ((uint64_t)g_evq.avail + sizeof(virtq_avail_t) + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
    }
    dsb_sy();
    virtio_queue_notify(&g_io, &g_evq);

    g_last_used = 0;
    g_ready = 1;
    return 1;
}

void kbd_poll(void) {
    if (!g_ready) return;

    {
        uint64_t a = (uint64_t)g_evq.used & ~63ull;
        uint64_t e = ((uint64_t)g_evq.used + sizeof(virtq_used_t) + 63) & ~63ull;
        while (a < e) { __asm__ volatile("dc ivac, %0" :: "r"(a)); a += 64; }
    }
    dsb_sy();
    uint16_t cur = g_evq.used->idx;

    while (g_last_used != cur) {
        uint32_t id  = g_evq.used->ring[g_last_used % g_evq.queue_size].id;
        uint32_t len = g_evq.used->ring[g_last_used % g_evq.queue_size].len;

        if (id < g_evq.queue_size && len == 8) {
            uint8_t *evp = g_event_bufs + id * 8;
            uint64_t a = (uint64_t)evp & ~63ull;
            __asm__ volatile("dc ivac, %0" :: "r"(a));
            dsb_sy();
            virtio_input_event_t *ev = (virtio_input_event_t *)evp;

            if (ev->type == EV_KEY) {
                int down = (ev->value != 0);
                uint16_t code = ev->code;

                if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                    if (down) {
                        g_shift = 1;
                        if (g_alt && !g_alt_shift_toggled) {
                            g_ru = !g_ru;
                            g_alt_shift_toggled = 1;
                        }
                    } else {
                        g_shift = 0;
                        g_alt_shift_toggled = 0;
                    }
                    goto next_ev;
                }
                if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
                    g_ctrl = down ? 1 : 0;
                    goto next_ev;
                }
                if (code == KEY_LEFTMETA || code == KEY_RIGHTMETA) {
                    g_super = down ? 1 : 0;
                    goto next_ev;
                }
                if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
                    if (down) {
                        g_alt = 1;
                        if (g_shift && !g_alt_shift_toggled) {
                            g_ru = !g_ru;
                            g_alt_shift_toggled = 1;
                        }
                    } else {
                        g_alt = 0;
                        g_alt_shift_toggled = 0;
                    }
                    goto next_ev;
                }

                if (!down) goto next_ev;

                if (code == KEY_F12) {
                    g_ru = !g_ru;
                    goto next_ev;
                }

                if (g_ctrl || g_super) {
                    if (code == KEY_C) { if (g_handler) g_handler(K_CTRL_C); goto next_ev; }
                    if (code == KEY_V) { if (g_handler) g_handler(K_CTRL_V); goto next_ev; }
                    if (code == KEY_S) { if (g_handler) g_handler(K_CTRL_S); goto next_ev; }
                    if (code == KEY_Q) { if (g_handler) g_handler(K_CTRL_Q); goto next_ev; }
                    goto next_ev;
                }

                if (g_ru) {
                    uint32_t cp = ru_lower(code);
                    if (cp) {
                        if (g_shift) cp -= 0x20;
                        emit_cp(cp);
                        goto next_ev;
                    }
                }
                uint32_t cp = en_char(code, g_shift);
                if (cp) emit_cp(cp);
            }
        }
next_ev:
        g_evq.avail->ring[g_evq.avail->idx % g_evq.queue_size] = (uint16_t)id;
        dsb_sy();
        g_evq.avail->idx++;
        {
            uint64_t a = (uint64_t)g_evq.avail & ~63ull;
            uint64_t e = ((uint64_t)g_evq.avail + sizeof(virtq_avail_t) + 63) & ~63ull;
            while (a < e) { __asm__ volatile("dc cvac, %0" :: "r"(a)); a += 64; }
        }
        dsb_sy();
        g_last_used++;
    }
    if (cur != 0) virtio_queue_notify(&g_io, &g_evq);
}