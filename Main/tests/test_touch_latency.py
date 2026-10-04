"""Replay production mouse/knuckle/buffer code with a native C harness, no hardware."""
import ctypes as C
import os
import math
import random
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
static void reset_special(void);
API void t_reset(void) {
    ptp_simulated_mouse_reset(); memset(&buffer,0,sizeof(buffer));
    memset(&pump,0,sizeof(pump)); memset(&knuckle,0,sizeof(knuckle));
    memset(&trajectory,0,sizeof(trajectory)); reset_special();
}
API void t_mouse_conf(int mask, int confidence, int x, int y, int time, int buttons, mouse_hid_report_t *out) {
    tp_multi_msg_t msg={.button_mask=(uint8_t)buttons, .scan_time=(uint16_t)time};
    for(int i=0;i<5;++i) if(mask&(1<<i)) msg.fingers[i]=(tp_finger_t){
        .x=(uint16_t)(x+i*10),.y=(uint16_t)y,.tip_switch=1,.confidence=(confidence>>i)&1,.contact_id=(uint8_t)i};
    parse_ptp_simulated_mouse_report(&msg,out);
}
API void t_mouse(int mask, int x, int y, int time, int buttons, mouse_hid_report_t *out) {
    t_mouse_conf(mask,mask,x,y,time,buttons,out);
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

# Execute the actual parser handoff/report block; mock only transport, config and
# haptics. This catches integration errors a standalone recognizer cannot see.
SPECIAL_WRAPPERS = r"""
static device_config_t test_config;
static edge_gesture_t edge_state;
static point_gesture_t point_state;
static unsigned aux_count, cancel_count;
static bool fail_publish;
static void reset_special(void) {
    memset(&test_config,0,sizeof(test_config));
    edge_gesture_reset(&edge_state); point_gesture_reset(&point_state);
    aux_count=cancel_count=0; fail_publish=false;
}
static void device_config_get(device_config_t *out) { *out=test_config; }
static void point_schedule(void) { }
static void aux_output_cancel_gesture(void) { ++cancel_count; }
static bool aux_output_once(unsigned a,int steps,unsigned g,unsigned t) {
    (void)a;(void)steps;(void)g;(void)t;++aux_count;return true;
}
#define aux_output_hold aux_output_once
#define aux_output_repeat aux_output_once
#define usb_aux_steps aux_output_once
static void input_recover(void) { report_buffer_reset(&buffer,buffer.mode); }
static void input_source_gesture(unsigned g,bool point) { (void)g;(void)point; }
static void input_source_button(unsigned g,bool down) { (void)g;(void)down; }
static void ptp_reset_force_click(tp_multi_msg_t *m) { m->button_mask=0; }
static void ptp_update_force_click_button(tp_multi_msg_t *m,int n) { (void)m;(void)n; }
static int input_mode(void) { return buffer.mode; }
static bool input_publish(unsigned g,const input_report_t *r,bool tap) {
    (void)g;return !fail_publish && report_buffer_push(&buffer,r,tap);
}
static void input_publish_pair(unsigned g,const input_report_t *d,const input_report_t *u) {
    input_publish(g,d,false);input_publish(g,u,false);
}
void parse_ptp_report(const tp_multi_msg_t *m,ptp_report_t *r) { (void)m;memset(r,0,sizeof(*r)); }
API void t_fail_publish(void) { fail_publish=true; }
API void t_config(unsigned offset,unsigned value) { test_config.bytes[offset]=(uint8_t)value; }
API unsigned t_aux_count(void) { return aux_count; }
API unsigned t_cancel_count(void) { return cancel_count; }
API int t_inside(unsigned p,unsigned radius,unsigned x,unsigned y,unsigned xmax,unsigned ymax,unsigned w,unsigned h) {
    return point_gesture_inside(p,radius,x,y,xmax,ymax,w,h);
}
API int t_knuckle_packet(const uint8_t *p,unsigned now,unsigned buffered,int v2) {
    knuckle_result_t r=knuckle_gesture_update_buffered(&knuckle,p,now,buffered,
        v2?knuckle_gesture_active_classifier():NULL);
    return r.suppress|(r.replay<<1)|(r.screenshot<<2);
}
API void t_features(unsigned lift,float *out) { knuckle_features_finish(&knuckle.features,lift,out); }
API void t_feature_add(unsigned z,unsigned area,unsigned now) { knuckle_features_add(&knuckle.features,z,area,now); }
API void t_special(int route,int tp_mode,int mask,int confidence,int x,int y,unsigned now,int online) {
    int current_mode=route,current_tp_mode=tp_mode,active_finger_count=0;
    bool publish=online!=0,published_pair=false;
    uint32_t report_generation=0,report_time_ms=now;
    struct { unsigned time_ms,read_done_us,generation; } frame={now,now*1000,0};
    knuckle_result_t knock={0};
    uint16_t last_raw_x[5]={0},last_raw_y[5]={0},slot_mouse_x[5]={0},slot_mouse_y[5]={0};
    tp_multi_msg_t tp_msg={.scan_time=(uint16_t)(now*10)};
    for(unsigned i=0;i<5;++i) if(mask&(1<<i)) {
        tp_msg.fingers[i]=(tp_finger_t){.x=x,.y=y,.tip_switch=1,.confidence=(confidence>>i)&1,.contact_id=i};
        last_raw_x[i]=slot_mouse_x[i]=x;last_raw_y[i]=slot_mouse_y[i]=y;
        ++active_finger_count;
    }
    /* PRODUCTION_HANDOFF */
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
                 'SYS/report_buffer.c','SYS/knuckle_gesture.c','SYS/edge_gesture.c',
                 'SYS/point_gesture.c','USB/usb_pump_state.h']
        code=SUPPORT+'\n'.join('#include "'+(MAIN/s).as_posix()+'"' for s in sources)+WRAPPERS
        parser=(MAIN/'I2C/TP/i2c_queue.c').read_text()
        start=parser.index('                bool ble_custom_gestures = false;')
        end=parser.index('                if (!publish && all_up)',start)
        code+=SPECIAL_WRAPPERS.replace('    /* PRODUCTION_HANDOFF */',parser[start:end])
        (root/'harness.c').write_text(code)
        dll=root/'touch.dll'
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib',
            '-fuse-ld=lld','-fno-stack-protector','-fno-builtin','-Xlinker','/noentry',
            '-Wall','-Wextra','-Werror','-I',str(root),'-I',str(MAIN),str(root/'harness.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
        cls.lib.t_motion.argtypes=[C.c_float,C.c_float,C.c_uint,C.POINTER(Step)]
        cls.lib.t_mouse.argtypes=[C.c_int]*5+[C.POINTER(Mouse)]
        cls.lib.t_mouse_conf.argtypes=[C.c_int]*6+[C.POINTER(Mouse)]
        cls.lib.t_inside.argtypes=[C.c_uint]*8
        cls.lib.t_knuckle_packet.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.c_int]
        cls.lib.t_features.argtypes=[C.c_uint,C.POINTER(C.c_float)]
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
        self.assertGreater(sum(values),30)
        self.assertLess(sum(values),40)
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

    def test_pointer_gain_is_fifty_percent_of_recorded_baseline(self):
        # Recorded from production before the speed change: 51 samples, 10 ms.
        baseline=[(1,0,34,0),(5,0,216,0),(20,0,1741,0),
                  (1,1,34,34),(5,-5,246,-246),(-20,10,-1878,939)]
        for dx,dy,bx,by in baseline:
            with self.subTest(dx=dx,dy=dy):
                self.lib.t_reset()
                out=[self.mouse(x=1100+dx*i,y=700+dy*i,t=i*100) for i in range(51)]
                self.assertAlmostEqual(sum(r.x for r in out),bx*.5,delta=1)
                self.assertAlmostEqual(sum(r.y for r in out),by*.5,delta=1)

    def confidence_mouse(self,mask,confidence,x=500,t=0,buttons=0):
        out=Mouse();self.lib.t_mouse_conf(mask,confidence,x,500,t,buttons,C.byref(out));return out

    def test_confidence_loss_never_becomes_a_tap(self):
        for mask in (1,3):
            with self.subTest(mask=mask):
                self.lib.t_reset();self.mouse(mask=mask,t=100);self.mouse(mask=mask,t=200)
                out=self.confidence_mouse(mask,0,t=300)
                self.assertEqual((out.buttons,out.x,out.y,out.wheel),(0,0,0,0))
                self.assertFalse(self.lib.t_tap_release())
                self.assertEqual(self.mouse(mask=0,t=400).buttons,0)
                self.assertFalse(self.lib.t_tap_release())

    def test_confidence_recovery_reanchors_and_only_new_contact_can_tap(self):
        self.mouse(t=100);self.mouse(t=200)
        self.confidence_mouse(1,0,x=1000,t=300)
        self.assertEqual(self.mouse(x=1200,t=400).x,0)
        self.assertEqual(self.mouse(x=1220,t=500).x,0)
        self.assertGreater(self.mouse(x=1230,t=600).x,0)
        self.assertEqual(self.mouse(mask=0,t=700).buttons,0)
        self.mouse(t=800);self.mouse(t=900)
        self.assertEqual(self.mouse(mask=0,t=1000).buttons,1)

    def test_confidence_loss_releases_virtual_drag_but_keeps_physical_button(self):
        self.mouse(t=100);self.mouse(t=200);self.mouse(mask=0,t=300);self.lib.t_tap_release()
        self.mouse(t=400);self.mouse(t=500)
        self.assertEqual(self.mouse(x=540,t=600).buttons,1)
        self.assertEqual(self.confidence_mouse(1,0,t=700).buttons,0)
        self.assertEqual(self.confidence_mouse(1,0,t=800,buttons=4).buttons,4)
        self.assertEqual(self.mouse(mask=0,t=900).buttons,0)

    def test_middle_drag_contact_replacement_reanchors(self):
        for i in range(5): self.mouse(mask=3,x=500+i*10,t=i*100,buttons=4)
        out=self.mouse(mask=5,x=700,t=500,buttons=4)
        self.assertEqual((out.x,out.y,out.buttons),(0,0,4))
        self.assertGreater(self.mouse(mask=5,x=710,t=600,buttons=4).x,0)

    def test_saturated_motion_is_bounded_and_reset_discards_remainder(self):
        out=[self.mouse(x=100+250*i,t=i*100) for i in range(8)]
        self.assertTrue(all(-127<=r.x<=127 for r in out))
        self.mouse(mask=0,t=800)
        self.assertEqual(self.mouse(x=200,t=900).x,0)
        self.assertEqual(self.mouse(x=200,t=1000).x,0)

    def special(self,x,y,t,mask=1,confidence=None,route=2,tp_mode=0,online=1):
        if confidence is None: confidence=mask
        self.lib.t_special(route,tp_mode,mask,confidence,x,y,t,online)
        result=[]
        while (r:=self.take(t)) is not None: result.append(r)
        return result

    def edge_config(self,e,rotation=0,repeat=0):
        self.lib.t_config(5,rotation);self.lib.t_config(7,repeat<<e)
        for at,v in enumerate((1,1,0,5,1)): self.lib.t_config(12+5*e+at,v)

    def point_config(self,p,rotation=0,convert=False):
        self.lib.t_config(5,rotation);self.lib.t_config(7,0x10<<p)
        self.lib.t_config(6,(2<<p) if convert else 0)
        for at,v in enumerate((1,13,5,1)): self.lib.t_config(32+4*p+at,v)

    def test_edge_tap_replayed_once_for_usb_and_ble_and_seeds_drag(self):
        for route in (0,2):
            with self.subTest(route=route):
                self.lib.t_reset();self.edge_config(0)
                self.special(500,10,10,route=route)
                self.special(500,10,20,route=route)
                reports=self.special(500,10,30,mask=0,route=route)
                self.assertEqual([r[1] for r in reports],[1,0])
                self.assertTrue(all(r[0]==0 and r[4]==0 for r in reports))
                self.assertTrue(all(r[1]==0 for r in self.special(500,10,40,mask=0,route=route)))
                self.mouse(t=500);self.mouse(t=600)
                self.assertEqual(self.mouse(x=540,t=700).buttons,1)

    def test_failed_edge_tap_publish_does_not_seed_double_drag(self):
        self.edge_config(0)
        self.special(500,10,10);self.special(500,10,20)
        self.lib.t_fail_publish()
        self.assertEqual(self.special(500,10,30,mask=0),[])
        self.mouse(t=500);self.mouse(t=600)
        self.assertEqual(self.mouse(x=540,t=700).buttons,0)

    def test_offline_edge_tap_does_not_seed_double_drag(self):
        self.edge_config(0)
        self.special(500,10,10);self.special(500,10,20)
        self.assertEqual(self.special(500,10,30,mask=0,online=0),[])
        self.mouse(t=500);self.mouse(t=600)
        self.assertEqual(self.mouse(x=540,t=700).buttons,0)

    def test_edges_all_rotations_steps_cancel_and_no_mouse_click(self):
        for rotation in range(4):
            xm,ym=(1532,2302) if rotation&1 else (2302,1532)
            for e in range(4):
                with self.subTest(rotation=rotation,edge=e):
                    self.lib.t_reset();self.edge_config(e,rotation)
                    x,y=(xm//2,10 if e==0 else ym-10) if e<2 else (10 if e==2 else xm-10,ym//2)
                    self.special(x,y,10)
                    x+=60 if e<2 else 0;y-=60 if e>=2 else 0
                    out=self.special(x,y,20)
                    self.assertEqual(self.lib.t_aux_count(),1)
                    self.assertTrue(all(r[0]==0 and r[1]==0 for r in out))
                    out=self.special(x,y,30,mask=0)
                    self.assertTrue(all(r[1]==0 for r in out))
                    self.assertEqual(self.lib.t_cancel_count(),1)

    def test_edge_reverse_remainder_and_continue_outside(self):
        self.edge_config(0,repeat=1)
        self.special(500,10,10);self.special(530,10,20)
        self.assertEqual(self.lib.t_aux_count(),1)
        self.special(535,500,30)  # Continue outside, insufficient new travel.
        self.assertEqual(self.lib.t_aux_count(),1)
        self.special(495,500,40)
        self.assertEqual(self.lib.t_aux_count(),2)
        self.assertEqual(self.lib.t_cancel_count(),0)
        self.special(495,500,50,mask=0)
        self.assertEqual(self.lib.t_cancel_count(),1)

    def test_edge_confidence_loss_cancels_without_tap(self):
        self.edge_config(0)
        self.special(500,10,10);self.special(550,10,20)
        self.special(550,10,30,confidence=0)
        self.assertEqual(self.lib.t_cancel_count(),1)
        self.special(560,10,40)
        self.assertEqual(self.lib.t_aux_count(),1)
        self.assertTrue(all(r[1]==0 for r in self.special(560,10,50,mask=0)))

    def test_corners_all_rotations_repeat_and_cancel(self):
        for rotation in range(4):
            xm,ym=(1532,2302) if rotation&1 else (2302,1532)
            for p in range(4):
                with self.subTest(rotation=rotation,point=p):
                    self.lib.t_reset();self.point_config(p,rotation)
                    x,y=(xm if p&1 else 0),(ym if p&2 else 0)
                    self.special(x,y,10)
                    self.assertEqual(self.lib.t_aux_count(),1)
                    self.special(x,y,609);self.assertEqual(self.lib.t_aux_count(),1)
                    self.special(x,y,610);self.assertEqual(self.lib.t_aux_count(),2)
                    self.special(x,y,810);self.assertEqual(self.lib.t_aux_count(),3)
                    self.special(x,y,820,confidence=0)
                    self.assertEqual(self.lib.t_cancel_count(),1)
                    self.special(x,y,1100)
                    self.assertEqual(self.lib.t_aux_count(),3)
                    self.assertTrue(all(r[1]==0 for r in self.special(x,y,1110,mask=0)))

    def test_corner_to_edge_handoff_has_no_mouse_tap(self):
        self.point_config(0,convert=True);self.edge_config(0)
        self.special(0,0,10);self.assertEqual(self.lib.t_aux_count(),1)
        self.special(30,0,20);self.assertEqual(self.lib.t_cancel_count(),1)
        self.special(60,0,30);self.assertEqual(self.lib.t_aux_count(),2)
        self.assertTrue(all(r[1]==0 for r in self.special(60,0,40,mask=0)))

    def test_corner_geometry_matches_original_circle_including_boundary(self):
        rng=random.Random(42)
        for rotation in range(4):
            xm,ym,w,h=(1532,2302,766,1149) if rotation&1 else (2302,1532,1149,766)
            for p in range(4):
                for radius in (1,5,15,30):
                    # Include rectangle/circle boundaries plus random near-corner samples.
                    points=[(0,0),(xm,ym),(xm+1,0),(0,ym+1)]
                    for _ in range(100):
                        dx=rng.randrange(xm//3);dy=rng.randrange(ym//3)
                        points.append((xm-dx if p&1 else dx,ym-dy if p&2 else dy))
                    for x,y in points:
                        dx=(xm-x if p&1 else x)*w/xm
                        dy=(ym-y if p&2 else y)*h/ym
                        r=min(w,h)*radius/100
                        expected=x<=xm and y<=ym and dx*dx+dy*dy<=r*r
                        self.assertEqual(bool(self.lib.t_inside(p,radius,x,y,xm,ym,w,h)),expected)
        self.assertEqual(self.lib.t_inside(0,5,0,50,1000,1000,1000,1000),1)
        self.assertEqual(self.lib.t_inside(0,5,1,50,1000,1000,1000,1000),0)
        self.assertEqual(self.lib.t_inside(0,5,0,0,0,1000,1000,1000),0)

    def test_knuckle_features_keep_weighted_statistics(self):
        samples=[(45,4,100),(80,9,110),(30,4,120)]
        for z,a,t in samples:self.lib.t_feature_add(z,a,t)
        out=(C.c_float*6)();self.lib.t_features(130,out)
        total=sum(z for z,a,t in samples)
        mean=sum(z*a for z,a,t in samples)/total
        variance=sum(z*a*a for z,a,t in samples)/total-mean*mean
        expected=[80,mean,30,10,3.5,math.sqrt(variance)]
        for a,b in zip(out,expected):self.assertAlmostEqual(a,b,places=5)

    def knuckle_packet(self,t,mask=1,x=500,z=45,area=2,v2=1,buffered=0):
        packet=(C.c_uint8*64)();packet[0]=0x40
        for i in range(5):
            if mask&(1<<i):
                at=4+8*i;packet[at]=1;packet[at+1]=x&255;packet[at+2]=x>>8
                packet[at+3]=200;packet[at+5]=z;packet[at+6]=packet[at+7]=area
        return self.lib.t_knuckle_packet(packet,t,buffered,v2)

    def test_knuckle_v1_v2_double_tap_and_rejection_boundaries(self):
        for v2 in (0,1):
            with self.subTest(v2=v2):
                self.lib.t_reset()
                self.assertEqual(self.knuckle_packet(0,v2=v2),1)
                self.assertEqual(self.knuckle_packet(10,v2=v2),1)
                self.assertEqual(self.knuckle_packet(20,mask=0,v2=v2)&4,0)
                self.knuckle_packet(100,v2=v2);self.knuckle_packet(110,v2=v2)
                self.assertEqual(self.knuckle_packet(120,mask=0,v2=v2)&4,4)
                self.assertEqual(self.knuckle_packet(130,mask=0,v2=v2),0)
                for bad in ({'mask':3},{'mask':2},{'x':513},{'area':0},{'buffered':16}):
                    self.lib.t_reset();self.knuckle_packet(0,v2=v2)
                    self.assertEqual(self.knuckle_packet(10,v2=v2,**bad),2)
                    self.assertEqual(self.knuckle_packet(20,mask=0,v2=v2),0)

    def test_knuckle_time_wrap_gap_and_reset_cancel_pair(self):
        for start in (0,0xfffffff0):
            self.lib.t_reset()
            self.knuckle_packet(start)
            self.knuckle_packet((start+10)&0xffffffff)
            self.knuckle_packet((start+20)&0xffffffff,mask=0)
            self.knuckle_packet((start+100)&0xffffffff)
            self.knuckle_packet((start+110)&0xffffffff)
            self.assertEqual(self.knuckle_packet((start+120)&0xffffffff,mask=0)&4,4)
        self.lib.t_reset();self.knuckle_packet(0)
        self.assertEqual(self.knuckle_packet(61),2)
        self.assertEqual(self.knuckle_packet(70,mask=0),0)
        self.lib.t_reset();self.knuckle_packet(0);self.knuckle_packet(10);self.knuckle_packet(20,mask=0)
        self.lib.t_reset()  # Same reset used at connection/mode recovery.
        self.knuckle_packet(100);self.knuckle_packet(110)
        self.assertEqual(self.knuckle_packet(120,mask=0)&4,0)

if __name__=='__main__':unittest.main()
