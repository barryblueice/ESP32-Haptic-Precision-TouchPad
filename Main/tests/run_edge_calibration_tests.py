"""Check production edge sampling with the existing deterministic RMI transport mock."""
from pathlib import Path

# Reuse just the transport fixture declarations, without running the old write experiment.
fixture_source = Path(__file__).with_name('run_rmi_probe_tests.py').read_text(encoding='utf-8')
exec(fixture_source.split("code += '\\n' + body(")[0])
code += '\n' + body(ROOT / 'main/main.c').split('void app_main(void)')[0]
code += r'''
static uint8_t packet[64];
static void sample(cal_stats_t *s, unsigned type, unsigned x, unsigned y, unsigned major, unsigned minor) {
    memset(packet,0,64);setword(packet,64);packet[2]=12;packet[3]=0x18;
    packet[4]=type;setword(packet+5,x);setword(packet+7,y);packet[10]=major;packet[11]=minor;
    cal_collect(packet,s);
}
static void stroke(cal_stats_t *s, unsigned y) {
    sample(s,0,0,0,0,0);
    sample(s,1,900,y,5,5);sample(s,1,1000,y,5,5);sample(s,1,1100,y,5,5);
    sample(s,0,0,0,0,0);
}
EXPORT int check_two_edges_and_width_units(void) {
    input_size=64;cal_stats_t high={0},low={0};cal_result_t h,l;
    for(unsigned i=0;i<8;++i){stroke(&high,1524);stroke(&low,18);}
    CHECK(cal_recommend(&high,&h)&&cal_recommend(&low,&l));
    CHECK(h.high&&!l.high&&h.entry_p90==8&&h.hold_p90==8&&h.width_percent==2);
    CHECK(l.entry_p90==18&&l.hold_p90==18&&l.width_percent==3);return 0;
}
EXPORT int check_coverage_rejects_fixed_y(void) {
    cal_stats_t s={.samples=200,.min_x=0,.max_x=2298,.min_y=1524,.max_y=1532};
    CHECK(!cal_coverage(&s));s.min_y=0;CHECK(cal_coverage(&s));
    s.samples=0;CHECK(!cal_coverage(&s));return 0;
}
EXPORT int check_incomplete_and_wrong_side_rejected(void) {
    cal_stats_t s={0};cal_result_t r;
    for(unsigned i=0;i<7;++i)stroke(&s,1524);
    CHECK(!cal_recommend(&s,&r));stroke(&s,0);CHECK(!cal_recommend(&s,&r));
    s=(cal_stats_t){0};for(unsigned i=0;i<8;++i)stroke(&s,766);
    CHECK(!cal_recommend(&s,&r));return 0;
}
EXPORT int check_initial_confidence_and_short_contacts(void) {
    cal_stats_t s={0};sample(&s,0,0,0,0,0);
    sample(&s,1,900,1524,17,5);sample(&s,1,900,1524,5,5);
    sample(&s,1,1000,1524,5,5);sample(&s,1,1100,1524,5,5);sample(&s,0,0,0,0,0);
    CHECK(s.count==1&&s.starts_untrusted==1&&s.untrusted==1);
    CHECK(s.strokes[0].first_untrusted&&s.strokes[0].first_y==1524);
    sample(&s,1,900,1532,5,5);sample(&s,0,0,0,0,0);
    CHECK(s.count==1&&s.short_strokes==1);return 0;
}
EXPORT int check_lift_gate_and_multitouch(void) {
    cal_stats_t s={0};sample(&s,1,900,1532,5,5);CHECK(!s.samples&&!s.active);
    sample(&s,0,0,0,0,0);sample(&s,1,900,1532,5,5);
    packet[12]=1;CHECK(cal_collect(packet,&s)==ESP_OK);
    sample(&s,1,1000,1532,5,5);sample(&s,1,1100,1532,5,5);sample(&s,0,0,0,0,0);
    CHECK(!s.count&&s.multi==1);return 0;
}
EXPORT int check_out_of_range_and_invalid_frames(void) {
    cal_stats_t s={0};sample(&s,0,0,0,0,0);sample(&s,1,2299,1532,5,5);
    sample(&s,1,1000,1533,5,5);sample(&s,3,1000,1532,5,5);
    CHECK(!s.samples&&s.untrusted==3);
    packet[2]=11;CHECK(cal_collect(packet,&s)==ESP_OK&&s.ignored==1);
    packet[2]=12;packet[3]=0x1a;CHECK(cal_collect(packet,&s)==ESP_OK&&s.ignored==2);
    setword(packet,0);CHECK(cal_collect(packet,&s)==ESP_ERR_INVALID_STATE);return 0;
}
EXPORT int check_stroke_weighting_and_hold_margin(void) {
    cal_stats_t s={0};cal_result_t r;for(unsigned i=0;i<8;++i)stroke(&s,1532);
    s.strokes[0].samples=10000;s.strokes[0].min_y=1450;
    CHECK(cal_recommend(&s,&r)&&r.entry_p90==0&&r.hold_p90==82&&r.width_percent==7);
    s.overflow=1;CHECK(!cal_recommend(&s,&r));return 0;
}
EXPORT int check_slot_change_without_lift_rejected(void) {
    cal_stats_t s={0};sample(&s,0,0,0,0,0);sample(&s,1,900,1532,5,5);
    memcpy(packet+12,packet+4,8);memset(packet+4,0,8);cal_collect(packet,&s);
    sample(&s,1,1000,1532,5,5);sample(&s,1,1100,1532,5,5);sample(&s,0,0,0,0,0);
    CHECK(!s.count);return 0;
}
EXPORT int check_real_device_query_without_border_writes(void) {
    clock_us=0;errors=0;pending=0;fault=0;chunk_limit=60;selected_page=-1;
    real_fixture=true;read_channel_failed=false;border_test.valid=false;
    uint8_t v[]={0xfa,8,0xfc,5,0x48,0x3d,0x48,0x3d,0,0,0,0,30,20};memcpy(control_value,v,14);
    CHECK(probe()==ESP_OK&&border_test.valid&&!control_writes&&!errors);
    control_value[10]=128;memcpy(border_test.original,control_value,14);
    CHECK(calibrate_edges()==ESP_ERR_INVALID_STATE&&!control_writes);return 0;
}
EXPORT int check_prepare_idle_survives_phase_reset(void) {
    input_size=64;cal_stats_t s={0};sample(&s,0,0,0,0,0);
    cal_begin_phase(&s);CHECK(s.armed&&!s.frames&&!s.received);
    sample(&s,1,900,1500,5,5);
    CHECK(s.samples==1&&s.raw_samples==1&&!s.waiting_lift);return 0;
}
EXPORT int check_prepare_held_contact_requires_new_lift(void) {
    cal_stats_t s={0};sample(&s,0,0,0,0,0);sample(&s,1,900,1500,5,5);
    cal_begin_phase(&s);CHECK(!s.armed&&!s.active);
    sample(&s,1,1000,1500,5,5);
    CHECK(!s.samples&&s.waiting_lift==1&&s.raw_samples==1);
    sample(&s,0,0,0,0,0);sample(&s,1,1100,1500,5,5);CHECK(s.samples==1);return 0;
}
EXPORT int check_raw_coordinates_visible_when_trust_filter_rejects(void) {
    cal_stats_t s={0};sample(&s,0,0,0,0,0);sample(&s,1,500,700,30,5);
    CHECK(s.received==2&&s.frames==2&&s.raw_samples==1&&!s.samples&&s.untrusted==1);
    CHECK(s.raw_min_x==500&&s.raw_max_x==500&&s.raw_min_y==700&&s.raw_max_y==700);return 0;
}
'''
out = ROOT / 'build/edge-calibration-host-tests'
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
(out / 'result.json').write_text(json.dumps({'passed': True, 'cases': names}, indent=2)+'\n', encoding='utf-8')
