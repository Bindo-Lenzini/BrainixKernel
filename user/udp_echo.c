// BrainixKernel — user/udp_echo.c
// Тест RX: отправляем DNS-запрос, ждём ответ.

#include "user.h"
#include "libc.h"

int main(void) {
    puts("=== UDP DNS test ===");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { puts("socket failed"); return 1; }

    if (bind(fd, 40000) < 0) { puts("bind failed"); return 1; }

    unsigned char query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        7, 'e','x','a','m','p','l','e', 3, 'c','o','m', 0,
        0x00, 0x01, 0x00, 0x01,
    };

    unsigned char dst_ip[4] = {10, 0, 2, 3};
    long s = sendto(fd, query, sizeof(query), 53, dst_ip);
    printf("sendto = %d\n", (int)s);

    // Ждём до ~10 секунд: 30000 итераций.
    for (int attempt = 0; attempt < 30000; attempt++) {
        char buf[512];
        unsigned short src_port = 0;
        unsigned char src_ip[4];

        long r = recvfrom(fd, buf, sizeof(buf), &src_port, src_ip);
        if (r > 0) {
            printf("recvfrom: %d bytes from %d.%d.%d.%d:%d\n",
                   (int)r, src_ip[0], src_ip[1], src_ip[2], src_ip[3],
                   (int)src_port);
            puts("DNS response received ✓");
            return 0;
        }
        yield();
    }

    puts("timeout, no DNS reply");
    return 1;
}
