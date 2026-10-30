// BrainixKernel — tcp.c

#include <stdint.h>
#include "tcp.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern int  net_send_tcp(const uint8_t src_ip[4], uint16_t src_port,
                         const uint8_t dst_ip[4], uint16_t dst_port,
                         uint32_t seq, uint32_t ack, uint8_t flags,
                         const void *payload, uint32_t payload_len);

static tcp_conn_t g_conns[TCP_MAX];
static uint32_t g_isn = 100000;
static uint32_t g_now = 0;

int tcp_alloc(void) {
    for (int i = 0; i < TCP_MAX; i++) {
        if (!g_conns[i].used) {
            tcp_conn_t *c = &g_conns[i];
            for (uint32_t k = 0; k < sizeof(tcp_conn_t); k++) ((uint8_t *)c)[k] = 0;
            c->used = 1;
            c->refs = 1;
            c->state = TCP_CLOSED;
            return i;
        }
    }
    return -1;
}

void tcp_ref(int idx) {
    if (idx < 0 || idx >= TCP_MAX || !g_conns[idx].used) return;
    if (g_conns[idx].refs != UINT16_MAX) g_conns[idx].refs++;
}

void tcp_free(int idx) {
    if (idx < 0 || idx >= TCP_MAX) return;
    tcp_conn_t *c = &g_conns[idx];
    if (!c->used || c->refs == 0) return;
    if (--c->refs == 0) {
        tcp_close(c);
        c->used = 0;
    }
}

tcp_conn_t *tcp_get(int idx) {
    if (idx < 0 || idx >= TCP_MAX) return 0;
    if (!g_conns[idx].used) return 0;
    return &g_conns[idx];
}

int tcp_send_syn(tcp_conn_t *c) {
    if (!c) return 0;
    c->syn_seq = g_isn;
    g_isn += 64000;
    c->seq = c->syn_seq;
    c->ack = 0;

    int r = net_send_tcp(c->local_ip, c->local_port,
                         c->remote_ip, c->remote_port,
                         c->seq, 0, TCP_SYN, 0, 0);
    if (!r) return 0;

    c->state = TCP_SYN_SENT;
    c->retransmit_at = g_now + 60;
    c->retransmit_cnt = 0;
    return 1;
}

static int tcp_send_ack(tcp_conn_t *c) {
    return net_send_tcp(c->local_ip, c->local_port,
                        c->remote_ip, c->remote_port,
                        c->seq, c->ack, TCP_ACK, 0, 0);
}

int tcp_send_data(tcp_conn_t *c, const void *buf, uint32_t len) {
    if (!c || !c->used) return 0;
    if (c->state != TCP_ESTABLISHED) return 0;
    if (len == 0) return 0;
    if (len > 1400) len = 1400;

    int r = net_send_tcp(c->local_ip, c->local_port,
                         c->remote_ip, c->remote_port,
                         c->seq, c->ack, TCP_PSH | TCP_ACK, buf, len);
    if (!r) return 0;

    c->seq += len;
    return (int)len;
}

uint32_t tcp_recv_data(tcp_conn_t *c, void *buf, uint32_t max) {
    if (!c || !c->used) return 0;
    if (c->rx_len == 0) return 0;

    uint32_t n = c->rx_len < max ? c->rx_len : max;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < n; i++) dst[i] = c->rx_buf[i];

    uint32_t rem = c->rx_len - n;
    for (uint32_t i = 0; i < rem; i++) c->rx_buf[i] = c->rx_buf[i + n];
    c->rx_len = rem;
    return n;
}

void tcp_close(tcp_conn_t *c) {
    if (!c || !c->used) return;
    if (c->state == TCP_ESTABLISHED) {
        net_send_tcp(c->local_ip, c->local_port,
                     c->remote_ip, c->remote_port,
                     c->seq, c->ack, TCP_FIN | TCP_ACK, 0, 0);
        c->seq += 1;
        c->state = TCP_FIN_WAIT;
    }
}

int tcp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len) {
    if (ip_total_len < 20) return 0;

    uint32_t ihl = (iph[0] & 0x0F) * 4;
    if (ihl < 20 || ihl + 20 > ip_total_len) return 0;

    const uint8_t *tcph = iph + ihl;
    uint16_t src_port = ((uint16_t)tcph[0] << 8) | tcph[1];
    uint16_t dst_port = ((uint16_t)tcph[2] << 8) | tcph[3];
    (void)dst_port;

    uint32_t seq = ((uint32_t)tcph[4] << 24) | ((uint32_t)tcph[5] << 16) |
                   ((uint32_t)tcph[6] << 8)  | (uint32_t)tcph[7];
    uint32_t ack = ((uint32_t)tcph[8] << 24) | ((uint32_t)tcph[9] << 16) |
                   ((uint32_t)tcph[10] << 8) | (uint32_t)tcph[11];

    uint8_t data_off = (tcph[12] >> 4) * 4;
    if (data_off < 20) return 0;
    if (ihl + 20 > ip_total_len) return 0;

    uint8_t flags = tcph[13];

    // Сколько данных.
    uint32_t payload_len = 0;
    if (ip_total_len > ihl + data_off) payload_len = ip_total_len - ihl - data_off;
    const uint8_t *payload = tcph + data_off;

    const uint8_t *rip = iph + 12;

    for (int i = 0; i < TCP_MAX; i++) {
        tcp_conn_t *c = &g_conns[i];
        if (!c->used) continue;
        if (c->remote_port != src_port) continue;
        if (c->remote_ip[0] != rip[0] || c->remote_ip[1] != rip[1] ||
            c->remote_ip[2] != rip[2] || c->remote_ip[3] != rip[3]) continue;

        (void)ack;

        if (c->state == TCP_SYN_SENT) {
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                c->ack = seq + 1;
                c->seq = c->syn_seq + 1;
                c->retransmit_at = 0;

                tcp_send_ack(c);
                c->state = TCP_ESTABLISHED;
                uart_puts("[tcp] ESTABLISHED\n");
                return 1;
            }
            if (flags & TCP_RST) {
                c->state = TCP_CLOSED;
                return 1;
            }
            return 1;
        }

        if (c->state == TCP_ESTABLISHED) {
            // Данные?
            if (payload_len > 0 && seq == c->ack) {
                uint32_t space = TCP_RX_BUF_SIZE - c->rx_len;
                uint32_t cp = payload_len < space ? payload_len : space;
                for (uint32_t k = 0; k < cp; k++) c->rx_buf[c->rx_len + k] = payload[k];
                c->rx_len += cp;
                c->ack += payload_len;
                tcp_send_ack(c);
            } else if (payload_len > 0) {
                // Out-of-order — просто ре-ACK, чтобы сервер переслал.
                tcp_send_ack(c);
            }

            // FIN от сервера?
            if (flags & TCP_FIN) {
                c->ack += 1;
                c->peer_closed = 1;
                tcp_send_ack(c);
            }

            if (flags & TCP_RST) {
                c->state = TCP_CLOSED;
                c->peer_closed = 1;
            }
            return 1;
        }

        if (c->state == TCP_FIN_WAIT) {
            if ((flags & TCP_ACK) && ack == c->seq) {
                c->state = TCP_CLOSED;
            }
            return 1;
        }
    }

    return 0;
}

void tcp_tick(uint32_t now_ticks) {
    g_now = now_ticks;
    for (int i = 0; i < TCP_MAX; i++) {
        tcp_conn_t *c = &g_conns[i];
        if (!c->used) continue;

        if (c->state == TCP_SYN_SENT && c->retransmit_at != 0 &&
            now_ticks >= c->retransmit_at)
        {
            if (c->retransmit_cnt >= 5) {
                c->state = TCP_CLOSED;
                continue;
            }
            c->retransmit_cnt++;
            c->retransmit_at = now_ticks + 60 * (1 << c->retransmit_cnt);
            net_send_tcp(c->local_ip, c->local_port,
                         c->remote_ip, c->remote_port,
                         c->syn_seq, 0, TCP_SYN, 0, 0);
        }
    }
}
