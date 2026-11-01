#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net.h"
#include "icmp.h"
#include "socket.h"
#include "tcp.h"
#include "udp.h"

static int failures;
static int checks;

void uart_puts(const char *text) { (void)text; }
void uart_putu(uint64_t value) { (void)value; }
void uart_puthex(uint64_t value) { (void)value; }

int net_send_tcp(const uint8_t src_ip[4], uint16_t src_port,
                 const uint8_t dst_ip[4], uint16_t dst_port,
                 uint32_t seq, uint32_t ack, uint8_t flags,
                 const void *payload, uint32_t payload_len) {
    (void)src_ip;
    (void)src_port;
    (void)dst_ip;
    (void)dst_port;
    (void)seq;
    (void)ack;
    (void)flags;
    (void)payload;
    (void)payload_len;
    return 1;
}

static void check(int condition, const char *description) {
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    } else {
        printf("PASS: %s\n", description);
    }
}

static void make_ipv4_frame(uint8_t *frame, uint32_t ip_len,
                            uint8_t protocol) {
    memset(frame, 0, 1600);
    frame[12] = 0x08;
    frame[13] = 0x00;
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[2] = (uint8_t)(ip_len >> 8);
    ip[3] = (uint8_t)ip_len;
    ip[9] = protocol;
    ip[12] = 1;
    ip[13] = 2;
    ip[14] = 3;
    ip[15] = 4;
}

static uint32_t next_random(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

int main(void) {
    uint8_t frame[1600];
    const uint8_t *ip = 0;
    uint32_t ip_len = 0;

    make_ipv4_frame(frame, 20, 17);
    check(net_ipv4_packet_view(frame, 34, &ip, &ip_len) &&
          ip == frame + 14 && ip_len == 20,
          "IPv4 view returns the declared in-frame packet length");

    check(!net_ipv4_packet_view(frame, 33, &ip, &ip_len),
          "truncated Ethernet and IPv4 headers are rejected");

    make_ipv4_frame(frame, 20, 1);
    uint8_t *icmp = frame + 14 + 20;
    icmp[0] = 0;
    icmp[4] = 0x42;
    icmp[5] = 0x42;
    check(!icmp_handle_frame(frame, 60),
          "ICMP padding beyond the declared IPv4 length is rejected");

    make_ipv4_frame(frame, 28, 1);
    icmp = frame + 14 + 20;
    icmp[0] = 0;
    icmp[4] = 0x42;
    icmp[5] = 0x42;
    check(icmp_handle_frame(frame, 60),
          "valid ICMP payload inside the declared IPv4 length is accepted");

    frame[14] = 0x65;
    check(!net_ipv4_packet_view(frame, 34, &ip, &ip_len),
          "non-IPv4 version is rejected");
    frame[14] = 0x44;
    check(!net_ipv4_packet_view(frame, 34, &ip, &ip_len),
          "IPv4 header lengths below the minimum are rejected");
    make_ipv4_frame(frame, 19, 17);
    check(!net_ipv4_packet_view(frame, 60, &ip, &ip_len),
          "IPv4 total lengths shorter than the header are rejected");
    make_ipv4_frame(frame, 48, 17);
    check(!net_ipv4_packet_view(frame, 34, &ip, &ip_len),
          "truncated IPv4 payload is not accepted by clamping its length");
    make_ipv4_frame(frame, 20, 17);
    frame[20] = 0x20;
    check(!net_ipv4_packet_view(frame, 34, &ip, &ip_len),
          "fragmented IPv4 packets are rejected when reassembly is unsupported");
    frame[20] = 0x80;
    check(!net_ipv4_packet_view(frame, 34, &ip, &ip_len),
          "IPv4 reserved fragment flag is rejected");

    int socket_idx = sock_alloc(0x5678);
    socket_t *socket = sock_get(socket_idx);
    make_ipv4_frame(frame, 28, 17);
    uint8_t *udp = frame + 14 + 20;
    udp[0] = 0x12;
    udp[1] = 0x34;
    udp[2] = 0x56;
    udp[3] = 0x78;
    udp[5] = 8;
    check(socket && udp_handle_ipv4(frame + 14, 28) &&
          socket->rx_head != socket->rx_tail,
          "valid UDP datagram is queued");
    udp[5] = 7;
    check(!udp_handle_ipv4(frame + 14, 28),
          "UDP lengths smaller than the header are rejected");
    udp[5] = 9;
    check(!udp_handle_ipv4(frame + 14, 28),
          "UDP lengths beyond the IPv4 payload are rejected");
    if (socket_idx >= 0) sock_free(socket_idx);

    int tcp_idx = tcp_alloc();
    tcp_conn_t *connection = tcp_get(tcp_idx);
    make_ipv4_frame(frame, 40, 6);
    uint8_t *tcp = frame + 14 + 20;
    tcp[0] = 0x12;
    tcp[1] = 0x34;
    tcp[12] = 0xF0;
    tcp[13] = TCP_FIN;
    if (connection) {
        connection->state = TCP_ESTABLISHED;
        connection->remote_port = 0x1234;
        connection->remote_ip[0] = 1;
        connection->remote_ip[1] = 2;
        connection->remote_ip[2] = 3;
        connection->remote_ip[3] = 4;
    }
    check(connection && !tcp_handle_ipv4(frame + 14, 40) &&
          !connection->peer_closed,
          "TCP data offset beyond the IPv4 packet is rejected without state change");
    if (tcp_idx >= 0) tcp_free(tcp_idx);

    uint32_t state = 0xB17A1C5u;
    for (uint32_t len = 0; len <= sizeof(frame); len++) {
        for (uint32_t i = 0; i < sizeof(frame); i++)
            frame[i] = (uint8_t)next_random(&state);
        frame[12] = 0x08;
        frame[13] = 0x00;
        (void)net_ipv4_packet_view(frame, len, &ip, &ip_len);
        (void)udp_handle_ipv4(frame + 14, len > 14 ? len - 14 : 0);
        (void)tcp_handle_ipv4(frame + 14, len > 14 ? len - 14 : 0);
    }
    check(1, "deterministic malformed-packet corpus completes without memory errors");

    if (failures != 0) {
        fprintf(stderr, "%d/%d network input checks failed\n", failures, checks);
        return 1;
    }
    printf("Network input tests passed (%d checks)\n", checks);
    return 0;
}
