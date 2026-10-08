static unsigned radio_inputs, radio_controls;
static uint32_t last_radio_type;
static bool radio_reject, radio_no_callback;
static esp_err_t esp_now_send(const uint8_t *mac,const uint8_t *packet,unsigned size)
{
    (void)mac;if(size!=38)return ESP_FAIL;
    last_radio_type=wire_u32(packet);
    if(last_radio_type==MOUSE_MODE||last_radio_type==WIRELESS_HAPTIC_PTP_MODE||last_radio_type==WIRE_AUX)
        ++radio_inputs;
    else ++radio_controls;
    if(radio_reject)return ESP_FAIL;
    if(!radio_no_callback)send_callback(NULL,ESP_NOW_SEND_SUCCESS);
    return ESP_OK;
}
static void connection_test_boot(int boot,bool enabled,bool high,bool keep_handoff)
{
    route_mutex=NULL;reset_test();current_mode=boot;config.bytes[51]=enabled|CFG_FLAG_CUSTOM_GESTURE_HAPTICS;test_vbus=high;
    route=-1;retiring=false;route_epoch=1;usb_mode=MOUSE_MODE;
    memset(links,0,sizeof(links));memset(flights,0,sizeof(flights));
    config_wait=wake_event=false;route_lock_depth=route_lock_errors=0;
    fail_manual_save=false;reset_reason=keep_handoff?ESP_RST_SW:0;restart_count=0;
    if(!keep_handoff)handoff.magic=handoff.inverse=0;
    connection_init(boot);input_set_link(0);connection_probe_ready(123);
    receiver_mac[0]=2;radio_inputs=radio_controls=0;radio_reject=radio_no_callback=false;
    send_done=acknowledged=false;test_settings_reply_pending=false;
    surface=(wire_surface_t){WIRE_VERSION,0,987};
}
static void poll_at(uint32_t t) { now=t;connection_poll(); }
static void poll_between_radio_submissions(void) { now+=10;connection_poll(); }
static void usb_drain(void)
{
    for(unsigned i=0;i<12;++i) {
        run_pump();
        for(unsigned ep=1;ep<3;++ep)if(endpoint_busy[ep])finish(ep,true);
    }
}
static void probe_answer(void)
{
    uint8_t b[38];connection_probe_packet(b);
    wire_probe_encode(b,WIRE_PROBE_ACK,&probe.token);
    wireless_receiver_probe_ack(receiver_mac,b,38);
}
static void resume_auto(int target,bool baseline,bool wait_up)
{
    handoff.magic=HANDOFF_MAGIC;handoff.inverse=~HANDOFF_MAGIC;
    handoff.mode=target;handoff.high=baseline;handoff.reason=HANDOFF_AUTO;handoff.wait_up=wait_up;
    connection_test_boot(0,true,baseline,true);poll_at(0);poll_at(300);
}
EXPORT int check_probe_wire_validation_and_vector(void)
{
    wire_probe_t token={0x12345678,0x01020304},out;uint8_t b[38];
    wire_probe_encode(b,WIRE_PROBE,&token);
    const uint8_t prefix[]={10,0,0,0,1,0x78,0x56,0x34,0x12,4,3,2,1};
    CHECK(!memcmp(b,prefix,sizeof(prefix))&&wire_probe_decode(b,38,WIRE_PROBE,&out));
    CHECK(out.session==token.session&&out.round==token.round);
    CHECK(!wire_probe_decode(NULL,38,WIRE_PROBE,&out));
    CHECK(!wire_probe_decode(b,37,WIRE_PROBE,&out)&&!wire_probe_decode(b,39,WIRE_PROBE,&out));
    CHECK(!wire_probe_decode(b,38,WIRE_PROBE_ACK,&out));
    for(unsigned i=13;i<38;++i){b[i]=1;CHECK(!wire_probe_decode(b,38,WIRE_PROBE,&out));b[i]=0;}
    b[4]=2;CHECK(!wire_probe_decode(b,38,WIRE_PROBE,&out));b[4]=1;
    wire_put32(b+5,0);CHECK(!wire_probe_decode(b,38,WIRE_PROBE,&out));
    wire_put32(b+5,1);wire_put32(b+9,0);CHECK(!wire_probe_decode(b,38,WIRE_PROBE,&out));
    return 0;
}
EXPORT int check_probe_retry_expiry_duplicate_and_wrap(void)
{
    wireless_probe_t p={.token={99,0}};uint8_t b[38];
    wireless_probe_begin(&p,0xffffff00U);
    CHECK(wireless_probe_packet(&p,0xffffff00U,b));
    CHECK(!wireless_probe_packet(&p,0xfffffff9U,b));
    CHECK(wireless_probe_packet(&p,0xfffffffaU,b));
    CHECK(wireless_probe_packet(&p,244,b));
    CHECK(!wireless_probe_packet(&p,494,b));
    wire_probe_encode(b,WIRE_PROBE_ACK,&p.token);CHECK(!wireless_probe_ack(&p,b,38,494));
    wireless_probe_begin(&p,1000);CHECK(wireless_probe_packet(&p,1000,b));
    wire_probe_t stale={99,1};wire_probe_encode(b,WIRE_PROBE_ACK,&stale);
    CHECK(!wireless_probe_ack(&p,b,38,1010));
    wire_probe_encode(b,WIRE_PROBE_ACK,&p.token);CHECK(wireless_probe_ack(&p,b,38,1020));
    CHECK(!wireless_probe_ack(&p,b,38,1200)&&p.seen_at==1020);
    CHECK(!wireless_probe_packet(&p,1300,b));
    CHECK(wireless_probe_fresh(&p,3519)&&!wireless_probe_fresh(&p,3520));
    p.token.round=0xffffffffU;wireless_probe_begin(&p,4000);CHECK(p.token.round==1);
    return 0;
}
EXPORT int check_usb_probe_only_and_mac_session_isolation(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);poll_at(310);
    connection_lock();connection_link(0,true);connection_unlock();input_apply_mode_request();usb_drain();
    CHECK(publish(1));unsigned queued=reports.count;
    test_settings_reply_pending=true;unsigned replies=test_settings_completions;
    wifi_hook=poll_between_radio_submissions;wifi_budget=80;wifi_send_task(NULL);wifi_hook=NULL;
    CHECK(radio_controls==3&&last_radio_type==WIRE_PROBE&&!radio_inputs&&reports.count==queued);
    CHECK(test_settings_completions==replies&&test_settings_reply_pending&&!acknowledged);
    poll_at(1320);uint8_t b[38];CHECK(connection_probe_packet(b));
    wire_probe_encode(b,WIRE_PROBE_ACK,&probe.token);
    uint8_t wrong[6]={4,2,3,4,5,6};wireless_receiver_probe_ack(wrong,b,38);CHECK(!probe.seen);
    b[5]^=1;wireless_receiver_probe_ack(receiver_mac,b,38);CHECK(!probe.seen);b[5]^=1;
    wireless_receiver_probe_ack(receiver_mac,b,38);CHECK(probe.seen&&!acknowledged&&!links[1]);
    CHECK(route==0&&!restart_count&&!route_lock_errors);
    test_settings_reply_pending=false;return 0;
}
EXPORT int check_unplug_fresh_ack_restarts_radio_once(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);poll_at(310);probe_answer();
    CHECK(probe.seen);saved_manual_mode=77;
    test_vbus=false;poll_at(320);CHECK(!connection_can_send(0));poll_at(610);CHECK(!restart_count);
    poll_at(620);CHECK(restart_count==1&&route==0&&handoff.mode==1&&handoff.reason==HANDOFF_AUTO&&handoff.wait_up);
    poll_at(900);CHECK(restart_count==1&&saved_manual_mode==77);
    connection_test_boot(0,true,false,true);poll_at(0);poll_at(300);poll_at(5000);
    CHECK(current_mode==1&&route==1&&!restart_count&&!handoff.magic&&source_wait_up);
    CHECK(!connection_can_send(1));
    return 0;
}
EXPORT int check_no_receiver_restarts_ble_then_usb(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);
    test_vbus=false;poll_at(310);poll_at(610);CHECK(policy.target==CONNECTION_PROBE&&!restart_count);
    uint8_t b[38];CHECK(connection_probe_packet(b));poll_at(1350);CHECK(!restart_count);
    poll_at(1360);CHECK(restart_count==1&&handoff.mode==2);
    connection_test_boot(0,true,false,true);poll_at(0);poll_at(300);poll_at(10000);
    CHECK(route==2&&!policy.ble&&!restart_count&&!probe_allowed);
    test_vbus=true;poll_at(10010);poll_at(10310);
    CHECK(restart_count==1&&handoff.mode==0);
    connection_test_boot(0,true,true,true);poll_at(0);poll_at(300);
    CHECK(route==0&&!restart_count);return 0;
}
EXPORT int check_cold_boot_detects_even_saved_ble(void)
{
    for(unsigned responds=0;responds<2;++responds) {
        connection_test_boot(2,true,false,false);CHECK(current_mode==0);poll_at(0);poll_at(300);
        CHECK(route==-1&&policy.target==CONNECTION_PROBE);
        if(responds)probe_answer();poll_at(1050);
        CHECK(restart_count==1&&handoff.mode==(responds?1:2)&&!handoff.wait_up);
        connection_test_boot(2,true,false,true);poll_at(0);poll_at(300);
        CHECK(route==(responds?1:2)&&!source_wait_up&&!restart_count);
    }
    connection_test_boot(2,true,true,false);poll_at(0);poll_at(300);
    CHECK(route==0&&!restart_count);return 0;
}
EXPORT int check_stale_ack_triggers_new_round_and_old_response_rejected(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);poll_at(310);probe_answer();
    wire_probe_t old=probe.token;test_vbus=false;poll_at(3000);poll_at(3300);
    CHECK(policy.target==CONNECTION_PROBE&&probe_waiting&&!restart_count);
    uint8_t b[38];CHECK(connection_probe_packet(b));wire_probe_encode(b,WIRE_PROBE_ACK,&old);
    wireless_receiver_probe_ack(receiver_mac,b,38);poll_at(4040);CHECK(!restart_count);
    poll_at(4050);CHECK(restart_count==1&&handoff.mode==2);return 0;
}
EXPORT int check_debounce_wake_and_charge_only(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);CHECK(route==0&&!links[0]);
    test_vbus=false;poll_at(310);test_vbus=true;poll_at(600);poll_at(900);CHECK(route==0&&!restart_count);
    connection_wake();poll_at(910);poll_at(1210);CHECK(route==0&&!restart_count);
    connection_policy_t p;connection_policy_init(&p,0,true,false,false);
    CHECK(connection_policy_sample(&p,false,0xffffff00U)==CONNECTION_WAIT);
    CHECK(connection_policy_sample(&p,false,43)==CONNECTION_WAIT);
    CHECK(connection_policy_sample(&p,false,44)==CONNECTION_PROBE);return 0;
}
EXPORT int check_manual_priority_ble_and_reboot_cable_change(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);
    CHECK(connection_manual_restart(1)==ESP_OK&&saved_manual_mode==1);
    connection_test_boot(1,true,true,true);poll_at(0);poll_at(300);
    CHECK(route==1&&policy.manual&&!restart_count);
    connection_wake();poll_at(310);poll_at(610);CHECK(policy.manual&&!restart_count);
    test_vbus=false;poll_at(620);poll_at(920);probe_answer();poll_at(930);
    CHECK(route==1&&!policy.manual&&!restart_count);
    test_vbus=true;poll_at(940);poll_at(1240);CHECK(restart_count==1&&handoff.mode==0);
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);
    CHECK(connection_manual_restart(2)==ESP_OK);
    connection_test_boot(2,true,false,true);poll_at(0);poll_at(300);
    test_vbus=true;poll_at(310);poll_at(610);CHECK(route==2&&policy.ble&&!restart_count);
    CHECK(connection_manual_restart(0)==ESP_OK);
    connection_test_boot(0,true,false,true);poll_at(0);poll_at(300);poll_at(1050);
    CHECK(restart_count==1&&handoff.mode==2);
    connection_test_boot(0,true,true,false);fail_manual_save=true;
    CHECK(connection_manual_restart(1)==ESP_FAIL&&!handoff.magic);return 0;
}
EXPORT int check_auto_flag_ack_disable_cancels_pending_discovery(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);
    test_vbus=false;poll_at(310);poll_at(610);CHECK(probe_waiting);
    config.bytes[51]=2;connection_config_pending();poll_at(1360);CHECK(!restart_count);
    connection_config_complete();poll_at(1370);CHECK(!policy.enabled&&!probe_allowed&&route==0);
    poll_at(5000);CHECK(!restart_count);
    config.bytes[51]=3;connection_config_pending();poll_at(5010);CHECK(!policy.enabled);
    connection_config_complete();poll_at(5020);CHECK(policy.enabled&&probe_waiting);
    probe_answer();poll_at(5030);CHECK(restart_count==1&&handoff.mode==1);
    connection_test_boot(1,false,false,false);poll_at(0);test_vbus=true;poll_at(10);poll_at(310);
    CHECK(route==1&&!restart_count&&!probe_allowed);return 0;
}
EXPORT int check_repeat_auto_flag_does_not_cancel_manual(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);connection_manual_restart(1);
    connection_test_boot(1,true,true,true);poll_at(0);poll_at(300);
    config.bytes[51]=3;connection_config_pending();poll_at(310);connection_config_complete();poll_at(320);
    CHECK(policy.manual&&route==1&&!restart_count);return 0;
}
EXPORT int check_restart_drains_radio_and_preserves_lift_gate(void)
{
    resume_auto(1,false,false);input_apply_mode_request();
    connection_lock();connection_link(1,true);connection_unlock();
    input_report_t r;while(input_take_report(&r))input_report_ack(&r);
    aux_output_event_t e;while(aux_output_take_event(&e,input_generation(),now))aux_output_event_complete(true);
    CHECK(aux_output_hold(5,1,input_generation(),now));
    CHECK(aux_output_take_event(&e,input_generation(),now));aux_output_event_complete(true);
    test_vbus=true;poll_at(310);poll_at(610);CHECK(retiring&&!restart_count&&transport_paused);
    acknowledged=true;acknowledged_at=now;send_done=false;
    wifi_hook=poll_between_radio_submissions;wifi_budget=20;wifi_send_task(NULL);wifi_hook=NULL;
    CHECK(restart_count==1&&handoff.mode==0&&handoff.wait_up&&radio_inputs>=2&&!route_lock_errors);
    connection_test_boot(0,true,true,true);poll_at(0);CHECK(route==0&&source_wait_up);
    return 0;
}
EXPORT int check_restart_release_deadline_and_disable_cancellation(void)
{
    resume_auto(2,false,false);connection_lock();connection_link(2,true);connection_unlock();
    test_vbus=true;poll_at(310);poll_at(610);CHECK(retiring&&!restart_count);
    poll_at(850);CHECK(!restart_count);poll_at(860);CHECK(restart_count==1&&handoff.mode==0);
    resume_auto(1,false,false);connection_lock();connection_link(1,true);connection_unlock();
    test_vbus=true;poll_at(310);poll_at(610);CHECK(retiring&&!restart_count);
    config.bytes[51]=2;connection_config_pending();poll_at(620);CHECK(!restart_count);
    connection_config_complete();poll_at(630);
    CHECK(!retiring&&!transport_paused&&route==1&&!restart_count);poll_at(2000);CHECK(!restart_count);return 0;
}
EXPORT int check_send_failure_or_missing_callback_does_not_block_ble_fallback(void)
{
    for(unsigned missing=0;missing<2;++missing) {
        connection_test_boot(0,true,false,false);poll_at(0);poll_at(300);
        radio_reject=!missing;radio_no_callback=missing;
        wifi_hook=poll_between_radio_submissions;wifi_budget=90;wifi_send_task(NULL);wifi_hook=NULL;
        CHECK(restart_count==1&&handoff.mode==2&&!radio_inputs&&!probe.seen);
    }
    return 0;
}
EXPORT int check_probe_receive_dispatch_cannot_change_usb_input_mode(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);poll_at(310);
    uint8_t b[38];CHECK(connection_probe_packet(b));
    wire_probe_encode(b,WIRE_PROBE_ACK,&probe.token);
    esp_now_recv_info_t info={receiver_mac};uint32_t serial=request_serial;
    wifi_now_recv_cb(&info,b,38);CHECK(probe.seen&&!acknowledged&&request_serial==serial);
    uint8_t mode=PTP_MODE;wifi_now_recv_cb(&info,&mode,1);CHECK(request_serial==serial);
    wifi_now_recv_cb(NULL,b,38);wifi_now_recv_cb(&info,NULL,38);wifi_now_recv_cb(&info,b,37);
    CHECK(request_serial==serial&&!restart_count);
    resume_auto(1,false,false);mode=MOUSE_MODE;serial=request_serial;
    wifi_now_recv_cb(&info,&mode,1);CHECK(request_serial!=serial&&requested_mode==MOUSE_MODE);
    return 0;
}
EXPORT int check_reinsert_during_probe_and_radio_loss_stays_selected(void)
{
    connection_test_boot(0,true,true,false);poll_at(0);poll_at(300);
    test_vbus=false;poll_at(310);poll_at(610);CHECK(probe_waiting);
    test_vbus=true;poll_at(620);poll_at(920);CHECK(policy.target==0&&!probe_waiting&&!restart_count);
    poll_at(1400);CHECK(!restart_count);
    resume_auto(1,false,false);connection_lock();connection_link(1,true);connection_link(1,false);connection_unlock();
    poll_at(4000);CHECK(route==1&&!restart_count&&!probe_allowed);
    /* A changed cable during an automatic reboot is re-evaluated after debounce. */
    connection_handoff(2,HANDOFF_AUTO,true);
    connection_test_boot(0,true,true,true);poll_at(0);poll_at(300);
    CHECK(restart_count==1&&handoff.mode==0);return 0;
}
