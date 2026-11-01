#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tcp.h"

void uart_puts(const char *text) {
    (void)text;
}

void uart_putu(uint64_t value) {
    (void)value;
}

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

static int check(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        return 0;
    }
    printf("PASS: %s\n", description);
    return 1;
}

int main(void) {
    int failures = 0;
    int idx = tcp_alloc();
    tcp_conn_t *connection = tcp_get(idx);
    uint8_t output[8] = {0};

    failures += !check(connection != 0, "TCP connection allocation");
    if (!connection) return 1;

    connection->rx_len = 5;
    memcpy(connection->rx_buf, "hello", 5);
    uint32_t peeked = tcp_peek_data(connection, output, sizeof(output));
    failures += !check(peeked == 5 && memcmp(output, "hello", 5) == 0 &&
                       connection->rx_len == 5,
                       "peek copies queued data without consuming it");
    failures += !check(!tcp_consume_data(connection, 6) &&
                       connection->rx_len == 5,
                       "consume rejects lengths beyond queued data");
    failures += !check(tcp_consume_data(connection, 2) &&
                       connection->rx_len == 3 &&
                       memcmp(connection->rx_buf, "llo", 3) == 0,
                       "consume removes only the committed prefix");

    memset(output, 0, sizeof(output));
    uint32_t received = tcp_recv_data(connection, output, sizeof(output));
    failures += !check(received == 3 && memcmp(output, "llo", 3) == 0 &&
                       connection->rx_len == 0,
                       "legacy receive remains peek plus consume");

    tcp_free(idx);
    return failures != 0;
}
