"""Run receiver regression cases plus extension cases against production C."""
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys
from run_host_tests import body, ROOT

receiver=Path(sys.argv[2]) if len(sys.argv)>2 else ROOT.parent/'2.4G'
runtime=(ROOT.parent/'2.4G/tools/receiver/host_runtime.h').read_text(encoding='utf-8')
runtime=runtime.replace('typedef struct { int unused; } esp_now_recv_info_t;', 'typedef struct { uint8_t src_addr[6]; } esp_now_recv_info_t;')
runtime=runtime.replace('if (size != 1) ++lock_error;', 'if (size != 1 && size != 38) ++lock_error;')
runtime=runtime.replace('const uint8_t haptic_ptp_hid_report_descriptor[]', 'uint8_t haptic_ptp_hid_report_descriptor[]')
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
code=runtime+'\n'+body(ROOT/'main/SYS/wireless_extension.h')+'\n'+body(ROOT/'main/SYS/aux_output.h')+'\n'
def source(name):
    p=receiver/name
    return p if p.exists() else ROOT.parent/'2.4G'/name
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
cases=cases.replace('    input_init();','''    input_init();
    requested = applied = ack_flight = (wire_surface_t){0};
    pending_apply = ready = ack_pending = flight_ack = false;
    last_sequence = last_seen = 0; memset(peer,0,6);
    count=0; flight=false; held_mask=release_due=neutral_due=0; aux_epoch=radio_epoch=0;
    aux_generation=input_generation(); usb_aux_flight=false; prefer_aux=true;
    descriptor_rotation=surface_rotation=disconnect_count=0;''',1)
# Reconnection now releases the two added HID collections before PTP input.
cases=cases.replace('    usbhid_step(); CHECK(usb_flight.release && !lock_error);', '''    usbhid_step();
    while(usb_aux_flight) { usb_complete(REPORT_MOUSE,true); usbhid_step(); }
    CHECK(usb_flight.release && !lock_error);''')
cases+='\n'+(ROOT/'tests/receiver_cases.c').read_text(encoding='utf-8')
cases+='\n'+(ROOT/'tests/receiver_settings_cases.c').read_text(encoding='utf-8')
code+='\n'+cases
code=code.replace('while (true)','while (test_steps-- > 0)')
out=ROOT/'build/receiver-host-tests';out.mkdir(parents=True,exist_ok=True)
c=out/'checks.c';dll=out/'checks.dll';c.write_text(code,encoding='utf-8')
clang=sys.argv[1]
subprocess.run([clang,'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe','-DAUX_OUTPUT_RECEIVER',
               '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
               '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)],check=True)
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
(out/'result.json').write_text(json.dumps({'passed':True,'cases':names},indent=2)+'\n',encoding='utf-8')
print(f'{len(names)} receiver scenarios passed')
