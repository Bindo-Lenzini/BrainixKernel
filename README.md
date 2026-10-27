BrainixKernel

A hobby operating system kernel for AArch64, written in C and ARM64 assembly.

![Build](https://github.com/Bindo-Lenzini/BrainixKernel/actions/workflows/build.yml/badge.svg) ![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)

BrainixKernel is an experimental operating system project targeting the AArch64 architecture. It is developed primarily on Apple Silicon and designed to run on QEMU's virt machine.

The project explores kernel development, virtual memory, task scheduling, system calls, device drivers, user-space programs, storage, networking, and a graphical desktop environment.

BrainixKernel is a work in progress. Features described in this document reflect the current source tree and project documentation; their availability and completeness may vary. This project is intended for development and experimentation, not production use.

Table of Contents
Overview
Features
Architecture and Platform
Repository Structure
Requirements
Building
Running in QEMU
User Space
Storage and Networking
Security Model
Continuous Integration
Current Limitations
Roadmap
Contributing
License
Overview

BrainixKernel is built without relying on a hosted operating system runtime for its kernel and user-space binaries. Its build system uses a freestanding AArch64 target and links the kernel with a custom linker script.

The repository contains:

Kernel code written in C and ARM64 assembly.
A linker script for the kernel image.
User-space executables and supporting runtime code.
Scripts for generating font data, an initial RAM filesystem, and a media image.
QEMU configuration for virtual devices, networking, and serial output.
A GitHub Actions workflow that builds the kernel binary.
Features

The current codebase includes implementations or supporting code for the following areas.

Subsystem	Description
Architecture	AArch64 kernel targeting QEMU virt
Kernel runtime	Kernel entry point, exception handling, memory management, and heap allocation
Virtual memory	MMU-related code and physical memory management
Tasks	Task management, scheduling, and system-call handling
User space	Freestanding C runtime, system-call stubs, shell, and utility programs
Graphics	Framebuffer, GPU-related code, window management, and desktop components
Input	Virtual keyboard and pointer/tablet device support
Storage	Block-device code, initrd support, and the custom bkfs filesystem
Networking	Network-device support and code for Ethernet-related networking, ARP, DHCP, IP, ICMP, UDP, and TCP
Security	Task credentials, capability definitions, and partial access-control enforcement

The presence of code in a subsystem does not necessarily mean that every expected feature, protocol, or interface is complete or fully tested.

Architecture and Platform
Target architecture
Architecture: AArch64 (ARM64)
Compiler target: aarch64-elf
Virtual machine: QEMU virt
Virtual CPU: Cortex-A72
Interrupt controller configuration: GICv2
Kernel language: C and ARM64 assembly
Linker: LLVM LLD

The build uses freestanding compilation options, including -ffreestanding and -nostdlib, and links the kernel using linker.ld.

QEMU devices

The current QEMU configuration includes:

Virtio GPU.
Virtio keyboard.
Virtio tablet.
Virtio block devices.
Virtio network device.
Serial console connected to standard input and output.

The virtual machine is configured with 2 GiB of memory. The current QEMU display configuration uses the Cocoa backend, intended for macOS.

Repository Structure
text
BrainixKernel/
├── .github/
│   └── workflows/
│       └── build.yml       # Automated build workflow
├── initrd/                 # Initial RAM filesystem inputs and user ELF files
├── media/                  # Media image inputs
├── scripts/
│   ├── mkfont.py           # Font-data generator
│   ├── mkinitrd.py         # Initial RAM filesystem generator
│   └── mkmedia.py          # Media image generator
├── src/                    # Kernel C and assembly sources
├── user/                   # User-space runtime and programs
├── linker.ld               # Kernel linker script
├── Makefile                # Build and QEMU run targets
├── mock_vnc.py             # Auxiliary VNC-related Python script
├── project.txt             # Project notes
└── README.md


The build system generates additional files, including build/kernel.elf, build/kernel.bin, generated font and initrd data, media.img, disk.img, and net.pcap.

Generated artifacts are distinct from the source files tracked in the repository.

Requirements
macOS

The documented development environment is macOS on Apple Silicon.

Install the required tools using Homebrew:

bash
brew install qemu llvm lld python3


Install Pillow for the Python image/font-generation tooling:

bash
python3 -m pip install Pillow


The build uses the following tools:

clang — compiles kernel and user-space C and assembly sources.
ld.lld — links kernel and user-space ELF files.
llvm-objcopy — converts the kernel ELF into a raw binary.
python3 and Pillow — support build-time asset generation.
qemu-system-aarch64 — runs the virtual machine.
make — orchestrates the build.

Ensure that the required executables are available through your PATH.

Building

Clone the repository and enter its directory:

bash
git clone https://github.com/Bindo-Lenzini/BrainixKernel.git
cd BrainixKernel


Build the kernel:

bash
make


The default build target produces:

text
build/kernel.elf
build/kernel.bin


build/kernel.elf is the linked kernel ELF image. build/kernel.bin is the raw binary generated from it.

The build also compiles user-space programs and generates data required by the kernel.

Clean build

To remove the build directory and selected generated artifacts before rebuilding:

bash
make clean
make


Note that the current clean target does not remove every generated file, including disk.img.

Linux build configuration

The Makefile defaults OBJCOPY to a Homebrew-specific path. On systems where llvm-objcopy is installed elsewhere, override it explicitly:

bash
make clean
make OBJCOPY=llvm-objcopy


This overrides the configured executable path without modifying the Makefile.

The QEMU display arguments currently use the macOS Cocoa backend. Building on Linux and running the graphical virtual machine are separate concerns; the default display configuration may need to be adapted for a Linux host.

Running in QEMU

On a compatible macOS development environment, run:

bash
make run


The run target builds the required kernel and generated images, then launches qemu-system-aarch64.

The virtual machine is configured with the kernel binary, block devices, a network device, serial output, and a graphical display.

Generated disk images

The current build system uses these image files:

File	Purpose
media.img	Generated from the contents of media/
disk.img	Empty 16 MiB disk image created by the Makefile
net.pcap	Packet capture output configured for the QEMU network backend

The existence of a disk image or virtual device does not imply that all corresponding storage or networking functionality is complete.

User Space

The user/ directory contains the freestanding user-space runtime and application sources.

The Makefile currently builds the following programs:

Program	Intended role
test	Test or demonstration program
sh	Shell
echo	Text output utility
wc	Word and line counting utility
ls	Directory listing utility
cat	File output utility
whoami	Credential inspection utility
udp_send	UDP transmission utility
udp_echo	UDP echo utility
host	Host or network lookup utility
tcp_connect	TCP connection utility
wget	Network download utility

These names describe the programs built by the current Makefile. They should not be interpreted as a claim of complete POSIX compatibility.

The user-space runtime includes startup code, system-call assembly stubs, a C library implementation, and DNS-related code.

Storage and Networking
Storage

Storage-related source files include the block-device implementation, bkfs, initrd handling, and filesystem-related interfaces.

The build creates an initial RAM filesystem containing user-space ELF executables. It also creates a separate media image from the files under media/.

The current filesystem should not be assumed to provide the full feature set of a mature Unix filesystem.

Networking

The source tree contains network-device and protocol code for components including:

Virtio networking.
ARP and DHCP.
IP and ICMP.
UDP and TCP.
Socket-related interfaces.
DNS support in the user-space runtime.

QEMU is configured with its user-mode networking backend and writes captured packets to net.pcap.

Protocol implementation, interoperability, error handling, and completeness should be evaluated independently. The presence of these components does not establish full standards compliance or production-grade networking.

Security Model

BrainixKernel contains a preliminary Unix-like credential and capability model.

Credentials

The project documentation describes task credentials containing:

Real, effective, and saved user IDs.
Real, effective, and saved group IDs.
Effective and permitted capability masks.

The documented credential inheritance path copies credentials when a task is forked. New tasks currently default to UID 0 with all capabilities set.

User-space programs can query several identity values through system calls, including UID, effective UID, GID, and effective GID.

Access control

The documented access-control implementation is partial:

Writable opens and opens using O_CREAT or O_TRUNC are denied for non-root effective UIDs.
Read-only opens are permitted by the current sys_open() policy.
The bkfs filesystem does not yet store per-inode UID, GID, and permission-mode metadata.

Consequently, the capability definitions must not be interpreted as proof that every privileged operation is protected by capability checks.

The current security model is experimental. It is not a security boundary suitable for untrusted workloads or production deployment.

Continuous Integration

The repository includes a GitHub Actions workflow at .github/workflows/build.yml.

The workflow runs on pushes and pull requests and performs the following steps:

Checks out the repository.
Installs Clang, LLD, LLVM, Python, Pillow, and DejaVu fonts on Ubuntu.
Creates a font-path compatibility shim for the font-generation script.
Runs a clean build with OBJCOPY=llvm-objcopy.
Uploads build/kernel.bin as the kernel-bin artifact.

The workflow validates the configured build process. It does not, by itself, establish that the kernel boots successfully in QEMU or that every subsystem passes runtime tests.

To inspect build results, open the Actions tab in the GitHub repository and select the relevant workflow run.

Current Limitations

The following limitations are documented or evident from the current build configuration:

Security enforcement: Credential and capability enforcement is incomplete.
Filesystem metadata: Per-inode ownership and permission metadata are not yet implemented in bkfs.
Multiprocessor support: The current QEMU configuration does not explicitly request multiple CPUs; the documented configuration is single-core.
Virtual memory: Advanced memory-management features such as demand paging, mmap, and a page cache remain planned work.
Scheduling: More advanced scheduling and wait-queue mechanisms remain planned work.
Hardening: Additional memory-protection and kernel-hardening measures remain on the roadmap.
Platform portability: The default QEMU display backend and OBJCOPY path are macOS-specific.
Runtime validation: A successful build is not equivalent to a successful boot or comprehensive subsystem testing.

These limitations are relevant when evaluating the project's current functionality and security properties.

Roadmap

The following items are carried forward from the project's existing development plan. They are goals, not claims of completed functionality.

Filesystem and system interfaces
Add per-inode mode, UID, and GID metadata to bkfs.
Implement stat(2), chmod(2), and chown(2).
Add SYS_KILL with capability and same-UID checks.
Add SYS_SETUID and SYS_SETGID.
Implement poll() and select().
Add nanosleep, clock_gettime, and proper timer interrupts.
Memory management and scheduling
Implement mmap, demand paging, and a page cache.
Develop a more advanced scheduler, such as MLFQ or a CFS-like design.
Add wait queues where required by the scheduler and blocking operations.
Hardening and user space
Investigate W^X memory protections for user and kernel mappings.
Add stack canaries and appropriate ARMv8-A privilege protections.
Evaluate ports of musl libc and BusyBox.
Multiprocessor support
Implement spinlocks and memory-ordering primitives.
Introduce per-CPU state.
Add inter-processor interrupts through the GIC.
Validate concurrent kernel operations under SMP.
Advanced security and filesystems
Investigate KASLR.
Develop a mandatory-access-control or LSM-style hook layer.
Consider chroot(2).
Explore journaling for bkfs, tmpfs, procfs, and sysfs.
Contributing

Contributions, bug reports, and technical discussions are welcome.

When reporting a problem, include:

The host operating system and architecture.
The compiler, linker, and QEMU versions.
The exact command used.
The relevant build or runtime output.
The expected behavior and the behavior observed.

For changes to kernel subsystems, keep implementation claims and documentation aligned. Distinguish between code that compiles, functionality that has been exercised in QEMU, and functionality that has been independently tested.

License

BrainixKernel is distributed under the GNU General Public License v2.0 (GPL-2.0). See the LICENSE file for the applicable license terms.

BrainixKernel is an experimental operating system project. Features, interfaces, and build requirements may change as development progresses.