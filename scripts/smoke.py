#!/usr/bin/env python3
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


BOOT_MARKER = b"BRAINIX_BOOT_OK:"
SELFTEST_START = b"BRAINIX_SELFTEST_START:"
SELFTEST_OK = b"BRAINIX_SELFTEST_OK:"
SELFTEST_FAIL = b"BRAINIX_SELFTEST_FAIL:"
UACCESS_OK = b"BRAINIX_UACCESS_SELFTEST_OK"
CREDENTIALS_OK = b"BRAINIX_CREDENTIAL_SELFTEST_OK"
USER_SHELL_READY = b"Brainix user-shell v0.4"
GUEST_SECURITY_OK = b"BRAINIX_GUEST_SECURITY_OK"
GUEST_SECURITY_FAIL = b"COW isolation FAILED"


def main():
    parser = argparse.ArgumentParser(description="Boot BrainixKernel under headless QEMU.")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--disk", required=True, type=Path)
    parser.add_argument("--serial-log", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()

    if not args.kernel.is_file():
        print("SMOKE_FAIL: KERNEL_IMAGE_MISSING: {}".format(args.kernel), file=sys.stderr)
        return 2
    if not args.disk.is_file():
        print("SMOKE_FAIL: DISK_IMAGE_MISSING: {}".format(args.disk), file=sys.stderr)
        return 2

    socket_dir = None
    try:
        args.serial_log.parent.mkdir(parents=True, exist_ok=True)
        log_fd, log_name = tempfile.mkstemp(
            prefix="." + args.serial_log.name + ".",
            suffix=".tmp",
            dir=str(args.serial_log.parent),
        )
        os.close(log_fd)
    except OSError as error:
        print("SMOKE_FAIL: SERIAL_LOG_CREATE_FAILED: {}".format(error), file=sys.stderr)
        return 8
    log_path = Path(log_name)
    try:
        socket_dir = Path(tempfile.mkdtemp(prefix="brainix-smoke-"))
    except OSError as error:
        log_path.unlink(missing_ok=True)
        print("SMOKE_FAIL: SERIAL_SOCKET_CREATE_FAILED: {}".format(error), file=sys.stderr)
        return 8
    serial_socket_path = socket_dir / "serial.sock"

    command = [
        args.qemu,
        "-M", "virt,gic-version=2",
        "-cpu", "cortex-a72",
        # Keep the headless CI guest lightweight; the graphical VM uses 2 GiB.
        "-m", "512M",
        "-device", "virtio-gpu-pci",
        "-device", "virtio-keyboard-pci",
        "-device", "virtio-tablet-pci",
        "-drive",
        "id=disk0,file={},format=raw,if=none,snapshot=on".format(args.disk.resolve()),
        "-device", "virtio-blk-pci,drive=disk0,disable-legacy=on",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0,disable-legacy=on",
        "-serial", "unix:{},server=on,wait=off".format(serial_socket_path),
        "-monitor", "none",
        "-display", "none",
        "-no-reboot",
        "-kernel", str(args.kernel.resolve()),
    ]

    process = None
    serial = None
    log_data = bytearray()
    try:
        try:
            process = subprocess.Popen(
                command,
                cwd=str(Path(__file__).resolve().parents[1]),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
        except FileNotFoundError:
            print("SMOKE_FAIL: QEMU_NOT_FOUND: {}".format(args.qemu), file=sys.stderr)
            return 3
        except OSError as error:
            print("SMOKE_FAIL: QEMU_START_FAILED: {}".format(error), file=sys.stderr)
            return 3

        deadline = time.monotonic() + args.timeout
        connect_deadline = min(deadline, time.monotonic() + 15)
        while serial is None and time.monotonic() < connect_deadline:
            if process.poll() is not None:
                error_output = process.stderr.read().decode("utf-8", errors="replace").strip()
                print(
                    "SMOKE_FAIL: QEMU_EXITED_BEFORE_SERIAL (exit={})".format(
                        process.returncode
                    ),
                    file=sys.stderr,
                )
                if error_output:
                    print(error_output, file=sys.stderr)
                return 4
            try:
                serial = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                serial.settimeout(0.1)
                serial.connect(str(serial_socket_path))
            except OSError:
                if serial is not None:
                    serial.close()
                serial = None
                time.sleep(0.1)

        if serial is None:
            print("SMOKE_FAIL: QEMU_SERIAL_CONNECT_FAILED", file=sys.stderr)
            return 4

        guest_test_sent = False
        while time.monotonic() < deadline:
            try:
                chunk = serial.recv(4096)
            except socket.timeout:
                chunk = b""
            except OSError as error:
                print("SMOKE_FAIL: SERIAL_READ_FAILED: {}".format(error), file=sys.stderr)
                return 4

            if chunk:
                log_data.extend(chunk)
                with log_path.open("ab") as log_file:
                    log_file.write(chunk)
                    log_file.flush()

            if SELFTEST_FAIL in log_data:
                print("SMOKE_FAIL: SELFTEST_FAILED")
                return 6

            if all(marker in log_data for marker in
                   (BOOT_MARKER, SELFTEST_START, SELFTEST_OK, UACCESS_OK,
                    CREDENTIALS_OK, USER_SHELL_READY)):
                if process.poll() is not None:
                    print("SMOKE_FAIL: QEMU_EXITED_AFTER_MARKERS (exit={})".format(
                        process.returncode
                    ))
                    return 7
                if not guest_test_sent:
                    try:
                        serial.sendall(b"test\n")
                    except OSError as error:
                        print("SMOKE_FAIL: GUEST_TEST_INPUT_FAILED: {}".format(error),
                              file=sys.stderr)
                        return 4
                    guest_test_sent = True

            if GUEST_SECURITY_FAIL in log_data:
                print("SMOKE_FAIL: GUEST_SECURITY_TEST_FAILED", file=sys.stderr)
                return 9

            if guest_test_sent and GUEST_SECURITY_OK in log_data:
                if process.poll() is not None:
                    print("SMOKE_FAIL: QEMU_EXITED_AFTER_GUEST_TEST (exit={})".format(
                        process.returncode
                    ))
                    return 7
                print("SMOKE_OK: boot, kernel selftests, credentials, user access and COW passed")
                return 0

            if process.poll() is not None:
                error_output = process.stderr.read().decode("utf-8", errors="replace").strip()
                print(
                    "SMOKE_FAIL: QEMU_EXITED_BEFORE_SUCCESS (exit={})".format(
                        process.returncode
                    ),
                    file=sys.stderr,
                )
                if error_output:
                    print(error_output, file=sys.stderr)
                return 4

            time.sleep(0.1)

        if BOOT_MARKER not in log_data:
            result = "KERNEL_BOOT_MARKER_MISSING"
        elif SELFTEST_START not in log_data:
            result = "SELFTEST_NOT_STARTED"
        elif SELFTEST_OK not in log_data:
            result = "SELFTEST_INCOMPLETE"
        elif UACCESS_OK not in log_data:
            result = "UACCESS_SELFTEST_INCOMPLETE"
        elif CREDENTIALS_OK not in log_data:
            result = "CREDENTIAL_SELFTEST_INCOMPLETE"
        elif USER_SHELL_READY not in log_data:
            result = "USER_SHELL_NOT_READY"
        elif not guest_test_sent:
            result = "GUEST_SECURITY_TEST_NOT_STARTED"
        elif GUEST_SECURITY_OK not in log_data:
            result = "GUEST_SECURITY_TEST_INCOMPLETE"
        else:
            result = "QEMU_DID_NOT_REMAIN_RUNNING"
        print("SMOKE_FAIL: {} (timeout={}s)".format(result, args.timeout), file=sys.stderr)
        return 5
    finally:
        if serial is not None:
            serial.close()
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        try:
            os.replace(log_path, args.serial_log)
        except OSError as error:
            print("SMOKE_FAIL: SERIAL_LOG_PUBLISH_FAILED: {}".format(error), file=sys.stderr)
            return 8
        if socket_dir is not None:
            shutil.rmtree(socket_dir)
        if process is not None and process.stderr:
            process.stderr.close()


if __name__ == "__main__":
    sys.exit(main())
