#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware-free failure-path tests for the Vector H743 flashing workflow."""

import base64
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import zlib

SCRIPT = Path(__file__).resolve().parents[1] / "Tools/vector_h743_flash.py"
spec = importlib.util.spec_from_file_location("vector_flash", SCRIPT)
flash = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = flash
spec.loader.exec_module(flash)

LISTING = '''Found DFU: [0483:df11] ver=0200, devnum=35, cfg=1, intf=0, path="3-1", alt=1, name="@Option Bytes   /0x5200201C/01*128 e", serial="200364500000"
Found DFU: [0483:df11] ver=0200, devnum=35, cfg=1, intf=0, path="3-1", alt=0, name="@Internal Flash   /0x08000000/16*128Kg", serial="200364500000"
'''


def image(base, size=256):
    return struct.pack("<II", 0x24001D9E, base + 0x81) + b"\x55" * (size - 8)


class FakeDfu:
    """Emulate sector erasure and flash reads, with injected transport faults."""
    def __init__(self, session, fault=None):
        self.session = session
        self.device = flash.parse_device(LISTING)
        self.memory = bytearray(bytes(range(256)) * (flash.FLASH_SIZE // 256))
        self.original = bytes(self.memory)
        self.options = bytes(range(flash.OPTION_SIZE))
        self.fault = fault
        self.writes = []

    def read(self, address, length, name, options=False):
        if self.fault == name + ":fail":
            raise flash.FlashError("Read/USB transport failure")
        if options:
            data = self.options[:length]
        else:
            offset = address - flash.FLASH_BASE
            data = bytes(self.memory[offset:offset + length])
        if self.fault == name + ":short":
            data = data[:-1]
        if self.fault == name + ":corrupt":
            data = bytes([data[0] ^ 1]) + data[1:]
        return self.session.save(name, data)

    def write(self, address, name):
        self.writes.append(name)
        if self.fault == name + ":write_fail":
            raise flash.FlashError("Programming failure")
        data = (self.session.path / name).read_bytes()
        start = address - flash.FLASH_BASE
        end = ((start + len(data) + flash.SECTOR_SIZE - 1) // flash.SECTOR_SIZE) * flash.SECTOR_SIZE
        self.memory[start:end] = b"\xff" * (end - start)
        self.memory[start:start + len(data)] = data


class FlashGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.counter = 0

    def setup_session(self, fault=None):
        self.counter += 1
        session = flash.Session(Path(self.tmp.name) / str(self.counter))
        return session, FakeDfu(session, fault)

    def test_every_backup_failure_prevents_all_writes(self):
        faults = ["original-flash-read1.bin:fail", "original-flash-read2.bin:fail",
                  "original-flash-read1.bin:short", "original-flash-read2.bin:short",
                  "original-flash-read2.bin:corrupt", "original-option-bytes.bin:fail",
                  "original-option-bytes.bin:short"]
        for fault in faults:
            with self.subTest(fault=fault):
                session, dfu = self.setup_session(fault)
                with self.assertRaises(flash.FlashError):
                    flash.program(dfu, session, image(flash.FLASH_BASE), image(flash.APP_BASE))
                self.assertEqual(dfu.writes, [])

    def test_backup_manifest_fsync_failure_prevents_all_writes(self):
        session, dfu = self.setup_session()
        save_state = session.state

        def fail_verified(status, **values):
            if status == "backup_verified":
                raise OSError("Disk full or fsync failed")
            save_state(status, **values)

        with patch.object(session, "state", side_effect=fail_verified):
            with self.assertRaises(OSError):
                flash.program(dfu, session, image(flash.FLASH_BASE), image(flash.APP_BASE))
        self.assertEqual(dfu.writes, [])

    def test_bootloader_backup_file_failure_prevents_all_writes(self):
        session, dfu = self.setup_session()
        save = session.save

        def fail_boot_backup(name, data):
            if name == "original-bootloader.bin":
                raise OSError("Backup file could not be saved")
            return save(name, data)

        with patch.object(session, "save", side_effect=fail_boot_backup):
            with self.assertRaises(OSError):
                flash.program(dfu, session, image(flash.FLASH_BASE), image(flash.APP_BASE))
        self.assertEqual(dfu.writes, [])

    def test_bootloader_failure_prevents_application_write(self):
        for fault in ["px4-bootloader.bin:write_fail", "bootloader-readback.bin:corrupt",
                      "bootloader-readback.bin:fail"]:
            with self.subTest(fault=fault):
                session, dfu = self.setup_session(fault)
                with self.assertRaises(flash.FlashError):
                    flash.program(dfu, session, image(flash.FLASH_BASE), image(flash.APP_BASE))
                self.assertEqual(dfu.writes, ["px4-bootloader.bin"])

    def test_post_write_mismatch_never_reports_success(self):
        for fault in ["px4-application.bin:write_fail", "px4-full-flash-readback.bin:corrupt",
                      "option-bytes-readback.bin:corrupt"]:
            with self.subTest(fault=fault):
                session, dfu = self.setup_session(fault)
                with self.assertRaises(flash.FlashError):
                    flash.program(dfu, session, image(flash.FLASH_BASE), image(flash.APP_BASE))
                self.assertNotEqual(session.manifest["status"], "images_verified_awaiting_reboot")

    def test_success_preserves_recovery_files_and_untouched_sectors(self):
        session, dfu = self.setup_session()
        app = image(flash.APP_BASE, flash.SECTOR_SIZE + 8)
        flash.program(dfu, session, image(flash.FLASH_BASE), app)
        self.assertEqual(dfu.writes, ["px4-bootloader.bin", "px4-application.bin"])
        self.assertEqual((session.path / "original-bootloader.bin").read_bytes(), dfu.original[:flash.SECTOR_SIZE])
        self.assertEqual((session.path / "original-flash-read1.bin").read_bytes(), dfu.original)
        self.assertEqual((session.path / "original-flash-read2.bin").read_bytes(), dfu.original)
        self.assertEqual(session.manifest["status"], "images_verified_awaiting_reboot")
        self.assertEqual(dfu.memory[3 * flash.SECTOR_SIZE:], dfu.original[3 * flash.SECTOR_SIZE:])

    def test_no_existing_backup_directory_can_be_reused(self):
        session, _ = self.setup_session()
        with self.assertRaises(FileExistsError):
            flash.Session(session.path)


class SelectionAndImageTests(unittest.TestCase):
    def test_no_device_and_wrong_layout_are_rejected(self):
        for listing in ["", LISTING.replace("16*128Kg", "08*128Kg")]:
            with self.assertRaises(flash.FlashError):
                flash.parse_device(listing)

    def test_ambiguous_devices_require_selection(self):
        second = LISTING.replace("200364500000", "OTHER").replace("3-1", "3-2")
        with self.assertRaises(flash.FlashError):
            flash.parse_device(LISTING + second)
        self.assertEqual(flash.parse_device(LISTING + second, "OTHER").path, "3-2")

    def test_invalid_and_cross_address_vectors_are_rejected(self):
        for data in [image(flash.APP_BASE), b"\xff" * 256, image(flash.FLASH_BASE, flash.SECTOR_SIZE + 1)]:
            with self.assertRaises(flash.FlashError):
                flash.validate_image(data, flash.FLASH_BASE, flash.SECTOR_SIZE)

    def test_package_id_length_and_bin_match_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "image.bin"
            data = image(flash.APP_BASE)
            path.write_bytes(data)
            package = {"magic": "PX4FWv1", "board_id": flash.BOARD_ID, "image_size": len(data),
                       "image": base64.b64encode(zlib.compress(data)).decode()}
            for change in [{"board_id": 9}, {"image_size": len(data) + 1}, {"magic": "other"}]:
                path.with_suffix(".px4").write_text(json.dumps(dict(package, **change)))
                with self.assertRaises(flash.FlashError):
                    flash.load_payload(path, flash.APP_BASE, flash.APP_SIZE)
            path.with_suffix(".px4").write_text(json.dumps(package))
            self.assertEqual(flash.load_payload(path, flash.APP_BASE, flash.APP_SIZE)[0], data)
            path.write_bytes(data[:-1] + b"\x00")
            with self.assertRaises(flash.FlashError):
                flash.load_payload(path, flash.APP_BASE, flash.APP_SIZE)

    def test_wrong_usb_connector_is_not_used_after_reboot(self):
        ports = [SimpleNamespace(vid=0x1209, pid=0x5741, location="3-2:1.0", device="/dev/ttyACM0")]
        self.assertIsNone(flash.select_port(ports, usb_path="3-1"))
        self.assertIsNone(flash.select_port(ports, requested="/dev/ttyACM0", usb_path="3-1"))
        self.assertEqual(flash.select_port(ports, usb_path="3-2"), "/dev/ttyACM0")


class HealthTests(unittest.TestCase):
    def healthy_report(self):
        report = {"heartbeat": {"autopilot": 12, "base_mode": 29}, "autopilot_version": {},
                  "sys_autostart": 4091, "status_text": [], "shell": {
                      "ver all": "HW arch: VECTOR_H743\nPX4 git-hash: abcdef\n",
                      "mount": "/dev/mmcsd0 on /fs/microsd type vfat\n",
                      "listener vehicle_status -n 1": "pre_flight_checks_pass: True\n"}}
        for topic in ["sensor_accel", "sensor_gyro", "sensor_baro", "system_power"]:
            report["shell"][f"listener {topic} -n 1"] = (
                "timestamp: 1234567 (0.002 seconds ago)\nerror_count: 0\n"
                "x: 0.1\ny: 0.2\nz: -9.8\npressure: 90000\n")
        return report

    def test_first_boot_warnings_are_not_a_healthy_result(self):
        report = self.healthy_report()
        report["shell"]["listener system_power -n 1"] = "never published"
        report["shell"]["mount"] = "/ on / type rootfs"
        report["status_text"] = [{"severity": 2, "text": "Preflight Fail: Accel 0 uncalibrated"}]
        issues = flash.assess_health(report, 4091)
        self.assertTrue(any("system_power" in text for text in issues))
        self.assertTrue(any("microSD" in text for text in issues))
        self.assertTrue(any("uncalibrated" in text for text in issues))

    def test_stale_nonfinite_and_armed_states_fail(self):
        original = self.healthy_report()
        for topic, replacement in [("sensor_accel", "timestamp: 1234567 (20.0 seconds ago)"),
                                   ("sensor_gyro", "timestamp: 1234567 (0.001 seconds ago)\nerror_count: 0\nx: nan")]:
            report = copy.deepcopy(original)
            report["shell"][f"listener {topic} -n 1"] = replacement
            self.assertTrue(flash.assess_health(report, 4091))
        original["heartbeat"]["base_mode"] |= 128
        self.assertTrue(any("armed" in text for text in flash.assess_health(original, 4091)))

    def test_healthy_report_and_firmware_identity(self):
        self.assertEqual(flash.assess_health(self.healthy_report(), 4091, "abcdef"), [])
        self.assertTrue(flash.assess_health(self.healthy_report(), 4091, "different"))

    def test_usb_reenumeration_retries_connection(self):
        first, second = Mock(), Mock()
        first.telemetry.side_effect = OSError("USB device disconnected")
        ports = SimpleNamespace(comports=lambda: [SimpleNamespace(
            vid=0x1209, pid=0x5741, location="3-1", device="/dev/ttyACM0")])
        with patch.object(flash, "HealthLink", side_effect=[first, second]), \
             patch.object(flash.time, "sleep"):
            result = flash.connect_health((None, ports, None), None, "3-1", 2)
        self.assertIs(result, second)
        first.close.assert_called_once()
        second.close.assert_not_called()

    def test_px4_integer_parameters_are_decoded_bytewise(self):
        health = flash.HealthLink.__new__(flash.HealthLink)
        message = Mock()
        message.get_srcSystem.return_value = 1
        message.get_srcComponent.return_value = 1
        message.get_type.return_value = "PARAM_VALUE"
        message.to_dict.return_value = {}
        message.param_id = "SYS_AUTOSTART"
        message.param_type = 6
        message.param_value = struct.unpack("<f", struct.pack("<i", 4091))[0]
        health.mav = SimpleNamespace(MAV_PARAM_TYPE_INT32=6)
        health.target = (1, 1)
        health.next_heartbeat = float("inf")
        health.port = Mock(in_waiting=1)
        health.port.read.return_value = b"data"
        health.link = Mock()
        health.link.parse_buffer.return_value = [message]
        health.report = {}
        health.receive()
        self.assertEqual(health.report["sys_autostart"], 4091)

    def test_check_only_cannot_invoke_dfu_or_image_loading(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(flash, "dependencies", return_value=(None, None, None)), \
                 patch.object(flash, "check_health", return_value=3), \
                 patch.object(flash, "load_images", side_effect=AssertionError("Images not needed")), \
                 patch.object(flash.subprocess, "run", side_effect=AssertionError("No DFU allowed")), \
                 patch("builtins.input", side_effect=AssertionError("No write confirmation allowed")):
                self.assertEqual(flash.main(["--check-only", "--backup-dir", temporary]), 3)


class MountOutputChecks(unittest.TestCase):
    def test_nuttx_and_linux_style_mount_output(self):
        for mounted in ("  /fs/microsd type vfat\n", "/dev/mmcsd0 on /fs/microsd type vfat\n"):
            issues = flash.assess_health({"shell": {"mount": mounted}}, 4091)
            self.assertFalse(any("microSD mount" in x for x in issues), mounted)

    def test_unmounted_or_other_path_is_not_accepted(self):
        for mounted in ("", "  /fs/microsd_backup type vfat\n", "mount /fs/microsd\nERROR: failed\n"):
            issues = flash.assess_health({"shell": {"mount": mounted}}, 4091)
            self.assertTrue(any("microSD mount" in x for x in issues), mounted)


class PeripheralChecks(unittest.TestCase):
    def good(self):
        return {"shell": {
            "listener input_rc -n 1": "timestamp: 123 (0.01 seconds ago)\ntimestamp_last_signal: 120\nchannel_count: 16\nrc_lost: False\nrc_failsafe: False\n",
            "listener manual_control_setpoint -n 1": "timestamp: 123 (0.01 seconds ago)\nvalid: True\ndata_source: 1\nroll: 0\npitch: 0\nyaw: 0\nthrottle: -1\n",
            "atxxxx status": "backend: Vector (write-only, display unverified), video: PAL\nframes sent: 23, transfer failures: 0"
        }}

    def test_live_rc_and_transport(self):
        self.assertEqual(flash.assess_rc_osd(self.good()), [])

    def test_lost_receiver_rejected_even_with_fresh_topic(self):
        report = self.good()
        key = "listener input_rc -n 1"
        report["shell"][key] = report["shell"][key].replace("rc_lost: False", "rc_lost: True")
        self.assertTrue(any("rc_lost" in item for item in flash.assess_rc_osd(report)))

    def test_recent_publication_does_not_hide_old_signal(self):
        report = self.good()
        key = "listener input_rc -n 1"
        report["shell"][key] = report["shell"][key].replace("timestamp: 123", "timestamp: 2000123")
        self.assertTrue(any("last valid signal" in item for item in flash.assess_rc_osd(report)))

    def test_stale_or_invalid_manual_input(self):
        for replacement in ("valid: False", "data_source: 2", "pitch: nan", "pitch: 3"):
            report = self.good()
            key = "listener manual_control_setpoint -n 1"
            original = "valid: True" if replacement.startswith("valid") else "data_source: 1" if replacement.startswith("data_source") else "pitch: 0"
            report["shell"][key] = report["shell"][key].replace(original, replacement)
            self.assertTrue(flash.assess_rc_osd(report), replacement)
        report = self.good()
        report["shell"][key] = report["shell"][key].replace("0.01 seconds", "3.0 seconds")
        self.assertTrue(flash.assess_rc_osd(report))

    def test_osd_missing_frames_or_transfer_failures(self):
        for status in ("", "backend: Vector (write-only), frames sent: 0, transfer failures: 0",
                       "backend: Vector (write-only), frames sent: 10, transfer failures: 2"):
            report = self.good()
            report["shell"]["atxxxx status"] = status
            self.assertTrue(flash.assess_rc_osd(report))


if __name__ == "__main__":
    unittest.main()
