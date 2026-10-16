// BrainixKernel (BKOS) — ksh.c
// Кольцевой буфер вывода для user-программ, работающих в окне.

#include <stdint.h>
#include "ksh.h"

#define KSH_BUF_SIZE 4096
static char     g_buf[KSH_BUF_SIZE];
static uint32_t g_head = 0;
static uint32_t g_tail = 0;

void ksh_init(void) {
    g_head = 0;
    g_tail = 0;
}

void ksh_out_write(const char *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (g_head + 1) % KSH_BUF_SIZE;
        if (next == g_tail) return;    // полный — дропаем
        g_buf[g_head] = data[i];
        g_head = next;
    }
}

int ksh_out_pop_line(char *out, int max) {
    if (g_head == g_tail) return 0;

    uint32_t p = g_tail;
    int n = 0;
    while (p != g_head) {
        char c = g_buf[p];
        if (c == '\n') {
            out[n] = 0;
            g_tail = (p + 1) % KSH_BUF_SIZE;
            return 1;
        }
        if (n < max - 1) out[n++] = c;
        p = (p + 1) % KSH_BUF_SIZE;
    }
    return 0;
}

void ksh_out_flush_partial(void) {
    if (g_head == g_tail) return;
    // Просто добавим перевод строки в конец, чтобы pop_line его выдал.
    char nl = '\n';
    ksh_out_write(&nl, 1);
}