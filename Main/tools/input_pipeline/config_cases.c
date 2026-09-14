static void config_reset(bool erase)
{
    initialized=pending_apply=pending_restart=halted=saved_restart=false;
    capabilities=0x3f; semaphore_ready=false;
    pending=busy=completed=false; epoch=0;
    queue_count=0; notify_hook=NULL; submit_hook=NULL;
    stores=commits=recoveries=restarts=disconnects=send_attempts=completions=0;
    storage_failure=haptic_state=0; current_mode=WIRED_MODE; usb_accept=usb_ready=true;
    test_steps=0; parser_halted=false;
    if(erase) { stored=false; for(unsigned i=0;i<6;++i) legacy_value[i]=-1; }
}
EXPORT int test_reference_protocol_vectors(void)
{
    rstp_request_t r; uint8_t out[64];
    CHECK(rstp_decode(vector_info_request,64,&r) && !r.status);
    const uint8_t info[12]={0x3f,0,0,0,1,0,0,0,0,0,1,0};
    rstp_response(out,&r,RSTP_OK,info,12); CHECK(!memcmp(out,vector_info_response,64));
    CHECK(rstp_decode(vector_read_request,64,&r) && !r.status);
    device_config_t c; device_config_defaults(&c);
    rstp_response(out,&r,RSTP_OK,c.bytes,32); CHECK(!memcmp(out,vector_read_response,64));
    CHECK(rstp_decode(vector_write_request,64,&r) && !r.status && r.config.bytes[0]==75);
    rstp_response(out,&r,RSTP_RESTART,NULL,0); CHECK(!memcmp(out,vector_reconnect_response,64));
    rstp_response(out,&r,RSTP_INVALID,NULL,0); CHECK(!memcmp(out,vector_invalid_response,64));
    rstp_response(out,&r,RSTP_OK,NULL,0); CHECK(!memcmp(out,vector_write_response,64));
    return 0;
}
EXPORT int test_configured_sleep_enable_timeout_wake_and_failure(void)
{
    config_reset(true); CHECK(device_config_init()==ESP_OK);
    timer=NULL; timer_failed=false; sleep_requested=false; config_clock_us=0; timer_period_us=0;
    sleep_active=wake_pending=false; active.bytes[6]=0;
    tp_modern_sleep_init(); CHECK(timer && !timer_period_us);
    config_clock_us=3600000000LL; sleep_cb(NULL); CHECK(!sleep_requested && !tp_modern_sleep_is_active());
    timer=NULL; active.bytes[6]=1; rstp_put32(active.bytes+8,1000); config_clock_us=0;
    tp_modern_sleep_init(); CHECK(timer_period_us==1000000);
    config_clock_us=999999; sleep_cb(NULL); CHECK(!sleep_requested);
    config_clock_us=1000000; sleep_cb(NULL); CHECK(sleep_requested && tp_modern_sleep_is_active());
    tp_modern_sleep_signal_activity_from_isr(); CHECK(!tp_modern_sleep_is_active());
    tp_modern_sleep_record_activity(); CHECK(!sleep_requested && !wake_pending && timer_period_us==1000000);
    config_clock_us=1999999; sleep_cb(NULL); CHECK(!sleep_requested);
    timer=NULL; timer_failed=true; tp_modern_sleep_init(); CHECK(!(device_config_capabilities()&16));
    timer_failed=false; return 0;
}
EXPORT int test_descriptors_all_boot_orientations(void)
{
    config_reset(true); CHECK(device_config_init()==ESP_OK);
    uint8_t original[sizeof(ptp_hid_report_descriptor)]; memcpy(original,ptp_hid_report_descriptor,sizeof(original));
    for(unsigned rotation=0;rotation<4;++rotation) {
        memcpy(ptp_hid_report_descriptor,original,sizeof(original)); active.bytes[5]=rotation;
        usb_descriptor_init(); unsigned usage=0, page=0, logical_count=0, physical_count=0;
        for(unsigned i=0;i<sizeof(original);) {
            uint8_t tag=ptp_hid_report_descriptor[i]; unsigned size=tag&3; if(size==3) size=4;
            if(tag==0x09) usage=ptp_hid_report_descriptor[i+1];
            if(tag==0x05) page=ptp_hid_report_descriptor[i+1];
            if(page==1 && (tag==0x26 || tag==0x46) && (usage==0x30 || usage==0x31)) {
                uint16_t value=ptp_hid_report_descriptor[i+1] | ((uint16_t)ptp_hid_report_descriptor[i+2]<<8);
                bool long_axis=(usage==0x30) != ((rotation&1)!=0);
                if(tag==0x26) { CHECK(value==(long_axis?2302:1532)); ++logical_count; }
                else { CHECK(value==(long_axis?1149:766)); ++physical_count; }
            }
            i+=size+1;
        }
        CHECK(logical_count==10 && physical_count==10);
    }
    memcpy(ptp_hid_report_descriptor,original,sizeof(original));
    const uint8_t consumer[]={0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x07,0x15,0,0x26,0xea,0,0x19,0,0x2a,0xea,0,0x75,0x10,0x95,1,0x81,0,0xc0};
    CHECK(!memcmp(mouse_hid_report_descriptor+sizeof(mouse_hid_report_descriptor)-sizeof(consumer),consumer,sizeof(consumer)));
    return 0;
}
EXPORT int test_configuration_migration_and_boot(void)
{
    config_reset(true); legacy_value[0]=101; legacy_value[1]=4; legacy_value[2]=255;
    CHECK(device_config_init()==ESP_OK && device_config_value(0)==63 && device_config_value(1)==3);
    config_reset(true); legacy_value[1]=3; legacy_value[2]=1;
    legacy_value[3]=150; legacy_value[4]=100; legacy_value[5]=130;
    CHECK(device_config_init()==ESP_OK); device_config_t c; device_config_get(&c);
    CHECK(c.bytes[0]==75 && c.bytes[1]==1 && c.bytes[2]==150 && c.bytes[3]==150 && c.bytes[4]==150);
    CHECK(c.bytes[5]==2 && c.bytes[6]==1 && stores==1 && commits==1);
    CHECK(device_config_rotation()==2 && device_config_x_max()==2302);
    config_reset(false); legacy_value[1]=4; CHECK(device_config_init()==ESP_OK);
    device_config_get(&c); CHECK(c.bytes[0]==75 && stores==0);
    disk[4]=99; config_reset(false); CHECK(device_config_init()==ESP_OK);
    device_config_get(&c); CHECK(c.bytes[0]==63 && stores==0 && disk[4]==99);
    return 0;
}
EXPORT int test_legacy_controls_are_vendor_features(void)
{
    /* Windows selects its optional haptic/force controls by usage, whereas
     * the legacy configurator discovers these fields by Report ID in the
     * Touch Pad application collection. Keep that ABI without OS ownership. */
    unsigned page=0, usage=0, id=0, bits=0, count=0, minimum=0, maximum=0;
    unsigned depth=0, app_page=0, app_usage=0, found=0;
    for(unsigned i=0;i<sizeof(ptp_hid_report_descriptor);) {
        uint8_t tag=ptp_hid_report_descriptor[i];
        unsigned size=tag&3; if(size==3) size=4;
        CHECK(i+size<sizeof(ptp_hid_report_descriptor));
        uint32_t value=0;
        for(unsigned j=0;j<size;++j) value|=(uint32_t)ptp_hid_report_descriptor[i+1+j]<<(8*j);
        switch(tag&0xfc) {
        case 0x04: page=value; break;
        case 0x08: usage=value; break;
        case 0x84: id=value; break;
        case 0x74: bits=value; break;
        case 0x94: count=value; break;
        case 0x14: minimum=value; break;
        case 0x24: maximum=value; break;
        case 0xa0:
            if(!depth) { app_page=page; app_usage=usage; }
            ++depth; break;
        case 0xc0: CHECK(depth); --depth; break;
        case 0xb0:
            CHECK(!(page==0x0d && usage==0xb0));
            CHECK(!(page==0x0e && usage==0x23));
            if(id==0x40 || id==0x41) {
                CHECK(app_page==0x0d && app_usage==5);
                CHECK(page==0xff00 && usage==id && bits==8 && count==1 && value==2);
                CHECK(minimum==(id==0x40 ? 1:0) && maximum==(id==0x40 ? 3:100));
                unsigned mask=id==0x40 ? 1:2; CHECK(!(found&mask)); found|=mask;
            }
            break;
        }
        if((tag&0x0c)==0) usage=0; /* Local items expire at every Main item. */
        i+=size+1;
    }
    CHECK(!depth && found==3);
    return 0;
}
EXPORT int test_configuration_atomic_storage_failures(void)
{
    for(int fault=1;fault<=3;++fault) {
        config_reset(true); CHECK(device_config_init()==ESP_OK);
        device_config_t before,target,after; device_config_get(&before); target=before;
        target.bytes[0]=75; target.bytes[5]=1; target.bytes[12]=1; target.bytes[13]=2;
        uint8_t old_disk[40]; memcpy(old_disk,disk,40); storage_failure=fault;
        CHECK(device_config_save(&target)==RSTP_STORAGE);
        device_config_get(&after); CHECK(!memcmp(&before,&after,32) && !memcmp(old_disk,disk,40));
        CHECK(!halted && !saved_restart && !restarts && recoveries==0);
        CHECK(device_config_set_legacy(0,25,true)!=ESP_OK); device_config_get(&after);
        CHECK(!memcmp(&before,&after,32));
    }
    return 0;
}
EXPORT int test_configuration_full_save_and_legacy_consistency(void)
{
    config_reset(true); CHECK(device_config_init()==ESP_OK);
    CHECK(device_config_set_legacy(0,25,true)==ESP_OK);
    CHECK(device_config_set_legacy(1,3,true)==ESP_OK);
    device_config_t c; device_config_get(&c); CHECK(c.bytes[0]==25 && c.bytes[1]==3 && ptp_button_press_threshold==3);
    c.bytes[5]=1; c.bytes[0]=75;
    CHECK(device_config_save(&c)==RSTP_RESTART && halted && saved_restart && parser_halted);
    CHECK(device_config_value(0)==25 && device_config_rotation()==2);
    CHECK(device_config_set_legacy(0,50,true)!=ESP_OK);
    CHECK(device_config_save(&c)==RSTP_BUSY);
    config_reset(false); CHECK(device_config_init()==ESP_OK);
    CHECK(device_config_value(0)==75 && device_config_rotation()==1 && device_config_x_max()==1532 && device_config_y_max()==2302);
    return 0;
}
EXPORT int test_configuration_rejections_without_writes(void)
{
    config_reset(true); CHECK(device_config_init()==ESP_OK); stores=0;
    device_config_t c; device_config_get(&c); c.bytes[2]=101;
    CHECK(device_config_save(&c)==RSTP_INVALID && !stores);
    device_config_get(&c); c.bytes[12]=1; CHECK(device_config_save(&c)==RSTP_INVALID && !stores);
    device_config_get(&c); device_config_disable(1); c.bytes[0]=75;
    CHECK(device_config_save(&c)==RSTP_UNSUPPORTED && !stores);
    haptic_state=SURFACE_FAULT; CHECK(!(device_config_capabilities() & 0x11));
    return 0;
}
static void queue_write(void)
{
    uint8_t frame[64]={'R','S','T','P',1,3,17,0,32,0}; device_config_t c; device_config_get(&c);
    c.bytes[0]=75; memcpy(frame+12,c.bytes,32); usb_config_receive(frame,64);
}
static void completion_hook(void)
{
    ++completions;
    if (config_test_mode==1 && completions==1) { usb_ready=false; usb_config_send(); return; }
    usb_ready=true; usb_config_send();
    if (config_test_mode==2 && completions==1) { usb_config_complete(false); return; }
    if (config_test_mode==3) { usb_config_detach(); return; }
    if (config_test_mode==4) return;
    usb_config_complete(true);
}
EXPORT int test_usb_config_completion_controls_restart(void)
{
    for(int mode=0;mode<=4;++mode) {
        config_reset(true); CHECK(device_config_init()==ESP_OK); CHECK(usb_config_init()==ESP_OK);
        stores=0; config_test_mode=mode; notify_hook=completion_hook; queue_write(); test_steps=8;
        config_task(NULL);
        CHECK(stores==1 && halted && transmitted[10]==7 && transmitted[6]==17 && transmitted[8]==0);
        CHECK(restarts==(mode<3 ? 1:0) && disconnects==restarts);
        if(mode==1) CHECK(completions>=2);
        if(mode==2) CHECK(send_attempts==2);
        if(mode==3) CHECK(!pending && !busy);
        if(mode==4) CHECK(busy && !completed);
    }
    return 0;
}
EXPORT int test_usb_config_storage_failure_and_stale_session(void)
{
    config_reset(true); CHECK(device_config_init()==ESP_OK); CHECK(usb_config_init()==ESP_OK);
    storage_failure=3; config_test_mode=0; notify_hook=completion_hook;
    queue_write(); test_steps=8; config_task(NULL);
    CHECK(transmitted[10]==RSTP_STORAGE && !restarts && !halted && device_config_value(0)==63);
    storage_failure=0; stores=0; queue_write(); usb_config_detach(); test_steps=4; config_task(NULL);
    CHECK(!stores && !restarts);
    return 0;
}
