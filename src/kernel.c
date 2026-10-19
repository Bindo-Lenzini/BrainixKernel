#include "version.h"
#include "protosw.h"
#include "vnc.h"

// BrainixKernel (BKOS) — kernel.c
// B3: ELF-загрузка user-программ.

#include <stdint.h>
#include "physmem.h"
#include "heap.h"
#include "dtb.h"
#include "task.h"
#include "sched.h"
#include "syscall.h"
#include "gpu.h"
#include "fb.h"
#include "kbd.h"
#include "mouse.h"
#include "win.h"
#include "initrd.h"
#include "ksh.h"
#include "blk.h"
#include "bkfs.h"
#include "editor.h"
#include "wm.h"
#include "icons.h"
#include "files.h"
#include "startmenu.h"
#include "rtc.h"
#include "wallpaper.h"
#include "desktop.h"
#include "personalize.h"
#include "about.h"
#include "backdrop.h"
#include "virtio_net.h"
#include "net.h"
#include "arp.h"
#include "dhcp.h"
#include "sha256.h"
#include "icmp.h"
#include "mmu.h"
#include "elf.h"

extern volatile int g_in_syscall;

static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void)    { __asm__ volatile("isb"    ::: "memory"); }

#define UART0_BASE   (0x09000000ULL + KERNEL_VA_OFFSET)
#define UART0_DR     ((volatile uint32_t *)(UART0_BASE + 0x00))
#define UART0_FR     ((volatile uint32_t *)(UART0_BASE + 0x18))
#define UART0_IMSC   ((volatile uint32_t *)(UART0_BASE + 0x38))
#define UART0_ICR    ((volatile uint32_t *)(UART0_BASE + 0x44))

#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)
#define UART_RXIM    (1u << 4)
#define UART_RTIM    (1u << 6)
#define UART_RXIC    (1u << 4)
#define UART_RTIC    (1u << 6)
#define UART_IRQ     33

void uart_putc(char c) { while (*UART0_FR & UART_FR_TXFF) {} *UART0_DR = (uint32_t)c; }
void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
void uart_puthex(uint64_t v) {
    static const char hex[] = "059016789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) uart_putc(hex[(v >> (uint64_t)i) & 0xF]);
}
void uart_putu(uint64_t v) {
    char buf[24]; int i = 23; buf[i--] = '\0';
    if (v == 0) buf[i--] = '0';
    while (v) { buf[i--] = (char)('0' + (v % 10)); v /= 10; }
    uart_puts(&buf[i + 1]);
}

#define RX_BUF_SIZE 256
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;
static void rx_push(uint8_t b) {
    uint32_t next = (rx_head + 1) % RX_BUF_SIZE;
    if (next == rx_tail) return;
    rx_buf[rx_head] = b; rx_head = next;
}
int uart_rx_pop(uint8_t *out) {
    if (rx_head == rx_tail) return 0;
    *out = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return 1;
}
static void uart_drain_rx(void) {
    while (!(*UART0_FR & UART_FR_RXFE)) {
        uint8_t b = (uint8_t)(*UART0_DR & 0xFF);
        rx_push(b);
    }
}
static void uart_init_rx(void) { *UART0_IMSC = UART_RXIM | UART_RTIM; dsb_sy(); }
void panic(const char *msg) {
    uart_puts("\n[panic] "); uart_puts(msg); uart_puts("\n");
    for (;;) __asm__ volatile("wfe");
}

#define GICD_BASE   (0x08000000ULL + KERNEL_VA_OFFSET)
#define GICC_BASE   (0x08010000ULL + KERNEL_VA_OFFSET)
#define GICD_CTLR          ((volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n)  ((volatile uint32_t *)(GICD_BASE + 0x100 + 4*(n)))
#define GICC_CTLR          ((volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR           ((volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR           ((volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR          ((volatile uint32_t *)(GICC_BASE + 0x010))
#define GIC_ENABLE   1u
#define GIC_PMR_ALL  0xFFu
#define GIC_IAR_SPURIOUS_MIN 1020u

static void gic_init(void) {
    *GICD_CTLR = 0; dsb_sy();
    *GICD_ISENABLER(0) = (1u << 30);
    *GICD_ISENABLER(1) = (1u << (UART_IRQ - 32));
    dsb_sy();
    *GICD_CTLR = GIC_ENABLE; dsb_sy();
    *GICC_PMR = GIC_PMR_ALL; dsb_sy();
    *GICC_CTLR = GIC_ENABLE; dsb_sy();
}

#define TIMER_IRQ          30
#define TIMER_HZ_DEFAULT   24000000ull
static uint64_t timer_hz = TIMER_HZ_DEFAULT;
static void timer_set(uint64_t ticks_from_now) {
    uint64_t now;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(now + ticks_from_now));
    uint64_t ctl = 1;
    __asm__ volatile("msr cntp_ctl_el0, %x0" :: "r"(ctl));
    isb();
}
static void timer_init(void) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq != 0) timer_hz = freq;
    timer_set(timer_hz / 60);
}


// ============================================================
// Состояние
// ============================================================

static win_t      g_term;
static editor_t   g_edit;
static files_t    g_files;
static pers_t     g_pers;

static int        g_win_ksh_id    = -1;
static int        g_win_edit_id   = -1;
static int        g_win_files_id  = -1;
static int        g_win_pers_id   = -1;
static int        g_win_about_id  = -1;

static int        g_mouse_x = 640;
static int        g_mouse_y = 400;
static uint32_t   g_saved_bg[MOUSE_W * FB_SCALE * MOUSE_H * FB_SCALE];
static int        g_cursor_drawn = 0;
static int        g_ui_ready = 0;

static int        g_drag_active = 0;
static int        g_drag_win_id = -1;
static int        g_drag_off_x = 0;
static int        g_drag_off_y = 0;

#define SAVE_W 1280
#define SAVE_H 800
static uint32_t  *g_save_under = 0;
static int        g_su_x = 0, g_su_y = 0, g_su_w = 0, g_su_h = 0;
static int        g_su_valid = 0;

static int        g_resize_active = 0;
static int        g_resize_win_id = -1;
static int        g_resize_start_mx = 0;
static int        g_resize_start_my = 0;
static int        g_resize_start_w  = 0;
static int        g_resize_start_h  = 0;

static volatile int  g_edit_request = 0;
static char          g_edit_request_name[64];
static volatile int  g_pers_request = 0;

static uint32_t g_last_click_tick = 0;
static int      g_last_click_icon = -1;
static volatile int g_full_redraw_request = 0;

void request_full_redraw(void) { g_full_redraw_request = 1; }
int ksh_request_edit(const char *name) {
    if (!bkfs_is_ready()) return 0;
    int i = 0;
    while (name[i] && i < 63) { g_edit_request_name[i] = name[i]; i++; }
    g_edit_request_name[i] = 0;
    g_edit_request = 1;
    return 1;
}
int ksh_request_personalize(void) { g_pers_request = 1; return 1; }

// ---- Dock ----
#define DOCK_H        64
#define DOCK_W        280
#define DOCK_X        ((1280 - DOCK_W) / 2)
#define DOCK_Y        (800 - DOCK_H - 12)
#define ICON_CELL     48
#define ICON_PAD      16
#define ICON_GAP      14

static const int g_cell_x[4] = {
    DOCK_X + ICON_PAD + (ICON_CELL + ICON_GAP) * 0,
    DOCK_X + ICON_PAD + (ICON_CELL + ICON_GAP) * 1,
    DOCK_X + ICON_PAD + (ICON_CELL + ICON_GAP) * 2,
    DOCK_X + ICON_PAD + (ICON_CELL + ICON_GAP) * 3,
};

static const uint8_t cursor_pattern[MOUSE_H][MOUSE_W] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,1,1,0,0,0,0},
    {1,1,1,1,1,1,1,1,1,0,0,0},
    {1,1,1,1,1,1,1,1,1,1,0,0},
    {1,1,1,1,1,1,1,1,1,1,1,0},
    {1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,0,0,0,0,0,0},
    {1,1,1,0,1,1,1,0,0,0,0,0},
    {1,1,0,0,0,1,1,1,0,0,0,0},
    {1,0,0,0,0,0,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,1,1,1,0,0},
    {0,0,0,0,0,0,0,0,1,1,1,0},
    {0,0,0,0,0,0,0,0,0,1,0,0},
};

static void cursor_hide(void) {
    if (!g_cursor_drawn) return;
    fb_put_pixels(g_mouse_x, g_mouse_y, MOUSE_W, MOUSE_H, g_saved_bg);
    g_cursor_drawn = 0;
}
static void cursor_show(void) {
    if (g_cursor_drawn) return;
    fb_get_pixels(g_mouse_x, g_mouse_y, MOUSE_W, MOUSE_H, g_saved_bg);
    for (int j = 0; j < MOUSE_H; j++) {
        for (int i = 0; i < MOUSE_W; i++) {
            if (!cursor_pattern[j][i]) continue;
            uint32_t col = 0xFFFFFFFF;
            if (i == 0 || j == 0 || i == MOUSE_W - 1 ||
                cursor_pattern[j][i-1] == 0 || cursor_pattern[j][i+1] == 0)
                col = 0xFF000000;
            fb_putpixel(g_mouse_x + i, g_mouse_y + j, col);
        }
    }
    g_cursor_drawn = 1;
}

static void su_save(int x, int y, int w, int h) {
    if (!g_save_under) { g_su_valid = 0; return; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 1280) w = 1280 - x;
    if (y + h > 800)  h = 800  - y;
    if (w <= 0 || h <= 0) { g_su_valid = 0; return; }
    for (int j = 0; j < h; j++) {
        uint32_t *dst = g_save_under + (uint64_t)j * SAVE_W;
        for (int i = 0; i < w; i++)
            dst[i] = fb_getpixel(x + i, y + j);
    }
    g_su_x = x; g_su_y = y; g_su_w = w; g_su_h = h;
    g_su_valid = 1;
}
static void su_restore(void) {
    if (!g_su_valid) return;
    for (int j = 0; j < g_su_h; j++) {
        const uint32_t *src = g_save_under + (uint64_t)j * SAVE_W;
        int py = g_su_y + j;
        for (int i = 0; i < g_su_w; i++)
            fb_putpixel(g_su_x + i, py, src[i]);
    }
    g_su_valid = 0;
}

static void draw_content(wnd_type_t type, int cx, int cy, int cw, int ch) {
    if (type == WND_KSH)              win_draw_content(&g_term, cx, cy, cw, ch);
    else if (type == WND_EDITOR)      editor_draw_content(&g_edit, cx, cy, cw, ch);
    else if (type == WND_FILES)       files_draw_content(&g_files, cx, cy, cw, ch);
    else if (type == WND_PERSONALIZE) pers_draw_content(&g_pers, cx, cy, cw, ch);
    else if (type == WND_ABOUT)       about_draw_content(cx, cy, cw, ch);
}

static void draw_desktop(void);
static void full_redraw(void) {
    draw_desktop();
    wm_draw_all(draw_content);
    startmenu_draw(g_mouse_x, g_mouse_y);
}

static void ui_handle_mouse(void) {
    int rx = mouse_x();
    int ry = mouse_y();
    int nx = rx * 1280 / 32767;
    int ny = ry * 800 / 32767;
    if (nx < 0) nx = 0; if (nx > 1280 - MOUSE_W) nx = 1280 - MOUSE_W;
    if (ny < 0) ny = 0; if (ny > 800 - MOUSE_H) ny = 800 - MOUSE_H;
    if (nx == g_mouse_x && ny == g_mouse_y) return;

    if (g_resize_active && g_resize_win_id >= 0) {
        int dx = nx - g_resize_start_mx;
        int dy = ny - g_resize_start_my;
        cursor_hide();
        su_restore();
        wnd_t *w = wm_get(g_resize_win_id);
        if (w) {
            wm_set_size(g_resize_win_id, g_resize_start_w + dx, g_resize_start_h + dy);
            int margin = 16;
            su_save(w->x - margin, w->y - margin, w->w + 2*margin, w->h + 2*margin);
            wm_draw_window_only(g_resize_win_id, draw_content);
        }
        g_mouse_x = nx; g_mouse_y = ny;
        cursor_show(); fb_flush_dirty();
        return;
    }
    if (g_drag_active && g_drag_win_id >= 0) {
        wnd_t *w = wm_get(g_drag_win_id);
        if (w) {
            int new_x = nx - g_drag_off_x;
            int new_y = ny - g_drag_off_y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x > 1280 - w->w) new_x = 1280 - w->w;
            if (new_y > 800  - w->h) new_y = 800  - w->h;
            if (new_x == w->x && new_y == w->y) return;
            cursor_hide();
            su_restore();
            w->x = new_x; w->y = new_y;
            int margin = 16;
            su_save(w->x - margin, w->y - margin, w->w + 2*margin, w->h + 2*margin);
            wm_draw_window_only(g_drag_win_id, draw_content);
        }
        g_mouse_x = nx; g_mouse_y = ny;
        cursor_show(); fb_flush_dirty();
        return;
    }

    cursor_hide();
    g_mouse_x = nx; g_mouse_y = ny;
    cursor_show();
    if (startmenu_is_open()) { full_redraw(); cursor_show(); }
}

void kwin_cmd_help(win_t *w) {
    win_puts(w, "ksh (KernelShell) commands:");
    win_puts(w, "  help / ver / tasks / mem / sysinfo");
    win_puts(w, "  disk / ls / cat <file> / motd");
    win_puts(w, "  mkfs / lsw / touch / write / rm");
    win_puts(w, "  edit <file> - open in editor");
    win_puts(w, "  wallpaper [preset|image]");
    win_puts(w, "  images - photos - personalize");
}
void kwin_cmd_ver(win_t *w) {
    win_puts(w, "BrainixKernel OS v" BKOS_VERSION);
    win_puts(w, "ELF loader + per-process AS");
}
void kwin_cmd_tasks(win_t *w) {
    win_puts(w, "kernel: A, B | user: U1, U2, U-fault, U-ELF");
    win_puts(w, "scheduler: round-robin, 60 Hz");
}
void kwin_cmd_mem(win_t *w) {
    char buf[64];
    uint64_t free_p = physmem_free_pages();
    uint64_t total  = physmem_total_pages();
    int i = 0;
    const char *pre = "free pages: ";
    for (int j = 0; pre[j]; j++) buf[i++] = pre[j];
    char tmp[24]; int t = 0;
    if (free_p == 0) tmp[t++] = '0';
    while (free_p) { tmp[t++] = (char)('0' + (free_p % 10)); free_p /= 10; }
    while (t > 0) buf[i++] = tmp[--t];
    buf[i] = 0; win_puts(w, buf);

    i = 0; pre = "total pages: ";
    for (int j = 0; pre[j]; j++) buf[i++] = pre[j];
    t = 0;
    if (total == 0) tmp[t++] = '0';
    while (total) { tmp[t++] = (char)('0' + (total % 10)); total /= 10; }
    while (t > 0) buf[i++] = tmp[--t];
    buf[i] = 0; win_puts(w, buf);
}

extern char user_cat_start[];
extern char user_cat_end[];
extern char user_echo_start[];
extern char user_echo_end[];

int ksh_launch_program(const char *cmdline) {
    if (!cmdline || !cmdline[0]) return 0;

    #define MAX_ARGS 16
    const char *argv_ptrs[MAX_ARGS];
    char argv_bufs[MAX_ARGS][64];
    int argc = 0;

    const char *p = cmdline;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        int n = 0;
        while (*p && *p != ' ' && *p != '\t' && n < 63) {
            argv_bufs[argc][n++] = *p++;
        }
        argv_bufs[argc][n] = 0;
        argv_ptrs[argc] = argv_bufs[argc];
        argc++;
    }

    if (argc == 0) return 0;

    const char *name = argv_bufs[0];
    const initrd_file_t *f = initrd_find(name);
    if (!f) return 0;

    const char *args[MAX_ARGS];
    for (int i = 0; i < argc; i++) args[i] = argv_ptrs[i];

    task_t *t = task_create_from_elf_ex(name, f->data, f->size, argc, args);
    if (!t) return 0;
    t->to_window = 1;
    sched_add(t);
    return 1;
}

void win_ksh_pump(win_t *w) {
    char line[WIN_LINE_MAX];
    int any = 0;
    while (ksh_out_pop_line(line, WIN_LINE_MAX)) {
        win_puts(w, line); any = 1;
    }
    if (any) full_redraw();
}

static void open_files_window(void) {
    if (g_win_files_id < 0) {
        files_init(&g_files);
        g_win_files_id = wm_create(WND_FILES, 500, 180, 560, 420, "Files - BKFS");
    } else {
        files_refresh(&g_files);
        wm_show(g_win_files_id, 1);
    }
    wm_raise(g_win_files_id);
}
static void open_editor(const char *name) {
    editor_open(&g_edit, name);
    if (g_win_edit_id < 0)
        g_win_edit_id = wm_create(WND_EDITOR, 700, 90, 520, 620, "Editor");
    wm_show(g_win_edit_id, 1);
    wm_raise(g_win_edit_id);
    char t[40]; int i = 0;
    const char *pre = "Editor - ";
    for (int j = 0; pre[j] && i < 30; j++) t[i++] = pre[j];
    for (int j = 0; name[j] && i < 39; j++) t[i++] = name[j];
    t[i] = 0;
    wm_set_title(g_win_edit_id, t);
}
static void open_personalize_window(void) {
    pers_close(&g_pers); pers_init(&g_pers);
    if (g_win_pers_id < 0)
        g_win_pers_id = wm_create(WND_PERSONALIZE, 400, 100, 760, 580, "Personalization");
    wm_show(g_win_pers_id, 1);
    wm_raise(g_win_pers_id);
}
static void open_about_window(void) {
    if (g_win_about_id < 0)
        g_win_about_id = wm_create(WND_ABOUT, 340, 60, 600, 680, "About This System");
    wm_show(g_win_about_id, 1);
    wm_raise(g_win_about_id);
}

static void open_ksh_with_sysinfo(void) {
    if (g_win_ksh_id < 0) return;
    const char *cmd = "sysinfo";
    for (int i = 0; cmd[i]; i++) win_key(&g_term, cmd[i]);
    win_key(&g_term, '\n');
    wm_show(g_win_ksh_id, 1);
    wm_raise(g_win_ksh_id);
}

static int dock_hit(int x, int y) {
    if (y < DOCK_Y || y >= DOCK_Y + DOCK_H) return -1;
    for (int i = 0; i < 4; i++) {
        int cx = g_cell_x[i];
        if (x >= cx && x < cx + ICON_CELL) return i;
    }
    return -1;
}
static void handle_dock_click(int cell) {
    switch (cell) {
        case 0: startmenu_toggle(); break;
        case 1: if (g_win_ksh_id >= 0) { wm_show(g_win_ksh_id, 1); wm_raise(g_win_ksh_id); } break;
        case 2: open_files_window(); break;
        case 3: open_editor("untitled.txt"); break;
    }
}
static void handle_startmenu_click(int item) {
    startmenu_close();
    switch (item) {
        case STARTMENU_ITEM_SHUTDOWN: system_shutdown(); break;
        case STARTMENU_ITEM_REBOOT:   system_reboot();   break;
        case STARTMENU_ITEM_SLEEP:    uart_puts("[power] sleep\n"); break;
        case STARTMENU_ITEM_HIBER:    uart_puts("[power] hibernate\n"); break;
    }
}
static void desktop_double_click(int idx) {
    switch (idx) {
        case DI_COMPUTER:    open_about_window(); break;
        case DI_RECYCLE:
            win_puts(&g_term, "[Recycle Bin] empty");
            if (g_win_ksh_id >= 0) wm_raise(g_win_ksh_id);
            break;
        case DI_TERMINAL:
            if (g_win_ksh_id >= 0) { wm_show(g_win_ksh_id, 1); wm_raise(g_win_ksh_id); }
            break;
        case DI_FILES:       open_files_window(); break;
        case DI_EDITOR:      open_editor("untitled.txt"); break;
        case DI_PERSONALIZE: open_personalize_window(); break;
    }
}

static uint32_t tick_count = 0;

uint64_t irq_handler(uint64_t sp) {
    uint32_t iar = *GICC_IAR;
    uint32_t irq = iar & 0x3FF;
    if (irq >= GIC_IAR_SPURIOUS_MIN) return sp;
    int do_switch = 0;

    if (irq == UART_IRQ) {
        uart_drain_rx();
        *UART0_ICR = UART_RXIC | UART_RTIC;
        dsb_sy();
        sched_wake_blocked();
    }

    if (irq == TIMER_IRQ) {
        uart_drain_rx();
        timer_set(timer_hz / 60);
        tick_count++;
        kbd_poll();
        mouse_poll();
        net_poll();
        // vnc_poll();


        if (g_ui_ready) {
            if (mouse_left_pressed()) {
                int cx = g_mouse_x, cy = g_mouse_y;
                int sm_item = startmenu_hit(cx, cy);
                if (sm_item >= 0) {
                    handle_startmenu_click(sm_item);
                    full_redraw(); cursor_show();
                } else {
                    if (startmenu_is_open()) startmenu_close();
                    int close_id  = wm_hit_close(cx, cy);
                    int max_id    = wm_hit_maximize(cx, cy);
                    int min_id    = wm_hit_minimize(cx, cy);
                    int resize_id = wm_hit_resize(cx, cy);
                    int dock_cell = dock_hit(cx, cy);

                    if (close_id >= 0) {
                        wnd_t *w = wm_get(close_id);
                        if (w) {
                            if (w->type == WND_EDITOR) { editor_close(&g_edit); wm_show(close_id, 0); }
                            else if (w->type == WND_FILES) wm_show(close_id, 0);
                            else if (w->type == WND_PERSONALIZE) { pers_close(&g_pers); wm_show(close_id, 0); }
                            else wm_show(close_id, 0);
                        }
                        full_redraw(); cursor_show();
                    } else if (max_id >= 0) {
                        wm_raise(max_id); wm_maximize_toggle(max_id);
                        full_redraw(); cursor_show();
                    } else if (min_id >= 0) {
                        wm_minimize(min_id);
                        if (g_win_ksh_id >= 0 && g_win_ksh_id != min_id)
                            wm_raise(g_win_ksh_id);
                        full_redraw(); cursor_show();
                    } else if (resize_id >= 0) {
                        wm_raise(resize_id);
                        wnd_t *w = wm_get(resize_id);
                        if (w) {
                            g_resize_active = 1;
                            g_resize_win_id = resize_id;
                            g_resize_start_mx = cx; g_resize_start_my = cy;
                            g_resize_start_w  = w->w; g_resize_start_h = w->h;
                            int margin = 16;
                            su_save(w->x - margin, w->y - margin,
                                    w->w + 2*margin, w->h + 2*margin);
                        }
                        full_redraw(); cursor_show();
                    } else if (dock_cell >= 0) {
                        handle_dock_click(dock_cell);
                        full_redraw(); cursor_show();
                    } else {
                        int title_id   = wm_hit_title(cx, cy);
                        int content_id = wm_hit_content(cx, cy);
                        int body_id    = wm_hit_test(cx, cy);
                        int di         = desktop_hit(cx, cy);

                        if (title_id >= 0) {
                            wm_raise(title_id);
                            wnd_t *w = wm_get(title_id);
                            if (w) {
                                g_drag_active = 1;
                                g_drag_win_id = title_id;
                                g_drag_off_x = cx - w->x;
                                g_drag_off_y = cy - w->y;
                                int margin = 16;
                                su_save(w->x - margin, w->y - margin,
                                        w->w + 2*margin, w->h + 2*margin);
                            }
                            full_redraw(); cursor_show();
                        } else if (content_id >= 0) {
                            wnd_t *w = wm_get(content_id);
                            wm_raise(content_id);

                            if (w->type == WND_ABOUT) {
                                int ccx = w->x + 4, ccy = w->y + 32;
                                int ccw = w->w - 8, cch = w->h - 40;
                                if (about_hit_report(cx, cy, ccx, ccy, ccw, cch)) {
                                    open_ksh_with_sysinfo();
                                }
                            } else {
                                int ccx = w->x + 4, ccy = w->y + 32;
                                int ccw = w->w - 8, cch = w->h - 40;
                                if (w->type == WND_PERSONALIZE)
                                    pers_click(&g_pers, cx, cy, ccx, ccy, ccw, cch);
                                else if (w->type == WND_EDITOR)
                                    editor_click(&g_edit, cx, cy, ccx, ccy, ccw, cch);
                                else if (w->type == WND_FILES)
                                    files_click(&g_files, cx, cy, ccx, ccy, ccw, cch);
                            }
                            full_redraw(); cursor_show();
                        } else if (body_id >= 0) {
                            wm_raise(body_id); full_redraw(); cursor_show();
                        } else if (di >= 0) {
                            int prev_icon = g_last_click_icon;
                            uint32_t prev_tick = g_last_click_tick;
                            desktop_select(di);
                            if (prev_icon == di && (tick_count - prev_tick) < 25) {
                                desktop_double_click(di);
                                g_last_click_icon = -1;
                            } else {
                                g_last_click_icon = di;
                                g_last_click_tick = tick_count;
                            }
                            full_redraw(); cursor_show();
                        } else {
                            if (desktop_selected() >= 0) {
                                desktop_clear_selection();
                                full_redraw(); cursor_show();
                            }
                        }
                    }
                }
            }

            if (!mouse_left_down()) {
                g_drag_active = 0; g_drag_win_id = -1;
                g_resize_active = 0; g_resize_win_id = -1;
                g_su_valid = 0;
            }
            ui_handle_mouse();

            if (g_edit_request) {
                g_edit_request = 0;
                open_editor(g_edit_request_name);
                full_redraw(); cursor_show();
            }
            if (g_pers_request) {
                g_pers_request = 0;
                open_personalize_window();
                full_redraw(); cursor_show();
            }
            win_ksh_pump(&g_term);
            if (g_full_redraw_request) {
                g_full_redraw_request = 0;
                full_redraw(); cursor_show();
            }
            if ((tick_count % 30) == 0) {
                g_term.cursor_visible = !g_term.cursor_visible;
                g_edit.cursor_visible = !g_edit.cursor_visible;
                g_pers.cursor_visible = !g_pers.cursor_visible;
                full_redraw(); cursor_show();
            }
            fb_flush_dirty();
        }
        // Переключаем задачу только если мы НЕ в syscall.
        // Иначе frame-вложенность сломается (см. exception.c).
        if (!g_in_syscall) {
            do_switch = 1;
        }
    }
    *GICC_EOIR = iar;
    dsb_sy();
    if (do_switch) return sched_yield_from_frame(sp);
    return sp;
}


static void yield_kernel(void) {
    __asm__ volatile("svc #0");
}

static void task_printer_a(void) {
    for (;;) yield_kernel();
}

static void task_printer_b(void) {
    for (;;) { uart_puts("[B] alive\n"); for (volatile int i = 0; i < 5000000; i++) {} }
}

extern char user_print_u_start[];
extern char user_print_u_end[];
extern char user_fault_start[];
extern char user_fault_end[];

static void draw_dock_cell(int x, int y, int selected, int hover, int which) {
    if (selected) {
        fb_blend_round_rect_aa(x - 2, y - 2, ICON_CELL + 4, ICON_CELL + 4,
                               10, 0xFF4A90E2, 190);
    } else if (hover) {
        fb_blend_round_rect_aa(x - 2, y - 2, ICON_CELL + 4, ICON_CELL + 4,
                               10, 0xFF4A90E2, 100);
    }
    switch (which) {
        case 0: icon_draw_brainix(x, y, selected); break;
        case 1: icon_draw_ksh(x, y, selected);     break;
        case 2: icon_draw_files(x, y, selected);   break;
        case 3: icon_draw_editor(x, y, selected);  break;
    }
}

static void draw_top_bar(void) {
    backdrop_draw_frosted_rect(0, 0, 1280, 52, 0xFF1A2D48, 170);
    fb_blend_rect(0, 0, 1280, 1, 0xFFFFFFFF, 60);
    fb_draw_string_aa(16, 18, "BrainixKernel", 0xFFF0F0F0, 0);
    fb_draw_string_aa(156, 18, "OS", 0xFF7AB8FF, 0);
    fb_draw_string_aa(200, 18, "v" BKOS_VERSION, 0xFFAAAAAA, 0);
    {
        char buf[16];
        rtc_format_full_utc3(buf, sizeof(buf));
        fb_draw_string_aa(1104, 18, buf, 0xFFE0E0E0, 0);
    }
    fb_blend_rect(0, 51, 1280, 1, 0x00000000, 80);
}

static void draw_dock(void) {
    fb_darken_rect(DOCK_X - 6, DOCK_Y - 2, DOCK_W + 12, DOCK_H + 10, 200);
    fb_darken_rect(DOCK_X - 3, DOCK_Y - 1, DOCK_W + 6, DOCK_H + 6, 170);
    backdrop_draw_frosted(DOCK_X, DOCK_Y, DOCK_W, DOCK_H, 16,
                          0xFF0A1828, 190);
    fb_blend_round_rect_aa(DOCK_X + 4, DOCK_Y + 1, DOCK_W - 8, 1, 1,
                           0xFFFFFFFF, 90);

    int hover = dock_hit(g_mouse_x, g_mouse_y);
    wnd_t *foc = wm_focused();
    int ksh_sel   = (foc && foc->type == WND_KSH) ? 1 : 0;
    int files_sel = (foc && foc->type == WND_FILES) ? 1 : 0;
    int edit_sel  = (foc && foc->type == WND_EDITOR && editor_is_open(&g_edit)) ? 1 : 0;

    int ksh_min   = wm_is_minimized_any(WND_KSH);
    int files_min = wm_is_minimized_any(WND_FILES);
    int edit_min  = wm_is_minimized_any(WND_EDITOR);

    int y_offset[4] = { 0, 0, 0, 0 };
    if (hover >= 0 && hover < 4) y_offset[hover] = -6;
    int base_y = DOCK_Y + 8;

    draw_dock_cell(g_cell_x[0], base_y + y_offset[0], startmenu_is_open(), hover == 0, 0);
    draw_dock_cell(g_cell_x[1], base_y + y_offset[1], ksh_sel,   hover == 1, 1);
    draw_dock_cell(g_cell_x[2], base_y + y_offset[2], files_sel, hover == 2, 2);
    draw_dock_cell(g_cell_x[3], base_y + y_offset[3], edit_sel,  hover == 3, 3);

    if (ksh_min) { int cx = g_cell_x[1] + ICON_CELL / 2; fb_fill_circle_aa(cx, DOCK_Y + DOCK_H - 5, 3, 0xFFFFBD2E); }
    if (files_min) { int cx = g_cell_x[2] + ICON_CELL / 2; fb_fill_circle_aa(cx, DOCK_Y + DOCK_H - 5, 3, 0xFFFFBD2E); }
    if (edit_min) { int cx = g_cell_x[3] + ICON_CELL / 2; fb_fill_circle_aa(cx, DOCK_Y + DOCK_H - 5, 3, 0xFFFFBD2E); }
}

static void draw_desktop(void) {
    wallpaper_draw();
    backdrop_capture();
    desktop_draw();
    draw_top_bar();
    draw_dock();
}

static void on_key(char c) {
    wnd_t *f = wm_focused();
    if (!f) return;
    if (f->type == WND_KSH) {
        win_key(&g_term, c);
    } else if (f->type == WND_EDITOR) {
        editor_key(&g_edit, c);
        if (!editor_is_open(&g_edit) && g_win_edit_id >= 0) {
            wm_show(g_win_edit_id, 0);
            if (g_win_ksh_id >= 0) wm_raise(g_win_ksh_id);
        }
    } else if (f->type == WND_FILES) {
        files_key(&g_files, c);
    } else if (f->type == WND_PERSONALIZE) {
        pers_key(&g_pers, c);
    }
    full_redraw();
    cursor_show();
}

void kernel_main(uint64_t dtb) {
    uart_puts("\033[?2004l");
    uart_puts("BrainixKernel (BKOS) booted successfully!\n");
    uart_puts("Welcome to your own OS.\n");

    uart_puts("[step 7] DTB at "); uart_puthex(dtb); uart_puts("\n");
    dtb_set(PA_TO_KVA(dtb));
    ram_bank_t banks[DTB_MAX_RAM_BANKS];
    int nb = dtb_get_ram_banks(banks, DTB_MAX_RAM_BANKS);
    uart_puts("  RAM banks: "); uart_putu((uint64_t)nb); uart_puts("\n");

    uart_puts("[step 4] init physmem\n");
    physmem_init();
    uart_puts("  total pages: "); uart_putu(physmem_total_pages());
    uart_puts("\n  free pages:  "); uart_putu(physmem_free_pages()); uart_puts("\n");

    uart_puts("[step 5] init heap\n"); heap_init();

    uart_puts("[step 18] init initrd\n");
    int nfiles = initrd_init();
    uart_puts("  files in initrd: "); uart_putu((uint64_t)nfiles); uart_puts("\n");

    uart_puts("[step 21] init ksh\n"); ksh_init();

    uart_puts("[step 2] init GICv2 + timer\n");
    gic_init(); timer_init();

    uart_puts("[step 11] UART RX interrupt enabled\n"); uart_init_rx();

    pci_ecam_t ecam;
    if (!dtb_get_pci_ecam(&ecam)) panic("no ECAM");

    uart_puts("[step 22] blk_init\n");
    if (!blk_init(ecam.ecam_base)) uart_puts("  [blk] not found\n");
    else { uart_puts("  [blk] capacity = "); uart_putu(blk_capacity()); uart_puts(" sectors\n"); }

    uart_puts("[step 41] vnet_init\n");
    if (!vnet_init(ecam.ecam_base)) uart_puts("  [net] not found\n");
    else uart_puts("  [net] ready\n");

    domaininit();
    net_init();
    if (!dhcp_acquire()) {
        uart_puts("  [net] DHCP failed, using fallback hardcoded config\n");
    }
    arp_init();

    uart_puts("[step 50] sha256 selftest\n");
    if (!sha256_selftest()) panic("sha256 selftest FAILED");
    net_print_mac("  [net] my MAC = ", net_get_mac().b);
    net_print_ip ("  [net] my IP  = ", net_get_ip());
    net_print_ip ("  [net] gateway = ", net_get_gateway());

    uart_puts("  [net] resolving gateway MAC via ARP...\n");
    mac_t resolved_mac;
    int arp_ok = arp_resolve(net_get_gateway(), &resolved_mac);
    if (arp_ok) {
        uart_puts("  [net] ARP: OK, MAC = ");
        net_print_mac("", resolved_mac.b);
    } else {
        uart_puts("  [net] ARP: FAILED, falling back to hardcoded MAC\n");
        mac_t gw_mac = {{0x52, 0x55, 0x0A, 0x00, 0x02, 0x02}};
        resolved_mac = gw_mac;
        net_print_mac("  [net] gw MAC (fallback) = ", resolved_mac.b);
    }

    uart_puts("  [net] ping 10.0.2.2 ...\n");
    for (int i = 0; i < 3; i++) {
        int r = icmp_ping(net_get_gateway(), resolved_mac, (uint16_t)(i + 1));
        uart_puts("  [net] ping #");
        uart_putu((uint64_t)(i + 1));
        uart_puts(r ? " OK\n" : " TIMEOUT\n");
    }

    uart_puts("[step 23] bkfs_init\n");
    if (bkfs_init()) {
        uart_puts("  [bkfs] mounted\n");
    } else {
        uart_puts("  [bkfs] not formatted, formatting...\n");
        int bf_r = bkfs_format();
        if (bf_r == BKFS_OK) uart_puts("  [bkfs] formatted\n");
        else                 uart_puts("  [bkfs] format failed\n");
    }

    uart_puts("[step 15] gpu_init\n");
    if (!gpu_init(ecam.ecam_base)) panic("gpu init failed");

    uart_puts("[step 40] framebuffer 2560x1600 (2x retina)\n");
    uint32_t *fb = gpu_create_framebuffer(2560, 1600);
    if (!fb) panic("fb alloc failed");
    fb_init(fb, 2560, 1600, 2560);

    uart_puts("[step 38] backdrop_init (Liquid Glass)\n");
    backdrop_init();

    uart_puts("[step 15] kbd_init\n");
    if (!kbd_init(ecam.ecam_base)) uart_puts("  [kbd] not found\n");
    else { uart_puts("  [kbd] ready\n"); kbd_set_handler(on_key); }

    uart_puts("[step 15] mouse_init\n");
    if (!mouse_init(ecam.ecam_base)) uart_puts("  [mouse] not found\n");
    else uart_puts("  [mouse] ready\n");

    uart_puts("[step 25] wm_init\n"); wm_init();

    {
        uint64_t pages = ((uint64_t)SAVE_W * SAVE_H * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
        g_save_under = (uint32_t *)phys_alloc_pages(pages);
        if (!g_save_under) panic("save-under alloc failed");
    }

    uart_puts("[step 43] create ksh window\n");
    win_init(&g_term);
    win_puts(&g_term, "BrainixKernel OS v" BKOS_VERSION);
    win_puts(&g_term, "ELF loader + per-process AS");
    win_puts(&g_term, "");
    win_puts(&g_term, "Try 'sysinfo'.");

    g_win_ksh_id = wm_create(WND_KSH, 380, 80, 620, 620, "ksh - KernelShell");
    wm_raise(g_win_ksh_id);

    editor_close(&g_edit);
    g_win_edit_id = -1;
    g_win_files_id = -1;
    g_win_pers_id = -1;
    g_win_about_id = -1;
    files_init(&g_files);
    pers_close(&g_pers);

    uart_puts("[step 43] draw desktop\n");
    full_redraw();
    fb_flush();
    cursor_show();
    fb_flush();

    g_ui_ready = 1;

    uart_puts("[step 25] create tasks\n");
    task_t *a = task_create("A", task_printer_a);
    task_t *b = task_create("B", task_printer_b);
    uint64_t echo_size = (uint64_t)(user_echo_end - user_echo_start);
    uint64_t u_size    = (uint64_t)(user_print_u_end - user_print_u_start);
    task_t *u1 = task_create_user("U1", user_echo_start, echo_size);
    task_t *u2 = task_create_user("U2", user_print_u_start, u_size);
    if (!a || !b || !u1 || !u2 ) panic("task_create failed");

    // User-space shell: sh.elf. Запускаем как U-shell (stdin/stdout через UART).
    const initrd_file_t *elf_file = initrd_find("sh.elf");
    task_t *ue = 0;
    if (elf_file) {
        uart_puts("  [elf] sh.elf size=");
        uart_putu(elf_file->size);
        uart_puts(" entry-loading...\n");
        ue = task_create_from_elf("U-shell", elf_file->data, elf_file->size);
        if (!ue) {
            uart_puts("  [elf] task_create_from_elf failed: ");
            uart_puts(elf_last_error());
            uart_puts("\n");
        } else {
            uart_puts("  [elf] U-shell created\n");
        }
    } else {
        uart_puts("  [elf] sh.elf not found in initrd\n");
    }

    sched_add(a);
    sched_add(ue);
//     sched_add(uf);
    (void)b; (void)u1; (void)u2;

    uart_puts("  free pages after tasks: ");
    uart_putu(physmem_free_pages()); uart_puts("\n");

    uart_puts("[step 11] starting scheduler\n");
    uart_puts("  >> ICMP ping test active <<\n");

    uart_puts("[vnc] Connecting to 10.0.2.2:5901...\n");
    // vnc_connect((const uint8_t[]){10,0,2,2}, 5901);
    sched_start();
    panic("sched_start returned");
}