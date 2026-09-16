static void config_test_restart(void)
{
    initialized = pending_apply = pending_restart = halted = saved_restart = writer_busy = false;
    capabilities = 0x7ff; surface_fault = 0;
}
static void legacy_v2_record(uint8_t record[60])
{
    device_config_t c; device_config_defaults(&c);
    c.bytes[0]=72; c.bytes[1]=3; c.bytes[2]=81; c.bytes[3]=111; c.bytes[4]=201;
    c.bytes[6]=0x0b; c.bytes[7]=0xa5;
    device_config_store_record(record,&c); record[4]=2;
    for(unsigned p=0;p<4;++p) {
        uint8_t *r=record+40+p*5;
        r[0]=1; r[1]=3+p*2; r[2]=p&1; r[3]=5+p; r[4]=2+p;
    }
}
EXPORT int check_v3_threshold_validation(void)
{
    device_config_t c;device_config_defaults(&c);
    CHECK(c.bytes[48]==20&&c.bytes[49]==35&&c.bytes[50]==70&&!c.bytes[51]);
    CHECK(c.bytes[2]==80&&c.bytes[3]==100&&c.bytes[4]==130);
    for(unsigned value=1;value<=100;++value) {
        c.bytes[48]=c.bytes[49]=c.bytes[50]=value;CHECK(device_config_valid(&c));
    }
    for(unsigned field=48;field<=50;++field)for(unsigned value=101;value<=255;++value) {
        device_config_defaults(&c);c.bytes[field]=value;CHECK(!device_config_valid(&c));
    }
    device_config_defaults(&c);c.bytes[48]=0;CHECK(!device_config_valid(&c));
    c.bytes[48]=36;CHECK(!device_config_valid(&c));c.bytes[48]=20;
    c.bytes[49]=71;CHECK(!device_config_valid(&c));c.bytes[49]=35;
    for(unsigned value=1;value<=255;++value){c.bytes[51]=value;CHECK(!device_config_valid(&c));}
    c.bytes[51]=0;c.bytes[2]=c.bytes[3]=c.bytes[4]=255;CHECK(device_config_valid(&c));
    return 0;
}
EXPORT int check_v3_wire_roundtrip_and_capability(void)
{
    device_config_t a,b;device_config_defaults(&a);b=a;
    b.bytes[48]=1;b.bytes[49]=50;b.bytes[50]=100;
    CHECK(device_config_supported(&a,&b,0x7ff));CHECK(!device_config_supported(&a,&b,0x3ff));
    CHECK(device_config_supported(&a,&b,0x400));
    b.bytes[2]++;CHECK(!device_config_supported(&a,&b,0x400));b.bytes[2]--;
    uint8_t wire[64]={0};memcpy(wire,"RSTP",4);wire[4]=1;wire[5]=3;wire[6]=9;wire[8]=52;
    memcpy(wire+12,b.bytes,52);rstp_request_t request;
    CHECK(rstp_decode(wire,64,&request)&&!request.status&&!memcmp(request.config.bytes,b.bytes,52));
    rstp_response(wire,&request,RSTP_OK,b.bytes,52);
    CHECK(wire[4]==1&&wire[8]==52&&wire[60]==1&&wire[61]==50&&wire[62]==100&&!wire[63]);
    wire[63]=1;CHECK(rstp_decode(wire,64,&request)&&request.status==RSTP_INVALID);
    return 0;
}
EXPORT int check_v2_conversion_and_invalid_records(void)
{
    uint8_t old[60];legacy_v2_record(old);device_config_t c;
    CHECK(device_config_load_record(&c,old,60));CHECK(!memcmp(c.bytes,old+8,32));
    CHECK(c.bytes[48]==20&&c.bytes[49]==35&&c.bytes[50]==70);
    for(unsigned p=0;p<4;++p) {
        CHECK(c.bytes[32+p*4]==1&&c.bytes[33+p*4]==3+p*2+(p&1));
        CHECK(c.bytes[34+p*4]==5+p&&c.bytes[35+p*4]==2+p);
    }
    for(unsigned action=0;action<=12;++action) {
        legacy_v2_record(old);old[40]=action!=0;old[41]=action;old[42]=1;
        CHECK(device_config_load_record(&c,old,60));
        CHECK(c.bytes[33]==(action?((action&1)?action+1:action-1):0));
    }
    old[42]=2;CHECK(!device_config_load_record(&c,old,60));
    legacy_v2_record(old);old[41]=48;CHECK(!device_config_load_record(&c,old,60));
    legacy_v2_record(old);old[4]=4;CHECK(!device_config_load_record(&c,old,60));
    old[4]=2;CHECK(!device_config_load_record(&c,old,59));
    return 0;
}
EXPORT int check_v2_migration_persistence_failure(void)
{
    uint8_t backup[60];legacy_v2_record(nvs_blob);memcpy(backup,nvs_blob,60);nvs_size=60;
    config_test_restart();fail_commit=true;CHECK(device_config_init()==ESP_OK);
    CHECK(!memcmp(backup,nvs_blob,60));device_config_t c;device_config_get(&c);
    CHECK(c.bytes[2]==81&&c.bytes[4]==201&&c.bytes[48]==20&&c.bytes[50]==70);
    config_test_restart();fail_commit=false;CHECK(device_config_init()==ESP_OK);
    CHECK(nvs_blob[4]==3&&nvs_size==60);device_config_t reloaded;
    CHECK(device_config_load_record(&reloaded,nvs_blob,nvs_size)&&!memcmp(&c,&reloaded,52));
    return 0;
}
EXPORT int check_v3_save_readback_and_controls_preserve_groups(void)
{
    config_test_restart();nvs_size=0;fail_commit=false;CHECK(device_config_init()==ESP_OK);
    device_config_t c;device_config_get(&c);c.bytes[48]=21;c.bytes[49]=36;c.bytes[50]=71;
    CHECK(device_config_save(&c)==RSTP_RESTART);
    config_test_restart();CHECK(device_config_init()==ESP_OK);device_config_t out;device_config_get(&out);
    CHECK(!memcmp(&c,&out,52));
    CHECK(device_config_set_controls(3,100,3,true)==ESP_OK);device_config_get(&out);
    CHECK(out.bytes[0]==100&&out.bytes[1]==3);
    CHECK(!memcmp(c.bytes+2,out.bytes+2,3)&&!memcmp(c.bytes+48,out.bytes+48,3));
    fail_commit=true;c=out;c.bytes[49]=40;CHECK(device_config_save(&c)==RSTP_STORAGE);
    device_config_get(&c);CHECK(!memcmp(&c,&out,52));fail_commit=false;
    config_test_restart();CHECK(device_config_init()==ESP_OK);device_config_get(&c);CHECK(!memcmp(&c,&out,52));
    return 0;
}
