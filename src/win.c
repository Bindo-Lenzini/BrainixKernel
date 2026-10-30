#include "version.h"
// BrainixKernel — win.c
// ksh + clipboard + 8×16 AA + sysinfo.

#include <stdint.h>
#include "win.h"
#include "fb.h"
#include "initrd.h"
#include "ksh.h"
#include "bkfs.h"
#include "cred.h"
#include "wallpaper.h"
#include "media.h"
#include "clipboard.h"
#include "physmem.h"
#include "blk.h"
#include "kbd.h"

extern void kwin_cmd_help(win_t *w);
extern void kwin_cmd_ver(win_t *w);
extern void kwin_cmd_tasks(win_t *w);
extern void kwin_cmd_mem(win_t *w);
extern int      ksh_launch_program(const char *name);
extern uint64_t blk_capacity(void);
extern int      ksh_request_edit(const char *name);
extern int      ksh_request_personalize(void);
extern void     request_full_redraw(void);

#define CLR_BODY      0xFF1A2E4A
#define CLR_FG        0xFFE0E0E0
#define CLR_PROMPT    0xFF7AB8FF
#define CLR_INPUT_BG  0xFF15243A
#define CLR_CURSOR    0xFF7AB8FF
#define LINE_H        14
#define FONT_W        8
#define PROMPT_W      (4 * FONT_W)

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == 0 && *b == 0;
}
static uint32_t u8_prev(const char *b, uint32_t pos) {
    if (pos == 0) return 0;
    uint32_t p = pos - 1;
    while (p > 0 && (b[p] & 0xC0) == 0x80) p--;
    return p;
}
static uint32_t u8_next(const char *b, uint32_t len, uint32_t pos) {
    if (pos >= len) return len;
    uint32_t p = pos + 1;
    while (p < len && (b[p] & 0xC0) == 0x80) p++;
    return p;
}
static int is_image_name(const char *name) {
    int len = str_len(name);
    if (len < 4) return 0;
    const char *e4 = name + len - 4;
    if (str_eq(e4, ".png") || str_eq(e4, ".PNG")) return 1;
    if (str_eq(e4, ".bmp") || str_eq(e4, ".BMP")) return 1;
    if (str_eq(e4, ".jpg") || str_eq(e4, ".JPG")) return 1;
    if (str_eq(e4, ".jpe") || str_eq(e4, ".JPE")) return 1;
    if (len >= 5) {
        const char *e5 = name + len - 5;
        if (str_eq(e5, ".jpeg") || str_eq(e5, ".JPEG")) return 1;
    }
    return 0;
}

void win_init(win_t *w) {
    w->history_count = 0;
    w->input_len = 0; w->input[0] = 0;
    w->cursor_pos = 0;
    w->cmd_history_count = 0;
    w->cmd_history_idx = -1;
    w->cursor_visible = 1;
    for (int i = 0; i < WIN_HISTORY_MAX; i++) w->history[i][0] = 0;
    for (int i = 0; i < WIN_CMD_HISTORY; i++) w->cmd_history[i][0] = 0;
}

void win_puts(win_t *w, const char *s) {
    if (w->history_count < WIN_HISTORY_MAX) {
        str_copy(w->history[w->history_count], s, WIN_LINE_MAX);
        w->history_count++;
        return;
    }
    for (int i = 1; i < WIN_HISTORY_MAX; i++)
        for (int j = 0; j < WIN_LINE_MAX; j++)
            w->history[i - 1][j] = w->history[i][j];
    str_copy(w->history[WIN_HISTORY_MAX - 1], s, WIN_LINE_MAX);
}

void win_draw_content(win_t *w, int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, CLR_BODY);
    int input_h = 30;
    int visible_rows = (ch - input_h - 6) / LINE_H;
    int start = 0;
    if (w->history_count > visible_rows)
        start = w->history_count - visible_rows;

    int ty = cy + 4;
    for (int i = start; i < w->history_count; i++) {
        fb_draw_string_aa(cx + 8, ty, w->history[i], CLR_FG, CLR_BODY);
        ty += LINE_H;
    }

    int iy = cy + ch - input_h - 2;
    fb_fill_rect(cx + 2, iy, cw - 4, input_h, CLR_INPUT_BG);

    int text_y = iy + (input_h - 16) / 2;
    fb_draw_string_aa(cx + 12, text_y, "ksh$", CLR_PROMPT, CLR_INPUT_BG);

    int text_x = cx + 12 + PROMPT_W;
    fb_draw_string_aa(text_x, text_y, w->input, CLR_FG, CLR_INPUT_BG);

    if (w->cursor_visible) {
        int col = 0;
        uint32_t cp = 0;
        uint32_t input_len = (uint32_t)w->input_len;
        uint32_t cur_pos   = (uint32_t)w->cursor_pos;
        while (cp < cur_pos) { cp = u8_next(w->input, cur_pos, cp); col++; }
        int px = text_x + col * FONT_W;

        if (cur_pos < input_len) {
            uint32_t next = u8_next(w->input, input_len, cur_pos);
            char tmp[5] = {0};
            for (uint32_t k = cur_pos; k < next && k - cur_pos < 4; k++)
                tmp[k - cur_pos] = w->input[k];
            fb_fill_rect(px, text_y, FONT_W, 16, CLR_CURSOR);
            fb_draw_string_aa(px, text_y, tmp, CLR_INPUT_BG, 0);
        } else {
            fb_fill_rect(px, text_y, FONT_W, 16, CLR_CURSOR);
        }
    }
}

static void tab_complete(win_t *w) {
    static const char *cmds[] = {
        "help", "ver", "tasks", "mem", "disk", "sysinfo",
        "mkfs", "lsw", "touch", "write", "rm",
        "edit", "wallpaper", "images", "photos", "personalize",
        "echo", "clear", "ls", "cat", "motd", "hexdump", "run", 0
    };
    int sp = -1;
    for (int i = 0; i < w->input_len; i++)
        if (w->input[i] == ' ') { sp = i; break; }

    if (sp < 0) {
        for (int i = 0; cmds[i]; i++) {
            const char *c = cmds[i];
            int k = 0;
            while (c[k] && w->input[k] && c[k] == w->input[k]) k++;
            if (k == w->input_len) {
                int L = str_len(c);
                for (int j = w->input_len; j < L; j++) w->input[j] = c[j];
                w->input[L] = ' '; w->input[L + 1] = 0;
                w->input_len = L + 1; w->cursor_pos = w->input_len;
                return;
            }
        }
        return;
    }
    if (sp == 9 && w->input[0] == 'w' && w->input[1] == 'a') {
        const char *prefix = w->input + 10;
        int plen = str_len(prefix);
        int n = wallpaper_count();
        for (int i = 0; i < n; i++) {
            const char *name = wallpaper_name(i);
            int k = 0;
            while (name[k] && prefix[k] && name[k] == prefix[k]) k++;
            if (k == plen) {
                int L = str_len(name);
                for (int j = plen; j < L; j++) w->input[10 + j] = name[j];
                w->input[10 + L] = 0;
                w->input_len = 10 + L; w->cursor_pos = w->input_len;
                return;
            }
        }
        int nf = initrd_count();
        for (int i = 0; i < nf; i++) {
            const initrd_file_t *f = initrd_get(i);
            if (!f || !is_image_name(f->name)) continue;
            int k = 0;
            while (f->name[k] && prefix[k] && f->name[k] == prefix[k]) k++;
            if (k == plen) {
                int L = str_len(f->name);
                for (int j = plen; j < L; j++) w->input[10 + j] = f->name[j];
                w->input[10 + L] = 0;
                w->input_len = 10 + L; w->cursor_pos = w->input_len;
                return;
            }
        }
        if (media_is_ready()) {
            int nm = media_count();
            for (int i = 0; i < nm; i++) {
                char mname[MEDIA_MAX_NAME]; uint32_t msize;
                if (!media_list(i, mname, sizeof(mname), &msize)) continue;
                if (!is_image_name(mname)) continue;
                int k = 0;
                while (mname[k] && prefix[k] && mname[k] == prefix[k]) k++;
                if (k == plen) {
                    int L = str_len(mname);
                    for (int j = plen; j < L; j++) w->input[10 + j] = mname[j];
                    w->input[10 + L] = 0;
                    w->input_len = 10 + L; w->cursor_pos = w->input_len;
                    return;
                }
            }
        }
    }
    const char *prefix = w->input + sp + 1;
    int plen = str_len(prefix);
    char fname[64]; uint32_t fsize;
    for (int i = 0; bkfs_list_for(i, fname, sizeof(fname), &fsize, cred_ui()); i++) {
        int k = 0;
        while (fname[k] && prefix[k] && fname[k] == prefix[k]) k++;
        if (k == plen) {
            int L = str_len(fname);
            for (int j = plen; j < L; j++) w->input[sp + 1 + j] = fname[j];
            w->input[sp + 1 + L] = 0;
            w->input_len = sp + 1 + L; w->cursor_pos = w->input_len;
            return;
        }
    }
}

static int str_eq_prefix(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}
static void print_initrd_file(win_t *w, const char *name) {
    const initrd_file_t *f = initrd_find(name);
    if (!f) { win_puts(w, "no such file"); return; }
    char line[WIN_LINE_MAX]; int li = 0;
    for (uint32_t i = 0; i < f->size; i++) {
        char c = (char)f->data[i];
        if (c == '\r') continue;
        if (c == '\n') { line[li] = 0; win_puts(w, line); li = 0; continue; }
        if (li < WIN_LINE_MAX - 1) line[li++] = c;
    }
    if (li > 0) { line[li] = 0; win_puts(w, line); }
}
static void print_bkfs_file(win_t *w, const char *name) {
    static uint8_t buf[8192];
    int n = bkfs_read_for(name, buf, sizeof(buf), cred_ui());
    if (n == BKFS_ENOENT) { win_puts(w, "no such file"); return; }
    if (n < 0) { win_puts(w, "read error"); return; }
    char line[WIN_LINE_MAX]; int li = 0;
    for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '\r') continue;
        if (c == '\n') { line[li] = 0; win_puts(w, line); li = 0; continue; }
        if (li < WIN_LINE_MAX - 1) line[li++] = c;
    }
    if (li > 0) { line[li] = 0; win_puts(w, line); }
}
static void push_cmd_history(win_t *w, const char *cmd) {
    if (!cmd[0]) return;
    if (w->cmd_history_count > 0) {
        int last = (w->cmd_history_count - 1) % WIN_CMD_HISTORY;
        if (str_eq(w->cmd_history[last], cmd)) return;
    }
    int idx = w->cmd_history_count % WIN_CMD_HISTORY;
    str_copy(w->cmd_history[idx], cmd, WIN_INPUT_MAX + 1);
    w->cmd_history_count++;
}
static const char *get_cmd_history(win_t *w, int back) {
    if (back >= w->cmd_history_count) return 0;
    int idx = (w->cmd_history_count - 1 - back) % WIN_CMD_HISTORY;
    return w->cmd_history[idx];
}
static void print_bkfs_list(win_t *w) {
    char fname[64]; uint32_t fsize;
    int n = 0;
    for (int i = 0; bkfs_list_for(i, fname, sizeof(fname), &fsize, cred_ui()); i++) {
        char buf[WIN_LINE_MAX]; int bi = 0;
        for (int j = 0; fname[j] && bi < WIN_LINE_MAX - 20; j++) buf[bi++] = fname[j];
        while (bi < 20) buf[bi++] = ' ';
        char tmp[16]; int t = 0;
        uint32_t sz = fsize;
        if (sz == 0) tmp[t++] = '0';
        while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
        while (t > 0) buf[bi++] = tmp[--t];
        const char *tail = " bytes";
        for (int j = 0; tail[j]; j++) buf[bi++] = tail[j];
        buf[bi] = 0; win_puts(w, buf); n++;
    }
    if (n == 0) win_puts(w, "(no files)");
}
static void print_initrd_images(win_t *w) {
    int n = initrd_count(); int found = 0;
    for (int i = 0; i < n; i++) {
        const initrd_file_t *f = initrd_get(i);
        if (!f || !is_image_name(f->name)) continue;
        char buf[WIN_LINE_MAX]; int bi = 0;
        for (int j = 0; f->name[j] && bi < WIN_LINE_MAX - 16; j++) buf[bi++] = f->name[j];
        while (bi < 24) buf[bi++] = ' ';
        char tmp[16]; int t = 0;
        uint32_t sz = f->size;
        if (sz == 0) tmp[t++] = '0';
        while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
        while (t > 0) buf[bi++] = tmp[--t];
        const char *tail = " bytes";
        for (int j = 0; tail[j]; j++) buf[bi++] = tail[j];
        buf[bi] = 0; win_puts(w, buf); found++;
    }
    if (!found) win_puts(w, "  (no images in initrd)");
}
static void print_media_images(win_t *w) {
    if (!media_is_ready()) { win_puts(w, "  (media disk not attached)"); return; }
    int nm = media_count(); int found = 0;
    for (int i = 0; i < nm; i++) {
        char mname[MEDIA_MAX_NAME]; uint32_t msize;
        if (!media_list(i, mname, sizeof(mname), &msize)) continue;
        if (!is_image_name(mname)) continue;
        char buf[WIN_LINE_MAX]; int bi = 0;
        for (int j = 0; mname[j] && bi < WIN_LINE_MAX - 16; j++) buf[bi++] = mname[j];
        while (bi < 24) buf[bi++] = ' ';
        char tmp[16]; int t = 0;
        uint32_t sz = msize;
        if (sz == 0) tmp[t++] = '0';
        while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
        while (t > 0) buf[bi++] = tmp[--t];
        const char *tail = " bytes";
        for (int j = 0; tail[j]; j++) buf[bi++] = tail[j];
        buf[bi] = 0; win_puts(w, buf); found++;
    }
    if (!found) win_puts(w, "  (no images on media)");
}

// ============================================================
// Команда sysinfo — детальный отчёт.
// ============================================================
static void u64_s(uint64_t v, char *out, int max) {
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (t > 0 && i < max - 1) out[i++] = tmp[--t];
    out[i] = 0;
}

static void cmd_sysinfo(win_t *w) {
    char buf[128];

    win_puts(w, "=== System Report ===");
    win_puts(w, "OS: BrainixKernel v" BRAINIX_VERSION);
    win_puts(w, "Arch: AArch64 (ARMv8-A)");
    win_puts(w, "");

    // RAM
    uint64_t total_pages = physmem_total_pages();
    uint64_t free_pages  = physmem_free_pages();
    uint64_t used_pages  = total_pages - free_pages;

    win_puts(w, "--- Memory ---");
    u64_s(total_pages * 4, buf, sizeof(buf));
    int i = 0;
    char line[128];
    const char *p1 = "Total: ";
    for (int j = 0; p1[j]; j++) line[i++] = p1[j];
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p2 = " KB (";
    for (int j = 0; p2[j]; j++) line[i++] = p2[j];
    u64_s(total_pages, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p3 = " pages)";
    for (int j = 0; p3[j]; j++) line[i++] = p3[j];
    line[i] = 0;
    win_puts(w, line);

    i = 0;
    const char *p4 = "Used:  ";
    for (int j = 0; p4[j]; j++) line[i++] = p4[j];
    u64_s(used_pages * 4, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p5 = " KB (";
    for (int j = 0; p5[j]; j++) line[i++] = p5[j];
    u64_s(used_pages, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p6 = " pages)";
    for (int j = 0; p6[j]; j++) line[i++] = p6[j];
    line[i] = 0;
    win_puts(w, line);

    i = 0;
    const char *p7 = "Free:  ";
    for (int j = 0; p7[j]; j++) line[i++] = p7[j];
    u64_s(free_pages * 4, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p8 = " KB (";
    for (int j = 0; p8[j]; j++) line[i++] = p8[j];
    u64_s(free_pages, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *p9 = " pages)";
    for (int j = 0; p9[j]; j++) line[i++] = p9[j];
    line[i] = 0;
    win_puts(w, line);

    if (total_pages > 0) {
        uint64_t pct = (used_pages * 100) / total_pages;
        i = 0;
        const char *p10 = "Usage: ";
        for (int j = 0; p10[j]; j++) line[i++] = p10[j];
        u64_s(pct, buf, sizeof(buf));
        for (int j = 0; buf[j]; j++) line[i++] = buf[j];
        line[i++] = '%';
        line[i] = 0;
        win_puts(w, line);
    }
    win_puts(w, "");

    // Disk
    win_puts(w, "--- Storage ---");
    uint64_t sectors = blk_capacity();
    i = 0;
    const char *d1 = "Sectors: ";
    for (int j = 0; d1[j]; j++) line[i++] = d1[j];
    u64_s(sectors, buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    line[i] = 0;
    win_puts(w, line);

    i = 0;
    const char *d2 = "Size:    ";
    for (int j = 0; d2[j]; j++) line[i++] = d2[j];
    u64_s((sectors * 512) / (1024 * 1024), buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    const char *d3 = " MB";
    for (int j = 0; d3[j]; j++) line[i++] = d3[j];
    line[i] = 0;
    win_puts(w, line);
    win_puts(w, "FS: BKFS");
    win_puts(w, "");

    // Display
    win_puts(w, "--- Display ---");
    i = 0;
    u64_s(fb_width(), buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    line[i++] = ' ';
    buf[0] = (char)0xC3; buf[1] = (char)0x97; buf[2] = 0;
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    line[i++] = ' ';
    u64_s(fb_height(), buf, sizeof(buf));
    for (int j = 0; buf[j]; j++) line[i++] = buf[j];
    line[i] = 0;
    win_puts(w, line);
    win_puts(w, "FB: 1280x800x32 (ARGB8888)");
    win_puts(w, "");

    // Locale
    win_puts(w, "--- Locale ---");
    i = 0;
    const char *l1 = "Keyboard: ";
    for (int j = 0; l1[j]; j++) line[i++] = l1[j];
    const char *lay = kbd_layout_ru() ? "Russian" : "English";
    for (int j = 0; lay[j]; j++) line[i++] = lay[j];
    line[i] = 0;
    win_puts(w, line);
    win_puts(w, "Timezone: UTC+3");
    win_puts(w, "");
    win_puts(w, "=== End Report ===");
}

static void exec_cmd(win_t *w, const char *cmd) {
    char line[WIN_LINE_MAX];
    int i = 0;
    const char *pre = "ksh$ ";
    for (int j = 0; pre[j]; j++) line[i++] = pre[j];
    for (int j = 0; cmd[j] && i < WIN_LINE_MAX - 1; j++) line[i++] = cmd[j];
    line[i] = 0;
    win_puts(w, line);

    if (!cmd[0]) return;
    if (str_eq(cmd, "help"))  { kwin_cmd_help(w); return; }
    if (str_eq(cmd, "ver"))   { kwin_cmd_ver(w); return; }
    if (str_eq(cmd, "tasks")) { kwin_cmd_tasks(w); return; }
    if (str_eq(cmd, "mem"))   { kwin_cmd_mem(w); return; }
    if (str_eq(cmd, "sysinfo")) { cmd_sysinfo(w); return; }
    if (str_eq(cmd, "clear")) { w->history_count = 0; return; }
    if (str_eq(cmd, "images")) { win_puts(w, "Images in initrd:"); print_initrd_images(w); return; }
    if (str_eq(cmd, "photos")) { win_puts(w, "Images on media disk:"); print_media_images(w); return; }
    if (str_eq(cmd, "personalize")) {
        if (ksh_request_personalize()) win_puts(w, "Personalization opened");
        else win_puts(w, "cannot open Personalization");
        return;
    }
    if (str_eq(cmd, "disk")) {
        uint64_t cap = blk_capacity();
        char buf[64]; int k = 0;
        const char *p = "capacity: ";
        for (int j = 0; p[j]; j++) buf[k++] = p[j];
        char tmp[24]; int t = 0;
        if (cap == 0) tmp[t++] = '0';
        while (cap) { tmp[t++] = (char)('0' + (cap % 10)); cap /= 10; }
        while (t > 0) buf[k++] = tmp[--t];
        const char *tail = " sectors";
        for (int j = 0; tail[j]; j++) buf[k++] = tail[j];
        buf[k] = 0; win_puts(w, buf);
        return;
    }
    if (str_eq(cmd, "wallpaper")) {
        win_puts(w, "Presets:");
        int n = wallpaper_count();
        for (int i = 0; i < n; i++) {
            char buf[48]; buf[0] = ' '; buf[1] = ' ';
            int k = 2;
            const char *name = wallpaper_name(i);
            for (int j = 0; name[j] && k < 46; j++) buf[k++] = name[j];
            buf[k] = 0; win_puts(w, buf);
        }
        win_puts(w, "Images in initrd:"); print_initrd_images(w);
        win_puts(w, "Images on media disk:"); print_media_images(w);
        win_puts(w, "usage: wallpaper <preset>");
        win_puts(w, "       wallpaper <file.png|.bmp|.jpg|.jpeg>");
        return;
    }
    if (str_eq_prefix(cmd, "wallpaper ")) {
        const char *name = cmd + 10;
        if (wallpaper_set_by_name(name)) {
            win_puts(w, "wallpaper changed");
            request_full_redraw();
        } else {
            const char *reason = wallpaper_last_reason();
            if (reason && reason[0]) {
                char buf[WIN_LINE_MAX]; int bi = 0;
                const char *pre2 = "cannot set wallpaper: ";
                for (int j = 0; pre2[j] && bi < WIN_LINE_MAX - 1; j++) buf[bi++] = pre2[j];
                for (int j = 0; reason[j] && bi < WIN_LINE_MAX - 1; j++) buf[bi++] = reason[j];
                buf[bi] = 0; win_puts(w, buf);
            } else win_puts(w, "cannot set wallpaper");
        }
        return;
    }
    if (str_eq(cmd, "mkfs")) {
        int r = bkfs_format_for(cred_ui());
        win_puts(w, r == BKFS_OK ? "formatted" : "permission denied");
        return;
    }
    if (str_eq(cmd, "lsw")) {
        if (!bkfs_is_ready()) { win_puts(w, "no filesystem (run mkfs)"); return; }
        print_bkfs_list(w); return;
    }
    if (str_eq(cmd, "touch")) { win_puts(w, "usage: touch <name>"); return; }
    if (str_eq_prefix(cmd, "touch ")) {
        if (!bkfs_is_ready()) { win_puts(w, "no filesystem"); return; }
        int r = bkfs_create_for(cmd + 6, cred_ui(), BKFS_DEFAULT_MODE);
        if (r == BKFS_OK) win_puts(w, "created");
        else if (r == BKFS_EEXIST) win_puts(w, "already exists");
        else win_puts(w, "error");
        return;
    }
    if (str_eq(cmd, "write")) { win_puts(w, "usage: write <name> <text>"); return; }
    if (str_eq_prefix(cmd, "write ")) {
        if (!bkfs_is_ready()) { win_puts(w, "no filesystem"); return; }
        const char *arg = cmd + 6;
        int sp = -1;
        for (int j = 0; arg[j]; j++) if (arg[j] == ' ') { sp = j; break; }
        if (sp <= 0) { win_puts(w, "usage: write <name> <text>"); return; }
        char name[64];
        for (int j = 0; j < sp && j < 63; j++) name[j] = arg[j];
        name[sp] = 0;
        const char *text = arg + sp + 1;
        uint32_t tlen = (uint32_t)str_len(text);
        int r = bkfs_create_for(name, cred_ui(), BKFS_DEFAULT_MODE);
        if (r == BKFS_EEXIST) r = BKFS_OK;
        if (r != BKFS_OK) { win_puts(w, "create failed"); return; }
        int w_len = bkfs_write_for(name, text, tlen, cred_ui());
        if (w_len < 0) { win_puts(w, "write failed"); return; }
        win_puts(w, "written"); return;
    }
    if (str_eq(cmd, "rm")) { win_puts(w, "usage: rm <name>"); return; }
    if (str_eq_prefix(cmd, "rm ")) {
        if (!bkfs_is_ready()) { win_puts(w, "no filesystem"); return; }
        int r = bkfs_delete_for(cmd + 3, cred_ui());
        if (r == BKFS_OK) win_puts(w, "removed");
        else if (r == BKFS_ENOENT) win_puts(w, "no such file");
        else win_puts(w, "error");
        return;
    }
    if (str_eq(cmd, "edit")) { win_puts(w, "usage: edit <file>"); return; }
    if (str_eq_prefix(cmd, "edit ")) {
        if (!bkfs_is_ready()) { win_puts(w, "no filesystem"); return; }
        const char *name = cmd + 5;
        if (ksh_request_edit(name)) win_puts(w, "editor opened");
        else win_puts(w, "cannot open editor");
        return;
    }
    if (str_eq_prefix(cmd, "echo ")) { win_puts(w, cmd + 5); return; }
    if (str_eq(cmd, "echo")) { win_puts(w, ""); return; }
    if (str_eq(cmd, "ls")) {
        win_puts(w, "--- initrd ---");
        int n = initrd_count();
        if (n == 0) win_puts(w, "(empty)");
        for (int k = 0; k < n; k++) {
            const initrd_file_t *f = initrd_get(k);
            if (!f) continue;
            char buf[WIN_LINE_MAX]; int bi = 0;
            for (int j = 0; f->name[j] && bi < WIN_LINE_MAX - 20; j++) buf[bi++] = f->name[j];
            while (bi < 20) buf[bi++] = ' ';
            char tmp[16]; int t = 0;
            uint32_t sz = f->size;
            if (sz == 0) tmp[t++] = '0';
            while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
            while (t > 0) buf[bi++] = tmp[--t];
            const char *tail = " bytes";
            for (int j = 0; tail[j]; j++) buf[bi++] = tail[j];
            buf[bi] = 0; win_puts(w, buf);
        }
        if (bkfs_is_ready()) {
            win_puts(w, "--- disk (BKFS) ---");
            print_bkfs_list(w);
        } else win_puts(w, "--- disk: not formatted ---");
        return;
    }
    if (str_eq(cmd, "motd")) { print_initrd_file(w, "motd.txt"); return; }
    if (str_eq(cmd, "cat")) { win_puts(w, "usage: cat <file>"); return; }
    if (str_eq_prefix(cmd, "cat ")) {
        const char *name = cmd + 4;
        if (bkfs_is_ready()) {
            int inode = bkfs_open(name);
            if (inode >= 0) {
                uint32_t uid, gid, mode;
                if (bkfs_get_metadata(inode, &uid, &gid, &mode) != BKFS_OK ||
                    !cred_may_access(cred_ui(), mode, uid, gid, CRED_R)) {
                    win_puts(w, "permission denied");
                } else {
                    print_bkfs_file(w, name);
                }
                return;
            }
        }
        print_initrd_file(w, name); return;
    }
    if (str_eq(cmd, "run")) { win_puts(w, "usage: run <prog>  (progs: cat)"); return; }
    if (str_eq_prefix(cmd, "run ")) {
        const char *line = cmd + 4;
        int ok = ksh_launch_program(line);
        if (!ok) win_puts(w, "run: failed (prog not found in initrd)");
        return;
    }
    win_puts(w, "unknown command. type 'help'.");
}

void win_key(win_t *w, char c) {
    uint32_t input_len = (uint32_t)w->input_len;
    uint32_t cur_pos   = (uint32_t)w->cursor_pos;

    if (c == 0x11) {
        const char *cmd = get_cmd_history(w, w->cmd_history_idx + 1);
        if (cmd) {
            w->cmd_history_idx++;
            str_copy(w->input, cmd, WIN_INPUT_MAX + 1);
            w->input_len = str_len(w->input); w->cursor_pos = w->input_len;
        }
        return;
    }
    if (c == 0x12) {
        if (w->cmd_history_idx > 0) {
            w->cmd_history_idx--;
            const char *cmd = get_cmd_history(w, w->cmd_history_idx);
            if (cmd) {
                str_copy(w->input, cmd, WIN_INPUT_MAX + 1);
                w->input_len = str_len(w->input); w->cursor_pos = w->input_len;
            }
        } else if (w->cmd_history_idx == 0) {
            w->cmd_history_idx = -1;
            w->input[0] = 0; w->input_len = 0; w->cursor_pos = 0;
        }
        return;
    }
    if (c == 0x13) { if (cur_pos > 0) w->cursor_pos = (int)u8_prev(w->input, cur_pos); return; }
    if (c == 0x14) { if (cur_pos < input_len) w->cursor_pos = (int)u8_next(w->input, input_len, cur_pos); return; }
    if (c == 0x15) { w->cursor_pos = 0; return; }
    if (c == 0x16) { w->cursor_pos = w->input_len; return; }
    if (c == 0x17) {
        if (cur_pos < input_len) {
            uint32_t next = u8_next(w->input, input_len, cur_pos);
            uint32_t n = next - cur_pos;
            for (uint32_t i = cur_pos; i + n < input_len; i++)
                w->input[i] = w->input[i + n];
            w->input_len -= (int)n;
            w->input[w->input_len] = 0;
        }
        return;
    }
    if (c == 0x1A) { w->input_len = 0; w->cursor_pos = 0; w->input[0] = 0; return; }
    if (c == 0x1B) {
        int plen = 0;
        const char *clip = clip_get(&plen);
        if (plen <= 0) return;
        if (w->input_len + plen > WIN_INPUT_MAX) plen = WIN_INPUT_MAX - w->input_len;
        for (int i = w->input_len; i > w->cursor_pos; i--)
            w->input[i + plen - 1] = w->input[i - 1];
        for (int i = 0; i < plen; i++)
            w->input[w->cursor_pos + i] = clip[i];
        w->input_len += plen;
        w->cursor_pos += plen;
        w->input[w->input_len] = 0;
        return;
    }
    if (c == '\b') {
        if (cur_pos > 0) {
            uint32_t prev = u8_prev(w->input, cur_pos);
            uint32_t n = cur_pos - prev;
            for (uint32_t i = prev; i + n < input_len; i++)
                w->input[i] = w->input[i + n];
            w->input_len -= (int)n;
            w->input[w->input_len] = 0;
            w->cursor_pos = (int)prev;
        }
        return;
    }
    if (c == '\t') { tab_complete(w); return; }
    if (c == '\n') {
        w->input[w->input_len] = 0;
        push_cmd_history(w, w->input);
        w->cmd_history_idx = -1;
        exec_cmd(w, w->input);
        w->input_len = 0; w->cursor_pos = 0; w->input[0] = 0;
        return;
    }
    if ((unsigned char)c < 0x20) return;
    if (w->input_len < WIN_INPUT_MAX) {
        for (int i = w->input_len; i > w->cursor_pos; i--)
            w->input[i] = w->input[i - 1];
        w->input[w->cursor_pos] = c;
        w->input_len++;
        w->input[w->input_len] = 0;
        w->cursor_pos++;
    }
}