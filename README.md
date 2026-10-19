BrainixKernel (BKOS)
====================

[![build](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml/badge.svg)](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml)

BrainixKernel is a small hobby kernel for AArch64 (ARMv8-A), written in C and
assembly without the C standard library. This is the headless edition: there is
no graphics stack, the console is the PL011 serial port.

It targets the QEMU `virt` machine (Cortex-A72, GICv2, 2 GB RAM).

Quick Start
-----------

* Build the kernel:     make
* Run in QEMU:          make run
* Clean build output:   make clean
* Exit QEMU:            Ctrl-A, then X

Requirements
------------

* clang and ld.lld with the aarch64-elf target
* llvm-objcopy
* qemu-system-aarch64
* python3

Features
--------

* Memory management: MMU, physical page allocator, kernel heap
* Processes: scheduler, ELF loader, system calls, pipes
* Networking: virtio-net, ARP, DHCP, IPv4, ICMP, UDP, TCP, sockets, DNS client
* Drivers: PCI, virtio-blk, virtio-net
* Storage: bkfs filesystem, initrd
* Userland: ksh (kernel shell) and sh, plus ls, cat, wc, echo, test, host,
  wget, udp_send, udp_echo, tcp_connect

Repository layout
-----------------

    Makefile          build rules (kernel + user programs + initrd)
    linker.ld         kernel linker script
    src/              kernel sources
      kernel.c        entry point, boot sequence, IRQ handler
      mmu.c           page tables, copy-on-write
      physmem.c       physical page allocator
      heap.c          kernel heap
      sched.c         scheduler
      task.c          tasks and processes
      elf.c           ELF loader
      syscall.c       system call dispatch
      exception.c     synchronous exception handling
      dtb.c           device tree parser
      pci.c           PCI / ECAM
      virtio*.c       virtio transport and net driver
      blk.c           block device
      bkfs.c          filesystem
      initrd.c        initial RAM disk
      ksh.c           kernel shell buffer
      net.c, arp.c,
      dhcp.c, icmp.c,
      udp.c, tcp.c,
      socket.c        network stack
    user/             userland programs, libc, DNS client
    scripts/          build helpers (initrd packer)
    initrd/           user ELF binaries packed into the initrd
    build/            generated files (not tracked)

Who are you?
------------

### New kernel developer

Start with `src/kernel.c`: the boot sequence is in `kernel_main()`, and the
interrupt path is in `irq_handler()`.

### Networking developer

The stack is layered: `virtio_net.c` -> `net.c` -> `arp.c` / `ifnet.c` ->
IP -> `icmp.c` / `udp.c` / `tcp.c` -> `socket.c`.

### Driver author

Devices are found through the device tree (`dtb.c`) and PCI ECAM (`pci.c`).
`virtio_net.c` and `blk.c` show the current pattern.

### User program author

Copy one of `user/*.c`, add its name to `USER_PROGS` in the `Makefile`, and it
is packed into the initrd automatically.

Status
------

This version has not yet been verified on a clean build. Known gaps:

* scripts/mkinitrd.py and the assembly files user/crt0.S, user/syscall.S and
  src/uaccess.S are required but are not in the repository yet.
* CI builds on Ubuntu, so llvm-objcopy must be on PATH there.

Contributing
------------

Open an issue for bugs or questions, or send a pull request. Keep changes
small and explain what you tested and how.

License
-------

BrainixKernel is licensed under the GNU General Public License, version 2
(GPL-2.0-only). See the LICENSE file for the full text.

Contributions are accepted under the same license.
