// BrainixKernel (BKOS) — socket.c

#include <stdint.h>
#include "socket.h"

static socket_t g_socks[SOCK_MAX];

int sock_alloc(uint16_t local_port_be) {
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!g_socks[i].used) {
            g_socks[i].used       = 1;
            g_socks[i].local_port = local_port_be;
            g_socks[i].rx_head    = 0;
            g_socks[i].rx_tail    = 0;
            return i;
        }
    }
    return -1;
}

void sock_free(int idx) {
    if (idx < 0 || idx >= SOCK_MAX) return;
    g_socks[idx].used = 0;
}

socket_t *sock_get(int idx) {
    if (idx < 0 || idx >= SOCK_MAX) return 0;
    if (!g_socks[idx].used) return 0;
    return &g_socks[idx];
}

socket_t *sock_find_by_port(uint16_t port_be) {
    for (int i = 0; i < SOCK_MAX; i++) {
        if (g_socks[i].used && g_socks[i].local_port == port_be)
            return &g_socks[i];
    }
    return 0;
}

int sock_push_rx(socket_t *s, const uint8_t *src_ip, uint16_t src_port_be,
                 const uint8_t *data, uint16_t len)
{
    if (!s) return 0;
    int next = (s->rx_tail + 1) % SOCK_RX_QUEUE;
    if (next == s->rx_head) return 0;   // очередь полна

    sock_pkt_t *p = &s->rx[s->rx_tail];
    for (int i = 0; i < 4; i++) p->src_ip[i] = src_ip[i];
    p->src_port = src_port_be;
    p->len = len > SOCK_BUF_SIZE ? SOCK_BUF_SIZE : len;
    for (uint16_t i = 0; i < p->len; i++) p->data[i] = data[i];
    s->rx_tail = next;
    return 1;
}
