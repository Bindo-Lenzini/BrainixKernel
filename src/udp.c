// BrainixKernel — udp.c

#include <stdint.h>
#include "udp.h"
#include "socket.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);

int udp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len) {
    if (ip_total_len < 20) return 0;

    uint32_t ihl = (iph[0] & 0x0F) * 4;
    if (ihl < 20 || ihl + 8 > ip_total_len) return 0;

    const uint8_t *udph = iph + ihl;
    uint16_t src_port_be = ((uint16_t)udph[0] << 8) | udph[1];
    uint16_t dst_port_be = ((uint16_t)udph[2] << 8) | udph[3];
    uint16_t udp_len     = ((uint16_t)udph[4] << 8) | udph[5];
    if (udp_len < 8 || ihl + udp_len > ip_total_len) return 0;

    socket_t *s = sock_find_by_port(dst_port_be);
    if (!s) return 1;

    const uint8_t *payload = udph + 8;
    uint16_t payload_len = udp_len - 8;

    const uint8_t *src_ip = iph + 12;
    sock_push_rx(s, src_ip, src_port_be, payload, payload_len);
    return 1;
}
