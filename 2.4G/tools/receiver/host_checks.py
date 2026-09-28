"""Run receiver regression cases plus extension cases against production C."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys
import argparse
import os
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1].parent / 'Main'

def body(path):
    return re.sub(r'^#include[^\n]*\n|^#pragma once[^\n]*\n', '',
                  path.read_text(encoding='utf-8'), flags=re.M)

parser = argparse.ArgumentParser()
parser.add_argument('--clang', default=os.environ.get('HOST_CLANG'))
parser.add_argument('--clang-arg', action='append', default=[])
parser.add_argument('--build-dir', type=Path, default=HERE.parents[1]/'build')
options = parser.parse_args()
native_path = os.pathsep.join(p for p in os.environ.get('PATH', '').split(os.pathsep)
                            if 'esp-clang' not in p.lower())
clang = options.clang or shutil.which('clang', path=native_path)
if not clang:
    parser.error('Specify --clang or HOST_CLANG: use a Windows host compiler, not ESP clang')
if Path(clang).is_file():
    clang = str(Path(clang).resolve())


receiver=HERE.parents[1]
runtime=(HERE/'host_runtime.h').read_text(encoding='utf-8')
runtime+='''
void *memmove(void *d,const void *s,size_t n) { unsigned char *p=d; const unsigned char *q=s; if(p<q)while(n--)*p++=*q++;else while(n){--n;p[n]=q[n];}return d; }
static unsigned descriptor_rotation, disconnect_count;
static void tud_disconnect(void) { mounted=false; ++disconnect_count; }
static void tud_connect(void) { mounted=true; }
static uint32_t esp_random(void) { return 12345; }
void receiver_descriptor_rotation(uint8_t rotation) { descriptor_rotation=rotation; }
'''
files=['main/protocol.h','main/input/report_buffer.h','main/input/input_pipeline.h',
       'main/wireless/wireless.h','main/usb/usbhid.h','main/nvs/ptp_nvs.h',
       'main/wireless/receiver_extension.h','main/wireless/receiver_settings.h','main/protocol.c','main/input/report_buffer.c',
       'main/input/input_pipeline.c','main/wireless/broadcast.c','main/wireless/heartbeat.c',
       'main/wireless/wifi_receive.c','main/usb/usbhid.c','main/nvs/ptp_nvs.c','main/main.c']
code=runtime+'\n'+body(ROOT/'main/SYS/wireless_extension.h')+'\n'+body(ROOT/'main/SYS/wireless_probe.h')+'\n'+body(ROOT/'main/SYS/aux_output.h')+'\n'
def source(name):
    return receiver/name
code+='\n'.join(body(source(f)) for f in files)
code+='\n'+body(ROOT/'main/SYS/aux_output.c')+'\n'+body(receiver/'main/wireless/receiver_extension.c')
code+='\n'+body(receiver/'main/wireless/receiver_settings.c')
code+='''
static void receiver_settings_reset_for_test(void) {
    actual_intensity=63; actual_level=2; wanted_intensity=wanted_level=dirty=0;
    memset(revision,0,sizeof(revision)); memset(transaction_revision,0,sizeof(transaction_revision));
    transaction=(wire_settings_t){0}; memset(target_mac,0,6);
    target_session=client_nonce=next_sequence=attempted_at=queried_at=0;
    transaction_pending=settings_synced=flight_settings=false;
}
'''
cases=(receiver/'tools/receiver/host_cases.c').read_text(encoding='utf-8')
cases+='\n'+(HERE/'receiver_cases.c').read_text(encoding='utf-8')
cases+='\n'+(HERE/'receiver_settings_cases.c').read_text(encoding='utf-8')
cases+='\n'+(HERE/'receiver_probe_cases.c').read_text(encoding='utf-8')
code+='\n'+cases
code=code.replace('while (true)','while (test_steps-- > 0)')
out=options.build_dir.resolve()/'receiver-host-tests';out.mkdir(parents=True,exist_ok=True)
c=out/'checks.c';dll=out/'checks.dll';c.write_text(code,encoding='utf-8')
subprocess.run([clang,'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe','-DAUX_OUTPUT_RECEIVER',
               '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
               '-Wl,/noentry','-Wl,/nodefaultlib',*options.clang_arg,str(c),'-o',str(dll)],check=True)
lib=ctypes.CDLL(str(dll));names=re.findall(r'EXPORT int (check_\w+)\(void\)',cases)
for name in names:
    line=getattr(lib,name)()
    if line:raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name+': passed')
# Decode the actual USB descriptor's short items, including report/global state.
descriptor=source('main/usb/usb_descriptor.c').read_text(encoding='utf-8')
descriptor=descriptor.split('haptic_ptp_hid_report_descriptor[] = {',1)[1].split('};',1)[0]
descriptor=re.sub(r'//[^\n]*|/\*.*?\*/','',descriptor,flags=re.S)
defines=dict(re.findall(r'#define\s+(REPORTID_\w+)\s+(0x[0-9a-fA-F]+)',source('main/protocol.h').read_text(encoding='utf-8')))
descriptor=re.sub(r'REPORTID_\w+',lambda m:defines[m[0]],descriptor)
items=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',descriptor))
i=0;report=minimum=maximum=0;features={}
while i<len(items):
    tag=items[i];i+=1;size=tag&3;size=4 if size==3 else size
    value=int.from_bytes(items[i:i+size],'little');i+=size
    if tag==0x85:report=value
    elif tag in (0x15,0x16,0x17):minimum=value
    elif tag in (0x25,0x26,0x27):maximum=value
    elif tag in (0xb1,0xb2,0xb3):features[report]=(minimum,maximum)
assert features[0x41]==(0,100) and features[0x40]==(1,3),features
names.append('check_windows_feature_descriptor_ranges')
print(names[-1]+': passed')
(out/'result.json').write_text(json.dumps({'passed':True,'cases':names,'count':len(names),'clang':str(clang),'clang_args':options.clang_arg},indent=2)+'\n',encoding='utf-8')
print(f'{len(names)} receiver scenarios passed')
