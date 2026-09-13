"""Execute production event/NVS C code; inspect the actual USB/BLE HID items."""
import ctypes as C
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from test_policy import Pair

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SUB = ROOT / "main/I2C/SUB_DEV"


class Event(C.Structure):
    _fields_ = [("click_id", C.c_uint32), ("generation", C.c_uint32),
                ("time_ms", C.c_uint32), ("pair", Pair),
                ("setting", C.c_uint8), ("release", C.c_bool)]


class Runtime(C.Structure):
    _fields_ = [("state", C.c_int), ("events", Event * 8),
                ("head", C.c_uint), ("count", C.c_uint),
                ("generation", C.c_uint32), ("next_id", C.c_uint32),
                ("input_id", C.c_uint32), ("active_id", C.c_uint32), ("dropped", C.c_uint32),
                ("down", C.c_bool), ("blocked", C.c_bool), ("pair", Pair), ("setting", C.c_uint8)]


def verify_interfaces():
    """Parse HID items, not occurrences of 0x42 (also a digitizer usage)."""
    for path, array in [("USB/usb_descriptor.c", "ptp_hid_report_descriptor"),
                        ("BLE/ble_hid_descriptor.c", "ble_ptp_hid_report_descriptor"),
                        ("BLE/ble_hid_descriptor.c", "ble_mouse_hid_report_descriptor")]:
        text = (ROOT / "main" / path).read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        constants = {}
        for source in [text, (ROOT / "main/BLE/BLE_bluedroid.h").read_text()]:
            for name, value in re.findall(r"^#define[ \t]+(\w+)[ \t]+([^\n]+)", source, re.M):
                if re.fullmatch(r"[\s,0-9a-fA-FxX]+", value):
                    constants.setdefault(name, value.strip())
        body = re.search(r"\b" + array + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)[1]
        for name, value in constants.items():
            body = re.sub(r"\b" + name + r"\b", value, body)
        data = bytes(int(v.strip(), 0) for v in body.split(",") if v.strip())
        pos = depth = report = minimum = maximum = size = count = 0
        features = {}
        reports = set()
        while pos < len(data):
            prefix = data[pos]; pos += 1
            length = [0, 1, 2, 4][prefix & 3]
            assert pos + length <= len(data), (path, "truncated item")
            value = int.from_bytes(data[pos:pos + length], "little"); pos += length
            tag = prefix & 0xFC
            if tag == 0x84: report = value; reports.add(value)
            elif tag == 0x14: minimum = value
            elif tag == 0x24: maximum = value
            elif tag == 0x74: size = value
            elif tag == 0x94: count = value
            elif tag == 0xA0: depth += 1
            elif tag == 0xC0: depth -= 1; assert depth >= 0
            elif tag == 0xB0: features.setdefault(report, []).append((minimum, maximum, size * count))
        assert depth == 0, (path, "unbalanced collections")
        assert features[0x41] == [(0, 100, 8)], (path, features.get(0x41))
        assert not ({0x42, 0x43} & reports), (path, "manual reports remain")
    gatt = (ROOT / "main/BLE/ble_hid_device_le_prf.c").read_text()
    assert re.search(r"\[HIDD_LE_IDX_REPORT_HAPTIC_INTENSITY_VAL\]\s*=\s*\{\{ESP_GATT_RSP_BY_APP", gatt)
    assert "hid_dev_register_reports(7, hid_rpt_map)" in gatt
    assert "hid_dev_register_reports(3, hid_rpt_map)" in gatt
    enum_text = (ROOT / "main/BLE/hidd_le_prf_int.h").read_text()
    enum_indices = set(re.findall(r"\bHIDD_LE_IDX_\w+", enum_text)) - {"HIDD_LE_IDX_NB"}
    initialized = set(re.findall(r"\[(HIDD_LE_IDX_\w+)\]\s*=", gatt)) - {"HIDD_LE_IDX_NB"}
    assert enum_indices == initialized, ("GATT table has missing attributes", enum_indices - initialized)
    for path in ["BLE/ble_hid_device_le_prf.c", "BLE/hidd_le_prf_int.h", "BLE/BLE_bluedroid.h", "USB/usbhid.c"]:
        text = (ROOT / "main" / path).read_text()
        assert "HAPTIC_WAVEFORM" not in text and "HAPTIC_MANUAL_TRIGGER" not in text
    main = (ROOT / "main/main.c").read_text()
    assert main.index("ptp_haptic_click_intensity_load_from_nvs();") < main.index("touchpad_init();") < main.index("sub_dev_init();") < main.index("cs40l25_surface_init();") < main.index("irq_int_init();")
    assert "#if CONFIG_SURFACE_HAPTIC_TEST_MODE" in main
    kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
    assert re.search(r"config SURFACE_HAPTIC_TEST_MODE\s+bool[^\n]*\s+default n", kconfig)


def run_runtime_tests():
    exports = ["surface_runtime_" + n for n in ["state", "button", "pop", "cancel", "current"]]
    exports += ["ptp_haptic_click_intensity_" + n for n in ["get", "set", "set_report", "load_from_nvs"]]
    with tempfile.TemporaryDirectory(prefix="surface_runtime_") as temp:
        dll = Path(temp) / ("runtime.dll" if sys.platform == "win32" else "runtime.so")
        command = [shutil.which("clang") or "clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
        if sys.platform == "win32":
            command += ["-nostdlib", "-fuse-ld=lld", "-fno-stack-protector", "-Xlinker", "/noentry"]
            for name in exports: command += ["-Xlinker", "/export:" + name]
        else: command += ["-fPIC"]
        command += ["-I", str(HERE / "host_include"), "-I", str(ROOT / "main")]
        command += [str(SUB / ("surface_haptic_" + n + ".c")) for n in ["policy", "runtime", "settings"]]
        command += [str(HERE / "policy_host_stub.c"), str(HERE / "settings_host_stub.c"), "-o", str(dll)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stdout + result.stderr)
        lib = C.CDLL(str(dll))
        lib.surface_runtime_state.argtypes = [C.POINTER(Runtime), C.c_int]
        lib.surface_runtime_button.argtypes = [C.POINTER(Runtime), C.c_bool, C.c_uint8, C.c_uint32]
        lib.surface_runtime_pop.argtypes = [C.POINTER(Runtime), C.c_uint32, C.POINTER(Event)]
        lib.surface_runtime_pop.restype = C.c_bool
        lib.surface_runtime_current.argtypes = [C.POINTER(Runtime), C.POINTER(Event)]
        lib.surface_runtime_current.restype = C.c_bool
        lib.surface_runtime_cancel.argtypes = [C.POINTER(Runtime)]
        lib.ptp_haptic_click_intensity_set.argtypes = [C.c_uint8, C.c_bool]
        lib.ptp_haptic_click_intensity_get.restype = C.c_uint8
        lib.ptp_haptic_click_intensity_set_report.argtypes = [C.POINTER(C.c_uint8), C.c_size_t, C.c_bool]

        class IntegrationTests(unittest.TestCase):
            def setUp(self):
                self.r = Runtime()
                self.state(1)

            def state(self, state): lib.surface_runtime_state(C.byref(self.r), state)
            def button(self, down, setting=63, now=0): lib.surface_runtime_button(C.byref(self.r), down, setting, now)
            def cancel(self): lib.surface_runtime_cancel(C.byref(self.r))
            def pop(self, now=0):
                event = Event()
                return event if lib.surface_runtime_pop(C.byref(self.r), now, C.byref(event)) else None

            def test_press_hold_release_and_duplicate_frames(self):
                self.button(True); press = self.pop()
                self.assertEqual(press.pair.press_index, 21)
                for _ in range(100): self.button(True, now=5000)
                self.assertIsNone(self.pop(5000))
                self.button(False, now=10000); release = self.pop(10000)
                self.assertTrue(release.release)
                self.assertEqual((release.click_id, release.pair.release_index), (press.click_id, 15))
                self.button(False); self.assertIsNone(self.pop())

            def test_snapshot_and_next_click(self):
                self.button(True, 25); self.button(True, 100); self.button(False, 100)
                self.assertEqual(self.pop().pair.press_index, 17)
                self.assertEqual(self.pop().pair.release_index, 13)
                self.button(True, 100); self.assertEqual(self.pop().pair.press_index, 36)

            def test_rapid_clicks_order(self):
                for _ in range(4): self.button(True); self.button(False)
                events = [self.pop() for _ in range(8)]
                self.assertEqual([e.release for e in events], [False, True] * 4)
                self.assertEqual([e.click_id for e in events], [1, 1, 2, 2, 3, 3, 4, 4])

            def test_final_button_release_on_contact_change(self):
                # Parser submits false for no finger, multi-finger and tracked-contact change.
                for _reason in ["no finger", "multi-finger", "tracked contact changed"]:
                    self.button(True); self.pop(); self.button(False)
                    self.assertTrue(self.pop().release)
                    self.button(False); self.assertIsNone(self.pop())

            def test_disable_cancels_queued_and_held_release(self):
                self.button(True); self.cancel(); self.button(False, 0)
                self.assertIsNone(self.pop())
                self.button(True, 0); self.button(False, 0); self.assertIsNone(self.pop())
                self.button(True); self.pop(); self.cancel(); self.button(False, 100)
                self.assertIsNone(self.pop())
                self.button(True, 100); self.assertEqual(self.pop().pair.press_index, 36)

            def test_overflow_waits_for_release(self):
                for _ in range(4): self.button(True); self.button(False)
                self.button(True)
                self.assertEqual((self.r.count, self.r.dropped, self.r.blocked), (0, 1, True))
                self.button(True); self.assertIsNone(self.pop())
                self.button(False); self.assertIsNone(self.pop())
                self.button(True); self.assertIsNotNone(self.pop())

            def test_expiry_and_clock_wrap(self):
                self.button(True, now=10); self.assertIsNone(self.pop(111))
                self.button(False); self.assertIsNone(self.pop(111))
                self.button(True, now=0xfffffff0)
                self.assertIsNotNone(self.pop(0x10))
                self.button(False, now=0x10); self.assertIsNotNone(self.pop(0x74))

            def test_sleep_wake_cancel_and_no_replay(self):
                self.button(True); self.state(2); self.state(3)
                self.assertIsNone(self.pop())
                self.state(1); self.button(True); self.assertIsNone(self.pop())
                self.button(False); self.assertIsNone(self.pop())
                self.button(True); self.assertIsNotNone(self.pop())

            def test_generation_invalidates_taken_event_and_fault_is_sticky(self):
                self.button(True); event = self.pop(); self.cancel()
                self.assertFalse(lib.surface_runtime_current(C.byref(self.r), C.byref(event)))
                self.state(4)
                for state in [0, 1, 2, 3]: self.state(state); self.assertEqual(self.r.state, 4)
                self.button(False); self.button(True); self.assertIsNone(self.pop())

            def test_migration_all_legacy_and_invalid_values(self):
                for old, expected in [(0, 0), (1, 25), (2, 63), (3, 75), (4, 100), (-1, 63), (5, 63), (100, 63)]:
                    lib.settings_test_reset(0, 0, 1, old, 0)
                    lib.ptp_haptic_click_intensity_load_from_nvs()
                    self.assertEqual(lib.ptp_haptic_click_intensity_get(), expected)
                    self.assertEqual([lib.settings_test_field(i) for i in range(3)], [1, expected, old])

            def test_new_key_precedence_and_repair(self):
                for value in [0, 1, 4, 63, 100, -1, 101, 255]:
                    lib.settings_test_reset(1, value, 1, 4, 0)
                    lib.ptp_haptic_click_intensity_load_from_nvs()
                    valid = 0 <= value <= 100
                    self.assertEqual(lib.ptp_haptic_click_intensity_get(), value if valid else 63)
                    self.assertEqual(lib.settings_test_field(0), 0 if valid else 1)
                lib.settings_test_reset(0, 0, 0, 0, 0)
                lib.ptp_haptic_click_intensity_load_from_nvs()
                self.assertEqual(lib.ptp_haptic_click_intensity_get(), 63)

            def test_new_key_wrong_storage_type_is_repaired(self):
                lib.settings_test_reset(2, 0, 1, 4, 0)
                lib.ptp_haptic_click_intensity_load_from_nvs()
                self.assertEqual(lib.ptp_haptic_click_intensity_get(), 63)
                self.assertEqual([lib.settings_test_field(i) for i in range(3)], [1, 63, 4])

            def test_intensity_all_bytes_reject_without_mutation(self):
                lib.settings_test_reset(1, 63, 0, 0, 0)
                lib.ptp_haptic_click_intensity_load_from_nvs()
                for value in range(256):
                    before = lib.ptp_haptic_click_intensity_get()
                    writes = lib.settings_test_field(0)
                    status = lib.ptp_haptic_click_intensity_set(value, True)
                    self.assertEqual(status == 0, value <= 100)
                    self.assertEqual(lib.ptp_haptic_click_intensity_get(), value if value <= 100 else before)
                    self.assertEqual(lib.settings_test_field(0), writes + (value <= 100))
                self.assertGreater(lib.settings_test_field(3), 0)

            def test_report_format_rejected_without_writing(self):
                lib.settings_test_reset(1, 63, 0, 0, 0)
                lib.ptp_haptic_click_intensity_load_from_nvs()
                data = (C.c_uint8 * 2)(100, 25)
                for pointer, length in [(None, 1), (data, 0), (data, 2), (data, 256)]:
                    self.assertNotEqual(lib.ptp_haptic_click_intensity_set_report(pointer, length, True), 0)
                    self.assertEqual(lib.ptp_haptic_click_intensity_get(), 63)
                    self.assertEqual(lib.settings_test_field(0), 0)
                self.assertEqual(lib.ptp_haptic_click_intensity_set_report(data, 1, True), 0)
                self.assertEqual(lib.ptp_haptic_click_intensity_get(), 100)

            def test_storage_failure_preserves_running_setting(self):
                lib.settings_test_reset(1, 75, 0, 0, 1)
                lib.ptp_haptic_click_intensity_load_from_nvs()
                self.assertNotEqual(lib.ptp_haptic_click_intensity_set(0, True), 0)
                self.assertEqual(lib.ptp_haptic_click_intensity_get(), 75)
                self.assertEqual(lib.settings_test_field(3), 0)

            def test_descriptors_and_mode_binding(self): verify_interfaces()

        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(IntegrationTests))
        if sys.platform == "win32":
            kernel = C.WinDLL("kernel32", use_last_error=True)
            kernel.FreeLibrary.argtypes = [C.c_void_p]
            kernel.FreeLibrary(lib._handle)
        if not result.wasSuccessful(): raise ValueError("Integration host tests failed")
        return dict(passed=result.testsRun, actual_c_modules=["policy", "runtime", "settings"],
                    descriptors=["USB PTP", "BLE PTP", "BLE mouse"])


if __name__ == "__main__":
    print(run_runtime_tests())
