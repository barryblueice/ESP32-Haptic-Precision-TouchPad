EXPORT int check_v5_document_default_vector(void)
{
    const uint8_t expected[52] = {
        0x3f,2,0x50,0x64,0x82,0,1,0,0x20,0xbf,2,0,
        0,0,0,5,1, 0,0,0,5,1, 0,0,0,5,1, 0,0,0,5,1,
        0,0,5,1, 0,0,5,1, 0,0,5,1, 0,0,5,1, 60,80,100,3
    };
    device_config_t c;device_config_defaults(&c);
    CHECK(DEVICE_CONFIG_VERSION==5&&!memcmp(c.bytes,expected,52));
    uint8_t wire[64];rstp_request_t req={.command=RSTP_WRITE,.sequence=65535};
    rstp_response(wire,&req,RSTP_OK,expected,52);
    CHECK(rstp_decode(wire,64,&req)&&req.status==RSTP_OK);
    CHECK(!device_config_supported(&c,&c,0xfff));
    CHECK(device_config_supported(&c,&c,0x1fff));
    c.bytes[51]=2;CHECK(device_config_supported(&c,&c,0xfff));
    for(unsigned flags=0;flags<4;++flags) {
        c.bytes[51]=flags;CHECK(device_config_valid(&c));
        CHECK(device_config_supported(&c,&c,0x3fff));
        CHECK(device_config_supported(&c,&c,0x1fff)==((flags&2)!=0));
        CHECK(device_config_supported(&c,&c,0x2fff)==((flags&1)==0));
    }
    c.bytes[51]=4;CHECK(!device_config_valid(&c));
    return 0;
}
EXPORT int check_v3_migration_preserves_values(void)
{
    device_config_t old,out;device_config_defaults(&old);
    old.bytes[48]=20;old.bytes[49]=35;old.bytes[50]=70;old.bytes[51]=0;
    old.bytes[16]=2;old.bytes[35]=2;old.bytes[0]=72;
    uint8_t record[60];device_config_store_record(record,&old);record[4]=3;
    CHECK(device_config_load_record(&out,record,60));
    CHECK(!memcmp(old.bytes,out.bytes,51)&&out.bytes[51]==3);
    record[59]=1;CHECK(!device_config_load_record(&out,record,60));
    record[4]=4;record[59]=0;CHECK(device_config_load_record(&out,record,60)&&out.bytes[51]==2);
    record[59]=1;CHECK(device_config_load_record(&out,record,60)&&out.bytes[51]==3);
    for(unsigned flags=2;flags<256;++flags){record[59]=flags;CHECK(!device_config_load_record(&out,record,60));}
    record[4]=5;
    for(unsigned flags=0;flags<4;++flags){record[59]=flags;CHECK(device_config_load_record(&out,record,60)&&out.bytes[51]==flags);}
    record[4]=6;CHECK(!device_config_load_record(&out,record,60));
    return 0;
}
EXPORT int check_switch_live_save_restart_and_failure(void)
{
    config_test_restart();capabilities=0x3fff;nvs_size=0;fail_commit=false;
    CHECK(device_config_init()==ESP_OK);
    device_config_t c,out;device_config_get(&c);c.bytes[51]=0;
    uint32_t generation=test_generation;unsigned notices=auto_config_notifications;
    CHECK(device_config_save(&c)==RSTP_OK&&!halted&&!saved_restart);
    device_config_get(&out);CHECK(!memcmp(&c,&out,52)&&!nvs_blob[59]&&nvs_blob[4]==5);
    CHECK(test_generation==generation&&auto_config_notifications==notices+1);
    CHECK(device_config_save(&c)==RSTP_OK&&auto_config_notifications==notices+1);
    config_test_restart();capabilities=0x3fff;CHECK(device_config_init()==ESP_OK);device_config_get(&out);
    CHECK(!out.bytes[51]);
    c.bytes[51]=1;fail_commit=true;
    CHECK(device_config_save(&c)==RSTP_STORAGE);device_config_get(&out);
    CHECK(!out.bytes[51]&&!nvs_blob[59]);
    fail_commit=false;CHECK(device_config_save(&c)==RSTP_OK);
    c.bytes[5]=1;CHECK(device_config_save(&c)==RSTP_RESTART&&halted);
    device_config_get(&out);CHECK(!memcmp(&c,&out,52));
    return 0;
}

EXPORT int check_haptic_switch_independent_save_and_migration(void)
{
    config_test_restart();capabilities=0x3fff;nvs_size=0;fail_commit=false;
    CHECK(device_config_init()==ESP_OK);
    device_config_t c,out;device_config_get(&c);c.bytes[51]=1;
    uint32_t generation=test_generation;unsigned notices=auto_config_notifications;
    CHECK(device_config_save(&c)==RSTP_OK&&!halted&&!saved_restart);
    device_config_get(&out);CHECK(!memcmp(&c,&out,52)&&nvs_blob[59]==1);
    CHECK(test_generation==generation&&auto_config_notifications==notices);
    config_test_restart();capabilities=0x3fff;CHECK(device_config_init()==ESP_OK);
    device_config_get(&out);CHECK(out.bytes[51]==1&&out.bytes[0]==63&&out.bytes[1]==2);
    c.bytes[51]=3;fail_commit=true;CHECK(device_config_save(&c)==RSTP_STORAGE);
    device_config_get(&out);CHECK(out.bytes[51]==1&&nvs_blob[59]==1);
    fail_commit=false;
    for(unsigned old_switch=0;old_switch<2;++old_switch) {
        nvs_blob[4]=4;nvs_blob[59]=old_switch;
        config_test_restart();CHECK(device_config_init()==ESP_OK);device_config_get(&out);
        CHECK(out.bytes[51]==(old_switch|2)&&nvs_blob[4]==5&&nvs_blob[59]==(old_switch|2));
    }
    return 0;
}
