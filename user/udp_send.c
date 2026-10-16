// BrainixKernel (BKOS) — user/udp_send.c
// Отправляет UDP на gateway:7. Без htons — kernel ABI ждёт host-order порт.

#include "user.h"
#include "libc.h"

int main(void) {
    puts("=== UDP send test ===");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { puts("socket failed"); return 1; }
    printf("socket fd = %d\n", fd);

    // ABI: порт в host order. Kernel сам пишет big-endian в заголовок.
    unsigned short my_port = 40000;
    int r = bind(fd, my_port);
    if (r < 0) { puts("bind failed"); return 1; }
    printf("bound to port %d\n", my_port);

    unsigned char dst_ip[4] = {10, 0, 2, 2};
    unsigned short dst_port = 7;
    const char *msg = "hello from BKOS UDP";

    long s = sendto(fd, msg, strlen(msg), dst_port, dst_ip);
    printf("sendto = %d\n", (int)s);

    if (s == (long)strlen(msg)) {
        puts("UDP send OK");
    } else {
        puts("UDP send FAILED");
    }
    return 0;
}
