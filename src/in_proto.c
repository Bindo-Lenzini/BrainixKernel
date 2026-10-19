#pragma clang diagnostic ignored "-Wunused-parameter"
#include "protosw.h"
#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "task.h"
#include "net.h"

/* Forward declarations */
int proto_tcp_close(void *ctx);
int proto_udp_close(void *ctx);


/* Внешние функции, которые мы используем */
extern ipv4_t net_get_ip(void);
extern int net_send_udp(ipv4_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *data, uint32_t len);
extern void net_poll(void);
extern void tcp_tick(uint32_t);

/* ================= TCP ОБЕРТКИ ================= */

int proto_tcp_attach(void *ctx) { return tcp_alloc(); }
void proto_tcp_detach(void *ctx) { proto_tcp_close(ctx); }

int proto_tcp_bind(void *ctx, struct sockaddr *sa) {
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    task_fd_t *f = (task_fd_t *)ctx;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c) return -1;
    c->local_port = sin->sin_port;
    return 0;
}

int proto_tcp_connect(void *ctx, struct sockaddr *sa) {
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    task_fd_t *f = (task_fd_t *)ctx;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c) return -1;
    
    ipv4_t my_ip = net_get_ip();
    c->local_ip[0] = my_ip.b[0]; c->local_ip[1] = my_ip.b[1];
    c->local_ip[2] = my_ip.b[2]; c->local_ip[3] = my_ip.b[3];
    c->local_port = 40002 + f->tcp_idx;
    
    uint8_t *a = (uint8_t *)&sin->sin_addr;
    c->remote_ip[0] = a[0]; c->remote_ip[1] = a[1];
    c->remote_ip[2] = a[2]; c->remote_ip[3] = a[3];
    c->remote_port = sin->sin_port;
    
    if (!tcp_send_syn(c)) return -1;
    static uint32_t fake_tick = 0;
    for (int i = 0; i < 5000; i++) {
        net_poll();
        tcp_tick(++fake_tick);
        if (c->state == TCP_ESTABLISHED) return 0;
        if (c->state == TCP_CLOSED) return -1;
        for (volatile int k = 0; k < 100000; k++) {}
    }
    return -1;
}

int proto_tcp_listen(void *ctx, int backlog) { return 0; }

int proto_tcp_send(void *ctx, const void *data, int len, struct sockaddr *to) {
    task_fd_t *f = (task_fd_t *)ctx;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c) return -1;
    return tcp_send_data(c, data, len);
}

int proto_tcp_recv(void *ctx, void *buf, int max_len, struct sockaddr *from) {
    task_fd_t *f = (task_fd_t *)ctx;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (!c || c->rx_len == 0) return 0;
    int copy_len = (int)c->rx_len > max_len ? max_len : (int)c->rx_len;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < copy_len; i++) dst[i] = c->rx_buf[i];
    for (int i = copy_len; i < (int)c->rx_len; i++) c->rx_buf[i - copy_len] = c->rx_buf[i];
    c->rx_len -= copy_len;
    
    struct sockaddr_in *sin = (struct sockaddr_in *)from;
    if (sin) {
        sin->sin_port = c->remote_port;
        uint8_t *a = (uint8_t *)&sin->sin_addr;
        a[0] = c->remote_ip[0]; a[1] = c->remote_ip[1]; a[2] = c->remote_ip[2]; a[3] = c->remote_ip[3];
    }
    return copy_len;
}

int proto_tcp_close(void *ctx) {
    task_fd_t *f = (task_fd_t *)ctx;
    tcp_conn_t *c = tcp_get(f->tcp_idx);
    if (c) { tcp_close(c); tcp_free(f->tcp_idx); }
    return 0;
}

void proto_tcp_input(void *pkt, int len) { tcp_handle_ipv4((const uint8_t *)pkt, len); }

/* ================= UDP ОБЕРТКИ ================= */

int proto_udp_attach(void *ctx) { return sock_alloc(0); }
void proto_udp_detach(void *ctx) { proto_udp_close(ctx); }

int proto_udp_bind(void *ctx, struct sockaddr *sa) {
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    task_fd_t *f = (task_fd_t *)ctx;
    socket_t *s = sock_get(f->sock_idx);
    if (!s) return -1;
    s->local_port = sin->sin_port;
    return 0;
}

int proto_udp_connect(void *ctx, struct sockaddr *sa) { return 0; }
int proto_udp_listen(void *ctx, int backlog) { return 0; }

int proto_udp_send(void *ctx, const void *data, int len, struct sockaddr *to) {
    struct sockaddr_in *sin = (struct sockaddr_in *)to;
    task_fd_t *f = (task_fd_t *)ctx;
    socket_t *s = sock_get(f->sock_idx);
    if (!s || s->local_port == 0) return -1;
    
    ipv4_t dst_ip;
    uint8_t *a = (uint8_t *)&sin->sin_addr;
    dst_ip.b[0] = a[0]; dst_ip.b[1] = a[1]; dst_ip.b[2] = a[2]; dst_ip.b[3] = a[3];
    
    if (!net_send_udp(dst_ip, s->local_port, sin->sin_port, data, (uint32_t)len)) return -1;
    return len;
}

int proto_udp_recv(void *ctx, void *buf, int max_len, struct sockaddr *from) {
    task_fd_t *f = (task_fd_t *)ctx;
    socket_t *s = sock_get(f->sock_idx);
    if (!s) return -1;
    if (s->rx_head == s->rx_tail) return 0;
    sock_pkt_t *p = &s->rx[s->rx_head];
    s->rx_head = (s->rx_head + 1) % SOCK_RX_QUEUE;
    
    int copy_len = p->len > max_len ? max_len : p->len;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < copy_len; i++) dst[i] = p->data[i];
    
    struct sockaddr_in *sin = (struct sockaddr_in *)from;
    if (sin) {
        sin->sin_port = p->src_port;
        uint8_t *a = (uint8_t *)&sin->sin_addr;
        a[0] = p->src_ip[0]; a[1] = p->src_ip[1]; a[2] = p->src_ip[2]; a[3] = p->src_ip[3];
    }
    return copy_len;
}

int proto_udp_close(void *ctx) {
    task_fd_t *f = (task_fd_t *)ctx;
    sock_free(f->sock_idx);
    return 0;
}

void proto_udp_input(void *pkt, int len) { udp_handle_ipv4((const uint8_t *)pkt, len); }

/* ================= ТАБЛИЦА ================= */
static struct protosw inetsw[] = {
    {
        .pr_type = SOCK_STREAM, .pr_protocol = IPPROTO_TCP, .pr_flags = PR_CONNREQUIRED,
        .pr_attach = proto_tcp_attach, .pr_detach = proto_tcp_detach,
        .pr_bind = proto_tcp_bind, .pr_connect = proto_tcp_connect, .pr_listen = proto_tcp_listen,
        .pr_send = proto_tcp_send, .pr_recv = proto_tcp_recv, .pr_close = proto_tcp_close,
        .pr_input = proto_tcp_input,
    },
    {
        .pr_type = SOCK_DGRAM, .pr_protocol = IPPROTO_UDP, .pr_flags = PR_ATOMIC | PR_ADDR,
        .pr_attach = proto_udp_attach, .pr_detach = proto_udp_detach,
        .pr_bind = proto_udp_bind, .pr_connect = proto_udp_connect, .pr_listen = proto_udp_listen,
        .pr_send = proto_udp_send, .pr_recv = proto_udp_recv, .pr_close = proto_udp_close,
        .pr_input = proto_udp_input,
    }
};

static struct domain inetdomain = {
    .dom_family = AF_INET, .dom_name = "internet", .dom_init = 0,
    .dom_protosw_count = sizeof(inetsw) / sizeof(struct protosw),
    .dom_protosw = inetsw, .dom_next = 0
};

void inetdomain_register(void) { domain_add(&inetdomain); }
