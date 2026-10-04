"""Replay the production input pipeline across USB suspend/resume and faults."""
import ctypes as C
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'main'
MOCK=r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef int portMUX_TYPE;
typedef int esp_err_t;
#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define pdPASS 1
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_ERROR_CHECK(e) ((void)(e))
#define ESP_LOGI(...) mock_log(__VA_ARGS__)
#define ESP_LOGW(...) mock_log(__VA_ARGS__)
#define CFG_FEATURE_FLAGS 0
#define CFG_FLAG_CUSTOM_GESTURE_HAPTICS 1
#include "pipeline_types.h"
extern QueueHandle_t tp_data_queue;
void mock_log(const char *,const char *,...);
int64_t esp_timer_get_time(void);
void xTaskNotifyGive(TaskHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
void cs40l25_surface_cancel_click(void);
void cs40l25_surface_button_update(bool,uint8_t);
void cs40l25_surface_gesture(bool);
unsigned cs40l25_surface_get_state(void);
uint8_t ptp_haptic_click_intensity_get(void);
uint8_t device_config_value(unsigned);
esp_err_t touchpad_mode_set(bool);
const char *esp_err_to_name(int);
enum {INPUT_DIAG_RAW_WAIT};
static inline uint32_t input_diag_now(void){return 0;}
static inline void input_diag_sample(int s,uint32_t n){(void)s;(void)n;}
static inline void input_diag_depth(unsigned r,unsigned o){(void)r;(void)o;}
static inline void input_diag_recovery(const char *r){(void)r;}
static inline void input_diag_capture(uint32_t g,bool a,uint32_t t){(void)g;(void)a;(void)t;}
'''
SUPPORT=r'''
int32_t current_mode=WIRED_MODE;
uint8_t current_tp_mode=PTP_MODE;
QueueHandle_t tp_data_queue;
static uint32_t clock_ms;
static input_frame_t raw_queue[16];
static unsigned raw_count;
void *memset(void *p,int c,size_t n){unsigned char *s=p;while(n--)*s++=(unsigned char)c;return p;}
void *memcpy(void *p,const void *q,size_t n){unsigned char *s=p;const unsigned char *t=q;while(n--)*s++=*t++;return p;}
void *memmove(void *p,const void *q,size_t n){unsigned char *s=p;const unsigned char *t=q;if(s<t){for(size_t i=0;i<n;++i)s[i]=t[i];}else{while(n){--n;s[n]=t[n];}}return p;}
void mock_log(const char *t,const char *f,...){(void)t;(void)f;}
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
void xTaskNotifyGive(TaskHandle_t h){(void)h;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)1;}
QueueHandle_t xQueueCreate(unsigned n,unsigned s){(void)n;(void)s;raw_count=0;return (void *)1;}
int xQueueSend(QueueHandle_t h,const void *p,unsigned t){(void)h;(void)t;if(raw_count==16)return 0;raw_queue[raw_count++]=*(const input_frame_t *)p;return 1;}
int xQueueReceive(QueueHandle_t h,void *p,unsigned t){(void)h;(void)t;if(!raw_count)return 0;*(input_frame_t *)p=raw_queue[0];--raw_count;memmove(raw_queue,raw_queue+1,raw_count*sizeof(*raw_queue));return 1;}
void cs40l25_surface_cancel_click(void){}
void cs40l25_surface_button_update(bool b,uint8_t s){(void)b;(void)s;}
void cs40l25_surface_gesture(bool p){(void)p;}
unsigned cs40l25_surface_get_state(void){return 1;}
uint8_t ptp_haptic_click_intensity_get(void){return 50;}
uint8_t device_config_value(unsigned v){(void)v;return 0;}
esp_err_t touchpad_mode_set(bool b){(void)b;return ESP_OK;}
const char *esp_err_to_name(int e){(void)e;return "mock";}
'''
WRAPPERS=r'''
#define API __declspec(dllexport)
/* Drive normal capture/observe/publish/ack boundaries, with no gesture filtering. */
API int frame(int down,int send) {
    clock_ms+=10;
    uint8_t raw[64]={64,0,12,0};raw[4]=down?1:0;
    if(!input_capture(raw,true,input_source_generation(),input_generation(),clock_ms))return 0;
    input_frame_t f;if(!input_next_frame(&f))return 0;
    bool local=input_source_observe(f.generation,!down);
    bool host=input_observe(f.output_generation,!down);
    if(!local||!host)return 0;
    input_report_t r={.mode=reports.mode,.time_ms=clock_ms};
    if(r.mode==PTP_MODE){r.data.ptp.contact_count=1;r.data.ptp.fingers[0].tip_conf_id=down?3:1;}
    else r.data.mouse.x=down?1:0;
    if(!input_publish(f.output_generation,&r,false))return 0;
    if(send && input_take_report(&r)){input_report_submitted(&r);input_report_ack(&r);}
    return 1;
}
API void reset(int mode){
    memset(&reports,0,sizeof(reports));mode_pending=false;mode_applied=false;
    clock_ms=100;current_mode=WIRED_MODE;current_tp_mode=(uint8_t)mode;
    input_pipeline_init();input_transport_start(WIRED_MODE,(uint8_t)mode,true,true);
    input_apply_mode_request();
    input_report_t r;while(input_take_report(&r)){input_report_submitted(&r);input_report_ack(&r);}
    frame(0,1);
}
API void power(int suspended){input_usb_suspend(suspended!=0);}
API void link(int up){input_usb_link(up!=0);}
API void bus_reset(void){input_usb_reset();}
API void fault(int which){
    if(which==0)input_source_recover("read_fail");
    else if(which==1)input_recover();
    else if(which==2)input_request_mode(reports.mode==PTP_MODE?MOUSE_MODE:PTP_MODE);
    else if(which==3)input_transport_quiesce();
    else if(which==4){uint8_t b[64]={64};input_capture(b,false,input_source_generation(),input_generation(),clock_ms);}
    else if(which==5){for(int i=0;i<17;++i){uint8_t b[64]={64,0,12};b[4]=1;input_capture(b,true,input_source_generation(),input_generation(),clock_ms);}}
}
API int gates(void){return source_wait_up|(output_wait_up<<1)|(reports.recovering<<2);}
API void ack_releases(void){input_report_t r;while(input_take_report(&r)){input_report_submitted(&r);input_report_ack(&r);}}
API unsigned gen(void){return input_source_generation();}
API int observe_old(unsigned generation){return input_source_observe(generation,false);}
'''

class UsbResumeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc=os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp=tempfile.TemporaryDirectory(prefix='usb_resume_');root=Path(cls.tmp.name)
        source=(MAIN/'SYS/input_pipeline.c').read_text(encoding='utf-8')
        (root/'input_pipeline.c').write_text(source,encoding='utf-8')
        for name in re.findall(r'^#include "([^"]+)"',source,re.M):
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "mock.h"\n')
        for src,dest in [('SYS/input_pipeline.h','pipeline_types.h'),('SYS/report_buffer.h','report_buffer.h'),('SYS/hid_msg.h','hid_msg.h'),('SYS/report_buffer.c','report_buffer.c'),('I2C/SUB_DEV/surface_haptic_settings.h','I2C/SUB_DEV/surface_haptic_settings.h')]:
            p=root/dest;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((MAIN/src).read_bytes())
        for name in ['stdio.h','esp_err.h','driver/i2c_master.h','freertos/FreeRTOS.h','freertos/queue.h','freertos/task.h']:
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#pragma once\n#include <stdint.h>\n')
        (root/'mock.h').write_text(MOCK)
        (root/'harness.c').write_text('#include "mock.h"\n'+SUPPORT+'\n#include "input_pipeline.c"\n#include "report_buffer.c"\n'+WRAPPERS)
        dll=root/'resume.dll'
        subprocess.run([cc,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld','-fno-stack-protector','-fno-builtin','-Xlinker','/noentry','-Wall','-Wextra','-Werror','-I',str(root),str(root/'harness.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle);cls.tmp.cleanup()
    def setUp(self):self.lib.reset(1)
    def test_waking_finger_continues_without_lift_in_ptp_and_mouse(self):
        for mode in (0,1):
            with self.subTest(mode=mode):
                self.lib.reset(mode);self.lib.power(1)
                self.assertEqual(self.lib.frame(1,1),0)
                self.lib.power(0)
                self.assertEqual(self.lib.gates(),0)
                self.assertEqual(self.lib.frame(1,1),1)
    def test_touch_after_resume_and_duplicate_events(self):
        self.lib.power(1);self.lib.power(1);self.lib.power(0);self.lib.power(0)
        self.assertEqual(self.lib.frame(1,1),1)
    def test_duplicate_suspend_after_waking_touch_keeps_token(self):
        self.lib.power(1);self.lib.frame(1,1);self.lib.power(1);self.lib.power(0)
        self.assertEqual(self.lib.frame(1,1),1)
    def test_old_held_contact_requires_lift_then_recovers(self):
        self.assertEqual(self.lib.frame(1,1),1)
        self.lib.power(1);self.lib.power(0)
        self.assertNotEqual(self.lib.gates(),0)
        self.assertEqual(self.lib.frame(1,1),0)
        self.lib.ack_releases();self.lib.frame(0,1)
        self.assertEqual(self.lib.frame(1,1),1)
    def test_faults_and_mode_or_route_changes_cancel_idle_resume(self):
        for fault in range(6):
            with self.subTest(fault=fault):
                self.lib.reset(1);self.lib.power(1);self.lib.frame(1,1)
                self.lib.fault(fault);self.lib.power(0)
                self.assertEqual(self.lib.frame(1,1),0)
    def test_detach_while_already_suspended_requires_lift(self):
        self.lib.power(1);self.lib.frame(1,1);self.lib.link(0);self.lib.link(1)
        self.assertEqual(self.lib.frame(1,1),0)
    def test_reset_while_suspended_requires_lift(self):
        self.lib.power(1);self.lib.frame(1,1);self.lib.bus_reset();self.lib.power(0)
        self.assertEqual(self.lib.frame(1,1),0)
    def test_pending_contact_history_is_not_treated_as_idle(self):
        self.lib.frame(1,0);self.lib.frame(0,0)
        self.lib.power(1);self.lib.frame(1,1);self.lib.power(0)
        self.assertEqual(self.lib.frame(1,1),0)
    def test_resume_retires_pre_resume_source_generation(self):
        self.lib.power(1);generation=self.lib.gen();self.lib.frame(1,1)
        self.lib.power(0)
        self.assertNotEqual(self.lib.gen(),generation)
        self.assertEqual(self.lib.observe_old(generation),0)
        self.assertEqual(self.lib.frame(1,1),1)

if __name__=='__main__':unittest.main()
