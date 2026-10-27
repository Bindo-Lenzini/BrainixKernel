// BrainixKernel — editor.c

#include <stdint.h>
#include "editor.h"
#include "fb.h"
#include "bkfs.h"
#include "clipboard.h"

#define CLR_BODY     0xFF101C2E
#define CLR_FG       0xFFE0E0E0
#define CLR_CURSOR   0xFFFFD166
#define CLR_STATUS   0xFF2C4A73
#define CLR_STATUSFG 0xFFF0F0F0
#define CLR_OK       0xFF6EE7A0
#define CLR_ERR      0xFFFF6B6B
#define CLR_SB_BG    0xFF0A1018
#define CLR_SB_THUMB 0xFF4A90E2
#define CLR_SB_IDLE  0xFF3A5570

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ---- UTF-8 helpers ----
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

// ---- Логические строки ----
static int editor_total_lines(const editor_t *e) {
    int lines = 1;
    for (uint32_t i = 0; i < e->len; i++)
        if (e->buf[i] == '\n') lines++;
    return lines;
}
static int editor_line_of(const editor_t *e, uint32_t pos) {
    int line = 0;
    uint32_t end = pos > e->len ? e->len : pos;
    for (uint32_t i = 0; i < end; i++)
        if (e->buf[i] == '\n') line++;
    return line;
}
static uint32_t editor_line_start(const editor_t *e, int target) {
    if (target <= 0) return 0;
    int line = 0;
    for (uint32_t i = 0; i < e->len; i++) {
        if (e->buf[i] == '\n') {
            line++;
            if (line == target) return i + 1;
        }
    }
    return e->len;
}
static void editor_scroll_to_cursor(editor_t *e) {
    int vr = e->visible_rows;
    if (vr < 1) vr = 1;
    int cur_line = editor_line_of(e, e->cursor);
    int total    = editor_total_lines(e);
    int max_scroll = total - vr;
    if (max_scroll < 0) max_scroll = 0;
    if (cur_line < e->scroll_line) e->scroll_line = cur_line;
    if (cur_line >= e->scroll_line + vr) e->scroll_line = cur_line - vr + 1;
    if (e->scroll_line > max_scroll) e->scroll_line = max_scroll;
    if (e->scroll_line < 0) e->scroll_line = 0;
}

// ---- Публичный API ----
void editor_open(editor_t *e, const char *name) {
    str_copy_n(e->name, name, 64);
    e->len = 0; e->cursor = 0;
    e->cursor_visible = 1; e->dirty = 0;
    e->status_msg = 0;
    e->scroll_line = 0;
    e->visible_rows = 20;
    int n = bkfs_read(name, e->buf, EDIT_MAX - 1);
    if (n > 0) { e->len = (uint32_t)n; e->buf[e->len] = 0; }
    else       { e->len = 0; e->buf[0] = 0; }
    e->cursor = e->len;
}

void editor_close(editor_t *e) { e->name[0] = 0; e->len = 0; }
int  editor_is_open(const editor_t *e) { return e->name[0] != 0; }

int editor_save(editor_t *e) {
    int r = bkfs_create(e->name);
    if (r == BKFS_EEXIST) r = BKFS_OK;
    if (r != BKFS_OK) { e->status_msg = 2; return 0; }
    int w = bkfs_write(e->name, e->buf, e->len);
    if (w < 0) { e->status_msg = 2; return 0; }
    e->dirty = 0; e->status_msg = 1;
    return 1;
}

void editor_draw_content(editor_t *e, int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, CLR_BODY);

    int status_h = 22;
    int sb_w = 10;
    int tx = cx + 8, ty = cy + 4;
    int cols = (cw - 16 - sb_w - 4) / 8;
    int rows = (ch - status_h - 6) / 12;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    e->visible_rows = rows;

    int total_lines = editor_total_lines(e);
    int max_scroll = total_lines - rows;
    if (max_scroll < 0) max_scroll = 0;
    if (e->scroll_line > max_scroll) e->scroll_line = max_scroll;
    if (e->scroll_line < 0) e->scroll_line = 0;

    int cur_line = editor_line_of(e, e->cursor);
    uint32_t cur_line_start = editor_line_start(e, cur_line);
    int cur_col = (int)(e->cursor - cur_line_start);

    for (int r = 0; r < rows; r++) {
        int line = e->scroll_line + r;
        if (line >= total_lines) break;
        uint32_t start = editor_line_start(e, line);
        int ry = ty + r * 12;

        uint32_t pos = start;
        int col = 0;
        while (pos < e->len && col < cols) {
            char ch2 = e->buf[pos];
            if (ch2 == '\n') break;
            if (ch2 == '\r') { pos++; continue; }

            // Определяем длину UTF-8 символа
            uint32_t next = u8_next(e->buf, e->len, pos);

            // Курсор попадает в этот символ?
            int under_cursor = (e->cursor >= pos && e->cursor < next);
            if (under_cursor && e->cursor_visible) {
                // Рисуем блок курсора
                fb_fill_rect(tx + col * 8, ry, 8, 12, CLR_CURSOR);
            }

            // Рисуем символ (UTF-8 декодируется в fb_draw_string_aa)
            char tmp[5] = {0};
            for (uint32_t k = pos; k < next && k - pos < 4; k++)
                tmp[k - pos] = e->buf[k];

            if (under_cursor && e->cursor_visible) {
                fb_draw_string_aa(tx + col * 8, ry - 2, tmp, CLR_BODY, 0);
            } else {
                fb_draw_string_aa(tx + col * 8, ry - 2, tmp, CLR_FG, CLR_BODY);
            }
            col++;
            pos = next;
        }

        // Курсор в конце строки
        if (cur_line == line && e->cursor_visible) {
            uint32_t line_end = start;
            while (line_end < e->len && e->buf[line_end] != '\n') line_end++;
            if (e->cursor == line_end && cur_col < cols) {
                fb_fill_rect(tx + col * 8, ry, 8, 12, CLR_CURSOR);
            }
        }
    }

    if (e->cursor == e->len && e->cursor_visible) {
        int cur_row = cur_line - e->scroll_line;
        if (cur_row >= 0 && cur_row < rows) {
            int px = tx + cur_col * 8;
            int py = ty + cur_row * 12;
            fb_fill_rect(px, py, 8, 12, CLR_CURSOR);
        }
    }

    // Скроллбар
    int sb_x = cx + cw - sb_w - 2;
    int sb_y = cy + 4;
    int sb_h = ch - status_h - 8;
    if (sb_h < 20) sb_h = 20;
    fb_fill_rect(sb_x, sb_y, sb_w, sb_h, CLR_SB_BG);
    if (total_lines > rows) {
        int thumb_h = sb_h * rows / total_lines;
        if (thumb_h < 16) thumb_h = 16;
        int thumb_y = sb_y;
        if (max_scroll > 0)
            thumb_y = sb_y + (sb_h - thumb_h) * e->scroll_line / max_scroll;
        fb_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, CLR_SB_THUMB);
    } else {
        fb_fill_rect(sb_x + 1, sb_y, sb_w - 2, sb_h, CLR_SB_IDLE);
    }

    // Статус
    int sy = cy + ch - status_h;
    fb_fill_rect(cx, sy, cw, status_h, CLR_STATUS);
    char title[80];
    int i = 0;
    for (int j = 0; e->name[j] && i < 60; j++) title[i++] = e->name[j];
    if (e->dirty) { title[i++] = ' '; title[i++] = '*'; }
    title[i] = 0;
    fb_draw_string(cx + 8, sy + 7, title, CLR_STATUSFG, CLR_STATUS);

    {
        char inf[32];
        int p = 0;
        const char *pre = "  Ln ";
        for (int j = 0; pre[j]; j++) inf[p++] = pre[j];
        int ln = cur_line + 1;
        char tmp[12]; int t = 0;
        if (ln == 0) tmp[t++] = '0';
        while (ln) { tmp[t++] = (char)('0' + (ln % 10)); ln /= 10; }
        while (t > 0) inf[p++] = tmp[--t];
        inf[p++] = '/';
        int ttl = total_lines;
        t = 0;
        if (ttl == 0) tmp[t++] = '0';
        while (ttl) { tmp[t++] = (char)('0' + (ttl % 10)); ttl /= 10; }
        while (t > 0) inf[p++] = tmp[--t];
        inf[p] = 0;
        fb_draw_string(cx + 8 + (i + 1) * 8, sy + 7, inf, CLR_STATUSFG, CLR_STATUS);
    }

    const char *right = 0;
    uint32_t rcol = CLR_STATUSFG;
    if (e->status_msg == 1) { right = "saved"; rcol = CLR_OK; }
    if (e->status_msg == 2) { right = "error"; rcol = CLR_ERR; }
    if (right) {
        int rlen = str_len(right);
        int rx = cx + cw - 8 - rlen * 8 - sb_w - 4;
        fb_draw_string(rx, sy + 7, right, rcol, CLR_STATUS);
    }
}

void editor_key(editor_t *e, char c) {
    // Ctrl+S — сохранить
    if (c == 0x18) { editor_save(e); return; }
    // Ctrl+Q — закрыть
    if (c == 0x19) { editor_close(e); return; }

    // Ctrl+C — копировать строку
    if (c == 0x1A) {
        uint32_t ls = e->cursor;
        while (ls > 0 && e->buf[ls-1] != '\n') ls--;
        uint32_t le = e->cursor;
        while (le < e->len && e->buf[le] != '\n') le++;
        if (le > ls) {
            clip_set(e->buf + ls, (int)(le - ls));
            e->status_msg = 1;
        }
        return;
    }

    // Ctrl+V — вставить
    if (c == 0x1B) {
        int plen = 0;
        const char *clip = clip_get(&plen);
        if (plen <= 0) return;
        if (e->len + (uint32_t)plen >= EDIT_MAX) return;
        for (uint32_t i = e->len; i > e->cursor; i--)
            e->buf[i + plen - 1] = e->buf[i - 1];
        for (int i = 0; i < plen; i++)
            e->buf[e->cursor + i] = clip[i];
        e->len += (uint32_t)plen;
        e->cursor += (uint32_t)plen;
        e->buf[e->len] = 0;
        e->dirty = 1;
        editor_scroll_to_cursor(e);
        return;
    }

    // Backspace (UTF-8 aware)
    if (c == '\b') {
        if (e->cursor > 0) {
            uint32_t prev = u8_prev(e->buf, e->cursor);
            uint32_t n = e->cursor - prev;
            for (uint32_t i = prev; i + n < e->len; i++)
                e->buf[i] = e->buf[i + n];
            e->len -= n;
            e->cursor = prev;
            e->buf[e->len] = 0;
            e->dirty = 1;
        }
        editor_scroll_to_cursor(e);
        return;
    }

    // DEL (UTF-8 aware)
    if (c == 0x17) {
        if (e->cursor < e->len) {
            uint32_t next = u8_next(e->buf, e->len, e->cursor);
            uint32_t n = next - e->cursor;
            for (uint32_t i = e->cursor; i + n < e->len; i++)
                e->buf[i] = e->buf[i + n];
            e->len -= n;
            e->buf[e->len] = 0;
            e->dirty = 1;
        }
        editor_scroll_to_cursor(e);
        return;
    }

    if (c == 0x13) { if (e->cursor > 0) e->cursor = u8_prev(e->buf, e->cursor); editor_scroll_to_cursor(e); return; }
    if (c == 0x14) { if (e->cursor < e->len) e->cursor = u8_next(e->buf, e->len, e->cursor); editor_scroll_to_cursor(e); return; }

    if (c == 0x15) {
        uint32_t p = e->cursor;
        while (p > 0 && e->buf[p-1] != '\n') p--;
        e->cursor = p; editor_scroll_to_cursor(e); return;
    }
    if (c == 0x16) {
        uint32_t p = e->cursor;
        while (p < e->len && e->buf[p] != '\n') p++;
        e->cursor = p; editor_scroll_to_cursor(e); return;
    }
    if (c == 0x11) {
        uint32_t ls = e->cursor;
        while (ls > 0 && e->buf[ls-1] != '\n') ls--;
        if (ls == 0) { editor_scroll_to_cursor(e); return; }
        uint32_t pe = ls - 1, ps = pe;
        while (ps > 0 && e->buf[ps-1] != '\n') ps--;
        uint32_t col = e->cursor - ls, plen = pe - ps;
        if (col > plen) col = plen;
        e->cursor = ps + col; editor_scroll_to_cursor(e); return;
    }
    if (c == 0x12) {
        uint32_t ls = e->cursor;
        while (ls > 0 && e->buf[ls-1] != '\n') ls--;
        uint32_t le = e->cursor;
        while (le < e->len && e->buf[le] != '\n') le++;
        if (le == e->len) { editor_scroll_to_cursor(e); return; }
        uint32_t ns = le + 1, ne = ns;
        while (ne < e->len && e->buf[ne] != '\n') ne++;
        uint32_t col = e->cursor - ls, nlen = ne - ns;
        if (col > nlen) col = nlen;
        e->cursor = ns + col; editor_scroll_to_cursor(e); return;
    }

    if ((unsigned char)c < 0x20 && c != '\n') return;
    if (e->len >= EDIT_MAX - 1) return;
    for (uint32_t i = e->len; i > e->cursor; i--)
        e->buf[i] = e->buf[i - 1];
    e->buf[e->cursor] = c;
    e->len++; e->cursor++;
    e->buf[e->len] = 0;
    e->dirty = 1;
    editor_scroll_to_cursor(e);
}

int editor_click(editor_t *e, int mx, int my,
                 int cx, int cy, int cw, int ch) {
    int status_h = 22, sb_w = 10;
    int sb_x = cx + cw - sb_w - 2, sb_y = cy + 4;
    int sb_h = ch - status_h - 8;
    if (sb_h < 20) sb_h = 20;
    if (mx < sb_x || mx >= sb_x + sb_w || my < sb_y || my >= sb_y + sb_h) return 0;
    int total_lines = editor_total_lines(e);
    int rows = e->visible_rows;
    if (rows < 1) rows = 1;
    int max_scroll = total_lines - rows;
    if (max_scroll <= 0) return 1;
    int y = my - sb_y;
    int new_scroll = max_scroll * y / sb_h;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    e->scroll_line = new_scroll;
    return 1;
}