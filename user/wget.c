// BrainixKernel (BKOS) — user/wget.c
// Минимальный HTTP/1.0 клиент.
// usage: wget <host> [port] [path]

#include "user.h"
#include "libc.h"
#include "dns.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: wget <host> [port] [path]");
        return 1;
    }

    const char *host = argv[1];
    int port = (argc >= 3) ? atoi(argv[2]) : 80;
    const char *path = (argc >= 4) ? argv[3] : "/";

    printf("Resolving %s...\n", host);
    unsigned char ip[4];
    if (!dns_resolve(host, ip)) {
        printf("wget: cannot resolve %s\n", host);
        return 1;
    }
    printf("IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { puts("socket failed"); return 1; }

    printf("Connecting to %s:%d ...\n", host, port);
    if (connect(fd, ip, port) < 0) {
        puts("wget: connect failed");
        close(fd);
        return 1;
    }
    puts("Connected.");

    // Собираем HTTP-запрос.
    char req[256];
    int i = 0;
    const char *p1 = "GET ";
    for (int j = 0; p1[j]; j++) req[i++] = p1[j];
    for (int j = 0; path[j] && i < 200; j++) req[i++] = path[j];
    const char *p2 = " HTTP/1.0\r\nHost: ";
    for (int j = 0; p2[j]; j++) req[i++] = p2[j];
    for (int j = 0; host[j] && i < 240; j++) req[i++] = host[j];
    const char *p3 = "\r\n\r\n";
    for (int j = 0; p3[j]; j++) req[i++] = p3[j];
    req[i] = 0;

    printf("Sending %d bytes request...\n", i);
    long s = send(fd, req, i);
    if (s != i) {
        printf("wget: send returned %d\n", (int)s);
        close(fd);
        return 1;
    }

    puts("--- response ---");
    char buf[512];
    long total = 0;
    for (int attempts = 0; attempts < 100000; attempts++) {
        long r = recv(fd, buf, sizeof(buf));
        if (r == -2) { yield(); continue; }
        if (r == 0)  { puts("\n--- eof ---"); break; }
        if (r < 0)   { puts("\n--- error ---"); break; }
        write(1, buf, r);
        total += r;
    }
    printf("\n--- %d bytes total ---\n", (int)total);

    close(fd);
    return 0;
}
