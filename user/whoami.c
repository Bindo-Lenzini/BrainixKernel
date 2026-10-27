// BrainixKernel (BKOS) — whoami.c
// Credentials test: prints real/effective uid and gid.
#include "libc.h"
#include "user.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    // mini-libc supports %d (int) only, so cast from long.
    int u   = (int)getuid();
    int eu  = (int)geteuid();
    int g   = (int)getgid();
    int eg  = (int)getegid();

    printf("uid=%d euid=%d gid=%d egid=%d\n", u, eu, g, eg);

    if (eu == 0) {
        puts("(root)");
    } else {
        puts("(unprivileged)");
    }
    return 0;
}
