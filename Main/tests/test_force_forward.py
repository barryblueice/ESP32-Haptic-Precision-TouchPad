"""Exercise the production C1 parser/forwarder with a simulated I2C bus (no hardware)."""
import ctypes as C
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
U8 = C.c_uint8
P = C.POINTER(U8)
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_size_t)
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_size_t, P, C.c_size_t)
NOW = C.CFUNCTYPE(C.c_uint32, C.c_void_p)
READY = C.CFUNCTYPE(C.c_bool, C.c_void_p)
SAMPLE = bytes.fromhex('00483F000000000000000000000000B8C0')
PREFIX = bytes.fromhex('22003F03232300120023')
SKIPPED, SENT, CANCELLED, READ_ERROR, CHECKSUM_ERROR, WRITE_ERROR = range(6)


class Sample(C.Structure):
    _fields_ = [('extra', C.c_uint16), ('channels', C.c_int16 * 6)]


class State(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        'epoch', 'failed_at', 'sent', 'read_failures', 'checksum_failures', 'forward_failures')]
    _fields_ += [('last_error', C.c_int), ('selected', C.c_bool), ('backoff', C.c_bool)]


class IO(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('select', WRITE), ('read', READ),
                ('forward', WRITE), ('now', NOW), ('ready', READY)]


def c1(extra, channels, header=0):
    tail = (65536 - extra - sum(channels)) & 0xffff
    return bytes([header]) + struct.pack('<H6hH', extra, *channels, tail)


class Bus:
    def __init__(self, raw=SAMPLE, fail_at=None, cancel_after=None):
        self.raw = raw
        self.fail_at = fail_at
        self.cancel_after = cancel_after
        self.clock = 0
        self.allowed = True
        self.calls = []
        self.callbacks = (WRITE(self.select), READ(self.read), WRITE(self.forward),
                          NOW(lambda _: self.clock), READY(lambda _: self.allowed))
        self.io = IO(None, *self.callbacks)

    def record(self, call):
        self.calls.append(call)
        self.clock = (self.clock + 1) & 0xffffffff
        if len(self.calls) == self.cancel_after:
            self.allowed = False
        return 0x107 if len(self.calls) == self.fail_at else 0

    def select(self, _, tx, size):
        return self.record(('select', bytes(tx[:size])))

    def read(self, _, tx, size, rx, count):
        error = self.record(('read', bytes(tx[:size]), count))
        if not error:
            C.memmove(rx, self.raw, min(count, len(self.raw)))
        return error

    def forward(self, _, tx, size):
        return self.record(('forward', bytes(tx[:size])))


class ForwardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        if not Path(compiler).exists():
            raise RuntimeError('Set FORCE_TEST_CC to a Windows clang/lld compiler')
        cls.tmp = tempfile.TemporaryDirectory(prefix='force_forward_')
        dll = Path(cls.tmp.name) / 'forward.dll'
        subprocess.run([compiler, '--target=x86_64-pc-windows-msvc', '-shared', '-nostdlib',
                        '-fuse-ld=lld', '-fno-stack-protector', '-Xlinker', '/noentry',
                        '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'main/I2C/TP/force_forward_protocol.c'), '-o', str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.lib.force_c1_decode.argtypes = [P, C.c_size_t, C.POINTER(Sample)]
        cls.lib.force_c1_decode.restype = C.c_bool
        cls.lib.force_forward_encode.argtypes = [C.POINTER(Sample), C.c_bool, P]
        cls.lib.force_forward_run.argtypes = [C.POINTER(State), C.POINTER(IO), P, C.c_size_t, C.c_uint32]
        cls.lib.force_forward_run.restype = C.c_int

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def decode(self, raw):
        sample = Sample()
        data = (U8 * len(raw)).from_buffer_copy(raw)
        return self.lib.force_c1_decode(data, len(raw), C.byref(sample)), sample

    def run_bus(self, state, bus, report=None, epoch=1):
        if report is None:
            report = bytes([64, 0, 4]) + bytes(61)
        data = (U8 * len(report)).from_buffer_copy(report)
        return self.lib.force_forward_run(C.byref(state), C.byref(bus.io), data, len(data), epoch)

    def test_document_sample_exact_wire_and_cache(self):
        state, bus = State(), Bus()
        self.assertEqual(self.run_bus(state, bus), SENT)
        expected_packet = PREFIX + struct.pack('<H6hB', 0x3f48, *([0] * 6), 0)
        self.assertEqual(bus.calls, [('select', bytes.fromhex('D0EC00C1')),
                                   ('read', bytes.fromhex('D0EE00'), 17),
                                   ('forward', expected_packet)])
        self.assertEqual(len(expected_packet), 25)
        self.assertEqual(self.run_bus(state, bus), SENT)
        self.assertEqual([x[0] for x in bus.calls], ['select', 'read', 'forward', 'read', 'forward'])
        self.assertEqual(state.sent, 2)

    def test_signed_channels_preserved_and_header_ignored(self):
        channels = [-32768, 32767, -1234, 4567, -1, 2]
        raw = c1(16200, channels, header=0xa5)
        ok, sample = self.decode(raw)
        self.assertTrue(ok)
        self.assertEqual(sample.extra, 16200)
        self.assertEqual(list(sample.channels), channels)
        state, bus = State(), Bus(raw)
        self.assertEqual(self.run_bus(state, bus), SENT)
        self.assertEqual(bus.calls[-1], ('forward', PREFIX + struct.pack('<H6hB', 16200, *channels, 0)))

    def test_non_modular_checksum_boundaries(self):
        for extra, channels, valid in [
            (0, [0]*6, False), (0, [-1, 0, 0, 0, 0, 0], False),
            (65535, [2, 0, 0, 0, 0, 0], False),
            (65535, [1, 0, 0, 0, 0, 0], True), (1, [0]*6, True),
            (65535, [32767]*6, False), (0, [-32768]*6, False),
        ]:
            with self.subTest(extra=extra, channels=channels):
                self.assertEqual(self.decode(c1(extra, channels))[0], valid)

    def test_invalid_c1_sizes_and_checksum(self):
        for raw in [SAMPLE[:-1], SAMPLE + b'\x00', bytes(17), SAMPLE[:-1] + b'\xc1']:
            self.assertFalse(self.decode(raw)[0])
        sample = Sample()
        self.assertFalse(self.lib.force_c1_decode(None, 17, C.byref(sample)))
        data = (U8*17).from_buffer_copy(SAMPLE)
        self.assertFalse(self.lib.force_c1_decode(data, 17, None))

    def test_qualified_keystroke_encoding(self):
        ok, sample = self.decode(SAMPLE)
        self.assertTrue(ok)
        packets = []
        for value in (False, True):
            packet = (U8*25)()
            self.lib.force_forward_encode(C.byref(sample), value, packet)
            self.assertEqual(packet[24], int(value))
            packets.append(bytes(packet))
        self.assertEqual(packets[0][:24], packets[1][:24])

    def test_report_id_and_length_gate(self):
        for report in [b'', b'\x00', b'\x00\x00', bytes(64),
                       bytes([2,0,4])+bytes(61), bytes([5,0,4])+bytes(61),
                       bytes([65,0,4])+bytes(61), bytes([255,255,4])+bytes(61),
                       bytes([64,0,4]), bytes([64,0,3])+bytes(61)]:
            state, bus = State(), Bus()
            self.assertEqual(self.run_bus(state, bus, report), SKIPPED)
            self.assertEqual(bus.calls, [])
        for report_id in (2, 4):
            self.assertEqual(self.run_bus(State(), Bus(), bytes([6,0,report_id,0,0,0])), SENT)

    def test_not_ready_then_wake_and_fast_epoch_change(self):
        state, bus = State(), Bus()
        self.assertEqual(self.run_bus(state, bus), SENT)
        bus.calls.clear()
        bus.allowed = False
        self.assertEqual(self.run_bus(state, bus), SKIPPED)
        self.assertEqual(bus.calls, [])
        self.assertFalse(state.selected)
        bus.allowed = True
        self.assertEqual(self.run_bus(state, bus), SENT)
        self.assertEqual(bus.calls[0][0], 'select')
        bus.calls.clear()
        # Sleep/wake or reset completed entirely between parser invocations.
        self.assertEqual(self.run_bus(state, bus, epoch=2), SENT)
        self.assertEqual(bus.calls[0][0], 'select')

    def test_lifecycle_cancellation_between_transactions(self):
        for cancel_after in (1, 2):
            state, bus = State(), Bus(cancel_after=cancel_after)
            self.assertEqual(self.run_bus(state, bus), CANCELLED)
            self.assertEqual(len(bus.calls), cancel_after)
            self.assertFalse(state.selected)
            self.assertEqual(state.sent, 0)
            bus.allowed = True
            self.assertEqual(self.run_bus(state, bus, epoch=2), SENT)
            self.assertEqual(bus.calls[cancel_after][0], 'select')

    def test_each_io_error_and_backoff_from_failure_completion(self):
        for fail_at in (1, 2, 3):
            state, bus = State(), Bus(fail_at=fail_at)
            expected = WRITE_ERROR if fail_at == 3 else READ_ERROR
            self.assertEqual(self.run_bus(state, bus), expected)
            self.assertEqual(len(bus.calls), fail_at)
            self.assertEqual(state.sent, 0)
            self.assertEqual(state.last_error, 0x107)
            self.assertEqual(state.read_failures, int(fail_at != 3))
            self.assertEqual(state.forward_failures, int(fail_at == 3))
            bus.clock = state.failed_at + 99
            self.assertEqual(self.run_bus(state, bus), SKIPPED)
            self.assertEqual(len(bus.calls), fail_at)
            bus.clock = state.failed_at + 100
            bus.raw = c1(16300, [1, 2, 3, 4, 5, 6])
            self.assertEqual(self.run_bus(state, bus), SENT)
            self.assertEqual(bus.calls[fail_at][0], 'read' if fail_at == 3 else 'select')
            self.assertEqual(bus.calls[-1][1], PREFIX + struct.pack('<H6hB', 16300, 1, 2, 3, 4, 5, 6, 0))

    def test_checksum_error_invalidates_cached_selection(self):
        state, bus = State(), Bus()
        self.assertEqual(self.run_bus(state, bus), SENT)
        bus.raw = bytes(17)
        self.assertEqual(self.run_bus(state, bus), CHECKSUM_ERROR)
        self.assertEqual(state.checksum_failures, 1)
        self.assertFalse(state.selected)
        self.assertEqual(len(bus.calls), 4)
        bus.raw = SAMPLE
        bus.clock = state.failed_at + 100
        self.assertEqual(self.run_bus(state, bus), SENT)
        self.assertEqual(bus.calls[4][0], 'select')

    def test_backoff_survives_epoch_and_clock_wrap(self):
        state, bus = State(), Bus(fail_at=1)
        bus.clock = 0xfffffff0
        self.assertEqual(self.run_bus(state, bus), READ_ERROR)
        bus.clock = (state.failed_at + 99) & 0xffffffff
        self.assertEqual(self.run_bus(state, bus, epoch=2), SKIPPED)
        self.assertEqual(len(bus.calls), 1)
        bus.clock = (state.failed_at + 100) & 0xffffffff
        self.assertEqual(self.run_bus(state, bus, epoch=2), SENT)
        self.assertEqual(bus.calls[1][0], 'select')


if __name__ == '__main__':
    unittest.main(verbosity=2)
