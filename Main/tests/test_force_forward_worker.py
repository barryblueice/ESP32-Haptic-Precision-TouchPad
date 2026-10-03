"""Test production force worker handoff with deterministic queue/I2C stubs."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = '''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_SURFACE_FORCE_FORWARD_ENABLE 1
#define REPORT_MAX_AGE_MS 100U
#define I2C_FREQ_HZ 400000
#define I2C_ADDR_BIT_LEN_7 0
#define ESP_OK 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int portMUX_TYPE;
typedef int esp_err_t;
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef void *QueueHandle_t;
typedef struct {int dev_addr_length, device_address, scl_speed_hz;} i2c_device_config_t;
typedef struct {uint8_t bytes[64]; uint32_t generation, output_generation, time_ms;} input_frame_t;
static unsigned clock_ms, source_generation = 1, bus_calls, queued, queue_size;
static bool sleeping, haptic_ready = true;
static unsigned char queue_bytes[256];
static void copy_bytes(void *out, const void *in, size_t n) {
    unsigned char *d = out; const unsigned char *s = in;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}
void *memcpy(void *d, const void *s, size_t n) { copy_bytes(d, s, n); return d; }
void *memset(void *d, int c, size_t n) {
    volatile unsigned char *p = d; for (size_t i = 0; i < n; ++i) p[i] = c; return d;
}
static QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    if (count != 1 || size > sizeof(queue_bytes)) return 0;
    queue_size = size; return queue_bytes;
}
static void vQueueDelete(QueueHandle_t q) {(void)q;}
static int xQueueOverwrite(QueueHandle_t q, const void *p) {
    (void)q; copy_bytes(queue_bytes, p, queue_size); queued = 1; return pdPASS;
}
static int xQueueReceive(QueueHandle_t q, void *p, unsigned wait) {
    (void)q; (void)wait; if (!queued) return 0;
    copy_bytes(p, queue_bytes, queue_size); queued = 0; return pdPASS;
}
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
    void *arg, unsigned priority, void *handle, int core) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    return pdPASS;
}
static bool input_force_forward_ready(uint32_t generation) {return generation == source_generation;}
static bool tp_modern_sleep_is_active(void) {return sleeping;}
static bool cs40l25_surface_is_ready(void) {return haptic_ready;}
static int64_t esp_timer_get_time(void) {return (int64_t)clock_ms * 1000;}
static int i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, void **dev) {
    (void)bus; (void)cfg; *dev = (void *)1; return 0;
}
static int i2c_master_transmit(void *dev, const uint8_t *p, size_t size, unsigned timeout) {
    (void)dev; (void)p; (void)size; (void)timeout; ++bus_calls; return 0;
}
static int i2c_master_transmit_receive(void *dev, const uint8_t *p, size_t size,
    uint8_t *out, size_t n, unsigned timeout) {
    (void)dev; (void)p; (void)size; (void)timeout; ++bus_calls;
    static const uint8_t sample[17] = {0,0x48,0x3f,0,0,0,0,0,0,0,0,0,0,0,0,0xb8,0xc0};
    copy_bytes(out, sample, n); return 0;
}
'''


class WorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='force_worker_')
        root = Path(cls.tmp.name)
        (root / 'stub.h').write_text(STUB)
        for name in ['sdkconfig.h', 'driver/i2c_master.h', 'SYS/input_pipeline.h',
                     'I2C/I2C_handle.h', 'I2C/TP/i2c_hid.h',
                     'I2C/SUB_DEV/cs40l25_surface.h', 'esp_log.h', 'esp_timer.h',
                     'freertos/queue.h']:
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('#include "stub.h"\n')
        wrapper = root / 'wrapper.c'
        source = (ROOT / 'main/I2C/TP/force_forward.c').as_posix()
        wrapper.write_text('#include "' + source + '"\n' + '''
__declspec(dllexport) void reset(void) {
    state = (force_forward_state_t){0}; lifecycle_epoch = 0;
    clock_ms = bus_calls = queued = 0; source_generation = 1;
    sleeping = false; haptic_ready = true;
    force_forward_init((void *)1, (void *)2);
}
__declspec(dllexport) void enqueue(unsigned gen, unsigned ms) {
    input_frame_t frame = {.bytes = {64, 0, 4}, .generation = gen, .time_ms = ms};
    force_forward_report(&frame);
}
__declspec(dllexport) void invalidate(void) {force_forward_invalidate();}
__declspec(dllexport) void set_clock(unsigned ms) {clock_ms = ms;}
__declspec(dllexport) void set_sleep(bool value) {sleeping = value;}
__declspec(dllexport) unsigned calls(void) {return bus_calls;}
__declspec(dllexport) unsigned drain(void) {
    forward_request_t request;
    if (xQueueReceive(forward_queue, &request, 0)) force_forward_process(&request);
    return state.sent;
}
''')
        compiler = os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        dll = root / 'worker.dll'
        subprocess.run([compiler, '--target=x86_64-pc-windows-msvc', '-shared', '-nostdlib',
                        '-fuse-ld=lld', '-fno-stack-protector', '-fno-builtin',
                        '-Xlinker', '/noentry', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-O2', '-I', str(root), str(wrapper),
                        str(ROOT / 'main/I2C/TP/force_forward_protocol.c'), '-o', str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.lib.enqueue.argtypes = [C.c_uint, C.c_uint]
        cls.lib.set_clock.argtypes = [C.c_uint]
        cls.lib.set_sleep.argtypes = [C.c_bool]
        cls.lib.calls.restype = cls.lib.drain.restype = C.c_uint

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.reset()

    def test_enqueue_does_not_perform_i2c(self):
        self.lib.enqueue(1, 0)
        self.assertEqual(self.lib.calls(), 0)
        self.assertEqual(self.lib.drain(), 1)
        self.assertEqual(self.lib.calls(), 3)

    def test_backlog_uses_latest_request_only(self):
        self.lib.enqueue(1, 0)
        self.lib.enqueue(1, 200)
        self.lib.set_clock(200)
        self.assertEqual(self.lib.drain(), 1)
        self.assertEqual(self.lib.drain(), 1)
        self.assertEqual(self.lib.calls(), 3)

    def test_reset_cancels_queued_request(self):
        self.lib.enqueue(1, 0)
        self.lib.invalidate()
        self.assertEqual(self.lib.drain(), 0)
        self.assertEqual(self.lib.calls(), 0)
        self.lib.enqueue(1, 0)
        self.assertEqual(self.lib.drain(), 1)

    def test_old_source_generation_cannot_forward(self):
        self.lib.enqueue(0, 0)
        self.assertEqual(self.lib.drain(), 0)
        self.assertEqual(self.lib.calls(), 0)

    def test_stale_request_is_discarded(self):
        self.lib.enqueue(1, 0)
        self.lib.set_clock(101)
        self.assertEqual(self.lib.drain(), 0)
        self.assertEqual(self.lib.calls(), 0)

    def test_sleep_cancels_queued_request(self):
        self.lib.enqueue(1, 0)
        self.lib.set_sleep(True)
        self.assertEqual(self.lib.drain(), 0)
        self.assertEqual(self.lib.calls(), 0)
        self.lib.set_sleep(False)
        self.lib.enqueue(1, 0)
        self.assertEqual(self.lib.drain(), 1)


if __name__ == '__main__':
    unittest.main()
