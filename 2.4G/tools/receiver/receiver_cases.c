EXPORT int check_surface_ack_and_rotation(void)
{
    reset_all(); uint8_t mac[6]={1,2,3,4,5,6}, packet[38], reply[38], dest[6];
    wire_surface_t s={WIRE_VERSION,1,123}; wire_surface_encode(packet,WIRE_SURFACE,&s);
    receiver_ext_receive(mac,packet,0);CHECK(!receiver_ext_accept(mac));CHECK(!receiver_ext_ack(reply,dest));
    usbhid_step();CHECK(disconnect_count==1&&descriptor_rotation==1);CHECK(receiver_ext_accept(mac));
    CHECK(receiver_ext_ack(reply,dest)&&!memcmp(mac,dest,6));wire_surface_t ack;
    CHECK(wire_surface_decode(reply,38,WIRE_SURFACE_ACK,&ack)&&ack.session==123&&ack.rotation==1);
    receiver_ext_ack_complete(true);CHECK(!receiver_ext_ack(reply,dest));
    receiver_ext_receive(mac,packet,200);usbhid_step();CHECK(disconnect_count==1);
    CHECK(receiver_ext_ack(reply,dest));return 0;
}

EXPORT int check_usb_input_mode_feature_roundtrip(void)
{
    for(unsigned instance=REPORT_HAPTIC;instance<=REPORT_LEGACY;++instance) {
        reset_all();
        uint8_t id=instance==REPORT_HAPTIC?REPORTID_HAPTIC_FEATURE:REPORTID_LEGACY_FEATURE;
        uint8_t bytes[3]={0xaa,0xaa,0xaa};
        CHECK(tud_hid_get_report_cb(instance,id,HID_REPORT_TYPE_FEATURE,bytes+1,1)==1);
        CHECK(bytes[0]==0xaa && bytes[1]==0 && bytes[2]==0xaa);
        uint8_t set[]={id,3,0,0};
        tud_hid_set_report_cb(instance,0,HID_REPORT_TYPE_FEATURE,set,sizeof(set));
        CHECK(input_mode()==TP_PTP_MODE);
        CHECK(tud_hid_get_report_cb(instance,id,HID_REPORT_TYPE_FEATURE,bytes+1,1)==1 && bytes[1]==3);
        set[1]=0;tud_hid_set_report_cb(instance,id,HID_REPORT_TYPE_FEATURE,set+1,3);
        CHECK(input_mode()==TP_MOUSE_MODE);
        CHECK(tud_hid_get_report_cb(instance,id,HID_REPORT_TYPE_FEATURE,bytes+1,1)==1 && bytes[1]==0);
    }
    return 0;
}

EXPORT int check_usb_mount_keeps_negotiated_ptp(void)
{
    for(unsigned instance=REPORT_HAPTIC;instance<=REPORT_LEGACY;++instance) {
        reset_all();
        uint8_t id=instance==REPORT_HAPTIC?REPORTID_HAPTIC_FEATURE:REPORTID_LEGACY_FEATURE;
        uint8_t value=3;
        tud_hid_set_report_cb(instance,id,HID_REPORT_TYPE_FEATURE,&value,1);
        event(TINYUSB_EVENT_ATTACHED);
        CHECK(input_mode()==TP_PTP_MODE && usb_ready);
        wireless_control_step(0);CHECK(radio_byte==TP_PTP_MODE && control_in_flight);
        mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);wireless_control_step(1);
        event(TINYUSB_EVENT_SUSPENDED);event(TINYUSB_EVENT_RESUMED);
        CHECK(input_mode()==TP_PTP_MODE);
        /* New radio sessions must replay the negotiated mode, not a mount default. */
        uint8_t mac[6]={1},packet[38];wire_surface_t surface={WIRE_VERSION,0,7};
        wire_surface_encode(packet,WIRE_SURFACE,&surface);
        receiver_ext_receive(mac,packet,2);usbhid_step();wireless_control_step(2);
        CHECK(flight_ack);mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);wireless_control_step(3);
        CHECK(!flight_ack && !flight_settings && radio_byte==TP_PTP_MODE);
    }
    return 0;
}
EXPORT int check_wire_actions_dedup_and_release(void)
{
    reset_all(); input_set_mode(TP_PTP_MODE);
    uint8_t mac[6]={1},packet[38];wire_surface_t s={WIRE_VERSION,0,7};wire_surface_encode(packet,WIRE_SURFACE,&s);
    receiver_ext_receive(mac,packet,0);usbhid_step();
    wire_action_t a={7,1,5,1};wire_action_encode(packet,&a);
    receiver_ext_receive(mac,packet,0);CHECK(count==1);
    receiver_ext_receive(mac,packet,1);CHECK(count==1);
    a.session=8;a.sequence=2;wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,2);CHECK(count==1);
    aux_output_report_t out;CHECK(aux_output_take(&out,input_generation(),2)&&out.id==8&&out.data[2]==0x52);
    aux_output_complete(true);CHECK(aux_output_take(&out,input_generation(),2)&&out.release);aux_output_complete(true);
    CHECK(!aux_output_take(&out,input_generation(),2));
    a=(wire_action_t){7,2,3,2};wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,2);CHECK(count==1);
    a=(wire_action_t){7,3,0,0};wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,2);CHECK(count==0);
    CHECK(input_mode()==TP_PTP_MODE);return 0;
}
EXPORT int check_wire_function_keys_and_release(void)
{
    /* Expected USB usages, independent of the production action lookup. */
    static const uint8_t usages[] = {
        0xe2,0xcd,0xb6,0xb5,0xb7,
        0x29,0x28,0x2b,0x2c,0x2a,0x4c,0x49,0x4a,0x4d,0x4b,0x4e,0x46,
        0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45,
        0x06,0x19,0x1b,0x1d,0x1c,0x04
    };
    for(unsigned action=13;action<=47;++action) {
        reset_all();input_set_mode(TP_PTP_MODE);
        uint8_t mac[6]={1},packet[38];wire_surface_t s={WIRE_VERSION,0,7};
        wire_surface_encode(packet,WIRE_SURFACE,&s);receiver_ext_receive(mac,packet,0);usbhid_step();
        wire_action_t a={7,1,action,1};wire_action_encode(packet,&a);
        receiver_ext_receive(mac,packet,0);receiver_ext_receive(mac,packet,1);CHECK(count==1);
        aux_output_report_t r;CHECK(aux_output_take(&r,input_generation(),500)&&!r.release);
        CHECK(r.id==(action<=17?7:8)&&r.data[action<=17?0:2]==usages[action-13]);
        CHECK(r.length==(action<=17?2:8));
        if(action>=18)CHECK(r.data[0]==(action>=42?1:0));
        for(unsigned i=0;i<r.length;++i)
            if(i!=(action<=17?0:2) && !(action>=42 && i==0)) CHECK(!r.data[i]);
        aux_output_complete(true);CHECK(aux_output_take(&r,input_generation(),500)&&r.release);
        for(unsigned i=0;i<r.length;++i) CHECK(!r.data[i]);
        aux_output_complete(true);CHECK(!aux_output_active());
    }
    return 0;
}

EXPORT int check_session_replays_host_mode(void)
{
    for(unsigned selected=TP_MOUSE_MODE;selected<=TP_PTP_MODE;++selected) {
        reset_all(); input_set_mode(selected);
        uint8_t mac[6]={1},packet[38];
        wire_surface_t s={WIRE_VERSION,0,7};
        for(unsigned change=0;change<3;++change) {
            if(change==1) ++s.session;
            if(change==2) s.rotation=2;
            wire_surface_encode(packet,WIRE_SURFACE,&s);
            uint32_t serial=requested_serial;
            receiver_ext_receive(mac,packet,change*10);usbhid_step();
            CHECK(input_mode()==selected && requested_serial==serial+1 && control_pending);
            wireless_control_step(change*10);
            CHECK(control_in_flight && flight_ack);
            mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);wireless_control_step(change*10+1);
            CHECK(control_in_flight && !flight_ack && !flight_settings && radio_byte==selected);
            mode_send_complete(NULL,ESP_NOW_SEND_SUCCESS);
            /* Do not let the next step launch a settings transaction in this case. */
            receiver_ext_usb_ready(false);wireless_control_step(change*10+2);
            CHECK(!control_pending && !control_in_flight && !lock_error);
            receiver_ext_usb_ready(true);
        }
    }
    return 0;
}

EXPORT int check_duplicate_surface_preserves_input(void)
{
    reset_all();input_set_mode(TP_PTP_MODE);
    uint8_t mac[6]={1},packet[38];wire_surface_t s={WIRE_VERSION,0,7};
    wire_surface_encode(packet,WIRE_SURFACE,&s);receiver_ext_receive(mac,packet,0);usbhid_step();
    CHECK(aux_output_steps(18,1,input_generation(),1));
    uint32_t generation=input_generation(),serial=requested_serial;
    unsigned queued=count;
    endpoint_ready=false;
    receiver_ext_receive(mac,packet,2);usbhid_step();
    CHECK(input_generation()==generation && requested_serial==serial && count==queued);
    CHECK(ack_pending && !pending_apply && !disconnect_count);
    return 0;
}

EXPORT int check_session_change_with_key_in_flight(void)
{
    for(unsigned success=0;success<2;++success) {
        reset_all();input_set_mode(TP_PTP_MODE);
        uint8_t mac[6]={1},packet[38];wire_surface_t s={WIRE_VERSION,0,7};
        wire_surface_encode(packet,WIRE_SURFACE,&s);receiver_ext_receive(mac,packet,0);usbhid_step();
        wire_action_t a={7,1,42,1};wire_action_encode(packet,&a);
        receiver_ext_receive(mac,packet,1);usbhid_step();
        CHECK(usb_aux_flight && usb_bytes[0]==1 && usb_bytes[2]==0x06);
        a.sequence=2;a.action=43;wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,2);
        s.session=8;wire_surface_encode(packet,WIRE_SURFACE,&s);
        receiver_ext_receive(mac,packet,3);usbhid_step();
        CHECK(usb_aux_flight && count==0);
        usb_complete(REPORT_MOUSE,success);usbhid_step();
        CHECK(usb_aux_flight && usb_id==8 && usb_bytes[0]==0 && usb_bytes[2]==0);
        usb_complete(REPORT_MOUSE,true);CHECK(!aux_output_active());
        wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,4);CHECK(!count);
        a.session=8;a.sequence=1;wire_action_encode(packet,&a);receiver_ext_receive(mac,packet,5);
        usbhid_step();CHECK(usb_aux_flight && usb_bytes[0]==1 && usb_bytes[2]==0x19);
        usb_complete(REPORT_MOUSE,true);usbhid_step();
        CHECK(usb_bytes[0]==0 && usb_bytes[2]==0);usb_complete(REPORT_MOUSE,true);
    }
    return 0;
}

EXPORT int check_wire_function_action_validation(void)
{
    uint8_t packet[38];wireless_msg_t out;
    wire_action_t a={7,1,13,1};wire_action_encode(packet,&a);
    CHECK(wireless_decode(packet,38,&out));
    CHECK(!wireless_decode(packet,37,&out));
    for(unsigned action=7;action<=12;++action) {
        a.action=action;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    }
    a.action=13;a.steps=-1;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    a.steps=1;a.hold=true;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    a.hold=false;a.action=48;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    a.action=49;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    a.action=255;wire_action_encode(packet,&a);CHECK(!wireless_decode(packet,38,&out));
    return 0;
}

EXPORT int check_aux_completion_and_failure(void)
{
    ready_for(REPORT_HAPTIC);CHECK(aux_output_steps(2,1,input_generation(),0));
    usbhid_step();CHECK(usb_aux_flight&&usb_id==7&&usb_bytes[0]==0xe9);
    usb_complete(REPORT_HAPTIC,true);CHECK(usb_aux_flight&&usb_busy);
    usb_complete(REPORT_MOUSE,false);CHECK(!usb_busy&&aux_output_release_pending());
    usbhid_step();CHECK(usb_aux_flight&&usb_id==7&&usb_bytes[0]==0);
    usb_complete(REPORT_MOUSE,true);CHECK(!aux_output_release_pending());return 0;
}
EXPORT int check_wire_hold_until_cancel(void)
{
    for(unsigned category=0;category<2;++category) {
        reset_all();input_set_mode(TP_PTP_MODE);
        uint8_t mac[6]={1},other[6]={2},packet[38];
        wire_surface_t s={WIRE_VERSION,0,7};wire_surface_encode(packet,WIRE_SURFACE,&s);
        receiver_ext_receive(mac,packet,0);usbhid_step();
        wire_action_t a={7,1,category?5:2,1,true};wire_action_encode(packet,&a);
        receiver_ext_receive(mac,packet,0);receiver_ext_receive(mac,packet,1);CHECK(count==1);
        aux_output_report_t out;
        CHECK(aux_output_take(&out,input_generation(),1)&&!out.release&&out.id==(category?8:7));
        aux_output_complete(true);
        CHECK(!aux_output_take(&out,input_generation(),1000)&&!aux_output_active());
        a=(wire_action_t){7,2,0,0,false};wire_action_encode(packet,&a);
        receiver_ext_receive(other,packet,1000);CHECK(!aux_output_release_pending());
        receiver_ext_receive(mac,packet,1000);
        CHECK(aux_output_take(&out,input_generation(),1000)&&out.release&&out.id==(category?8:7));
        aux_output_complete(true);CHECK(!aux_output_active());
    }
    return 0;
}
EXPORT int check_receiver_hold_recovery(void)
{
    for(unsigned reason=0;reason<3;++reason) {
        ready_for(REPORT_HAPTIC);
        CHECK(aux_output_hold(5,1,input_generation(),0));
        aux_output_report_t out;CHECK(aux_output_take(&out,input_generation(),0));aux_output_complete(true);
        if(reason==0)input_check_link(5001);
        if(reason==1)input_set_mode(TP_MOUSE_MODE);
        if(reason==2)input_recover();
        CHECK(aux_output_take(&out,input_generation(),5001)&&out.release&&out.id==8);
        aux_output_complete(true);CHECK(!aux_output_active());
    }
    return 0;
}
