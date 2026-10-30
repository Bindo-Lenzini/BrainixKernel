// BrainixKernel — cred.c
#include "cred.h"
#include "heap.h"
#include <stddef.h>

static struct cred g_ui_cred;
static int g_ui_cred_initialized;

void cred_init_root(struct cred *c) {
    c->supplementary_group_count = 0;
    for (uint32_t i = 0; i < CRED_MAX_GROUPS; i++)
        c->supplementary_groups[i] = 0;
    c->uid = c->euid = c->suid = ROOT_UID;
    c->gid = c->egid = c->sgid = ROOT_GID;
    c->cap_effective = ~0ULL;
    c->cap_permitted = ~0ULL;
}

void cred_init_unprivileged(struct cred *c) {
    c->supplementary_group_count = 0;
    for (uint32_t i = 0; i < CRED_MAX_GROUPS; i++)
        c->supplementary_groups[i] = 0;
    c->uid = c->euid = c->suid = DEFAULT_UID;
    c->gid = c->egid = c->sgid = DEFAULT_GID;
    c->cap_effective = 0;
    c->cap_permitted = 0;
}

struct cred *cred_alloc_root(void) {
    struct cred *c = (struct cred *)kmalloc(sizeof(struct cred));
    if (!c) return NULL;
    cred_init_root(c);
    return c;
}

struct cred *cred_alloc_unprivileged(void) {
    struct cred *c = (struct cred *)kmalloc(sizeof(struct cred));
    if (!c) return NULL;
    cred_init_unprivileged(c);
    return c;
}

struct cred *cred_copy(const struct cred *src) {
    if (!src) return NULL;
    struct cred *c = (struct cred *)kmalloc(sizeof(struct cred));
    if (!c) return NULL;
    *c = *src;
    return c;
}

void cred_free(struct cred *c) {
    if (c) kfree(c);
}

int cred_has_cap(const struct cred *c, unsigned int capability) {
    if (!c || capability > CAP_LAST_CAP) return 0;
    return (c->cap_effective & CAP_TO_MASK(capability)) != 0;
}

int cred_is_group_member(const struct cred *c, gid_t gid) {
    if (!c) return 0;
    if (c->egid == gid) return 1;
    uint32_t count = c->supplementary_group_count;
    if (count > CRED_MAX_GROUPS) count = CRED_MAX_GROUPS;
    for (uint32_t i = 0; i < count; i++) {
        if (c->supplementary_groups[i] == gid) return 1;
    }
    return 0;
}

int cred_set_supplementary_groups(struct cred *c, const gid_t *groups,
                                  uint32_t count) {
    if (!c || !cred_has_cap(c, CAP_SETGID) || count > CRED_MAX_GROUPS ||
        (count != 0 && !groups)) {
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (groups[i] > CRED_MAX_ID) return 0;
        for (uint32_t j = 0; j < i; j++) {
            if (groups[i] == groups[j]) return 0;
        }
    }

    for (uint32_t i = 0; i < CRED_MAX_GROUPS; i++)
        c->supplementary_groups[i] = 0;
    for (uint32_t i = 0; i < count; i++)
        c->supplementary_groups[i] = groups[i];
    c->supplementary_group_count = count;
    return 1;
}

struct cred *cred_ui(void) {
    if (!g_ui_cred_initialized) {
        cred_init_unprivileged(&g_ui_cred);
        g_ui_cred_initialized = 1;
    }
    return &g_ui_cred;
}

int cred_may_access(const struct cred *c, mode_t file_mode,
                    uid_t file_uid, gid_t file_gid, int want) {
    if (!c) return 0;

    if (cred_has_cap(c, CAP_DAC_OVERRIDE)) {
        if ((want & CRED_X) == 0 || (file_mode & 0111u) != 0) return 1;
    }

    // If file uid matches euid, use owner bits.
    if (c->euid == file_uid) {
        mode_t bits = (file_mode >> 6) & 7;
        return (bits & (mode_t)want) == (mode_t)want;
    }
    // If file gid matches egid, use group bits.
    if (cred_is_group_member(c, file_gid)) {
        mode_t bits = (file_mode >> 3) & 7;
        return (bits & (mode_t)want) == (mode_t)want;
    }
    // Otherwise use other bits.
    mode_t bits = file_mode & 7;
    return (bits & (mode_t)want) == (mode_t)want;
}
