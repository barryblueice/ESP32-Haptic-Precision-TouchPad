"""Compile production NVS transactions and RSTP worker with deterministic SDK stubs."""
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def source(path):
    return re.sub(r'^#include[^\n]*\n|^#pragma once[^\n]*\n', '', (ROOT / path).read_text(encoding='utf-8'), flags=re.M)

def run():
    vectors = json.loads((HERE / 'protocol_vectors.json').read_text())
    fixtures = '\n'.join('static const uint8_t vector_' + name + '[] = {' + ','.join(str(b) for b in bytes.fromhex(data)) + '};'
                         for name, data in vectors.items())
    descriptor = source('main/USB/usb_descriptor.c')
    descriptor_defines = '\n'.join(re.findall(r'^#define (?:REPORTID_|LOGICAL_|PHYSICAL_).*$', descriptor, re.M))
    descriptor_arrays = '\n'.join(re.findall(r'(?:const )?uint8_t (?:mouse_hid|ptp_hid)_report_descriptor\[\] = \{.*?\n\};', descriptor, re.S))
    from test_pipeline import function
    parts = [(HERE / 'host_preamble.h').read_text(), '#define WIRED_MODE 0',
             source('main/SYS/rstp_protocol.h'), source('main/SYS/rstp_protocol.c'),
             source('tools/input_pipeline/config_host.h'),
             source('main/SYS/device_config.c').replace('static device_config_t active, pending;', 'static device_config_t active, next_config;').replace('pending = *c', 'next_config = *c').replace('active = pending;', 'active = next_config;'),
             source('main/USB/usb_config.c'), descriptor_defines, descriptor_arrays,
             function('main/USB/usb_descriptor.c', 'usb_descriptor_init'), fixtures,
             re.sub(r'\bactive\b', 'sleep_active', source('main/I2C/TP/tp_sleep.c')),
             source('tools/input_pipeline/config_cases.c')]
    code = '\n'.join(parts).replace('while (true)', 'while (test_steps-- > 0)')
    paths = os.pathsep.join(p for p in os.environ.get('PATH', '').split(os.pathsep) if 'esp-clang' not in p.lower())
    clang = shutil.which('clang', path=paths)
    if not clang:
        raise RuntimeError('Native Windows Clang is required')
    temp_root = ROOT / 'build/input_validation'
    temp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rstp-config-', dir=temp_root) as temp:
        c = Path(temp) / 'config.c'; dll = Path(temp) / 'config.dll'
        c.write_text(code, encoding='utf-8')
        done = subprocess.run([clang, '-std=c11', '-O1', '-fno-builtin', '-Werror=implicit-function-declaration',
                               '-shared', '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)],
                              capture_output=True, text=True, timeout=60)
        if done.returncode:
            raise RuntimeError(done.stdout + done.stderr)
        lib = ctypes.CDLL(str(dll))
        try:
            for name in re.findall(r'EXPORT int (test_\w+)\(void\)', parts[-1]):
                result = getattr(lib, name)()
                if result:
                    raise AssertionError(f'{name}: C line {result}: {code.splitlines()[result-1]}')
                print(f'{name}: passed', flush=True)
        finally:
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)

if __name__ == '__main__':
    run()
