"""Exercise the production NimBLE service, GAP and queued notification adapter."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys
from run_host_tests import body, ROOT

project = json.loads((ROOT/'build/project_description.json').read_text(encoding='utf-8'))
sdk = Path(project['idf_path'])/'components/bt/host/nimble/nimble/nimble'
source = body(ROOT/'main/BLE/ble_nimble.c')
runtime = (ROOT/'tests/nimble_runtime.h').read_text(encoding='utf-8')
cases = (ROOT/'tests/ble_gatt_cases.c').read_text(encoding='utf-8')
gatts_source = (sdk/'host/src/ble_gatts.c').read_text(encoding='utf-8')
access_start = gatts_source.index('static int\nble_gatts_val_access(')
access_end = gatts_source.index('static int\nble_gatts_chr_val_access(', access_start)
sdk_access = gatts_source[access_start:access_end]
definitions = {}
for header in [*(sdk/'host/include/host').glob('*.h'), *(sdk/'include/nimble').glob('*.h')]:
    header_text = header.read_text(encoding='utf-8')
    for name, value in re.findall(r'^#define\s+(BLE_\w+)[ \t]+([^\n]+)', header_text, re.M):
        definitions[name] = value
    for name, value in re.findall(r'^\s*(BLE_\w+)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*,', header_text, re.M):
        definitions[name] = value
needed = set(re.findall(r'\bBLE_\w+\b', source+runtime+cases+sdk_access)) & definitions.keys()
while True:
    expanded = needed | (set(re.findall(r'\bBLE_\w+\b', '\n'.join(definitions[n] for n in needed))) & definitions.keys())
    if expanded == needed:
        break
    needed = expanded
constants = '\n'.join(f'#define {name} {definitions[name]}' for name in sorted(needed))+'\n'
code = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
code += '\ntypedef int esp_err_t;\n'
for name in ['SYS/hid_msg.h', 'SYS/report_buffer.h', 'SYS/aux_output.h', 'BLE/ble_hid.h']:
    code += body(ROOT/'main'/name)+'\n'
code += constants+runtime+'\n'+sdk_access+'\n'+body(ROOT/'main/BLE/ble_hid_descriptor.c')+'\n'+source+'\n'+cases
out = ROOT/'build/ble-gatt-host-tests'
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
(out/'result.json').write_text(json.dumps({'passed': True, 'cases': names, 'sdk': str(sdk)}, indent=2)+'\n', encoding='utf-8')
