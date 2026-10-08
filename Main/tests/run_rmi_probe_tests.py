"""Exercise the standalone production RMI probe with a deterministic HID device."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

base = (ROOT / 'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
record = (ROOT.parent / 'Hacking/README.md').read_text(encoding='utf-8').split('```')[1]
fixture = bytes.fromhex(record.strip())
code = base + '\n#include <stdarg.h>\n'
code += 'static const uint8_t fixture_report[] = {' + ','.join(map(str, fixture)) + '};\n'
code += r'''
typedef int esp_err_t;
typedef int i2c_master_bus_handle_t;
typedef int i2c_master_dev_handle_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define TP_INT_GPIO 34
#define TP_RESET_GPIO 33
#define HID_DESC_REG 1
#define pdMS_TO_TICKS(x) (x)
static int64_t clock_us;
static int fault, errors, writes, reads, mode_count, border_count, border[4];
static unsigned request_address, pending, received, chunk_limit, attn_left;
static uint8_t reply[512];
static uint8_t control_value[14];
static bool real_fixture;
static unsigned control_writes, fail_control_write, ignore_control_write, timeout_verify;
static unsigned mutation_errors, reset_count, phase_count;
static unsigned first_ylo;
static bool ignore_all_restores;
static bool prefix(const char *a, const char *b) { while (*b) if (*a++ != *b++) return false; return true; }
static void log_info(const char *fmt, ...) {
    if (!prefix(fmt, "RESULT: INACTIVE_BORDER ")) return;
    ++border_count; va_list ap; va_start(ap, fmt);
    for (int i=0;i<4;++i) border[i]=va_arg(ap,int);
    va_end(ap);
}
#define ESP_LOGI(tag, ...) log_info(__VA_ARGS__)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOG_BUFFER_HEX_LEVEL(...) ((void)0)
static const char *esp_err_to_name(int e) { (void)e;return "error"; }
static int64_t esp_timer_get_time(void) { return clock_us; }
static void vTaskDelay(unsigned ticks) { clock_us += ticks*1000; }
static unsigned word(const uint8_t *p) { return p[0]|(p[1]<<8); }
static void setword(uint8_t *p,unsigned v) { p[0]=v;p[1]=v>>8; }
static int gpio_get_level(int pin) { (void)pin; return pending && fault!=1 ? 0 : 1; }
static int gpio_set_level(int pin,int value) {
    (void)pin;if(!value){++reset_count;pending=0;fault=0;}return ESP_OK;
}
static void fake_register(unsigned addr, unsigned len) {
    memset(reply,0,sizeof(reply));
    if(addr==0x00e9) { uint8_t p[]={0x10,0x20,0x30,0x40,1,1};memcpy(reply,p,6); }
    if(addr==0x00e3) { uint8_t p[]={0x60,0x70,0x80,0xa0,1,0x12};memcpy(reply,p,6); }
    if(addr==0x0010) { reply[0]=1;memcpy(reply+11,"TESTDEVICE",10); }
    if(addr==0x0060) reply[0]=1;
    if(addr==0x0064) reply[0]=3;
    if(addr==0x0065) { reply[0]=6;reply[1]=5;reply[2]=1; }
    if(addr==0x0066) { uint8_t p[]={2,1,3,1,14,15};memcpy(reply,p,6); }
    if(addr==0x0082 || addr==0x0019) memcpy(reply,control_value,14);
    if(real_fixture) {
        if(addr==0x00e3)reply[2]=0x19;
        if(addr==0x0010)memcpy(reply+11,"TM3651-001",10);
        if(addr==0x0064)reply[0]=12;
        if(addr==0x0065) { uint8_t p[]={0x1e,0,0x8f,0xd3,0x10,0,0,0,0,0,0,0x0c};memcpy(reply,p,sizeof(p)); }
        if(addr==0x0066) { uint8_t p[]={0x0e,0x0f,0x14,0x87,0x12,0x15,0xaf,2,0x16,0xdd,1,7,0x83,2,5,4,2,2,1,2,4,5,3,7,1,1,6,1,0x28,7};memcpy(reply,p,sizeof(p)); }
    }
    if(fault==7 && addr==0x0066) reply[5]=3; /* Borders absent */
    if(fault==8 && addr==0x0066) reply[4]=10; /* Truncated Control8 */
    if(fault==9 && addr==0x0060) reply[0]=0; /* No register descriptors */
    if(fault==10 && addr==0x0064) reply[0]=36; /* Oversized presence */
    if(fault==11 && addr==0x0066) reply[5]=0x80; /* Unterminated subpacket bitmap */
    if(fault==12 && addr==0x0065) reply[2]=0; /* No Control8 */
    pending=len;received=0;request_address=addr;
}
static int i2c_master_transmit_receive(int dev,const uint8_t *a,size_t an,void *out,size_t n,int timeout) {
    (void)dev;(void)timeout;if(an!=2){++errors;return ESP_FAIL;}
    uint8_t *p=out;memset(p,0,n);
    if(word(a)==0x20 && n==30) {
        setword(p,30);setword(p+2,0x100);setword(p+4,sizeof(fixture_report));setword(p+6,0x21);
        setword(p+8,0x24);setword(p+10,64);setword(p+12,0x25);setword(p+14,23);
        setword(p+16,0x22);setword(p+18,0x23);setword(p+20,0x06cb);setword(p+22,0xce43);return ESP_OK;
    }
    if(word(a)==0x21 && n==sizeof(fixture_report)) { memcpy(p,fixture_report,n);return ESP_OK; }
    ++errors;return ESP_FAIL;
}
static int i2c_master_transmit(int dev,const uint8_t *p,size_t n,int timeout) {
    (void)dev;(void)timeout;++writes;
    if(word(p)==0x22) {
        if(n==4 && p[2]==0 && (p[3]==8 || p[3]==1)) return ESP_OK;
        if(n==13 && p[2]==0x3f && p[3]==3 && p[4]==15 && word(p+5)==0x23 &&
           word(p+7)==6 && p[9]==15 && p[10]==1 && !p[11] && !p[12]) { ++mode_count;return ESP_OK; }
        ++errors;return ESP_FAIL;
    }
    if(word(p)!=0x25 || n!=25 || word(p+2)!=23) { ++errors;return ESP_FAIL; }
    if(p[4]==9) {
        if(p[5]==14 && word(p+6)==0x19 && real_fixture) {
            ++control_writes;
            if(control_writes==1)first_ylo=p[18];
            for(unsigned i=0;i<14;++i)if(i!=10 && p[8+i]!=control_value[i])++mutation_errors;
            if(control_writes!=ignore_control_write && !(ignore_all_restores && control_writes>1))memcpy(control_value,p+8,14);
            for(unsigned i=22;i<n;++i)if(p[i])++errors;
            if(control_writes==fail_control_write)return ESP_FAIL; /* Accepted write, lost ACK. */
            return ESP_OK;
        }
        if(p[5]!=1 || p[6]!=255 || p[7]!=0) ++errors; /* Forbid sensor writes. */
        for(unsigned i=9;i<n;++i) if(p[i]) ++errors;
        return errors?ESP_FAIL:ESP_OK;
    }
    if(p[4]!=10 || p[5]!=0 || pending) { ++errors;return ESP_FAIL; }
    for(unsigned i=10;i<n;++i) if(p[i]) ++errors;
    fake_register(word(p+6),word(p+8));
    if(timeout_verify && control_writes==timeout_verify && word(p+6)==0x19) { fault=1;timeout_verify=0; }
    return ESP_OK;
}
static int i2c_master_receive(int dev,uint8_t *p,size_t n,int timeout) {
    (void)dev;(void)timeout;++reads;
    if(n!=64 || !pending){++errors;return ESP_FAIL;}
    memset(p,0,n);setword(p,64);
    if(attn_left){--attn_left;p[2]=12;p[3]=0x18;p[4]=1;return ESP_OK;}
    if(fault==6)return ESP_FAIL;
    p[2]=11;
    unsigned count=pending>chunk_limit?chunk_limit:pending;
    p[3]=count;memcpy(p+4,reply+received,count);received+=count;pending-=count;
    if(fault==2)p[3]=0;
    if(fault==3)p[3]=61;
    if(fault==4)setword(p,3);
    if(fault==5)setword(p,0);
    return ESP_OK;
}
'''
code += '\n' + body(ROOT / 'tests/fixtures/rmi_border_probe.c').split('void app_main(void)')[0]
code += r'''
static void reset_test(void) {
    clock_us=0;fault=errors=writes=reads=mode_count=border_count=0;
    pending=received=attn_left=0;chunk_limit=60;selected_page=-1;probe_deadline=15000000;
    f01=(rmi_function_t){0};f11=(rmi_function_t){0};f12=(rmi_function_t){0};
    read_channel_failed=known_product=real_fixture=false;
    memset(&border_test,0,sizeof(border_test));
    control_writes=fail_control_write=ignore_control_write=timeout_verify=mutation_errors=reset_count=phase_count=0;
    first_ylo=0;ignore_all_restores=false;
    uint8_t p[]={0xfe,8,0xfc,5,0,16,0,16,10,20,30,40,12,10};memcpy(control_value,p,14);
}
EXPORT int check_recorded_hid_descriptor(void) {
    reset_test();CHECK(discover_hid()==ESP_OK);
    CHECK(report_bytes[9][1]==21 && report_bytes[10][1]==21);
    CHECK(report_bytes[11][0]==62 && report_bytes[12][0]==62 && report_bytes[15][2]==4);return 0;
}
EXPORT int check_f12_end_to_end_interleaved_and_split(void) {
    reset_test();chunk_limit=3;attn_left=4;CHECK(probe()==ESP_OK);
    CHECK(!errors && mode_count==1 && border_count==1);
    CHECK(border[0]==10 && border[1]==20 && border[2]==30 && border[3]==40);
    CHECK(f01.found && f12.found && !f11.found && request_address==0x82);return 0;
}
EXPORT int check_timeout_and_malformed_replies(void) {
    for(int k=1;k<=6;++k) {
        reset_test();CHECK(discover_hid()==ESP_OK);fault=k;uint8_t v[6];
        int err=rmi_read(0xe9,v,6);CHECK(err!=ESP_OK && !border_count && !errors);
        CHECK(writes==2); /* One page write, one request; no ambiguous retries. */
        if(k==1)CHECK(err==ESP_ERR_TIMEOUT && clock_us>=750000 && clock_us<760000);
    }
    reset_test();CHECK(discover_hid()==ESP_OK);clock_us=15000000;
    uint8_t v;CHECK(rmi_read(0,&v,1)==ESP_ERR_TIMEOUT && !writes);return 0;
}
EXPORT int check_optional_and_broken_f12_layouts(void) {
    for(int k=7;k<=12;++k) {
        reset_test();fault=k;int err=probe();
        CHECK((k==7 ? err==ESP_OK : err!=ESP_OK) && !errors && !border_count);
    }
    return 0;
}
EXPORT int check_control8_offsets_and_extended_sizes(void) {
    uint8_t presence[]={5,1}; /* Control0, Control2, Control8 */
    uint8_t items[]={0,0,0,0,0,1,0,1, 0,1,1,0x80,1, 14,7};
    control8_t c;CHECK(find_control8(presence,2,items,sizeof(items),&c)==ESP_OK);
    CHECK(c.offset==2 && c.size==14 && c.subpackets==7);
    for(unsigned n=0;n<sizeof(items);++n)CHECK(find_control8(presence,2,items,n,&c)!=ESP_OK);
    uint8_t absent[]={0};CHECK(find_control8(absent,1,items,sizeof(items),&c)==ESP_ERR_NOT_FOUND);return 0;
}
EXPORT int check_hid_descriptor_bounds_and_stack(void) {
    uint8_t truncated[]={0x75};CHECK(parse_reports(truncated,1)==ESP_ERR_INVALID_SIZE);
    uint8_t pop[]={0xb4};CHECK(parse_reports(pop,1)==ESP_ERR_INVALID_SIZE);
    uint8_t pushed[]={0x85,9,0x75,8,0x95,20,0xa4,0x95,1,0x91,2,0xb4,0x91,2};
    CHECK(parse_reports(pushed,sizeof(pushed))==ESP_OK && report_bytes[9][1]==22);
    uint8_t invalid_id[]={0x85,0};CHECK(parse_reports(invalid_id,2)==ESP_ERR_INVALID_ARG);return 0;
}
static int prepare_experiment(void) {
    reset_test();real_fixture=true;
    uint8_t p[]={0xfa,8,0xfc,5,0x48,0x3d,0x48,0x3d,0,0,0,0,30,20};memcpy(control_value,p,14);
    CHECK(probe()==ESP_OK && border_test.valid && border_test.addr==0x19);
    CHECK(!memcmp(border_test.original,p,14));return 0;
}
EXPORT int check_real_device_layout_and_restore(void) {
    CHECK(prepare_experiment()==0);CHECK(experiment()==ESP_OK);
    CHECK(control_writes==2 && first_ylo==128 && border_test.attempted && border_test.restored);
    CHECK(!memcmp(control_value,border_test.original,14) && !errors && !mutation_errors);
    CHECK(clock_us>=70000000);return 0;
}
EXPORT int check_rejected_write_restores_without_modified_phase(void) {
    CHECK(prepare_experiment()==0);ignore_control_write=1;
    CHECK(experiment()!=ESP_OK && border_test.restored && control_writes==2);
    CHECK(clock_us<25000000 && !memcmp(control_value,border_test.original,14));return 0;
}
EXPORT int check_accepted_write_with_bus_error_restores(void) {
    CHECK(prepare_experiment()==0);fail_control_write=1;
    CHECK(experiment()!=ESP_OK && border_test.restored && control_writes==2);
    CHECK(!memcmp(control_value,border_test.original,14) && !mutation_errors);return 0;
}
EXPORT int check_timeout_reset_recovery_restores(void) {
    CHECK(prepare_experiment()==0);timeout_verify=1;
    CHECK(experiment()!=ESP_OK && border_test.restored && reset_count==1 && control_writes==3);
    CHECK(!pending && !memcmp(control_value,border_test.original,14) && !errors);return 0;
}
EXPORT int check_failed_restore_ack_retries_original(void) {
    CHECK(prepare_experiment()==0);fail_control_write=2;
    CHECK(experiment()==ESP_OK && border_test.restored && reset_count==1 && control_writes==3);
    CHECK(!memcmp(control_value,border_test.original,14) && !mutation_errors);return 0;
}
EXPORT int check_unknown_module_never_writes(void) {
    reset_test();CHECK(probe()==ESP_OK && !border_test.valid);
    CHECK(experiment()==ESP_ERR_NOT_SUPPORTED && !control_writes && !border_test.attempted);return 0;
}
EXPORT int check_touch_report_filtering_and_coordinates(void) {
    reset_test();input_size=64;touch_stats_t s={0};uint8_t p[64]={64,0,12,0x18};
    p[4]=1;setword(p+5,40);setword(p+7,800);CHECK(collect_touch(p,&s)==ESP_OK);
    CHECK(s.min_y==800 && s.max_y==800);
    setword(p+5,0);setword(p+7,0);CHECK(collect_touch(p,&s)==ESP_OK && s.samples==2 && s.min_x==0 && s.max_x==40);
    CHECK(s.min_y==0 && s.max_y==800 && s.last_y==0);
    p[2]=11;CHECK(collect_touch(p,&s)==ESP_OK && s.samples==2);
    p[2]=12;p[4]=3;CHECK(collect_touch(p,&s)==ESP_OK && s.samples==2);
    p[4]=1;p[3]=0x1a;CHECK(collect_touch(p,&s)==ESP_OK && s.samples==2);
    p[3]=0x18;setword(p+5,65535);CHECK(collect_touch(p,&s)==ESP_OK && s.samples==2);
    p[0]=0;CHECK(collect_touch(p,&s)==ESP_ERR_INVALID_STATE && read_channel_failed);return 0;
}
EXPORT int check_restore_failure_is_not_reported_as_success(void) {
    CHECK(prepare_experiment()==0);ignore_all_restores=true;
    CHECK(experiment()!=ESP_OK && !border_test.restored && reset_count==1);
    CHECK(control_writes==3 && !mutation_errors);return 0;
}
EXPORT int check_ylo_saturation_and_original_change_guard(void) {
    CHECK(prepare_experiment()==0);control_value[10]=border_test.original[10]=200;
    CHECK(experiment()==ESP_OK && first_ylo==255 && control_value[10]==200);
    CHECK(prepare_experiment()==0);control_value[10]=border_test.original[10]=255;
    CHECK(experiment()==ESP_ERR_NOT_SUPPORTED && !control_writes);
    CHECK(prepare_experiment()==0);control_value[9]=1;
    CHECK(experiment()==ESP_ERR_INVALID_STATE && !control_writes && !border_test.attempted);return 0;
}
'''
out = ROOT / 'build/rmi-probe-host-tests'
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
