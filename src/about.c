#include "version.h"
// BrainixKernel — about.c
// Окно "About This System" — macOS Big Sur style with RAM donut.

#include <stdint.h>
#include "about.h"
#include "fb.h"
#include "font8x16_aa.h"
#include "physmem.h"
#include "blk.h"
#include "kbd.h"

#define CLR_BG            0xFF1A1A1E
#define CLR_FG            0xFFFFFFFF
#define CLR_FG_DIM        0xFFA8A8B0
#define CLR_SEP           0xFF2E2E36
#define CLR_ACCENT        0xFF4A90E2
#define CLR_ACCENT_D      0xFF1E4480
#define CLR_LOGO_LIGHT    0xFF5AB0F0
#define CLR_DONUT_USED    0xFF5AA8F0
#define CLR_DONUT_USED_H  0xFF88C8FF
#define CLR_DONUT_FREE    0xFF2E3A4A
#define CLR_BTN           0xFF3A80D0
#define CLR_BTN_HOVER     0xFF5AA0E8

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int u8_len(const char *s) {
    int n = 0;
    while (*s) {
        if (((uint8_t)*s & 0xC0) != 0x80) n++;
        s++;
    }
    return n;
}

static void u64_to_str(uint64_t v, char *out, int max) {
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (t > 0 && i < max - 1) out[i++] = tmp[--t];
    out[i] = 0;
}

static void fmt_bytes(uint64_t bytes, char *out, int max) {
    char num[24];
    uint64_t gb = bytes / (1024ULL * 1024 * 1024);
    uint64_t mb = bytes / (1024ULL * 1024);
    uint64_t kb = bytes / 1024ULL;

    if (gb >= 1) {
        uint64_t gb_10 = (bytes * 10) / (1024ULL * 1024 * 1024);
        u64_to_str(gb_10 / 10, num, sizeof(num));
        int L = str_len(num);
        int i = 0;
        for (int j = 0; j < L && i < max - 6; j++) out[i++] = num[j];
        out[i++] = '.';
        out[i++] = (char)('0' + (gb_10 % 10));
        out[i++] = ' '; out[i++] = 'G'; out[i++] = 'B';
        out[i] = 0;
        return;
    }
    if (mb >= 1) {
        u64_to_str(mb, num, sizeof(num));
        int i = 0;
        for (int j = 0; num[j] && i < max - 4; j++) out[i++] = num[j];
        out[i++] = ' '; out[i++] = 'M'; out[i++] = 'B';
        out[i] = 0;
        return;
    }
    if (kb >= 1) {
        u64_to_str(kb, num, sizeof(num));
        int i = 0;
        for (int j = 0; num[j] && i < max - 4; j++) out[i++] = num[j];
        out[i++] = ' '; out[i++] = 'K'; out[i++] = 'B';
        out[i] = 0;
        return;
    }
    u64_to_str(bytes, num, sizeof(num));
    int i = 0;
    for (int j = 0; num[j] && i < max - 3; j++) out[i++] = num[j];
    out[i++] = ' '; out[i++] = 'B';
    out[i] = 0;
}

static int append(char *buf, int bi, const char *s) {
    for (int j = 0; s[j]; j++) buf[bi++] = s[j];
    return bi;
}

// ============================================================
// Bhaskara approximation for sin/cos (integer, no libm)
// sin(deg) для deg 0..180 → значение × 1000
// ============================================================
static int isin(int deg) {
    int sign = 1;
    if (deg > 180) { deg -= 180; sign = -1; }
    if (deg < 0)   { deg = -deg; sign = -1; }
    int num = 4 * deg * (180 - deg);
    int den = 40500 - deg * (180 - deg);
    if (den == 0) return sign * 1000;
    return sign * (num * 1000 / den);
}
static int icos(int deg) { return isin(deg + 90); }

// ============================================================
// Круговая диаграмма ОЗУ (donut)
// ============================================================
static void draw_ram_donut(int cx, int cy, int r_out, int r_in, int pct) {
    // Фон кольца
    for (int y = -r_out; y <= r_out; y++) {
        for (int x = -r_out; x <= r_out; x++) {
            int d2 = x*x + y*y;
            if (d2 > r_out*r_out) continue;
            if (d2 < r_in*r_in) continue;
            // Мягкая внутренняя/внешняя кромка (AA)
            fb_putpixel(cx + x, cy + y, CLR_DONUT_FREE);
        }
    }

    // "Used" часть от 0° (вверх) по часовой
    int deg_end = pct * 360 / 100;
    for (int deg = 0; deg < deg_end; deg++) {
        int dx = isin(deg);
        int dy = -icos(deg);
        for (int r = r_in; r <= r_out; r++) {
            int px = cx + dx * r / 1000;
            int py = cy + dy * r / 1000;
            fb_putpixel(px, py, CLR_DONUT_USED);
        }
    }

    // Внутренний/внешний AA-контур (очень тонкий)
    for (int deg = 0; deg < 360; deg++) {
        int dx = isin(deg);
        int dy = -icos(deg);
        fb_putpixel(cx + dx * r_out / 1000, cy + dy * r_out / 1000, 0xFF3A4550);
        fb_putpixel(cx + dx * r_in  / 1000, cy + dy * r_in  / 1000, 0xFF3A4550);
    }
}

// ============================================================
// Логотип
// ============================================================
static void draw_big_logo(int cx, int cy, int r) {
    fb_blend_round_rect_aa(cx - r, cy - r + 5, r*2, r*2, r, 0xFF000000, 90);
    fb_fill_circle_aa(cx, cy, r, CLR_ACCENT_D);
    fb_fill_circle_aa(cx,     cy - 1, r - 1, CLR_ACCENT);
    fb_fill_circle_aa(cx - 2, cy - 3, r - 4, 0xFF3A88C8);
    fb_fill_circle_aa(cx - 4, cy - 6, r - 10, CLR_LOGO_LIGHT);

    // Спекулярный блик
    fb_fill_circle_aa(cx - (r * 3) / 10, cy - (r * 4) / 10, r / 5, 0xFFC8E8FF);
    fb_fill_circle_aa(cx - (r * 3) / 10, cy - (r * 4) / 10, r / 10, 0xFFFFFFFF);

    // Стойка B
    int bw = (r * 10) / 48;
    int bh = (r * 52) / 48;
    int bx = cx - (r * 15) / 48;
    int by = cy - bh / 2;
    fb_fill_round_rect_aa(bx, by, bw, bh, bw / 3, 0xFFFFFFFF);

    // Верхняя дуга
    int arc_r = (r * 14) / 48;
    int arc_dx = (r * 4) / 48;
    int arc_dy = (r * 13) / 48;
    fb_fill_circle_aa(cx + arc_dx, cy - arc_dy, arc_r, 0xFFFFFFFF);
    fb_fill_circle_aa(cx + arc_dx, cy - arc_dy, arc_r / 2, CLR_ACCENT_D);
    // Нижняя дуга
    fb_fill_circle_aa(cx + arc_dx, cy + arc_dy, arc_r, 0xFFFFFFFF);
    fb_fill_circle_aa(cx + arc_dx, cy + arc_dy, arc_r / 2, CLR_ACCENT_D);

    // Перекрываем стойкой левые края дуг
    fb_fill_round_rect_aa(bx, by, bw + 2, bh, bw / 3, 0xFFFFFFFF);
    fb_fill_rect(cx + arc_r + arc_dx - 2, cy - 5, 2, 10, 0xFFFFFFFF);
}

static void hr(int x, int y, int w) {
    fb_blend_rect(x, y, w, 1, CLR_SEP, 220);
}
static void section(int x, int y, const char *text) {
    fb_draw_string_aa(x, y, text, CLR_FG_DIM, CLR_BG);
}
static void row(int x, int y, int w, const char *label, const char *value) {
    fb_draw_string_aa(x, y, label, CLR_FG_DIM, CLR_BG);
    int vpx = u8_len(value) * 8;
    fb_draw_string_aa(x + w - vpx, y, value, CLR_FG, CLR_BG);
}

// ============================================================
// Координаты кнопки System Report (для hit-test)
// ============================================================
#define BTN_W  200
#define BTN_H  28
#define BTN_MARGIN 24

static void btn_rect(int cx, int cy, int cw, int ch,
                     int *out_x, int *out_y) {
    *out_x = cx + (cw - BTN_W) / 2;
    *out_y = cy + ch - BTN_H - BTN_MARGIN;
}

int about_hit_report(int mx, int my, int cx, int cy, int cw, int ch) {
    int bx, by;
    btn_rect(cx, cy, cw, ch, &bx, &by);
    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) return 1;
    return 0;
}

// ============================================================
// Главная функция
// ============================================================
void about_draw_content(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, CLR_BG);

    // ===================== HEADER (logo + title) =====================
    int logo_cx = cx + 56;
    int logo_cy = cy + 56;
    draw_big_logo(logo_cx, logo_cy, 40);

    fb_draw_string_aa(cx + 116, cy + 36, "BrainixKernel OS", CLR_FG, CLR_BG);
    fb_draw_string_aa(cx + 116, cy + 60, "Версия " BRAINIX_VERSION, CLR_FG_DIM, CLR_BG);
    fb_draw_string_aa(cx + 116, cy + 80, "ARM64, Brainix v" BRAINIX_VERSION, CLR_FG_DIM, CLR_BG);

    // ===================== DATA =====================
    uint64_t total_pages = physmem_total_pages();
    uint64_t free_pages  = physmem_free_pages();
    uint64_t used_pages  = total_pages - free_pages;

    uint64_t total_ram = total_pages * 4096ULL;
    uint64_t free_ram  = free_pages  * 4096ULL;
    uint64_t used_ram  = used_pages  * 4096ULL;

    int used_pct = 0;
    if (total_pages > 0)
        used_pct = (int)((used_pages * 100) / total_pages);

    uint64_t disk_sectors = blk_capacity();
    uint64_t disk_bytes   = disk_sectors * 512ULL;

    char total_ram_s[24], free_ram_s[24], used_ram_s[24];
    char disk_s[24], pages_s[24], w_s[12], h_s[12];
    char pct_s[8];

    fmt_bytes(total_ram, total_ram_s, sizeof(total_ram_s));
    fmt_bytes(free_ram,  free_ram_s,  sizeof(free_ram_s));
    fmt_bytes(used_ram,  used_ram_s,  sizeof(used_ram_s));
    fmt_bytes(disk_bytes, disk_s,     sizeof(disk_s));
    u64_to_str(total_pages, pages_s,  sizeof(pages_s));
    u64_to_str(fb_width(),  w_s,      sizeof(w_s));
    u64_to_str(fb_height(), h_s,      sizeof(h_s));
    u64_to_str((uint64_t)used_pct, pct_s, sizeof(pct_s));

    // ===================== PANEL =====================
    int px = cx + 24;
    int pw = cw - 48;
    int py = cy + 118;

    int row_h   = 22;
    int title_h = 20;

    // --- Память + Donut ---
    section(px, py, "Память"); py += title_h;
    row(px, py, pw - 160, "ОЗУ всего",    total_ram_s); py += row_h;
    row(px, py, pw - 160, "Использовано", used_ram_s);  py += row_h;
    row(px, py, pw - 160, "Свободно",     free_ram_s);  py += row_h;

    // Donut справа от секции памяти
    int donut_cx = px + pw - 60;
    int donut_cy = py - row_h * 2 + 6;   // визуально центрируем на трёх строках
    draw_ram_donut(donut_cx, donut_cy, 44, 30, used_pct);

    // Процент в центре
    {
        char pbuf[8];
        int i = 0;
        for (int j = 0; pct_s[j]; j++) pbuf[i++] = pct_s[j];
        pbuf[i++] = '%';
        pbuf[i] = 0;
        int tw = u8_len(pbuf) * 8;
        fb_draw_string_aa(donut_cx - tw / 2, donut_cy - 8, pbuf, CLR_FG, CLR_BG);
    }

    // "Страниц"
    {
        char buf[64]; int bi = 0;
        bi = append(buf, bi, pages_s);
        buf[bi++] = (char)0xC3; buf[bi++] = (char)0x97;
        bi = append(buf, bi, " 4 KB");
        buf[bi] = 0;
        row(px, py, pw - 160, "Страниц", buf);
        py += row_h;
    }

    // Пустая строка под donut, потом разделитель
    if (py < donut_cy + 56) py = donut_cy + 56;
    py += 8; hr(px, py, pw); py += 12;

    // --- О системе ---
    section(px, py, "О системе"); py += title_h;
    row(px, py, pw, "Модель",       "BrainixStation M1"); py += row_h;
    row(px, py, pw, "Процессор",    "ARM Cortex-A72");    py += row_h;
    row(px, py, pw, "Архитектура",  "64-bit (ARMv8-A)");  py += row_h;
    py += 4; hr(px, py, pw); py += 12;

    // --- Устройства ---
    section(px, py, "Устройства"); py += title_h;
    {
        char buf[64]; int bi = 0;
        bi = append(buf, bi, disk_s);
        bi = append(buf, bi, " (");
        char sec_str[24];
        u64_to_str(disk_sectors, sec_str, sizeof(sec_str));
        bi = append(buf, bi, sec_str);
        bi = append(buf, bi, " секторов)");
        buf[bi] = 0;
        row(px, py, pw, "Накопитель", buf);
        py += row_h;
    }
    row(px, py, pw, "Файловая система", "BKFS"); py += row_h;
    {
        char buf[32]; int bi = 0;
        bi = append(buf, bi, w_s);
        buf[bi++] = (char)0xC3; buf[bi++] = (char)0x97;
        bi = append(buf, bi, " ");
        bi = append(buf, bi, h_s);
        buf[bi] = 0;
        row(px, py, pw, "Дисплей", buf);
        py += row_h;
    }
    py += 4; hr(px, py, pw); py += 12;

    // --- Региональные ---
    section(px, py, "Региональные"); py += title_h;
    row(px, py, pw, "Раскладка",
        kbd_layout_ru() ? "Русская (F12)" : "English (F12)"); py += row_h;
    row(px, py, pw, "Часовой пояс", "UTC+3");

    // ===================== System Report button =====================
    int bx, by;
    btn_rect(cx, cy, cw, ch, &bx, &by);
    fb_fill_round_rect_aa(bx, by, BTN_W, BTN_H, 8, CLR_BTN);
    fb_blend_rect(bx + 4, by + 1, BTN_W - 8, 1, 0xFFFFFFFF, 60);
    const char *label = "System Report";
    int lw = u8_len(label) * 8;
    fb_draw_string_aa(bx + (BTN_W - lw) / 2, by + 6, label, CLR_FG, CLR_BTN);
}