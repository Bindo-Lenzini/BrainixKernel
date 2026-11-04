#ifndef BRAINIX_VIRTIO_BLK_INTERNAL_H
#define BRAINIX_VIRTIO_BLK_INTERNAL_H

#include <stdint.h>
#include "virtio.h"

#define VIRTIO_BLK_STATUS_OK       0
#define VIRTIO_BLK_STATUS_IOERR    1
#define VIRTIO_BLK_STATUS_UNSUPP   2
#define VIRTIO_BLK_DATA_SIZE       512
#define VIRTIO_BLK_STATUS_SIZE    1
#define VIRTIO_BLK_READ_USED_LEN  (VIRTIO_BLK_DATA_SIZE + VIRTIO_BLK_STATUS_SIZE)
#define VIRTIO_BLK_WRITE_USED_LEN VIRTIO_BLK_STATUS_SIZE

static inline int virtio_blk_completion_valid(uint16_t last_used,
                                             uint16_t used_idx,
                                             uint16_t queue_size,
                                             uint32_t id, uint32_t len,
                                             uint8_t status, int is_write) {
    uint16_t pending = (uint16_t)(used_idx - last_used);
    uint32_t expected_len = is_write ? VIRTIO_BLK_WRITE_USED_LEN
                                     : VIRTIO_BLK_READ_USED_LEN;

    return queue_size >= 3 && queue_size <= VIRTQ_SIZE &&
           pending == 1 && id == 0 &&
           status <= VIRTIO_BLK_STATUS_UNSUPP &&
           len >= VIRTIO_BLK_STATUS_SIZE && len <= expected_len &&
           (status != VIRTIO_BLK_STATUS_OK || len == expected_len);
}

#endif
