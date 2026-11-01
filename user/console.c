#include "user.h"

#define CONSOLE_IPC_FD 3

int main(void) {
    static const char ready[] = "BRAINIX_CONSOLE_SERVER_READY\n";
    char input;
    char output[IPC_MESSAGE_MAX];

    if (write(STDOUT_FILENO, ready, sizeof(ready) - 1) !=
        (long)(sizeof(ready) - 1)) {
        return 1;
    }

    for (;;) {
        long got = read(STDIN_FILENO, &input, sizeof(input));
        if (got > 0) {
            for (;;) {
                long sent = ipc_send(CONSOLE_IPC_FD, &input, 1);
                if (sent == 1) break;
                if (sent != IPC_EAGAIN) return 1;
                yield();
            }
        } else if (got < 0 && got != IPC_EAGAIN) {
            return 1;
        }

        long received = ipc_recv(CONSOLE_IPC_FD, output, sizeof(output));
        if (received > 0 &&
            write(STDOUT_FILENO, output, (unsigned long)received) != received) {
            return 1;
        }
        if (received < 0 && received != IPC_EAGAIN) return 1;
        yield();
    }
}
