// BrainixKernel (BKOS) — user/sh.c
// User-space shell v0.4: все команды — внешние .elf.
// Только help и exit — builtins.

#include "user.h"
#include "libc.h"

#define LINE_MAX 128
#define ARGV_MAX 16
#define STAGE_MAX 8

typedef struct {
    char *argv[ARGV_MAX];
    int   argc;
} stage_t;

static void builtin_help(void) {
    puts("builtins: help, exit");
    puts("external: ls, cat, echo, wc, test, ...");
    puts("pipes:    cmd1 | cmd2 | cmd3");
}

static void resolve_path(const char *in, char *out) {
    int has_ext = 0;
    for (int i = 0; in[i]; i++) {
        if (in[i] == '.') { has_ext = 1; break; }
    }
    strcpy(out, in);
    if (!has_ext) strcat(out, ".elf");
}

int main(void) {
    char line[LINE_MAX];
    stage_t stages[STAGE_MAX];

    printf("BKOS user-shell v0.4\n");
    printf("type 'help' for commands\n");

    for (;;) {
        write(1, "$ ", 2);

        int n = 0;
        while (n < LINE_MAX - 1) {
            char c;
            long r;
            while ((r = read(0, &c, 1)) <= 0) yield();
            if (c == '\r') continue;
            if (c == '\n') break;
            if (c == 0x7F || c == '\b') {
                if (n > 0) { n--; write(1, "\b \b", 3); }
                continue;
            }
            line[n++] = c;
            write(1, &c, 1);
        }
        line[n] = 0;
        write(1, "\n", 1);
        if (n == 0) continue;

        // Split по '|'.
        int n_stages = 0;
        char *p = line;
        while (*p && n_stages < STAGE_MAX) {
            stages[n_stages].argc = 0;
            while (*p && *p != '|') {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '|') break;
                stages[n_stages].argv[stages[n_stages].argc++] = p;
                while (*p && *p != ' ' && *p != '\t' && *p != '|') p++;
                if (*p) { *p++ = 0; }
            }
            stages[n_stages].argv[stages[n_stages].argc] = 0;
            if (stages[n_stages].argc > 0) n_stages++;
            if (*p == '|') p++;
        }
        if (n_stages == 0) continue;

        // Builtins — только если одна стадия.
        if (n_stages == 1) {
            const char *cmd0 = stages[0].argv[0];
            if (strcmp(cmd0, "exit") == 0) break;
            if (strcmp(cmd0, "help") == 0) { builtin_help(); continue; }
        }

        // Pipeline.
        int prev_read = -1;
        int pids[STAGE_MAX];
        int n_pids = 0;

        for (int s = 0; s < n_stages; s++) {
            int is_last = (s == n_stages - 1);
            int pfd[2] = {-1, -1};

            if (!is_last) {
                if (pipe(pfd) < 0) { puts("pipe failed"); break; }
            }

            int pid = fork();
            if (pid == 0) {
                if (prev_read >= 0) {
                    dup2(prev_read, 0);
                    close(prev_read);
                }
                if (!is_last) {
                    close(pfd[0]);
                    dup2(pfd[1], 1);
                    close(pfd[1]);
                }
                char path[64];
                resolve_path(stages[s].argv[0], path);
                exec(path, stages[s].argv);
                printf("sh: %s: not found\n", path);
                exit(127);
            } else if (pid > 0) {
                pids[n_pids++] = pid;
                if (prev_read >= 0) close(prev_read);
                if (!is_last) {
                    close(pfd[1]);
                    prev_read = pfd[0];
                }
            } else {
                puts("fork failed");
            }
        }
        if (prev_read >= 0) close(prev_read);

        for (int i = 0; i < n_pids; i++) {
            int r;
            while ((r = wait(pids[i])) == EAGAIN) yield();
        }
    }

    puts("bye");
    return 0;
}
