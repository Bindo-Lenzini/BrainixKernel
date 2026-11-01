#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "socket.h"

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
    int idx = sock_alloc(9000);
    socket_t *socket = sock_get(idx);
    const uint8_t src_ip[4] = {192, 0, 2, 1};
    uint8_t output[8] = {0};

    failures += !check(socket != 0, "UDP socket allocation");
    if (!socket) return 1;

    failures += !check(sock_push_rx(socket, src_ip, 1234,
                                    (const uint8_t *)"hello", 5),
                       "incoming datagram queued");
    failures += !check(sock_peek_rx(socket, output, sizeof(output)) == 5 &&
                       memcmp(output, "hello", 5) == 0 &&
                       socket->rx_head != socket->rx_tail &&
                       sock_peek_rx(socket, output, sizeof(output)) == 5,
                       "peeking preserves datagram until commit");
    failures += !check(sock_peek_rx(socket, 0, sizeof(output)) == -1 &&
                       socket->rx_head != socket->rx_tail,
                       "invalid destination does not dequeue datagram");
    failures += !check(sock_consume_rx(socket) &&
                       socket->rx_head == socket->rx_tail &&
                       !sock_consume_rx(socket),
                       "consume removes exactly one queued datagram");

    failures += !check(sock_push_rx(socket, src_ip, 1235, 0, 0) &&
                       sock_peek_rx(socket, 0, sizeof(output)) == 0 &&
                       sock_consume_rx(socket),
                       "zero-length datagrams remain consumable");

    sock_free(idx);
    return failures != 0;
}
