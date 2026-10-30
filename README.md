# BrainixKernel

An experimental AArch64 kernel written in C and ARM64 assembly, developed on
Apple Silicon and tested in QEMU.

[![Build](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml/badge.svg)](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](./LICENSE)

> [!WARNING]
> **BrainixKernel is unfinished experimental software. It is not a production
> operating system and does not guarantee security, reliability, or data
> integrity.** Security mechanisms and tests are incomplete. Do not rely on
> this kernel to protect sensitive data, and do not expose it to untrusted
> users or networks. Use it for development and experimentation at your own
> risk.

The project is an ongoing effort to build a small, understandable kernel from
scratch and explore modern operating-system design. This page describes the
current state of development; implemented code does not necessarily mean a
feature is complete or fully validated.

## Contents

- [Project overview](#project-overview)
- [Current features](#current-features)
- [Latest development update](#latest-development-update)
- [Build and run](#build-and-run)
- [Testing and CI](#testing-and-ci)
- [Security status and limitations](#security-status-and-limitations)
- [Roadmap](#roadmap)
- [Project layout](#project-layout)
- [License](#license)

## Project overview

BrainixKernel targets the AArch64 architecture and QEMU's `virt` machine. It
uses a freestanding toolchain and a custom linker script. The repository
contains the kernel, a small userland, build helpers, and code for virtual
devices, storage, networking, and a graphical desktop.

| Area | Current scope |
| --- | --- |
| Architecture | AArch64 / ARM64, `aarch64-elf`, QEMU `virt`, Cortex-A72, GICv2 |
| Kernel | C and ARM64 assembly, exception handling, memory management, system calls |
| Processes | User tasks, round-robin scheduling, `fork`, `exec`, `wait`, and signals |
| Devices and graphics | Virtio devices, framebuffer, keyboard, tablet, and window manager |
| Storage | Initrd, block-device support, and the custom BKFS filesystem |
| Networking | Experimental virtio networking and ARP, DHCP, IP, ICMP, UDP, TCP, and socket code |
| Userland | Freestanding runtime, shell, and utility programs |

Subsystems are evolving. The presence of code does not imply full standards
compliance, complete functionality, or comprehensive testing.

## Current features

- Per-task user address spaces and copy-on-write (`COW`) fork.
- ELF loading with segment bounds, permission, and entry-point validation.
- User-memory copy helpers with user-address range and overflow checks.
- User tasks default to UID/GID `1000` and empty effective/permitted capability
  masks; forked tasks inherit credentials.
- A small userland including a shell, basic utilities, and network examples.
- Experimental virtual-device, storage, network, and graphical desktop support.

## Latest development update

### 2026-10-30 — memory safety checks and CI

- **User-memory access:** added user virtual-address range and overflow checks;
  fault recovery is restricted to the active user-copy range.
- **ELF and W^X:** strengthened ELF header and segment validation, rejected
  writable-executable segments and invalid layouts, and applied read-only,
  writable/non-executable, or executable/read-only user mappings.
- **COW fork:** made cloning transactional so parent mappings are changed only
  after a successful clone; partial child mappings and acquired page references
  are released if cloning fails.
- **`exec` cleanup:** failed image/stack setup now releases the temporary
  address space and ASID without replacing the running program. Successful
  `exec` switches to the new address space before reclaiming the old one.
- **Pipe failure handling:** roll back pipe and file-descriptor resources when
  returning descriptors to an invalid user pointer fails.
- **File descriptor access modes:** reject invalid `open` flag combinations,
  deny writable opens of the read-only initrd, and enforce read/write mode on
  descriptors.
- **BKFS permissions:** BKFS v3 stores a 16-bit UID, 16-bit GID, and mode in
  each inode. `open`, `read`, `write`, truncation, `exec`, `chmod`, `chown`,
  listing, and deletion apply credential checks. Supplementary groups are
  inherited across `fork`, limited to 8, and can only be changed by a process
  with `CAP_SETGID`. Existing v2 files migrate as root-owned mode `0644`;
  migration is resumable and incompatible disks are not autoformatted.
- **Kernel UI filesystem access:** the kernel shell, editor, and file manager
  use an explicit unprivileged UID/GID 1000 identity rather than inheriting the
  interrupted task's credentials. The UI cannot format BKFS or bypass per-file
  access checks through its built-in file operations.
- **Regression tests:** host tests cover BKFS metadata, owner/group/other and
  supplementary-group decisions, `chmod`/`chown`, protected file operations,
  v2 migration, and interrupted migration. QEMU guest tests verify unprivileged
  `chmod`/`chown` behavior, rejection of execution without an execute bit,
  supplementary-group privilege checks, `O_RDONLY | O_TRUNC` rejection
  without modifying a file, writable initrd denial, and invalid open flags,
  alongside malformed-ELF cases, invalid user pointers, pipe rollback, COW
  isolation, repeated `exec` and fork/wait cycles, and reaping a faulted child.
- **Continuous integration:** CI runs the host ELF tests, builds the kernel,
  and boots a headless QEMU smoke test with 512 MiB of guest RAM. The regular
  graphical QEMU configuration remains at 2 GiB.

The [latest CI run](https://github.com/Bindo-Lenzini/BrainixKernel/actions)
passed these configured checks. Passing them only confirms the tested
scenarios; it is not a complete security audit.

## Build and run

### Requirements

The documented development environment is macOS on Apple Silicon. Linux can
also be used for cross-compilation and headless tests.

Install the required tools on macOS:

```sh
brew install qemu llvm lld python
python3 -m pip install pillow
```

The build uses Clang, LLD, `llvm-objcopy`, Python 3, Pillow, and Make.
`qemu-system-aarch64` is required to run the virtual machine.

### Build

```sh
git clone https://github.com/Bindo-Lenzini/BrainixKernel.git
cd BrainixKernel
make
```

The build produces:

- `build/kernel.elf` — linked kernel ELF image.
- `build/kernel.bin` — raw kernel image for QEMU.

If `llvm-objcopy` is not at the Makefile's default Homebrew path, specify it:

```sh
make OBJCOPY=llvm-objcopy
```

### Run the graphical VM

```sh
make run
```

The graphical QEMU configuration uses the macOS Cocoa display backend and
2 GiB of guest RAM. Its default display settings may need adjustment on Linux.

### Run headless tests

Install `qemu-system-aarch64` and the build dependencies, then run:

```sh
make host-test HOSTCC=cc
make smoke OBJCOPY=llvm-objcopy QEMU=qemu-system-aarch64
```

The smoke target uses a headless QEMU guest with 512 MiB of RAM and a disk
snapshot; guest disk writes are not persisted to `disk.img`.

`make clean` removes `build/`, `media.img`, and `net.pcap`. It keeps
`disk.img` and ELF files in `initrd/`, which may be tracked build inputs.

## Testing and CI

| Check | What it covers |
| --- | --- |
| `make host-test` | ELF parser cases, BKFS permissions and v2 migration, scheduler reaper adoption/fallback, and COW allocation-failure rollback |
| `make smoke` | QEMU boot, kernel self-tests, credentials, BKFS user-file creation, `chmod`/`chown`, execute-bit denial, supplementary-group pointer/range/privilege checks, initrd write denial, invalid open flags, user-pointer handling, pipe rollback, COW isolation, 260 repeated `exec` and fork/wait cycles, and a faulted-child teardown check |
| GitHub Actions | Host tests, kernel build, and the headless QEMU smoke target |

Tests cover selected regression scenarios. They do not prove the absence of
bugs or vulnerabilities and do not replace broader testing or independent
review.

## Security status and limitations

Security is an active area of development, not a finished property of the
kernel. Current work includes targeted checks around user pointers, ELF
permissions, COW cloning, default task credentials, and BKFS read/write
permissions. Important limitations remain:

- BKFS stores UID/GID values up to 65535 and supports up to 8 supplementary
  groups. Directories, `umask`, ACLs, and POSIX errno reporting are not
  implemented. Initrd remains read-only, has no per-file metadata, and treats
  `.elf` entries as executable based on their name.
- The kernel UI has a fixed unprivileged identity rather than login/session
  credentials. Low-level BKFS sector/inode APIs remain trusted kernel
  interfaces; UI code must use the credential-checked high-level operations.
- Credential and capability enforcement across syscalls remains partial.
- Address spaces, kernel stacks, credentials, and inherited descriptors are
  reclaimed when a parent reaps a dead child. Orphaned tasks are reparented to
  the registered user-shell reaper; if no live reaper exists, dead parentless
  tasks are reclaimed at a later syscall-triggered scheduling point.
- ASIDs are recycled after `exec` and wait/reap; more lifecycle and exhaustion
  testing is still needed.
- Writable aliases and kernel mappings need broader W^X review.
- Syscall, filesystem, network, and device input paths need more systematic
  negative testing and fuzzing.
- The kernel is single-core; synchronization for SMP is not implemented.

Do not treat passing tests or this feature list as a security guarantee.

## Roadmap

Near-term development priorities:

1. Expand teardown and COW allocation-failure tests.
2. Audit syscall boundaries, authorization checks, and partial-I/O behavior.
3. Add malformed-input, allocation-failure, and fuzz tests for BKFS, networking,
   and device interfaces.
4. Extend memory-protection review to kernel mappings and physical aliases.
5. Improve CI coverage, then pursue scheduler, synchronization, and userland
   improvements.

Longer-term goals include `mmap`, demand paging, a page cache, SMP support,
stronger filesystem permissions, and additional kernel-hardening measures.
BrainixKernel is not currently a drop-in replacement for XNU, Linux, or another
production operating system.

## Project layout

```text
.github/workflows/  GitHub Actions workflow
initrd/             User programs packaged into the initial ramdisk
media/              Files used to generate the media image
scripts/            Build helpers and QEMU smoke-test scripts
src/                Kernel C and ARM64 assembly sources
tests/              Host-side regression tests
user/               User programs, runtime, and linker script
linker.ld           Kernel linker script
Makefile            Build, run, test, and clean targets
```

## License

BrainixKernel is distributed under the GNU General Public License, version 2.
See [LICENSE](./LICENSE).
