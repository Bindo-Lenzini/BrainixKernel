#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "physmem.h"

#define TEST_PAGE_LIMIT 32

typedef struct {
    void *address;
    uint16_t refs;
} test_page_t;

static test_page_t pages[TEST_PAGE_LIMIT];
static uint32_t bkfs_refs[256];

int bkfs_inode_retain(int inode) {
    if (inode < 0 || inode >= 256 || bkfs_refs[inode] == UINT32_MAX)
        return -1;
    bkfs_refs[inode]++;
    return 0;
}

void bkfs_inode_release(int inode) {
    if (inode >= 0 && inode < 256 && bkfs_refs[inode] > 0)
        bkfs_refs[inode]--;
}

void *phys_alloc_page(void) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address) continue;
        void *page = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
        if (!page) return NULL;
        pages[i].address = page;
        pages[i].refs = 1;
        return page;
    }
    return NULL;
}

void phys_free_page(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address != address) continue;
        if (--pages[i].refs == 0) {
            free(pages[i].address);
            pages[i].address = NULL;
        }
        return;
    }
}

void phys_inc_ref(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++) {
        if (pages[i].address == address && pages[i].refs != UINT16_MAX) {
            pages[i].refs++;
            return;
        }
    }
}

uint16_t phys_get_ref(void *address) {
    for (int i = 0; i < TEST_PAGE_LIMIT; i++)
        if (pages[i].address == address) return pages[i].refs;
    return 0;
}

static int active_pages(void) {
    int count = 0;
    for (int i = 0; i < TEST_PAGE_LIMIT; i++)
        if (pages[i].address) count++;
    return count;
}

static int check(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        return 0;
    }
    printf("PASS: %s\n", description);
    return 1;
}

int main(void) {
    int passed = 1;
    int channel = ipc_alloc();
    if (channel < 0) {
        fprintf(stderr, "FAIL: channel allocation\n");
        return 1;
    }
    ipc_ref(channel, 0);
    ipc_ref(channel, 1);
    uint8_t *shared_page = ipc_shared_page(channel);
    if (!shared_page || phys_get_ref(shared_page) != 1) {
        fprintf(stderr, "FAIL: shared-page allocation\n");
        return 1;
    }
    int zeroed = 1;
    for (uint32_t i = 0; i < PAGE_SIZE; i++) zeroed &= shared_page[i] == 0;
    passed &= check(zeroed, "shared page is zeroed before publication");
    shared_page[0] = 0xA5;

    const char request[] = "ping";
    char received[IPC_MESSAGE_MAX];
    memset(received, 0, sizeof(received));
    passed &= check(ipc_peek(channel, 1, received, sizeof(received)) ==
                        IPC_EAGAIN,
                    "empty live endpoint reports EAGAIN");
    passed &= check(ipc_send(channel, 0, request, sizeof(request)) ==
                        (int)sizeof(request),
                    "message send preserves message length");
    passed &= check(ipc_peek(channel, 1, received, 2) == -1,
                    "undersized receive buffer rejects without consuming");
    passed &= check(ipc_peek(channel, 1, received, sizeof(received)) ==
                        (int)sizeof(request) &&
                    memcmp(received, request, sizeof(request)) == 0,
                    "message is delivered intact and remains queued");
    passed &= check(ipc_consume(channel, 1),
                    "successful receive consumes exactly one message");
    passed &= check(ipc_consume(channel, 1) == 0,
                    "empty queue cannot consume another message");

    int full = 1;
    for (int i = 0; i < IPC_QUEUE_DEPTH; i++) {
        uint8_t value = (uint8_t)i;
        full &= ipc_send(channel, 0, &value, sizeof(value)) == 1;
    }
    passed &= check(full &&
                    ipc_send(channel, 0, request, sizeof(request)) ==
                        IPC_EAGAIN,
                    "bounded queue reports EAGAIN when full");
    int fifo = 1;
    for (int i = 0; i < IPC_QUEUE_DEPTH; i++) {
        uint8_t value = UINT8_MAX;
        fifo &= ipc_peek(channel, 1, &value, sizeof(value)) == 1 &&
                value == (uint8_t)i;
        fifo &= ipc_consume(channel, 1);
    }
    passed &= check(fifo, "queued messages drain in FIFO order");

    passed &= check(ipc_send(channel, 0, request, sizeof(request)) ==
                        (int)sizeof(request),
                    "message can be queued before peer closure");
    ipc_close(channel, 0);
    passed &= check(ipc_peek(channel, 1, received, sizeof(received)) ==
                        (int)sizeof(request) &&
                    memcmp(received, request, sizeof(request)) == 0,
                    "queued data remains available after peer closure");
    passed &= check(ipc_consume(channel, 1),
                    "queued data can be consumed after peer closure");
    passed &= check(ipc_peek(channel, 1, received, sizeof(received)) == 0,
                    "peer closure produces EOF after queued messages drain");
    passed &= check(ipc_send(channel, 1, request, sizeof(request)) == -1,
                    "send to closed peer is rejected");
    phys_inc_ref(shared_page);
    passed &= check(phys_get_ref(shared_page) == 2,
                    "mapped-page reference survives endpoint closure");
    ipc_close(channel, 1);
    passed &= check(phys_get_ref(shared_page) == 1,
                    "channel releases its page while mappings retain theirs");
    phys_free_page(shared_page);

    int reused = ipc_alloc();
    passed &= check(reused == channel,
                    "channel slot is reclaimed after both endpoints close");
    uint8_t *reused_page = ipc_shared_page(reused);
    passed &= check(reused_page && reused_page[0] == 0 &&
                    phys_get_ref(reused_page) == 1,
                    "reused channel receives a fresh zeroed shared page");
    ipc_abort(reused);

    for (int i = 0; i < IPC_MAX; i++) {
        int idx = ipc_alloc();
        if (idx < 0) {
            fprintf(stderr, "FAIL: channel exhaustion fixture\n");
            return 1;
        }
    }
    passed &= check(ipc_alloc() == -1,
                    "channel pool exhaustion is reported");
    for (int i = 0; i < IPC_MAX; i++) ipc_abort(i);
    passed &= check(active_pages() == 0,
                    "IPC channel teardown releases all backing pages");

    int carrier = ipc_alloc();
    int capability = ipc_alloc();
    if (carrier < 0 || capability < 0) {
        fprintf(stderr, "FAIL: endpoint-transfer fixture allocation\n");
        return 1;
    }
    ipc_ref(carrier, 0);
    ipc_ref(carrier, 1);
    ipc_ref(capability, 0);
    ipc_ref(capability, 1);
    const char transfer_payload[] = "cap";
    uintptr_t received_file = 0;
    uint32_t received_offset = 0;
    uint8_t received_file_rights = 0;
    passed &= check(ipc_send_endpoint(carrier, 0, transfer_payload,
                                      sizeof(transfer_payload),
                                      capability, 0, IPC_RIGHT_SEND) ==
                        (int)sizeof(transfer_payload),
                    "message can carry an IPC endpoint reference");
    received_file = 0;
    received_offset = 0;
    received_file_rights = 0;
    passed &= check(ipc_peek_initrd(carrier, 1, received, sizeof(received),
                                    &received_file, &received_offset,
                                    &received_file_rights) == -1 &&
                    ipc_consume_initrd(carrier, 1, &received_file,
                                       &received_offset,
                                       &received_file_rights) == 0,
                    "file receivers cannot consume endpoint capabilities");
    passed &= check(ipc_send_endpoint(carrier, 0, transfer_payload,
                                      sizeof(transfer_payload),
                                      capability, 0, 0) == -1 &&
                    ipc_send_endpoint(carrier, 0, transfer_payload,
                                      sizeof(transfer_payload), capability, 0,
                                      IPC_RIGHT_ALL + 1) == -1,
                    "endpoint transfer rejects empty and unknown rights");
    passed &= check(ipc_send_endpoint(capability, 1, transfer_payload,
                                      sizeof(transfer_payload), carrier, 1,
                                      IPC_RIGHT_ALL) ==
                        -1,
                    "endpoint delegation rejects reference cycles");
    ipc_close(capability, 0);
    int transferred_idx = -1;
    uint8_t transferred_endpoint = 0;
    uint8_t transferred_rights = 0;
    passed &= check(ipc_peek(carrier, 1, received, sizeof(received)) == -1 &&
                    ipc_peek_endpoint(carrier, 1, received, sizeof(received),
                                      &transferred_idx,
                                      &transferred_endpoint,
                                      &transferred_rights) ==
                        (int)sizeof(transfer_payload) &&
                    transferred_idx == capability &&
                    transferred_endpoint == 0 &&
                    transferred_rights == IPC_RIGHT_SEND &&
                    memcmp(received, transfer_payload,
                           sizeof(transfer_payload)) == 0,
                    "plain receive preserves messages carrying capabilities");
    passed &= check(ipc_consume_endpoint(carrier, 1, &transferred_idx,
                                         &transferred_endpoint,
                                         &transferred_rights) &&
                    transferred_idx == capability &&
                    transferred_endpoint == 0 &&
                    transferred_rights == IPC_RIGHT_SEND &&
                    ipc_send(capability, transferred_endpoint, request,
                             sizeof(request)) == (int)sizeof(request),
                    "receiving endpoint adopts the queued reference");
    ipc_close(capability, 0);
    ipc_close(capability, 1);
    ipc_close(carrier, 0);
    ipc_close(carrier, 1);

    int cleanup_carrier = ipc_alloc();
    int cleanup_capability = ipc_alloc();
    if (cleanup_carrier < 0 || cleanup_capability < 0) {
        fprintf(stderr, "FAIL: endpoint cleanup fixture allocation\n");
        return 1;
    }
    ipc_ref(cleanup_carrier, 0);
    ipc_ref(cleanup_carrier, 1);
    ipc_ref(cleanup_capability, 0);
    ipc_ref(cleanup_capability, 1);
    passed &= check(ipc_send_endpoint(cleanup_carrier, 0, request,
                                      sizeof(request), cleanup_capability, 0,
                                      IPC_RIGHT_SEND) ==
                        (int)sizeof(request),
                    "queued endpoint remains referenced until consumed");
    ipc_close(cleanup_capability, 0);
    ipc_close(cleanup_capability, 1);
    ipc_close(cleanup_carrier, 0);
    ipc_close(cleanup_carrier, 1);
    passed &= check(active_pages() == 0,
                    "discarding a channel releases queued endpoint references");

    int vfs_carrier = ipc_alloc();
    int vfs_proxy = ipc_alloc();
    if (vfs_carrier < 0 || vfs_proxy < 0) {
        fprintf(stderr, "FAIL: VFS-capability fixture allocation\n");
        return 1;
    }
    ipc_ref(vfs_carrier, 0);
    ipc_ref(vfs_carrier, 1);
    ipc_ref(vfs_proxy, 0);
    ipc_ref(vfs_proxy, 1);
    passed &= check(ipc_register_vfs_proxy(
                        vfs_proxy, 0,
                        IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE |
                            IPC_FILE_RIGHT_DELEGATE) == 0 &&
                    ipc_vfs_proxy_rights(vfs_proxy, 1) ==
                        (IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE |
                         IPC_FILE_RIGHT_DELEGATE) &&
                    ipc_register_vfs_proxy(vfs_proxy, 0,
                                           IPC_FILE_RIGHT_READ) == -1,
                    "VFS registration grants only its peer endpoint and cannot broaden");
    passed &= check(ipc_send_vfs(vfs_carrier, 0, transfer_payload,
                                 sizeof(transfer_payload), vfs_proxy, 1,
                                 IPC_FILE_RIGHT_READ) ==
                        (int)sizeof(transfer_payload),
                    "message can carry a restricted VFS proxy");
    passed &= check(ipc_send_vfs(vfs_carrier, 0, transfer_payload,
                                 sizeof(transfer_payload), vfs_proxy, 1, 0) ==
                        -1 &&
                    ipc_send_vfs(vfs_carrier, 0, transfer_payload,
                                 sizeof(transfer_payload), vfs_proxy, 1,
                                 IPC_FILE_RIGHT_ALL + 1) == -1 &&
                    ipc_send_vfs(vfs_proxy, 0, transfer_payload,
                                 sizeof(transfer_payload), vfs_carrier, 1,
                                 IPC_FILE_RIGHT_READ) == -1,
                    "VFS delegation rejects invalid rights and transfer cycles");
    uintptr_t vfs_received_file = 0;
    uint32_t vfs_received_offset = 0;
    uint8_t vfs_received_file_rights = 0;
    int vfs_received_inode = -1;
    uint32_t vfs_received_inode_offset = 0;
    uint8_t vfs_received_inode_rights = 0;
    passed &= check(ipc_peek(vfs_carrier, 1, received, sizeof(received)) == -1 &&
                    ipc_peek_endpoint(vfs_carrier, 1, received,
                                      sizeof(received), &transferred_idx,
                                      &transferred_endpoint,
                                      &transferred_rights) == -1 &&
                    ipc_peek_initrd(vfs_carrier, 1, received,
                                    sizeof(received), &vfs_received_file,
                                    &vfs_received_offset,
                                    &vfs_received_file_rights) == -1 &&
                    ipc_peek_bkfs(vfs_carrier, 1, received,
                                  sizeof(received), &vfs_received_inode,
                                  &vfs_received_inode_offset,
                                  &vfs_received_inode_rights) == -1 &&
                    ipc_consume_endpoint(vfs_carrier, 1, &transferred_idx,
                                         &transferred_endpoint,
                                         &transferred_rights) == 0 &&
                    ipc_peek_vfs(vfs_carrier, 1, received, sizeof(received),
                                 &transferred_idx, &transferred_endpoint,
                                 &transferred_rights) ==
                        (int)sizeof(transfer_payload) &&
                    transferred_idx == vfs_proxy &&
                    transferred_endpoint == 1 &&
                    transferred_rights == IPC_FILE_RIGHT_READ,
                    "generic endpoint/file APIs cannot consume VFS proxy transfers");
    ipc_close(vfs_proxy, 0);
    ipc_close(vfs_proxy, 1);
    passed &= check(ipc_consume_vfs(vfs_carrier, 1, &transferred_idx,
                                    &transferred_endpoint,
                                    &transferred_rights) &&
                    transferred_idx == vfs_proxy &&
                    transferred_endpoint == 1 &&
                    transferred_rights == IPC_FILE_RIGHT_READ &&
                    ipc_vfs_proxy_rights(transferred_idx,
                                         transferred_endpoint) ==
                        (IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE |
                         IPC_FILE_RIGHT_DELEGATE),
                    "consuming VFS capability transfers queued endpoint reference");
    ipc_close(transferred_idx, transferred_endpoint);
    ipc_close(vfs_carrier, 0);
    ipc_close(vfs_carrier, 1);
    passed &= check(active_pages() == 0,
                    "VFS capability endpoint is released after final close");

    int vfs_cleanup_carrier = ipc_alloc();
    int vfs_cleanup_proxy = ipc_alloc();
    if (vfs_cleanup_carrier < 0 || vfs_cleanup_proxy < 0) {
        fprintf(stderr, "FAIL: VFS cleanup fixture allocation\n");
        return 1;
    }
    ipc_ref(vfs_cleanup_carrier, 0);
    ipc_ref(vfs_cleanup_carrier, 1);
    ipc_ref(vfs_cleanup_proxy, 0);
    ipc_ref(vfs_cleanup_proxy, 1);
    passed &= check(ipc_register_vfs_proxy(vfs_cleanup_proxy, 0,
                                           IPC_FILE_RIGHT_READ) == 0 &&
                    ipc_send_vfs(vfs_cleanup_carrier, 0, request,
                                 sizeof(request), vfs_cleanup_proxy, 1,
                                 IPC_FILE_RIGHT_READ) ==
                        (int)sizeof(request),
                    "queued VFS proxy retains its channel endpoint");
    ipc_close(vfs_cleanup_proxy, 0);
    ipc_close(vfs_cleanup_proxy, 1);
    ipc_close(vfs_cleanup_carrier, 0);
    ipc_close(vfs_cleanup_carrier, 1);
    passed &= check(active_pages() == 0,
                    "discarding queued VFS proxy releases endpoint and page");

    int vfs_failed_service = ipc_alloc();
    if (vfs_failed_service < 0) {
        fprintf(stderr, "FAIL: VFS service-failure fixture allocation\n");
        return 1;
    }
    ipc_ref(vfs_failed_service, 0);
    ipc_ref(vfs_failed_service, 1);
    passed &= check(ipc_register_vfs_proxy(vfs_failed_service, 0,
                                           IPC_FILE_RIGHT_READ) == 0,
                    "VFS service endpoint can register a client capability");
    ipc_close(vfs_failed_service, 0);
    passed &= check(ipc_vfs_proxy_rights(vfs_failed_service, 1) ==
                        IPC_FILE_RIGHT_READ &&
                    ipc_send(vfs_failed_service, 1, request,
                             sizeof(request)) == -1 &&
                    ipc_peek(vfs_failed_service, 1, received,
                             sizeof(received)) == 0,
                    "closed VFS service fails closed for existing client endpoint");
    ipc_close(vfs_failed_service, 1);
    passed &= check(active_pages() == 0,
                    "closed VFS service channel releases after client closes");

    int file_carrier = ipc_alloc();
    if (file_carrier < 0) {
        fprintf(stderr, "FAIL: file-capability fixture allocation\n");
        return 1;
    }
    ipc_ref(file_carrier, 0);
    ipc_ref(file_carrier, 1);
    const uintptr_t file_token = 0x1000;
    const uint32_t file_offset = 7;
    passed &= check(ipc_send_initrd(file_carrier, 0, transfer_payload,
                                    sizeof(transfer_payload), file_token,
                                    file_offset,
                                    IPC_FILE_RIGHT_READ) ==
                        (int)sizeof(transfer_payload),
                    "message can carry a restricted initrd file capability");
    passed &= check(ipc_send_initrd(file_carrier, 0, transfer_payload,
                                    sizeof(transfer_payload), file_token, 0,
                                    0) == -1 &&
                    ipc_send_initrd(file_carrier, 0, transfer_payload,
                                    sizeof(transfer_payload), file_token, 0,
                                    IPC_FILE_RIGHT_ALL + 1) == -1,
                    "initrd transfer rejects empty and unknown rights");
    transferred_idx = -1;
    transferred_endpoint = 0;
    transferred_rights = 0;
    passed &= check(ipc_peek(file_carrier, 1, received, sizeof(received)) ==
                        -1 &&
                    ipc_peek_endpoint(file_carrier, 1, received,
                                      sizeof(received), &transferred_idx,
                                      &transferred_endpoint,
                                      &transferred_rights) == -1 &&
                    ipc_consume_endpoint(file_carrier, 1, &transferred_idx,
                                         &transferred_endpoint,
                                         &transferred_rights) == 0,
                    "endpoint receivers cannot consume file capabilities");
    passed &= check(ipc_peek_initrd(file_carrier, 1, received,
                                    sizeof(received), &received_file,
                                    &received_offset,
                                    &received_file_rights) ==
                        (int)sizeof(transfer_payload) &&
                    received_file == file_token &&
                    received_offset == file_offset &&
                    received_file_rights == IPC_FILE_RIGHT_READ &&
                    memcmp(received, transfer_payload,
                           sizeof(transfer_payload)) == 0 &&
                    ipc_consume_initrd(file_carrier, 1, &received_file,
                                       &received_offset,
                                       &received_file_rights) &&
                    received_file == file_token &&
                    received_offset == file_offset &&
                    received_file_rights == IPC_FILE_RIGHT_READ,
                    "file capability metadata survives peek and ownership transfer");
    int inode_token = 9;
    int received_inode = -1;
    uint32_t received_inode_offset = 0;
    uint8_t received_inode_rights = 0;
    passed &= check(ipc_send_bkfs(file_carrier, 0, transfer_payload,
                                  sizeof(transfer_payload), inode_token, 11,
                                  IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE) ==
                        (int)sizeof(transfer_payload) &&
                    bkfs_refs[inode_token] == 1,
                    "queued BKFS capability pins its inode");
    passed &= check(ipc_peek_initrd(file_carrier, 1, received,
                                    sizeof(received), &received_file,
                                    &received_offset,
                                    &received_file_rights) == -1 &&
                    ipc_peek(file_carrier, 1, received, sizeof(received)) ==
                        -1 &&
                    ipc_peek_endpoint(file_carrier, 1, received,
                                      sizeof(received), &transferred_idx,
                                      &transferred_endpoint,
                                      &transferred_rights) == -1 &&
                    ipc_consume_endpoint(file_carrier, 1, &transferred_idx,
                                         &transferred_endpoint,
                                         &transferred_rights) == 0 &&
                    ipc_peek_bkfs(file_carrier, 1, received,
                                  sizeof(received), &received_inode,
                                  &received_inode_offset,
                                  &received_inode_rights) ==
                        (int)sizeof(transfer_payload) &&
                    received_inode == inode_token &&
                    received_inode_offset == 11 &&
                    received_inode_rights ==
                        (IPC_FILE_RIGHT_READ | IPC_FILE_RIGHT_WRITE),
                    "BKFS capability is type-isolated and preserves metadata");
    passed &= check(ipc_consume_bkfs(file_carrier, 1, &received_inode,
                                     &received_inode_offset,
                                     &received_inode_rights) &&
                    received_inode == inode_token &&
                    bkfs_refs[inode_token] == 0,
                    "consuming BKFS capability releases its queue pin");
    ipc_close(file_carrier, 0);
    ipc_close(file_carrier, 1);
    passed &= check(active_pages() == 0,
                    "file-capability channel teardown releases backing page");
    int bkfs_cleanup_carrier = ipc_alloc();
    if (bkfs_cleanup_carrier < 0) {
        fprintf(stderr, "FAIL: BKFS cleanup fixture allocation\n");
        return 1;
    }
    ipc_ref(bkfs_cleanup_carrier, 0);
    ipc_ref(bkfs_cleanup_carrier, 1);
    passed &= check(ipc_send_bkfs(bkfs_cleanup_carrier, 0, transfer_payload,
                                  sizeof(transfer_payload), inode_token, 0,
                                  IPC_FILE_RIGHT_READ) ==
                        (int)sizeof(transfer_payload) &&
                    bkfs_refs[inode_token] == 1,
                    "queued BKFS capability holds a pin until channel cleanup");
    ipc_close(bkfs_cleanup_carrier, 0);
    ipc_close(bkfs_cleanup_carrier, 1);
    passed &= check(bkfs_refs[inode_token] == 0 && active_pages() == 0,
                    "discarding BKFS message releases inode and channel references");
    return passed ? 0 : 1;
}
