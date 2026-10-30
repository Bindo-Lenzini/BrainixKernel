// BrainixKernel — files.c
// Файловый менеджер BKFS: список, навигация, скроллбар, удаление.

#include <stdint.h>
#include "files.h"
#include "fb.h"
#include "bkfs.h"
#include "cred.h"

#define CLR_BODY      0xFF101C2E
#define CLR_FG        0xFFE0E0E0
#define CLR_FG_DIM    0xFF9DA9B8
#define CLR_HEADER    0xFF15243A
#define CLR_HEADER_FG 0xFF7AB8FF
#define CLR_SELECT    0xFF4A90E2
#define CLR_SELECT_FG 0xFFFFFFFF
#define CLR_STATUS    0xFF2C4A73
#define CLR_STATUS_FG 0xFFF0F0F0
#define CLR_OK        0xFF6EE7A0
#define CLR_ERR       0xFFFF6B6B
#define CLR_SB_BG     0xFF0A1018
#define CLR_SB_THUMB  0xFF4A90E2
#define CLR_SB_IDLE   0xFF3A5570

static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

void files_init(files_t *f) {
    f->count = 0;
    f->selected = 0;
    f->status_msg = 0;
    f->cursor_visible = 1;
    files_refresh(f);
}

void files_refresh(files_t *f) {
    f->count = 0;
    char fname[FILES_NAME_MAX];
    uint32_t fsize;
    const struct cred *cred = cred_ui();
    for (int i = 0; bkfs_list_for(i, fname, FILES_NAME_MAX, &fsize, cred); i++) {
        if (f->count >= FILES_MAX_ENTRIES) break;
        str_copy_n(f->names[f->count], fname, FILES_NAME_MAX);
        f->sizes[f->count] = fsize;
        f->count++;
    }
    if (f->selected >= f->count) f->selected = (f->count > 0) ? f->count - 1 : 0;
    if (f->selected < 0) f->selected = 0;
}

static void fmt_size(uint32_t sz, char *out, int max) {
    int i = 0;
    if (sz < 1024) {
        char tmp[16]; int t = 0;
        if (sz == 0) tmp[t++] = '0';
        while (sz) { tmp[t++] = (char)('0' + (sz % 10)); sz /= 10; }
        while (t > 0 && i < max - 1) out[i++] = tmp[--t];
        if (i < max - 1) out[i++] = ' ';
        if (i < max - 1) out[i++] = 'B';
    } else {
        uint32_t kb = sz / 1024;
        char tmp[16]; int t = 0;
        if (kb == 0) tmp[t++] = '0';
        while (kb) { tmp[t++] = (char)('0' + (kb % 10)); kb /= 10; }
        while (t > 0 && i < max - 1) out[i++] = tmp[--t];
        if (i < max - 1) out[i++] = ' ';
        if (i < max - 1) out[i++] = 'K';
        if (i < max - 1) out[i++] = 'B';
    }
    out[i] = 0;
}

// Сколько строк влезает.
static int files_visible_rows(int ch) {
    int rows = (ch - 26 - 22) / 16;
    if (rows < 1) rows = 1;
    return rows;
}

void files_draw_content(files_t *f, int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, CLR_BODY);

    // Header
    fb_fill_rect(cx, cy, cw, 22, CLR_HEADER);
    fb_draw_string(cx + 8, cy + 7, "Name", CLR_HEADER_FG, CLR_HEADER);
    fb_draw_string(cx + cw - 88, cy + 7, "Size", CLR_HEADER_FG, CLR_HEADER);

    int row_h = 16;
    int top = cy + 26;
    int avail_rows = files_visible_rows(ch);

    int sb_w = 10;
    int list_w = cw - sb_w - 4;

    int first = 0;
    if (f->selected >= avail_rows) first = f->selected - avail_rows + 1;
    if (f->selected < first) first = f->selected;
    if (first < 0) first = 0;

    for (int i = 0; i < avail_rows; i++) {
        int idx = first + i;
        if (idx >= f->count) break;

        int y = top + i * row_h;
        int is_sel = (idx == f->selected);
        uint32_t bg = is_sel ? CLR_SELECT : CLR_BODY;
        uint32_t fg = is_sel ? CLR_SELECT_FG : CLR_FG;

        if (is_sel) fb_fill_rect(cx + 2, y, list_w - 4, row_h, bg);

        fb_draw_string(cx + 8, y + 4, f->names[idx], fg, bg);

        char szbuf[16];
        fmt_size(f->sizes[idx], szbuf, sizeof(szbuf));
        int sw = str_len(szbuf) * 8;
        fb_draw_string(cx + list_w - 8 - sw, y + 4, szbuf,
                       is_sel ? CLR_SELECT_FG : CLR_FG_DIM, bg);
    }

    if (f->count == 0) {
        fb_draw_string(cx + 8, top + 4, "(no files)", CLR_FG_DIM, CLR_BODY);
    }

    // ---- Скроллбар ----
    int sb_x = cx + cw - sb_w - 2;
    int sb_y = cy + 26;
    int sb_h = ch - 26 - 22 - 4;
    if (sb_h < 20) sb_h = 20;
    fb_fill_rect(sb_x, sb_y, sb_w, sb_h, CLR_SB_BG);

    if (f->count > avail_rows) {
        int thumb_h = sb_h * avail_rows / f->count;
        if (thumb_h < 16) thumb_h = 16;
        int max_scroll = f->count - avail_rows;
        int thumb_y = sb_y;
        if (max_scroll > 0)
            thumb_y = sb_y + (sb_h - thumb_h) * first / max_scroll;
        fb_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, CLR_SB_THUMB);
    } else {
        fb_fill_rect(sb_x + 1, sb_y, sb_w - 2, sb_h, CLR_SB_IDLE);
    }

    // ---- Статус ----
    int sy = cy + ch - 20;
    fb_fill_rect(cx, sy, cw, 20, CLR_STATUS);
    char line[128];
    int i = 0;
    const char *pre = "Files: ";
    for (int j = 0; pre[j]; j++) line[i++] = pre[j];
    char tmp[16]; int t = 0;
    int cnt = f->count;
    if (cnt == 0) tmp[t++] = '0';
    while (cnt) { tmp[t++] = (char)('0' + (cnt % 10)); cnt /= 10; }
    while (t > 0) line[i++] = tmp[--t];
    const char *tail = " items    [Del] remove   [Enter] open";
    for (int j = 0; tail[j] && i < 120; j++) line[i++] = tail[j];
    line[i] = 0;
    fb_draw_string(cx + 8, sy + 6, line, CLR_STATUS_FG, CLR_STATUS);

    const char *right = 0;
    uint32_t rcol = CLR_STATUS_FG;
    if (f->status_msg == 1) { right = "deleted"; rcol = CLR_OK; }
    if (f->status_msg == 2) { right = "error";   rcol = CLR_ERR; }
    if (right) {
        int rlen = str_len(right);
        int rx = cx + cw - 8 - rlen * 8 - sb_w - 4;
        fb_draw_string(rx, sy + 6, right, rcol, CLR_STATUS);
    }
}

void files_key(files_t *f, char c) {
    if (f->count == 0 && c != '\n') return;

    if (c == 0x11) {                    // UP
        if (f->selected > 0) f->selected--;
        return;
    }
    if (c == 0x12) {                    // DOWN
        if (f->selected < f->count - 1) f->selected++;
        return;
    }
    if (c == 0x15) { f->selected = 0; return; }
    if (c == 0x16) { f->selected = f->count > 0 ? f->count - 1 : 0; return; }

    if (c == 0x17) {                    // DEL
        if (f->selected >= 0 && f->selected < f->count) {
            int r = bkfs_delete_for(f->names[f->selected], cred_ui());
            files_refresh(f);
            f->status_msg = (r == BKFS_OK) ? 1 : 2;
        }
        return;
    }

    if (c == '\n') {
        f->status_msg = 1;
        return;
    }
}

int files_click(files_t *f, int mx, int my,
                int cx, int cy, int cw, int ch) {
    int sb_w = 10;
    int sb_x = cx + cw - sb_w - 2;
    int sb_y = cy + 26;
    int sb_h = ch - 26 - 22 - 4;
    if (sb_h < 20) sb_h = 20;

    if (mx >= sb_x && mx < sb_x + sb_w &&
        my >= sb_y && my < sb_y + sb_h) {
        int avail_rows = files_visible_rows(ch);
        int max_scroll = f->count - avail_rows;
        if (max_scroll <= 0) return 1;
        int y = my - sb_y;
        int new_sel = max_scroll * y / sb_h;
        if (new_sel < 0) new_sel = 0;
        if (new_sel >= f->count) new_sel = f->count - 1;
        // Прыгаем так, чтобы выделенный оказался примерно там же
        f->selected = new_sel;
        return 1;
    }
    return 0;
}