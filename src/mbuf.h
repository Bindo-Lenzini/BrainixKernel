// BrainixKernel — mbuf.h
// Вдохновлено FreeBSD, но без легаси: только цепочка, len, cap.
#ifndef BRAINIX_MBUF_H
#define BRAINIX_MBUF_H

#include <stdint.h>

struct mbuf {
    struct mbuf *m_next;    // Следующий буфер в цепочке
    uint8_t     *m_data;    // Указатель на начало данных
    uint16_t     m_len;     // Сколько данных реально записано
    uint16_t     m_cap;     // Емкость этого буфера
};

// Выделить mbuf с полезной нагрузкой size байт.
struct mbuf *m_get(uint16_t size);

// Освободить всю цепочку, начиная с m.
void m_freem(struct mbuf *m);

// Дописать данные в конец цепочки. Если в последнем mbuf не хватает места,
// выделит новый и свяжет его.
int m_append(struct mbuf *m, const void *data, uint16_t len);

// Возвращает общую длину всех данных в цепочке.
uint32_t m_length(struct mbuf *m);

#endif
