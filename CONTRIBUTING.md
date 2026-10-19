# Contributing to BrainixKernel

Thanks for your interest. Bug reports, fixes, documentation and new features are all welcome.

## Before you start

* For bugs and small fixes, open a pull request directly.
* For larger changes (new subsystems, changes to the boot sequence or the public
  interfaces between modules), open an issue first and describe the plan. This
  saves time if the direction does not fit the project.

## Building and testing

1. Install clang, lld, llvm (for llvm-objcopy) and python3.
2. Run `make` and make sure the build succeeds without new warnings.
3. Run `make run` and check the boot log on the serial console.
4. If your change touches networking, describe what you tested (for example,
   `ping` or `wget` inside the guest).

Please do not commit files from `build/`, disk images (`*.img`) or capture files (`*.pcap`).

## Code style

* C99, four-space indentation, no tabs in C sources.
* Keep the kernel free of the C standard library (`-ffreestanding -nostdlib`).
* Use the existing `uart_puts` and related helpers for kernel output.
* Keep each commit focused on one change, and write a short message in the imperative
  mood, for example `net: fix checksum on short UDP packets`.

## Pull requests

* Describe what changed and why.
* Say how you tested it.
* Keep the diff small. Split unrelated changes into separate pull requests.
* Continuous integration will build the project on every pull request. A failing
  build must be fixed before merging.

## Reporting bugs

Use the bug report template. Include the commit hash, the host system, the clang
version, and the full output of `make` or the serial log.

## License

By submitting a contribution you agree that it is licensed under the GNU General
Public License, version 2 (see LICENSE).
