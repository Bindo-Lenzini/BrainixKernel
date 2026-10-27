#include "user.h"

static int atoi_(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) { puts("usage: kill <pid> <sig>\n"); return 1; }
    if (kill(atoi_(argv[1]), atoi_(argv[2])) < 0) { puts("kill: failed\n"); return 1; }
    return 0;
}
