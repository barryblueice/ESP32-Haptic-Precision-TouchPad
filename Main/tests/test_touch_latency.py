"""Replay production mouse/knuckle/buffer code with a native C harness, no hardware."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'main'

SUPPORT = r"""
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
void *memset(void *p, int c, size_t n) { unsigned char *s=p; while(n--) *s++=(unsigned char)c; return p; }
void *memcpy(void *p, const void *q, size_t n) { unsigned char *s=p; const unsigned char *t=q; while(n--) *s++=*t++; return p; }
void *memmove(void *p, const void *q, size_t n) {
    unsigned char *s=p; const unsigned char *t=q;
    if(s<t) { for(size_t i=0;i<n;++i)s[i]=t[i]; }
    else { while(n) { --n; s[n]=t[n]; } } return p;
}
float sqrtf(float v) { if(v<=0)return 0; float x=v>1?v:1; for(int i=0;i<32;++i)x=(x+v/x)*0.5f; return x; }
int abs(int v) { return v<0?-v:v; }
int _fltused = 0;
#define API __declspec(dllexport)
"""
WRAPPERS = r"""
static report_buffer_t buffer;
static usb_pump_state_t pump;
static knuckle_gesture_t knuckle;
static pointer_motion_t trajectory;
API void t_reset(void) {
    ptp_simulated_mouse_reset(); memset(&buffer,0,sizeof(buffer));
    memset(&pump,0,sizeof(pump)); memset(&knuckle,0,sizeof(knuckle));
    memset(&trajectory,0,sizeof(trajectory));
}
API void t_mouse(int mask, int x, int y, int time, int buttons, mouse_hid_report_t *out) {
    tp_multi_msg_t msg={.button_mask=(uint8_t)buttons, .scan_time=(uint16_t)time};
    for(int i=0;i<5;++i) if(mask&(1<<i)) msg.fingers[i]=(tp_finger_t){
        .x=(uint16_t)(x+i*10),.y=(uint16_t)y,.tip_switch=1,.confidence=1,.contact_id=(uint8_t)i};
    parse_ptp_simulated_mouse_report(&msg,out);
}
API int t_tap_release(void) { return ptp_simulated_mouse_click_needs_release(); }
API void t_motion(float x,float y,unsigned time,pointer_step_t *out) {
    *out=pointer_motion_update(&trajectory,x,y,(uint16_t)time);
}
API void t_buffer_mode(int mode) { memset(&buffer,0,sizeof(buffer)); buffer.mode=(uint8_t)mode; buffer.last.mode=(uint8_t)mode; }
API int t_push(unsigned time,int buttons,int x,int tip,int tap) {
    input_report_t r={.mode=buffer.mode,.time_ms=time,.read_done_us=time*1000U};
    if(buffer.mode==MOUSE_MODE) r.data.mouse=(mouse_hid_report_t){.buttons=(uint8_t)buttons,.x=(int8_t)x};
    else { r.data.ptp.buttons=(uint8_t)buttons; r.data.ptp.contact_count=1;
        r.data.ptp.fingers[0]=(finger_t){.tip_conf_id=(uint8_t)tip,.x=(uint16_t)x}; }
    return report_buffer_push(&buffer,&r,tap!=0);
}
API int t_take(unsigned now,int *values) {
    input_report_t r;
    if(!report_buffer_take(&buffer,now,&r))return 0;
    values[0]=r.mode==MOUSE_MODE?r.data.mouse.x:r.data.ptp.fingers[0].x;
    values[1]=r.mode==MOUSE_MODE?r.data.mouse.buttons:r.data.ptp.buttons;
    values[2]=(int)r.time_ms; values[3]=r.release; values[4]=(int)r.read_done_us;
    return 1;
}
API unsigned t_buffer_stat(int which) {
    return which==0?buffer.count:which==1?buffer.stats.recoveries:which==2?buffer.stats.longest_wait_ms:buffer.recovering;
}
API void t_recover(void) { report_buffer_reset(&buffer,buffer.mode); }
API int t_observe(int up) { return report_buffer_observe(&buffer,up!=0); }
API void t_ack_release(void) {
    input_report_t r={.mode=buffer.mode,.generation=buffer.generation,.release=true}; report_buffer_ack(&buffer,&r);
}
API void t_request(void) { usb_pump_request(&pump); }
API int t_schedule(void) { return usb_pump_schedule(&pump); }
API int t_finish(void) { return usb_pump_finish(&pump); }
API void t_pump_wrap(void) { pump.requested=UINT32_MAX; }
API int t_knuckle(unsigned time,int x,int count,unsigned buffered) {
    uint8_t p[64]={0x40,0,2,0};
    for(int i=0;i<count;++i) { unsigned at=4+8*i; p[at]=1;
        p[at+1]=(uint8_t)x;p[at+2]=(uint8_t)(x>>8);p[at+3]=200;p[at+5]=45;p[at+6]=2;p[at+7]=2; }
    knuckle_result_t r=knuckle_gesture_update_buffered(&knuckle,p,time,buffered,knuckle_gesture_active_classifier());
    return r.suppress|(r.replay<<1)|(r.screenshot<<2);
}
"""

class Mouse(C.Structure):
    _pack_ = 1
    _fields_ = [('buttons', C.c_uint8)] + [(k, C.c_int8) for k in ('x','y','wheel','pan')]

class Step(C.Structure):
    _fields_ = [('dx',C.c_float),('dy',C.c_float),('speed',C.c_float),('reanchored',C.c_bool)]

class TouchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp = tempfile.TemporaryDirectory(prefix='touch_latency_')
        root=Path(cls.tmp.name)
        stubs={
            'stdio.h':'',
            'math.h':'#define M_PI 3.14159265358979323846\n#define isfinite(x) __builtin_isfinite(x)\n#define fabsf(x) __builtin_fabsf(x)\nfloat sqrtf(float);\n',
            'sdkconfig.h':'#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1\n',
            'driver/i2c_master.h':'typedef void *i2c_master_bus_handle_t; typedef void *i2c_master_dev_handle_t;\n',
            'freertos/FreeRTOS.h':'#include <stdint.h>\n',
            'freertos/queue.h':'typedef void *QueueHandle_t;\n',
            'esp_err.h':'typedef int esp_err_t;\n',
            'esp_timer.h':'typedef void *esp_timer_handle_t;\n',
        }
        for name,text in stubs.items():
            dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('#pragma once\n'+text)
        sources=['I2C/TP/pointer_motion.c','I2C/TP/ptp_simulated_mouse_gesture.c',
                 'SYS/report_buffer.c','SYS/knuckle_gesture.c','USB/usb_pump_state.h']
        code=SUPPORT+'\n'.join('#include "'+(MAIN/s).as_posix()+'"' for s in sources)+WRAPPERS
        (root/'harness.c').write_text(code)
        dll=root/'touch.dll'
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib',
            '-fuse-ld=lld','-fno-stack-protector','-fno-builtin','-Xlinker','/noentry',
            '-Wall','-Wextra','-Werror','-I',str(root),'-I',str(MAIN),str(root/'harness.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
        cls.lib.t_motion.argtypes=[C.c_float,C.c_float,C.c_uint,C.POINTER(Step)]
        cls.lib.t_mouse.argtypes=[C.c_int]*5+[C.POINTER(Mouse)]
        cls.lib.t_take.argtypes=[C.c_uint,C.POINTER(C.c_int)]

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()

    def setUp(self): self.lib.t_reset()
    def mouse(self,x=500,y=500,t=0,mask=1,buttons=0):
        out=Mouse();self.lib.t_mouse(mask,x,y,t,buttons,C.byref(out));return out
    def motion(self,x,y,t):
        out=Step();self.lib.t_motion(x,y,t,C.byref(out));return out
    def take(self,t):
        out=(C.c_int*5)(); return list(out) if self.lib.t_take(t,out) else None

    def test_first_movement_uses_first_contact_anchor(self):
        self.assertEqual(self.mouse(t=100).x,0)
        self.assertGreater(self.mouse(x=505,t=200).x,0)
    def test_slow_motion_is_not_frozen(self):
        self.mouse(t=100)
        values=[self.mouse(x=500+i,t=100+i*100).x for i in range(1,101)]
        self.assertGreater(sum(values),60)
        self.assertLess(max(values),5)
    def test_reverse_responds_in_next_frame(self):
        for i in range(10): self.mouse(x=500+10*i,t=i*100)
        self.assertLess(self.mouse(x=575,t=1000).x,0)
    def test_stop_tail_within_one_count_after_two_frames(self):
        for step in (1,5,20,60):
            with self.subTest(step=step):
                self.lib.t_reset()
                for i in range(20):self.mouse(x=100+step*i,t=i*100)
                out=[self.mouse(x=100+step*19,t=(20+i)*100).x for i in range(20)]
                self.assertLessEqual(sum(abs(x) for x in out[2:]),1,out)
    def test_static_jitter_has_no_runaway_drift(self):
        out=[self.mouse(x=500+(i%3-1),y=500,t=i*100).x for i in range(300)]
        self.assertLessEqual(abs(sum(out)),2)
        self.assertLessEqual(max(map(abs,out)),1)
    def test_single_spike_is_rejected_without_reverse_jump(self):
        self.mouse(t=100);self.mouse(x=505,t=200)
        self.assertEqual(self.mouse(x=1500,t=300).x,0)
        self.assertGreaterEqual(self.mouse(x=510,t=400).x,0)
    def test_stable_relocation_reanchors_then_resumes(self):
        self.mouse(t=100);self.mouse(x=505,t=200)
        self.assertEqual(self.mouse(x=1500,t=300).x,0)
        self.assertEqual(self.mouse(x=1502,t=400).x,0)
        self.assertGreater(self.mouse(x=1510,t=500).x,0)
    def test_repeated_unstable_spikes_do_not_freeze_valid_motion(self):
        self.motion(500,500,100)
        for i,x in enumerate((1500,1000,1800,1300)):
            self.assertEqual(self.motion(x,500,200+i*100).dx,0)
        self.assertGreater(self.motion(510,500,600).dx,0)
    def test_contact_replacement_and_reset_reanchor(self):
        self.mouse(t=100);self.mouse(x=505,t=200)
        self.assertEqual(self.mouse(x=1400,t=300,mask=2).x,0)
        self.assertGreater(self.mouse(x=1410,t=400,mask=2).x,0)
        self.lib.t_reset();self.assertEqual(self.mouse(x=1600,t=500).x,0)
    def test_timestamp_wrap_and_equal_time_are_bounded(self):
        self.motion(500,500,65500)
        self.assertGreater(self.motion(505,500,64).dx,0)
        self.assertGreater(self.motion(506,500,64).dx,0)
        self.assertTrue(self.motion(600,500,3000).reanchored)
    def test_tap_and_force_click_do_not_duplicate(self):
        self.mouse(t=100);self.mouse(t=200)
        self.assertEqual(self.mouse(mask=0,t=300).buttons,1)
        self.assertEqual(self.lib.t_tap_release(),1)
        self.assertEqual(self.lib.t_tap_release(),0)
        self.lib.t_reset()
        self.mouse(t=100);self.mouse(t=200,buttons=1)
        self.assertEqual(self.mouse(mask=0,t=300).buttons,0)
    def test_double_tap_drag(self):
        self.mouse(t=100);self.mouse(t=200);self.mouse(mask=0,t=300)
        self.lib.t_tap_release()
        self.mouse(t=400);self.mouse(t=500)
        self.assertEqual(self.mouse(x=540,t=600).buttons,1)
        self.assertEqual(self.mouse(mask=0,t=700).buttons,0)
    def test_ghost_second_finger_does_not_become_right_tap(self):
        self.mouse(t=100);self.mouse(t=200)
        self.mouse(mask=3,t=300);self.mouse(t=400)
        self.assertEqual(self.mouse(mask=0,t=500).buttons,1)
    def test_scroll_and_middle_drag(self):
        scroll=[self.mouse(mask=3,x=500,y=300+i*10,t=i*100) for i in range(35)]
        self.assertGreater(sum(abs(r.wheel) for r in scroll),0)
        self.assertEqual(sum(abs(r.x)+abs(r.y) for r in scroll),0)
        self.lib.t_reset()
        drag=[self.mouse(mask=3,x=500+i*8,t=i*100,buttons=4) for i in range(15)]
        self.assertTrue(all(r.buttons==4 for r in drag))
        self.assertGreater(sum(r.x for r in drag),0)
        self.assertEqual(sum(abs(r.wheel) for r in drag),0)
    def test_ptp_merge_keeps_latest_time_but_oldest_deadline(self):
        self.lib.t_buffer_mode(1)
        self.lib.t_push(10,0,10,3,0)  # Down edge cannot be merged.
        self.lib.t_push(20,0,20,3,0)
        self.lib.t_push(90,0,90,3,0)
        self.assertEqual(self.take(95)[:3],[10,0,10])
        latest=self.take(100)
        self.assertEqual(latest[:3],[90,0,90]);self.assertEqual(latest[4],90000)
        self.lib.t_push(110,0,100,3,0);self.lib.t_push(200,0,110,3,0)
        self.assertEqual(self.take(211)[3],1)
        self.assertEqual(self.lib.t_buffer_stat(1),1)
    def test_mouse_merge_conserves_displacement_and_button_edges(self):
        for t,b,x in ((1,0,100),(2,0,100),(3,1,7),(4,1,8),(5,0,0)):
            self.assertTrue(self.lib.t_push(t,b,x,0,0))
        out=[]
        while (r:=self.take(10)) is not None:out.append(r)
        self.assertEqual(sum(r[0] for r in out),215)
        self.assertEqual([r[1] for r in out],[0,0,1,1,0])
    def test_tap_pair_and_recovery_require_release_ack_and_real_lift(self):
        self.assertTrue(self.lib.t_push(1,1,0,0,1))
        self.assertEqual(self.take(2)[1],1);self.assertEqual(self.take(2)[1],0)
        self.lib.t_recover()
        self.assertFalse(self.lib.t_observe(0));self.assertFalse(self.lib.t_push(3,0,5,0,0))
        self.lib.t_ack_release();self.assertFalse(self.lib.t_observe(0))
        self.assertTrue(self.lib.t_observe(1));self.assertTrue(self.lib.t_push(4,0,5,0,0))
    def test_overflow_is_not_silently_dropped(self):
        for i in range(32):self.assertTrue(self.lib.t_push(i,i%2,0,0,0))
        self.assertFalse(self.lib.t_push(32,0,0,0,0))
        self.assertEqual(self.lib.t_buffer_stat(3),1)
    def test_queued_pump_request_survives_consumed_notification(self):
        self.lib.t_request();self.assertTrue(self.lib.t_schedule())
        self.lib.t_request();self.assertFalse(self.lib.t_schedule())
        self.assertTrue(self.lib.t_finish());self.assertTrue(self.lib.t_schedule())
        self.assertFalse(self.lib.t_finish())
    def test_busy_endpoint_does_not_spin_and_completion_wakes(self):
        self.lib.t_request();self.assertTrue(self.lib.t_schedule())
        self.assertFalse(self.lib.t_finish())  # No work arriving while busy.
        self.lib.t_request();self.assertTrue(self.lib.t_schedule())
        self.assertFalse(self.lib.t_finish())
    def test_pump_counter_wrap_and_enqueue_retry(self):
        self.lib.t_pump_wrap();self.assertTrue(self.lib.t_schedule())
        self.lib.t_request();self.assertTrue(self.lib.t_finish())
        self.assertTrue(self.lib.t_schedule());self.assertFalse(self.lib.t_finish())
        self.assertTrue(self.lib.t_schedule())  # Failed enqueue retired its queued flag.
    def test_stationary_knuckle_double_tap_still_screenshots(self):
        self.assertEqual(self.lib.t_knuckle(0,500,1,0),1)
        self.assertEqual(self.lib.t_knuckle(10,500,1,1),1)
        self.assertEqual(self.lib.t_knuckle(20,500,0,2)&4,0)
        self.assertEqual(self.lib.t_knuckle(100,500,1,0),1)
        self.lib.t_knuckle(110,500,1,1)
        self.assertEqual(self.lib.t_knuckle(120,500,0,2)&4,4)
    def test_knuckle_buffer_limit_and_timeout_release(self):
        self.lib.t_knuckle(0,500,1,0)
        self.assertEqual(self.lib.t_knuckle(10,500,1,16),2)
        self.lib.t_reset();self.lib.t_knuckle(0,500,1,0)
        self.assertEqual(self.lib.t_knuckle(121,500,1,1),2)
    def test_knuckle_movement_releases_candidate_once(self):
        self.assertEqual(self.lib.t_knuckle(0,500,1,0)&1,1)
        self.assertEqual(self.lib.t_knuckle(10,513,1,1),2)
        self.assertEqual(self.lib.t_knuckle(20,515,1,0),0)
        self.assertEqual(self.lib.t_knuckle(30,515,0,0),0)

if __name__=='__main__':unittest.main()
