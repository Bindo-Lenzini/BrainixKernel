// BrainixKernel — net.c

#include <stdint.h>
#include "net.h"
#include "virtio_net.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern void uart_puthex(uint64_t v);
extern void uart_putc(char c);

static mac_t   g_my_mac;
static ipv4_t  g_my_ip    = {{10, 0, 2, 15}};
static ipv4_t  g_gateway  = {{10, 0, 2, 2}};
static ipv4_t  g_netmask  = {{255, 255, 255, 0}};

void net_init(void) {
    vnet_get_mac(g_my_mac.b);
}

mac_t  net_get_mac(void)     { return g_my_mac; }
ipv4_t net_get_ip(void)      { return g_my_ip; }
ipv4_t net_get_gateway(void) { return g_gateway; }
ipv4_t net_get_netmask(void) { return g_netmask; }

void net_set_ip(ipv4_t ip)        { g_my_ip   = ip; }
void net_set_gateway(ipv4_t gw)   { g_gateway = gw; }
void net_set_netmask(ipv4_t mask) { g_netmask = mask; }

static const char g_hex[] = "0123456789abcdef";

void net_print_mac(const char *prefix, const uint8_t *mac) {
    uart_puts(prefix);
    for (int i = 0; i < 6; i++) {
        uart_putc(g_hex[(mac[i] >> 4) & 0xF]);
        uart_putc(g_hex[mac[i] & 0xF]);
        if (i < 5) uart_putc(':');
    }
    uart_puts("\n");
}

static void print_dec_u32(uint32_t v) {
    char b[12]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) uart_putc(b[--n]);
}

void net_print_ip(const char *prefix, ipv4_t ip) {
    uart_puts(prefix);
    print_dec_u32(ip.b[0]); uart_putc('.');
    print_dec_u32(ip.b[1]); uart_putc('.');
    print_dec_u32(ip.b[2]); uart_putc('.');
    print_dec_u32(ip.b[3]);
    uart_puts("\n");
}

uint8_t *net_build_eth(uint8_t *frame,
                       const uint8_t *dst_mac,
                       const uint8_t *src_mac,
                       uint16_t ethertype)
{
    eth_hdr_t *eth = (eth_hdr_t *)frame;
    for (int i = 0; i < 6; i++) eth->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = src_mac[i];
    eth->ethertype = htons16(ethertype);
    return frame + 14;
}
// ============================================================
// UDP send.
// ============================================================

#include "arp.h"

static uint16_t ip_cksum(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)p[i] << 8) | p[i + 1];
    if (len & 1) sum += (uint32_t)p[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

int net_send_udp(ipv4_t dst_ip, uint16_t src_port_be, uint16_t dst_port_be,
                 const void *payload, uint32_t payload_len)
{
    if (payload_len > 1472) return 0;

    // 1. Резолв MAC.
    mac_t dst_mac;
    if (!net_route_mac(dst_ip, &dst_mac)) return 0;

    // 2. Пакет.
    uint8_t frame[1600];
    mac_t my_mac = net_get_mac();
    uint8_t *ip_start = net_build_eth(frame, dst_mac.b, my_mac.b, ETH_TYPE_IPV4);

    uint32_t udp_len = 8 + payload_len;
    uint32_t ip_len  = 20 + udp_len;

    uint8_t *iph = ip_start;
    iph[0]  = 0x45;
    iph[1]  = 0;
    iph[2]  = (uint8_t)((ip_len >> 8) & 0xFF);
    iph[3]  = (uint8_t)(ip_len & 0xFF);
    iph[4]  = 0x12; iph[5] = 0x34;
    iph[6]  = 0; iph[7] = 0;
    iph[8]  = 64;
    iph[9]  = 17;    // UDP
    iph[10] = 0; iph[11] = 0;
    ipv4_t my_ip = net_get_ip();
    iph[12] = my_ip.b[0];  iph[13] = my_ip.b[1];
    iph[14] = my_ip.b[2];  iph[15] = my_ip.b[3];
    iph[16] = dst_ip.b[0]; iph[17] = dst_ip.b[1];
    iph[18] = dst_ip.b[2]; iph[19] = dst_ip.b[3];

    uint16_t cs = ip_cksum(iph, 20);
    iph[10] = (uint8_t)((cs >> 8) & 0xFF);
    iph[11] = (uint8_t)(cs & 0xFF);

    uint8_t *udph = iph + 20;
    udph[0] = (uint8_t)((src_port_be >> 8) & 0xFF);
    udph[1] = (uint8_t)(src_port_be & 0xFF);
    udph[2] = (uint8_t)((dst_port_be >> 8) & 0xFF);
    udph[3] = (uint8_t)(dst_port_be & 0xFF);
    udph[4] = (uint8_t)((udp_len >> 8) & 0xFF);
    udph[5] = (uint8_t)(udp_len & 0xFF);
    udph[6] = 0; udph[7] = 0;   // checksum = 0 (SLIRP примет)

    const uint8_t *src = (const uint8_t *)payload;
    for (uint32_t i = 0; i < payload_len; i++) udph[8 + i] = src[i];

    return vnet_send(frame, 14 + ip_len);
}

// ============================================================
// Фоновый RX: разбор входящих Ethernet-фреймов.
// ============================================================

extern int  arp_handle_frame(const uint8_t *frame, uint32_t len);
extern int  icmp_handle_frame(const uint8_t *frame, uint32_t len);
extern int  udp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len);
extern int  tcp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len);

void net_poll(void) {
    if (!vnet_ready()) return;

    uint8_t frame[1600];

    for (int n = 0; n < 8; n++) {
        uint32_t flen = 0;
        if (!vnet_recv(frame, sizeof(frame), &flen)) break;
        if (flen < 14) continue;

        const eth_hdr_t *eth = (const eth_hdr_t *)frame;
        uint16_t etype = ntohs16(eth->ethertype);

        if (etype == ETH_TYPE_ARP) {
            arp_handle_frame(frame, flen);
            continue;
        }

        if (etype == ETH_TYPE_IPV4) {
            if (flen < 14 + 20) continue;
            const uint8_t *iph = frame + 14;
            uint8_t proto = iph[9];
            uint32_t ip_len = ((uint32_t)iph[2] << 8) | iph[3];
            if (ip_len > flen - 14) ip_len = flen - 14;

            if (proto == 1) {
                icmp_handle_frame(frame, flen);
            } else if (proto == 17) {
                udp_handle_ipv4(iph, ip_len);
            } else if (proto == 6) {
                tcp_handle_ipv4(iph, ip_len);
            }
            continue;
        }
    }
}


// ============================================================
// TCP send.
// ============================================================

static uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                             const uint8_t *seg, uint32_t seg_len)
{
    uint32_t sum = 0;
    // Псевдо-заголовок.
    sum += ((uint32_t)src_ip[0] << 8) | src_ip[1];
    sum += ((uint32_t)src_ip[2] << 8) | src_ip[3];
    sum += ((uint32_t)dst_ip[0] << 8) | dst_ip[1];
    sum += ((uint32_t)dst_ip[2] << 8) | dst_ip[3];
    sum += 6;                // protocol TCP
    sum += seg_len;
    // Сам сегмент.
    for (uint32_t i = 0; i + 1 < seg_len; i += 2)
        sum += ((uint32_t)seg[i] << 8) | seg[i + 1];
    if (seg_len & 1) sum += (uint32_t)seg[seg_len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

int net_send_tcp(const uint8_t src_ip[4], uint16_t src_port,
                 const uint8_t dst_ip[4], uint16_t dst_port,
                 uint32_t seq, uint32_t ack, uint8_t flags,
                 const void *payload, uint32_t payload_len)
{
    if (payload_len > 1400) return 0;

    ipv4_t dip;
    dip.b[0] = dst_ip[0]; dip.b[1] = dst_ip[1];
    dip.b[2] = dst_ip[2]; dip.b[3] = dst_ip[3];

    mac_t dst_mac;
    if (!net_route_mac(dip, &dst_mac)) return 0;

    uint8_t frame[1600];
    mac_t my_mac = net_get_mac();
    uint8_t *ip_start = net_build_eth(frame, dst_mac.b, my_mac.b, ETH_TYPE_IPV4);

    uint32_t tcp_len = 20 + payload_len;
    uint32_t ip_len  = 20 + tcp_len;

    uint8_t *iph = ip_start;
    iph[0]  = 0x45;
    iph[1]  = 0;
    iph[2]  = (uint8_t)((ip_len >> 8) & 0xFF);
    iph[3]  = (uint8_t)(ip_len & 0xFF);
    iph[4]  = 0x42; iph[5] = 0x00;
    iph[6]  = 0; iph[7] = 0;
    iph[8]  = 64;
    iph[9]  = 6;    // TCP
    iph[10] = 0; iph[11] = 0;
    for (int i = 0; i < 4; i++) iph[12 + i] = src_ip[i];
    for (int i = 0; i < 4; i++) iph[16 + i] = dst_ip[i];

    uint16_t cs = ip_cksum(iph, 20);
    iph[10] = (uint8_t)((cs >> 8) & 0xFF);
    iph[11] = (uint8_t)(cs & 0xFF);

    uint8_t *tcph = iph + 20;
    tcph[0]  = (uint8_t)((src_port >> 8) & 0xFF);
    tcph[1]  = (uint8_t)(src_port & 0xFF);
    tcph[2]  = (uint8_t)((dst_port >> 8) & 0xFF);
    tcph[3]  = (uint8_t)(dst_port & 0xFF);
    tcph[4]  = (seq >> 24) & 0xFF;
    tcph[5]  = (seq >> 16) & 0xFF;
    tcph[6]  = (seq >> 8)  & 0xFF;
    tcph[7]  = (seq)       & 0xFF;
    tcph[8]  = (ack >> 24) & 0xFF;
    tcph[9]  = (ack >> 16) & 0xFF;
    tcph[10] = (ack >> 8)  & 0xFF;
    tcph[11] = (ack)       & 0xFF;
    // Data offset = 5 words (20 bytes TCP header, no options).
    // payload НЕ входит в data offset — это размер заголовка.
    tcph[12] = 0x50;
    tcph[13] = flags;
    tcph[14] = 0xFF; tcph[15] = 0xFF;   // window = 65535
    tcph[16] = 0; tcph[17] = 0;         // checksum (заполним)
    tcph[18] = 0; tcph[19] = 0;         // urgent

    const uint8_t *src = (const uint8_t *)payload;
    for (uint32_t i = 0; i < payload_len; i++) tcph[20 + i] = src[i];

    uint16_t tcs = tcp_checksum(src_ip, dst_ip, tcph, tcp_len);
    tcph[16] = (uint8_t)((tcs >> 8) & 0xFF);
    tcph[17] = (uint8_t)(tcs & 0xFF);

    // Минимальный размер Ethernet-фрейма = 60 байт (без FCS).
    // SYN без данных даёт 54 байта → паддинг нулями.
    uint32_t frame_len = 14 + ip_len;
    if (frame_len < 60) {
        for (uint32_t i = frame_len; i < 60; i++) frame[i] = 0;
        frame_len = 60;
    }
    return vnet_send(frame, frame_len);
}

// ============================================================
// IP-маршрутизация: MAC адрес для отправки.
// ============================================================

int net_route_mac(ipv4_t dst_ip, mac_t *out_mac) {
    ipv4_t my_ip = net_get_ip();
    ipv4_t mask  = net_get_netmask();

    int same_subnet = 1;
    for (int i = 0; i < 4; i++) {
        if ((my_ip.b[i] & mask.b[i]) != (dst_ip.b[i] & mask.b[i])) {
            same_subnet = 0;
            break;
        }
    }

    if (same_subnet) {
        return arp_resolve(dst_ip, out_mac);
    } else {
        return arp_resolve(net_get_gateway(), out_mac);
    }
}
