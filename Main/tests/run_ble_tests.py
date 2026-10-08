"""Exercise the real BLE sender, input pipeline and report buffer without hardware."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys
from run_host_tests import body, ROOT

base = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = (ROOT/'tests/wireless_haptic_runtime.h').read_text(encoding='utf-8')
code = base + '\n' + runtime.split('/* PRODUCTION_TYPES */')[0]
headers = ['SYS/hid_msg.h', 'SYS/report_buffer.h', 'SYS/input_pipeline.h',
           'SYS/rstp_protocol.h', 'SYS/aux_output.h', 'SYS/wireless_extension.h',
           'I2C/SUB_DEV/surface_haptic_policy.h', 'I2C/SUB_DEV/surface_haptic_runtime.h']
code += '\n'.join(body(ROOT/'main'/name) for name in headers) + '\n'
code += runtime.split('/* PRODUCTION_TYPES */')[1] + '\n'
for name in ['SYS/report_buffer.c', 'I2C/SUB_DEV/surface_haptic_policy.c',
             'I2C/SUB_DEV/surface_haptic_runtime.c', 'SYS/input_pipeline.c', 'SYS/aux_output.c']:
    code += '\n#undef TAG\n' + body(ROOT/'main'/name)
code += r"""
static unsigned budget, test_sends;
static void connection_lock(void) {}
static void connection_unlock(void) {}
static void connection_link(int transport, bool ready)
{ (void)transport; input_set_link(ready ? (1U << MOUSE_MODE) : 0); }
static bool send_fails, synchronous_complete;
static struct {
    uint16_t conn;
    uint8_t id, type, length, data[8];
} sent[64];
"""
code += body(ROOT/'main/BLE/ble_hid.h') + '\n'
code += body(ROOT/'main/BLE/ble_hid_descriptor.c') + '\n'
code += r"""
uint16_t hid_dev_report_handle(uint8_t id) { return id + 100; }
esp_err_t ble_hid_send_mouse(uint16_t conn, uint32_t epoch, const input_report_t *report)
{
    if (test_sends < 64) {
        sent[test_sends].conn = conn; sent[test_sends].id = 1;
        sent[test_sends].type = 1; sent[test_sends].length = 5;
        memcpy(sent[test_sends].data, &report->data.mouse, 5);
    }
    ++test_sends;
    if (synchronous_complete && !send_fails)
        ble_input_complete(conn, epoch, hid_dev_report_handle(1), BLE_TX_OK);
    return send_fails ? ESP_FAIL : ESP_OK;
}
esp_err_t ble_hid_send_aux(uint16_t conn, uint32_t epoch, const aux_output_report_t *r)
{
    uint8_t id = r->id == 2 ? 1 : r->id;
    if (test_sends < 64) {
        sent[test_sends].conn=conn;sent[test_sends].id=id;
        sent[test_sends].type=1;sent[test_sends].length=r->length;
        memcpy(sent[test_sends].data,r->data,r->length);
    }
    ++test_sends;
    if (synchronous_complete && !send_fails)
        ble_input_complete(conn,epoch,hid_dev_report_handle(id),BLE_TX_OK);
    return send_fails ? ESP_FAIL : ESP_OK;
}
"""
ble = body(ROOT/'main/BLE/blehid.c')
for name in ('flight', 'success', 'done'):
    ble = re.sub(r'\b'+name+r'\b', 'ble_'+name, ble)
code += ble.replace('while (true)', 'while (budget-- > 0)')
cases = (ROOT/'tests/ble_cases.c').read_text(encoding='utf-8')
code += '\n' + cases
out = ROOT/'build/ble-host-tests'
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
    print(name + ': passed')
(out/'result.json').write_text(json.dumps({'passed': True, 'cases': names}, indent=2)+'\n', encoding='utf-8')
