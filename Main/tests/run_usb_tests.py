"""Exercise production USB callbacks/pump, config transport, parser and pipeline."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

base = (ROOT / 'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = (ROOT / 'tests/wireless_haptic_runtime.h').read_text(encoding='utf-8')
code = base + '\n' + runtime.split('/* PRODUCTION_TYPES */')[0]
code = code.replace('static void xTaskNotifyGive(TaskHandle_t task) { (void)task; }',
                    'static unsigned wakes; static void xTaskNotifyGive(TaskHandle_t task) { (void)task; ++wakes; }')
headers = ['SYS/knuckle_model.h','SYS/knuckle_gesture.h', 'SYS/hid_msg.h', 'SYS/report_buffer.h', 'SYS/input_pipeline.h',
           'SYS/rstp_protocol.h', 'SYS/connection.h', 'SYS/connection_policy.h', 'SYS/edge_gesture.h', 'SYS/point_gesture.h',
           'SYS/aux_output.h', 'SYS/wireless_extension.h', 'SYS/wireless_probe.h', 'USB/usb_config.h',
           'I2C/SUB_DEV/surface_haptic_policy.h', 'I2C/SUB_DEV/surface_haptic_runtime.h']
code += '\n'.join(body(ROOT / 'main' / name) for name in headers) + '\n'
tail = runtime.split('/* PRODUCTION_TYPES */')[1]
tail = tail[tail.index('int32_t current_mode;'):]
tail = tail.replace('static int touchpad_mode_set(bool ptp) { (void)ptp;++mode_writes;return fail_mode?ESP_FAIL:ESP_OK; }',
                    'static void (*mode_hook)(void); static int touchpad_mode_set(bool ptp) { (void)ptp;++mode_writes;if(mode_hook)mode_hook();return fail_mode?ESP_FAIL:ESP_OK; }')
code += (ROOT / 'tests/usb_runtime.h').read_text(encoding='utf-8') + '\n' + tail
code += (ROOT / 'tests/connection_runtime.h').read_text(encoding='utf-8') + '\n'
sources = ['SYS/knuckle_gesture.c', 'SYS/rstp_protocol.c', 'SYS/report_buffer.c',
           'I2C/SUB_DEV/surface_haptic_policy.c', 'I2C/SUB_DEV/surface_haptic_runtime.c',
           'SYS/input_pipeline.c', 'SYS/aux_output.c', 'SYS/edge_gesture.c',
           'SYS/point_gesture.c', 'SYS/connection_policy.c', 'SYS/connection.c', 'I2C/TP/tp_coordinates.h', 'I2C/TP/tp_report_handle.c',
           'I2C/TP/ptp_simulated_mouse_gesture.c', 'USB/usb_aux.h']
for name in sources:
    code += '\n#undef TAG\n' + body(ROOT / 'main' / name)
code += '\n#undef TAG\n' + body(ROOT / 'main/I2C/TP/i2c_queue.c').replace(
    'while (1) {', 'while (parser_budget-- > 0) {')
code += '\n#undef TAG\n' + body(ROOT / 'main/USB/usb_config.c').replace(
    'while (true)', 'while (config_budget-- > 0)')
usb = body(ROOT / 'main/USB/usbhid.c')
# Hardware install/descriptors are covered by the IDF build. Keep all actual
# callbacks, reset observer, sender scheduler and deferred submission code.
code += '\n#undef TAG\n#define TAG "USB_TEST"\n' + usb[usb.index('static uint8_t ptp_input_mode'):usb.index('void usbhid_init')]
code += usb[usb.index('static void usb_send_pump'):].replace('while (true)', 'while (usb_budget-- > 0)')
wifi = body(ROOT / 'main/WIFI/wifi_handle.c')
code += '\n' + wifi[wifi.index('static portMUX_TYPE send_lock'):wifi.index('void wireless_wifi_init')]
code += wifi[wifi.index('void wifi_send_task'):].replace('while (true)', 'while (wifi_budget-- > 0)')
code += '''
typedef struct { const uint8_t *src_addr; } esp_now_recv_info_t;
static void wireless_settings_receive(const uint8_t *mac,const uint8_t *data,unsigned size)
{ (void)mac;(void)data;(void)size; }
'''
receiver = body(ROOT / 'main/WIFI/broadcast.c')
code += receiver[:receiver.index('void wireless_espnow_init')]
cases = (ROOT / 'tests/usb_cases.c').read_text(encoding='utf-8')
cases += '\n' + (ROOT / 'tests/connection_cases.c').read_text(encoding='utf-8')
code += '\n' + cases
out = ROOT / 'build/usb-host-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out / 'checks.c', out / 'checks.dll'
c.write_text(code, encoding='utf-8')
subprocess.run([sys.argv[1], '-std=c11', '-O1', '-fno-builtin', '-mno-stack-arg-probe',
                '-Werror=implicit-function-declaration', '-shared', '-nostdlib', '-fuse-ld=lld',
                '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)], check=True)
lib = ctypes.CDLL(str(dll))
names = re.findall(r'EXPORT int (check_\w+)\(void\)', cases)
for name in names:
    line = getattr(lib, name)()
    if line:
        raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name + ': passed')
(out / 'result.json').write_text(json.dumps({'passed': True, 'cases': names, 'compiler': sys.argv[1]}, indent=2)+'\n', encoding='utf-8')
print(f'{len(names)} USB scenarios passed')

