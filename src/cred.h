// BrainixKernel — cred.h
// UNIX-style credentials and capabilities.
#ifndef BRAINIX_CRED_H
#define BRAINIX_CRED_H

#include <stdint.h>

typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;

/* Linux-style capability bits (subset) */
#define CAP_CHOWN            0
#define CAP_DAC_OVERRIDE     1
#define CAP_DAC_READ_SEARCH  2
#define CAP_FOWNER           3
#define CAP_FSETID           4
#define CAP_KILL             5
#define CAP_SETGID           6
#define CAP_SETUID           7
#define CAP_NET_RAW          13
#define CAP_SYS_PTRACE       19
#define CAP_SYS_ADMIN        21
#define CAP_SYS_BOOT         22
#define CAP_LAST_CAP         22

#define CAP_TO_MASK(cap) (1ULL << (cap))
#define CRED_MAX_GROUPS 8
#define CRED_MAX_ID 65535u

struct cred {
    uid_t    uid, euid, suid;
    gid_t    gid, egid, sgid;
    uint32_t supplementary_group_count;
    gid_t    supplementary_groups[CRED_MAX_GROUPS];
    uint64_t cap_effective;
    uint64_t cap_permitted;
};

#define ROOT_UID 0
#define ROOT_GID 0
#define DEFAULT_UID 1000
#define DEFAULT_GID 1000

/* Access types for cred_may_access() */
#define CRED_R 4
#define CRED_W 2
#define CRED_X 1

void cred_init_root(struct cred *c);
struct cred *cred_alloc_root(void);
void cred_init_unprivileged(struct cred *c);
struct cred *cred_alloc_unprivileged(void);
struct cred *cred_copy(const struct cred *src);
void cred_free(struct cred *c);
int cred_has_cap(const struct cred *c, unsigned int capability);
int cred_is_group_member(const struct cred *c, gid_t gid);
int cred_set_supplementary_groups(struct cred *c, const gid_t *groups,
                                  uint32_t count);
struct cred *cred_ui(void);

/*
 * Check if a process with cred `c` may access a file with given
 * mode, uid, gid for the requested access (CRED_R/W/X bitmask).
 * Returns 1 if allowed, 0 otherwise.
 */
int cred_may_access(const struct cred *c, mode_t file_mode,
                    uid_t file_uid, gid_t file_gid, int want);

#endif
