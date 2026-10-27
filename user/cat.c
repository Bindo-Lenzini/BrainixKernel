// BrainixKernel — user/cat.c
#include "user.h"
#include "libc.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        // cat без аргументов — копирует stdin в stdout (Unix-стиль).
        char buf[256];
        int n;
        while ((n = read(0, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: not found\n", argv[i]);
            continue;
        }
        char buf[256];
        int n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }
        close(fd);
    }
    return 0;
}
