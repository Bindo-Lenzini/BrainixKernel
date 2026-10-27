#include "user.h"

static volatile int got = 0;

static void on_usr1(int sig) { got = sig; }

int main(void) {
    struct sigaction sa;
    sa.sa_handler = on_usr1;
    sa.sa_mask = 0;
    sigaction(SIGUSR1, &sa, 0);

    print("sigtest pid="); printu(getpid()); print(" waiting for SIGUSR1\n");
    while (!got) { /* ждём */ }
    print("sigtest: got signal, OK\n");
    return 0;
}
