// BrainixKernel (BKOS) — mbuf.c
#include "mbuf.h"
#include "heap.h"

extern void uart_puts(const char *s);

struct mbuf *m_get(uint16_t size) {
    // Выделяем структуру и данные одним куском.
    // В FreeBSD это разнесено (mhdr + mclust), но у нас kmalloc умеет выделять блоки.
    struct mbuf *m = (struct mbuf *)kmalloc(sizeof(struct mbuf) + size);
    if (!m) return 0;

    m->m_next = 0;
    m->m_data = (uint8_t *)m + sizeof(struct mbuf);
    m->m_len  = 0;
    m->m_cap  = size;
    return m;
}

void m_freem(struct mbuf *m) {
    while (m) {
        struct mbuf *next = m->m_next;
        kfree(m);
        m = next;
    }
}

int m_append(struct mbuf *m, const void *data, uint16_t len) {
    if (!m || len == 0) return 0;

    // Ищем последний mbuf в цепочке
    struct mbuf *last = m;
    while (last->m_next) last = last->m_next;

    // Если в последнем не хватает места — создаём новый
    if (last->m_len + len > last->m_cap) {
        struct mbuf *new_m = m_get(len > 2048 ? len : 2048); // Дефолтный размер кластера 2КБ
        if (!new_m) return 0;
        last->m_next = new_m;
        last = new_m;
    }

    // Копируем данные
    const uint8_t *src = (const uint8_t *)data;
    for (uint16_t i = 0; i < len; i++) {
        last->m_data[last->m_len + i] = src[i];
    }
    last->m_len += len;
    return 1;
}

uint32_t m_length(struct mbuf *m) {
    uint32_t total = 0;
    while (m) {
        total += m->m_len;
        m = m->m_next;
    }
    return total;
}
