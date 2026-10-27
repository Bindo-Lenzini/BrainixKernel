// BrainixKernel (BKOS) — cred.c
#include "cred.h"
#include "heap.h"
#include <stddef.h>

void cred_init_root(struct cred *c) {
    c->uid = c->euid = c->suid = ROOT_UID;
    c->gid = c->egid = c->sgid = ROOT_GID;
    c->cap_effective = ~0ULL;
    c->cap_permitted = ~0ULL;
}

struct cred *cred_alloc_root(void) {
    struct cred *c = (struct cred *)kmalloc(sizeof(struct cred));
    if (!c) return NULL;
    cred_init_root(c);
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

int cred_may_access(const struct cred *c, mode_t file_mode,
                    uid_t file_uid, gid_t file_gid, int want) {
    if (!c) return 0;

    // Root bypasses permission checks (unless capabilities are restricted).
    if (c->euid == 0) return 1;

    // If file uid matches euid, use owner bits.
    if (c->euid == file_uid) {
        mode_t bits = (file_mode >> 6) & 7;
        return (bits & (mode_t)want) == (mode_t)want;
    }
    // If file gid matches egid, use group bits.
    if (c->egid == file_gid) {
        mode_t bits = (file_mode >> 3) & 7;
        return (bits & (mode_t)want) == (mode_t)want;
    }
    // Otherwise use other bits.
    mode_t bits = file_mode & 7;
    return (bits & (mode_t)want) == (mode_t)want;
}
