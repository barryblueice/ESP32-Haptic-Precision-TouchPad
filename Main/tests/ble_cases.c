static void connect_ready(void)
{
    step_hook=0;test_sends=test_acks=test_failures=0;send_fails=false;
    ble_input_connection(true,1);ble_input_subscription(1,true);ble_input_mtu(1,64);
    for(unsigned i=0;i<3;++i)ble_input_aux_subscription(1,i,true);
    aux_output_reset(false);
}
EXPORT int check_ble_subscription_and_mtu(void)
{
    ble_input_connection(true,1);ble_input_subscription(1,true);CHECK(!test_link);
    ble_input_mtu(1,36);for(unsigned i=0;i<3;++i)ble_input_aux_subscription(1,i,true);CHECK(!test_link);
    ble_input_mtu(1,37);CHECK(test_link==(1U<<PTP_MODE));
    ble_input_aux_subscription(1,1,false);CHECK(!test_link);ble_input_aux_subscription(1,1,true);CHECK(test_link);
    ble_input_connection(false,2);CHECK(test_link);ble_input_connection(false,1);CHECK(!test_link);return 0;
}
static void complete_hook(void)
{
    if(ble_flight)ble_input_complete(sent_conn,hid_dev_report_handle(sent_id),true);
}
EXPORT int check_ble_async_action_release(void)
{
    connect_ready();CHECK(aux_output_steps(5,1,generation,now));
    step_hook=complete_hook;budget=4;ble_hid_task(0);step_hook=0;
    CHECK(test_sends==2&&sent_id==8&&!sent_bytes[2]);CHECK(!aux_output_active());return 0;
}
EXPORT int check_ble_congestion_and_submit_failure(void)
{
    connect_ready();CHECK(aux_output_steps(2,1,generation,now));ble_input_congestion(1,true);
    budget=2;ble_hid_task(0);CHECK(!test_sends);
    ble_input_congestion(1,false);send_fails=true;budget=1;ble_hid_task(0);CHECK(test_sends==1&&!ble_flight);
    send_fails=false;step_hook=complete_hook;budget=4;ble_hid_task(0);step_hook=0;
    CHECK(test_sends==3&&sent_id==7&&!sent_bytes[0]&&!aux_output_active());return 0;
}
static unsigned completion_number;
static void fail_then_release(void)
{
    if(ble_flight)ble_input_complete(sent_conn,hid_dev_report_handle(sent_id),completion_number++!=0);
}
EXPORT int check_ble_failed_completion_keeps_release(void)
{
    connect_ready();CHECK(aux_output_steps(2,1,generation,now));completion_number=0;
    step_hook=fail_then_release;budget=4;ble_hid_task(0);step_hook=0;
    CHECK(test_sends==2&&test_failures==1&&sent_id==7&&!sent_bytes[0]&&!aux_output_active());
    ble_input_connection(false,1);CHECK(!ble_flight&&!test_link&&!aux_output_active());return 0;
}
