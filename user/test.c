#include "user.h"
#include "libc.h"

int main(void) {
    printf("=== COW test ===\n");

    // Большой статический буфер — при fork шарится COW.
    static char shared_buf[4096];

    shared_buf[0]   = 'P';  // parent's mark
    shared_buf[4095] = '!';

    int pid = fork();

    if (pid == 0) {
        // Ребёнок: меняет буфер.
        printf("[child] before: shared_buf[0]   = %c\n", shared_buf[0]);
        shared_buf[0]   = 'C';
        shared_buf[4095] = '?';
        printf("[child] after:  shared_buf[0]   = %c\n", shared_buf[0]);

        // Спим немного, чтобы родитель успел.
        for (volatile int i = 0; i < 50000000; i++) {}
        exit(0);
    } else if (pid > 0) {
        // Родитель: ждёт и проверяет, что его буфер не изменился.
        int r;
        while ((r = wait(pid)) == -2) yield();
        printf("[parent] after child exit: shared_buf[0]   = %c\n", shared_buf[0]);
        printf("[parent] after child exit: shared_buf[4095] = %c\n", shared_buf[4095]);

        if (shared_buf[0] == 'P' && shared_buf[4095] == '!') {
            puts("[parent] COW OK: my buffer unchanged");
        } else {
            puts("[parent] COW BROKEN: child's writes leaked into parent");
        }
    } else {
        puts("fork failed");
    }
    return 0;
}
