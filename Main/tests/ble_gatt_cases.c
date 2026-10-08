static void reset_adapter(void)
{
    init_rc=count_rc=add_rc=adv_rc=fields_rc=security_rc=store_rc=notify_rc=0;
    count_calls=add_calls=host_starts=deinit_calls=store_inits=adv_calls=security_calls=delete_calls=0;
    notify_calls=mbuf_frees=completed_calls=event_calls=save_calls=0;
    mock_epoch=0;mock_generation=1;mock_conn=0;
    mock_connected=mock_ready=mock_encrypted=fail_alloc=fail_append=fail_save=adv_active=false;
    mock_strength=63;queued_count=0;host_synced=false;protocol_mode=1;
    memset(&peer,0,sizeof(peer));memset(&mouse_mailbox,0,sizeof(mouse_mailbox));
    memset(&ble_hs_cfg,0,sizeof(ble_hs_cfg));
    (void)ble_hid_init();
}
static void connect_peer(uint16_t conn)
{
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT};e.connect.conn_handle=conn;
    adv_active=false;gap_event(&e,NULL);
}
static void disconnect_peer(void)
{
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};e.disconnect.conn.conn_handle=peer.conn;
    gap_event(&e,NULL);
}
static void subscribe(uint16_t conn,uint16_t handle,bool enabled,uint8_t reason)
{
    struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE};e.subscribe.conn_handle=conn;
    e.subscribe.attr_handle=handle;e.subscribe.cur_notify=enabled;e.subscribe.reason=reason;gap_event(&e,NULL);
}
static void encrypt(int status)
{
    mock_encrypted=status==0;struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE};
    e.enc_change.conn_handle=peer.conn;e.enc_change.status=status;gap_event(&e,NULL);
}
static void ready_peer(void)
{
    connect_peer(1);subscribe(1,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);encrypt(0);
}
static input_report_t report(void)
{
    input_report_t r={.mode=MOUSE_MODE,.generation=mock_generation,.data.mouse={1,-127,127,-3,4}};return r;
}
EXPORT int check_nimble_init_and_service_layout(void)
{
    reset_adapter();CHECK(count_calls==1&&add_calls==1&&host_starts==1&&store_inits==1);
    CHECK(ble_hs_cfg.sm_bonding&&!ble_hs_cfg.sm_mitm&&ble_hs_cfg.sm_io_cap==BLE_HS_IO_NO_INPUT_OUTPUT);
    CHECK((ble_hs_cfg.sm_our_key_dist&(BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID))==3);
    CHECK(((ble_uuid16_t *)services[0].uuid)->value==0x180f);
    CHECK(((ble_uuid16_t *)services[1].uuid)->value==0x1812&&hid_includes[0]==&services[0]&&!hid_includes[1]);
    CHECK(mouse_handle&&boot_handle&&battery_handle&&mouse_handle!=battery_handle);
    CHECK((hid_chars[4].flags&BLE_GATT_CHR_F_NOTIFY)&&hid_chars[4].val_handle==&mouse_handle);
    unsigned inputs=0,refs=0;
    for(const struct ble_gatt_chr_def *c=hid_chars;c->uuid;++c) {
        if(c->flags&BLE_GATT_CHR_F_NOTIFY)++inputs;
        if(c->descriptors)for(struct ble_gatt_dsc_def *d=c->descriptors;d->uuid;++d) {
            CHECK(((ble_uuid16_t *)d->uuid)->value!=0x2902);
            if(((ble_uuid16_t *)d->uuid)->value==0x2908)++refs;
        }
    }
    CHECK(inputs==4&&refs==5);
    CHECK(hid_chars[8].val_handle==&consumer_handle&&hid_chars[9].val_handle==&keyboard_handle);
    return 0;
}
EXPORT int check_nimble_registration_failures(void)
{
    reset_adapter();count_rc=BLE_HS_ENOMEM;unsigned starts=host_starts,adds=add_calls;
    CHECK(ble_hid_init()==ESP_FAIL&&host_starts==starts&&add_calls==adds&&deinit_calls==1);
    count_rc=0;add_rc=BLE_HS_ENOMEM;
    CHECK(ble_hid_init()==ESP_FAIL&&host_starts==starts&&deinit_calls==2);
    init_rc=ESP_FAIL;CHECK(ble_hid_init()==ESP_FAIL&&deinit_calls==2);return 0;
}
EXPORT int check_nimble_advertising_and_reset(void)
{
    reset_adapter();CHECK(!adv_calls);on_sync();CHECK(host_synced&&adv_calls==1);
    CHECK(advertised_address==BLE_OWN_ADDR_PUBLIC&&advertised_params.itvl_min==32&&advertised_params.itvl_max==48);
    CHECK(advertised.uuids16->value==0x1812&&advertised.appearance==0x03c9);
    CHECK(scan_response.name_len==12&&!memcmp(scan_response.name,"R-SODIUM BLE",12));
    ready_peer();CHECK(mock_ready&&security_calls==1);
    disconnect_peer();CHECK(!mock_ready&&adv_calls==2);
    adv_active=false;adv_rc=BLE_HS_EBUSY;advertise();CHECK(adv_retry.active&&adv_retry.ticks==250);
    adv_rc=0;retry_advertising(NULL);CHECK(adv_active);
    ready_peer();on_reset(1);adv_active=false;CHECK(!host_synced&&!peer.connected&&!mock_ready&&!adv_retry.active);
    on_sync();CHECK(host_synced&&adv_active);return 0;
}
EXPORT int check_nimble_subscription_security_and_restore(void)
{
    reset_adapter();connect_peer(1);
    subscribe(1,battery_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);
    subscribe(1,boot_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);encrypt(0);CHECK(!mock_ready);
    subscribe(2,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);CHECK(!mock_ready);
    subscribe(1,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);CHECK(mock_ready);
    subscribe(1,mouse_handle,false,BLE_GAP_SUBSCRIBE_REASON_WRITE);CHECK(!mock_ready);
    disconnect_peer();connect_peer(1);
    subscribe(1,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_RESTORE);CHECK(!mock_ready);
    encrypt(0);CHECK(mock_ready);encrypt(1);CHECK(!mock_ready);
    disconnect_peer();connect_peer(1);encrypt(0);CHECK(!mock_ready);
    subscribe(1,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_RESTORE);CHECK(mock_ready);
    return 0;
}
EXPORT int check_nimble_repeat_pairing_is_peer_scoped(void)
{
    reset_adapter();ready_peer();struct ble_gap_event e={.type=BLE_GAP_EVENT_REPEAT_PAIRING};
    e.repeat_pairing.conn_handle=2;CHECK(gap_event(&e,NULL)==BLE_GAP_REPEAT_PAIRING_IGNORE&&!delete_calls);
    e.repeat_pairing.conn_handle=1;CHECK(gap_event(&e,NULL)==BLE_GAP_REPEAT_PAIRING_RETRY);
    CHECK(delete_calls==1&&deleted_peer.val[0]==42&&!mock_ready);
    store_rc=1;CHECK(gap_event(&e,NULL)==BLE_GAP_REPEAT_PAIRING_IGNORE);return 0;
}
static int access_value(unsigned value,uint8_t op,unsigned offset,struct os_mbuf *om)
{
    struct ble_gatt_access_ctxt c={.op=op,.offset=offset,.om=om};
    return gatt_access(1,1,&c,(void *)(uintptr_t)value);
}
EXPORT int check_nimble_feature_and_read_blob_boundaries(void)
{
    reset_adapter();struct os_mbuf om={0};
    CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_READ_CHR,0,&om)==0&&om.len==1&&om.data[0]==63);
    om.len=0;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_READ_CHR,1,&om)==0);
    om.len=0;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_READ_CHR,2,&om)==BLE_ATT_ERR_INVALID_OFFSET);
    for(unsigned i=0;i<2;++i) {om.len=1;om.data[0]=i?100:0;
        CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,0,&om)==0&&mock_strength==om.data[0]);}
    CHECK(save_calls==2);om.data[0]=101;
    CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,0,&om)==BLE_ATT_ERR_VALUE_NOT_ALLOWED);
    om.len=0;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,0,&om)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
    om.len=2;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,0,&om)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
    om.len=1;om.data[0]=50;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,1,&om)==BLE_ATT_ERR_INVALID_OFFSET);
    fail_save=true;CHECK(access_value(VALUE_STRENGTH,BLE_GATT_ACCESS_OP_WRITE_CHR,0,&om)==BLE_ATT_ERR_UNLIKELY);
    CHECK(mock_strength==100&&save_calls==2);
    for(unsigned offset=0;offset<=ble_mouse_hid_report_len;offset+=22) {
        om.len=0;CHECK(access_value(VALUE_MAP,BLE_GATT_ACCESS_OP_READ_CHR,offset,&om)==0);
        CHECK(om.len==ble_mouse_hid_report_len&&!memcmp(om.data,ble_mouse_hid_report_descriptor,om.len));
    }
    om.len=0;CHECK(access_value(VALUE_MAP,BLE_GATT_ACCESS_OP_READ_CHR,ble_mouse_hid_report_len+1,&om)==BLE_ATT_ERR_INVALID_OFFSET);
    fail_append=true;CHECK(access_value(VALUE_MAP,BLE_GATT_ACCESS_OP_READ_CHR,0,&om)==BLE_ATT_ERR_INSUFFICIENT_RES);
    return 0;
}
EXPORT int check_nimble_notify_once_and_resource_retry(void)
{
    reset_adapter();ready_peer();input_report_t r=report();uint32_t epoch=peer.epoch;
    CHECK(ble_hid_send_mouse(1,epoch,&r)==ESP_OK);
    CHECK(ble_hid_send_mouse(1,epoch,&r)==ESP_ERR_NO_MEM&&!notify_calls);
    drain_events();CHECK(notify_calls==1&&event_calls==1&&completed_calls==1&&mbuf_frees==1);
    CHECK(completed_result==BLE_TX_OK&&completed_epoch==epoch&&completed_handle==mouse_handle);
    const uint8_t expected[]={1,0x81,0x7f,0xfd,4};CHECK(sent_length==5&&!memcmp(sent_data,expected,5));
    notify_rc=BLE_HS_ENOMEM;ble_hid_send_mouse(1,epoch,&r);drain_events();
    CHECK(completed_calls==2&&completed_result==BLE_TX_RETRY&&event_calls==2&&mbuf_frees==2);
    fail_alloc=true;ble_hid_send_mouse(1,epoch,&r);drain_events();
    CHECK(completed_calls==3&&completed_result==BLE_TX_RETRY&&notify_calls==2);
    fail_alloc=false;notify_rc=BLE_HS_ENOTCONN;ble_hid_send_mouse(1,epoch,&r);drain_events();
    CHECK(completed_calls==4&&completed_result==BLE_TX_FAILED);return 0;
}
EXPORT int check_nimble_reused_handle_drops_old_mailbox(void)
{
    reset_adapter();ready_peer();input_report_t r=report();uint32_t epoch=peer.epoch;
    ble_hid_send_mouse(1,epoch,&r);disconnect_peer();ready_peer();
    CHECK(peer.epoch!=epoch);drain_events();CHECK(!notify_calls&&!completed_calls&&!mouse_mailbox.busy);
    ble_hid_send_mouse(1,peer.epoch,&r);++mock_generation;drain_events();
    CHECK(!notify_calls&&completed_calls==1&&completed_result==BLE_TX_RETRY);
    r.generation=mock_generation;ble_hid_send_mouse(1,peer.epoch,&r);encrypt(1);drain_events();
    CHECK(!notify_calls&&completed_calls==2&&completed_result==BLE_TX_RETRY);return 0;
}
EXPORT int check_nimble_battery_does_not_complete_mouse(void)
{
    reset_adapter();ready_peer();pending_battery=82;
    send_battery_event(NULL);CHECK(battery_level==82&&!notify_calls);
    subscribe(1,battery_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);send_battery_event(NULL);
    CHECK(notify_calls==1&&sent_handle==battery_handle&&sent_length==1&&sent_data[0]==82&&!completed_calls);
    encrypt(1);send_battery_event(NULL);CHECK(notify_calls==1);
    encrypt(0);subscribe(1,battery_handle,false,BLE_GAP_SUBSCRIBE_REASON_WRITE);send_battery_event(NULL);
    CHECK(notify_calls==1);return 0;
}

EXPORT int check_nimble_sdk_read_blob_slicing(void)
{
    reset_adapter();
    for(unsigned offset=0;offset<=ble_mouse_hid_report_len;++offset) {
        struct os_mbuf output={0}, *out=&output;
        struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_READ_CHR,.offset=offset};
        CHECK(ble_gatts_val_access(1,1,offset,&ctxt,&out,gatt_access,(void *)VALUE_MAP)==0);
        CHECK(output.len==ble_mouse_hid_report_len-offset);
        CHECK(!memcmp(output.data,ble_mouse_hid_report_descriptor+offset,output.len));
    }
    struct os_mbuf output={0}, *out=&output;
    struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_READ_CHR,.offset=1};
    CHECK(ble_gatts_val_access(1,1,1,&ctxt,&out,gatt_access,(void *)VALUE_STRENGTH)==0&&output.len==0);
    ctxt.offset=2;CHECK(ble_gatts_val_access(1,1,2,&ctxt,&out,gatt_access,(void *)VALUE_STRENGTH)==BLE_ATT_ERR_INVALID_OFFSET);
    return 0;
}
EXPORT int check_nimble_advertising_setup_failure_recovers(void)
{
    reset_adapter();fields_rc=BLE_HS_ENOMEM;on_sync();
    CHECK(host_synced&&!advertising_configured&&!adv_calls&&adv_retry.active);
    fields_rc=0;retry_advertising(NULL);CHECK(advertising_configured&&adv_calls==1&&adv_active);
    /* A failed connection attempt must resume advertising. */
    adv_active=false;struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT};e.connect.status=1;
    gap_event(&e,NULL);CHECK(adv_calls==2&&!peer.connected);
    return 0;
}
EXPORT int check_nimble_aux_subscription_and_security(void)
{
    reset_adapter();connect_peer(1);
    subscribe(1,consumer_handle,true,BLE_GAP_SUBSCRIBE_REASON_RESTORE);
    subscribe(1,keyboard_handle,true,BLE_GAP_SUBSCRIBE_REASON_RESTORE);
    CHECK(!mock_aux_ready&&!mock_ready);encrypt(0);
    CHECK(mock_aux_ready==(AUX_OUTPUT_CONSUMER|AUX_OUTPUT_KEYBOARD)&&!mock_ready);
    subscribe(1,mouse_handle,true,BLE_GAP_SUBSCRIBE_REASON_WRITE);CHECK(mock_ready&&mock_aux_ready==AUX_OUTPUT_ALL);
    subscribe(1,consumer_handle,false,BLE_GAP_SUBSCRIBE_REASON_WRITE);
    CHECK(mock_ready&&mock_aux_ready==(AUX_OUTPUT_MOUSE|AUX_OUTPUT_KEYBOARD));
    encrypt(1);CHECK(!mock_ready&&!mock_aux_ready);
    encrypt(0);CHECK(mock_ready&&mock_aux_ready==(AUX_OUTPUT_MOUSE|AUX_OUTPUT_KEYBOARD));
    disconnect_peer();CHECK(!mock_aux_ready);return 0;
}
EXPORT int check_nimble_aux_payload_mtu23_and_read_values(void)
{
    reset_adapter();ready_peer();subscribe(1,consumer_handle,true,0);subscribe(1,keyboard_handle,true,0);
    struct os_mbuf om={0};
    CHECK(!access_value(REF_CONSUMER,BLE_GATT_ACCESS_OP_READ_DSC,0,&om)&&om.len==2&&om.data[0]==7&&om.data[1]==1);
    om.len=0;CHECK(!access_value(REF_KEYBOARD,BLE_GATT_ACCESS_OP_READ_DSC,0,&om)&&om.data[0]==8&&om.data[1]==1);
    const uint8_t ids[]={7,8};const unsigned lengths[]={2,8},values[]={VALUE_CONSUMER,VALUE_KEYBOARD};
    for(unsigned i=0;i<2;++i) {
        aux_output_report_t r={.id=ids[i],.length=lengths[i],.generation=mock_generation};
        r.data[0]=i?1:0xe9;if(i)r.data[2]=6;
        unsigned completions=completed_calls;
        CHECK(ble_hid_send_aux(1,peer.epoch,&r)==ESP_OK);drain_events();
        CHECK(completed_calls==completions+1&&completed_result==BLE_TX_OK);
        CHECK(sent_length==lengths[i]&&sent_length<=20&&!memcmp(sent_data,r.data,r.length));
        CHECK(sent_handle==hid_dev_report_handle(ids[i]));
        om.len=0;CHECK(!access_value(values[i],BLE_GATT_ACCESS_OP_READ_CHR,0,&om)&&om.len==r.length&&!memcmp(om.data,r.data,r.length));
        memset(r.data,0,sizeof(r.data));r.release=true;
        CHECK(ble_hid_send_aux(1,peer.epoch,&r)==ESP_OK);drain_events();
        CHECK(!memcmp(sent_data,r.data,r.length));
        r.length=9;CHECK(ble_hid_send_aux(1,peer.epoch,&r)==ESP_ERR_INVALID_ARG);
    }
    return 0;
}
EXPORT int check_nimble_scroll_preserves_successful_buttons(void)
{
    reset_adapter();ready_peer();input_report_t mouse=report();
    mouse.data.mouse.buttons=3;ble_hid_send_mouse(1,peer.epoch,&mouse);drain_events();
    aux_output_report_t r={.id=2,.length=5,.generation=mock_generation,.data={0,0,0,0xfd,4}};
    CHECK(ble_hid_send_aux(1,peer.epoch,&r)==ESP_OK);
    CHECK(ble_hid_send_mouse(1,peer.epoch,&mouse)==ESP_ERR_NO_MEM);drain_events();
    const uint8_t expected[]={3,0,0,0xfd,4};CHECK(sent_handle==mouse_handle&&!memcmp(sent_data,expected,5));
    mouse.data.mouse.buttons=0;notify_rc=BLE_HS_ENOMEM;ble_hid_send_mouse(1,peer.epoch,&mouse);drain_events();
    notify_rc=0;ble_hid_send_aux(1,peer.epoch,&r);drain_events();CHECK(sent_data[0]==3);
    ble_hid_send_mouse(1,peer.epoch,&mouse);drain_events();
    ble_hid_send_aux(1,peer.epoch,&r);drain_events();CHECK(sent_data[0]==0);return 0;
}
EXPORT int check_nimble_aux_mailbox_retry_cancel_and_reconnect(void)
{
    reset_adapter();ready_peer();subscribe(1,keyboard_handle,true,0);
    aux_output_report_t r={.id=8,.length=8,.generation=mock_generation,.data={1,0,6}};
    ble_hid_send_aux(1,peer.epoch,&r);subscribe(1,keyboard_handle,false,0);drain_events();
    CHECK(!notify_calls&&completed_result==BLE_TX_RETRY);
    subscribe(1,keyboard_handle,true,0);fail_alloc=true;
    ble_hid_send_aux(1,peer.epoch,&r);drain_events();CHECK(!notify_calls&&completed_result==BLE_TX_RETRY);
    fail_alloc=false;notify_rc=BLE_HS_EAGAIN;
    ble_hid_send_aux(1,peer.epoch,&r);drain_events();CHECK(notify_calls==1&&completed_calls==3&&completed_result==BLE_TX_RETRY);
    notify_rc=0;ble_hid_send_aux(1,peer.epoch,&r);++mock_generation;drain_events();CHECK(notify_calls==1);
    r.generation=mock_generation;ble_hid_send_aux(1,peer.epoch,&r);disconnect_peer();ready_peer();drain_events();
    CHECK(notify_calls==1&&completed_calls==4);return 0;
}
