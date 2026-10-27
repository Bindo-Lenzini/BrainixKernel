# BrainixKernel

A Unix-like **kernel**, built from scratch in ARM64 assembly and C, developed on an Apple Silicon (M1) Mac and run under QEMU emulation.

[![build](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml/badge.svg)](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt)

## Features

- 🖥️ **GUI Subsystem**: Custom window manager, virtual framebuffer, virtio-gpu, keyboard/mouse input.
- 🌐 **Networking**: TCP/IP stack, virtio-net, ARP, DHCP, DNS, ICMP, basic socket API.
- 🗄️ **Storage & VFS**: Custom `bkfs` filesystem, initrd, block device abstraction.
- ⚙️ **Processes**: `fork`, `exec`, `wait`, pipes, file descriptors, and POSIX-like signals.
- 🔒 **Security**: UNIX-style credentials (`uid`/`gid`), Linux-style capabilities, and VFS access control.

## Setup (macOS, Apple Silicon)

Install the required tools via Homebrew:

```bash
brew install qemu llvm lld python3
pip3 install pillow
Clang on Apple Silicon can already cross-compile for aarch64-elf, so no separate GNU cross-toolchain is required.

Build & Run
To build the kernel and boot it in QEMU:

bash
make clean
make
make run
Welcome to your own kernel.

Project Layout
text
BrainixKernel/
├── src/               # Kernel source code (C and Assembly)
├── user/              # Userland programs (libc, shell, network utils)
├── scripts/           # Build scripts (font generator, initrd builder)
├── linker.ld          # Kernel linker script
└── Makefile           # Build system
Security Model
BrainixKernel provides a minimal UNIX-like privileges model, in the spirit of Linux/XNU but drastically smaller.

Credentials
Each task owns a struct cred *cred (see src/cred.h) holding:

uid/euid/suid, gid/egid/sgid — real, effective and saved IDs.

cap_effective, cap_permitted — Linux-style capability bitmask.

fork() inherits the parent's credentials via cred_copy(). On task creation, credentials default to root (uid=0, all capabilities set). Syscalls SYS_GETUID, SYS_GETEUID, SYS_GETGID, SYS_GETEGID expose them to userland (see user/whoami.c).

Capabilities & Access Control
A Linux-style capability mask is defined (CAP_CHOWN, CAP_KILL, CAP_SYS_ADMIN, etc.). Enforcement is currently partial:

sys_open() denies O_CREAT / O_TRUNC / writable opens for any non-root euid.

sys_open() allows all read-only opens.

bkfs does not yet store per-inode uid/gid/mode bits.

Roadmap
Short-term
□ VFS: Store mode, uid, gid in the bkfs inode; wire stat(2), chmod(2), chown(2).
□ Security: Add SYS_KILL and enforce CAP_KILL / same-uid rule. Add SYS_SETUID / SYS_SETGID.
□ I/O Multiplexing: Implement poll() and select() for asynchronous I/O.
□ Time: Add nanosleep, clock_gettime, and proper timer interrupts.
Medium-term
□ Memory: Implement mmap / demand paging / page cache (the biggest gap after SMP).
□ Scheduler: Move from round-robin to MLFQ or CFS-like scheduler with wait-queues.
□ Hardening: W^X for user and kernel mappings, Stack canaries, PAN / PXN (ARMv8-A).
□ Userland: Port musl libc and BusyBox.
Long-term
□ SMP: Spinlocks, memory barriers, per-CPU state, IPIs via GIC.
□ Advanced Security: KASLR, MAC/LSM hook layer (open, exec, kill), chroot(2).
□ Filesystems: journaling for bkfs, tmpfs, procfs/sysfs.
Build on Linux / CI
The Makefile defaults OBJCOPY to Homebrew's llvm-objcopy (macOS path), but it is overridable:

bash
OBJCOPY=llvm-objcopy make
CI (.github/workflows/build.yml) automates this on ubuntu-latest. It uses a font shim for scripts/mkfont.py since it looks for a macOS system font by default:

bash
sudo mkdir -p /System/Library/Fonts
sudo cp /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf /System/Library/Fonts/SFNSMono.ttf
SMP Note
The kernel is currently single-core (-smp 1 is implicit). The QEMU virt machine can be started with -smp 4 to exercise SMP once spinlocks, memory barriers, per-CPU state and IPIs land. This is a documented future work item, not wired up yet.

text

### Шаг 3. Отправь в Git
После того как выйдешь из редактора (после `Ctrl + X`), вернешься в терминал. Там выполни:

```bash
cd ~/BrainixKernel
git add README.md
git commit -m "docs: complete README rewrite — features, security, roadmap"
git push origin main
