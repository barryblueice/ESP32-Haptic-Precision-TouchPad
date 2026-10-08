static const uint8_t settings_test_mac[6]={9,8,7,6,5,4};
static void settings_connect(uint32_t session)
{
    wire_surface_t s={WIRE_VERSION,0,session}; uint8_t p[38];
    wire_surface_encode(p,WIRE_SURFACE,&s);
    receiver_ext_receive(settings_test_mac,p,0);
    receiver_ext_apply(&s); receiver_ext_applied(&s); receiver_ext_usb_ready(true);
}
static void settings_answer(const uint8_t request[38], uint8_t intensity,uint8_t level,uint8_t status,uint32_t now)
{
    wire_settings_t s; wire_settings_decode(request,38,WIRE_SETTINGS,&s);
    s.intensity=intensity;s.level=level;s.status=status;
    uint8_t p[38];wire_settings_encode(p,WIRE_SETTINGS_ACK,&s);
    receiver_settings_receive(settings_test_mac,p,now);
}
EXPORT int check_settings_query_then_atomic_set(void)
{
    reset_all();uint8_t p[38],mac[6];wire_settings_t s;
    receiver_settings_set(1,100);receiver_settings_set(2,3);
    CHECK(!receiver_settings_next(p,mac,0));settings_connect(777);
    CHECK(receiver_settings_next(p,mac,0));CHECK(wire_settings_decode(p,38,8,&s) && !s.mask);
    CHECK(receiver_settings_get(1)==63 && receiver_settings_get(2)==2);
    settings_answer(p,25,1,0,1);
    CHECK(receiver_settings_get(1)==25 && receiver_settings_get(2)==1);
    CHECK(receiver_settings_next(p,mac,2));CHECK(wire_settings_decode(p,38,8,&s));
    CHECK(s.mask==3 && s.intensity==100 && s.level==3);
    settings_answer(p,100,3,0,3);
    CHECK(receiver_settings_get(1)==100 && receiver_settings_get(2)==3 && !dirty);
    CHECK(!receiver_settings_next(p,mac,4));return 0;
}
EXPORT int check_settings_retry_and_newer_edits(void)
{
    reset_all();settings_connect(777);uint8_t p[38],old[38],mac[6];wire_settings_t s;
    CHECK(receiver_settings_next(p,mac,0));settings_answer(p,63,2,0,1);
    receiver_settings_set(1,0);CHECK(receiver_settings_next(p,mac,2));memcpy(old,p,38);
    CHECK(!receiver_settings_next(p,mac,251));CHECK(receiver_settings_next(p,mac,252));CHECK(!memcmp(p,old,38));
    receiver_settings_set(1,80);receiver_settings_set(2,1);
    settings_answer(old,0,2,0,253);CHECK(dirty==3);
    CHECK(receiver_settings_next(p,mac,254));CHECK(wire_settings_decode(p,38,8,&s));
    CHECK(s.mask==3 && s.intensity==80 && s.level==1);
    settings_answer(old,0,2,0,255);CHECK(transaction_pending);
    settings_answer(p,80,1,0,256);CHECK(!dirty && !transaction_pending);return 0;
}
EXPORT int check_settings_busy_storage_and_peer_validation(void)
{
    reset_all();settings_connect(777);uint8_t p[38],ack[38],mac[6];wire_settings_t s;
    CHECK(receiver_settings_next(p,mac,0));settings_answer(p,63,2,0,1);
    receiver_settings_set(1,90);CHECK(receiver_settings_next(p,mac,2));
    CHECK(wire_settings_decode(p,38,8,&s));s.intensity=90;s.level=2;
    wire_settings_encode(ack,9,&s);receiver_settings_receive(mac,ack,3);
    /* Correct peer above; reset for explicit wrong-session and wrong-peer replies. */
    receiver_settings_set(1,75);CHECK(receiver_settings_next(p,mac,4));
    CHECK(wire_settings_decode(p,38,8,&s));s.intensity=75;s.level=2;s.session++;
    wire_settings_encode(ack,9,&s);receiver_settings_receive(settings_test_mac,ack,5);CHECK(transaction_pending);
    s.session--;wire_settings_encode(ack,9,&s);mac[0]^=1;receiver_settings_receive(mac,ack,5);CHECK(transaction_pending);
    settings_answer(p,90,2,WIRE_SETTINGS_BUSY,6);CHECK(transaction_pending && dirty==1);
    CHECK(receiver_settings_next(p,mac,254));settings_answer(p,90,2,WIRE_SETTINGS_STORAGE,255);
    CHECK(!transaction_pending && !dirty && receiver_settings_get(1)==90);return 0;
}
EXPORT int check_settings_reconnect_and_usb_padding(void)
{
    reset_all();uint8_t p[38],old[38],mac[6];wire_settings_t s;
    settings_connect(777);CHECK(receiver_settings_next(p,mac,0));settings_answer(p,63,2,0,1);
    uint8_t set[]={0x41,0,0,0};
    tud_hid_set_report_cb(REPORT_HAPTIC,0,HID_REPORT_TYPE_FEATURE,set,4);
    CHECK(dirty==1 && wanted_intensity==0);CHECK(receiver_settings_next(old,mac,2));
    CHECK(!receiver_settings_next(p,mac,2600));
    settings_connect(778);CHECK(receiver_settings_next(p,mac,3));CHECK(wire_settings_decode(p,38,8,&s) && !s.mask);
    settings_answer(old,0,2,0,4);CHECK(transaction_pending);
    settings_answer(p,63,2,0,5);CHECK(receiver_settings_next(p,mac,6));CHECK(wire_settings_decode(p,38,8,&s)&&s.intensity==0&&s.mask==1);
    settings_answer(p,0,2,0,7);
    set[1]=100;set[3]=1;tud_hid_set_report_cb(REPORT_HAPTIC,0,HID_REPORT_TYPE_FEATURE,set,4);CHECK(!dirty);
    set[3]=0;tud_hid_set_report_cb(REPORT_HAPTIC,0,HID_REPORT_TYPE_FEATURE,set,4);CHECK(dirty==1&&wanted_intensity==100);
    set[1]=101;tud_hid_set_report_cb(REPORT_HAPTIC,0,HID_REPORT_TYPE_FEATURE,set,4);CHECK(wanted_intensity==100);
    return 0;
}
EXPORT int check_settings_radio_serialization_and_receive_routing(void)
{
    reset_all();settings_connect(777);
    wireless_control_step(0);CHECK(flight_ack && control_in_flight && radio_count==1);
    mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);wireless_control_step(1);
    CHECK(flight_settings && radio_count==2 && wire_u32(radio_pointer)==8);
    uint8_t saved[38];memcpy(saved,radio_pointer,38);
    wireless_request_mode();wireless_control_step(2);
    CHECK(radio_count==2 && !memcmp(saved,radio_pointer,38));
    mode_send_complete(NULL,ESP_FAIL);wireless_control_step(3);
    CHECK(!flight_settings && radio_count==3 && control_in_flight);
    mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);wireless_control_step(4);
    CHECK(transaction_pending && !control_in_flight);
    radio_result=ESP_FAIL;wireless_control_step(251);
    CHECK(radio_count==4 && !control_in_flight && transaction_pending);
    radio_result=ESP_OK;wireless_control_step(501);
    CHECK(radio_count==5 && flight_settings && !memcmp(saved,radio_pointer,38));
    wire_settings_t s;CHECK(wire_settings_decode(saved,38,8,&s));s.intensity=75;s.level=3;
    wire_settings_encode(saved,9,&s);
    receive_queue=xQueueCreate(16,sizeof(receive_frame_t));esp_now_recv_info_t info={0};memcpy(info.src_addr,settings_test_mac,6);
    fake_now=502;uint32_t gen=input_generation();wifi_now_recv_cb(&info,saved,38);
    CHECK(qcount==1);wireless_receive_step();
    CHECK(receiver_settings_get(1)==75 && receiver_settings_get(2)==3 && !transaction_pending);
    CHECK(input_generation()==gen && !lock_error);
    return 0;
}
