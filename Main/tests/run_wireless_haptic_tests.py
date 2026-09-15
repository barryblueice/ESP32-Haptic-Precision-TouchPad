"""Exercise production parser/pipeline/haptics and ESP-NOW sender without hardware."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body


base = (ROOT / 'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = (ROOT / 'tests/wireless_haptic_runtime.h').read_text(encoding='utf-8')
code = base + '\n' + runtime.split('/* PRODUCTION_TYPES */')[0]
headers = ['SYS/hid_msg.h', 'SYS/report_buffer.h', 'SYS/input_pipeline.h',
           'SYS/rstp_protocol.h', 'SYS/edge_gesture.h', 'SYS/point_gesture.h',
           'SYS/aux_output.h', 'SYS/wireless_extension.h',
           'I2C/SUB_DEV/surface_haptic_policy.h', 'I2C/SUB_DEV/surface_haptic_runtime.h']
code += '\n'.join(body(ROOT / 'main' / name) for name in headers) + '\n'
code += runtime.split('/* PRODUCTION_TYPES */')[1] + '\n'
sources = ['SYS/rstp_protocol.c', 'SYS/report_buffer.c',
           'I2C/SUB_DEV/surface_haptic_policy.c', 'I2C/SUB_DEV/surface_haptic_runtime.c',
           'SYS/input_pipeline.c', 'SYS/aux_output.c', 'SYS/edge_gesture.c',
           'SYS/point_gesture.c', 'I2C/TP/tp_coordinates.h',
           'I2C/TP/tp_report_handle.c', 'I2C/TP/ptp_simulated_mouse_gesture.c', 'USB/usb_aux.h']
for name in sources:
    code += '\n#undef TAG\n' + body(ROOT / 'main' / name)
parser = body(ROOT / 'main/I2C/TP/i2c_queue.c')
parser = parser.replace('while (1) {', 'while (parser_budget-- > 0) { parser_hook();')
code += '\n#undef TAG\n' + parser
# Initialization is verified by the firmware build; retain the actual ACK,
# completion callback and the complete sender loop for deterministic radio tests.
wifi = body(ROOT / 'main/WIFI/wifi_handle.c')
code += '\n' + wifi[wifi.index('static portMUX_TYPE send_lock'):wifi.index('void wireless_wifi_init')]
code += wifi[wifi.index('void wifi_send_task'):].replace('while (true)', 'while (wifi_budget-- > 0)')
cases = (ROOT / 'tests/wireless_haptic_cases.c').read_text(encoding='utf-8') + '\n' + (ROOT / 'tests/vbus_pressure_cases.c').read_text(encoding='utf-8')
code += '\n' + cases
out = ROOT / 'build/wireless-haptic-host-tests'
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
(out / 'result.json').write_text(json.dumps({'passed': True, 'cases': names}, indent=2)+'\n', encoding='utf-8')
print(f'{len(names)} wireless haptic scenarios passed')
