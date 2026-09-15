static const uint8_t test_peer[6]={1,2,3,4,5,6};
static void settings_reset(void)
{
    now=worker_notifications=0; nvs_size=nvs_pending_size=commits=0;
    fail_commit=writer_busy=false;surface_fault=0;capabilities=0x3ff;
    initialized=pending_apply=pending_restart=halted=saved_restart=false;
    device_config_init();commits=0;
    peer_valid=request_pending=completed_valid=reply_pending=reply_attempted=false;
    newest=completed_request=completed_reply=reply=(wire_settings_t){0};
    reply_serial=reply_flight=reply_at=peer_seen=0;
    wireless_settings_init(777);wireless_settings_peer(test_peer);
}
static wire_settings_t settings_request(uint32_t sequence,uint8_t mask,uint8_t strength,uint8_t level)
{
    wire_settings_t s={777,123,sequence,mask,strength,level,0};uint8_t p[38];
    wire_settings_encode(p,8,&s);wireless_settings_receive(test_peer,p,38);return s;
}
static bool settings_reply(wire_settings_t *result)
{
    uint8_t p[38],mac[6];now+=20;
    return wireless_settings_reply(p,mac) && !memcmp(mac,test_peer,6) && wire_settings_decode(p,38,9,result);
}
EXPORT int check_settings_worker_defers_and_persists(void)
{
    settings_reset();wire_settings_t s;
    settings_request(1,0,0,0);CHECK(request_pending && !commits && !reply_pending);
    wireless_settings_process();CHECK(settings_reply(&s)&&s.intensity==63&&s.level==2&&!s.status);
    wireless_settings_reply_complete(true);
    settings_request(2,3,100,3);CHECK(device_config_value(0)==63&&!commits);
    wireless_settings_process();CHECK(commits==1 && device_config_value(0)==100 && device_config_value(1)==3);
    CHECK(settings_reply(&s)&&s.mask==3&&s.intensity==100&&s.level==3&&!s.status);
    device_config_t saved;CHECK(device_config_load_record(&saved,nvs_blob,nvs_size));
    CHECK(saved.bytes[0]==100&&saved.bytes[1]==3);return 0;
}
EXPORT int check_settings_duplicate_and_unchanged_do_not_write(void)
{
    settings_reset();settings_request(1,0,0,0);wireless_settings_process();
    settings_request(2,1,0,0);wireless_settings_process();CHECK(commits==1);
    settings_request(2,1,0,0);wireless_settings_process();CHECK(commits==1);
    settings_request(3,1,0,0);wireless_settings_process();CHECK(commits==1);
    settings_request(4,2,0,1);wireless_settings_process();CHECK(commits==2);
    CHECK(device_config_value(0)==0&&device_config_value(1)==1);return 0;
}
EXPORT int check_settings_busy_storage_and_fault(void)
{
    settings_reset();wire_settings_t s;
    settings_request(1,0,0,0);wireless_settings_process();
    writer_busy=true;settings_request(2,3,90,3);wireless_settings_process();
    CHECK(settings_reply(&s)&&s.status==WIRE_SETTINGS_BUSY&&s.intensity==63&&s.level==2&&!commits);
    writer_busy=false;fail_commit=true;settings_request(2,3,90,3);wireless_settings_process();
    CHECK(settings_reply(&s)&&s.status==WIRE_SETTINGS_STORAGE&&s.intensity==63&&s.level==2&&commits==1);
    fail_commit=false;settings_request(2,3,90,3);wireless_settings_process();CHECK(commits==1);
    settings_request(3,3,90,3);wireless_settings_process();CHECK(commits==2);
    surface_fault=1;settings_request(4,1,50,0);wireless_settings_process();
    CHECK(settings_reply(&s)&&s.status==WIRE_SETTINGS_UNSUPPORTED&&s.intensity==90&&commits==2);return 0;
}
EXPORT int check_settings_session_peer_order_and_expiry(void)
{
    settings_reset();uint8_t p[38],mac[6]={9};wire_settings_t s={777,123,1,1,50,0,0};
    wire_settings_encode(p,8,&s);wireless_settings_receive(test_peer,p,38);CHECK(!request_pending);
    s.mask=s.intensity=0;wire_settings_encode(p,8,&s);
    wireless_settings_receive(mac,p,38);CHECK(!request_pending);
    s.session++;wire_settings_encode(p,8,&s);wireless_settings_receive(test_peer,p,38);CHECK(!request_pending);
    settings_request(1,0,0,0);wireless_settings_process();
    settings_request(3,1,50,0);wireless_settings_process();CHECK(commits==1);
    settings_request(2,1,75,0);CHECK(!request_pending);
    settings_request(3,1,75,0);CHECK(!request_pending);
    now=2500;settings_request(4,1,75,0);CHECK(!request_pending);
    wireless_settings_peer(test_peer);settings_request(4,1,75,0);CHECK(request_pending);
    now+=2500;wireless_settings_process();CHECK(commits==1);
    return 0;
}
EXPORT int check_settings_reply_completion_cannot_clear_new_reply(void)
{
    settings_reset();wire_settings_t s;
    settings_request(1,0,0,0);wireless_settings_process();CHECK(settings_reply(&s));
    settings_request(2,1,25,0);wireless_settings_process();
    wireless_settings_reply_complete(true);CHECK(reply_pending);
    CHECK(settings_reply(&s)&&s.sequence==2&&s.intensity==25);
    wireless_settings_reply_complete(false);CHECK(reply_pending);
    CHECK(settings_reply(&s));wireless_settings_reply_complete(true);CHECK(!reply_pending);return 0;
}
EXPORT int check_settings_wire_validation(void)
{
    uint8_t p[38];wire_settings_t s={777,123,1,3,100,3,0},d;
    wire_settings_encode(p,8,&s);CHECK(wire_settings_decode(p,38,8,&d));
    CHECK(!wire_settings_decode(p,37,8,&d));p[37]=1;CHECK(!wire_settings_decode(p,38,8,&d));p[37]=0;
    p[18]=101;CHECK(!wire_settings_decode(p,38,8,&d));p[18]=100;
    p[19]=0;CHECK(!wire_settings_decode(p,38,8,&d));p[19]=3;
    p[20]=1;CHECK(!wire_settings_decode(p,38,8,&d));p[20]=0;
    p[17]=0;CHECK(!wire_settings_decode(p,38,8,&d));
    s.mask=s.intensity=s.level=0;wire_settings_encode(p,8,&s);CHECK(wire_settings_decode(p,38,8,&d));
    s.intensity=0;s.level=1;wire_settings_encode(p,9,&s);CHECK(wire_settings_decode(p,38,9,&d));
    p[20]=4;CHECK(!wire_settings_decode(p,38,9,&d));return 0;
}
