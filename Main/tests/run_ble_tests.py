"""Exercise the real BLE sender with deterministic notification completions."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys
from run_host_tests import body,ROOT

runtime=(ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime+='''
typedef int esp_err_t;
typedef uint8_t esp_gatt_if_t;
typedef int portMUX_TYPE;
#define CONFIG_BLE_ENABLE_PTP_MODE 1
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_OK 0
#define ESP_FAIL -1
#define HID_RPT_ID_PTP_IN 1
#define HID_RPT_ID_MOUSE_IN 1
#define HID_REPORT_TYPE_INPUT 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static struct { uint8_t gatt_if; } hidd_le_env;
static unsigned budget, test_link, test_sends, test_acks, test_failures;
static uint32_t generation=1, now;
static uint16_t sent_id, sent_conn;
static uint8_t sent_bytes[8];
static bool send_fails;
static void (*step_hook)(void);
static void ulTaskNotifyTake(int clear,unsigned ticks) { (void)clear;now+=ticks;if(step_hook)step_hook(); }
static int64_t esp_timer_get_time(void) { return (int64_t)now*1000; }
static void input_wake_sender(void) {}
static void input_register_sender(void) {}
static void input_log_stats(void) {}
static void input_set_link(uint8_t mask) { if(mask!=test_link){++generation;test_link=mask;} }
static uint32_t input_generation(void) { return generation; }
static void input_recover(void) { ++generation; }
static void input_submit_failed(void) { ++test_failures; }
'''
code=runtime+'\n'+body(ROOT/'main/SYS/hid_msg.h')+'\n'+body(ROOT/'main/SYS/report_buffer.h')+'\n'
code+='''
static bool input_take_report(input_report_t *out) { (void)out;return false; }
static bool input_report_current(const input_report_t *r) { return r->generation==generation; }
static void input_report_ack(const input_report_t *r) { (void)r;++test_acks; }
'''
code+=body(ROOT/'main/BLE/ble_hid_dev.h')+'\n'+body(ROOT/'main/SYS/aux_output.h')+'\n'
code+='''
uint16_t hid_dev_report_handle(uint8_t id) { return id+100; }
esp_err_t hid_dev_send_report(esp_gatt_if_t gatts,uint16_t conn,uint8_t id,uint8_t type,uint8_t len,uint8_t *data)
{
    (void)gatts;(void)type;++test_sends;sent_id=id;sent_conn=conn;memcpy(sent_bytes,data,len>8?8:len);
    return send_fails?ESP_FAIL:ESP_OK;
}
'''
code+=body(ROOT/'main/SYS/aux_output.c')+'\n'
ble=body(ROOT/'main/BLE/blehid.c')
for name in ('flight','success','done'):ble=re.sub(r'\b'+name+r'\b','ble_'+name,ble)
ble=ble.replace('while (true)','while (budget-- > 0)')
code+=ble+'\n'+(ROOT/'tests/ble_cases.c').read_text(encoding='utf-8')
out=ROOT/'build/ble-host-tests';out.mkdir(parents=True,exist_ok=True)
c=out/'checks.c';dll=out/'checks.dll';c.write_text(code,encoding='utf-8')
subprocess.run([sys.argv[1],'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe',
    '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
    '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)],check=True)
lib=ctypes.CDLL(str(dll));names=re.findall(r'EXPORT int (check_\w+)\(void\)',code)
for name in names:
    line=getattr(lib,name)()
    if line:raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name+': passed')
(out/'result.json').write_text(json.dumps({'passed':True,'cases':names},indent=2)+'\n',encoding='utf-8')
