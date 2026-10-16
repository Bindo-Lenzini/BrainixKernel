// BrainixKernel (BKOS) — clipboard.c

#include "clipboard.h"

static char g_clip[CLIP_MAX];
static int  g_clip_len = 0;

void clip_set(const char *data, int len) {
    if (len < 0) len = 0;
    if (len > CLIP_MAX) len = CLIP_MAX;
    for (int i = 0; i < len; i++) g_clip[i] = data[i];
    g_clip_len = len;
}

const char *clip_get(int *out_len) {
    if (out_len) *out_len = g_clip_len;
    return g_clip;
}

void clip_clear(void) { g_clip_len = 0; }
int  clip_len(void)   { return g_clip_len; }