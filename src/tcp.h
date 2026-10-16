// BrainixKernel (BKOS) — tcp.h
#ifndef BKOS_TCP_H
#define BKOS_TCP_H

#include <stdint.h>

#define TCP_MAX         4
#define TCP_RX_BUF_SIZE 8192

#define TCP_CLOSED      0
#define TCP_SYN_SENT    1
#define TCP_ESTABLISHED 2
#define TCP_FIN_WAIT    3
#define TCP_TIME_WAIT   4

#define TCP_FIN         0x01
#define TCP_SYN         0x02
#define TCP_RST         0x04
#define TCP_PSH         0x08
#define TCP_ACK         0x10

typedef struct {
    int      used;
    int      state;

    uint8_t  local_ip[4];
    uint16_t local_port;
    uint8_t  remote_ip[4];
    uint16_t remote_port;

    uint32_t seq;
    uint32_t ack;
    uint32_t syn_seq;

    uint32_t retransmit_at;
    int      retransmit_cnt;

    // Приёмный буфер (линейный).
    uint8_t  rx_buf[TCP_RX_BUF_SIZE];
    uint32_t rx_len;

    int      peer_closed;    // получили FIN от удалённой стороны
} tcp_conn_t;

int  tcp_alloc(void);
void tcp_free(int idx);
tcp_conn_t *tcp_get(int idx);

int  tcp_send_syn(tcp_conn_t *c);
int  tcp_send_data(tcp_conn_t *c, const void *buf, uint32_t len);
uint32_t tcp_recv_data(tcp_conn_t *c, void *buf, uint32_t max);
void tcp_close(tcp_conn_t *c);

int  tcp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len);
void tcp_tick(uint32_t now_ticks);

#endif
