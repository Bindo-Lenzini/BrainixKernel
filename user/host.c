// BrainixKernel (BKOS) — user/host.c
// Простой DNS-резолвер в командной строке.

#include "user.h"
#include "libc.h"
#include "dns.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: host <name>");
        return 1;
    }

    unsigned char ip[4];
    if (dns_resolve(argv[1], ip)) {
        printf("%s has address %d.%d.%d.%d\n",
               argv[1], ip[0], ip[1], ip[2], ip[3]);
        return 0;
    }
    printf("host: %s: not found\n", argv[1]);
    return 1;
}
