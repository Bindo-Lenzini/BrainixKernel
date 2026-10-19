// BrainixKernel (BKOS) — net.h
// Общие утилиты сети: byte order, типы, Ethernet/ARP заголовки.
#ifndef BKOS_NET_H
#define BKOS_NET_H

#include <stdint.h>

// ---- byte order (ARM little-endian ↔ network big-endian) ----
static inline uint16_t net_bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}
static inline uint32_t net_bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}
#define htons16 net_bswap16
#define ntohs16 net_bswap16
#define htonl32 net_bswap32
#define ntohl32 net_bswap32

// ---- IPv4 ----
typedef struct { uint8_t b[4]; } ipv4_t;
typedef struct { uint8_t b[6]; } mac_t;

static inline int ipv4_eq(ipv4_t a, ipv4_t b) {
    return a.b[0]==b.b[0] && a.b[1]==b.b[1] &&
           a.b[2]==b.b[2] && a.b[3]==b.b[3];
}

// ---- Ethernet (14 байт) ----
typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;   // в сетевом порядке
} eth_hdr_t;

#define ETH_TYPE_IPV4  0x0800
#define ETH_TYPE_ARP   0x0806

// ---- ARP (28 байт для IPv4-over-Ethernet) ----
typedef struct __attribute__((packed)) {
    uint16_t htype;      // 1 = Ethernet
    uint16_t ptype;      // 0x0800 = IPv4
    uint8_t  hlen;       // 6
    uint8_t  plen;       // 4
    uint16_t oper;       // 1 = request, 2 = reply
    uint8_t  sha[6];     // sender hardware address
    uint8_t  spa[4];     // sender protocol address
    uint8_t  tha[6];     // target hardware address
    uint8_t  tpa[4];     // target protocol address
} arp_pkt_t;

// ---- Конфиг сети (QEMU user-mode дефолт) ----
void    net_init(void);
mac_t   net_get_mac(void);
ipv4_t  net_get_ip(void);
ipv4_t  net_get_gateway(void);
ipv4_t  net_get_netmask(void);

// ---- Печать в UART ----
void net_print_mac(const char *prefix, const uint8_t *mac);
void net_print_ip (const char *prefix, ipv4_t ip);

// ---- Хелпер: собрать Ethernet-фрейм ----
// Заполняет eth_hdr_t и возвращает указатель на payload (frame+14).
// Отправить IPv4/UDP-пакет. MAC резолвится через ARP.
//   src_port_be, dst_port_be — network byte order.
// Возврат 1/0.
// Забрать один входящий пакет из virtio-net и раздать его по слоям
// (ARP / IPv4 → ICMP / UDP). Вызывать из таймера 60 Hz.
void net_poll(void);

// Отправить TCP-сегмент.
//   seq, ack — host order
//   flags — комбинация TCP_SYN/TCP_ACK/...
//   payload/payload_len — данные (может быть 0)
int net_send_tcp(const uint8_t src_ip[4], uint16_t src_port,
                 const uint8_t dst_ip[4], uint16_t dst_port,
                 uint32_t seq, uint32_t ack, uint8_t flags,
                 const void *payload, uint32_t payload_len);

// Определить MAC, на который нужно отправить пакет для dst_ip.
//   dst в нашей подсети → ARP по dst.
//   dst вне подсети     → ARP по gateway (пакет шлётся на MAC шлюза).
int net_route_mac(ipv4_t dst_ip, mac_t *out_mac);

int net_send_udp(ipv4_t dst_ip, uint16_t src_port_be, uint16_t dst_port_be,
                 const void *payload, uint32_t payload_len);

uint8_t *net_build_eth(uint8_t *frame,
                       const uint8_t *dst_mac,
                       const uint8_t *src_mac,
                       uint16_t ethertype);

// Сеттеры — используются DHCP-клиентом.
void net_set_ip(ipv4_t ip);
void net_set_gateway(ipv4_t gw);
void net_set_netmask(ipv4_t mask);

#endif
