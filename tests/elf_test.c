#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "elf.h"
#include "mmu.h"

typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} test_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} test_phdr_t;

#define TEST_FILE_SIZE 0x1004
#define TEST_PHDR_OFFSET sizeof(test_ehdr_t)
#define TEST_FLAGS_RX (ELF_PF_R | ELF_PF_X)

static uint8_t image[TEST_FILE_SIZE];
static int failures;
static int checks;

static test_ehdr_t *header(void) {
    return (test_ehdr_t *)image;
}

static test_phdr_t *program_header(unsigned index) {
    return (test_phdr_t *)(image + TEST_PHDR_OFFSET +
                           index * sizeof(test_phdr_t));
}

static void make_valid_image(void) {
    memset(image, 0, sizeof(image));
    test_ehdr_t *eh = header();
    eh->ident[0] = 0x7F;
    eh->ident[1] = 'E';
    eh->ident[2] = 'L';
    eh->ident[3] = 'F';
    eh->ident[4] = 2;
    eh->ident[5] = 1;
    eh->ident[6] = 1;
    eh->type = 2;
    eh->machine = 183;
    eh->version = 1;
    eh->entry = USER_CODE_BASE;
    eh->phoff = TEST_PHDR_OFFSET;
    eh->ehsize = sizeof(test_ehdr_t);
    eh->phentsize = sizeof(test_phdr_t);
    eh->phnum = 1;

    test_phdr_t *ph = program_header(0);
    ph->type = 1;
    ph->flags = TEST_FLAGS_RX;
    ph->offset = 0x1000;
    ph->vaddr = USER_CODE_BASE;
    ph->filesz = 4;
    ph->memsz = 0x1000;
    ph->align = 0x1000;
}

static void expect_parse(const char *name, int expected) {
    elf_info_t info;
    int result = elf_parse(image, sizeof(image), &info);
    checks++;
    if (result != expected) {
        fprintf(stderr, "FAIL: %s: expected parse=%d, got %d (%s)\n",
                name, expected, result, elf_last_error());
        failures++;
    }
}

int main(void) {
    make_valid_image();
    expect_parse("valid RX image", 1);

    make_valid_image();
    program_header(0)->flags = ELF_PF_R | ELF_PF_W | ELF_PF_X;
    expect_parse("W+X segment", 0);

    make_valid_image();
    program_header(0)->offset = UINT64_MAX - 1;
    program_header(0)->filesz = 8;
    expect_parse("file range overflow", 0);

    make_valid_image();
    program_header(0)->vaddr = USER_VA_LIMIT - 0x1000;
    program_header(0)->memsz = 0x2000;
    expect_parse("user address range overflow", 0);

    make_valid_image();
    program_header(0)->flags = ELF_PF_R;
    expect_parse("entry outside executable segment", 0);

    make_valid_image();
    program_header(0)->vaddr = USER_HEAP_BASE;
    expect_parse("heap overlap", 0);

    make_valid_image();
    header()->phoff = UINT64_MAX;
    expect_parse("program header table overflow", 0);

    make_valid_image();
    header()->phnum = 2;
    test_phdr_t *second = program_header(1);
    *second = *program_header(0);
    second->flags = ELF_PF_R;
    expect_parse("overlapping load pages", 0);

    if (failures != 0) {
        fprintf(stderr, "%d/%d ELF tests failed\n", failures, checks);
        return 1;
    }
    printf("ELF parser tests passed (%d checks)\n", checks);
    return 0;
}
