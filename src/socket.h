// BrainixKernel — socket.h
// Примитивная абстракция UDP-сокета. Привязка к fd делается в syscall.c.

#ifndef BRAINIX_SOCKET_H
#define BRAINIX_SOCKET_H

#include <stdint.h>

#define SOCK_MAX        8
#define SOCK_RX_QUEUE   8
#define SOCK_BUF_SIZE   2048
#define SOCK_MAX_PACKET 1472   // безопасный UDP payload

typedef struct {
    uint8_t  src_ip[4];
    uint16_t src_port;      // network byte order
    uint16_t len;
    uint8_t  data[SOCK_BUF_SIZE];
} sock_pkt_t;

typedef struct {
    int      used;
    uint16_t local_port;    // network byte order

    sock_pkt_t rx[SOCK_RX_QUEUE];
    int      rx_head;
    int      rx_tail;
} socket_t;

int  sock_alloc(uint16_t local_port_be);
void sock_free(int idx);
socket_t *sock_get(int idx);
socket_t *sock_find_by_port(uint16_t port_be);

// Положить входящий пакет в очередь. Возвращает 1 при успехе.
int  sock_push_rx(socket_t *s, const uint8_t *src_ip, uint16_t src_port_be,
                  const uint8_t *data, uint16_t len);

#endif
