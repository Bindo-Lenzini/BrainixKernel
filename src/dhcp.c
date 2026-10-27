// BrainixKernel — dhcp.c
#include <stdint.h>
#include "dhcp.h"
#include "net.h"
#include "virtio_net.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern void uart_puthex(uint64_t v);

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define BOOTREQUEST 1
#define BOOTREPLY   2
#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5
#define MAGIC_COOKIE 0x63825363u

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
} bootp_t;

static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mem_zero(void *d, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) dd[i] = 0;
}
static uint16_t ip_cksum(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)p[i] << 8) | p[i + 1];
    if (len & 1) sum += (uint32_t)p[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}
static uint32_t g_xid = 0;
static uint32_t make_xid(void) {
    uint64_t t = 0;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
    uint32_t v = (uint32_t)(t ^ (t >> 32));
    if (v == 0) v = 0x12345678u;
    return v;
}
static uint32_t build_options(uint8_t *opt, uint8_t msg_type,
                              const uint8_t *server_id,
                              const uint8_t *requested_ip) {
    uint32_t i = 0;
    opt[i++] = 53; opt[i++] = 1; opt[i++] = msg_type;
    if (msg_type == DHCPREQUEST && server_id && requested_ip) {
        opt[i++] = 54; opt[i++] = 4;
        for (int k = 0; k < 4; k++) opt[i++] = server_id[k];
        opt[i++] = 50; opt[i++] = 4;
        for (int k = 0; k < 4; k++) opt[i++] = requested_ip[k];
    }
    opt[i++] = 55; opt[i++] = 5;
    opt[i++] = 1; opt[i++] = 3; opt[i++] = 6; opt[i++] = 51; opt[i++] = 54;
    opt[i++] = 255;
    return i;
}
static uint32_t build_dhcp_frame(uint8_t *frame, mac_t my_mac,
                                 uint8_t msg_type,
                                 const uint8_t *server_id,
                                 const uint8_t *requested_ip) {
    uint8_t bcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t *ip_start = net_build_eth(frame, bcast_mac, my_mac.b, ETH_TYPE_IPV4);
    uint8_t opts[64];
    uint32_t opt_len = build_options(opts, msg_type, server_id, requested_ip);
    uint32_t bootp_len = sizeof(bootp_t) + opt_len;
    uint32_t udp_len = 8 + bootp_len;
    uint32_t ip_len  = 20 + udp_len;
    uint8_t *iph = ip_start;
    iph[0] = 0x45; iph[1] = 0;
    iph[2] = (ip_len >> 8) & 0xFF; iph[3] = ip_len & 0xFF;
    iph[4] = 0; iph[5] = 0; iph[6] = 0; iph[7] = 0;
    iph[8] = 64; iph[9] = 17;
    iph[10] = 0; iph[11] = 0;
    for (int k = 0; k < 4; k++) iph[12 + k] = 0;
    for (int k = 0; k < 4; k++) iph[16 + k] = 0xFF;
    uint16_t cs = ip_cksum(iph, 20);
    iph[10] = (cs >> 8) & 0xFF; iph[11] = cs & 0xFF;
    uint8_t *udph = iph + 20;
    udph[0] = (DHCP_CLIENT_PORT >> 8) & 0xFF;
    udph[1] = DHCP_CLIENT_PORT & 0xFF;
    udph[2] = (DHCP_SERVER_PORT >> 8) & 0xFF;
    udph[3] = DHCP_SERVER_PORT & 0xFF;
    udph[4] = (udp_len >> 8) & 0xFF; udph[5] = udp_len & 0xFF;
    udph[6] = 0; udph[7] = 0;
    bootp_t *bp = (bootp_t *)(udph + 8);
    mem_zero(bp, sizeof(bootp_t));
    bp->op = BOOTREQUEST; bp->htype = 1; bp->hlen = 6; bp->hops = 0;
    bp->xid = htonl32(g_xid);
    bp->secs = 0;
    bp->flags = htons16(0x8000);
    mem_copy(bp->chaddr, my_mac.b, 6);
    bp->magic_cookie = htonl32(MAGIC_COOKIE);
    mem_copy((uint8_t *)bp + sizeof(bootp_t), opts, opt_len);
    return 14 + ip_len;
}

typedef struct {
    uint8_t yiaddr[4];
    uint8_t server_id[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint8_t dns[4];
    int     has_mask, has_gw, has_dns, has_server;
    uint8_t msg_type;
} dhcp_reply_t;

static int parse_dhcp_reply(const uint8_t *frame, uint32_t len,
                            dhcp_reply_t *out) {
    if (len < 14 + 20 + 8 + sizeof(bootp_t)) return 0;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    if (ntohs16(eth->ethertype) != ETH_TYPE_IPV4) return 0;
    const uint8_t *iph = frame + 14;
    if ((iph[0] >> 4) != 4) return 0;
    uint32_t ihl = (iph[0] & 0x0F) * 4;
    if (ihl < 20) return 0;
    if (iph[9] != 17) return 0;
    uint32_t avail = len - 14;
    if (ihl + 8 + sizeof(bootp_t) > avail) return 0;
    const uint8_t *udph = iph + ihl;
    uint16_t src_port = ((uint16_t)udph[0] << 8) | udph[1];
    uint16_t dst_port = ((uint16_t)udph[2] << 8) | udph[3];
    if (src_port != DHCP_SERVER_PORT || dst_port != DHCP_CLIENT_PORT) return 0;
    uint16_t udp_len = ((uint16_t)udph[4] << 8) | udph[5];
    if (udp_len < 8 + sizeof(bootp_t)) return 0;
    if (ihl + udp_len > avail) return 0;
    const bootp_t *bp = (const bootp_t *)(udph + 8);
    if (bp->op != BOOTREPLY) return 0;
    if (ntohl32(bp->magic_cookie) != MAGIC_COOKIE) return 0;
    if (ntohl32(bp->xid) != g_xid) return 0;
    mem_copy(out->yiaddr, bp->yiaddr, 4);
    out->has_mask = out->has_gw = out->has_dns = out->has_server = 0;
    out->msg_type = 0;
    const uint8_t *opt = (const uint8_t *)bp + sizeof(bootp_t);
    const uint8_t *opt_end = udph + udp_len;
    while (opt < opt_end) {
        uint8_t code = *opt++;
        if (code == 0) continue;
        if (code == 255) break;
        if (opt >= opt_end) break;
        uint8_t olen = *opt++;
        if (opt + olen > opt_end) break;
        switch (code) {
            case 53: if (olen >= 1) out->msg_type = opt[0]; break;
            case 1:  if (olen == 4) { mem_copy(out->mask, opt, 4);      out->has_mask = 1;   } break;
            case 3:  if (olen >= 4){ mem_copy(out->gw, opt, 4);         out->has_gw = 1;     } break;
            case 6:  if (olen >= 4){ mem_copy(out->dns, opt, 4);        out->has_dns = 1;    } break;
            case 54: if (olen == 4) { mem_copy(out->server_id, opt, 4); out->has_server = 1;} break;
        }
        opt += olen;
    }
    return out->msg_type != 0;
}

static int wait_reply(uint8_t want_type, dhcp_reply_t *out, int timeout_iters) {
    uint8_t rxbuf[1600];
    for (int iter = 0; iter < timeout_iters; iter++) {
        uint32_t flen = 0;
        while (vnet_recv(rxbuf, sizeof(rxbuf), &flen)) {
            dhcp_reply_t r;
            if (parse_dhcp_reply(rxbuf, flen, &r) && r.msg_type == want_type) {
                *out = r; return 1;
            }
        }
        for (volatile int i = 0; i < 500000; i++) {}
    }
    return 0;
}

int dhcp_acquire(void) {
    mac_t my_mac = net_get_mac();
    g_xid = make_xid();
    uart_puts("  [dhcp] xid="); uart_puthex(g_xid); uart_puts("\n");
    uint8_t frame[600];
    uint32_t flen;
    flen = build_dhcp_frame(frame, my_mac, DHCPDISCOVER, 0, 0);
    if (!vnet_send(frame, flen)) {
        uart_puts("  [dhcp] DISCOVER send failed\n"); return 0;
    }
    uart_puts("  [dhcp] DISCOVER sent, len="); uart_putu(flen); uart_puts("\n");
    dhcp_reply_t offer;
    if (!wait_reply(DHCPOFFER, &offer, 400)) {
        uart_puts("  [dhcp] no OFFER (timeout)\n"); return 0;
    }
    uart_puts("  [dhcp] OFFER yiaddr=");
    for (int i = 0; i < 4; i++) {
        uart_putu(offer.yiaddr[i]); if (i < 3) uart_puts(".");
    }
    uart_puts("\n");
    if (!offer.has_server) {
        uart_puts("  [dhcp] OFFER without server id\n"); return 0;
    }
    flen = build_dhcp_frame(frame, my_mac, DHCPREQUEST, offer.server_id, offer.yiaddr);
    if (!vnet_send(frame, flen)) {
        uart_puts("  [dhcp] REQUEST send failed\n"); return 0;
    }
    uart_puts("  [dhcp] REQUEST sent\n");
    dhcp_reply_t ack;
    if (!wait_reply(DHCPACK, &ack, 400)) {
        uart_puts("  [dhcp] no ACK (timeout)\n"); return 0;
    }
    ipv4_t ip;  mem_copy(ip.b, ack.yiaddr, 4);
    net_set_ip(ip);
    if (ack.has_mask) { ipv4_t m; mem_copy(m.b, ack.mask, 4); net_set_netmask(m); }
    else              { ipv4_t m = {{255,255,255,0}};         net_set_netmask(m); }
    if (ack.has_gw)   { ipv4_t g; mem_copy(g.b, ack.gw, 4);   net_set_gateway(g); }
    uart_puts("  [dhcp] ACK: ip=");  net_print_ip("", net_get_ip());
    uart_puts("  [dhcp]      gw=");  net_print_ip("", net_get_gateway());
    uart_puts("  [dhcp]    mask=");  net_print_ip("", net_get_netmask());
    if (ack.has_dns) {
        uart_puts("  [dhcp]     dns=");
        for (int i = 0; i < 4; i++) {
            uart_putu(ack.dns[i]); if (i < 3) uart_puts(".");
        }
        uart_puts("\n");
    }
    return 1;
}
