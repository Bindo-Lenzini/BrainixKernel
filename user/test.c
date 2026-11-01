#include "user.h"
#include "libc.h"

#define IPC_SHARED_TEST_VA 0x30000000ULL
#define IPC_SHARED_ALIAS_VA (IPC_SHARED_TEST_VA + IPC_SHARED_PAGE_SIZE)

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--exec-cycle") == 0) {
        int remaining = atoi(argv[2]);
        if (remaining <= 0) return 0;

        char next_count[12];
        itoa(remaining - 1, next_count);
        char *next_argv[] = {"test.elf", "--exec-cycle", next_count, 0};
        return exec("test.elf", next_argv) == -1 ? 1 : 2;
    }

    printf("=== security and COW tests ===\n");
    int failures = 0;

    if (getuid() == 1000 && geteuid() == 1000) {
        puts("[cred] unprivileged identity OK");
    } else {
        puts("[cred] unexpected user identity");
        failures++;
    }

    int readonly_fd = open("hello.txt", O_RDONLY);
    if (readonly_fd >= 0) {
        char byte;
        if (write(readonly_fd, "x", 1) == -1 &&
            read(readonly_fd, &byte, 1) == 1) {
            puts("[auth] read-only initrd descriptor remains readable");
        } else {
            puts("[auth] read-only initrd descriptor behavior failed");
            failures++;
        }
        close(readonly_fd);
    } else {
        puts("[auth] initrd read-only open failed");
        failures++;
    }

    if (open("test.elf", O_WRONLY) == -1) {
        puts("[auth] writable initrd open rejected");
    } else {
        puts("[auth] writable initrd open accepted");
        failures++;
    }

    char before_trunc = 0;
    char after_trunc = 0;
    int before_fd = open("test.elf", O_RDONLY);
    int before_ok = before_fd >= 0 && read(before_fd, &before_trunc, 1) == 1;
    if (before_fd >= 0) close(before_fd);

    int trunc_fd = open("test.elf", O_RDONLY | O_TRUNC);
    if (trunc_fd >= 0) close(trunc_fd);
    int after_fd = open("test.elf", O_RDONLY);
    int after_ok = after_fd >= 0 && read(after_fd, &after_trunc, 1) == 1;
    if (after_fd >= 0) close(after_fd);
    if (before_ok && trunc_fd == -1 && after_ok &&
        before_trunc == after_trunc) {
        puts("[auth] O_RDONLY|O_TRUNC rejected without truncation");
    } else {
        puts("[auth] O_RDONLY|O_TRUNC rejection or file integrity failed");
        failures++;
    }

    if (open("hello.txt", (int)0xFFFFFFFFu) == -1) {
        puts("[auth] all-bits-set open flags rejected");
    } else {
        puts("[auth] all-bits-set open flags accepted");
        failures++;
    }

    int owned_fd = open("auth-user.tmp", O_CREAT | O_RDWR);
    if (owned_fd >= 0) {
        int wrote = write(owned_fd, "u", 1) == 1;
        close(owned_fd);
        int chmod_owner = chmod("auth-user.tmp", 0600);
        int chmod_group = chmod("auth-user.tmp", 0200);
        int read_denied = open("auth-user.tmp", O_RDONLY) == -1;
        int write_allowed = open("auth-user.tmp", O_WRONLY);
        if (write_allowed >= 0) close(write_allowed);
        int chmod_restore = chmod("auth-user.tmp", 0640);
        owned_fd = open("auth-user.tmp", O_RDONLY);
        char owned_byte = 0;
        if (wrote && chmod_owner == 0 && chmod_group == 0 &&
            read_denied && write_allowed >= 0 && chmod_restore == 0 &&
            chmod("auth-user.tmp", 010000) == -1 &&
            chown("auth-user.tmp", 2000, -1) == -1 &&
            chown("auth-user.tmp", 65536, -1) == -1 &&
            chown("auth-user.tmp", -1, 1000) == 0 &&
            owned_fd >= 0 &&
            write(owned_fd, "x", 1) == -1 &&
            read(owned_fd, &owned_byte, 1) == 1 && owned_byte == 'u') {
            puts("[auth] BKFS chmod/chown and descriptor permissions enforced");
        } else {
            puts("[auth] BKFS chmod/chown or descriptor permissions failed");
            failures++;
        }
        if (owned_fd >= 0) close(owned_fd);
        if (exec("auth-user.tmp", 0) == -1) {
            puts("[auth] non-executable BKFS file rejected by exec");
        } else {
            puts("[auth] non-executable BKFS file was executed");
            failures++;
        }
    } else {
        puts("[auth] unprivileged BKFS file creation failed");
        failures++;
    }

    unsigned int extra_group = 1000;
    unsigned int groups_out[8] = {0};
    unsigned int *bad_groups = (unsigned int *)(uintptr_t)0x20000000ULL;
    if (getgroups(0, 0) == 0 &&
        getgroups(1, groups_out) == 0 &&
        getgroups(-1, groups_out) == -1 &&
        getgroups(1, bad_groups) == 0 &&
        setgroups(1, &extra_group) == -1 &&
        setgroups(1, bad_groups) == -1 &&
        setgroups(-1, &extra_group) == -1 &&
        setgroups(9, &extra_group) == -1 &&
        getgroups(0, 0) == 0) {
        puts("[cred] group query, pointer/range checks and privilege guard enforced");
    } else {
        puts("[cred] supplementary group validation or privilege guard failed");
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

    int ipc_bad_fds = 0;
    int ipc_fds[2] = {-1, -1};
    int transfer_fds[2] = {-1, -1};
    for (int i = 0; i < 16; i++) {
        if (ipc_pair((int *)(uintptr_t)0x20000000ULL) == -1)
            ipc_bad_fds++;
    }
    int pair_result = ipc_pair(ipc_fds);
    int transfer_pair_result = ipc_pair(transfer_fds);
    int ipc_ok = ipc_bad_fds == 16 && pair_result == 0 &&
                 transfer_pair_result == 0;
    if (ipc_bad_fds != 16) puts("[ipc] invalid-output rollback failed");
    if (pair_result != 0) puts("[ipc] endpoint pair creation failed");
    if (transfer_pair_result != 0)
        puts("[ipc] transfer endpoint pair creation failed");
    if (ipc_ok) {
        ipc_ok = ipc_map(ipc_fds[0],
                         (void *)(uintptr_t)IPC_SHARED_TEST_VA) == 0 &&
                 ipc_map(ipc_fds[0],
                         (void *)(uintptr_t)IPC_SHARED_TEST_VA) == -1;
        if (!ipc_ok) puts("[ipc] shared page mapping or duplicate rejection failed");
    }
    if (ipc_ok) {
        int ipc_pid = fork();
        if (ipc_pid == 0) {
            close(ipc_fds[0]);
            close(transfer_fds[0]);
            close(transfer_fds[1]);
            volatile uint8_t *shared =
                (volatile uint8_t *)(uintptr_t)IPC_SHARED_TEST_VA;
            volatile uint8_t *alias =
                (volatile uint8_t *)(uintptr_t)IPC_SHARED_ALIAS_VA;
            if (ipc_map(ipc_fds[1],
                        (void *)(uintptr_t)IPC_SHARED_ALIAS_VA) != 0) {
                puts("[ipc child] shared alias mapping failed");
                exit(31);
            }
            while (shared[1] != 1) yield();
            if (alias[0] != 0x53) {
                puts("[ipc child] inherited shared data mismatch");
                exit(32);
            }
            alias[0] = 0x43;
            shared[1] = 2;

            int received_endpoint = -1;
            unsigned int received_rights = 0;
            char transfer_message[16];
            long invalid_fd_output = IPC_EAGAIN;
            char forbidden_message[IPC_MESSAGE_MAX];
            for (int i = 0; i < 10000 &&
                 invalid_fd_output == IPC_EAGAIN; i++) {
                invalid_fd_output = ipc_recv_endpoint(
                    ipc_fds[1], transfer_message, sizeof(transfer_message),
                    (int *)(uintptr_t)0x20000000ULL, &received_rights);
                if (invalid_fd_output == IPC_EAGAIN) yield();
            }
            long invalid_rights_output = ipc_recv_endpoint(
                ipc_fds[1], transfer_message, sizeof(transfer_message),
                &received_endpoint,
                (unsigned int *)(uintptr_t)0x20000000ULL);
            long plain_received = IPC_EAGAIN;
            for (int i = 0; i < 10000 &&
                 plain_received == IPC_EAGAIN; i++) {
                plain_received = ipc_recv(ipc_fds[1], transfer_message,
                                          sizeof(transfer_message));
                if (plain_received == IPC_EAGAIN) yield();
            }
            long transfer_received = IPC_EAGAIN;
            for (int i = 0; i < 10000 &&
                 transfer_received == IPC_EAGAIN; i++) {
                transfer_received = ipc_recv_endpoint(
                    ipc_fds[1], transfer_message, sizeof(transfer_message),
                    &received_endpoint, &received_rights);
                if (transfer_received == IPC_EAGAIN) yield();
            }
            if (invalid_fd_output != -1 || invalid_rights_output != -1 ||
                plain_received != -1 ||
                transfer_received != 6 ||
                memcmp(transfer_message, "grant", 6) != 0 ||
                received_endpoint < 0 ||
                received_rights != IPC_RIGHT_SEND ||
                ipc_recv(received_endpoint, forbidden_message,
                         sizeof(forbidden_message)) != -1 ||
                ipc_map(received_endpoint, (void *)(uintptr_t)
                        (IPC_SHARED_TEST_VA + 0x1000)) != -1 ||
                ipc_send_endpoint(ipc_fds[1], "x", 2, received_endpoint,
                                  IPC_RIGHT_SEND) != -1 ||
                ipc_send(received_endpoint, "granted", 8) != 8) {
                puts("[ipc child] endpoint delegation failed");
                exit(33);
            }
            close(received_endpoint);

            char request[IPC_MESSAGE_MAX];
            long received = IPC_EAGAIN;
            for (int i = 0; i < 10000 && received == IPC_EAGAIN; i++) {
                received = ipc_recv(ipc_fds[1], request, sizeof(request));
                if (received == IPC_EAGAIN) yield();
            }
            long reply = ipc_send(ipc_fds[1], "world", 6);
            if (received != 6 || memcmp(request, "hello", 6) != 0 ||
                reply != 6) {
                printf("[ipc child] received=%d reply=%d\n",
                       (int)received, (int)reply);
                puts("[ipc child] request validation or reply send failed");
                exit(30);
            }
            exit(0);
        } else if (ipc_pid > 0) {
            close(ipc_fds[1]);
            volatile uint8_t *shared =
                (volatile uint8_t *)(uintptr_t)IPC_SHARED_TEST_VA;
            shared[0] = 0x53;
            shared[1] = 1;
            while (shared[1] != 2) yield();
            ipc_ok = shared[0] == 0x43;
            if (!ipc_ok) puts("[ipc] shared alias writes were not visible");

            long transfer_sent = ipc_send_endpoint(
                ipc_fds[0], "grant", 6, transfer_fds[1], IPC_RIGHT_SEND);
            close(transfer_fds[1]);
            char transfer_response[16];
            long transfer_reply = IPC_EAGAIN;
            for (int i = 0; i < 10000 &&
                 transfer_reply == IPC_EAGAIN; i++) {
                transfer_reply = ipc_recv(transfer_fds[0], transfer_response,
                                          sizeof(transfer_response));
                if (transfer_reply == IPC_EAGAIN) yield();
            }
            ipc_ok = ipc_ok && transfer_sent == 6 &&
                     transfer_reply == 8 &&
                     memcmp(transfer_response, "granted", 8) == 0;
            if (!ipc_ok) puts("[ipc] delegated endpoint was not usable");
            close(transfer_fds[0]);

            long sent = ipc_send(ipc_fds[0], "hello", 6);
            ipc_ok = ipc_ok && sent == 6;
            if (!ipc_ok) puts("[ipc] request send failed");

            char response[IPC_MESSAGE_MAX];
            long received = IPC_EAGAIN;
            for (int i = 0; i < 10000 && received == IPC_EAGAIN; i++) {
                received = ipc_recv(ipc_fds[0],
                                    (void *)(uintptr_t)0x20000000ULL,
                                    sizeof(response));
                if (received == -1) break;
                if (received == IPC_EAGAIN) yield();
            }
            if (received != -1) puts("[ipc] invalid receive did not fail as expected");
            ipc_ok = ipc_ok && received == -1;
            received = IPC_EAGAIN;
            for (int i = 0; i < 10000 && received == IPC_EAGAIN; i++) {
                received = ipc_recv(ipc_fds[0], response, sizeof(response));
                if (received == IPC_EAGAIN) yield();
            }
            int child_status = IPC_EAGAIN;
            for (int i = 0; i < 10000 && child_status == IPC_EAGAIN; i++) {
                child_status = wait(ipc_pid);
                if (child_status == IPC_EAGAIN) yield();
            }
            if (received != 6 || memcmp(response, "world", 6) != 0)
                puts("[ipc] response receive failed");
            if (child_status != 0) puts("[ipc] child exited with an error");
            ipc_ok = ipc_ok && received == 6 &&
                     memcmp(response, "world", 6) == 0 &&
                     child_status == 0;
            close(ipc_fds[0]);
        } else {
            ipc_ok = 0;
            close(ipc_fds[0]);
            close(ipc_fds[1]);
            close(transfer_fds[0]);
            close(transfer_fds[1]);
        }
    }
    if (ipc_ok) {
        puts("[ipc] shared-page alias, IPC, and endpoint delegation passed");
    } else {
        puts("[ipc] endpoint exchange or rollback failed");
        failures++;
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

    int exec_pid = fork();
    if (exec_pid == 0) {
        char *exec_argv[] = {"test.elf", "--exec-cycle", "260", 0};
        if (exec("test.elf", exec_argv) == -1) exit(1);
        exit(2);
    } else if (exec_pid > 0) {
        int exec_status;
        while ((exec_status = wait(exec_pid)) == -2) yield();
        if (exec_status == 0) {
            puts("[exec] repeated exec reclaims address spaces and ASIDs");
        } else {
            puts("[exec] repeated exec failed");
            failures++;
        }
    } else {
        puts("[exec] test fork failed");
        failures++;
    }

    int reap_fds[2];
    if (pipe(reap_fds) == 0) {
        int reap_failures = 0;
        for (int i = 0; i < 260; i++) {
            int reap_pid = fork();
            if (reap_pid == 0) exit(0);
            if (reap_pid < 0) {
                reap_failures++;
                break;
            }

            int reap_status;
            while ((reap_status = wait(reap_pid)) == -2) yield();
            if (reap_status != 0) {
                reap_failures++;
                break;
            }
        }

        char probe = 'R';
        char received = 0;
        if (reap_failures == 0 &&
            (write(reap_fds[1], &probe, 1) != 1 ||
             read(reap_fds[0], &received, 1) != 1 || received != probe)) {
            reap_failures++;
        }
        close(reap_fds[0]);
        close(reap_fds[1]);
        if (reap_failures == 0) {
            puts("[teardown] 260 fork/wait cycles reclaimed task resources");
        } else {
            puts("[teardown] repeated fork/wait cleanup failed");
            failures++;
        }
    } else {
        puts("[teardown] pipe setup failed");
        failures++;
    }

    int fault_pid = fork();
    if (fault_pid == 0) {
        *(volatile uint64_t *)(uintptr_t)0x20000000ULL = 1;
        exit(0);
    } else if (fault_pid > 0) {
        int fault_status;
        while ((fault_status = wait(fault_pid)) == -2) yield();
        if (fault_status != 0 && fault_status != -1) {
            puts("[teardown] faulted child reaped");
        } else {
            puts("[teardown] faulted child was not reaped");
            failures++;
        }
    } else {
        puts("[teardown] fault test fork failed");
        failures++;
    }

    if (failures == 0) puts("BRAINIX_GUEST_SECURITY_OK");
    else puts("BRAINIX_GUEST_SECURITY_FAIL");
    return failures == 0 ? 0 : 1;
}
