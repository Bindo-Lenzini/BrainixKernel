// BrainixKernel — user/wc.c
// Считает строки, слова, байты из stdin.
#include "user.h"
#include "libc.h"

int main(void) {
    char buf[256];
    int lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    int n;

    while ((n = read(0, buf, sizeof(buf))) > 0) {
        bytes += n;
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') lines++;
            if (c == ' ' || c == '\t' || c == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    printf("%d %d %d\n", lines, words, bytes);
    return 0;
}
