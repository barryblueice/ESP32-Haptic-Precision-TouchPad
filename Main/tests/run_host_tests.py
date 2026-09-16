"""Build the production C algorithms with deterministic RTOS/NVS boundaries."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def body(path):
    return re.sub(r'^#include[^\n]*\n|^#pragma once[^\n]*\n', '',
                  path.read_text(encoding='utf-8'), flags=re.M)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--clang', default=os.environ.get('HOST_CLANG') or shutil.which('clang'))
    args=parser.parse_args()
    if not args.clang:
        parser.error('Specify native clang with --clang or HOST_CLANG')
    files=['SYS/rstp_protocol.h','SYS/hid_msg.h','SYS/edge_gesture.h','SYS/point_gesture.h',
           'SYS/aux_output.h','SYS/wireless_extension.h','SYS/rstp_protocol.c','SYS/edge_gesture.c',
           'SYS/point_gesture.c','SYS/aux_output.c','SYS/device_config.c','I2C/TP/tp_coordinates.h']
    code=(ROOT/'tests/host_runtime.h').read_text(encoding='utf-8')+'\n'
    code+='\n'.join(body(ROOT/'main'/f) for f in files)
    cases=(ROOT/'tests/core_cases.c').read_text(encoding='utf-8')+'\n'+(ROOT/'tests/vbus_config_cases.c').read_text(encoding='utf-8')
    cases+='\n'+(ROOT/'tests/function_key_cases.c').read_text(encoding='utf-8')
    code+='\n'+cases
    names=re.findall(r'EXPORT int (check_\w+)\(void\)',cases)
    output=ROOT/'build/host-tests';output.mkdir(parents=True,exist_ok=True)
    c=output/'checks.c';dll=output/'checks.dll';c.write_text(code,encoding='utf-8')
    command=[args.clang,'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe',
             '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
             '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)]
    subprocess.run(command,check=True)
    lib=ctypes.CDLL(str(dll))
    for name in names:
        line=getattr(lib,name)()
        if line:raise AssertionError(f'{name} failed: {code.splitlines()[line-1]} (generated line {line})')
        print(name+': passed')
    fixture=json.loads((ROOT/'tests/protocol_vectors.json').read_text(encoding='utf-8'))
    vector=(ctypes.c_ubyte*64).from_buffer_copy(bytes.fromhex(fixture['v3_point_write_request']))
    line=lib.check_vector(vector)
    if line:raise AssertionError(f'protocol vector failed at {line}')
    result={'passed':True,'cases':names+['v3_point_write_request'],'compiler':args.clang}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f'{len(names)+1} production-C scenarios passed')

if __name__=='__main__':main()
