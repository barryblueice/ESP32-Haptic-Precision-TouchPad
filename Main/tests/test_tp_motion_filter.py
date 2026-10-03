"""Replay synthetic trajectories through the production C coordinate filter."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MotionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('TP_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp = tempfile.TemporaryDirectory(prefix='tp_motion_')
        wrapper = Path(cls.tmp.name) / 'wrapper.c'
        wrapper.write_text('''
#include "tp_motion_filter.h"
int _fltused = 0;
static tp_motion_filter_t slots[5];
__declspec(dllexport) void reset(unsigned slot) {
    volatile unsigned char *p = (volatile unsigned char *)&slots[slot];
    for (unsigned i = 0; i < sizeof(slots[slot]); ++i) p[i] = 0;
}
__declspec(dllexport) uint32_t step(unsigned slot, uint16_t x, uint16_t y, uint32_t ms) {
    uint16_t ox, oy;
    tp_motion_filter_update(&slots[slot], x, y, ms, &ox, &oy);
    return (uint32_t)ox | ((uint32_t)oy << 16);
}
__declspec(dllexport) uint32_t guarded(unsigned slot) {
    return (uint32_t)slots[slot].guarded_x | ((uint32_t)slots[slot].guarded_y << 16);
}
''')
        dll = Path(cls.tmp.name) / 'motion.dll'
        subprocess.run([compiler, '--target=x86_64-pc-windows-msvc', '-shared', '-nostdlib',
                        '-fuse-ld=lld', '-fno-stack-protector', '-Xlinker', '/noentry',
                        '-Wall', '-Wextra', '-Werror', '-O2',
                        '-I', str(ROOT / 'main/I2C/TP'), str(wrapper),
                        str(ROOT / 'main/I2C/TP/tp_motion_filter.c'), '-o', str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.lib.reset.argtypes = [C.c_uint]
        cls.lib.step.argtypes = [C.c_uint, C.c_uint16, C.c_uint16, C.c_uint32]
        cls.lib.step.restype = C.c_uint32
        cls.lib.guarded.argtypes = [C.c_uint]
        cls.lib.guarded.restype = C.c_uint32

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def setUp(self):
        for slot in range(5):
            self.lib.reset(slot)

    def step(self, x, y, ms, slot=0):
        value = self.lib.step(slot, x, y, ms)
        return value & 65535, value >> 16

    def test_slow_motion_has_no_start_deadzone(self):
        points = [self.step(500 + i, 700, i * 8)[0] for i in range(21)]
        self.assertGreater(points[5], 500)
        self.assertGreater(points[-1], 515)
        self.assertEqual(points, sorted(points))
        self.assertLessEqual(max(b - a for a, b in zip(points, points[1:])), 2)

    def test_normal_motion_starts_in_first_sample(self):
        self.step(500, 700, 0)
        x, y = self.step(510, 700, 8)
        self.assertGreaterEqual(x, 507)
        self.assertEqual(y, 700)

    def test_direction_reversal_has_no_median_pause(self):
        for i in range(10):
            previous, _ = self.step(500 + i * 10, 700, i * 8)
        x, _ = self.step(580, 700, 80)
        self.assertLess(x, previous)

    def test_stationary_jitter_is_reduced(self):
        self.step(500, 700, 0)
        points = [self.step(500 + (2 if i % 2 else -2), 700, i * 8)[0]
                  for i in range(1, 101)]
        self.assertLessEqual(max(points) - min(points), 2)

    def test_fast_motion_remains_responsive_and_stops(self):
        self.step(500, 700, 0)
        for i in range(1, 41):
            x, y = self.step(500 + i * 20, 700, i * 8)
            self.assertLessEqual(500 + i * 20 - x, 5)
            self.assertEqual(y, 700)
        for i in range(41, 61):
            x, _ = self.step(1300, 700, i * 8)
            self.assertLessEqual(x, 1300)
        self.assertEqual(x, 1300)

    def test_one_frame_spike_is_removed(self):
        self.step(500, 700, 0)
        for i, x in enumerate([500, 1800, 500, 500], 1):
            self.assertEqual(self.step(x, 700, i * 8), (500, 700))

    def test_two_frame_jump_is_rejected_without_poisoning_mouse_input(self):
        self.step(500, 700, 0)
        for i, x in enumerate([1800, 1800, 500, 500, 500], 1):
            self.assertEqual(self.step(x, 700, i * 8), (500, 700))
            self.assertEqual(self.lib.guarded(0) & 65535, 500)

    def test_persistent_jump_recovers(self):
        self.step(500, 700, 0)
        points = [self.step(1800, 700, i * 8)[0] for i in range(1, 21)]
        self.assertEqual(points[:2], [500] * 2)
        self.assertGreater(points[2], 500)
        self.assertEqual(points[-1], 1800)

    def test_lift_and_retouch_reset_history(self):
        self.step(500, 700, 0)
        self.step(550, 750, 8)
        self.lib.reset(0)
        self.assertEqual(self.step(1800, 200, 16), (1800, 200))

    def test_contacts_are_independent(self):
        for slot in range(5):
            self.step(200 + slot * 300, 700, 0, slot)
        for i in range(1, 20):
            self.step(200 + i * 10, 700, i * 8)
            for slot in range(1, 5):
                self.assertEqual(self.step(200 + slot * 300, 700, i * 8, slot),
                                 (200 + slot * 300, 700))

    def test_time_wrap_and_equal_timestamps(self):
        self.step(500, 700, 0xfffffff8)
        self.step(510, 700, 0)
        x, y = self.step(520, 700, 0)
        self.assertTrue(500 < x < 520)
        self.assertEqual(y, 700)

    def test_long_gap_rebases(self):
        self.step(500, 700, 0)
        self.assertEqual(self.step(1800, 200, 101), (1800, 200))

    def test_sample_rate_consistency(self):
        finals = []
        for slot, interval in enumerate([4, 8, 16]):
            for ms in range(0, 801, interval):
                x, _ = self.step(500 + ms // 10, 700, ms, slot)
            finals.append(x)
        self.assertLessEqual(max(finals) - min(finals), 3)

    def test_diagonal_and_reverse_motion_are_symmetric(self):
        self.step(500, 500, 0, 0)
        self.step(1500, 1500, 0, 1)
        for i in range(1, 41):
            x, y = self.step(500 + i * 10, 500 + i * 10, i * 8, 0)
            rx, ry = self.step(1500 - i * 10, 1500 - i * 10, i * 8, 1)
            self.assertEqual(x, y)
            self.assertEqual(rx, ry)
            self.assertLessEqual(abs(x + rx - 2000), 1)

    def test_full_coordinate_range_does_not_overflow(self):
        self.step(0, 0, 0)
        for i in range(1, 30):
            x, y = self.step(65535, 65535, i * 8)
            self.assertEqual(x, y)
        self.assertEqual(x, 65535)


if __name__ == '__main__':
    unittest.main()
