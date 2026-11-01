#include "user.h"
#include "libc.h"
#include "vfs.h"

#define IPC_SHARED_TEST_VA 0x30000000ULL
#define IPC_SHARED_ALIAS_VA (IPC_SHARED_TEST_VA + IPC_SHARED_PAGE_SIZE)

static int exec_rejects_bad_argv(char *const *argv) {
    int child = fork();
    if (child == 0) {
        exit(exec("whoami.elf", argv) == -1 ? 42 : 43);
    }
    if (child < 0) return 0;

    int status = IPC_EAGAIN;
    while (status == IPC_EAGAIN) {
        status = wait(child);
        if (status == IPC_EAGAIN) yield();
    }
    return status == 42;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--vfs-read-fd") == 0) {
        int fd = atoi(argv[2]);
        char byte = 0;
        return vfs_fd_control(fd, 0) ==
                (FD_RIGHT_READ | FD_RIGHT_DELEGATE) &&
            fd_restrict(fd, FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
            write(fd, "x", 1) == -1 &&
            read(fd, &byte, 1) == 1 && byte == 'H' ? 0 : 1;
    }

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

    char **bad_argv_vector = (char **)(uintptr_t)0x20000000ULL;
    char *bad_argv_string[] = {
        "whoami.elf", (char *)(uintptr_t)0x20000000ULL, 0
    };
    char *unterminated_argv[17];
    for (int i = 0; i < 17; i++) unterminated_argv[i] = "arg";
    char long_argument[128];
    memset(long_argument, 'x', sizeof(long_argument));
    char *long_argv[] = {"whoami.elf", long_argument, 0};
    if (exec_rejects_bad_argv(bad_argv_vector) &&
        exec_rejects_bad_argv(bad_argv_string) &&
        exec_rejects_bad_argv(unterminated_argv) &&
        exec_rejects_bad_argv(long_argv)) {
        puts("[exec] malformed argv rejected at syscall boundary");
    } else {
        puts("[exec] malformed argv was accepted");
        failures++;
    }

    int vfs_initrd_fd = vfs_open("hello.txt", O_RDONLY);
    char vfs_initrd_data[14] = {0};
    int vfs_initrd_ok = vfs_initrd_fd >= 0 &&
        read(vfs_initrd_fd, vfs_initrd_data, sizeof(vfs_initrd_data) - 1) == 13 &&
        memcmp(vfs_initrd_data, "Hello, world!", 13) == 0 &&
        vfs_open("hello.txt", O_WRONLY) == -1 &&
        vfs_open("vfs-missing.tmp", O_RDONLY) == -1;
    int backend_open_denied =
        open_backend("hello.txt", O_RDONLY) == -1;
    int vfs_rights_fds[2] = {-1, -1};
    int vfs_proxy_isolated = vfs_initrd_fd >= 0 &&
        ipc_pair(vfs_rights_fds) == 0 &&
        vfs_fd_control(vfs_rights_fds[0], FD_RIGHT_READ) == -1 &&
        vfs_fd_control(vfs_rights_fds[0], VFS_FD_REGISTER |
                        FD_RIGHT_READ) == -1 &&
        ipc_send_file(vfs_rights_fds[0], "x", 2, vfs_initrd_fd,
                      FD_RIGHT_READ) == -1;
    if (vfs_rights_fds[0] >= 0) close(vfs_rights_fds[0]);
    if (vfs_rights_fds[1] >= 0) close(vfs_rights_fds[1]);
    if (vfs_initrd_fd >= 0) close(vfs_initrd_fd);

    int vfs_bkfs_fd = vfs_open("vfs-broker.tmp",
                               O_CREAT | O_RDWR | O_TRUNC);
    int vfs_bkfs_write = vfs_bkfs_fd >= 0
        ? (int)write(vfs_bkfs_fd, "broker", 7) : -1;
    if (vfs_bkfs_fd >= 0) close(vfs_bkfs_fd);
    vfs_bkfs_fd = vfs_open("vfs-broker.tmp", O_RDONLY);
    char vfs_bkfs_data[8] = {0};
    int vfs_bkfs_ok = vfs_bkfs_fd >= 0 &&
        read(vfs_bkfs_fd, vfs_bkfs_data, sizeof(vfs_bkfs_data) - 1) == 7 &&
        memcmp(vfs_bkfs_data, "broker", 7) == 0;
    if (vfs_bkfs_fd >= 0) close(vfs_bkfs_fd);
    int vfs_exec_fd = vfs_open("hello.txt", O_RDONLY);
    int vfs_exec_ok = 0;
    if (vfs_exec_fd >= 0) {
        char fd_text[12];
        itoa(vfs_exec_fd, fd_text);
        char *exec_args[] = {"test.elf", "--vfs-read-fd", fd_text, 0};
        int child = fork();
        if (child == 0) {
            exec("test.elf", exec_args);
            exit(1);
        }
        if (child > 0) {
            int status = IPC_EAGAIN;
            while (status == IPC_EAGAIN) {
                status = wait(child);
                if (status == IPC_EAGAIN) yield();
            }
            vfs_exec_ok = status == 0;
        }
        close(vfs_exec_fd);
    }
    int vfs_delegate_fd = vfs_open("hello.txt", O_RDONLY);
    int vfs_transport[2] = {-1, -1};
    int vfs_transfer_ok = 0;
    if (vfs_delegate_fd >= 0 &&
        ipc_pair(vfs_transport) == 0) {
        long escalation = ipc_send_vfs(
            vfs_transport[0], "x", 2, vfs_delegate_fd,
            FD_RIGHT_READ | FD_RIGHT_WRITE | FD_RIGHT_DELEGATE);
        long sent = ipc_send_vfs(vfs_transport[0], "file", 5,
                                 vfs_delegate_fd, FD_RIGHT_READ);
        int child = sent == 5 ? fork() : -1;
        if (child == 0) {
            close(vfs_transport[0]);
            close(vfs_delegate_fd);
            int received_fd = -1;
            unsigned int received_rights = 0;
            char message[8];
            long received = ipc_recv_vfs(vfs_transport[1], message,
                                         sizeof(message), &received_fd,
                                         &received_rights);
            char byte = 0;
            int valid = received == 5 &&
                memcmp(message, "file", 5) == 0 &&
                received_fd >= 0 &&
                received_rights == FD_RIGHT_READ &&
                vfs_fd_control(received_fd, 0) == FD_RIGHT_READ &&
                fd_restrict(received_fd,
                            FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
                write(received_fd, "x", 1) == -1 &&
                read(received_fd, &byte, 1) == 1 && byte == 'H';
            if (received_fd >= 0) close(received_fd);
            exit(valid ? 0 : 1);
        } else if (child > 0) {
            close(vfs_transport[1]);
            close(vfs_delegate_fd);
            vfs_delegate_fd = -1;
            int status = IPC_EAGAIN;
            while (status == IPC_EAGAIN) {
                status = wait(child);
                if (status == IPC_EAGAIN) yield();
            }
            vfs_transfer_ok = escalation == -1 && status == 0;
        }
    }
    if (vfs_delegate_fd >= 0) close(vfs_delegate_fd);
    if (vfs_transport[0] >= 0) close(vfs_transport[0]);
    if (vfs_transport[1] >= 0) close(vfs_transport[1]);
    char vfs_large_data[300];
    char vfs_large_result[300] = {0};
    for (int i = 0; i < (int)sizeof(vfs_large_data); i++)
        vfs_large_data[i] = (char)('A' + i % 26);
    int vfs_large_fd = vfs_open("vfs-large.tmp",
                                O_CREAT | O_RDWR | O_TRUNC);
    long vfs_large_first_write = vfs_large_fd >= 0
        ? write(vfs_large_fd, vfs_large_data, sizeof(vfs_large_data)) : -1;
    long vfs_large_second_write = vfs_large_first_write >= 0
        ? write(vfs_large_fd, vfs_large_data + vfs_large_first_write,
                sizeof(vfs_large_data) - (unsigned long)vfs_large_first_write)
        : -1;
    if (vfs_large_fd >= 0) close(vfs_large_fd);
    vfs_large_fd = vfs_open("vfs-large.tmp", O_RDONLY);
    long vfs_large_first_read = vfs_large_fd >= 0
        ? read(vfs_large_fd, vfs_large_result, sizeof(vfs_large_result)) : -1;
    long vfs_large_second_read = vfs_large_first_read >= 0
        ? read(vfs_large_fd, vfs_large_result + vfs_large_first_read,
               sizeof(vfs_large_result) - (unsigned long)vfs_large_first_read)
        : -1;
    long vfs_large_eof = vfs_large_fd >= 0
        ? read(vfs_large_fd, vfs_large_result, 1) : -1;
    if (vfs_large_fd >= 0) close(vfs_large_fd);
    int vfs_chunking_ok = vfs_large_first_write == VFS_IO_DATA_MAX &&
        vfs_large_second_write == 300 - (long)VFS_IO_DATA_MAX &&
        vfs_large_first_read == VFS_IO_DATA_MAX &&
        vfs_large_second_read == 300 - (long)VFS_IO_DATA_MAX &&
        vfs_large_eof == 0 &&
        memcmp(vfs_large_data, vfs_large_result,
               sizeof(vfs_large_data)) == 0;
    int vfs_reopen_ok = 1;
    for (int i = 0; i < 8; i++) {
        int fd = vfs_open("hello.txt", O_RDONLY);
        if (fd < 0 || close(fd) < 0) {
            vfs_reopen_ok = 0;
            break;
        }
    }
    if (vfs_initrd_ok && backend_open_denied && vfs_proxy_isolated &&
        vfs_bkfs_write == 7 && vfs_bkfs_ok && vfs_exec_ok &&
        vfs_transfer_ok &&
        vfs_chunking_ok && vfs_reopen_ok) {
        puts("[vfs] proxy I/O, transfer/attenuation, chunking, EOF, close/reopen, fork/exec and backend isolation passed");
    } else {
        printf("[vfs debug] initrd=%d backend=%d proxy=%d bkfs_write=%d bkfs=%d exec=%d transfer=%d chunk=%d reopen=%d wr=%d/%d rd=%d/%d eof=%d\n",
               vfs_initrd_ok, backend_open_denied, vfs_proxy_isolated,
               vfs_bkfs_write, vfs_bkfs_ok, vfs_exec_ok,
               vfs_transfer_ok, vfs_chunking_ok, vfs_reopen_ok,
               (int)vfs_large_first_write, (int)vfs_large_second_write,
               (int)vfs_large_first_read, (int)vfs_large_second_read,
               (int)vfs_large_eof);
        puts("[vfs] user-space open broker failed");
        failures++;
    }

    int readonly_fd = open("hello.txt", O_RDONLY);
    if (readonly_fd >= 0) {
        char byte;
        vfs_io_request_t unauthorized_write = {
            .magic = VFS_IO_MAGIC,
            .operation = VFS_OP_WRITE,
            .length = 1,
        };
        unauthorized_write.data[0] = 'x';
        int restricted_dup = -1;
        int restrict_ok = write(readonly_fd, "x", 1) == -1 &&
                          read(readonly_fd, &byte, 1) == 1 &&
                          vfs_fd_control(readonly_fd, 0) ==
                              (FD_RIGHT_READ | FD_RIGHT_DELEGATE) &&
                          fd_restrict(readonly_fd, FD_RIGHT_READ) == 0 &&
                          fd_restrict(readonly_fd,
                                      FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
                          ipc_send(readonly_fd, &unauthorized_write,
                                   VFS_IO_HEADER_SIZE + 1) == -1;
        if (restrict_ok) restricted_dup = dup2(readonly_fd, 15);
        if (restrict_ok && restricted_dup == 15 &&
            read(restricted_dup, &byte, 1) == 1 &&
            write(restricted_dup, "x", 1) == -1 &&
            fd_restrict(restricted_dup, FD_RIGHT_READ | FD_RIGHT_WRITE) ==
                -1) {
            puts("[auth] VFS proxy rights deny direct IPC writes and survive dup2");
        } else {
            puts("[auth] file descriptor attenuation or dup2 failed");
            failures++;
        }
        if (restricted_dup >= 0) close(restricted_dup);
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
        owned_fd = open("auth-user.tmp", O_RDWR);
        int rights_restricted = owned_fd >= 0 &&
            fd_restrict(owned_fd, FD_RIGHT_READ) == 0 &&
            fd_restrict(owned_fd, FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
            vfs_fd_control(owned_fd, FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
            write(owned_fd, "x", 1) == -1;
        int rights_dup = rights_restricted ? dup2(owned_fd, 15) : -1;
        int rights_pid = rights_dup >= 0 ? fork() : -1;
        int inherited_rights_ok = 0;
        if (rights_pid == 0) {
            char child_byte = 0;
            exit(fd_restrict(rights_dup, FD_RIGHT_ALL) == -1 &&
                 vfs_fd_control(rights_dup,
                                FD_RIGHT_READ | FD_RIGHT_WRITE) == -1 &&
                 write(rights_dup, "x", 1) == -1 &&
                 read(rights_dup, &child_byte, 1) == 1 &&
                 child_byte == 'u' ? 0 : 1);
        } else if (rights_pid > 0) {
            int child_status = IPC_EAGAIN;
            while (child_status == IPC_EAGAIN) {
                child_status = wait(rights_pid);
                if (child_status == IPC_EAGAIN) yield();
            }
            inherited_rights_ok = child_status == 0;
        }
        char owned_byte = 0;
        if (wrote && chmod_owner == 0 && chmod_group == 0 &&
            read_denied && write_allowed >= 0 && chmod_restore == 0 &&
            chmod("auth-user.tmp", 010000) == -1 &&
            chown("auth-user.tmp", 2000, -1) == -1 &&
            chown("auth-user.tmp", 65536, -1) == -1 &&
            chown("auth-user.tmp", -1, 1000) == 0 &&
            owned_fd >= 0 && rights_restricted && rights_dup == 15 &&
            rights_pid > 0 && inherited_rights_ok &&
            read(owned_fd, &owned_byte, 1) == 0) {
            puts("[auth] VFS rights attenuation survives dup2 and fork");
        } else {
            puts("[auth] BKFS permissions or fd-right attenuation failed");
            failures++;
        }
        if (rights_dup >= 0) close(rights_dup);
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
    int invalid_rights_fds[2] = {-1, -1};
    int invalid_rights_rejected =
        ipc_pair_rights(invalid_rights_fds, 0, IPC_RIGHT_ALL) == -1 &&
        ipc_pair_rights(invalid_rights_fds, IPC_RIGHT_ALL | 0x10,
                        IPC_RIGHT_ALL) == -1;
    for (int i = 0; i < 16; i++) {
        if (ipc_pair((int *)(uintptr_t)0x20000000ULL) == -1)
            ipc_bad_fds++;
    }
    int pair_result = ipc_pair(ipc_fds);
    int transfer_pair_result = ipc_pair_rights(
        transfer_fds, IPC_RIGHT_ALL,
        IPC_RIGHT_SEND | IPC_RIGHT_DELEGATE);
    int ipc_ok = ipc_bad_fds == 16 && invalid_rights_rejected &&
                 pair_result == 0 &&
                 transfer_pair_result == 0;
    if (ipc_ok && fd_restrict(ipc_fds[0], 0) != -1) {
        ipc_ok = 0;
        puts("[ipc] file-rights restriction accepted a non-file descriptor");
    }
    if (!invalid_rights_rejected)
        puts("[ipc] invalid endpoint-rights policy was accepted");
    if (ipc_bad_fds != 16) puts("[ipc] invalid-output rollback failed");
    if (pair_result != 0) puts("[ipc] endpoint pair creation failed");
    if (transfer_pair_result != 0)
        puts("[ipc] transfer endpoint pair creation failed");
    if (ipc_ok) {
        char denied_message[8];
        ipc_ok = ipc_recv(transfer_fds[1], denied_message,
                          sizeof(denied_message)) == -1 &&
                 ipc_map(transfer_fds[1],
                         (void *)(uintptr_t)(IPC_SHARED_TEST_VA + 0x1000)) ==
                     -1;
        if (!ipc_ok)
            puts("[ipc] endpoint creation policy did not restrict rights");
    }
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

            long escalation_attempt = ipc_send_endpoint(
                ipc_fds[0], "grant", 6, transfer_fds[1], IPC_RIGHT_ALL);
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
            ipc_ok = ipc_ok && escalation_attempt == -1 &&
                     transfer_sent == 6 &&
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
