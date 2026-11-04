#include <stdint.h>
#include <stdio.h>
#include "virtio_blk.h"

static int failures;
static int checks;

static void check(int condition, const char *description) {
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    } else {
        printf("PASS: %s\n", description);
    }
}

int main(void) {
    check(virtio_blk_completion_valid(UINT16_MAX, 0, VIRTQ_SIZE, 0,
                                      VIRTIO_BLK_READ_USED_LEN,
                                      VIRTIO_BLK_STATUS_OK, 0),
          "single block completion accepts 16-bit used-index wraparound");
    check(virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                      VIRTIO_BLK_WRITE_USED_LEN,
                                      VIRTIO_BLK_STATUS_OK, 1),
          "successful write completion reports exactly the status byte");
    check(virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                      VIRTIO_BLK_READ_USED_LEN,
                                      VIRTIO_BLK_STATUS_OK, 0),
          "successful read completion reports the data and status bytes");
    check(virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                      VIRTIO_BLK_STATUS_SIZE,
                                      VIRTIO_BLK_STATUS_IOERR, 0),
          "device I/O error completion remains structurally valid");
    check(virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                      VIRTIO_BLK_STATUS_SIZE,
                                      VIRTIO_BLK_STATUS_UNSUPP, 1),
          "unsupported-operation completion remains structurally valid");
    check(!virtio_blk_completion_valid(7, 7, VIRTQ_SIZE, 0,
                                       VIRTIO_BLK_READ_USED_LEN,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "unchanged used index is not accepted as a completion");
    check(!virtio_blk_completion_valid(7, 9, VIRTQ_SIZE, 0,
                                       VIRTIO_BLK_READ_USED_LEN,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "multiple used entries are rejected for a single outstanding request");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 1,
                                       VIRTIO_BLK_READ_USED_LEN,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "unexpected descriptor-chain ID is rejected");
    check(!virtio_blk_completion_valid(7, 8, 2, 0,
                                       VIRTIO_BLK_READ_USED_LEN,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "queue smaller than the three-descriptor chain is rejected");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                       VIRTIO_BLK_READ_USED_LEN + 1,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "device-reported read length beyond writable buffers is rejected");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                       VIRTIO_BLK_READ_USED_LEN - 1,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "short successful read is rejected");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0, 0,
                                       VIRTIO_BLK_STATUS_OK, 1),
          "completion without a written status byte is rejected");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                       VIRTIO_BLK_WRITE_USED_LEN,
                                       VIRTIO_BLK_STATUS_UNSUPP + 1, 1),
          "unknown device status is rejected");
    check(!virtio_blk_completion_valid(7, 8, VIRTQ_SIZE + 1, 0,
                                       VIRTIO_BLK_READ_USED_LEN,
                                       VIRTIO_BLK_STATUS_OK, 0),
          "queue size beyond the allocated ring is rejected");

    if (failures != 0) {
        fprintf(stderr, "%d/%d VirtIO block checks failed\n",
                failures, checks);
        return 1;
    }
    printf("VirtIO block tests passed (%d checks)\n", checks);
    return 0;
}
