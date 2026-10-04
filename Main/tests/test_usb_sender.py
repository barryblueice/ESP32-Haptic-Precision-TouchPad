"""Execute production USB pump/completion bodies with deterministic transport mocks."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'main'

def function(text,signature):
    start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

MOCK=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "SYS/report_buffer.h"
#include "USB/usb_pump_state.h"
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
typedef struct { uint8_t id,data[16];unsigned length; } usb_aux_report_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(...) ((void)0)
#define VBUS_DET_GPIO 1
#define REPORTID_TOUCHPAD 1
#define REPORTID_MOUSE 2
#define INPUT_DIAG_USB_FLIGHT 0
#define INPUT_DIAG_COMPLETE 1
#define INPUT_DIAG_SUBMIT 2
static bool ready,accepted,link_up=true,request_on_take;
static unsigned takes,submits,acks,failed,flights,notifies,recoveries;
static bool have_source;
static input_report_t source;
static unsigned gen=1;
void *memset(void *p,int c,size_t n) { unsigned char *s=p;while(n--)*s++=(unsigned char)c;return p; }
void *memcpy(void *p,const void *q,size_t n) { unsigned char *s=p;const unsigned char *t=q;while(n--)*s++=*t++;return p; }
static void connection_lock(void) { }
static void connection_unlock(void) { }
static bool connection_selected(int m) { (void)m;return link_up; }
static bool connection_can_send(int m) { (void)m;return link_up; }
static unsigned connection_epoch(void) { return gen; }
static void connection_flight(int m,unsigned e,bool active) { (void)m;(void)e;flights+=active?1:-1; }
static int gpio_get_level(int p) { (void)p;return 1; }
static bool tud_suspended(void) { return false; }
static bool tud_mounted(void) { return link_up; }
static bool tud_remote_wakeup(void) { return false; }
static bool tud_hid_n_ready(unsigned i) { (void)i;return ready; }
static bool tud_hid_n_report(unsigned i,unsigned id,const void *p,unsigned n) { (void)i;(void)id;(void)p;(void)n;++submits;return accepted; }
static void usb_config_send(void) { }
static void usb_config_complete(bool s) { (void)s; }
static bool usb_aux_release_pending(void) { return false; }
static bool usb_aux_take(usb_aux_report_t *r,unsigned g,unsigned t) { (void)r;(void)g;(void)t;return false; }
static bool usb_aux_report_current(usb_aux_report_t *r) { (void)r;return true; }
static void usb_aux_unsubmitted(void) { }
static void usb_aux_complete(bool s) { (void)s; }
static unsigned input_generation(void) { return gen; }
static bool input_report_current(const input_report_t *r) { return r->generation==gen; }
static void input_report_submitted(const input_report_t *r) { (void)r; }
static void input_report_ack(const input_report_t *r) { (void)r;++acks; }
static void input_submit_failed(void) { ++failed; }
static void input_recover(void) { ++recoveries;++gen; }
static unsigned input_diag_now(void) { return 1; }
static void input_diag_sample(int s,unsigned t) { (void)s;(void)t; }
static int64_t esp_timer_get_time(void) { return 1000; }
static void xTaskNotifyGive(TaskHandle_t t) { (void)t;++notifies; }
static void input_wake_sender(void);
static bool input_take_report(input_report_t *r);
"""
EXPORTS=r"""
static void input_wake_sender(void) { usbhid_notify_sender(usb_sender_task); }
static bool input_take_report(input_report_t *r) {
    ++takes;
    if(request_on_take) { request_on_take=false; input_wake_sender(); }
    if(!have_source)return false;*r=source;have_source=false;return true;
}
__declspec(dllexport) void reset(void) {
    ready=accepted=link_up=true;request_on_take=false;have_source=true;gen=1;
    takes=submits=acks=failed=flights=notifies=recoveries=0;
    memset(usb_busy,0,sizeof(usb_busy));memset(&pump_state,0,sizeof(pump_state));
    usb_epoch=1;usb_configured=true;usb_have_pending=false;usb_aux_flight=false;
    usb_sender_task=(void *)1;
    source=(input_report_t){.mode=MOUSE_MODE,.generation=1};
    source.data.mouse.x=7;
}
__declspec(dllexport) void set(int option,int value) {
    if(option==0)ready=value;else if(option==1)accepted=value;
    else if(option==2)request_on_take=value;else if(option==3)gen=(unsigned)value;
    else if(option==4)link_up=value;
}
__declspec(dllexport) void pump(void) { usb_pump_schedule(&pump_state);usb_send_pump(NULL); }
__declspec(dllexport) void complete(int success) { usb_complete(2,success!=0); }
__declspec(dllexport) unsigned stat(int i) {
    unsigned v[]={takes,submits,acks,failed,flights,notifies,recoveries,usb_have_pending,usb_busy[2]};return v[i];
}
"""

class UsbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp=tempfile.TemporaryDirectory(prefix='usb_sender_');root=Path(cls.tmp.name)
        stubs={'stdio.h':'','driver/i2c_master.h':'','freertos/FreeRTOS.h':'','freertos/queue.h':'','esp_err.h':'typedef int esp_err_t;'}
        for name,text in stubs.items():
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
        text=(MAIN/'USB/usbhid.c').read_text()
        globals=text[text.index('static uint8_t ptp_input_mode'):text.index('/* All pipeline wakes')]
        funcs=[function(text,s) for s in ('bool usbhid_notify_sender(', 'static void usb_complete(', 'static void usb_send_pump(')]
        # Only peripheral/RTOS calls are mocked. Function bodies are taken verbatim
        # so tests exercise pending/in-flight ownership and actual submission paths.
        (root/'usb.c').write_text(MOCK+globals+'\n'.join(funcs)+EXPORTS)
        dll=root/'usb.dll'
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-fno-stack-protector','-fno-builtin','-Xlinker','/noentry','-Wall','-Wextra','-Werror',
            '-Wno-unused-variable','-I',str(root),'-I',str(MAIN),str(root/'usb.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle);cls.tmp.cleanup()
    def setUp(self):self.lib.reset()
    def stat(self,i):return self.lib.stat(i)
    def test_endpoint_not_ready_retains_pending_and_retries(self):
        self.lib.set(0,0);self.lib.pump()
        self.assertEqual((self.stat(0),self.stat(1),self.stat(7)),(1,0,1))
        self.lib.set(0,1);self.lib.pump()
        self.assertEqual((self.stat(0),self.stat(1),self.stat(7),self.stat(8)),(1,1,0,1))
    def test_rejected_submission_keeps_report_until_accepted(self):
        self.lib.set(1,0);self.lib.pump()
        self.assertEqual((self.stat(3),self.stat(7),self.stat(8)),(1,1,0))
        self.lib.set(1,1);self.lib.pump();self.lib.complete(1)
        self.assertEqual((self.stat(0),self.stat(1),self.stat(2),self.stat(4)),(1,2,1,0))
    def test_in_flight_report_blocks_second_take_until_completion(self):
        self.lib.pump();self.lib.pump()
        self.assertEqual((self.stat(0),self.stat(1),self.stat(4)),(1,1,1))
        self.lib.complete(1)
        self.assertEqual((self.stat(2),self.stat(4),self.stat(8)),(1,0,0))
        self.assertGreater(self.stat(5),0)
    def test_arrival_inside_pump_gets_followup_wake(self):
        self.lib.set(2,1);self.lib.pump()
        self.assertEqual(self.stat(5),2)  # Input wake plus pump's follow-up wake.
        self.lib.pump();self.assertEqual(self.stat(5),2)  # Busy must not spin.
    def test_failed_completion_enters_recovery(self):
        self.lib.pump();self.lib.complete(0)
        self.assertEqual((self.stat(2),self.stat(3),self.stat(6),self.stat(8)),(0,1,1,0))
    def test_mode_generation_invalidates_unsubmitted_report(self):
        self.lib.set(0,0);self.lib.pump();self.lib.set(3,2)
        self.lib.set(0,1);self.lib.pump()
        self.assertEqual((self.stat(1),self.stat(7)),(0,0))
    def test_old_route_completion_does_not_ack_new_session(self):
        self.lib.pump();self.lib.set(3,2);self.lib.complete(1)
        self.assertEqual(self.stat(2),0)

if __name__=='__main__':unittest.main()
