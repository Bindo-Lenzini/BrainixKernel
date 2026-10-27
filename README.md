# BrainixKernel

A Unix-like kernel, built from scratch in ARM64 assembly and C, developed
on an Apple Silicon (M1) Mac and run under QEMU emulation.

## Setup (macOS, Apple Silicon)

Install the tools via Homebrew:

```bash
brew install qemu llvm lld
```

Clang on Apple Silicon can already cross-compile for `aarch64-elf`,
so no separate GNU cross-toolchain is required.

## Project layout

```
BrainixKernel/
├── src/
│   ├── _start.s     # ARM64 boot stub — sets up the stack, jumps to C
│   └── kernel.c      # Kernel entry point, UART "hello world"
├── linker.ld          # Places the kernel at QEMU virt's load address
├── Makefile
└── README.md
```

## Build

```bash
make
```

## Run

```bash
make run
```

You should see:

```
BrainixKernel booted successfully!
Welcome to your own OS.
```

Press `Ctrl-A` then `X` to quit QEMU.

## Clean

```bash
make clean
```

## What's next

This is the smallest possible kernel — it boots and prints a message,
nothing more. Natural next steps, roughly in order:

1. Exception level handling (drop from EL2 to EL1 cleanly)
2. Exception vector table + basic exception handlers
3. GICv2 interrupt controller setup (so timers/interrupts work)
4. A simple physical memory allocator
5. MMU setup and virtual memory

## Security model
BrainixKernel provides a minimal UNIX-like privileges model.
- **Credentials**: Each task has `struct cred` (`src/cred.h`) with `uid/euid/suid`, `gid/egid/sgid`, and Linux-style capability bitmasks.
- **Inheritance**: `fork()` inherits credentials via `cred_copy()`.
- **Syscalls**: `SYS_GETUID`, `SYS_GETEUID`, `SYS_GETGID`, `SYS_GETEGID` expose them to userland (test: `user/whoami.c`).
- **Enforcement**: `sys_open()` denies `O_CREAT`, `O_TRUNC`, or writable opens for non-root users.

### Roadmap (Security)
1. Add per-inode `mode/uid/gid` to bkfs; wire `stat/chmod/chown`.
2. Add `SYS_KILL` and enforce `CAP_KILL`.
3. W^X mappings, Stack canaries, PAN/PXN.
4. KASLR, MAC/LSM hooks, `chroot(2)`.

## Build on Linux / CI
The `Makefile` allows overriding the macOS `OBJCOPY` path:
```bash
OBJCOPY=llvm-objcopy make
CI (.github/workflows/build.yml) automates this on ubuntu-latest using a font shim.

SMP note
The kernel is currently single-core. QEMU can be started with -smp 4 once spinlocks, barriers, and IPIs are implemented.
