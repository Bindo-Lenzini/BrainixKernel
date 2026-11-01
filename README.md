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
- [Update log](#update-log)
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
- Bounded message channels with paired file-descriptor endpoints, nonblocking
  calls, and blocking send/receive variants; endpoints are inherited on `fork`
  and messages are copied through validated user-memory helpers. Processes can
  explicitly delegate an IPC endpoint with a message, creating a recipient fd.
  An endpoint can also map its zeroed shared page at a caller-selected
  page-aligned address; shared writable mappings remain shared across COW `fork`.
- User tasks default to UID/GID `1000` and empty effective/permitted capability
  masks; forked tasks inherit credentials.
- A small userland including a shell, basic utilities, network examples, and a
  user-space console-routing service connected to the shell over IPC.
- Experimental virtual-device, storage, network, and graphical desktop support.

The console service routes terminal input to the shell and shell output to the
UART through IPC. PL011 register access, interrupt handling, and the UART
receive buffer remain in the kernel; this is not yet a user-space UART driver.

### IPC API

The user runtime exposes `ipc_pair(int fds[2])` and
`ipc_pair_rights(int fds[2], first_rights, second_rights)`. The original
`ipc_pair` grants all rights to both endpoints; `ipc_pair_rights` creates
endpoints with explicit, non-empty subsets of the supported rights and rejects
unknown bits. Services can use it to create least-authority client/server
channels from the start. The runtime also exposes `ipc_send(fd, message, len)`,
`ipc_recv(fd, message, capacity)`, their blocking variants
`ipc_send_wait`/`ipc_recv_wait`, and `ipc_map(fd, address)`. Each pair creates
two bidirectional endpoint descriptors. Message queues hold up to 8 messages
per receiving endpoint, with a 256-byte maximum per message. The regular send
and receive calls are nonblocking: `IPC_EAGAIN` (`-2`) means the queue is full
or empty; blocking variants sleep and retry when queue state changes. Receive
returns `0` for EOF after the peer closes, and invalid arguments return `-1`.
`ipc_send_endpoint(fd, message, len, transfer_fd, rights)` explicitly queues a
reference to an IPC endpoint along with the message; the sender keeps its fd.
The transferred endpoint must belong to a different channel than the transport,
and queued delegation cycles are rejected. IPC endpoints carry independent
`IPC_RIGHT_SEND`, `IPC_RIGHT_RECEIVE`, `IPC_RIGHT_MAP`, and
`IPC_RIGHT_DELEGATE` rights. New pairs begin with all rights. Sending an
endpoint requires delegation authority on both the transport and transferred
endpoint, and the requested rights must be a non-empty subset of the
transferred endpoint's rights; delegation cannot increase authority.
`ipc_recv_endpoint(fd, message, capacity, &received_fd, &received_rights)`
installs the delegated endpoint as a new fd and reports its rights, or writes
`-1` and zero when the message carries no endpoint. Send, receive, and map
operations are rejected when the corresponding right is absent. Fork and
`dup2` preserve, but do not expand, endpoint rights.
Ordinary receive refuses an endpoint-bearing message without consuming it.
Queued endpoint references are released if their message is discarded with its
channel.
`ipc_map` maps the channel's zero-initialized 4 KiB shared page at a free,
page-aligned user address. The mapping remains writable and shared after
`fork`; closing endpoints does not unmap existing mappings.

## UPDATE LOG

### 2026-11-01 — IPC, shared pages, and user-space console services

- Added paired bidirectional IPC endpoints with bounded FIFO message queues,
  explicit `EAGAIN` when a queue is full or empty, and EOF after peer closure.
- Added register-ABI system calls for creating endpoint pairs, sending messages,
  receiving messages, and mapping an endpoint's zeroed 4 KiB shared page.
  Endpoint descriptors retain/release channel references across `fork`,
  `dup2`, `close`, and task teardown.
- Added endpoint rights for send, receive, shared-page mapping, and delegation.
  Endpoint transfers carry an explicitly selected subset of rights, and the
  kernel rejects unauthorized operations and rights escalation. Added
  `ipc_pair_rights` so services can create endpoints with explicit rights
  policies instead of starting from full-authority descriptors.
- Shared mappings carry an explicit software PTE bit and stay writable across
  COW fork. Mapping and address-space teardown update physical-page references;
  closing endpoints releases the channel's owner reference without invalidating
  existing mappings.
- User-memory faults do not consume queued messages; host and guest tests cover
  queue bounds, FIFO order, forked endpoint exchange, shared aliases, and failed
  user copies.
- COW faults during validated kernel-to-user copies are resolved for the active
  task, allowing IPC receives into forked user buffers.
- Added `console.elf` input/output user tasks with a kernel-created IPC endpoint
  pair. UART input is forwarded to the shell and shell output is relayed to the
  UART; the shell continues to use its ordinary stdin/stdout descriptors.
- Added blocking IPC send/receive calls. Blocked syscalls retry after IPC state
  changes; queue sends wake receivers, queue consumption wakes senders, and
  closing endpoints wakes waiters. Terminal reads/writes and the console
  services use these wakeups instead of polling.
- Split console input and output into separate user tasks so the input service
  can wait for UART interrupts while the output service blocks on IPC.
- Added explicit IPC endpoint delegation with messages. The sender retains its
  descriptor, the receiver adopts a new fd, and ordinary receive preserves a
  queued capability rather than silently discarding it.
- Kept UART MMIO, interrupt handling, and receive buffering in the kernel HAL.
  The console service owns routing, but does not directly drive UART hardware.
- The headless QEMU smoke test now requires the console-server ready marker and
  verifies the shell's test command through the routed input/output path.
- Host regression tests, the kernel build, and the headless QEMU smoke test pass.
- Broader capability authorization and moving the physical UART driver to
  user-space remain future work.

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
| `make host-test` | ELF parser cases, BKFS permissions, malformed block-pointer rejection, blank-disk detection and v2 migration, scheduler reaper adoption/fallback and dead-orphan teardown, IPC queue, endpoint delegation/cycle prevention and shared-page lifetime semantics, and COW allocation-failure rollback/shared-mapping behavior |
| `make smoke` | QEMU boot, console-server startup and shell input/output routed over IPC, kernel self-tests, credentials, BKFS user-file creation, `chmod`/`chown`, execute-bit denial, supplementary-group pointer/range/privilege checks, initrd write denial, invalid open flags, user-pointer handling, pipe and IPC rollback, endpoint creation-policy validation, restricted endpoint operations and delegation, shared-page aliases across `fork`, IPC message exchange, COW isolation, 260 repeated `exec` and fork/wait cycles, and a faulted-child teardown check |
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

1. Add capability-aware service authorization and endpoint policies.
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
