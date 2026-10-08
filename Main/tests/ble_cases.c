static void complete_hook(void)
{
    if (ble_flight) ble_input_complete(active_conn, active_epoch, flight_handle, BLE_TX_OK);
}
static void run_sender(unsigned iterations)
{
    budget = iterations;
    ble_hid_task(NULL);
}
static void reset_test(void)
{
    now = 0; test_sends = 0; send_fails = synchronous_complete = false; wifi_hook = NULL;
    current_mode = BLE_MODE; current_tp_mode = MOUSE_MODE;
    reports = (report_buffer_t){0}; ready_mask = 0;
    mode_pending = false; mode_applied = true; fail_mode = false;
    input_pipeline_init();
    ble_input_connection(true, 1);
}
static void connect_ready(void)
{
    reset_test();
    ble_input_subscription(1, true);
    ble_input_aux_subscription(1, AUX_OUTPUT_MOUSE);
    input_source_observe(input_source_generation(), true);
    input_observe(input_generation(), true);
    wifi_hook = complete_hook; run_sender(2); wifi_hook = NULL;
    test_sends = 0;
}
static bool publish_mouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan, bool tap)
{
    input_report_t r = {.mode = MOUSE_MODE, .time_ms = now,
        .data.mouse = {buttons, x, y, wheel, pan}};
    return input_publish(input_generation(), &r, tap);
}
EXPORT int check_ble_mouse_descriptor(void)
{
    unsigned size = 0, count = 0, id = 0, mouse_bits = 0, strength_bits = 0, consumer_bits = 0, keyboard_bits = 0;
    unsigned page = 0, usage = 0, mouse_collections = 0;
    for (unsigned i = 0; i < ble_mouse_hid_report_len;) {
        uint8_t tag = ble_mouse_hid_report_descriptor[i++];
        unsigned length = tag & 3U, value = 0;
        if (length == 3) length = 4;
        CHECK(tag != 0xfe && i + length <= ble_mouse_hid_report_len);
        for (unsigned j = 0; j < length; ++j) value |= (unsigned)ble_mouse_hid_report_descriptor[i++] << (j * 8);
        switch (tag & 0xfc) {
        case 0x04: page = value; CHECK(page != 0x0d); break;
        case 0x08: usage = value; break;
        case 0x74: size = value; break;
        case 0x94: count = value; break;
        case 0x84: id = value; break;
        case 0xa0: if (value == 1 && page == 1 && usage == 2) ++mouse_collections; break;
        case 0x80:
            if(id==1)mouse_bits+=size*count;
            else if(id==7)consumer_bits+=size*count;
            else if(id==8)keyboard_bits+=size*count;
            else CHECK(false);
            break;
        case 0xb0: CHECK(id == REPORTID_HAPTIC_INTENSITY); strength_bits += size * count; break;
        case 0x90: CHECK(false); break;
        }
    }
    CHECK(mouse_collections == 1 && mouse_bits == 40 && strength_bits == 8);
    CHECK(consumer_bits==16&&keyboard_bits==64);
    return 0;
}
EXPORT int check_ble_subscription_default_mtu(void)
{
    reset_test(); run_sender(2); CHECK(!test_sends && !ready_mask);
    ble_input_subscription(2, true); run_sender(2); CHECK(!test_sends && !ready_mask);
    /* No MTU exchange or auxiliary subscriptions: the initial release fits MTU 23. */
    ble_input_subscription(1, true); CHECK(ready_mask == (1U << MOUSE_MODE));
    wifi_hook = complete_hook; run_sender(2); wifi_hook = NULL;
    CHECK(test_sends == 1 && sent[0].length == 5 && !reports.release_mask);
    ble_input_subscription(1, false); run_sender(2); CHECK(!ready_mask && test_sends == 1);
    return 0;
}
EXPORT int check_ble_mouse_payload_and_tap_release(void)
{
    connect_ready(); CHECK(input_output_ready(input_generation()));
    CHECK(publish_mouse(1, -127, 127, -3, 4, true));
    wifi_hook = complete_hook; run_sender(4); wifi_hook = NULL;
    const uint8_t down[] = {1, 0x81, 0x7f, 0xfd, 4}, up[5] = {0};
    CHECK(test_sends == 2 && !ble_flight);
    for (unsigned i = 0; i < 2; ++i)
        CHECK(sent[i].conn == 1 && sent[i].id == 1 && sent[i].type == 1 && sent[i].length == 5);
    CHECK(!memcmp(sent[0].data, down, 5) && !memcmp(sent[1].data, up, 5));
    CHECK(!reports.count); return 0;
}
EXPORT int check_ble_congestion(void)
{
    connect_ready(); CHECK(publish_mouse(0, 12, -9, 0, 0, false));
    ble_input_congestion(1, true); run_sender(3); CHECK(!test_sends);
    ble_input_congestion(2, false); run_sender(2); CHECK(!test_sends);
    ble_input_congestion(1, false);
    wifi_hook = complete_hook; run_sender(3); wifi_hook = NULL;
    CHECK(test_sends == 1 && sent[0].data[1] == 12 && sent[0].data[2] == (uint8_t)-9);
    return 0;
}
static unsigned ticks;
static void retry_hook(void)
{
    if (++ticks == 1) send_fails = false;
    else complete_hook();
}
EXPORT int check_ble_submit_failure_retries_same_report(void)
{
    connect_ready(); CHECK(publish_mouse(1, 7, -8, 2, -1, true));
    send_fails = true; ticks = 0; wifi_hook = retry_hook; run_sender(5); wifi_hook = NULL;
    CHECK(test_sends == 3 && reports.stats.submit_failures == 1);
    CHECK(!memcmp(sent[0].data, sent[1].data, 5));
    CHECK(sent[0].data[0] == 1 && !sent[2].data[0] && !reports.count && !ble_flight);
    return 0;
}
static void failed_completion_hook(void)
{
    if (ble_flight) ble_input_complete(active_conn, active_epoch, flight_handle, test_sends != 1 ? BLE_TX_OK : BLE_TX_FAILED);
}
EXPORT int check_ble_failed_completion_recovers_release(void)
{
    connect_ready(); uint32_t generation = input_generation();
    CHECK(publish_mouse(1, 5, 0, 0, 0, false));
    wifi_hook = failed_completion_hook; run_sender(4); wifi_hook = NULL;
    CHECK(input_generation() != generation && reports.stats.submit_failures == 1);
    CHECK(test_sends == 2 && sent[0].data[0] == 1 && !sent[1].data[0]);
    CHECK(!reports.release_mask && !ble_flight); return 0;
}
static bool ignored_events, delayed_release;
static void delayed_completion_hook(void)
{
    ++ticks;
    if (ticks == 1) {
        ble_input_complete(2, active_epoch, flight_handle, BLE_TX_OK);
        ble_input_complete(1, active_epoch, flight_handle + 1, BLE_TX_OK);
        ble_input_subscription(2, false);
        ble_input_congestion(2, true);
        ble_input_connection(false, 2);
        ignored_events = !ble_done && ready_mask && !congested && ble_flight;
    }
    if (ticks == 2) delayed_release = test_sends == 1 && reports.count == 1;
    if (ticks >= 3) complete_hook();
}
EXPORT int check_ble_tap_waits_for_matching_completion(void)
{
    connect_ready(); CHECK(publish_mouse(1, 0, 0, 0, 0, true));
    ignored_events = delayed_release = false; ticks = 0;
    wifi_hook = delayed_completion_hook; run_sender(6); wifi_hook = NULL;
    CHECK(ignored_events && delayed_release);
    CHECK(test_sends == 2 && !sent[1].data[0] && !ble_flight); return 0;
}
static bool reconnected;
static void reconnect_hook(void)
{
    if (++ticks == 1) {
        ble_input_connection(false, 1);
        reconnected = !ble_flight && !ready_mask;
        ble_input_connection(true, 2); ble_input_subscription(2, true);
        ble_input_complete(1, active_epoch - 1, hid_dev_report_handle(1), BLE_TX_OK);
        ble_input_connection(false, 1); ble_input_subscription(1, false);
        ble_input_congestion(1, true);
        send_fails = false;
    } else complete_hook();
}
EXPORT int check_ble_reconnect_drops_previous_pending_report(void)
{
    for (unsigned fail = 0; fail < 2; ++fail) {
        connect_ready(); CHECK(publish_mouse(1, 14, 0, 0, 0, true));
        send_fails = fail; ticks = 0; reconnected = false;
        wifi_hook = reconnect_hook;
        /* Failed submissions and in-flight reports must both be discarded. */
        run_sender(5); wifi_hook = NULL;
        CHECK(reconnected && test_sends == 2 && !ble_flight && !reports.count);
        CHECK(sent[1].conn == 2 && !sent[1].data[0] && !sent[1].data[1]);
    }
    return 0;
}
EXPORT int check_ble_stale_queued_mouse_recovers(void)
{
    connect_ready(); CHECK(publish_mouse(1, 20, 0, 0, 0, true));
    now += REPORT_MAX_AGE_MS + 1;
    wifi_hook = complete_hook; run_sender(3); wifi_hook = NULL;
    CHECK(test_sends == 1 && !sent[0].data[0] && !sent[0].data[1]);
    CHECK(!reports.count && !reports.release_mask); return 0;
}

EXPORT int check_ble_synchronous_completion(void)
{
    connect_ready(); CHECK(publish_mouse(1, 9, -3, 1, 2, true));
    synchronous_complete = true; run_sender(5); synchronous_complete = false;
    CHECK(test_sends == 2 && !reports.count && !ble_flight);
    CHECK(sent[0].data[0] == 1 && sent[1].data[0] == 0);
    return 0;
}
static void resource_retry_hook(void)
{
    if (!ble_flight) return;
    ble_input_complete(active_conn, active_epoch, flight_handle, test_sends == 1 ? BLE_TX_RETRY : BLE_TX_OK);
    /* A duplicate event cannot overwrite the first completion. */
    ble_input_complete(active_conn, active_epoch, flight_handle, BLE_TX_FAILED);
}
EXPORT int check_ble_host_resource_retry_preserves_click(void)
{
    connect_ready(); uint32_t generation = input_generation();
    CHECK(publish_mouse(1, 7, -8, 2, -1, true));
    wifi_hook = resource_retry_hook; run_sender(6); wifi_hook = NULL;
    CHECK(input_generation() == generation && test_sends == 3 && reports.stats.submit_failures == 1);
    CHECK(!memcmp(sent[0].data, sent[1].data, 5) && !sent[2].data[0]);
    CHECK(!reports.count && !ble_flight); return 0;
}
static void reused_handle_hook(void)
{
    if (++ticks == 1) {
        uint32_t old_epoch = active_epoch;
        ble_input_connection(false, 1); ble_input_connection(true, 1);
        ble_input_subscription(1, true);
        ble_input_complete(1, old_epoch, flight_handle, BLE_TX_OK);
    } else if (ticks == 2) {
        ble_input_complete(1, active_epoch - 2, flight_handle, BLE_TX_OK);
        ignored_events = !ble_done && ble_flight;
    } else complete_hook();
}
EXPORT int check_ble_reused_handle_ignores_previous_completion(void)
{
    connect_ready(); CHECK(publish_mouse(1, 14, 0, 0, 0, true));
    ticks = 0; ignored_events = false; wifi_hook = reused_handle_hook;
    run_sender(6); wifi_hook = NULL;
    CHECK(ignored_events && test_sends == 2 && !ble_flight && !reports.count);
    CHECK(sent[1].conn == 1 && !sent[1].data[0] && !sent[1].data[1]);return 0;
}

static void all_aux_ready(void)
{
    connect_ready(); ble_input_aux_subscription(1,AUX_OUTPUT_ALL);
    synchronous_complete=true;run_sender(6);test_sends=0;
}
EXPORT int check_ble_aux_reports_and_releases(void)
{
    const uint8_t actions[]={1,2,3,4,5,6,13,14,15,16,17,18,30,42};
    const uint8_t ids[]={7,7,1,1,8,8,7,7,7,7,7,8,8,8};
    const uint8_t values[]={0x6f,0xe9,1,1,0x52,0x4f,0xe2,0xcd,0xb6,0xb5,0xb7,0x29,0x3a,0x06};
    for(unsigned i=0;i<sizeof(actions);++i) {
        all_aux_ready();
        CHECK(aux_output_once(actions[i],1,input_generation(),now));run_sender(8);
        CHECK(test_sends==(ids[i]==1?1:2)&&sent[0].id==ids[i]);
        unsigned offset=ids[i]==7?0:ids[i]==8?2:actions[i]==3?3:4;
        CHECK(sent[0].data[offset]==values[i]);
        CHECK(sent[0].length==(ids[i]==7?2:ids[i]==8?8:5));
        if(actions[i]==42)CHECK(sent[0].data[0]==1);
        if(ids[i]!=1) { const uint8_t zero[8]={0};CHECK(sent[1].id==ids[i]&&!memcmp(sent[1].data,zero,sent[1].length)); }
    }
    return 0;
}
EXPORT int check_ble_mouse_only_drops_unsubscribed_actions(void)
{
    connect_ready();synchronous_complete=true;
    CHECK(aux_output_once(14,1,input_generation(),now));
    CHECK(aux_output_once(42,1,input_generation(),now));
    CHECK(aux_output_steps(3,-3,input_generation(),now));
    CHECK(aux_output_steps(4,4,input_generation(),now));run_sender(8);
    CHECK(test_sends==2&&sent[0].id==1&&sent[0].data[3]==(uint8_t)-3&&sent[1].data[4]==4);
    test_sends=0;ble_input_aux_subscription(1,AUX_OUTPUT_ALL);run_sender(8);
    const uint8_t zero[8]={0};CHECK(test_sends==2);
    CHECK(sent[0].id==7&&sent[1].id==8&&!memcmp(sent[0].data,zero,2)&&!memcmp(sent[1].data,zero,8));
    return 0;
}
EXPORT int check_ble_aux_independent_subscription_restore(void)
{
    connect_ready();ble_input_aux_subscription(1,AUX_OUTPUT_MOUSE|AUX_OUTPUT_KEYBOARD);
    synchronous_complete=true;run_sender(6);CHECK(test_sends==1&&sent[0].id==8);test_sends=0;
    CHECK(aux_output_hold(5,1,input_generation(),now));run_sender(5);
    CHECK(test_sends==1&&sent[0].data[2]==0x52);
    ble_input_aux_subscription(1,AUX_OUTPUT_MOUSE);
    CHECK(aux_output_once(42,1,input_generation(),now));run_sender(4);CHECK(test_sends==1);
    ble_input_aux_subscription(1,AUX_OUTPUT_MOUSE|AUX_OUTPUT_KEYBOARD);
    CHECK(aux_output_once(30,1,input_generation(),now));run_sender(8);
    CHECK(test_sends==4&&sent[1].id==8&&!sent[1].data[2]&&sent[2].data[2]==0x3a&&!sent[3].data[2]);
    return 0;
}
EXPORT int check_ble_aux_retry_and_fairness(void)
{
    all_aux_ready();synchronous_complete=false;
    CHECK(aux_output_once(14,1,input_generation(),now));
    CHECK(publish_mouse(0,9,0,0,0,false));
    wifi_hook=resource_retry_hook;run_sender(9);wifi_hook=NULL;
    CHECK(test_sends==4&&sent[0].id==7&&sent[1].id==7&&!memcmp(sent[0].data,sent[1].data,2));
    CHECK(sent[2].id==7&&!sent[2].data[0]&&sent[3].id==1&&sent[3].data[1]==9);
    CHECK(!ble_flight&&!reports.count);return 0;
}
static void aux_cancel_retry_hook(void)
{
    if(++ticks==1) { aux_output_cancel();ble_input_complete(1,active_epoch,flight_handle,BLE_TX_RETRY); }
    else complete_hook();
}
EXPORT int check_ble_aux_cancel_resource_retry(void)
{
    all_aux_ready();synchronous_complete=false;ticks=0;
    CHECK(aux_output_steps(1,3,input_generation(),now));
    wifi_hook=aux_cancel_retry_hook;run_sender(8);wifi_hook=NULL;
    CHECK(test_sends==1&&!ble_flight&&!count);return 0;
}
static void aux_security_hook(void)
{
    if(++ticks==1) {
        ble_input_subscription(1,false);ble_input_aux_subscription(1,0);
        ble_input_complete(1,active_epoch,flight_handle,BLE_TX_RETRY);
    } else if(ticks==3) {
        ble_input_subscription(1,true);ble_input_aux_subscription(1,AUX_OUTPUT_ALL);
    } else complete_hook();
}
EXPORT int check_ble_aux_security_loss_drops_pending(void)
{
    all_aux_ready();synchronous_complete=false;ticks=0;
    CHECK(aux_output_once(42,1,input_generation(),now));
    wifi_hook=aux_security_hook;run_sender(12);wifi_hook=NULL;
    CHECK(test_sends==4&&sent[0].id==8&&sent[0].data[2]==6);
    for(unsigned i=1;i<test_sends;++i) {const uint8_t zero[8]={0};CHECK(!memcmp(sent[i].data,zero,sent[i].length));}
    CHECK(!ble_flight);return 0;
}
EXPORT int check_ble_aux_reused_handle(void)
{
    all_aux_ready();synchronous_complete=false;ticks=0;ignored_events=false;
    CHECK(aux_output_once(14,1,input_generation(),now));
    wifi_hook=reused_handle_hook;run_sender(8);wifi_hook=NULL;
    CHECK(ignored_events&&test_sends==2&&sent[0].id==7&&sent[1].id==1&&!sent[1].data[0]);
    CHECK(!ble_flight&&!count);return 0;
}
static void busy_pointer_hook(void)
{ publish_mouse(0,1,0,0,0,false); }
EXPORT int check_ble_aux_and_mouse_alternate_under_load(void)
{
    all_aux_ready();CHECK(aux_output_steps(3,1000,input_generation(),now));
    CHECK(publish_mouse(0,1,0,0,0,false));wifi_hook=busy_pointer_hook;run_sender(12);wifi_hook=NULL;
    CHECK(test_sends==12);
    for(unsigned i=0;i<12;++i) {
        CHECK(sent[i].id==1);
        CHECK(i%2?sent[i].data[1]>0:sent[i].data[3]==127);
    }
    return 0;
}
