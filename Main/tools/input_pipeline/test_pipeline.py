"""Execute production C buffer, pipeline and sender bodies with deterministic I/O.

Only SDK includes, scheduling and hardware are substituted. Binaries are created
in a temporary directory. Requires Python and clang; Windows needs no CRT.
"""
import ctypes
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def source(path):
    return re.sub(r'^#include[^\n]*\n|^#pragma once[^\n]*\n', '',
                  (ROOT / path).read_text(), flags=re.M)


def function(path, name):
    text = source(path)
    match = re.search(r'^[^\n]*\b' + name + r'\([^;]*?\)\s*\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


def run():
    parts = [(HERE / 'host_preamble.h').read_text(), source('main/SYS/hid_msg.h'),
             source('main/SYS/report_buffer.h'), source('main/SYS/report_buffer.c'),
             source('main/SYS/input_pipeline.h'), source('tools/input_pipeline/host_runtime.h'),
             source('main/SYS/input_pipeline.c')]
    parts += ['#define SENSITIVITY 2.0f', source('main/I2C/TP/tp_report_handle.c')]
    usb = source('main/USB/usbhid.c')
    parts += [source('tools/input_pipeline/host_transports.h'),
              usb[usb.index('static portMUX_TYPE usb_tx_lock'):usb.index('uint16_t tud_hid_get_report_cb')],
              '\n'.join(re.findall(r'^#define REPORTID_.*$', usb, re.M)),
              function('main/USB/usbhid.c', 'tud_hid_get_report_cb'),
              function('main/USB/usbhid.c', 'tinyusb_event_cb'),
              function('main/USB/usbhid.c', 'usbhid_task'), source('main/BLE/blehid.c'),
              function('main/WIFI/heartbeat.c', 'wireless_make_heartbeat')]
    wifi = source('main/WIFI/wifi_handle.c')
    parts += [wifi[wifi.index('static portMUX_TYPE send_lock'):wifi.index('void wireless_wifi_init')],
              function('main/WIFI/wifi_handle.c', 'wifi_send_task'),
              function('main/WIFI/broadcast.c', 'wifi_now_recv_cb'),
              source('tools/input_pipeline/host_initialization.h'),
              function('main/GPIO/irq_tp_int.c', 'irq_int_init'),
              function('main/GPIO/irq_func_btn.c', 'irq_func_btn_init')]
    orientations = ['LANDSCAPE', 'LANDSCAPE_FLIPPED', 'PORTRAIT', 'PORTRAIT_FLIPPED']
    for index, orientation in enumerate(orientations):
        parts.append(''.join(f'#undef CONFIG_TP_ROTATION_{item}\n' for item in orientations) +
                     f'#define CONFIG_TP_ROTATION_{orientation} 1\n' +
                     source('main/I2C/TP/tp_coordinates.h').replace('tp_rotate_coordinates', f'rotate_{index}'))
    parts.append(source('tools/input_pipeline/host_cases.c'))
    code = '\n'.join(parts).replace('while (true)', 'while (test_steps-- > 0)')
    temp_root = ROOT / 'build/input_validation'
    temp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='touchpad-input-', dir=temp_root) as temp:
        temp = Path(temp)
        assert temp.resolve().is_relative_to(temp_root.resolve())
        c, dll = temp / 'test.c', temp / 'test.dll'
        c.write_text(code, encoding='utf-8')
        host_paths = os.pathsep.join(p for p in os.environ.get('PATH', '').split(os.pathsep) if 'esp-clang' not in p.lower())
        clang = shutil.which('clang', path=host_paths)
        if not clang:
            raise RuntimeError('Native Windows Clang is required for DLL tests')
        command = [clang, '-std=c11', '-O1', '-fno-builtin',
                   '-Werror=implicit-function-declaration', '-shared', '-nostdlib', '-fuse-ld=lld',
                   '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)]
        print(f'Compiling host cases with {clang}', flush=True)
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
        library = ctypes.CDLL(str(dll))
        cases = re.findall(r'EXPORT int (test_\w+)\(void\)', parts[-1])
        try:
            for name in cases:
                print(f'Running {name}', flush=True)
                result = getattr(library, name)()
                if result:
                    context = code.splitlines()[max(0, result - 2):result + 1]
                    raise AssertionError(f'{name}: C line {result}: {context}')
                print(f'{name}: passed')
            print(f'{len(cases)} production C input/transport scenarios passed')
        finally:
            # ctypes does not automatically unload Windows DLLs before cleanup.
            import _ctypes
            _ctypes.FreeLibrary(library._handle)


if __name__ == '__main__':
    try:
        if '--worker' in sys.argv:
            run()
        else:
            subprocess.run([sys.executable, '-B', '-u', str(Path(__file__).resolve()), '--worker'],
                           check=True, timeout=180)
    except subprocess.CalledProcessError as error:
        print(error.stdout or '')
        print(error.stderr or '')
        raise
