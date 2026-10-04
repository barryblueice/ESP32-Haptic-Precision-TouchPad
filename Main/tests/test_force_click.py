"""Replay production pressure-click logic with native C, without hardware."""
import ctypes as C
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import test_touch_latency as support

MAIN = Path(__file__).resolve().parents[1] / 'main'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


MOCK = r"""
#include "SYS/hid_msg.h"
#include "SYS/rstp_protocol.h"
#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1
#define ESP_LOGI(...) ((void)0)
static bool pressure_vbus_high;
static uint8_t current_tp_mode,ptp_button_press_threshold;
static uint8_t click_light_weight_threshold=80,click_midium_weight_threshold=100,click_strong_weight_threshold=130;
static void device_config_get(device_config_t *out) {
    *out=(device_config_t){0};
    out->bytes[CFG_WIRELESS_LIGHT]=60;
    out->bytes[CFG_WIRELESS_MEDIUM]=80;
    out->bytes[CFG_WIRELESS_STRONG]=100;
}
static uint16_t device_config_x_max(void) { return 2302; }
"""
WRAPPERS = r"""
API void t_reset(int powered,int level,int mode) {
    tp_multi_msg_t msg={0};ptp_reset_force_click(&msg);
    pressure_vbus_high=powered;ptp_button_press_threshold=level;current_tp_mode=mode;
    click_midium_weight_threshold=100;
}
API void t_custom_threshold(unsigned value) { click_midium_weight_threshold=value; }
API unsigned t_threshold(unsigned value) { return ptp_middle_press_threshold(value); }
API unsigned t_frame(int count,int z1,int z2,int x) {
    tp_multi_msg_t msg={0};
    for(int i=0;i<count;++i) msg.fingers[i]=(tp_finger_t){
        .tip_switch=1,.confidence=1,.contact_id=i,.x=x,.y=500,.pressure_z=i?z2:z1};
    ptp_update_force_click_button(&msg,count);
    return msg.button_mask;
}
"""


class ForceClickTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='force_click_')
        root=Path(cls.tmp.name)
        stubs={'stdio.h':'', 'esp_err.h':'typedef int esp_err_t; ',
               'math.h':'float sqrtf(float);',
               'driver/i2c_master.h':'typedef void *i2c_master_bus_handle_t; typedef void *i2c_master_dev_handle_t;',
               'freertos/FreeRTOS.h':'#include <stdint.h>',
               'freertos/queue.h':'typedef void *QueueHandle_t;'}
        for name,text in stubs.items():
            dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_text('#pragma once\n'+text+'\n')
        source=(MAIN/'I2C/TP/i2c_queue.c').read_text()
        state=re.search(r'typedef struct \{\s+bool tracking_contact;.*?\} ptp_force_click_state_t;',source,re.S).group()
        defines='\n'.join(re.findall(r'^#define FORCE_CLICK_.*',source,re.M))
        funcs=['ptp_map_button_press_threshold','ptp_middle_press_threshold','ptp_reset_force_click',
               'ptp_apply_force_click_deadzone','ptp_update_force_click_button']
        bodies=[]
        for name in funcs:
            match=re.search(r'static (?:uint8_t|void) '+name+r'\(',source)
            bodies.append(function(source,match.group()))
        code=support.SUPPORT+MOCK+defines+'\n'+state+'\nstatic ptp_force_click_state_t ptp_force_click_state;\n'+'\n'.join(bodies)+WRAPPERS
        (root/'harness.c').write_text(code)
        compiler=os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        dll=root/'force_click.dll'
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib',
            '-fuse-ld=lld','-fno-stack-protector','-fno-builtin','-Xlinker','/noentry',
            '-Wall','-Wextra','-Werror','-I',str(root),'-I',str(MAIN),str(root/'harness.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
        cls.lib.t_frame.argtypes=[C.c_int]*4

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def setUp(self): self.lib.t_reset(1,2,0)
    def frame(self,z1,z2=0,count=2,x=500): return self.lib.t_frame(count,z1,z2,x)

    def test_light_two_finger_pressure_no_longer_clicks(self):
        for powered,pressure in ((1,60),(0,48)):
            self.lib.t_reset(powered,2,0)
            self.assertEqual([self.frame(pressure,pressure) for _ in range(20)],[0]*20)

    def test_default_levels_have_distinct_higher_boundaries(self):
        for powered,thresholds in ((1,(144,180,234)),(0,(108,144,180))):
            for level,threshold in enumerate(thresholds,1):
                with self.subTest(powered=powered,level=level):
                    self.lib.t_reset(powered,level,0)
                    low=(threshold-1)//2
                    for _ in range(10):self.assertEqual(self.frame(low,threshold-1-low),0)
                    z=threshold//2
                    self.assertEqual(self.frame(z,threshold-z),0)
                    self.assertEqual(self.frame(z,threshold-z),4)

    def test_middle_threshold_never_wraps_for_any_configured_value(self):
        values=[self.lib.t_threshold(i) for i in range(256)]
        self.assertTrue(all(1<=v<=255 for v in values))
        self.assertEqual(values,sorted(values))
        self.assertEqual(values[100],180)
        self.assertEqual(values[200],255)
        self.assertEqual(values[255],255)

    def test_custom_high_threshold_cannot_become_a_light_click(self):
        self.lib.t_custom_threshold(220)
        for _ in range(10):self.assertEqual(self.frame(120,120),0)
        self.assertEqual(self.frame(128,128),0)
        self.assertEqual(self.frame(128,128),4)

    def test_one_frame_spike_does_not_click(self):
        self.frame(30,30)
        self.assertEqual(self.frame(100,100),0)
        for _ in range(10):self.assertEqual(self.frame(30,30),0)

    def test_release_hysteresis_and_real_lift(self):
        self.frame(90,90);self.frame(90,90)
        self.assertEqual(self.frame(90,90),4)
        for _ in range(10):self.assertEqual(self.frame(85,84),4)
        releases=[self.frame(75,75) for _ in range(20)]
        self.assertEqual(releases[-1],0)
        self.assertTrue(all(v==0 for v in releases[releases.index(0):]))
        self.lib.t_reset(1,2,0)
        self.frame(90,90);self.frame(90,90);self.frame(90,90)
        self.assertEqual(self.frame(0,count=0),0)

    def test_single_finger_left_right_and_ptp_thresholds_unchanged(self):
        for mode,x,button in ((0,500,1),(0,1800,2),(1,1800,1)):
            self.lib.t_reset(1,2,mode)
            for _ in range(10):self.assertEqual(self.frame(99,count=1,x=x),0)
            self.assertEqual(self.frame(100,count=1,x=x),0)
            self.assertEqual(self.frame(100,count=1,x=x),button)

    def test_ptp_two_finger_contact_does_not_emit_middle_button(self):
        self.lib.t_reset(1,2,1)
        for _ in range(10):self.assertEqual(self.frame(200,200),0)

    def test_middle_button_type_remains_latched_after_finger_lift(self):
        self.frame(100,100);self.frame(100,100)
        self.assertEqual(self.frame(100,100),4)
        self.assertEqual(self.frame(200,count=1),4)
        self.assertEqual(self.frame(0,count=0),0)


if __name__=='__main__': unittest.main()
