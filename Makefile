BUILD_DIR  := build
SRC_DIR    := src
SCRIPTS    := scripts
INITRD_DIR := initrd
USER_DIR   := user

KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
DISK_IMG   := disk.img
NET_PCAP   := net.pcap

CC      := clang
LD      := ld.lld
OBJCOPY := llvm-objcopy
QEMU    := qemu-system-aarch64
PYTHON  := python3

TARGET  := aarch64-elf

CFLAGS := --target=$(TARGET) \
          -ffreestanding -nostdlib -O2 -Wall -Wextra \
          -mgeneral-regs-only -fno-stack-protector \
          -fno-pic -fno-pie -fno-builtin -mno-outline-atomics \
          -I$(SRC_DIR)

ASFLAGS := --target=$(TARGET)

USER_CFLAGS := --target=$(TARGET) \
               -ffreestanding -nostdlib -O2 -Wall -Wextra \
               -mgeneral-regs-only -fno-stack-protector \
               -fno-pic -fno-pie -fno-builtin -mno-outline-atomics \
               -I$(USER_DIR)

# Headless: без GPU, клавиатуры, мыши и дисплея. Консоль — serial.
QEMU_FLAGS := -M virt,gic-version=2 -cpu cortex-a72 -m 2048M \
              -drive id=disk0,file=$(DISK_IMG),format=raw,if=none,cache=writethrough \
              -device virtio-blk-pci,drive=disk0,disable-legacy=on \
              -netdev user,id=net0 \
              -object filter-dump,id=dump0,netdev=net0,file=$(NET_PCAP) \
              -device virtio-net-pci,netdev=net0,disable-legacy=on \
              -nographic

C_SOURCES := \
    $(SRC_DIR)/kernel.c \
    $(SRC_DIR)/exception.c \
    $(SRC_DIR)/mmu.c \
    $(SRC_DIR)/physmem.c \
    $(SRC_DIR)/heap.c \
    $(SRC_DIR)/dtb.c \
    $(SRC_DIR)/elf.c \
    $(SRC_DIR)/task.c \
    $(SRC_DIR)/sched.c \
    $(SRC_DIR)/syscall.c \
    $(SRC_DIR)/pipe.c \
    $(SRC_DIR)/socket.c \
    $(SRC_DIR)/protosw.c \
    $(SRC_DIR)/in_proto.c \
    $(SRC_DIR)/udp.c \
    $(SRC_DIR)/tcp.c \
    $(SRC_DIR)/virtio.c \
    $(SRC_DIR)/virtio_net.c \
    $(SRC_DIR)/pci.c \
    $(SRC_DIR)/blk.c \
    $(SRC_DIR)/bkfs.c \
    $(SRC_DIR)/initrd.c \
    $(SRC_DIR)/ksh.c \
    $(SRC_DIR)/rtc.c \
    $(SRC_DIR)/net.c \
    $(SRC_DIR)/arp.c \
    $(SRC_DIR)/dhcp.c \
    $(SRC_DIR)/sha256.c \
    $(SRC_DIR)/sha256_selftest.c \
    $(SRC_DIR)/icmp.c \
    $(SRC_DIR)/ifnet.c \
    $(SRC_DIR)/mbuf.c \
    $(SRC_DIR)/ct.c

GEN_C := \
    $(BUILD_DIR)/initrd_data.c

ASM_OBJS := \
    $(BUILD_DIR)/_start.o \
    $(BUILD_DIR)/uaccess.o

C_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
GEN_OBJS := $(patsubst $(BUILD_DIR)/%.c,$(BUILD_DIR)/%.o,$(GEN_C))

ALL_OBJS := $(ASM_OBJS) $(C_OBJS) $(GEN_OBJS)

USER_OBJS_BASE := \
    $(BUILD_DIR)/user/crt0.o \
    $(BUILD_DIR)/user/syscall.o \
    $(BUILD_DIR)/user/libc.o \
    $(BUILD_DIR)/user/dns.o

USER_PROGS := test sh echo wc ls cat udp_send udp_echo host tcp_connect wget

.PHONY: all clean run

all: $(KERNEL_BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/user:
	@mkdir -p $(BUILD_DIR)/user

$(BUILD_DIR)/user/crt0.o: $(USER_DIR)/crt0.S | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/syscall.o: $(USER_DIR)/syscall.S | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/libc.o: $(USER_DIR)/libc.c $(USER_DIR)/libc.h $(USER_DIR)/user.h | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user/dns.o: $(USER_DIR)/dns.c $(USER_DIR)/dns.h $(USER_DIR)/user.h $(USER_DIR)/libc.h | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

define build_user_prog
$(BUILD_DIR)/user/$(1).o: $(USER_DIR)/$(1).c $(USER_DIR)/user.h $(USER_DIR)/libc.h | $(BUILD_DIR)/user
	$$(CC) $$(USER_CFLAGS) -c $$< -o $$@

$(BUILD_DIR)/user/$(1).elf: $(USER_OBJS_BASE) $(BUILD_DIR)/user/$(1).o $(USER_DIR)/linker.ld
	$$(LD) -s -z max-page-size=0x1000 -T $(USER_DIR)/linker.ld -o $$@ \
	    $(USER_OBJS_BASE) $(BUILD_DIR)/user/$(1).o

$(INITRD_DIR)/$(1).elf: $(BUILD_DIR)/user/$(1).elf
	cp $$< $$@
endef

$(foreach p,$(USER_PROGS),$(eval $(call build_user_prog,$(p))))

INITRD_ELFS := $(foreach p,$(USER_PROGS),$(INITRD_DIR)/$(p).elf)

$(BUILD_DIR)/initrd_data.c: $(SCRIPTS)/mkinitrd.py $(INITRD_ELFS) $(wildcard $(INITRD_DIR)/*) | $(BUILD_DIR)
	$(PYTHON) $(SCRIPTS)/mkinitrd.py $(INITRD_DIR) $@

$(DISK_IMG):
	@echo "[disk] creating empty $@ (16 MB)"
	@dd if=/dev/zero of=$@ bs=1M count=16 2>/dev/null
	@echo "[disk] done"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(ALL_OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(ALL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

run: $(KERNEL_BIN) $(DISK_IMG) $(INITRD_ELFS)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL_BIN)

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DISK_IMG) $(NET_PCAP)
	rm -f $(INITRD_ELFS)
