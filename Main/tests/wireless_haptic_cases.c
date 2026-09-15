enum { SAMPLE, LINK, RECOVER, READ_FAIL, OVERFLOW, MODE, CONFIG_CHANGE, SLEEP,
       READY, FAULT, STRENGTH, AGE, STOP_DRAIN, START_DRAIN, OLD_FRAME, STALE_FRAME, NATIVE_FRAME, VBUS };
typedef struct { unsigned kind, z, tips, x, y, arg; } step_t;
#define UP {SAMPLE,0,0,1100,700,0}
#define TOUCH(z) {SAMPLE,z,1,1100,700,0}
#define AT(z,x,y) {SAMPLE,z,1,x,y,0}
#define EVENT(k,a) {k,0,0,0,0,a}
static const step_t *steps;
static unsigned step_index, host_down, host_up, host_actions, host_reports, last_buttons;
static bool drain_enabled;
static uint32_t saved_output_generation;

static void drain_haptic(void)
{
    surface_haptic_event_t event;
    while(surface_runtime_pop(&haptic,now,&event)) {
        if(event.release)++played_release;else ++played_press;
        if(surface_haptic_play_event(&event.pair,event.release)!=BSP_STATUS_OK)++hardware_errors;
    }
}
static void drain_host(void)
{
    if(!drain_enabled)return;
    input_report_t r;
    for(unsigned i=0;i<64 && input_take_report(&r);++i) {
        unsigned buttons=r.mode==PTP_MODE?r.data.ptp.buttons:r.data.mouse.buttons;
        if(!r.release){++host_reports;if(buttons&&!last_buttons)++host_down;if(!buttons&&last_buttons)++host_up;}
        last_buttons=buttons;input_report_ack(&r);
    }
    if(input_output_ready(input_generation())) {
        aux_output_event_t e;
        for(unsigned i=0;i<64 && aux_output_take_event(&e,input_generation(),now);++i) {
            if(e.action)++host_actions;
            aux_output_event_complete(true);
        }
    }
}
static void capture_at(unsigned z,unsigned tips,unsigned x,unsigned y,uint32_t output_gen)
{
    uint8_t bytes[64]={0x40};
    for(unsigned i=0;i<tips;++i) {
        unsigned off=4+i*8;uint16_t raw_y=1532-y;
        bytes[off]=1;bytes[off+1]=x;bytes[off+2]=x>>8;
        bytes[off+3]=raw_y;bytes[off+4]=raw_y>>8;bytes[off+5]=z;bytes[off+6]=bytes[off+7]=5;
    }
    input_capture(bytes,true,input_source_generation(),output_gen,now);
}
static void sample_hook(void)
{
    drain_haptic();drain_host();now+=10;
    const step_t *s=&steps[step_index++];
    switch(s->kind) {
    case VBUS: test_vbus=s->arg;break;
    case SAMPLE: capture_at(s->z,s->tips,s->x,s->y,input_generation());break;
    case LINK: saved_output_generation=input_generation();input_set_link(s->arg);break;
    case RECOVER: input_recover();break;
    case READ_FAIL: input_capture(NULL,false,input_source_generation(),input_generation(),now);break;
    case OVERFLOW: for(unsigned i=0;i<17;++i)capture_at(150,1,1100,700,input_generation());break;
    case MODE: input_request_mode(s->arg);break;
    case CONFIG_CHANGE: config_change=true;break;
    case SLEEP: surface_runtime_state(&haptic,SURFACE_SLEEPING);break;
    case READY: surface_runtime_state(&haptic,SURFACE_READY);break;
    case FAULT: surface_runtime_state(&haptic,SURFACE_FAULT);break;
    case STRENGTH: config.bytes[0]=s->arg;break;
    case AGE: now+=s->arg;break;
    case STOP_DRAIN: drain_enabled=false;break;
    case START_DRAIN: drain_enabled=true;break;
    case OLD_FRAME: capture_at(s->z,s->tips,s->x,s->y,saved_output_generation);break;
    case STALE_FRAME: capture_at(150,1,1100,700,input_generation());now+=101;break;
    case NATIVE_FRAME: {
        uint8_t bytes[64]={0};bytes[3]=s->arg;
        input_capture(bytes,true,input_source_generation(),input_generation(),now);break;
    }
    }
}
static void reset_test(unsigned transport,unsigned mode)
{
    now=0;test_vbus=1;ptp_button_press_threshold=2;wifi_hook=NULL;parser_hook=sample_hook;raw_count=0;
    current_mode=transport;current_tp_mode=mode;reports=(report_buffer_t){0};
    ready_mask=0;mode_pending=false;mode_applied=true;requested_mode=mode;request_serial=0;
    haptic=(surface_haptic_runtime_t){0};surface_runtime_state(&haptic,SURFACE_READY);
    device_config_defaults(&config);fail_mode=config_change=false;mode_writes=0;
    input_pipeline_init();reset_input_state();aux_output_reset(false);
    cancellations=played_press=played_release=hardware_errors=0;
    host_down=host_up=host_actions=host_reports=last_buttons=0;drain_enabled=true;
    step_index=0;
}
static void run_steps(const step_t *sequence,unsigned n)
{
    steps=sequence;step_index=0;parser_budget=n;i2c_queue_task(NULL);drain_haptic();drain_host();
}
#define RUN(sequence) run_steps(sequence,sizeof(sequence)/sizeof(sequence[0]))

EXPORT int check_offline_press_release_and_hold(void)
{
    for(unsigned mode=0;mode<2;++mode) {
        reset_test(_2_4_MODE,mode);
        const step_t sequence[]={UP,TOUCH(150),TOUCH(150),TOUCH(150),TOUCH(150),TOUCH(150),UP};
        RUN(sequence);CHECK(played_press==1&&played_release==1&&!host_down&&!host_reports&&!hardware_errors);
        CHECK(mapped_press==21&&mapped_release==15);
    }
    return 0;
}
EXPORT int check_disconnect_recovery_keeps_local_hold(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(150),TOUCH(150),TOUCH(150),
        EVENT(LINK,0),TOUCH(150),EVENT(RECOVER,0),TOUCH(150),EVENT(LINK,3),TOUCH(150),UP,
        TOUCH(150),TOUCH(150),TOUCH(150),UP};
    RUN(sequence);CHECK(played_press==2&&played_release==2&&host_down==2);
    CHECK(input_source_generation()==1&&!cancellations);return 0;
}
EXPORT int check_reconnect_drops_offline_tap_and_corner(void)
{
    reset_test(_2_4_MODE,MOUSE_MODE);
    const step_t tap[]={UP,TOUCH(40),EVENT(LINK,3),UP,TOUCH(40),TOUCH(40)};
    RUN(tap);CHECK(!host_down); /* The offline tap cannot seed a double-tap drag. */
    reset_test(_2_4_MODE,PTP_MODE);config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=1;
    config.bytes[CFG_EDGE_REPEAT]=0x10;
    const step_t point[]={UP,AT(150,0,0),AT(150,0,0),EVENT(LINK,3),EVENT(AGE,1000),AT(150,0,0),UP,
        AT(150,0,0),UP};
    RUN(point);CHECK(host_actions==1&&!played_press&&!host_down);return 0;
}
EXPORT int check_old_raw_frames_cannot_cross_reconnect(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={UP,EVENT(LINK,3),{OLD_FRAME,0,0,1100,700,0},
        {OLD_FRAME,150,1,1100,700,0},{OLD_FRAME,150,1,1100,700,0},{OLD_FRAME,150,1,1100,700,0},
        UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&!host_down&&!host_reports);return 0;
}
EXPORT int check_output_backpressure_preserves_local_feedback(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={EVENT(LINK,3),UP,EVENT(STOP_DRAIN,0),TOUCH(150),TOUCH(150),TOUCH(150),
        EVENT(AGE,150),EVENT(START_DRAIN,0),TOUCH(150),UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&input_source_generation()==1&&!cancellations);
    /* Fill with alternating button edges rather than coalescible motion. */
    input_set_link(3);input_observe(input_generation(),true);drain_host();
    input_observe(input_generation(),true);
    uint32_t generation=input_generation();input_report_t r={.mode=PTP_MODE,.time_ms=now};
    for(unsigned i=0;i<=REPORT_BUFFER_CAPACITY;++i){r.data.ptp.buttons=i&1;input_publish(generation,&r,false);}
    CHECK(input_generation()!=generation&&input_source_generation()==1&&!cancellations);return 0;
}
EXPORT int check_source_errors_require_real_lift(void)
{
    const unsigned errors[]={READ_FAIL,OVERFLOW,CONFIG_CHANGE,STALE_FRAME,NATIVE_FRAME};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
        reset_test(_2_4_MODE,PTP_MODE);
        step_t sequence[]={UP,TOUCH(150),TOUCH(150),TOUCH(150),EVENT(READ_FAIL,0),
            TOUCH(150),TOUCH(150),TOUCH(150),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP};
        sequence[4].kind=errors[i];RUN(sequence);
        CHECK(played_press==2&&played_release==1&&cancellations&&input_source_generation()>1);
    }
    return 0;
}
EXPORT int check_mode_and_source_generation_races(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={UP,TOUCH(150),TOUCH(150),TOUCH(150),EVENT(MODE,PTP_MODE),TOUCH(150),
        EVENT(MODE,MOUSE_MODE),TOUCH(150),TOUCH(150),TOUCH(150),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP};
    RUN(sequence);CHECK(mode_writes==1&&played_press==2&&played_release==1);
    uint32_t old=input_source_generation();input_source_recover("race");
    input_source_button(old,true);CHECK(!haptic.count);
    input_frame_t frame;uint8_t bytes[64]={0x40};
    input_capture(bytes,true,old,input_generation(),now);CHECK(!input_next_frame(&frame));return 0;
}
EXPORT int check_haptic_disabled_sleep_and_fault(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={UP,EVENT(STRENGTH,0),TOUCH(150),TOUCH(150),TOUCH(150),UP,
        EVENT(STRENGTH,63),EVENT(SLEEP,0),TOUCH(150),TOUCH(150),TOUCH(150),EVENT(READY,0),
        TOUCH(150),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP,EVENT(FAULT,0),
        TOUCH(150),TOUCH(150),TOUCH(150),UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&haptic.state==SURFACE_FAULT);return 0;
}
EXPORT int check_regions_and_multiple_contacts_suppress_force(void)
{
    reset_test(_2_4_MODE,PTP_MODE);config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
    const step_t sequence[]={UP,AT(150,1100,0),AT(150,1100,0),AT(150,1100,0),UP,
        {SAMPLE,150,2,1100,700,0},{SAMPLE,150,2,1100,700,0},{SAMPLE,150,2,1100,700,0},UP};
    RUN(sequence);CHECK(!played_press&&!played_release&&!host_actions);return 0;
}
EXPORT int check_usb_ble_recovery_still_cancels_local_feedback(void)
{
    const unsigned transports[]={WIRED_MODE,BLE_MODE};
    for(unsigned i=0;i<2;++i) {
        reset_test(transports[i],PTP_MODE);
        const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(150),TOUCH(150),TOUCH(150),
            EVENT(RECOVER,0),TOUCH(150),TOUCH(150),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP};
        RUN(sequence);CHECK(played_press==2&&played_release==1&&cancellations&&host_down==2);
    }
    return 0;
}

EXPORT int check_simulated_tap_drag_and_native_buttons(void)
{
    reset_test(_2_4_MODE,MOUSE_MODE);
    const step_t tap_drag[]={EVENT(LINK,3),UP,TOUCH(40),UP,TOUCH(40),EVENT(AGE,180),TOUCH(40),TOUCH(40),UP};
    RUN(tap_drag);CHECK(host_down==2&&host_up==2&&played_press==2&&played_release==2);
    reset_test(_2_4_MODE,MOUSE_MODE);
    const step_t native[]={EVENT(NATIVE_FRAME,0),EVENT(NATIVE_FRAME,1),EVENT(NATIVE_FRAME,1),
        EVENT(RECOVER,0),EVENT(NATIVE_FRAME,1),EVENT(NATIVE_FRAME,0)};
    RUN(native);CHECK(played_press==1&&played_release==1&&!host_down&&!cancellations);return 0;
}

EXPORT int check_pressure_release_without_lift_and_stale_haptics(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t pressure[]={UP,TOUCH(150),TOUCH(150),TOUCH(150),
        TOUCH(0),TOUCH(0),TOUCH(0),TOUCH(0),TOUCH(0),TOUCH(0),TOUCH(0),TOUCH(0)};
    RUN(pressure);CHECK(played_press==1&&played_release==1);
    surface_runtime_button(&haptic,true,63,now);now+=101;drain_haptic();
    CHECK(played_press==1&&played_release==1&&haptic.dropped==1);
    surface_runtime_button(&haptic,false,63,now);drain_haptic();CHECK(played_release==1);return 0;
}

EXPORT int check_pair_overflow_and_stale_report_preserve_source(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t ready[]={EVENT(LINK,3),UP};RUN(ready);
    input_source_button(input_source_generation(),true);CHECK(haptic.count==1);
    uint32_t generation=input_generation();input_report_t r={.mode=PTP_MODE,.time_ms=now};
    for(unsigned i=0;i<REPORT_BUFFER_CAPACITY-1;++i){r.data.ptp.buttons=i&1;CHECK(input_publish(generation,&r,false));}
    CHECK(!input_publish_pair(generation,&r,&r));CHECK(input_generation()!=generation);
    CHECK(input_source_generation()==1&&!cancellations&&haptic.count==1);
    input_observe(input_generation(),true);drain_host();input_observe(input_generation(),true);
    r.generation=input_generation();now+=101;CHECK(!input_report_current(&r));
    CHECK(input_source_generation()==1&&!cancellations&&haptic.count==1);return 0;
}

static uint32_t fail_packet_type, immediate_fail_type;
static unsigned sends[10];
static bool radio_action_queued;
static esp_err_t esp_now_send(const uint8_t *mac,const uint8_t *packet,unsigned length)
{
    (void)mac;if(length!=38){++hardware_errors;return ESP_FAIL;}
    uint32_t type=wire_u32(packet);if(type<10)++sends[type];
    /* Let the startup release/cancel finish before failing the requested action. */
    bool can_fail=type!=WIRE_AUX||radio_action_queued;
    if(can_fail&&type==immediate_fail_type)return ESP_FAIL;
    send_callback(NULL,can_fail&&type==fail_packet_type?ESP_NOW_SEND_FAIL:ESP_NOW_SEND_SUCCESS);
    return ESP_OK;
}
static void radio_hook(void) { now+=10; }
static void radio_aux_hook(void)
{
    now+=10;input_observe(input_generation(),true);
    if(!radio_action_queued&&input_output_ready(input_generation())) {
        radio_action_queued=true;aux_output_hold(1,1,input_generation(),now);
    }
}
static void reset_radio(void)
{
    reset_test(_2_4_MODE,PTP_MODE);memset(sends,0,sizeof(sends));
    fail_packet_type=immediate_fail_type=UINT32_MAX;send_done=false;send_status=ESP_NOW_SEND_SUCCESS;
    acknowledged=true;acknowledged_at=0;surface=(wire_surface_t){WIRE_VERSION,0,1};
    wifi_hook=radio_hook;radio_action_queued=false;
    test_settings_reply_pending=false;test_settings_completions=0;
}
EXPORT int check_control_failures_do_not_reset_input(void)
{
    for(unsigned immediate=0;immediate<2;++immediate)for(unsigned kind=0;kind<2;++kind) {
        reset_radio();unsigned type=kind?WIRE_SURFACE:ALIVE_MODE;
        if(immediate)immediate_fail_type=type;else fail_packet_type=type;
        wifi_budget=230;wifi_send_task(NULL);wifi_hook=NULL;
        CHECK(sends[type]>=2&&sends[type]<=3&&reports.stats.submit_failures>=2);
        CHECK(input_source_generation()==1&&!cancellations);
        CHECK(reports.stats.recoveries==2); /* init plus first acknowledged link only */
    }
    return 0;
}
EXPORT int check_pointer_failure_and_ack_timeout_only_recover_output(void)
{
    for(unsigned immediate=0;immediate<2;++immediate) {
        reset_radio();input_set_link(3);input_observe(input_generation(),true);
        surface_runtime_button(&haptic,true,63,now);
        if(immediate)immediate_fail_type=WIRELESS_HAPTIC_PTP_MODE;else fail_packet_type=WIRELESS_HAPTIC_PTP_MODE;
        wifi_budget=10;wifi_send_task(NULL);wifi_hook=NULL;
        CHECK(sends[WIRELESS_HAPTIC_PTP_MODE]&&reports.stats.submit_failures&&reports.stats.recoveries>2);
        CHECK(input_source_generation()==1&&!cancellations&&haptic.down&&haptic.count==1);
    }
    reset_radio();wifi_budget=260;wifi_send_task(NULL);wifi_hook=NULL;
    CHECK(!ready_mask&&input_source_generation()==1&&!cancellations);return 0;
}

EXPORT int check_auxiliary_send_failure_preserves_source(void)
{
    for(unsigned immediate=0;immediate<2;++immediate) {
        reset_radio();wifi_hook=radio_aux_hook;
        if(immediate)immediate_fail_type=WIRE_AUX;else fail_packet_type=WIRE_AUX;
        wifi_budget=20;wifi_send_task(NULL);wifi_hook=NULL;
        CHECK(radio_action_queued);
        CHECK(sends[WIRE_AUX]);
        CHECK(reports.stats.submit_failures);
        CHECK(reports.stats.recoveries>2);
        CHECK(input_source_generation()==1&&!cancellations);
    }
    return 0;
}
EXPORT int check_settings_ack_failure_preserves_local_feedback(void)
{
    for(unsigned immediate=0;immediate<2;++immediate) {
        reset_radio();test_settings_reply_pending=true;
        surface_runtime_button(&haptic,true,63,now);
        if(immediate)immediate_fail_type=WIRE_SETTINGS_ACK;else fail_packet_type=WIRE_SETTINGS_ACK;
        wifi_budget=10;wifi_send_task(NULL);wifi_hook=NULL;
        CHECK(sends[WIRE_SETTINGS_ACK] && test_settings_completions && test_settings_reply_pending);
        CHECK(reports.stats.recoveries==2 && input_source_generation()==1 && !cancellations && haptic.down);
    }
    reset_radio();test_settings_reply_pending=true;
    wifi_budget=10;wifi_send_task(NULL);wifi_hook=NULL;
    CHECK(sends[WIRE_SETTINGS_ACK]==1 && test_settings_completions==1 && !test_settings_reply_pending);
    return 0;
}
