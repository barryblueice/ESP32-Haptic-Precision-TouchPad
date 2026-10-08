"""Exercise the production settings worker, codec and NVS configuration path."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

runtime=(ROOT/'tests/host_runtime.h').read_text(encoding='utf-8')
runtime=runtime.replace('static bool fail_commit;', 'static bool fail_commit; static unsigned commits;')
runtime=runtime.replace('(void)h;if(fail_commit)', '(void)h;++commits;if(fail_commit)')
runtime+='''
typedef void *TaskHandle_t;
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
static uint32_t now, worker_notifications;
static int64_t esp_timer_get_time(void) { return (int64_t)now*1000; }
static void xTaskNotifyGive(TaskHandle_t t) { (void)t; ++worker_notifications; }
static void ulTaskNotifyTake(int clear, unsigned timeout) { (void)clear;(void)timeout; }
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *out) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;*out=(void *)1;return pdPASS;
}
'''
files=['SYS/rstp_protocol.h','SYS/wireless_extension.h','SYS/rstp_protocol.c',
       'SYS/device_config.c','WIFI/wireless_settings.h','WIFI/wireless_settings.c']
code=runtime+'\n'+'\n'.join(body(ROOT/'main'/f) for f in files)
cases=(ROOT/'tests/wireless_settings_cases.c').read_text(encoding='utf-8')
code+='\n'+cases
out=ROOT/'build/wireless-settings-host-tests';out.mkdir(parents=True,exist_ok=True)
c=out/'checks.c';dll=out/'checks.dll';c.write_text(code,encoding='utf-8')
subprocess.run([sys.argv[1],'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe',
               '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
               '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)],check=True)
lib=ctypes.CDLL(str(dll));names=re.findall(r'EXPORT int (check_\w+)\(void\)',cases)
for name in names:
    line=getattr(lib,name)()
    if line:raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name+': passed')
(out/'result.json').write_text(json.dumps({'passed':True,'cases':names},indent=2)+'\n',encoding='utf-8')
print(f'{len(names)} wireless settings scenarios passed')
