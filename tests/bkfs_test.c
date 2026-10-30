#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bkfs.h"
#include "blk.h"
#include "cred.h"

#define TEST_SECTORS 128
#define TEST_MAGIC 0x53464B42u
#define TEST_BLOCK_SIZE 512u
#define TEST_INODE_COUNT 256u
#define TEST_DATA_START 65u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t reserved[512 - 7 * 4];
} test_super_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    char name[59];
    uint32_t size;
    uint32_t blocks[15];
    uint8_t pad[4];
} test_inode_v2_t;

static uint8_t disk[TEST_SECTORS][TEST_BLOCK_SIZE];
static int fail_write_sector = -1;
static int fail_read_sector = -1;
static int failures;

int blk_read_sector(uint64_t sector, void *buf) {
    if (sector >= TEST_SECTORS) return 0;
    if ((int)sector == fail_read_sector) return 0;
    memcpy(buf, disk[sector], TEST_BLOCK_SIZE);
    return 1;
}

int blk_write_sector(uint64_t sector, const void *buf) {
    if (sector >= TEST_SECTORS) return 0;
    if ((int)sector == fail_write_sector) {
        fail_write_sector = -1;
        fail_read_sector = -1;
        return 0;
    }
    memcpy(disk[sector], buf, TEST_BLOCK_SIZE);
    return 1;
}

uint64_t blk_capacity(void) { return TEST_SECTORS; }

void *kmalloc(size_t size) {
    (void)size;
    return NULL;
}

void kfree(void *ptr) { (void)ptr; }

static void check(int condition, const char *name) {
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        failures++;
    }
}

static void reset_disk(void) {
    memset(disk, 0, sizeof(disk));
    fail_write_sector = -1;
}

static void create_v2_image(void) {
    test_super_t super;
    memset(&super, 0, sizeof(super));
    super.magic = TEST_MAGIC;
    super.version = 2;
    super.block_size = TEST_BLOCK_SIZE;
    super.total_blocks = TEST_SECTORS;
    super.inode_count = TEST_INODE_COUNT;
    super.inode_start = 1;
    super.data_start = TEST_DATA_START;
    memcpy(disk[0], &super, sizeof(super));

    test_inode_v2_t *inode = (test_inode_v2_t *)disk[1];
    inode[0].type = 1;
    memcpy(inode[0].name, "legacy", sizeof("legacy"));
    inode[0].size = 4;
    inode[0].blocks[0] = TEST_DATA_START;
    memcpy(disk[TEST_DATA_START], "data", 4);
}

int main(void) {
    reset_disk();
    check(bkfs_init() == 0, "blank disk identified as unformatted");
    check(bkfs_format() == BKFS_OK, "new v3 filesystem formatted");

    int inode = -1;
    check(bkfs_create_for("owned", cred_ui(), 0640) == BKFS_OK,
          "create stores owner, group, and mode");
    inode = bkfs_open("owned");
    uint32_t uid = 0, gid = 0, mode = 0;
    check(inode >= 0 &&
          bkfs_get_metadata(inode, &uid, &gid, &mode) == BKFS_OK &&
          uid == 1000 && gid == 1000 && mode == 0640,
          "metadata reads back from disk");
    struct cred invalid_id;
    cred_init_unprivileged(&invalid_id);
    invalid_id.euid = 65536;
    check(bkfs_create_for("bad-id", &invalid_id, 0644) == BKFS_EINVAL,
          "IDs outside the on-disk range are rejected");

    struct cred owner, other, group_member, root;
    cred_init_unprivileged(&owner);
    owner.supplementary_group_count = 1;
    owner.supplementary_groups[0] = 3000;
    cred_init_unprivileged(&other);
    cred_init_unprivileged(&group_member);
    cred_init_root(&root);
    other.euid = other.uid = 2000;
    other.egid = other.gid = 2000;
    group_member.euid = group_member.uid = 2000;
    group_member.egid = group_member.gid = 1000;
    group_member.supplementary_group_count = 1;
    group_member.supplementary_groups[0] = 3000;
    check(cred_may_access(&owner, 0640, 1000, 1000, CRED_R | CRED_W) &&
          !cred_may_access(&owner, 0640, 1000, 1000, CRED_X),
          "owner access uses owner mode bits");
    check(cred_may_access(&group_member, 0640, 1000, 1000, CRED_R) &&
          !cred_may_access(&group_member, 0640, 1000, 1000, CRED_W),
          "group access uses group mode bits");
    check(cred_is_group_member(&group_member, 3000) &&
          !cred_is_group_member(&other, 3000) &&
          cred_may_access(&group_member, 0060, 1000, 3000, CRED_R),
          "supplementary group membership is recognized");
    gid_t permitted_groups[] = {3000, 4000};
    gid_t duplicate_groups[] = {3000, 3000};
    check(cred_set_supplementary_groups(&root, permitted_groups, 2) &&
          cred_is_group_member(&root, 4000) &&
          !cred_set_supplementary_groups(&other, permitted_groups, 2) &&
          !cred_set_supplementary_groups(&root, duplicate_groups, 2) &&
          !cred_set_supplementary_groups(&root, permitted_groups,
                                         CRED_MAX_GROUPS + 1),
          "setgroups requires capability and validates its complete input");
    check(!cred_may_access(&other, 0640, 1000, 1000, CRED_R) &&
          cred_may_access(&root, 0000, 1000, 1000, CRED_R | CRED_W) &&
          !cred_may_access(&root, 0000, 1000, 1000, CRED_X),
          "other and capability-based root access are enforced");
    check(cred_may_access(&owner, 0750, 1000, 1000, CRED_X) &&
          !cred_may_access(&other, 0750, 1000, 1000, CRED_X),
          "execute permission follows mode bits");

    check(bkfs_chmod_for("owned", 0750, &owner) == BKFS_OK &&
          bkfs_chmod_for("owned", 0777, &other) == BKFS_EINVAL &&
          bkfs_chmod_for("owned", 010000, &owner) == BKFS_EINVAL,
          "chmod is limited to owner or CAP_FOWNER");
    check(bkfs_chown_for("owned", 2000, UINT32_MAX, &owner) == BKFS_EINVAL &&
          bkfs_chown_for("owned", UINT32_MAX, 3000, &owner) == BKFS_OK,
          "unprivileged chown only selects member groups");
    char protected_buf[4];
    check(bkfs_write_for("owned", "x", 1, &other) == BKFS_EINVAL &&
          bkfs_read_for("owned", protected_buf, sizeof(protected_buf), &other) ==
              BKFS_EINVAL,
          "protected read and write helpers enforce permissions");
    check(bkfs_chmod_for("owned", 06750, &owner) == BKFS_OK &&
          bkfs_write_at_for(inode, 0, "x", 1, &owner) == 1 &&
          bkfs_get_metadata(inode, 0, 0, &mode) == BKFS_OK &&
          (mode & 06000u) == 0 &&
          bkfs_chmod_for("owned", 06750, &root) == BKFS_OK &&
          bkfs_truncate_for(inode, &owner) == BKFS_OK &&
          bkfs_get_metadata(inode, 0, 0, &mode) == BKFS_OK &&
          (mode & 06000u) == 0,
          "unprivileged writes and truncation clear set-ID mode bits");
    check(bkfs_create_for("delete-me", &owner, 0600) == BKFS_OK &&
          bkfs_delete_for("delete-me", &other) == BKFS_EINVAL &&
          bkfs_delete_for("delete-me", &owner) == BKFS_OK,
          "delete helper enforces owner policy");
    check(bkfs_create_for("group-file", &root, 0640) == BKFS_OK &&
          bkfs_write_for("group-file", "g", 1, &root) == 1 &&
          bkfs_chown_for("group-file", UINT32_MAX, 3000, &root) == BKFS_OK &&
          bkfs_read_for("group-file", protected_buf, sizeof(protected_buf),
                        &group_member) == 1 &&
          protected_buf[0] == 'g' &&
          bkfs_write_for("group-file", "x", 1, &group_member) == BKFS_EINVAL,
          "supplementary group grants only the configured access bits");
    check(bkfs_format_for(cred_ui()) == BKFS_EINVAL,
          "unprivileged UI cannot format BKFS");
    check(bkfs_create_for("private", &root, 0600) == BKFS_OK &&
          bkfs_read("private", protected_buf, sizeof(protected_buf)) ==
              BKFS_EINVAL &&
          bkfs_delete("private") == BKFS_EINVAL,
          "legacy high-level APIs use the unprivileged UI credential");
    int private_visible = 0;
    char listed_name[64];
    for (int i = 0;
         bkfs_list(i, listed_name, sizeof(listed_name), 0);
         i++) {
        if (strcmp(listed_name, "private") == 0) private_visible = 1;
    }
    check(!private_visible, "UI listing omits files without read access");

    check(bkfs_init() == 1, "v3 metadata survives remount");
    inode = bkfs_open("owned");
    check(inode >= 0 && bkfs_size(inode) == 0,
          "inode remains readable after remount");
    uid = gid = mode = 0;
    check(inode >= 0 &&
          bkfs_get_metadata(inode, &uid, &gid, &mode) == BKFS_OK &&
          uid == 1000 && gid == 3000 && mode == 0750,
          "chmod and chown metadata persist after remount");

    reset_disk();
    create_v2_image();
    fail_write_sector = 5;
    check(bkfs_init() == -1, "interrupted migration fails closed");
    check(((test_super_t *)disk[0])->version == 0x80000003u,
          "interrupted migration leaves resumable marker");
    check(bkfs_init() == 1, "interrupted v2 migration resumes");
    inode = bkfs_open("legacy");
    uid = gid = mode = 0;
    check(inode >= 0 &&
          bkfs_get_metadata(inode, &uid, &gid, &mode) == BKFS_OK &&
          uid == 0 && gid == 0 && mode == 0644,
          "v2 files migrate as root-owned mode 0644");
    char legacy_data[4];
    check(inode >= 0 && bkfs_read_at(inode, 0, legacy_data, 4) == 4 &&
          memcmp(legacy_data, "data", 4) == 0,
          "v2 migration preserves file contents");
    check(((test_super_t *)disk[0])->version == 3,
          "migration commits v3 version after inode conversion");

    reset_disk();
    test_super_t future;
    memset(&future, 0, sizeof(future));
    future.magic = TEST_MAGIC;
    future.version = 99;
    future.block_size = TEST_BLOCK_SIZE;
    future.total_blocks = TEST_SECTORS;
    future.inode_count = TEST_INODE_COUNT;
    future.inode_start = 1;
    future.data_start = TEST_DATA_START;
    memcpy(disk[0], &future, sizeof(future));
    check(bkfs_init() == -1 &&
          ((test_super_t *)disk[0])->version == 99 &&
          !bkfs_is_ready(),
          "unsupported version is rejected without modifying disk");

    reset_disk();
    fail_read_sector = 0;
    check(bkfs_init() == -1, "disk read failure is not treated as blank media");

    reset_disk();
    disk[0][0] = 0x55;
    check(bkfs_init() == -1 && disk[0][0] == 0x55,
          "nonempty foreign disk is not treated as unformatted");

    return failures == 0 ? 0 : 1;
}
