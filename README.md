# BrainixKernel

An experimental AArch64 kernel written in C and ARM64 assembly, developed on
Apple Silicon and tested in QEMU.

[![Build](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml/badge.svg)](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](./LICENSE)

> [!WARNING]
> **BrainixKernel is an unfinished experimental kernel. It is not production
> ready and does not guarantee security, reliability, or data integrity.**
> Security mechanisms and tests are incomplete. Do not rely on this kernel to
> protect sensitive data, and do not expose it to untrusted users or networks.
> Use it for development and experimentation at your own risk.

The project is an ongoing effort to build a small, understandable kernel from
scratch and explore modern operating-system design. This page describes the
current state of development; implemented code does not necessarily mean a
feature is complete or fully validated.

## Project identity

**BrainixKernel is only a kernel.** It is not an operating system or a
distribution. The repository may contain experimental user-space programs and
bring-up tools used to develop and test the kernel; these do not make
BrainixKernel an OS distribution.

**LambdaNexus OS is a separate operating-system distribution based on
the BrainixKernel kernel.** LambdaNexus-specific desktop, system services,
applications, and distribution infrastructure belong to that separate
distribution. Any user-space or UI code currently in this repository is
experimental bring-up or test material, not the definition of BrainixKernel as
an OS. Work on the LambdaNexus distribution is outside the current
kernel-security focus.

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
- Kernel-backed initrd and BKFS file capabilities carry explicit read/write
  rights and support `fd_restrict`; ordinary userland `open` currently returns
  an IPC-backed VFS proxy with independently attenuable read/write rights.
- A small userland including a shell, basic utilities, network examples, and a
  user-space console-routing service and experimental VFS open broker connected
  to clients over IPC.
- Experimental virtual-device, storage, network, and graphical desktop support.

The console service routes terminal input to the shell and shell output to the
UART through IPC. PL011 register access, interrupt handling, and the UART
receive buffer remain in the kernel; this is not yet a user-space UART driver.
Console IPC descriptors follow least authority: the input service can only
send, the output service can only receive, and the shell can send and receive.
None of these descriptors can map the shared page or delegate endpoints.

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
`ipc_send_vfs`/`ipc_recv_vfs` provide a distinct transfer type for VFS proxy
descriptors. The sender selects a subset of read, write, and delegation rights;
the receiver obtains a proxy fd, not a generic IPC endpoint or kernel-backed
file descriptor. The VFS broker registers proxy channels, so ordinary IPC
endpoints cannot be relabeled as file proxies. Queued VFS transfers retain
their channel reference, reject transfer cycles, and remain isolated from
generic endpoint/file receive APIs.
`ipc_map` maps the channel's zero-initialized 4 KiB shared page at a free,
page-aligned user address. The mapping remains writable and shared after
`fork`; closing endpoints does not unmap existing mappings.

Kernel-backed initrd and BKFS descriptors carry `FD_RIGHT_READ` and/or
`FD_RIGHT_WRITE`, plus `FD_RIGHT_DELEGATE`, constrained by the requested open
mode and file permissions. `fd_restrict(fd, rights)` can remove rights from
these file capabilities, including reducing them to no access; it rejects
unknown rights and attempts to add authority. `dup2` and `fork` preserve the
reduced rights. `ipc_send_file`/`ipc_recv_file` transfer these descriptors with
a selected subset of rights and the current file offset.
Initrd capabilities remain read-only. BKFS inodes are pinned while referenced
by an open descriptor or queued IPC capability; `fork` and `dup2` retain their
own references, and the final close releases them. Deleting a pinned inode or
formatting BKFS while any inode is pinned returns `EBUSY`, preventing a
transferred capability from aliasing a reused inode. BKFS reads and writes
continue to enforce the receiver's current file permissions.

### Capability invariants and failure behavior

The security review uses these invariants as the baseline for capability
changes:

- **Authorized creation:** user tasks cannot relabel a generic IPC endpoint as
  a VFS proxy. The VFS broker can register only its assigned endpoint, and the
  kernel records the maximum rights for the peer.
- **Monotonic authority:** restriction, duplication, inheritance, and transfer
  may preserve or reduce rights, never add rights beyond the source descriptor
  and registered maximum.
- **Type isolation:** endpoint, kernel-backed file, and VFS proxy transfers
  remain distinct; a receive API for one type cannot consume another type.
- **Lifetime safety:** queued capabilities retain their backing endpoint or
  inode until consumed or discarded; final close releases the reference.
- **Fail closed on service loss:** a client endpoint does not imply the
  service is still alive. After its peer closes, operations fail or report
  EOF; they must not fall back to a broader kernel-backed open path. There is
  no general capability-revocation mechanism yet.

Host IPC tests cover registration, attenuation, type isolation, queued
reference lifetime, and a closed VFS service endpoint. Guest tests cover
unprivileged registration denial and rights surviving VFS proxy transfer,
`dup2`, `fork`, and `exec`. These are targeted invariants, not a complete
capability-system proof.

The experimental `vfsd.elf` service accepts ordinary userland
`open(path, flags)` calls from the shell and its descendants and returns an
IPC-backed file descriptor. Userland `read`/`write` calls on that descriptor
are sent back to the service, which uses a private kernel-backed descriptor
for the actual I/O. Its kernel-backed
`open_backend` operation is restricted to the VFS service task and revoked
when that task successfully executes another image. This exercises user-space
path brokerage and request routing for initrd and BKFS files. The filesystem
and block-device backends remain kernel-backed. VFS proxies support ordinary
`read`, `write`, `close`, `dup2`, `fork`, and `exec`, but cannot be transferred
using `ipc_send_file`; that operation is only defined for kernel-backed file
capabilities. `fd_restrict` can remove proxy read/write rights, and the kernel
checks those rights on VFS RPC requests, including requests sent directly
through the IPC API. Duplication, `fork`, and `exec` preserve attenuation.
Each proxy I/O request carries at most 240 data bytes, so reads and writes may
complete partially. `vfsd` currently supports up to six simultaneously open
files. The service runs with the default unprivileged user credentials.

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
- Added explicit read/write rights to opened initrd and BKFS descriptors.
  `fd_restrict` only permits attenuation, and `dup2`/`fork` preserve reduced
  rights; guest tests check denied reads/writes and reject rights restoration.
- Added IPC transfer for read-only initrd file capabilities. Delegation carries
  the file offset and a non-escalating rights subset; ordinary and endpoint
  receive APIs cannot consume file-bearing messages, and invalid output pointers
  leave the queued capability intact. BKFS descriptors now transfer with
  selected rights and offsets; open descriptors and queued transfers pin inodes,
  so deletion and formatting fail with `EBUSY` until all references are gone.
- Added an experimental user-space VFS open broker. Ordinary userland `open`
  sends a path and flags over IPC with a per-file endpoint; `vfsd.elf` opens
  files using a role-restricted kernel backend and services userland `read` and
  `write` requests over that endpoint. Guest tests cover initrd/BKFS I/O,
  permission failures, and backend isolation; filesystem storage remains
  kernel-backed.
- Added kernel-tracked read/write rights to VFS proxy descriptors. Rights can
  only be attenuated; the kernel validates RPC requests even when sent through
  the raw IPC API, and `dup2`, `fork`, and `exec` preserve the reduced rights.
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
  Console tasks receive least-authority endpoint rights, and verify forbidden
  receive/send, mapping, and delegation operations fail during startup.
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

### 2026-11-01 — syscall, memory, and packet-input hardening

- Made UART reads transactional on copy-out failure and completed `brk` growth
  rollback and shrink-time unmapping.
- Tracked writable and executable user mappings per physical page. Conflicting
  W/X aliases are rejected across address spaces; unmapping, COW transitions,
  cloning, and teardown update those claims. Failed user mappings roll back
  intermediate tables, and unmapping prunes empty table pages. The privileged
  kernel direct map remains writable, while SCTLR.WXN prevents EL1 execution
  through writable mappings; kernel aliases are not exposed as user mappings.
- Rejected malformed IPv4 lengths, unsupported fragments, invalid ICMP
  payload lengths, and invalid TCP header offsets rather than trusting Ethernet
  padding or clamping packet lengths to received frame size.
- Added ASID exhaustion/reuse checks, eager-clone failure and permission
  regressions, and sanitizer-backed malformed packet corpus tests to
  `make host-test`.
- Pinned both interactive and headless QEMU configurations to one CPU. The
  kernel remains single-core: global state and interrupt paths are not
  synchronized for SMP, and multi-core boot stays disabled pending a complete
  lock-order design and race validation.

### 2026-11-04 — VirtIO network receive validation

- Bounded VirtIO RX used-ring advancement and validated descriptor IDs,
  in-flight ownership, duplicate completions, and device-reported DMA lengths
  before accessing receive buffers.
- Dropped empty or oversized Ethernet frames and undersized caller buffers
  without truncating packets; safely recycled valid descriptors. Invalid
  queue completions disable RX instead of silently consuming buffers.
- Added host regression coverage for used-index wraparound and malformed
  completion metadata. These tests exercise validation logic, not faulty
  physical devices or SMP behavior.

### 2026-11-04 — VirtIO block completion validation

- Validated one-at-a-time used-ring advancement, descriptor-chain ID, device
  status, and reported writable-buffer length before accepting block I/O.
- Reject null buffers and sectors beyond device capacity; disable a device
  after a timeout or malformed completion so later requests cannot reuse
  buffers while an earlier DMA may still be active.
- Added host cases for index wraparound, valid I/O errors, and malformed
  completion metadata. The tests cover the pure validation policy; hardware
  timeout and fault injection are not yet available.

### 2026-11-04 — VirtIO GPU queue and geometry validation

- Validate single-command control-queue completions, used-ring IDs, and
  response lengths. Timeouts or malformed completions disable the GPU queue
  rather than reusing buffers while a device may still access them.
- Check framebuffer dimension arithmetic before allocating backing pages, and
  clip rectangle coordinates using widened signed arithmetic.
- Add host tests for completion metadata and framebuffer layout boundaries.
  They do not emulate a faulty GPU or test DMA cancellation.

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
| `make host-test` | ELF parser cases, BKFS permissions and malformed block-pointer rejection, scheduler reaper adoption/fallback and dead-orphan teardown, IPC capability/queue behavior, transactional TCP/pipe/UDP receives, bounded physical-page allocation, user mapping W^X alias tracking, COW and eager-clone rollback/permission preservation, ASID exhaustion/reuse, VirtIO RX, block, and GPU completion validation, and an ASan/UBSan malformed IPv4/ICMP/UDP/TCP input corpus |
| `make smoke` | QEMU boot, console-server startup and shell input/output routed over IPC, kernel self-tests, credentials, BKFS user-file creation, `chmod`/`chown`, execute-bit denial, supplementary-group pointer/range/privilege checks, initrd write denial, invalid open flags, file descriptor rights attenuation across `dup2`/`fork`, read-only initrd capability transfer over IPC, user-pointer handling, pipe and IPC rollback, endpoint creation-policy validation, restricted endpoint operations and delegation, shared-page aliases across `fork`, IPC message exchange, COW isolation, 260 repeated `exec` and fork/wait cycles, and a faulted-child teardown check |
| GitHub Actions | Host tests, kernel build, and the headless QEMU smoke target |

Tests cover selected regression scenarios. They do not prove the absence of
bugs or vulnerabilities and do not replace broader testing or independent
review.

## Security status and limitations

Security is an active area of development, not a finished property of the
kernel. Current work includes targeted checks around user pointers, ELF
permissions, COW cloning, default task credentials, BKFS read/write permissions,
and bounded copying of syscall strings and `exec` argument vectors. Malformed,
unreadable, or unterminated argument vectors are rejected instead of silently
truncated. TCP receive data is removed from the stream queue only after the
user-buffer copy succeeds; pipe reads use the same peek/copy/consume ordering.
UART input is staged until its user-buffer copy succeeds.
UDP datagrams likewise remain queued until payload and requested source-address
copies succeed. Contiguous physical-page requests larger than detected RAM are
rejected before allocator bitmap traversal. `brk` growth rolls back pages if
allocation or mapping fails, and shrinking releases pages no longer covered by
the break. Important limitations remain:

The current working threat-model baseline treats user processes and data
crossing syscall, IPC, filesystem, and network boundaries as untrusted, and
includes allocation failures in the robustness scope. Physical attacks,
compromised firmware, hardware side channels, and SMP races are out of scope
for this baseline; this is a bounded development scope, not a security
certification.

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
- Trusted kernel aliases and non-user mapping paths need broader W^X review.
- Syscall, filesystem, network, and device input paths need more systematic
  negative testing and fuzzing.
- The kernel is single-core; synchronization for SMP is not implemented.
- User mappings reject writable/executable aliases across all address spaces.
  The EL1 direct map remains writable and is protected from EL1 execution by
  WXN; strict protection against privileged kernel writes through that alias
  would require a separate mapping policy.

Do not treat passing tests or this feature list as a security guarantee.

### SMP synchronization plan

The kernel remains single-core; the following shared state must be protected
before enabling secondary CPUs:

- Scheduler/task registry, task state, parent/reaper links, task IDs, and
  descriptor lifetime.
- Physical allocator bitmap/refcounts, address-space page tables, COW updates,
  ASID allocation, and TLB invalidation.
- IPC channel queues, endpoint references and transfer-cycle checks; pipe
  buffers and references; socket/TCP connection state and receive queues.
- BKFS metadata/cache and inode references, block-device state, VirtIO queues,
  network tables, and UART input state.

Implementation prerequisites:

1. Add per-CPU state plus IRQ-safe spinlocks/atomics with documented memory
   ordering; keep the scheduler lock out of blocking operations and resource
   teardown.
2. Establish and audit a lock-order graph. The initial required nested order
   is address-space state before physical-page allocator state. IPC, filesystem,
   and device resources must not be released or called into while holding an
   unrelated spinlock; detach work under the lock and perform releases after
   unlocking.
3. Never hold a spinlock across user-memory copies, allocation, blocking, or
   device waits. Queue operations that copy to userspace need a reservation or
   generation token so failure can leave the queued item intact without
   allowing a second receiver to consume it.
4. Add lock assertions/race tests for references, queue close-vs-receive,
   COW-vs-unmap, task reap-vs-wakeup, and interrupt-vs-device completion.
   Enable SMP only after those tests pass on a multi-core emulator.

## Roadmap

Near-term development priorities:

1. Complete capability authorization across every syscall and kernel resource;
   current file, endpoint, and VFS-proxy controls are not a complete
   object-capability system.
2. Expand malformed-input and failure-injection testing for filesystem and
   device interfaces; the network parser corpus is bounded regression
   fuzzing, not exhaustive fuzzing.
3. Review future VM and kernel mapping APIs against the physical W^X alias
   tracker and define a policy for privileged writable aliases.
4. Implement and validate IRQ-safe synchronization before adding SMP support.

Longer-term goals include `mmap`, demand paging, a page cache, SMP support,
stronger filesystem permissions, and additional kernel-hardening measures.
BrainixKernel is an experimental kernel, not a production-ready or compatible
replacement for XNU, Linux, or another production kernel.

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
