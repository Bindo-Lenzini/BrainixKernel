// BrainixKernel — icmp.c
// ICMPv4 Echo Request/Reply (ping).

#include <stdint.h>
#include "icmp.h"
#include "net.h"
#include "virtio_net.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern void uart_puthex(uint64_t v);

#define IP_PROTO_ICMP  1

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t ident;
    uint16_t frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} ipv4_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} icmp_hdr_t;

#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0

static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mem_zero(void *d, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) dd[i] = 0;
}

static uint16_t ip_checksum(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2) {
        sum += ((uint32_t)p[i] << 8) | p[i + 1];
    }
    if (len & 1) sum += (uint32_t)p[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t g_ident = 0x4242;
static int      g_got_reply = 0;
static uint16_t g_reply_seq = 0;

int icmp_handle_frame(const uint8_t *frame, uint32_t len) {
    if (len < 14 + 20) return 0;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    if (ntohs16(eth->ethertype) != ETH_TYPE_IPV4) return 0;

    const ipv4_hdr_t *ip = (const ipv4_hdr_t *)(frame + 14);
    if ((ip->ver_ihl >> 4) != 4) return 0;

    uint32_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20) return 0;
    if (14 + ihl + 8 > len) return 0;

    if (ip->proto != IP_PROTO_ICMP) return 0;

    const icmp_hdr_t *ic = (const icmp_hdr_t *)(frame + 14 + ihl);
    uart_puts("  [icmp] got: type=");
    uart_putu(ic->type);
    uart_puts(" code=");
    uart_putu(ic->code);
    uart_puts(" id=");
    uart_puthex(ntohs16(ic->ident));
    uart_puts(" seq=");
    uart_putu(ntohs16(ic->seq));
    uart_puts("\n");

    if (ic->type == ICMP_ECHO_REPLY && ntohs16(ic->ident) == g_ident) {
        g_got_reply = 1;
        g_reply_seq = ntohs16(ic->seq);
    }
    return 1;
}

int icmp_ping(ipv4_t target, mac_t target_mac, uint16_t seq) {
    uint8_t frame[128];
    mem_zero(frame, 128);

    mac_t my_mac = net_get_mac();
    uint8_t *payload = net_build_eth(frame, target_mac.b, my_mac.b, ETH_TYPE_IPV4);

    ipv4_hdr_t *ip = (ipv4_hdr_t *)payload;
    ip->ver_ihl   = 0x45;
    ip->tos       = 0;
    ip->total_len = htons16(20 + 8 + 4);
    ip->ident     = htons16(g_ident);
    ip->frag      = 0;
    ip->ttl       = 64;
    ip->proto     = IP_PROTO_ICMP;
    ip->checksum  = 0;
    ipv4_t my_ip = net_get_ip();
    mem_copy(ip->src, my_ip.b, 4);
    mem_copy(ip->dst, target.b, 4);
    ip->checksum = htons16(ip_checksum(ip, 20));

    icmp_hdr_t *ic = (icmp_hdr_t *)(payload + 20);
    ic->type     = ICMP_ECHO_REQUEST;
    ic->code     = 0;
    ic->checksum = 0;
    ic->ident    = htons16(g_ident);
    ic->seq      = htons16(seq);

    uint8_t *ic_data = (uint8_t *)ic + 8;
    ic_data[0] = 0xDE; ic_data[1] = 0xAD;
    ic_data[2] = 0xBE; ic_data[3] = 0xEF;

    ic->checksum = htons16(ip_checksum(ic, 12));

    // Паддинг до минимума Ethernet = 60 байт.
    uint32_t total = 14 + 20 + 8 + 4;
    if (total < 60) total = 60;

    uart_puts("  [icmp] TX total=");
    uart_putu((uint64_t)total);
    uart_puts(" seq=");
    uart_putu((uint64_t)seq);
    uart_puts("\n");

    g_got_reply = 0;

    if (!vnet_send(frame, total)) return 0;

    uint8_t rxbuf[1600];
    for (int iter = 0; iter < 300; iter++) {
        uint32_t flen = 0;
        while (vnet_recv(rxbuf, sizeof(rxbuf), &flen)) {
            icmp_handle_frame(rxbuf, flen);
            if (g_got_reply) return 1;
        }
        if ((iter % 20) == 19) {
            for (volatile int i = 0; i < 2000000; i++) {}
        }
    }
    return 0;
}