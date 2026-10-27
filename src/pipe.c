// BrainixKernel — pipe.c

#include <stdint.h>
#include "pipe.h"

typedef struct {
    int      used;
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t head;      // индекс чтения
    uint32_t tail;      // индекс записи
    int      readers;
    int      writers;
} pipe_t;

static pipe_t g_pipes[PIPE_MAX];

int pipe_alloc(void) {
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!g_pipes[i].used) {
            g_pipes[i].used    = 1;
            g_pipes[i].head    = 0;
            g_pipes[i].tail    = 0;
            g_pipes[i].readers = 0;
            g_pipes[i].writers = 0;
            return i;
        }
    }
    return -1;
}

void pipe_ref_read(int idx) {
    if (idx < 0 || idx >= PIPE_MAX) return;
    g_pipes[idx].readers++;
}

void pipe_ref_write(int idx) {
    if (idx < 0 || idx >= PIPE_MAX) return;
    g_pipes[idx].writers++;
}

// Закрытие концов вызывается через sys_close.
// Их реализует pipe_close_read / pipe_close_write — чтобы декрементить
// и освобождать pipe.
void pipe_close_read(int idx) {
    if (idx < 0 || idx >= PIPE_MAX) return;
    if (g_pipes[idx].readers > 0) g_pipes[idx].readers--;
    if (g_pipes[idx].readers == 0 && g_pipes[idx].writers == 0) {
        g_pipes[idx].used = 0;
    }
}

void pipe_close_write(int idx) {
    if (idx < 0 || idx >= PIPE_MAX) return;
    if (g_pipes[idx].writers > 0) g_pipes[idx].writers--;
    if (g_pipes[idx].readers == 0 && g_pipes[idx].writers == 0) {
        g_pipes[idx].used = 0;
    }
}

int pipe_read(int idx, void *buf, uint32_t max) {
    if (idx < 0 || idx >= PIPE_MAX) return -1;
    pipe_t *p = &g_pipes[idx];
    if (!p->used) return -1;

    uint32_t avail = (p->tail + PIPE_BUF_SIZE - p->head) % PIPE_BUF_SIZE;
    if (avail == 0) {
        if (p->writers == 0) return 0;   // EOF
        return -1;                        // EAGAIN
    }

    uint32_t n = (max < avail) ? max : avail;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
    }
    return (int)n;
}

int pipe_write(int idx, const void *buf, uint32_t len) {
    if (idx < 0 || idx >= PIPE_MAX) return -1;
    pipe_t *p = &g_pipes[idx];
    if (!p->used) return -1;
    if (p->readers == 0) return -1;

    uint32_t space = PIPE_BUF_SIZE - 1 -
                     ((p->tail + PIPE_BUF_SIZE - p->head) % PIPE_BUF_SIZE);
    if (space == 0) return -1;   // полный

    uint32_t n = (len < space) ? len : space;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < n; i++) {
        p->buf[p->tail] = src[i];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
    }
    return (int)n;
}
