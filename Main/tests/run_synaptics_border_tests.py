"""Exercise the normal firmware's startup query with the recorded HID transport."""
from pathlib import Path

# Reuse the deterministic device, not the standalone write experiment.
fixture_source = Path(__file__).with_name('run_rmi_probe_tests.py').read_text(encoding='utf-8')
exec(fixture_source.split("code += '\\n' + body(")[0])
code = code.replace('static bool real_fixture;', '''static bool real_fixture, use_f11, extended_device;
static int clip_seen;
static unsigned captures, fifo_position, capture_type, image_completes;
static bool f54_stuck, f54_tx_fail, bad_mapping;
static uint8_t f54_caps;
static unsigned f34_reads, f51_reads;
static bool f01_optional, broken_query_descriptor;
static unsigned lts_reads, holdoff_reads, package_reads;
static unsigned long observed_firmware;
''')
code = code.replace('    if (!prefix(fmt, "RESULT: INACTIVE_BORDER ")) return;', '''
    if (prefix(fmt, "F54 IMAGE_COMPLETE")) { ++image_completes; return; }
    if (prefix(fmt, "F01 firmware_build_id=")) {
        va_list ap; va_start(ap, fmt); observed_firmware=va_arg(ap,unsigned long);va_end(ap);return;
    }
    if (prefix(fmt, "RESULT: F11 Query11=")) {
        va_list ap; va_start(ap, fmt); (void)va_arg(ap, int);
        clip_seen = va_arg(ap, int); va_end(ap); return;
    }
    if (!prefix(fmt, "RESULT: INACTIVE_BORDER ")) return;''')
code = code.replace('    pending=len;received=0;request_address=addr;', '''
    if (!use_f11) {
        if (addr==0x61 || addr==0x67) reply[0]=2;
        if (addr==0x62) { reply[0]=2;reply[1]=1; }
        if (addr==0x63) { reply[0]=1;reply[1]=1; }
        if (addr==0x68) { reply[0]=2;reply[1]=2; }
        if (addr==0x69) { reply[0]=16;reply[1]=3; }
    }
    if (extended_device) {
        if (addr==0xdd) { uint8_t p[]={0x27,0,0x11,0,0x41,0x34};memcpy(reply,p,6); }
        if (addr==0x1e9) { uint8_t p[]={0x3b,0x3a,0x10,0,1,0x54};memcpy(reply,p,6); }
        if (addr==0x3e9) { uint8_t p[]={5,4,0,0,1,0x55};memcpy(reply,p,6); }
        if (addr==0x4e9) { uint8_t p[]={0,0,0,0,0x40,0x51};memcpy(reply,p,6); }
        if (addr>=0x27 && addr<=0x2e) ++f34_reads;
        if (addr==0x27) reply[0]=8;
        if (addr==0x28) { reply[0]=1;reply[1]=7; }
        if (addr==0x2a) { reply[0]=1;reply[1]=16;reply[3]=32; }
        if (addr==0x305) { reply[0]=4;reply[1]=3;reply[2]=1; }
        if (addr==0x301) { reply[0]=0;reply[1]=1;reply[2]=255;reply[3]=3; }
        if (addr==0x302) { reply[0]=0;reply[1]=255;reply[2]=2; }
        if (bad_mapping && (addr==0x301 || addr==0x302)) memset(reply,255,len);
        if (addr==0x13b) { reply[0]=4;reply[1]=3;reply[2]=f54_caps;reply[3]=100;reply[5]=2; }
        if (addr==0x13a) reply[0]=(captures && f54_stuck) ? 1 : 0;
        if (addr==0x103) for(unsigned i=0;i<len;++i)reply[i]=(uint8_t)(fifo_position+i);
        if (addr==0x400) { ++f51_reads;reply[0]=0xa5; }
    }
    if (use_f11) {
        if (addr==0x00e3) reply[5]=0x11;
        if (addr==0x0060) { reply[0]=0x18; reply[1]=0x38; }
        if (addr==0x0067) reply[1]=4; /* Query7/8: touch shapes */
        if (addr==0x006b) reply[0]=0x40; /* Query11 after optional queries */
    }
    if (broken_query_descriptor && addr==0x61) reply[0]=0;
    if (f01_optional) {
        if (addr==0x10) reply[1]=0xe4;
        if (addr==0x25 || addr==0x26) reply[0]=1;
        if (addr==0x27) reply[0]=3;
        if (addr==0x21) ++package_reads;
        if (addr==0x22) { reply[0]=0x56;reply[1]=0x34;reply[2]=0x12; }
        if (addr==0x34) ++lts_reads;
        if (addr==0x35) ++holdoff_reads;
    }
    pending=len;received=0;request_address=addr;''')
code = code.replace('    if(p[4]==9) {', '''
    if(p[4]==9 && extended_device) {
        unsigned a=word(p+6);
        if(a==0x100 && p[5]==1) { capture_type=p[8];return ESP_OK; }
        if(a==0x101 && p[5]==2) { fifo_position=word(p+8);return ESP_OK; }
        if(a==0x13a && p[5]==1 && p[8]==1) {
            ++captures;return f54_tx_fail ? ESP_FAIL : ESP_OK;
        }
    }
    if(p[4]==9) {''')
code += '\n' + body(ROOT / 'main/I2C/TP/synaptics_border.c')
code += r'''
static void reset_test(void) {
    clock_us=0;fault=errors=writes=reads=mode_count=border_count=0;
    pending=received=attn_left=0;chunk_limit=60;
    real_fixture=use_f11=false;clip_seen=-1;
    extended_device=f54_stuck=f54_tx_fail=bad_mapping=false;
    captures=fifo_position=capture_type=image_completes=f34_reads=f51_reads=0;f54_caps=0x4c;
    f01_optional=broken_query_descriptor=false;
    lts_reads=holdoff_reads=package_reads=0;observed_firmware=0;
    control_writes=fail_control_write=ignore_control_write=timeout_verify=0;
    mutation_errors=reset_count=0;ignore_all_restores=false;
    uint8_t p[]={0xfa,8,0xfc,5,0x48,0x3d,0x48,0x3d,10,20,30,40,30,20};
    memcpy(control_value,p,14);
}
EXPORT int check_current_values_without_sensor_writes(void) {
    reset_test();real_fixture=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(!errors && !control_writes && mode_count==1 && border_count==1);
    CHECK(border[0]==10 && border[1]==20 && border[2]==30 && border[3]==40);
    CHECK(!pending && clock_us<15000000);return 0;
}
EXPORT int check_repeated_query_does_not_reuse_cached_values(void) {
    reset_test();real_fixture=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    memset(control_value+8,0,4);
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(border_count==2 && !border[0] && !border[1] && !border[2] && !border[3]);
    CHECK(!errors && !control_writes);return 0;
}
EXPORT int check_interleaved_touch_and_fragmented_replies(void) {
    reset_test();chunk_limit=3;attn_left=4;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(border_count==1 && border[2]==30 && !errors && !control_writes);return 0;
}
EXPORT int check_invalid_replies_abort_without_retry(void) {
    for(int k=1;k<=6;++k) {
        reset_test();fault=k;
        CHECK(synaptics_border_log_startup(1)!=ESP_OK);
        CHECK(read_channel_failed && !border_count && !control_writes && !errors);
        unsigned before=writes;uint8_t v;
        CHECK(rmi_read(0xe9,&v,1)==ESP_ERR_INVALID_STATE && writes==before);
        CHECK(clock_us<2000000);
    }
    return 0;
}
EXPORT int check_absent_or_malformed_border_is_not_reported_as_zero(void) {
    for(int k=7;k<=12;++k) {
        reset_test();fault=k;
        int err=synaptics_border_log_startup(1);
        CHECK((k==7 ? err==ESP_OK : err!=ESP_OK));
        CHECK(!border_count && !control_writes && !errors);
    }
    return 0;
}
EXPORT int check_f11_optional_query_offsets(void) {
    reset_test();use_f11=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(clip_seen==1 && !border_count && !control_writes && !errors);return 0;
}
EXPORT int check_packet_offsets_count_registers_not_bytes(void) {
    uint8_t presence[]={5,1};
    uint8_t items[]={0,0,0,0,0,1,0,1, 0,1,1,0x80,1, 14,7};
    control8_t c;
    CHECK(find_control8(presence,2,items,sizeof(items),&c)==ESP_OK);
    CHECK(c.offset==2 && c.size==14 && c.subpackets==7);
    for(unsigned n=0;n<sizeof(items);++n)
        CHECK(find_control8(presence,2,items,n,&c)!=ESP_OK);
    return 0;
}
EXPORT int check_all_functions_and_six_complete_images(void) {
    reset_test();real_fixture=extended_device=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(f34.found && f34.version==2 && f54.found && f55.found && f51.found);
    CHECK(f34_reads==8 && f51_reads==1 && captures==6 && image_completes==6);
    CHECK(mapping_valid && mapped_rx==3 && mapped_tx==2);
    CHECK(capture_type==20 && !errors && !control_writes);
    for(unsigned i=0;i<12;++i)CHECK(report_image[i]==i);
    return 0;
}
EXPORT int check_f54_fifo_chunk_boundaries(void) {
    reset_test();extended_device=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    unsigned before=captures;
    CHECK(capture_f54(3,30,20)==ESP_OK && captures==before+1);
    for(unsigned i=0;i<1200;++i)CHECK(report_image[i]==(uint8_t)i);
    CHECK(fifo_position==1184 && !errors && !control_writes);return 0;
}
EXPORT int check_f54_capability_gating(void) {
    reset_test();extended_device=true;f54_caps=0;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(captures==2 && image_completes==2 && !errors && !control_writes);return 0;
}
EXPORT int check_f54_busy_timeout_stops_remaining_reports(void) {
    reset_test();extended_device=f54_stuck=true;
    CHECK(synaptics_border_log_startup(1)==ESP_ERR_TIMEOUT);
    CHECK(captures==1 && !image_completes && !errors && !control_writes);
    CHECK(clock_us<5000000);return 0;
}
EXPORT int check_f54_write_error_never_retries(void) {
    reset_test();extended_device=f54_tx_fail=true;
    CHECK(synaptics_border_log_startup(1)==ESP_FAIL);
    CHECK(captures==1 && !image_completes && read_channel_failed && !errors && !control_writes);
    return 0;
}
EXPORT int check_disabled_electrodes_do_not_fall_back_to_full_matrix(void) {
    reset_test();extended_device=bad_mapping=true;
    CHECK(synaptics_border_log_startup(1)!=ESP_OK);
    CHECK(!mapping_valid && !captures && !image_completes && !errors);return 0;
}
EXPORT int check_diagnostic_write_allowlist_and_buffer_limit(void) {
    reset_test();extended_device=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    uint8_t force_cal=2,erase=4;unsigned before=writes;
    CHECK(f54_request_write(f54.command,&force_cal,1)==ESP_ERR_INVALID_STATE);
    CHECK(f54_request_write(f34.data+4,&erase,1)==ESP_ERR_INVALID_STATE);
    CHECK(f54_request_write(f12.control,&erase,1)==ESP_ERR_INVALID_STATE);
    CHECK(capture_f54(3,255,255)==ESP_ERR_INVALID_SIZE);
    CHECK(writes==before && !control_writes);return 0;
}
EXPORT int check_f01_optional_lts_and_build_id_layout(void) {
    reset_test();f01_optional=true;
    CHECK(synaptics_border_log_startup(1)==ESP_OK);
    CHECK(lts_reads==1 && holdoff_reads==1 && package_reads==1);
    CHECK(observed_firmware==0x123456 && !errors && !control_writes);return 0;
}
EXPORT int check_bad_query_descriptor_does_not_hide_other_functions(void) {
    reset_test();extended_device=broken_query_descriptor=true;
    CHECK(synaptics_border_log_startup(1)==ESP_ERR_INVALID_SIZE);
    CHECK(border_count==1 && f34_reads==8 && f51_reads==1 && captures==6);
    CHECK(!errors && !control_writes);return 0;
}
'''
out = ROOT / 'build/synaptics-border-host-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out / 'checks.c', out / 'checks.dll'
c.write_text(code, encoding='utf-8')
subprocess.run([sys.argv[1], '-std=c11', '-O1', '-fno-builtin', '-mno-stack-arg-probe',
                '-Werror=implicit-function-declaration', '-shared', '-nostdlib', '-fuse-ld=lld',
                '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)], check=True)
lib = ctypes.CDLL(str(dll))
names = re.findall(r'EXPORT int (check_\w+)\(void\)', code)
for name in names:
    line = getattr(lib, name)()
    if line:
        raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name + ': passed')
(out / 'result.json').write_text(json.dumps({'passed': True, 'cases': names,
    'compiler': sys.argv[1]}, indent=2) + '\n', encoding='utf-8')
