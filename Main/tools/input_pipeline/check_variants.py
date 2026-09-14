"""Check configuration branches using the existing VS Code ESP-IDF compile commands.

This does not regenerate CMake, change sdkconfig, compile objects, or link firmware.
"""
import argparse
import ctypes
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['main.c', 'surface_haptic_test.c', 'I2C/TP/i2c_cmd.c', 'I2C/TP/i2c_queue.c',
           'I2C/TP/ptp_simulated_mouse_gesture.c', 'I2C/TP/tp_report_handle.c',
           'SYS/input_pipeline.c', 'SYS/report_buffer.c', 'USB/usbhid.c',
           'USB/usb_descriptor.c', 'BLE/blehid.c', 'BLE/ble_hid_dev.c',
           'BLE/ble_hid_device_le_prf.c', 'BLE/ble_bluedroid_init.c', 'BLE/ble_hid_descriptor.c',
           'WIFI/wifi_handle.c', 'WIFI/broadcast.c']
SOURCES += ['SYS/rstp_protocol.c', 'SYS/device_config.c', 'SYS/edge_gesture.c',
            'USB/usb_config.c', 'USB/usb_aux.c', 'I2C/TP/tp_sleep.c']
CHOICES = ['TP_ROTATION_LANDSCAPE', 'TP_ROTATION_LANDSCAPE_FLIPPED', 'TP_ROTATION_PORTRAIT',
           'TP_ROTATION_PORTRAIT_FLIPPED', 'ORI_MOUSE_MODE', 'PTP_SIMULATED_MOUSE_MODE',
           'BLE_ENABLE_MOUSE_MODE', 'BLE_ENABLE_PTP_MODE', 'SURFACE_HAPTIC_TEST_MODE']
VARIANTS = {
    'original_mouse': ['TP_ROTATION_LANDSCAPE', 'ORI_MOUSE_MODE', 'BLE_ENABLE_MOUSE_MODE'],
    'ble_ptp_flipped': ['TP_ROTATION_LANDSCAPE_FLIPPED', 'PTP_SIMULATED_MOUSE_MODE', 'BLE_ENABLE_PTP_MODE'],
    'standalone_portrait': ['TP_ROTATION_PORTRAIT', 'PTP_SIMULATED_MOUSE_MODE', 'BLE_ENABLE_MOUSE_MODE', 'SURFACE_HAPTIC_TEST_MODE'],
    'portrait_flipped': ['TP_ROTATION_PORTRAIT_FLIPPED', 'PTP_SIMULATED_MOUSE_MODE', 'BLE_ENABLE_MOUSE_MODE'],
}


def arguments(command):
    argc = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    argv = parse(command, ctypes.byref(argc))
    try:
        return [argv[i] for i in range(argc.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))


def run(report=None):
    commands = json.loads((ROOT / 'build/compile_commands.json').read_text())
    by_file = {Path(c['file']).resolve(): c for c in commands}
    results = {}
    with tempfile.TemporaryDirectory(prefix='touchpad-syntax-') as temp:
        temp = Path(temp).resolve()
        assert temp.is_relative_to(Path(tempfile.gettempdir()).resolve())
        override = temp / 'variant.h'
        for name, enabled in VARIANTS.items():
            override.write_text('#include "sdkconfig.h"\n' +
                                ''.join(f'#undef CONFIG_{key}\n' for key in CHOICES) +
                                ''.join(f'#define CONFIG_{key} 1\n' for key in enabled))
            for source in SOURCES:
                entry = by_file[(ROOT / 'main' / source).resolve()]
                original = arguments(entry['command'])
                args = []
                skip = False
                for arg in original:
                    if skip:
                        skip = False
                        continue
                    if arg in ['-o', '-MF', '-MT', '-MQ']:
                        skip = True
                    elif arg not in ['-c', '-MD', '-MMD', '-MP']:
                        args.append(arg)
                args += ['-fsyntax-only', '-include', str(override)]
                done = subprocess.run(args, cwd=entry['directory'], capture_output=True, text=True)
                if done.returncode:
                    raise RuntimeError(f'{name}: {source}\n{done.stdout}\n{done.stderr}')
            results[name] = {'syntax_only': True, 'source_count': len(SOURCES), 'passed': True}
            print(f'{name}: {len(SOURCES)} source files passed (syntax only)')
    if report:
        Path(report).write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8', newline='\n')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', help='Optional JSON output path')
    run(parser.parse_args().report)
