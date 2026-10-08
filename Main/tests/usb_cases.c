static void run_pump(void)
{
    in_usb=true;usb_send_pump(NULL);in_usb=false;
}
static void finish(unsigned instance,bool success)
{
    endpoint_busy[instance]=false;in_usb=true;
    if(success)tud_hid_report_complete_cb(instance,NULL,0);
    else tud_hid_report_failed_cb(instance,HID_REPORT_TYPE_INPUT,NULL,0);
    in_usb=false;
}
static void bus_reset(void)
{
    mounted=false;suspended=false;memset(endpoint_busy,0,sizeof(endpoint_busy));
    uint8_t count=0;const usbd_class_driver_t *d=usbd_app_driver_get_cb(&count);
    if(count!=1||d->open(0,NULL,0))++tx_errors;
    in_usb=true;d->reset(0);in_usb=false;
}
static void usb_event(unsigned id)
{
    if(id==TINYUSB_EVENT_ATTACHED)mounted=true;
    if(id==TINYUSB_EVENT_SUSPENDED)suspended=true;
    if(id==TINYUSB_EVENT_RESUMED)suspended=false;
    tinyusb_event_t event={id};in_usb=true;tinyusb_event_cb(&event,NULL);in_usb=false;
}
static void feature(unsigned mode)
{
    uint8_t value=mode;
    in_usb=true;tud_hid_set_report_cb(1,REPORTID_FEATURE,HID_REPORT_TYPE_FEATURE,&value,1);in_usb=false;
}
static void sample(unsigned tips,unsigned x,unsigned z)
{
    uint8_t data[64]={0x40};
    data[4]=tips?1:0;data[5]=x;data[6]=x>>8;data[7]=0xbc;data[8]=2;
    data[9]=z;data[10]=data[11]=5;
    now+=10;
    input_capture(data,true,input_source_generation(),input_generation(),now);
    parser_budget=1;i2c_queue_task(NULL);
}
static void reset_test(void)
{
    queue_count=0;now=0;current_mode=WIRED_MODE;current_tp_mode=PTP_MODE;
    reports=(report_buffer_t){0};ready_mask=0;mode_pending=mode_applied=false;
    request_serial=0;fail_mode=config_change=false;mode_writes=0;mode_hook=NULL;
    haptic=(surface_haptic_runtime_t){0};surface_runtime_state(&haptic,SURFACE_READY);
    device_config_defaults(&config);wifi_hook=NULL;test_vbus=1;
    test_capabilities=0x3fff;
    input_pipeline_init();reset_input_state();usb_config_init();
    memset(submitted,0,sizeof(submitted));memset(endpoint_reject,0,sizeof(endpoint_reject));
    tx_errors=0;deferred=NULL;deferred_count=0;pump_queued=false;defer_reject=false;
    bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
    wakes=0;
}
static bool publish(unsigned buttons)
{
    input_report_t r={.mode=input_mode(),.time_ms=now};
    if(r.mode==PTP_MODE) {
        r.data.ptp.contact_count=1;r.data.ptp.fingers[0].tip_conf_id=3;r.data.ptp.buttons=buttons;
    } else {r.data.mouse.x=5;r.data.mouse.buttons=buttons;}
    return input_publish(input_generation(),&r,false);
}
static void knuckle_usb_sample(unsigned tips,unsigned x,unsigned axis,unsigned z,unsigned delay)
{
    uint8_t data[64]={0x40};
    data[4]=tips?1:0;data[5]=x;data[6]=x>>8;data[7]=0xbc;data[8]=2;
    data[9]=z;data[10]=data[11]=axis;now+=delay;
    input_capture(data,true,input_source_generation(),input_generation(),now);
    parser_budget=1;i2c_queue_task(NULL);
}
EXPORT int check_usb_knuckle_replay_keeps_ptp_down_move_up(void)
{
    reset_test();feature(3);input_apply_mode_request();
    sample(0,1000,20);input_report_t r;
    while(input_take_report(&r))input_report_ack(&r);
    knuckle_usb_sample(1,1000,3,45,10);uint32_t down=now;
    knuckle_usb_sample(1,1020,3,45,10);
    CHECK(knuckle_frame_count==2&&!reports.count);
    knuckle_usb_sample(1,1100,3,45,10);
    CHECK(!knuckle_frame_count&&input_take_report(&r));
    CHECK(r.mode==PTP_MODE&&(r.data.ptp.fingers[0].tip_conf_id&2));
    CHECK(r.data.ptp.fingers[0].x==1000&&r.data.ptp.scan_time==(uint16_t)(down*10));
    input_report_ack(&r);
    CHECK(input_take_report(&r)&&(r.data.ptp.fingers[0].tip_conf_id&2));
    CHECK(r.data.ptp.scan_time==(uint16_t)(now*10));input_report_ack(&r);
    knuckle_usb_sample(0,0,0,0,10);
    CHECK(input_take_report(&r)&&!(r.data.ptp.fingers[0].tip_conf_id&2));input_report_ack(&r);
    CHECK(!reports.recovering&&!knuckle_state.pending);
    return 0;
}
EXPORT int check_usb_knuckle_rejected_tap_has_ptp_pair(void)
{
    reset_test();feature(3);input_apply_mode_request();
    sample(0,1000,20);input_report_t r;
    while(input_take_report(&r))input_report_ack(&r);
    knuckle_usb_sample(1,1000,3,45,10);uint32_t down=now;
    knuckle_usb_sample(1,1000,5,75,10);
    knuckle_usb_sample(0,0,0,0,20);
    unsigned downs=0,ups=0;uint16_t first_scan=0,last_scan=0;
    while(input_take_report(&r)) {
        if(r.data.ptp.fingers[0].tip_conf_id&2) {if(!downs)first_scan=r.data.ptp.scan_time;++downs;}
        else {++ups;last_scan=r.data.ptp.scan_time;}
        input_report_ack(&r);
    }
    CHECK(downs>=1&&ups==1&&first_scan==(uint16_t)(down*10)&&last_scan==(uint16_t)(now*10));
    CHECK(!knuckle_frame_count&&!knuckle_state.pending&&!reports.recovering);
    return 0;
}

EXPORT int check_usb_clean_start_first_swipe(void)
{
    reset_test();CHECK(!reports.recovering&&!source_wait_up&&!reports.release_mask);
    sample(1,1000,20);sample(1,1100,20);sample(1,1200,20);run_pump();
    CHECK(submitted[2]&&last_id[2]==REPORTID_MOUSE&&last_data[2][1]);
    CHECK(!submitted[1]&&!tx_errors);return 0;
}
EXPORT int check_usb_ptp_first_contact_without_empty_frame(void)
{
    reset_test();feature(3);input_apply_mode_request();
    sample(1,1000,20);run_pump();
    CHECK(submitted[1]==1&&last_id[1]==REPORTID_TOUCHPAD&&!submitted[2]);
    CHECK(mode_writes==1&&!tx_errors);return 0;
}
EXPORT int check_usb_startup_hold_is_fresh_gesture(void)
{
    reset_test();sample(1,1000,150);bus_reset();input_apply_mode_request();
    usb_event(TINYUSB_EVENT_ATTACHED);feature(3);input_apply_mode_request();
    for(unsigned i=0;i<3;++i)sample(1,1100+i*100,150);
    run_pump();CHECK(submitted[1]&&!source_wait_up);
    finish(1,true);
    sample(0,1000,0);sample(1,1000,20);sample(1,1100,20);run_pump();
    CHECK(submitted[1]&&!source_wait_up&&!tx_errors);return 0;
}
EXPORT int check_usb_bus_reset_retires_mouse_and_ptp(void)
{
    for(unsigned mode=0;mode<2;++mode) {
        reset_test();if(mode){feature(3);input_apply_mode_request();}
        CHECK(publish(1));run_pump();unsigned instance=mode?1:2;
        CHECK(usb_busy[instance]&&host_active_mask);
        uint32_t old=usb_epoch;bus_reset();
        CHECK(usb_epoch!=old&&!usb_busy[1]&&!usb_busy[2]&&!host_active_mask&&!reports.release_mask);
        /* A cancelled transfer supplies no completion. A late callback before
         * reconfiguration also cannot acknowledge a current input generation. */
        finish(instance,true);input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
        feature(3);input_apply_mode_request();CHECK(publish(0));run_pump();
        CHECK(usb_busy[1]&&!tx_errors);
    }
    return 0;
}
EXPORT int check_usb_reset_retires_aux_and_config(void)
{
    reset_test();aux_output_once(13,1,input_generation(),now);
    pending=true;completed=false;memset(response,0x5a,sizeof(response));run_pump();
    CHECK(usb_aux_flight&&usb_busy[2]&&busy&&endpoint_busy[0]);
    uint32_t old_epoch=epoch;unsigned old_wakes=wakes;bus_reset();
    CHECK(epoch!=old_epoch&&wakes>old_wakes&&!busy&&!pending&&!completed&&!aux_output_active());
    input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
    feature(3);input_apply_mode_request();CHECK(publish(0));run_pump();CHECK(usb_busy[1]);
    pending=true;run_pump();CHECK(submitted[0]==2&&!tx_errors);return 0;
}
EXPORT int check_usb_config_queued_command_epoch(void)
{
    reset_test();uint8_t packet[64]={0};packet[0]='R';packet[1]='S';packet[2]='T';packet[3]='P';
    usb_config_receive(packet,sizeof(packet));CHECK(((test_queue_t*)commands)->count==1);
    bus_reset();config_budget=1;config_task(NULL);
    CHECK(!((test_queue_t*)commands)->count&&!usb_config_active());return 0;
}
EXPORT int check_usb_v5_flags_readback_and_capabilities(void)
{
    for(unsigned caps=0;caps<4;++caps)for(unsigned flags=0;flags<4;++flags) {
        reset_test();test_capabilities=0xfff|(caps<<12);config.bytes[51]=flags;
        uint8_t packet[64]={0};memcpy(packet,"RSTP",4);
        packet[4]=1;packet[5]=RSTP_INFO;packet[6]=9;
        usb_config_receive(packet,64);config_budget=2;config_task(NULL);
        CHECK(response[8]==12&&!response[10]&&response[6]==9);
        CHECK(rstp_u32(response+12)==test_capabilities&&response[22]==5&&!response[23]);
        packet[5]=RSTP_READ;packet[6]=10;
        usb_config_receive(packet,64);config_budget=2;config_task(NULL);
        unsigned expected=flags;
        if(!(caps&1))expected&=~1U;
        if(!(caps&2))expected|=2;
        CHECK(response[8]==52&&!response[10]&&response[6]==10&&response[63]==expected);
        CHECK(!memcmp(response+12,config.bytes,51)&&config.bytes[51]==flags);
    }
    test_capabilities=0x3fff;return 0;
}

EXPORT int check_usb_aux_stall_does_not_block_ptp(void)
{
    reset_test();feature(3);input_apply_mode_request();
    aux_output_once(13,1,input_generation(),now);run_pump();CHECK(usb_aux_flight);
    CHECK(publish(0));run_pump();CHECK(submitted[1]==1&&endpoint_busy[2]);
    finish(2,true);run_pump();CHECK(last_id[2]==7&&last_data[2][0]==0);
    CHECK(!tx_errors);return 0;
}
EXPORT int check_usb_mode_get_and_pending_idempotence(void)
{
    reset_test();uint8_t value=99;
    CHECK(tud_hid_get_report_cb(1,5,HID_REPORT_TYPE_FEATURE,&value,1)==1&&value==0);
    feature(3);uint32_t serial=request_serial,gen=input_generation();feature(3);
    CHECK(serial==request_serial&&gen==input_generation());input_apply_mode_request();
    CHECK(tud_hid_get_report_cb(1,5,HID_REPORT_TYPE_FEATURE,&value,1)==1&&value==3);
    CHECK(mode_writes==1);feature(0);input_apply_mode_request();CHECK(mode_writes==1);
    feature(3);input_apply_mode_request();usb_event(TINYUSB_EVENT_SUSPENDED);usb_event(TINYUSB_EVENT_RESUMED);
    CHECK(ptp_input_mode==3);bus_reset();CHECK(ptp_input_mode==0);
    feature(3);usb_event(TINYUSB_EVENT_ATTACHED);CHECK(ptp_input_mode==3);
    feature(7);CHECK(ptp_input_mode==0);return 0;
}
EXPORT int check_usb_mode_retry_100ms_and_replacement(void)
{
    reset_test();physical_mode_valid=false;fail_mode=true;feature(3);input_apply_mode_request();
    unsigned writes=mode_writes;CHECK(mode_pending&&mode_retry);
    now+=99;CHECK(!input_apply_mode_request()&&writes==mode_writes);
    ++now;CHECK(input_apply_mode_request()&&mode_pending&&mode_writes==writes+1);
    feature(0);CHECK(!mode_retry);fail_mode=false;input_apply_mode_request();
    CHECK(!mode_pending&&input_mode()==MOUSE_MODE&&mode_writes==writes+2);return 0;
}
static void replace_during_i2c(void) { mode_hook=NULL;feature(0); }
EXPORT int check_usb_mode_i2c_race_keeps_latest_request(void)
{
    reset_test();physical_mode_valid=false;feature(3);mode_hook=replace_during_i2c;
    input_apply_mode_request();CHECK(mode_pending&&requested_mode==MOUSE_MODE);
    input_apply_mode_request();CHECK(!mode_pending&&input_mode()==MOUSE_MODE&&ptp_input_mode==0);return 0;
}
EXPORT int check_usb_lift_before_release_completion(void)
{
    reset_test();feature(3);input_apply_mode_request();sample(1,1000,30);run_pump();finish(1,true);
    CHECK(host_active_mask==(1U<<PTP_MODE));feature(0);input_apply_mode_request();
    run_pump();CHECK(usb_flight[1].release&&usb_busy[1]);
    sample(0,1000,0);sample(1,1000,30);CHECK(reports.recovery_ready&&!source_wait_up);
    finish(1,true);CHECK(!reports.recovering);
    sample(1,1100,30);sample(1,1200,30);run_pump();CHECK(submitted[2]&&!tx_errors);return 0;
}
EXPORT int check_usb_suspend_preserves_accepted_transfers(void)
{
    reset_test();feature(3);input_apply_mode_request();CHECK(publish(1));run_pump();
    aux_output_hold(1,1,input_generation(),now);run_pump();CHECK(usb_busy[1]&&usb_busy[2]);
    uint32_t old=usb_epoch;usb_event(TINYUSB_EVENT_SUSPENDED);run_pump();
    CHECK(usb_epoch==old&&usb_busy[1]&&usb_busy[2]&&ptp_input_mode==3);
    usb_event(TINYUSB_EVENT_RESUMED);finish(1,true);finish(2,true);run_pump();
    CHECK(last_id[2]==7&&!last_data[2][0]&&usb_flight[1].release);
    finish(1,true);finish(2,true);sample(0,1000,0);sample(1,1000,20);run_pump();
    CHECK(usb_busy[1]&&!tx_errors);return 0;
}
EXPORT int check_usb_faults_cannot_be_cleared_by_reenumeration(void)
{
    for(unsigned fault=0;fault<3;++fault) {
        reset_test();feature(3);input_apply_mode_request();
        uint8_t data[64]={0x40};data[4]=1;
        uint32_t old=input_source_generation();
        if(fault==0)input_capture(NULL,false,old,input_generation(),now);
        if(fault==1)for(unsigned i=0;i<17;++i)input_capture(data,true,old,input_generation(),now);
        if(fault==2){input_capture(data,true,old,input_generation(),now);now+=101;parser_budget=1;i2c_queue_task(NULL);}
        CHECK(source_uncertain&&input_source_generation()!=old);
        bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);feature(3);input_apply_mode_request();
        sample(1,1000,20);run_pump();CHECK(!submitted[1]&&source_wait_up);
        sample(0,1000,0);sample(1,1000,20);run_pump();CHECK(submitted[1]&&!tx_errors);
    }
    return 0;
}
EXPORT int check_usb_scheduler_serializes_and_coalesces(void)
{
    reset_test();CHECK(publish(0));usb_budget=3;usbhid_task(NULL);
    CHECK(deferred_count==1&&deferred&&!submitted[2]);
    bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
    in_usb=true;deferred(deferred_arg);in_usb=false;deferred=NULL;
    CHECK(!submitted[2]&&!pump_queued);
    CHECK(publish(0));usb_budget=1;usbhid_task(NULL);
    in_usb=true;deferred(deferred_arg);in_usb=false;deferred=NULL;
    CHECK(submitted[2]==1&&!tx_errors);return 0;
}
EXPORT int check_usb_repeated_reset_and_submit_failure(void)
{
    reset_test();for(unsigned i=0;i<10;++i) {
        bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
        CHECK(publish(0));endpoint_reject[2]=true;run_pump();
        CHECK(!usb_busy[2]&&usb_have_pending);endpoint_reject[2]=false;run_pump();CHECK(usb_busy[2]);
    }
    CHECK(submitted[2]==10&&!tx_errors);return 0;
}

EXPORT int check_usb_contact_before_first_configuration_is_admitted(void)
{
    reset_test();bus_reset();input_apply_mode_request();
    sample(1,1000,150);sample(1,1100,150);usb_event(TINYUSB_EVENT_ATTACHED);
    sample(1,1200,150);sample(1,1300,150);run_pump();CHECK(submitted[2]&&!source_wait_up);
    finish(2,true);
    sample(0,1200,0);sample(1,1000,20);sample(1,1100,20);sample(1,1200,20);run_pump();
    CHECK(submitted[2]&&!tx_errors);return 0;
}
static void capture_lift_during_i2c(void)
{
    mode_hook=NULL;uint8_t data[64]={0x40};
    input_capture(data,true,input_source_generation(),input_generation(),now);
}
EXPORT int check_usb_lift_captured_during_mode_write(void)
{
    reset_test();sample(1,1000,30);feature(3);physical_mode_valid=false;
    mode_hook=capture_lift_during_i2c;input_apply_mode_request();
    CHECK(!source_wait_up&&!reports.recovering);
    sample(1,1000,30);run_pump();CHECK(submitted[1]);return 0;
}
EXPORT int check_usb_fault_lift_queued_before_reconfiguration(void)
{
    reset_test();input_source_recover("read_fail");
    capture_lift_during_i2c();bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
    CHECK(!source_uncertain&&!source_wait_up);feature(3);input_apply_mode_request();
    sample(1,1000,30);run_pump();CHECK(submitted[1]);return 0;
}
EXPORT int check_usb_full_deferred_queue_retries(void)
{
    reset_test();CHECK(publish(0));defer_reject=true;usb_budget=2;usbhid_task(NULL);
    CHECK(!pump_queued&&!deferred&&!submitted[2]);
    defer_reject=false;usb_budget=1;usbhid_task(NULL);CHECK(pump_queued&&deferred);
    in_usb=true;deferred(deferred_arg);in_usb=false;deferred=NULL;
    CHECK(submitted[2]&&!tx_errors);return 0;
}

EXPORT int check_usb_failed_completion_keeps_release_debt(void)
{
    reset_test();feature(3);input_apply_mode_request();sample(1,1000,30);run_pump();
    finish(1,false);CHECK(source_wait_up&&source_uncertain&&reports.release_mask==(1U<<PTP_MODE));
    run_pump();CHECK(usb_busy[1]&&usb_flight[1].release&&!submitted[2]);
    sample(0,1000,0);finish(1,true);sample(1,1000,30);run_pump();
    CHECK(usb_busy[1]&&!usb_flight[1].release&&!tx_errors);return 0;
}
EXPORT int check_usb_old_lift_and_expired_motion(void)
{
    reset_test();uint32_t old_source=input_source_generation(),old_output=input_generation();
    input_source_recover("read_fail");uint8_t lift[64]={0x40};
    input_capture(lift,true,old_source,old_output,now);parser_budget=1;i2c_queue_task(NULL);
    CHECK(source_wait_up&&source_uncertain);
    sample(0,1000,0);CHECK(publish(0));endpoint_reject[2]=true;run_pump();
    CHECK(usb_have_pending);now+=101;endpoint_reject[2]=false;run_pump();
    CHECK(!usb_have_pending&&!submitted[1]&&!submitted[2]&&!reports.release_mask);
    CHECK(source_wait_up&&source_uncertain);return 0;
}

EXPORT int check_usb_runtime_reconnect_still_waits_for_lift(void)
{
    reset_test();sample(1,1000,30);sample(1,1100,30);sample(1,1200,30);
    run_pump();CHECK(usb_busy[2]);finish(2,true);CHECK(!input_starting());
    bus_reset();input_apply_mode_request();usb_event(TINYUSB_EVENT_ATTACHED);
    unsigned before=submitted[2];sample(1,1300,30);run_pump();
    CHECK(source_wait_up&&submitted[2]==before);
    sample(0,1300,0);sample(1,1000,30);sample(1,1100,30);sample(1,1200,30);run_pump();
    CHECK(submitted[2]>before&&!source_wait_up);return 0;
}
