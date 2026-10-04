"""Run the production reader and pressure forwarder together on a mocked bus."""
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
MOCK=r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_SURFACE_FORCE_FORWARD_ENABLE 1
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define I2C_ADDR_BIT_LEN_7 0
#define I2C_FREQ_HZ 400000
#define REPORT_MAX_AGE_MS 100U
#define TP_INT_GPIO 4
#define IRAM_ATTR
#define GPIO_MODE_INPUT 0
#define GPIO_INTR_DISABLE 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_NEGEDGE 1
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(ms) ((ms)/10)
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define portYIELD_FROM_ISR() ((void)0)
#define ESP_ERROR_CHECK(x) ((void)(x))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) mock_log(__VA_ARGS__)
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef void *esp_timer_handle_t;
typedef struct { uint8_t bytes[64];uint32_t generation,output_generation,time_ms,read_done_us; } input_frame_t;
typedef struct { int dev_addr_length;unsigned device_address,scl_speed_hz; } i2c_device_config_t;
typedef struct { void(*callback)(void *);const char *name; } esp_timer_create_args_t;
typedef struct { uint64_t pin_bit_mask;int mode,intr_type,pull_up_en,pull_down_en; } gpio_config_t;
extern i2c_master_dev_handle_t dev_handle;
extern esp_timer_handle_t timeout_watchdog_timer;
void mock_log(const char *,const char *,...);
int gpio_get_level(int);
int gpio_config(const gpio_config_t *);
int gpio_isr_handler_add(int,void(*)(void *),void *);
int gpio_set_intr_type(int,int);
int gpio_intr_enable(int);
void vTaskDelay(unsigned);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t *);
int xTaskCreatePinnedToCore(void(*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *,int);
int esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_start_once(esp_timer_handle_t,uint64_t);
int64_t esp_timer_get_time(void);
void tp_modern_sleep_init(void);
void tp_modern_sleep_record_activity(void);
void tp_modern_sleep_signal_activity_from_isr(void);
bool tp_modern_sleep_is_active(void);
void watchdog_timeout_callback(void *);
bool cs40l25_surface_is_ready(void);
uint32_t input_source_generation(void);
uint32_t input_generation(void);
bool input_force_forward_ready(uint32_t);
bool input_capture(const uint8_t *,bool,uint32_t,uint32_t,uint32_t);
void force_forward_report(const input_frame_t *);
void force_forward_invalidate(void);
int i2c_master_receive(void *,uint8_t *,size_t,int);
int i2c_master_transmit(void *,const uint8_t *,size_t,int);
int i2c_master_transmit_receive(void *,const uint8_t *,size_t,uint8_t *,size_t,int);
int i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **);
const char *esp_err_to_name(int);
enum { INPUT_DIAG_READ, INPUT_DIAG_FORCE };
static inline void input_diag_read_result(const uint8_t *b,bool s) { (void)b;(void)s; }
static inline uint32_t input_diag_now(void) { return 0; }
static inline void input_diag_sample(int stage,uint32_t us) { (void)stage;(void)us; }
"""
C_CODE=r"""
void *memcpy(void *p,const void *q,size_t n) { unsigned char *s=p;const unsigned char *t=q;while(n--)*s++=*t++;return p; }
void *memset(void *p,int c,size_t n) { unsigned char *s=p;while(n--)*s++=(unsigned char)c;return p; }
i2c_master_dev_handle_t dev_handle=(void *)2;
esp_timer_handle_t timeout_watchdog_timer;
static unsigned pending,received,captured,selected,queried,forwarded,delays,timer_us,notifies,order_error;
static unsigned clock_us,generation,cancel_at,force_calls,empty_length,activities;
static bool accept,read_error,sleeping,force_error;
void mock_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
int gpio_get_level(int pin) { (void)pin;return pending?0:1; }
int gpio_config(const gpio_config_t *c) { (void)c;return 0; }
int gpio_isr_handler_add(int p,void(*f)(void *),void *a) { (void)p;(void)f;(void)a;return 0; }
int gpio_set_intr_type(int p,int t) { (void)p;(void)t;return 0; }
int gpio_intr_enable(int p) { (void)p;return 0; }
void vTaskDelay(unsigned n) { delays+=n; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t;++notifies; }
unsigned ulTaskNotifyTake(int c,unsigned t) { (void)c;(void)t;return 0; }
void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t *w) { (void)t;(void)w; }
int xTaskCreatePinnedToCore(void(*f)(void *),const char *n,unsigned z,void *a,unsigned p,TaskHandle_t *h,int c)
{ (void)f;(void)n;(void)z;(void)a;(void)p;(void)c;if(h)*h=(void *)1;return 1; }
int esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *h) { (void)a;*h=(void *)1;return 0; }
int esp_timer_stop(esp_timer_handle_t t) { (void)t;return 0; }
int esp_timer_start_once(esp_timer_handle_t t,uint64_t us) { (void)t;timer_us=(unsigned)us;return 0; }
int64_t esp_timer_get_time(void) { return clock_us; }
void tp_modern_sleep_init(void) { }
void tp_modern_sleep_record_activity(void) { ++activities; }
void tp_modern_sleep_signal_activity_from_isr(void) { }
bool tp_modern_sleep_is_active(void) { return sleeping; }
void watchdog_timeout_callback(void *a) { (void)a; }
bool cs40l25_surface_is_ready(void) { return true; }
uint32_t input_source_generation(void) { return generation; }
uint32_t input_generation(void) { return 1; }
bool input_force_forward_ready(uint32_t g) { return g==generation; }
bool input_capture(const uint8_t *p,bool ok,uint32_t g,uint32_t o,uint32_t t)
{ (void)p;(void)g;(void)o;(void)t;++captured;return ok&&accept&&!empty_length; }
int i2c_master_receive(void *d,uint8_t *p,size_t n,int timeout)
{ (void)d;(void)timeout;--pending;++received;clock_us+=1500;memset(p,0,n);p[0]=64;p[2]=2;if(empty_length){unsigned len=empty_length==1?0:empty_length;p[0]=(uint8_t)len;p[1]=(uint8_t)(len>>8);}return read_error?257:0; }
static int force_result(void) {
    if(captured!=received)++order_error;
    if(++force_calls==cancel_at) { ++generation;force_forward_invalidate(); }
    clock_us+=force_error?10000:500;
    return force_error?257:0;
}
int i2c_master_transmit(void *d,const uint8_t *p,size_t n,int timeout)
{ (void)d;(void)p;(void)timeout;if(n==4)++selected;else ++forwarded;return force_result(); }
int i2c_master_transmit_receive(void *d,const uint8_t *p,size_t n,uint8_t *r,size_t z,int timeout)
{
    (void)d;(void)p;(void)n;(void)timeout;
    static const uint8_t sample[17]={0,0x48,0x3f,0,0,0,0,0,0,0,0,0,0,0,0,0xb8,0xc0};
    memcpy(r,sample,z);++queried;return force_result();
}
int i2c_master_bus_add_device(void *b,const i2c_device_config_t *c,void **d) { (void)b;(void)c;*d=(void *)1;return 0; }
const char *esp_err_to_name(int e) { (void)e;return "mock"; }
"""
EXPORTS=r"""
__declspec(dllexport) void run(unsigned count,int flags,unsigned cancel) {
    pending=count;received=captured=selected=queried=forwarded=delays=timer_us=notifies=order_error=0;
    clock_us=100000;generation=1;force_calls=0;cancel_at=cancel;
    empty_length=(flags&16)?1:(flags&32)?2:(flags&64)?65535:0;activities=0;
    accept=!(flags&1);read_error=flags&2;sleeping=flags&4;force_error=flags&8;
    memset(&state,0,sizeof(state));lifecycle_epoch=0;
    force_forward_init((void *)1,dev_handle);
    tp_drain_pending();
}
__declspec(dllexport) unsigned stat(int i) {
    unsigned values[]={received,captured,selected,queried,forwarded,delays,timer_us,notifies,order_error,pending,activities};return values[i];
}
__declspec(dllexport) void fire_timer(void) { drain_timer_wake(NULL); }
"""

class ReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        cls.tmp=tempfile.TemporaryDirectory(prefix='touch_reader_');root=Path(cls.tmp.name)
        files=['GPIO/irq_tp_int.c','I2C/TP/force_forward.c']
        for file in files:
            text=(MAIN/file).read_text();(root/Path(file).name).write_text(text)
            for name in re.findall(r'^#include "([^"]+)"',text,re.M):
                dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('#include "mock.h"\n')
        for name in ('force_forward_protocol.c','force_forward_protocol.h'):
            (root/name).write_bytes((MAIN/'I2C/TP'/name).read_bytes())
        (root/'mock.h').write_text(MOCK)
        (root/'harness.c').write_text('#include "mock.h"\n'+C_CODE+'\n#include "force_forward_protocol.h"\n#include "force_forward.c"\n#undef TAG\n#include "irq_tp_int.c"\n'+EXPORTS)
        dll=root/'reader.dll'
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-fno-stack-protector','-fno-builtin','-Xlinker','/noentry','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-I',str(root),str(root/'harness.c'),str(root/'force_forward_protocol.c'),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
    @classmethod
    def tearDownClass(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle);cls.tmp.cleanup()
    def stats(self):return [self.lib.stat(i) for i in range(10)]
    def test_publish_precedes_pressure_and_each_read_has_one_forward(self):
        self.lib.run(3,0,0)
        self.assertEqual(self.stats(),[3,3,1,3,3,0,0,0,0,0])
    def test_batch_limit_uses_one_ms_timer_and_callback_only_notifies(self):
        self.lib.run(12,0,0)
        s=self.stats();self.assertEqual(s[:2],[8,8]);self.assertEqual(s[5:7],[0,1000]);self.assertEqual(s[9],4)
        self.lib.fire_timer();self.assertEqual(self.lib.stat(7),1);self.assertEqual(self.lib.stat(0),8)
    def test_rejected_capture_never_forwards(self):
        self.lib.run(3,1,0);self.assertEqual(self.stats()[:5],[3,3,0,0,0])
    def test_empty_reports_end_batch_and_retry_without_pressure_or_activity(self):
        for flag in (16,32,64):
            with self.subTest(flag=flag):
                self.lib.run(12,flag,0)
                self.assertEqual(self.stats(),[1,1,0,0,0,0,10000,0,0,11])
                self.assertEqual(self.lib.stat(10),0)
    def test_empty_report_on_deasserted_int_does_not_schedule_retry(self):
        self.lib.run(1,64,0)
        self.assertEqual(self.lib.stat(6),0)
    def test_valid_report_records_sleep_activity(self):
        self.lib.run(3,0,0)
        self.assertEqual(self.lib.stat(10),3)
    def test_read_failure_retains_ten_ms_backoff(self):
        self.lib.run(3,2,0);self.assertEqual(self.stats()[:6],[1,1,0,0,0,1])
        self.assertEqual(self.lib.stat(10),0)
    def test_pressure_timeout_does_not_stop_following_touch_reads(self):
        self.lib.run(3,8,0);self.assertEqual(self.stats()[:5],[3,3,1,0,0]);self.assertEqual(self.lib.stat(5),0)
    def test_sleep_blocks_pressure(self):
        self.lib.run(2,4,0);self.assertEqual(self.stats()[:5],[2,2,0,0,0])
    def test_lifecycle_change_cancels_remaining_pressure_transactions(self):
        self.lib.run(1,0,1);self.assertEqual(self.stats()[2:5],[1,0,0])
        self.lib.run(1,0,2);self.assertEqual(self.stats()[2:5],[1,1,0])

if __name__=='__main__':unittest.main()
