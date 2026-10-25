"""Replay F12 object types through the real parser, pipeline and recognizers."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

base = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = (ROOT/'tests/wireless_haptic_runtime.h').read_text(encoding='utf-8')
runtime = runtime.replace('device_config_rotation(void) { return 0; }',
                          'device_config_rotation(void) { return config.bytes[CFG_ROTATION]; }')
code = base + '\n' + runtime.split('/* PRODUCTION_TYPES */')[0]
code += '\n#define M_PI 3.14159265358979323846\n#define isfinite(x) __builtin_isfinite(x)\n'
code += 'static void usbhid_remote_wakeup_request(void) {}\n'
headers = ['SYS/knuckle_model.h','SYS/knuckle_gesture.h','SYS/hid_msg.h',
           'SYS/report_buffer.h','SYS/input_pipeline.h','SYS/rstp_protocol.h',
           'SYS/connection.h','SYS/connection_policy.h','SYS/edge_gesture.h',
           'SYS/point_gesture.h','SYS/aux_output.h','SYS/wireless_extension.h',
           'SYS/wireless_probe.h','I2C/SUB_DEV/surface_haptic_policy.h',
           'I2C/SUB_DEV/surface_haptic_runtime.h','I2C/TP/pointer_motion.h']
code += '\n'.join(body(ROOT/'main'/name) for name in headers) + '\n'
code += runtime.split('/* PRODUCTION_TYPES */')[1] + '\n'
code += (ROOT/'tests/connection_runtime.h').read_text(encoding='utf-8') + '\n'
sources = ['I2C/TP/pointer_motion.c','SYS/knuckle_gesture.c','SYS/rstp_protocol.c',
           'SYS/report_buffer.c','I2C/SUB_DEV/surface_haptic_policy.c',
           'I2C/SUB_DEV/surface_haptic_runtime.c','SYS/input_pipeline.c',
           'SYS/aux_output.c','SYS/edge_gesture.c','SYS/point_gesture.c',
           'SYS/connection_policy.c','SYS/connection.c','I2C/TP/tp_coordinates.h',
           'I2C/TP/tp_report_handle.c','I2C/TP/ptp_simulated_mouse_gesture.c','USB/usb_aux.h']
for name in sources:
    code += '\n#undef TAG\n' + body(ROOT/'main'/name)
code += '\n#undef TAG\n' + body(ROOT/'main/I2C/TP/i2c_queue.c').replace(
    'while (1) {', 'while (parser_budget-- > 0) {')
cases = (ROOT/'tests/f12_cases.c').read_text(encoding='utf-8')
code += '\n' + body(ROOT/'tests/f12_captured_pad_cases.h')
code += '\n' + cases
out = ROOT/'build/f12-host-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out/'checks.c', out/'checks.dll'
c.write_text(code, encoding='utf-8')
subprocess.run([sys.argv[1], '-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe',
    '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
    '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)], check=True)
lib = ctypes.CDLL(str(dll))
names = re.findall(r'EXPORT int (check_\w+)\(void\)', cases)
failures = {}
for name in names:
    line = getattr(lib, name)()
    if line:
        failures[name] = code.splitlines()[line-1]
    print(name + (': FAILED: '+failures[name] if line else ': passed'))
(out/'result.json').write_text(json.dumps({'cases': names, 'failures': failures,
    'compiler': sys.argv[1]}, indent=2)+'\n', encoding='utf-8')
if failures:
    raise SystemExit(1)
print(f'{len(names)} F12 production-parser scenarios passed')
