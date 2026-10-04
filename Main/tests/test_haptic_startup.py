"""Run the real hardware initialization function with mocked peripheral/BSP calls."""
import ctypes as C
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HW = ROOT / 'main/I2C/SUB_DEV'
BSP = HW / 'mcu-drivers/cs40l25/bsp'

MOCK_HEADER = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include "mcu-drivers/cs40l25/bsp/surface_fw_metadata.h"
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
#define ESP_OK 0
#define BSP_STATUS_OK 0
#define BSP_STATUS_FAIL 1
#define EN_OFF 0
#define EN_ON 1
#define GPIO_MODE_OUTPUT 1
#define GPIO_HAPTIC_BUCK_BOOST_EN 14
#define HAPTIC_MOTOR_ADDR 0x43
#define MP28167_ADDR 0x60
#define CS40L25_STATE_DSP_POWER_UP 5
#define CS40L25_DEVID 0x40a250
#define CS40L25B_DEVID 0x40a25b
#define CS40L25_SW_RESET_DEVID_REG 0
#define CS40L25_SW_RESET_REVID_REG 4
#define BOOST_VBST_CTL_1_REG 0x3800
#define BOOST_VBST_CTL_2_REG 0x3804
#define MSM_BLOCK_ENABLES_REG 0x2018
#define MSM_GLOBAL_ENABLES_REG 0x2014
#define XM_UNPACKED24_DSP1_SCRATCH_REG 0x2800000
#define CS40L25_SURFACE_VBST_CTL 0xaa
#define CS40L25_EVENT_FLAG_DSP_ERROR (1U << 0)
#define CS40L25_EVENT_FLAG_AMP_SHORT (1U << 1)
#define CS40L25_EVENT_FLAG_OVERTEMP_ERROR (1U << 2)
#define CS40L25_EVENT_FLAG_OVERTEMP_WARNING (1U << 3)
#define CS40L25_EVENT_FLAG_BOOST_INDUCTOR_SHORT (1U << 4)
#define CS40L25_EVENT_FLAG_BOOST_UNDERVOLTAGE (1U << 5)
#define CS40L25_EVENT_FLAG_BOOST_OVERVOLTAGE (1U << 6)
#define CS40L25_EVENT_FLAG_STATE_ERROR (1U << 7)
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(...) mock_log(__VA_ARGS__)
#define ESP_LOGE(...) mock_log(__VA_ARGS__)
extern i2c_master_dev_handle_t dev_haptic_motor_handle, sub_dev_mp28167_handle;
extern i2c_master_bus_handle_t bus_handle, sub_bus_handle;
extern const uint8_t cs40l25_fw_img[];
void mock_log(const char *, const char *, ...);
const char *esp_err_to_name(int);
int gpio_set_level(int, int);
int gpio_set_direction(int, int);
int i2c_master_probe(void *, unsigned, unsigned);
int i2c_master_transmit(void *, const uint8_t *, size_t, unsigned);
int i2c_master_transmit_receive(void *, const uint8_t *, size_t, uint8_t *, size_t, unsigned);
void vTaskDelay(unsigned);
unsigned xTaskGetTickCount(void);
unsigned bsp_initialize(void (*)(uint32_t, void *), void *);
"""

MOCK_C = r"""
#include "mock.h"
#include "surface_haptic_hw.h"
int _fltused = 0;
void *memcpy(void *out, const void *in, size_t n) {
    volatile uint8_t *d=out; const uint8_t *s=in;
    for(size_t i=0;i<n;++i) d[i]=s[i]; return out;
}
static unsigned scenario, ticks, snapshots, processes, configured;
void *bus_handle, *sub_bus_handle, *dev_haptic_motor_handle, *sub_dev_mp28167_handle;
/* Only header fields read by initialization; no actual firmware is sent. */
const uint8_t cs40l25_fw_img[28] = {
    [8]=SURFACE_FW_SIZE_BYTES & 255, [9]=(SURFACE_FW_SIZE_BYTES >> 8) & 255,
    [20]=SURFACE_FW_ID & 255, [21]=(SURFACE_FW_ID >> 8) & 255, [22]=SURFACE_FW_ID >> 16,
    [24]=SURFACE_FW_REVISION & 255, [25]=(SURFACE_FW_REVISION >> 8) & 255, [26]=SURFACE_FW_REVISION >> 16
};
void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(int e) { (void)e; return "mock"; }
int gpio_set_level(int pin,int level) { (void)pin; (void)level; return 0; }
int gpio_set_direction(int pin,int mode) { (void)pin; (void)mode; return 0; }
int i2c_master_probe(void *bus,unsigned address,unsigned timeout) {
    (void)bus; (void)address; (void)timeout; return 0;
}
int i2c_master_transmit(void *dev,const uint8_t *tx,size_t n,unsigned timeout) {
    (void)dev; (void)tx; (void)n; (void)timeout; return 0;
}
int i2c_master_transmit_receive(void *dev,const uint8_t *tx,size_t n,uint8_t *rx,size_t count,unsigned timeout) {
    (void)dev; (void)count; (void)timeout;
    if(n==1) { rx[0]=tx[0]==1 ? 157 : 0; return 0; } /* VREF raw 1256 */
    uint32_t reg=((uint32_t)tx[0]<<24)|((uint32_t)tx[1]<<16)|((uint32_t)tx[2]<<8)|tx[3];
    uint32_t value=reg==CS40L25_SW_RESET_DEVID_REG ? CS40L25_DEVID :
                   reg==BOOST_VBST_CTL_1_REG ? CS40L25_SURFACE_VBST_CTL : 0;
    for(unsigned i=0;i<4;++i) rx[i]=(uint8_t)(value>>(24-8*i));
    return 0;
}
void vTaskDelay(unsigned t) { ticks+=t; }
unsigned xTaskGetTickCount(void) { return ticks; }
unsigned bsp_initialize(void (*notify)(uint32_t,void *),void *arg) { (void)notify; (void)arg; return 0; }
unsigned bsp_dut_initialize(void) { return 0; }
unsigned bsp_dut_boot(bool cal) { (void)cal; return scenario==12; }
unsigned bsp_dut_power_up(void) { return scenario==11; }
unsigned bsp_dut_process(void) { return scenario==9 && ++processes>1; }
unsigned bsp_dut_get_startup_status(bsp_dut_startup_status_t *s) {
    ++snapshots;
    if(scenario==8 && snapshots>=2) return 1;
    s->driver_state=scenario==6 ? 6 : 5;
    s->halo_state=scenario==4 ? 0 : 0xcb;
    s->power_state=(scenario==1 || scenario==2) ? 1 : scenario==13 ? 3 :
                   (scenario==7 && snapshots<3) ? 0 : 2;
    s->scratch=scenario==5;
    s->heartbeat=scenario==3 ? 0 : scenario==1 ? 0x193+snapshots : 0x193;
    return 0;
}
unsigned bsp_dut_get_num_waves(uint32_t *count) { *count=scenario==10 ? 77 : SURFACE_FW_NUM_WAVES; return 0; }
unsigned bsp_dut_has_processed(bool *changed) { *changed=false; return 0; }
unsigned bsp_dut_update_haptic_config(uint8_t index) { (void)index; ++configured; return 0; }
unsigned bsp_dut_enable_haptic_processing(bool enable) { (void)enable; ++configured; return 0; }
void bsp_dut_log_gain(const char *phase) { (void)phase; }
void bsp_dut_dump_trigger_diagnostics(uint8_t waveform,uint32_t duration) { (void)waveform; (void)duration; }
__declspec(dllexport) int test_initialize(unsigned which) {
    scenario=which; ticks=snapshots=processes=configured=0;
    return surface_haptic_hw_initialize();
}
__declspec(dllexport) unsigned test_ticks(void) { return ticks; }
__declspec(dllexport) unsigned test_snapshots(void) { return snapshots; }
__declspec(dllexport) unsigned test_configured(void) { return configured; }
"""


class StartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp = tempfile.TemporaryDirectory(prefix='haptic_startup_')
        root = Path(cls.tmp.name)
        source = (HW / 'surface_haptic_hw.c').read_text()
        # Compile an unchanged copy of the production translation unit, with
        # peripheral headers substituted. No initialization logic is extracted.
        (root / 'surface_haptic_hw.c').write_text(source)
        for header in re.findall(r'^#include "([^"]+)"', source, re.M):
            dest = root / header
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text('#include "mock.h"\n')
        for src, dest in [
            (HW/'surface_haptic_hw.h', 'surface_haptic_hw.h'),
            (HW/'sub_dev.h', 'sub_dev.h'),
            (BSP/'surface_fw_metadata.h', 'mcu-drivers/cs40l25/bsp/surface_fw_metadata.h'),
            (BSP/'bsp_dut.h', 'I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h')
        ]:
            out=root/dest; out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(src.read_bytes())
        out=root/'I2C/SUB_DEV/mcu-drivers/common/bsp_driver_if.h'
        out.parent.mkdir(parents=True,exist_ok=True); out.write_text('#pragma once\n')
        (root/'mock.h').write_text(MOCK_HEADER)
        (root/'mock.c').write_text(MOCK_C)
        dll=root/'startup.dll'
        subprocess.run([compiler, '--target=x86_64-pc-windows-msvc', '-shared', '-nostdlib',
                        '-fuse-ld=lld', '-fno-stack-protector', '-Xlinker', '/noentry',
                        '-Wall', '-Wextra', '-Werror', '-I', str(root),
                        str(root/'surface_haptic_hw.c'), str(root/'mock.c'), '-o', str(dll)], check=True)
        cls.lib=C.CDLL(str(dll))
        cls.lib.test_initialize.argtypes=[C.c_uint]
        cls.lib.test_initialize.restype=C.c_int

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def test_logged_standby_with_static_nonzero_heartbeat_succeeds(self):
        self.assertEqual(self.lib.test_initialize(0), 1)
        self.assertEqual(self.lib.test_configured(), 2)
        # Keep the 250 ms supply wait and one 10 ms readiness observation,
        # without a fixed 100 ms pre-power-up delay.
        self.assertEqual(self.lib.test_ticks(), 260)

    def test_active_dsp_progress_succeeds(self):
        self.assertEqual(self.lib.test_initialize(1), 1)

    def test_invalid_or_stalled_dsp_times_out(self):
        for scenario in (2, 3, 4, 5, 6, 13):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.test_initialize(scenario), 0)
                self.assertGreaterEqual(self.lib.test_ticks(), 2250)
                self.assertEqual(self.lib.test_configured(), 0)

    def test_transitional_power_state_can_settle(self):
        self.assertEqual(self.lib.test_initialize(7), 1)
        self.assertGreaterEqual(self.lib.test_snapshots(), 3)

    def test_bus_process_boot_and_wave_failures_remain_fatal(self):
        for scenario in (8, 9, 10, 11, 12):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.test_initialize(scenario), 0)
                self.assertEqual(self.lib.test_configured(), 0)
                self.assertLess(self.lib.test_ticks(), 1000)


if __name__ == '__main__':
    unittest.main(verbosity=2)
