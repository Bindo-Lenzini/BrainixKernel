#include "user.h"
#include "libc.h"

int main(void) {
    printf("=== security and COW tests ===\n");
    int failures = 0;

    if (getuid() == 1000 && geteuid() == 1000) {
        puts("[cred] unprivileged identity OK");
    } else {
        puts("[cred] unexpected user identity");
        failures++;
    }

    long kernel_read = write(
        1, (const void *)(uintptr_t)0xFFFF000000000000ULL, 1);
    if (kernel_read == -1) {
        puts("[uaccess] kernel address rejected");
    } else {
        puts("[uaccess] kernel address was not rejected");
        failures++;
    }

    int fds[2];
    int pipe_failures = 0;
    for (int i = 0; i < 16; i++) {
        if (pipe((int *)(uintptr_t)0x20000000ULL) == -1) pipe_failures++;
    }
    if (pipe_failures == 16) {
        puts("[uaccess] failed pipe outputs roll back resources");
    } else {
        puts("[uaccess] failed pipe outputs were not rejected");
        failures++;
    }

    if (pipe(fds) == 0) {
        write(fds[1], "x", 1);
        long unmapped_read = read(fds[0], (void *)(uintptr_t)0x20000000ULL, 1);
        if (unmapped_read == -1) {
            puts("[uaccess] unmapped user address rejected");
        } else {
            puts("[uaccess] unmapped user address was not rejected");
            failures++;
        }
        close(fds[0]);
        close(fds[1]);
    } else {
        puts("[uaccess] pipe setup failed");
    }

    // Prime the parent's writable TLB entry before fork.
    static volatile char shared_buf[4096];

    shared_buf[0]   = 'P';  // parent's mark
    shared_buf[4095] = '!';

    int pid = fork();

    if (pid == 0) {
        for (volatile int i = 0; i < 50000000; i++) {}
        char child_view = shared_buf[0];
        printf("[child] parent write isolated: %s\n",
               child_view == 'P' ? "yes" : "no");
        shared_buf[0]   = 'C';
        shared_buf[4095] = '?';
        exit(child_view == 'P' ? 0 : 1);
    } else if (pid > 0) {
        // This write must COW before the child reads the original byte.
        shared_buf[0] = 'R';

        int child_status;
        while ((child_status = wait(pid)) == -2) yield();

        if (child_status == 0 && shared_buf[0] == 'R' &&
            shared_buf[4095] == '!') {
            puts("[parent] COW isolation OK");
        } else {
            puts("[parent] COW isolation FAILED");
            failures++;
        }
    } else {
        puts("fork failed");
        failures++;
    }
    if (failures == 0) puts("BRAINIX_GUEST_SECURITY_OK");
    return failures == 0 ? 0 : 1;
}
