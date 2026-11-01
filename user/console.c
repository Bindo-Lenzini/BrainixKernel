#include "user.h"

#define CONSOLE_IPC_FD 3

int main(int argc, char **argv) {
    static const char ready[] = "BRAINIX_CONSOLE_SERVER_READY\n";
    char input;
    char output[IPC_MESSAGE_MAX];

    if (argc != 2) return 1;
    if (argv[1][0] == 'i') {
        for (;;) {
            long got = read(STDIN_FILENO, &input, sizeof(input));
            if (got == 1) {
                long sent = ipc_send_wait(CONSOLE_IPC_FD, &input, 1);
                if (sent != 1) return 1;
            } else if (got != 0 && got != IPC_EAGAIN) {
                return 1;
            }
        }
    }
    if (argv[1][0] != 'o') return 1;

    if (write(STDOUT_FILENO, ready, sizeof(ready) - 1) !=
        (long)(sizeof(ready) - 1)) {
        return 1;
    }
    for (;;) {
        long received = ipc_recv_wait(CONSOLE_IPC_FD, output, sizeof(output));
        if (received > 0 &&
            write(STDOUT_FILENO, output, (unsigned long)received) != received) {
            return 1;
        }
        if (received == 0) return 0;
        if (received < 0 && received != IPC_EAGAIN) return 1;
    }
}
