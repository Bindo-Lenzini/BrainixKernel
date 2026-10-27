// BrainixKernel — user/tcp_connect.c
// Тест TCP connect: подключаемся к example.com:80 и рвём.

#include "user.h"
#include "libc.h"
#include "dns.h"

int main(int argc, char **argv) {
    const char *host = (argc >= 2) ? argv[1] : "example.com";
    unsigned short port = (argc >= 3) ? (unsigned short)atoi(argv[2]) : 80;

    puts("=== TCP connect test ===");
    printf("host: %s, port: %d\n", host, port);

    unsigned char ip[4];
    if (!dns_resolve(host, ip)) {
        printf("DNS: %s not resolved\n", host);
        return 1;
    }
    printf("resolved to %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { puts("socket failed"); return 1; }

    printf("connect to %d.%d.%d.%d:%d ...\n", ip[0], ip[1], ip[2], ip[3], port);
    int r = connect(fd, ip, port);
    if (r < 0) {
        puts("connect FAILED");
        return 1;
    }

    puts("CONNECTED ✓");
    close(fd);
    return 0;
}
