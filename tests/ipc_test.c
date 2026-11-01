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
    passed &= check(ipc_send_endpoint(carrier, 0, transfer_payload,
                                      sizeof(transfer_payload),
                                      capability, 0, IPC_RIGHT_SEND) ==
                        (int)sizeof(transfer_payload),
                    "message can carry an IPC endpoint reference");
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
    return passed ? 0 : 1;
}
