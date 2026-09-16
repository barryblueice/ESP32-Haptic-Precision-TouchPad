/* Protocol table oracle: RSTP 13..47, deliberately independent of queue mapping. */
static const uint8_t function_usages[35] = {
    0xe2,0xcd,0xb6,0xb5,0xb7,0x29,0x28,0x2b,0x2c,0x2a,0x4c,0x49,
    0x4a,0x4d,0x4b,0x4e,0x46,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x06,0x19,0x1b,0x1d,0x1c,0x04
};
EXPORT int check_function_config_and_persistence(void)
{
    capabilities=0xfff;surface_fault=0;
    CHECK(device_config_capabilities()==0xfff);
    CHECK(!(rstp_capabilities_normalize(0xeff)&0x800));
    for(unsigned a=13;a<=47;++a)for(unsigned p=0;p<4;++p) {
        device_config_t c=configured(),out,defaults;device_config_defaults(&defaults);
        c.bytes[33+p*4]=a;c.bytes[6]=0x1f;c.bytes[7]=0xf0;
        CHECK(device_config_valid(&c)&&device_config_supported(&defaults,&c,0xfff));
        CHECK(!device_config_supported(&defaults,&c,0x7ff));
        CHECK(!device_config_supported(&c,&c,0x7ff));
        uint8_t record[60];device_config_store_record(record,&c);
        CHECK(device_config_load_record(&out,record,60)&&!memcmp(&c,&out,52));
        config_test_restart();capabilities=0xfff;
        CHECK(device_config_init()==ESP_OK);
        CHECK(device_config_save(&c)==RSTP_RESTART);
        saved_restart=halted=false;CHECK(device_config_init()==ESP_OK);
        device_config_get(&out);CHECK(!memcmp(&c,&out,52));
        uint8_t packet[64]={0};memcpy(packet,"RSTP",4);packet[4]=1;packet[5]=3;packet[6]=1;packet[8]=52;
        memcpy(packet+12,c.bytes,52);rstp_request_t req;
        CHECK(rstp_decode(packet,64,&req)&&req.status==RSTP_OK);
        rstp_response(packet,&req,RSTP_OK,out.bytes,52);
        CHECK(packet[8]==52&&!memcmp(packet+12,c.bytes,52));
        c.bytes[32+p*4]=0;CHECK(device_config_valid(&c));
        CHECK(!device_config_supported(&defaults,&c,0x7ff));
        for(unsigned bad=48;bad<=255;++bad) {
            c.bytes[33+p*4]=bad;CHECK(!device_config_valid(&c));
        }
    }
    device_config_t c=configured(),base;device_config_defaults(&base);
    c.bytes[33]=42;CHECK(device_config_supported(&base,&c,0x900));
    CHECK(!device_config_supported(&base,&c,0x800));
    capabilities=0xfff;return 0;
}
EXPORT int check_function_v2_migration(void)
{
    for(unsigned a=13;a<=47;++a)for(unsigned p=0;p<4;++p) {
        uint8_t record[60];legacy_v2_record(record);
        unsigned offset=8+32+p*5;record[offset+1]=a;record[offset+2]=0;
        device_config_t out;CHECK(device_config_load_record(&out,record,60));
        CHECK(out.bytes[33+p*4]==a);
        record[offset+2]=1;CHECK(!device_config_load_record(&out,record,60));
        record[offset+2]=2;CHECK(!device_config_load_record(&out,record,60));
    }
    return 0;
}
EXPORT int check_function_reports_and_repeats(void)
{
    for(unsigned a=13;a<=47;++a)for(unsigned repeat=0;repeat<2;++repeat) {
        aux_output_reset(false);
        device_config_t c=configured();c.bytes[33]=a;c.bytes[7]=repeat?0x10:0;
        point_gesture_t s={0};tp_multi_msg_t m=contact(0,0);
        point_result_t p=point_gesture_update(&s,&c,&m,2302,1532,1149,766,0);
        CHECK(p.initial&&!p.hold&&p.action==a&&p.steps==1);
        CHECK(point_gesture_repeating(&s)==(bool)repeat);
        CHECK(aux_output_once(p.action,p.steps,test_generation,0));
        for(unsigned cycle=0;cycle<(repeat?3:1);++cycle) {
            aux_output_report_t r;
            /* Backpressure may last longer than the legacy movement expiry. */
            CHECK(aux_output_take(&r,test_generation,1000+cycle*200)&&!r.release);
            CHECK(r.id==(a<=17?7:8)&&r.length==(a<=17?2:8));
            CHECK(r.data[a<=17?0:2]==function_usages[a-13]);
            if(a>=18)CHECK(r.data[0]==(a>=42?1:0)&&!r.data[1]);
            CHECK(!aux_output_take(&r,test_generation,1000));
            aux_output_complete(true);
            CHECK(aux_output_repeat(a,1,test_generation,1000)); /* Busy: skip. */
            CHECK(aux_output_take(&r,test_generation,1000)&&r.release&&r.id==(a<=17?7:8));
            for(unsigned i=0;i<8;++i)CHECK(!r.data[i]);
            aux_output_complete(true);CHECK(!aux_output_active());
            unsigned due=600+cycle*200;
            CHECK(!point_gesture_tick(&s,due-1).steps);
            p=point_gesture_tick(&s,due);CHECK(p.steps==(repeat?1:0));
            if(repeat&&cycle<2)CHECK(aux_output_repeat(p.action,p.steps,test_generation,due));
        }
        m=(tp_multi_msg_t){0};CHECK(point_gesture_update(&s,&c,&m,2302,1532,1149,766,2000).cancel);
        aux_output_cancel_gesture();CHECK(!point_gesture_tick(&s,3000).steps&&!aux_output_active());
    }
    return 0;
}
EXPORT int check_function_cancellation_and_retry(void)
{
    for(unsigned a=13;a<=47;++a) {
        aux_output_report_t r;aux_output_reset(false);
        CHECK(aux_output_once(a,1,test_generation,0));aux_output_cancel_gesture();
        CHECK(aux_output_take(&r,test_generation,500)&&!r.release);
        aux_output_unsubmitted();CHECK(aux_output_take(&r,test_generation,1000)&&!r.release);
        CHECK(aux_output_repeat(a,1,test_generation,1000));
        aux_output_cancel_gesture();aux_output_complete(true);
        CHECK(aux_output_take(&r,test_generation,1000)&&r.release);
        aux_output_complete(false);CHECK(aux_output_take(&r,test_generation,2000)&&r.release);
        aux_output_complete(true);CHECK(!aux_output_active());
        device_config_t c=configured();c.bytes[33]=a;c.bytes[6]|=2;c.bytes[7]=0x10;
        point_gesture_t s={0};tp_multi_msg_t m=contact(0,0);
        point_gesture_update(&s,&c,&m,2000,1000,200,100,0);
        CHECK(aux_output_once(a,1,test_generation,0));CHECK(aux_output_take(&r,test_generation,0));
        m=contact(40,0);point_result_t p=point_gesture_update(&s,&c,&m,2000,1000,200,100,1);
        CHECK(p.handoff&&p.cancel&&!point_gesture_tick(&s,1000).steps);
        aux_output_cancel_gesture();aux_output_complete(true);
        CHECK(aux_output_take(&r,test_generation,1)&&r.release);aux_output_complete(true);
        CHECK(aux_output_once(a,1,test_generation,0));CHECK(aux_output_take(&r,test_generation,0));
        aux_output_reset(false);aux_output_complete(true);CHECK(!aux_output_active());
        aux_output_reset(true);
        for(unsigned i=0;i<2;++i){CHECK(aux_output_take(&r,test_generation,0)&&r.release);aux_output_complete(true);}
        CHECK(!aux_output_active());CHECK(!aux_output_hold(a,1,test_generation,0));
    }
    return 0;
}
EXPORT int check_function_radio_roundtrip(void)
{
    for(unsigned a=13;a<=47;++a) {
        aux_output_reset(false);aux_output_event_t e;
        CHECK(aux_output_take_event(&e,test_generation,0)&&!e.action);aux_output_event_complete(true);
        CHECK(aux_output_once(a,1,test_generation,0));
        CHECK(aux_output_take_event(&e,test_generation,1000)&&e.action==a&&!e.hold);
        wire_action_t w={123,1,e.action,e.steps,e.hold},out;uint8_t packet[38];
        wire_action_encode(packet,&w);CHECK(wire_action_decode(packet,38,&out));
        CHECK(out.action==a&&out.steps==1&&!out.hold);aux_output_event_complete(true);
        CHECK(!aux_output_take_event(&e,test_generation,1000));
        packet[15]=1;CHECK(!wire_action_decode(packet,38,&out));packet[15]=0;
        packet[13]=packet[14]=0xff;CHECK(!wire_action_decode(packet,38,&out));
        packet[13]=1;packet[14]=0;
        for(unsigned bad=48;bad<=255;++bad){packet[12]=bad;CHECK(!wire_action_decode(packet,38,&out));}
    }
    return 0;
}
