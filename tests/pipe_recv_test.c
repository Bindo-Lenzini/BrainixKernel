#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pipe.h"

static int check(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        return 0;
    }
    printf("PASS: %s\n", description);
    return 1;
}

int main(void) {
    int failures = 0;
    int idx = pipe_alloc();
    uint8_t output[8] = {0};

    failures += !check(idx >= 0, "pipe allocation");
    if (idx < 0) return 1;
    pipe_ref_read(idx);
    pipe_ref_write(idx);

    failures += !check(pipe_write(idx, "hello", 5) == 5,
                       "pipe accepts test data");
    failures += !check(pipe_peek(idx, output, 0) == 0 &&
                       pipe_peek(idx, output, sizeof(output)) == 5,
                       "zero-length peek preserves queued bytes");
    int peeked = pipe_peek(idx, output, sizeof(output));
    failures += !check(peeked == 5 && memcmp(output, "hello", 5) == 0 &&
                       pipe_peek(idx, output, sizeof(output)) == 5,
                       "peek leaves queued bytes available");
    failures += !check(!pipe_consume(idx, 6) &&
                       pipe_peek(idx, output, sizeof(output)) == 5,
                       "consume rejects lengths beyond queued data");
    failures += !check(pipe_consume(idx, 2) &&
                       pipe_peek(idx, output, sizeof(output)) == 3 &&
                       memcmp(output, "llo", 3) == 0,
                       "consume removes only the committed prefix");

    failures += !check(pipe_read(idx, output, sizeof(output)) == 3 &&
                       memcmp(output, "llo", 3) == 0 &&
                       pipe_peek(idx, output, sizeof(output)) == -1,
                       "legacy read remains peek plus consume");

    pipe_close_read(idx);
    pipe_close_write(idx);
    return failures != 0;
}
