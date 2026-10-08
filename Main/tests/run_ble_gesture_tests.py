"""Real BLE mouse parser -> shared gesture queue -> BLE sender integration."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

base = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = (ROOT/'tests/wireless_haptic_runtime.h').read_text(encoding='utf-8')
runtime = runtime.replace('return 2302;', 'return config.bytes[CFG_ROTATION] & 1 ? 1532 : 2302;')
runtime = runtime.replace('return 1532;', 'return config.bytes[CFG_ROTATION] & 1 ? 2302 : 1532;')
runtime = runtime.replace('device_config_rotation(void) { return 0; }',
                          'device_config_rotation(void) { return config.bytes[CFG_ROTATION]; }')
code = base + '\n' + runtime.split('/* PRODUCTION_TYPES */')[0]
headers = ['SYS/knuckle_model.h','SYS/knuckle_gesture.h', 'SYS/hid_msg.h', 'SYS/report_buffer.h', 'SYS/input_pipeline.h',
           'SYS/rstp_protocol.h', 'SYS/connection.h', 'SYS/connection_policy.h',
           'SYS/edge_gesture.h', 'SYS/point_gesture.h', 'SYS/aux_output.h',
           'SYS/wireless_extension.h', 'SYS/wireless_probe.h', 'I2C/SUB_DEV/surface_haptic_policy.h',
           'I2C/SUB_DEV/surface_haptic_runtime.h', 'BLE/ble_hid.h']
code += '\n'.join(body(ROOT/'main'/name) for name in headers) + '\n'
code += runtime.split('/* PRODUCTION_TYPES */')[1] + '\n'
code += (ROOT/'tests/connection_runtime.h').read_text(encoding='utf-8') + '\n'
for name in ['SYS/knuckle_gesture.c', 'SYS/rstp_protocol.c', 'SYS/report_buffer.c',
             'I2C/SUB_DEV/surface_haptic_policy.c', 'I2C/SUB_DEV/surface_haptic_runtime.c',
             'SYS/input_pipeline.c', 'SYS/aux_output.c', 'SYS/edge_gesture.c',
             'SYS/point_gesture.c', 'SYS/connection_policy.c', 'SYS/connection.c',
             'I2C/TP/tp_coordinates.h', 'I2C/TP/tp_report_handle.c',
             'I2C/TP/ptp_simulated_mouse_gesture.c', 'USB/usb_aux.h']:
    code += '\n#undef TAG\n' + body(ROOT/'main'/name)
parser = body(ROOT/'main/I2C/TP/i2c_queue.c')
code += '\n#undef TAG\n' + parser.replace('while (1) {', 'while (parser_budget-- > 0) { parser_hook();')
code += (ROOT/'tests/ble_gesture_runtime.h').read_text(encoding='utf-8')
sender = body(ROOT/'main/BLE/blehid.c')
for name in ('flight', 'success', 'done'):
    sender = re.sub(r'\b'+name+r'\b', 'ble_'+name, sender)
code += sender.replace('while (true)', 'while (sender_budget-- > 0)')
cases = (ROOT/'tests/ble_gesture_cases.c').read_text(encoding='utf-8')
calibration = json.loads((ROOT/'main/SYS/knuckle_training/samples.json').read_text(encoding='utf-8'))['samples']
pair = sorted((s for s in calibration if s['session']=='followup_double_knock'),key=lambda s:s['start_ms'])
rows = []
previous = pair[0]['start_ms']-10
for sample in pair:
    for dt, raw in sample['frames']:
        p=bytes.fromhex(raw); t=sample['start_ms']+dt
        x=int.from_bytes(p[5:7],'little'); y=1532-int.from_bytes(p[7:9],'little')
        rows.append('{%d,%d,%d,%d,%d,false,%d,%d}' % (x,y,p[4]&1,t-previous,p[10],p[9],p[11]))
        previous=t
cases += '\nEXPORT int check_ble_captured_real_double_knock(void) {\n'
cases += 'reset_ble(); const sample_t trace[]={' + ','.join(rows) + '}; RUN(trace);\n'
cases += 'CHECK(quiet_mouse()&&usages(8,0x46)==1&&usages(8,0)>=1&&point_haptics==1);return 0; }\n'
code += '\n' + cases
out = ROOT/'build/ble-gesture-host-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out/'checks.c', out/'checks.dll'
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
    print(name+': passed')
(out/'result.json').write_text(json.dumps({'passed': True, 'cases': names}, indent=2)+'\n', encoding='utf-8')

