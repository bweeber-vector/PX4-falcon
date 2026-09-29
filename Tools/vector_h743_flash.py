#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 PX4 Development Team. All rights reserved.
"""Back up, program, verify, and check a Vector H743 through STM32 ROM DFU.

Requires dfu-util, pyserial and pymavlink. No firmware writes in --check-only.
Exit codes: 0 checks passed, 1 operation failed, 2 CLI error, 3 health issues.
"""

import argparse
import base64
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

FLASH_BASE = 0x08000000
FLASH_SIZE = 2 * 1024 * 1024
SECTOR_SIZE = 128 * 1024
APP_BASE = FLASH_BASE + SECTOR_SIZE
APP_SIZE = FLASH_SIZE - SECTOR_SIZE
OPTION_BASE = 0x5200201C
OPTION_SIZE = 128
BOARD_ID = 6700
DFU_ID = "0483:df11"
APP_USB_ID = (0x1209, 0x5741)
ROOT = Path(__file__).resolve().parents[1]
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


class FlashError(RuntimeError):
    """An operation failed; no subsequent erase or write is allowed."""


def require(condition, message):
    if not condition:
        raise FlashError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def durable_write(path, data):
    """Exclusive creation prevents accidental replacement of recovery files."""
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


class Session:
    def __init__(self, directory):
        self.path = directory
        directory.mkdir(parents=True, mode=0o700, exist_ok=False)
        sync_directory(directory.parent)
        self.manifest = {"created_utc": timestamp(), "status": "created", "files": {}}
        self.state("created")

    def state(self, status, **values):
        self.manifest.update(values, status=status, updated_utc=timestamp())
        temporary = self.path / "manifest.tmp"
        with temporary.open("w") as stream:
            json.dump(self.manifest, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(self.path / "manifest.json")
        sync_directory(self.path)

    def record(self, name):
        path = self.path / name
        with path.open("rb") as stream:
            os.fsync(stream.fileno())
            data = stream.read()
        self.manifest["files"][name] = {"bytes": len(data), "sha256": sha256(data)}
        return data

    def save(self, name, data):
        durable_write(self.path / name, data)
        return self.record(name)


@dataclass(frozen=True)
class Device:
    serial: str
    path: str
    flash_alt: int
    option_alt: int


def parse_device(listing, serial=None):
    """Accept only the memory layout observed on the Vector STM32H743."""
    records = {}
    for line in listing.splitlines():
        if not line.startswith("Found DFU: [0483:df11]"):
            continue
        match = re.search(r'path="([^"]+)", alt=(\d+), name="([^"]+)", serial="([^"]+)"', line)
        require(match is not None, "Cannot parse DFU identity; refusing device selection")
        path, alt, name, identity = match.groups()
        if serial is None or identity == serial:
            records.setdefault((identity, path), {})[int(alt)] = re.sub(r"\s+", "", name)
    require(len(records) == 1,
            "Expected one STM32 DFU device. Enter ROM DFU mode; use --dfu-serial if several are connected.")
    (identity, path), alts = next(iter(records.items()))
    flash = [alt for alt, name in alts.items() if name == "@InternalFlash/0x08000000/16*128Kg"]
    options = [alt for alt, name in alts.items() if name == "@OptionBytes/0x5200201C/01*128e"]
    require(len(flash) == len(options) == 1,
            "DFU memory layout is not the expected 2 MiB Vector H743 layout; refusing to write")
    return Device(identity, path, flash[0], options[0])


def validate_image(data, base, limit):
    require(8 <= len(data) <= limit, f"Invalid image size for {base:#010x}: {len(data)}")
    stack, reset = struct.unpack_from("<II", data)
    # STM32H743 DTCM, AXI SRAM, D2 SRAM and SRAM4. NuttX SP symbols can be
    # unaligned; Cortex-M clears the low SP bits when loading the vector.
    ram = [(0x20000000, 0x20020000), (0x24000000, 0x24080000),
           (0x30000000, 0x30048000), (0x38000000, 0x38010000)]
    require(any(low < stack <= high for low, high in ram), "Invalid initial stack pointer")
    require(reset & 1 and base <= (reset & ~1) < base + len(data),
            f"Image reset vector does not target its flash region at {base:#010x}")


def load_payload(path, base, limit):
    """Require board metadata for raw images as well as packaged firmware."""
    require(path.suffix in (".bin", ".px4"), "Image must be .px4 or .bin")
    package_path = path if path.suffix == ".px4" else path.with_suffix(".px4")
    package = json.loads(package_path.read_text())
    require(package.get("magic") == "PX4FWv1", "Unrecognised firmware package format")
    require(package.get("board_id") == BOARD_ID, "Firmware package board_id must be 6700 (Vector H743)")
    compressed = base64.b64decode(package["image"], validate=True)
    decoder = zlib.decompressobj()
    data = decoder.decompress(compressed, limit + 1)
    require(decoder.eof and not decoder.unused_data and len(data) <= limit,
            "Invalid or oversized firmware payload")
    require(package.get("image_size") == len(data), "Firmware package image_size does not match its payload")
    if path.suffix == ".bin":
        require(path.read_bytes() == data, "Raw .bin differs from its adjacent .px4 package")
    validate_image(data, base, limit)
    return data, {"board_id": BOARD_ID, "git_identity": package.get("git_identity"),
                  "git_hash": package.get("git_hash"), "package": str(package_path.resolve())}


def load_images(bootloader_path, firmware_path):
    boot, boot_metadata = load_payload(bootloader_path, FLASH_BASE, SECTOR_SIZE)
    app, app_metadata = load_payload(firmware_path, APP_BASE, APP_SIZE)
    return boot, app, {"bootloader": boot_metadata, "application": app_metadata}


class Dfu:
    def __init__(self, executable, device, session, timeout=600):
        self.executable, self.device, self.session, self.timeout = executable, device, session, timeout

    def assert_present(self):
        result = subprocess.run([self.executable, "-d", DFU_ID, "--list"],
                                capture_output=True, text=True, timeout=15, check=True)
        require(parse_device(result.stdout, self.device.serial) == self.device,
                "The selected DFU device or its USB path changed; stopping")

    def transfer(self, alt, address, path, write=False):
        self.assert_present()
        require(not path.exists() or write, f"Refusing to overwrite {path}")
        arguments = [self.executable, "-d", DFU_ID, "-S", self.device.serial,
                     "-p", self.device.path, "-a", str(alt), "-s", address,
                     "-D" if write else "-U", str(path)]
        log = self.session.path / (path.stem + ("-write.log" if write else "-read.log"))
        print(f"{'Writing' if write else 'Reading'} {path.name}; log: {log}", flush=True)
        with log.open("xb") as output:
            process = subprocess.Popen(arguments, stdout=output, stderr=subprocess.STDOUT)
            start = last_notice = time.monotonic()
            try:
                while process.poll() is None:
                    if time.monotonic() - start > self.timeout:
                        raise FlashError(f"DFU transfer timed out; inspect {log}")
                    if time.monotonic() - last_notice >= 15:
                        lines = log.read_text(errors="replace").replace("\r", "\n").splitlines()
                        print(lines[-1] if lines else "DFU transfer in progress...", flush=True)
                        last_notice = time.monotonic()
                    time.sleep(0.25)
                require(process.returncode == 0, f"dfu-util failed ({process.returncode}); inspect {log}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()

    def read(self, address, length, name, options=False):
        path = self.session.path / name
        self.transfer(self.device.option_alt if options else self.device.flash_alt,
                      f"{address:#010x}:{length}", path)
        data = self.session.record(name)
        require(len(data) == length, f"Short read of {name}: {len(data)} instead of {length}")
        return data

    def write(self, address, name):
        path = self.session.path / name
        require(sha256(path.read_bytes()) == self.session.manifest["files"][name]["sha256"],
                "Image snapshot changed since validation")
        self.transfer(self.device.flash_alt, f"{address:#010x}", path, write=True)


def program(dfu, session, boot, app):
    """No calls to write are reachable until every backup check succeeds."""
    validate_image(boot, FLASH_BASE, SECTOR_SIZE)
    validate_image(app, APP_BASE, APP_SIZE)
    session.save("px4-bootloader.bin", boot)
    session.save("px4-application.bin", app)
    session.state("backing_up")
    first = dfu.read(FLASH_BASE, FLASH_SIZE, "original-flash-read1.bin")
    second = dfu.read(FLASH_BASE, FLASH_SIZE, "original-flash-read2.bin")
    require(len(first) == len(second) == FLASH_SIZE and first == second,
            "Full-flash backup reads differ or are incomplete; no erase/write performed")
    require((session.path / "original-flash-read1.bin").read_bytes() == first
            and (session.path / "original-flash-read2.bin").read_bytes() == first,
            "Persisted backups do not match their reads; no erase/write performed")
    options = dfu.read(OPTION_BASE, OPTION_SIZE, "original-option-bytes.bin", options=True)
    require(len(options) == OPTION_SIZE, "Incomplete option-byte backup; no erase/write performed")
    session.save("original-bootloader.bin", first[:SECTOR_SIZE])
    recovery = (f"Vector H743 recovery backup; DFU serial: {dfu.device.serial}\n"
                f"Full flash base {FLASH_BASE:#010x}, size {FLASH_SIZE} bytes.\n"
                f"SHA256 original-flash-read1.bin: {sha256(first)}\n"
                "Two independent full-flash reads matched before programming.\n"
                "original-bootloader.bin contains the complete 128 KiB bootloader sector.\n"
                "To restore, enter ROM DFU and write original-flash-read1.bin to\n"
                "0x08000000, then read back all 2097152 bytes and compare before reboot.\n"
                "This restores internal flash, including ArduPilot flash parameters,\n"
                "but does not restore microSD contents. Keep option bytes unchanged.\n")
    session.save("RECOVERY.txt", recovery.encode())
    session.state("backup_verified", backup_reads_match=True,
                  original_flash_sha256=sha256(first))
    print(f"Backup verified and saved: {session.path}", flush=True)

    session.state("bootloader_write_started")
    dfu.write(FLASH_BASE, "px4-bootloader.bin")
    require(dfu.read(FLASH_BASE, len(boot), "bootloader-readback.bin") == boot,
            "Bootloader verification failed; application was not written")
    session.state("bootloader_verified", bootloader_readback_matches=True)
    session.state("application_write_started")
    dfu.write(APP_BASE, "px4-application.bin")
    actual = dfu.read(FLASH_BASE, FLASH_SIZE, "px4-full-flash-readback.bin")
    require(actual[:len(boot)] == boot, "Bootloader no longer matches after application write")
    require(actual[SECTOR_SIZE:SECTOR_SIZE + len(app)] == app, "Application readback mismatch")
    # dfu-util erases the sectors intersecting each binary, including padding.
    end = ((SECTOR_SIZE + len(app) + SECTOR_SIZE - 1) // SECTOR_SIZE) * SECTOR_SIZE
    expected = bytearray(first)
    expected[:end] = b"\xff" * end
    expected[:len(boot)] = boot
    expected[SECTOR_SIZE:SECTOR_SIZE + len(app)] = app
    require(actual == expected, "Unexpected change in flash outside the images/erased sectors")
    require(dfu.read(OPTION_BASE, OPTION_SIZE, "option-bytes-readback.bin", options=True) == options,
            "Option-byte readback differs; keep the board in DFU for investigation")
    session.state("images_verified_awaiting_reboot", application_readback_matches=True,
                  option_bytes_unchanged=True, unwritten_flash_sectors_unchanged=True)
    print("Both images verified byte for byte; option bytes and untouched sectors match.", flush=True)


def dependencies():
    try:
        import serial
        from serial.tools import list_ports
        from pymavlink.dialects.v20 import common
    except ImportError as error:
        raise FlashError("Install Python dependencies in your environment: pip install pyserial pymavlink") from error
    return serial, list_ports, common


def select_port(ports, requested=None, usb_path=None):
    candidates = [p for p in ports if (p.vid, p.pid) == APP_USB_ID]
    if requested:
        candidates = [p for p in candidates if os.path.realpath(p.device) == os.path.realpath(requested)]
    if usb_path:
        candidates = [p for p in candidates if (p.location or "").split(":")[0] == usb_path]
    require(len(candidates) <= 1, "Multiple Vector USB ports; choose --port explicitly")
    return candidates[0].device if candidates else None


class HealthLink:
    """Read telemetry directly, avoiding mavutil's per-instance message cache."""
    def __init__(self, port, serial, mav):
        self.mav = mav
        self.port = serial.Serial(port, 115200, timeout=0.15, write_timeout=3, exclusive=True)
        self.link = mav.MAVLink(self.port, srcSystem=254, srcComponent=190)
        self.link.robust_parsing = True
        self.next_heartbeat = 0
        self.target = None
        self.report = {"port": port, "heartbeat": None, "autopilot_version": None,
                       "sys_autostart": None, "status_text": [], "shell": {}}

    def receive(self):
        if time.monotonic() >= self.next_heartbeat:
            self.link.heartbeat_send(self.mav.MAV_TYPE_GCS, self.mav.MAV_AUTOPILOT_INVALID,
                                     0, 0, self.mav.MAV_STATE_ACTIVE)
            self.next_heartbeat = time.monotonic() + 1
        messages = self.link.parse_buffer(self.port.read(self.port.in_waiting or 1)) or []
        result = []
        for message in messages:
            source = (message.get_srcSystem(), message.get_srcComponent())
            kind = message.get_type()
            if self.target is None and kind == "HEARTBEAT" and source[1] == 1:
                require(message.autopilot == self.mav.MAV_AUTOPILOT_PX4, "USB autopilot is not PX4")
                self.target = source
            if self.target is None or source != self.target:
                continue
            result.append(message)
            data = message.to_dict()
            if kind == "HEARTBEAT":
                self.report["heartbeat"] = data
            elif kind == "AUTOPILOT_VERSION":
                self.report["autopilot_version"] = data
            elif kind == "SYS_STATUS":
                self.report["system_status"] = data
            elif kind == "STATUSTEXT":
                item = {"severity": message.severity, "text": message.text}
                if item not in self.report["status_text"]:
                    self.report["status_text"].append(item)
            elif kind == "PARAM_VALUE" and message.param_id == "SYS_AUTOSTART":
                require(message.param_type == self.mav.MAV_PARAM_TYPE_INT32,
                        "SYS_AUTOSTART has an unexpected parameter type")
                # PX4 advertises bytewise encoding; this is not float(value).
                self.report["sys_autostart"] = struct.unpack("<i", struct.pack("<f", message.param_value))[0]
        return result

    def telemetry(self, heartbeat_timeout=20):
        end = time.monotonic() + heartbeat_timeout
        while self.target is None and time.monotonic() < end:
            self.receive()
        require(self.target is not None, "No PX4 heartbeat received")
        end = time.monotonic() + 6
        next_request = 0
        while time.monotonic() < end:
            if time.monotonic() >= next_request:
                self.link.param_request_read_send(*self.target, b"SYS_AUTOSTART", -1)
                self.link.command_long_send(*self.target, self.mav.MAV_CMD_REQUEST_MESSAGE, 0,
                                           self.mav.MAVLINK_MSG_ID_AUTOPILOT_VERSION, 0, 0, 0, 0, 0, 0)
                next_request = time.monotonic() + 2
            self.receive()

    def send_shell(self, command):
        data = command.encode()
        for start in range(0, len(data), 70):
            chunk = data[start:start + 70]
            self.link.serial_control_send(10, self.mav.SERIAL_CONTROL_FLAG_EXCLUSIVE |
                                          self.mav.SERIAL_CONTROL_FLAG_RESPOND, 0, 0,
                                          len(chunk), list(chunk) + [0] * (70 - len(chunk)))

    def shell(self, command, timeout=5):
        self.send_shell(command + "\n")
        end = time.monotonic() + timeout
        text = ""
        while time.monotonic() < end:
            for message in self.receive():
                if message.get_type() == "SERIAL_CONTROL" and message.count:
                    text += bytes(message.data[:message.count]).decode(errors="replace")
            cleaned = ANSI.sub("", text).replace("\r", "")
            if (not command or command in cleaned) and cleaned.rstrip().endswith("nsh>"):
                return cleaned
        raise FlashError(f"MAVLink shell timed out for {command!r}; response: {text[-200:]}")

    def close(self):
        try:
            self.link.serial_control_send(10, 0, 0, 0, 0, [0] * 70)
        finally:
            self.port.close()


def assess_health(report, expected_airframe, expected_git=None):
    issues = []
    heartbeat = report.get("heartbeat") or {}
    if heartbeat.get("autopilot") != 12:
        issues.append("PX4 heartbeat unavailable")
    if heartbeat.get("base_mode", 0) & 128:
        issues.append("Controller is armed; disarmed bench state required")
    if report.get("autopilot_version") is None:
        issues.append("Firmware version response unavailable")
    if report.get("sys_autostart") != expected_airframe:
        issues.append(f"SYS_AUTOSTART is {report.get('sys_autostart')}; expected {expected_airframe}")
    shell = report.get("shell", {})
    if "HW arch: VECTOR_H743" not in shell.get("ver all", ""):
        issues.append("Vector H743 hardware identity was not confirmed")
    if expected_git and f"PX4 git-hash: {expected_git}" not in shell.get("ver all", ""):
        issues.append("Running firmware git hash differs from the selected package")
    for topic in ("sensor_accel", "sensor_gyro", "sensor_baro", "system_power"):
        text = shell.get(f"listener {topic} -n 1", "")
        age = re.search(r"timestamp:\s*\d+\s*\(([0-9.]+) seconds ago\)", text)
        if not age or float(age.group(1)) > 2:
            issues.append(f"{topic}: no fresh published sample")
        if topic != "system_power":
            errors = re.search(r"error_count:\s*(\d+)", text)
            if not errors or int(errors.group(1)) != 0:
                issues.append(f"{topic}: error counter missing or nonzero")
            fields = ("pressure",) if topic == "sensor_baro" else ("x", "y", "z")
            for field in fields:
                value = re.search(rf"^\s*{field}:\s*(\S+)", text, re.MULTILINE)
                try:
                    valid = value is not None and math.isfinite(float(value.group(1)))
                except ValueError:
                    valid = False
                if not valid:
                    issues.append(f"{topic}: invalid {field} measurement")
    if not re.search(r"^\s*(?:.*\s+on\s+)?/fs/microsd\s+type\s+\S+", shell.get("mount", ""), re.MULTILINE):
        issues.append("microSD mount not confirmed; parameters/logs may not persist")
    if not re.search(r"pre_flight_checks_pass:\s*True\b", shell.get("listener vehicle_status -n 1", "")):
        issues.append("PX4 preflight checks are not passing")
    for item in report.get("status_text", []):
        if item["severity"] <= 4 and item["text"].startswith("Preflight Fail:"):
            issues.append(item["text"])
    return list(dict.fromkeys(issues))


def assess_rc_osd(report):
    """Check live receiver data and OSD transport; display visibility is manual."""
    issues = []
    shell = report.get("shell", {})
    for topic in ("input_rc", "manual_control_setpoint"):
        text = shell.get(f"listener {topic} -n 1", "")
        # RC publishers may continue reporting a LOST signal; freshness alone
        # is insufficient. Also inspect the protocol and mapped-input flags.
        age = re.search(r"timestamp:\s*\d+\s*\(([0-9.]+) seconds ago\)", text)
        if not age or float(age.group(1)) > 1:
            issues.append(f"{topic}: no fresh sample")
        if topic == "input_rc":
            published = re.search(r"^\s*timestamp:\s*(\d+)", text, re.MULTILINE)
            last_signal = re.search(r"^\s*timestamp_last_signal:\s*(\d+)", text, re.MULTILINE)
            if (not published or not last_signal or int(last_signal.group(1)) == 0
                    or not 0 <= int(published.group(1)) - int(last_signal.group(1)) <= 1_000_000):
                issues.append("RC last valid signal is stale or unavailable")
            channels = re.search(r"channel_count:\s*(\d+)", text)
            if not channels or int(channels.group(1)) < 6:
                issues.append("RC needs at least six channels for Hammer sticks/arm/mode")
            for flag in ("rc_lost", "rc_failsafe"):
                if not re.search(rf"\b{flag}:\s*False\b", text):
                    issues.append(f"RC {flag} is set or unavailable")
        else:
            if not re.search(r"\bvalid:\s*True\b", text):
                issues.append("Mapped RC input is not valid")
            if not re.search(r"\bdata_source:\s*1\b", text):
                issues.append("Manual-control source is not the RC receiver")
            for field in ("roll", "pitch", "yaw", "throttle"):
                value = re.search(rf"^\s*{field}:\s*(\S+)", text, re.MULTILINE)
                try:
                    valid = value is not None and math.isfinite(float(value.group(1))) and abs(float(value.group(1))) <= 1.01
                except ValueError:
                    valid = False
                if not valid:
                    issues.append(f"Mapped RC {field} is invalid")
    osd = shell.get("atxxxx status", "")
    frames = re.search(r"frames sent:\s*(\d+), transfer failures:\s*(\d+)", osd)
    if "Vector (write-only" not in osd or not frames or int(frames.group(1)) == 0 or int(frames.group(2)) != 0:
        issues.append("Vector OSD driver is not sending frames cleanly")
    return issues


def connect_health(deps, requested, usb_path, timeout):
    """Retry transient bootloader ports/disconnects while USB re-enumerates."""
    serial, list_ports, mav = deps
    end = time.monotonic() + timeout
    last_error = "Vector USB serial port did not appear"
    while time.monotonic() < end:
        port = select_port(list_ports.comports(), requested, usb_path)
        if port:
            health = None
            try:
                health = HealthLink(port, serial, mav)
                health.telemetry(heartbeat_timeout=min(10, max(0.1, end - time.monotonic())))
                return health
            except (OSError, FlashError) as error:
                last_error = str(error)
                if health:
                    try:
                        health.close()
                    except OSError:
                        pass
        time.sleep(0.5)
    raise FlashError(f"PX4 connection timed out: {last_error}. Reconnect normally on the same USB connector.")


def check_health(session, requested, expected_airframe, deps, expected_git=None, usb_path=None, timeout=60, peripherals=False):
    _, _, mav = deps
    session.state("waiting_for_px4")
    health = connect_health(deps, requested, usb_path, timeout)
    try:
        session.state("checking_health", serial_port=health.report["port"])
        require(not (health.report["heartbeat"]["base_mode"] & mav.MAV_MODE_FLAG_SAFETY_ARMED),
                "Controller is armed; stopping bench checks without issuing actuator commands")
        health.shell("")
        for command in ("ver all", "icm45686 status", "bmp388 status", "mount",
                        "listener sensor_accel -n 1", "listener sensor_gyro -n 1",
                        "listener sensor_baro -n 1", "listener system_power -n 1",
                        "listener vehicle_status -n 1", "dmesg"):
            health.report["shell"][command] = health.shell(command)
        if peripherals:
            for command in ("rc_input status", "atxxxx status", "vtx status", "vtxtable status",
                            "listener input_rc -n 1", "listener manual_control_setpoint -n 1",
                            "listener manual_control_switches -n 1", "listener vehicle_magnetometer -n 1",
                            "listener vehicle_gps_position -n 1"):
                health.report["shell"][command] = health.shell(command)
        issues = assess_health(health.report, expected_airframe, expected_git)
        if peripherals:
            issues.extend(assess_rc_osd(health.report))
            health.report["osd_visual_verification"] = "required; write-only SPI cannot confirm visible overlay"
            print("OSD: visually verify the overlay, moving sticks, and RC LOST on transmitter loss.")
        health.report["issues"] = issues
        session.state("boot_verified_with_health_issues" if issues else "bench_checks_passed", health_issues=issues)
        print(f"PX4 airframe: {health.report['sys_autostart']}")
        for issue in issues:
            print(f"HEALTH: {issue}")
        if not issues:
            print("Basic boot and sensor checks passed. This does not certify flight readiness.")
        return 3 if issues else 0
    finally:
        try:
            session.save("health.json", (json.dumps(health.report, indent=2) + "\n").encode())
            session.save("health-shell.log", "\n".join(health.report["shell"].values()).encode())
            session.state(session.manifest["status"])
        finally:
            health.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootloader", type=Path, default=ROOT / "build/vector_h743_bootloader/vector_h743_bootloader.bin",
                        help="Bootloader .bin with matching adjacent .px4 metadata, or .px4 package")
    parser.add_argument("--firmware", type=Path, default=ROOT / "build/vector_h743_default/vector_h743_default.px4",
                        help="PX4 .px4 package, or .bin with a matching adjacent .px4 package")
    parser.add_argument("--dfu-serial", help="Select a specific STM32 ROM DFU serial number")
    parser.add_argument("--port", help="Application serial port; default: detect on the selected DFU USB connector")
    parser.add_argument("--backup-dir", type=Path, default=Path.home() / "Documents/flight-controller-backups")
    parser.add_argument("--expected-airframe", type=int, default=4091)
    parser.add_argument("--reboot-timeout", type=float, default=60)
    parser.add_argument("--check-only", action="store_true", help="Only query a running PX4 controller; no DFU or flash operations")
    parser.add_argument("--peripherals", action="store_true",
                        help="Also check live RC and Vector OSD transport; capture compass/GNSS/VTX diagnostics")
    args = parser.parse_args(argv)
    if not math.isfinite(args.reboot_timeout) or args.reboot_timeout <= 0:
        parser.error("--reboot-timeout must be positive")
    session = None
    try:
        deps = dependencies()  # Check health dependencies before making any firmware change.
        device = None
        if not args.check_only:
            boot, app, metadata = load_images(args.bootloader, args.firmware)
            executable = shutil.which("dfu-util")
            require(executable is not None, "dfu-util is not installed")
            listing = subprocess.run([executable, "-d", DFU_ID, "--list"], capture_output=True,
                                     text=True, timeout=15, check=True)
            device = parse_device(listing.stdout, args.dfu_serial)
            print(f"DFU device {device.serial} on USB {device.path}; 2 MiB flash.")
            print(f"Bootloader: {args.bootloader}\nApplication: {args.firmware}")
            print("Close QGroundControl. Confirm this is the Vector H743, USB power only,\n"
                  "flight battery disconnected and propellers removed.")
            confirmation = input(f"Type FLASH {device.serial} to back up and program: ").strip()
            require(confirmation == f"FLASH {device.serial}", "Cancelled before any backup or write")
        label = "health" if args.check_only else re.sub(r"[^A-Za-z0-9_.-]", "_", device.serial)
        directory = args.backup_dir.expanduser() / f"vector-H743-{label}-{datetime.now(timezone.utc):%Y%m%d-%H%M%S-%f}"
        session = Session(directory)
        print(f"Session files: {directory}", flush=True)
        if device:
            session.state("device_selected", device=vars(device), firmware=metadata,
                          flash_base=hex(FLASH_BASE), application_base=hex(APP_BASE))
            program(Dfu(executable, device, session), session, boot, app)
            input("Unplug/reconnect USB normally on the SAME connector, without holding BOOT/RESET.\n"
                  "Keep the flight battery disconnected. Press Enter when reconnected: ")
        return check_health(session, args.port, args.expected_airframe, deps,
                            metadata["application"]["git_hash"] if device else None,
                            usb_path=device.path if device else None, timeout=args.reboot_timeout, peripherals=args.peripherals)
    except (Exception, KeyboardInterrupt) as error:
        message = str(error) or "Interrupted or input closed"
        if session:
            try:
                session.state("stopped", failed_at=session.manifest["status"], error=message)
            except OSError:
                pass
            print(f"Stopped: {message}\nPreserved session files: {session.path}", file=sys.stderr)
        else:
            print(f"Stopped before firmware writes: {message}", file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1


if __name__ == "__main__":
    sys.exit(main())
