// BrainixKernel — user/ls.c
#include "user.h"
#include "libc.h"

#define MAX_FILES 64

int main(void) {
    static char names[MAX_FILES][64];
    int n = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        int r = readdir(i, names[n], 64);
        if (r < 0) break;
        n++;
    }

    // Простой selection sort (файлов мало).
    for (int i = 0; i < n - 1; i++) {
        int min = i;
        for (int j = i + 1; j < n; j++) {
            if (strcmp(names[j], names[min]) < 0) min = j;
        }
        if (min != i) {
            char tmp[64];
            strcpy(tmp, names[i]);
            strcpy(names[i], names[min]);
            strcpy(names[min], tmp);
        }
    }

    for (int i = 0; i < n; i++) {
        printf("%s\n", names[i]);
    }
    return 0;
}
