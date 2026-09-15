EXPORT int check_surface_ack_and_rotation(void)
{
    reset_all(); uint8_t mac[6]={1,2,3,4,5,6}, packet[38], reply[38], dest[6];
    wire_surface_t s={1,1,123}; wire_surface_encode(packet,WIRE_SURFACE,&s);
    receiver_ext_receive(mac,packet,0);CHECK(!receiver_ext_accept(mac));CHECK(!receiver_ext_ack(reply,dest));
    usbhid_step();CHECK(disconnect_count==1&&descriptor_rotation==1);CHECK(receiver_ext_accept(mac));
    CHECK(receiver_ext_ack(reply,dest)&&!memcmp(mac,dest,6));wire_surface_t ack;
    CHECK(wire_surface_decode(reply,38,WIRE_SURFACE_ACK,&ack)&&ack.session==123&&ack.rotation==1);
    receiver_ext_ack_complete(true);CHECK(!receiver_ext_ack(reply,dest));
    receiver_ext_receive(mac,packet,200);usbhid_step();CHECK(disconnect_count==1);
    CHECK(receiver_ext_ack(reply,dest));return 0;
}
EXPORT int check_wire_actions_dedup_and_release(void)
{
    reset_all(); input_set_mode(TP_PTP_MODE);
    uint8_t mac[6]={1},packet[38];wire_surface_t s={1,0,7};wire_surface_encode(packet,WIRE_SURFACE,&s);
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
EXPORT int check_aux_completion_and_failure(void)
{
    ready_for(REPORT_HAPTIC);CHECK(aux_output_steps(2,1,input_generation(),0));
    usbhid_step();CHECK(usb_aux_flight&&usb_id==7&&usb_bytes[0]==0xe9);
    usb_complete(REPORT_HAPTIC,true);CHECK(usb_aux_flight&&usb_busy);
    usb_complete(REPORT_MOUSE,false);CHECK(!usb_busy&&aux_output_release_pending());
    usbhid_step();CHECK(usb_aux_flight&&usb_id==7&&usb_bytes[0]==0);
    usb_complete(REPORT_MOUSE,true);CHECK(!aux_output_release_pending());return 0;
}
