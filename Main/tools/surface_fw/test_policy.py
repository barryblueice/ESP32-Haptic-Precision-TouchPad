"""Test the actual C policy against the SAM CSV and a recording BSP (no board).

Run directly, or through verify.py --self-test. Requires host Clang; the Windows
DLL uses no CRT, SDK installation, or external Surface checkout.
"""
import csv
import ctypes
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFERENCE = HERE / "policy_reference"


class Pair(ctypes.Structure):
    _fields_ = [("enabled", ctypes.c_bool), ("press_index", ctypes.c_uint8),
                ("release_index", ctypes.c_uint8)]


def verify_policy_reference():
    manifest = json.loads((REFERENCE / "manifest.json").read_text(encoding="utf-8"))
    for name, expected in manifest["files"].items():
        actual = hashlib.sha256((REFERENCE / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"Policy reference hash mismatch: {name}")
    with (REFERENCE / "setting_to_indices.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if [int(row["setting"]) for row in rows] != list(range(256)):
        raise ValueError("Policy reference must cover every byte input exactly once")
    # Match raw SAM register packets to this project's driver definitions.
    packets = manifest["register_packets"]
    spec = (ROOT / "main/I2C/SUB_DEV/mcu-drivers/cs40l25/cs40l25_spec.h").read_text()
    match = re.search(r"#define\s+DSP_VIRTUAL1_MBOX_DSP_VIRTUAL1_MBOX_1_REG\s+\((0x[0-9A-Fa-f]+)\)", spec)
    if match is None or int(match[1], 16) != int(packets["0x0008e7e8"][:8], 16):
        raise ValueError("SAM trigger packet does not match the driver's MBOX1 address")
    fw_reference = json.loads((HERE / "reference.json").read_text())
    for packet, symbol in (("0x0008e7f0", "13"), ("0x0008e7f8", "14")):
        if int(packets[packet][:8], 16) != fw_reference["symbols"][symbol]:
            raise ValueError("SAM press/release packet differs from target firmware symbols")
    return rows


def run_policy_tests():
    rows = verify_policy_reference()
    clang = shutil.which("clang")
    if clang is None:
        raise RuntimeError("Clang is required to execute the actual C policy tests")
    with tempfile.TemporaryDirectory(prefix="surface_policy_") as temporary:
        library_path = Path(temporary) / ("policy.dll" if sys.platform == "win32" else "policy.so")
        command = [clang, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
        if sys.platform == "win32":
            command += ["-nostdlib", "-fuse-ld=lld", "-fno-stack-protector",
                        "-Xlinker", "/noentry",
                        "-Xlinker", "/export:surface_haptic_resolve",
                        "-Xlinker", "/export:surface_haptic_play_event"]
        else:
            command += ["-fPIC"]
        command += ["-I", str(ROOT / "main"),
                    str(ROOT / "main/I2C/SUB_DEV/surface_haptic_policy.c"),
                    str(HERE / "policy_host_stub.c"), "-o", str(library_path)]
        try:
            subprocess.run(command, check=True, capture_output=True, text=True)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(f"Host C build failed:\n{error.stdout}\n{error.stderr}") from error
        lib = ctypes.CDLL(str(library_path))
        lib.surface_haptic_resolve.argtypes = [ctypes.c_uint8, ctypes.POINTER(Pair)]
        lib.surface_haptic_resolve.restype = ctypes.c_bool
        lib.surface_haptic_play_event.argtypes = [ctypes.POINTER(Pair), ctypes.c_bool]
        lib.surface_haptic_play_event.restype = ctypes.c_uint32
        lib.policy_test_reset.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        lib.policy_test_reset.restype = None
        lib.policy_test_field.argtypes = [ctypes.c_uint]
        lib.policy_test_field.restype = ctypes.c_uint32
        lib.policy_test_ok.restype = lib.policy_test_fail.restype = ctypes.c_uint32
        ok, fail = lib.policy_test_ok(), lib.policy_test_fail()

        class PolicyTests(unittest.TestCase):
            def setUp(self):
                lib.policy_test_reset(ok, ok)

            def fields(self):
                return [lib.policy_test_field(i) for i in range(10)]

            def test_all_256_settings_against_sam(self):
                for row in rows:
                    setting = int(row["setting"])
                    for enabled in (False, True):
                        with self.subTest(setting=setting, initial_enabled=enabled):
                            pair = Pair(enabled, 0xA5, 0x5A)
                            before = bytes(pair)
                            valid = lib.surface_haptic_resolve(setting, ctypes.byref(pair))
                            self.assertEqual(valid, row["status"] == "0x80000000")
                            if valid:
                                self.assertEqual((pair.press_index, pair.release_index),
                                                 (int(row["press_index"]), int(row["release_index"])))
                                self.assertEqual(pair.enabled, setting != 0)
                            else:
                                self.assertEqual(bytes(pair), before)
                self.assertEqual(self.fields(), [0] * 10)

            def test_null_arguments(self):
                self.assertFalse(lib.surface_haptic_resolve(63, None))
                self.assertEqual(lib.surface_haptic_play_event(None, False), fail)
                self.assertEqual(self.fields(), [0] * 10)

            def test_disabled_pair_never_writes(self):
                pair = Pair()
                self.assertTrue(lib.surface_haptic_resolve(0, ctypes.byref(pair)))
                for release in (False, True):
                    self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), ok)
                self.assertEqual(self.fields(), [0] * 10)

            def test_all_enabled_settings_both_events(self):
                for row in rows[1:101]:
                    pair = Pair()
                    self.assertTrue(lib.surface_haptic_resolve(int(row["setting"]), ctypes.byref(pair)))
                    for release in (False, True):
                        with self.subTest(setting=row["setting"], release=release):
                            lib.policy_test_reset(ok, ok)
                            self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), ok)
                            press, up = int(row["press_index"]), int(row["release_index"])
                            self.assertEqual(self.fields(), [1, 1, press, up, 0, 0, 0,
                                                             up if release else press, 0, 12])

            def test_either_invalid_index_rejected_before_io(self):
                for invalid in range(78, 256):
                    for pair in (Pair(True, invalid, 13), Pair(True, 17, invalid)):
                        for release in (False, True):
                            self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), fail)
                self.assertEqual(self.fields(), [0] * 10)

            def test_valid_index_boundaries(self):
                pair = Pair(True, 0, 77)
                for release, index in ((False, 0), (True, 77)):
                    lib.policy_test_reset(ok, ok)
                    self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), ok)
                    self.assertEqual(self.fields(), [1, 1, 0, 77, 0, 0, 0, index, 0, 12])

            def test_mapping_failure_stops_trigger(self):
                pair = Pair(True, 21, 15)
                for status in (fail, 0x12345678):
                    for release in (False, True):
                        lib.policy_test_reset(status, ok)
                        self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), status)
                        self.assertEqual(self.fields(), [1, 0, 21, 15, 0, 0, 0, 0, 0, 1])

            def test_trigger_failure_propagates_without_retry(self):
                pair = Pair(True, 36, 29)
                for status in (fail, 0x87654321):
                    for release in (False, True):
                        lib.policy_test_reset(ok, status)
                        self.assertEqual(lib.surface_haptic_play_event(ctypes.byref(pair), release), status)
                        self.assertEqual(self.fields(), [1, 1, 36, 29, 0, 0, 0,
                                                         29 if release else 36, 0, 12])

        try:
            result = unittest.TextTestRunner(verbosity=2).run(
                unittest.defaultTestLoader.loadTestsFromTestCase(PolicyTests))
            if not result.wasSuccessful():
                raise RuntimeError("C policy tests failed")
            return {"test_methods": result.testsRun, "settings_compared": 256,
                    "actual_c_module": True, "bsp": "recording stub", "passed": True}
        finally:
            if sys.platform == "win32":
                import _ctypes
                _ctypes.FreeLibrary(lib._handle)


if __name__ == "__main__":
    print(json.dumps(run_policy_tests(), indent=2))
