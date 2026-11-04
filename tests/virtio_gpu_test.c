#include <stdint.h>
#include <stdio.h>
#include "gpu.h"

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
    uint32_t fb_pages = 0;
    uint32_t entries_pages = 0;

    check(gpu_framebuffer_layout(640, 480, &fb_pages, &entries_pages) &&
          fb_pages == 300 && entries_pages == 2,
          "framebuffer layout computes bounded 640x480 backing");
    check(!gpu_framebuffer_layout(0, 480, &fb_pages, &entries_pages),
          "zero framebuffer width is rejected");
    check(!gpu_framebuffer_layout(640, 0, &fb_pages, &entries_pages),
          "zero framebuffer height is rejected");
    check(!gpu_framebuffer_layout(UINT32_MAX, UINT32_MAX, &fb_pages,
                                  &entries_pages),
          "overflowing framebuffer dimensions are rejected");
    check(!gpu_framebuffer_layout(1, 1, 0, &entries_pages),
          "missing framebuffer output pointer is rejected");

    check(virtio_gpu_completion_valid(UINT16_MAX, 0, VIRTQ_SIZE, 0,
                                      VIRTIO_GPU_CTRL_HDR_SIZE,
                                      VIRTIO_GPU_CTRL_HDR_SIZE),
          "GPU command completion accepts used-index wraparound");
    check(!virtio_gpu_completion_valid(7, 7, VIRTQ_SIZE, 0,
                                       VIRTIO_GPU_CTRL_HDR_SIZE,
                                       VIRTIO_GPU_CTRL_HDR_SIZE),
          "GPU completion with unchanged used index is rejected");
    check(!virtio_gpu_completion_valid(7, 9, VIRTQ_SIZE, 0,
                                       VIRTIO_GPU_CTRL_HDR_SIZE,
                                       VIRTIO_GPU_CTRL_HDR_SIZE),
          "GPU queue advancing multiple entries for one command is rejected");
    check(!virtio_gpu_completion_valid(7, 8, VIRTQ_SIZE, 1,
                                       VIRTIO_GPU_CTRL_HDR_SIZE,
                                       VIRTIO_GPU_CTRL_HDR_SIZE),
          "unexpected GPU descriptor-chain ID is rejected");
    check(!virtio_gpu_completion_valid(7, 8, 2, 0,
                                       VIRTIO_GPU_CTRL_HDR_SIZE,
                                       VIRTIO_GPU_CTRL_HDR_SIZE),
          "GPU queue too small for a three-descriptor chain is rejected");
    check(!virtio_gpu_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                       VIRTIO_GPU_CTRL_HDR_SIZE + 1,
                                       VIRTIO_GPU_CTRL_HDR_SIZE + 1),
          "oversized GPU response is rejected");
    check(!virtio_gpu_completion_valid(7, 8, VIRTQ_SIZE, 0,
                                       VIRTIO_GPU_CTRL_HDR_SIZE - 1,
                                       VIRTIO_GPU_CTRL_HDR_SIZE),
          "short GPU response header is rejected");

    if (failures != 0) {
        fprintf(stderr, "%d/%d VirtIO GPU checks failed\n",
                failures, checks);
        return 1;
    }
    printf("VirtIO GPU tests passed (%d checks)\n", checks);
    return 0;
}
