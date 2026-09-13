static void reset_test(uint8_t mode)
{
    reports = (report_buffer_t){0}; current_tp_mode = mode; current_mode = WIRED_MODE;
    parser = sender = NULL; ready_mask = 0; mode_pending = mode_applied = false;
    request_serial = 0; raw_count = 0; fail_alloc = false; check_error = 0;
    mode_error = mode_writes = cancellations = notifications = delay_count = 0;
    clock_ms = 0; wait_hook = NULL; mode_hook = NULL;
    attempts = sent_count = submit_errors = 0; test_steps = 0;
    usb_ready = mounted = true; usb_busy[1] = usb_busy[2] = false;
    send_done = false; connected = subscribed = congested = false;
    memset(sent, 0, sizeof(sent)); memset(wifi_packets, 0, sizeof(wifi_packets));
    input_pipeline_init(); input_register_parser(); input_register_sender();
}
static void activate(uint8_t mode)
{
    reset_test(mode); input_set_link(1U << mode);
    input_report_t r;
    while (input_take_report(&r)) input_report_ack(&r);
    input_observe(input_generation(), true);
}
static bool mouse(int buttons, int x, bool tap)
{
    input_report_t r = {.mode = MOUSE_MODE, .time_ms = clock_ms};
    r.data.mouse.buttons = buttons; r.data.mouse.x = x;
    return input_publish(input_generation(), &r, tap);
}
static bool ptp(int mask, int x)
{
    input_report_t r = {.mode = PTP_MODE, .time_ms = clock_ms};
    for (int i = 0; i < 5; ++i) {
        r.data.ptp.fingers[i].tip_conf_id = (i << 2) | 1 | ((mask & (1 << i)) ? 2 : 0);
        r.data.ptp.fingers[i].x = x;
    }
    return input_publish(input_generation(), &r, false);
}
EXPORT int test_relative_movement_accumulates(void)
{
    activate(MOUSE_MODE);
    for (int i = 0; i < 10; ++i) CHECK(mouse(0, 100, false));
    CHECK(reports.count == 1 && reports.stats.merged == 9);
    input_report_t r; int sum = 0, count = 0;
    while (input_take_report(&r)) { sum += r.data.mouse.x; ++count; input_report_ack(&r); }
    CHECK(sum == 1000 && count == 8); return 0;
}
EXPORT int test_click_edges_and_drag_order(void)
{
    activate(MOUSE_MODE); CHECK(mouse(1, 0, false));
    CHECK(mouse(1, 10, false)); CHECK(mouse(1, 20, false)); CHECK(mouse(0, 0, false));
    input_report_t r;
    CHECK(input_take_report(&r) && r.data.mouse.buttons == 1 && r.data.mouse.x == 0);
    CHECK(input_take_report(&r) && r.data.mouse.buttons == 1 && r.data.mouse.x == 30);
    CHECK(input_take_report(&r) && r.data.mouse.buttons == 0);
    CHECK(!input_take_report(&r)); return 0;
}
EXPORT int test_ptp_contact_edges_preserved(void)
{
    activate(PTP_MODE); CHECK(ptp(1, 10)); CHECK(ptp(1, 20)); CHECK(ptp(1, 30));
    CHECK(ptp(3, 40)); CHECK(ptp(2, 50)); CHECK(ptp(0, 60));
    input_report_t r;
    CHECK(input_take_report(&r) && r.data.ptp.fingers[0].x == 10);
    CHECK(input_take_report(&r) && r.data.ptp.fingers[0].x == 30);
    CHECK(input_take_report(&r) && (r.data.ptp.fingers[1].tip_conf_id & 2));
    CHECK(input_take_report(&r) && !(r.data.ptp.fingers[0].tip_conf_id & 2));
    CHECK(input_take_report(&r) && !(r.data.ptp.fingers[1].tip_conf_id & 2)); return 0;
}
EXPORT int test_tap_atomic_capacity_failure(void)
{
    activate(MOUSE_MODE);
    for (int i = 0; i < 31; ++i) CHECK(mouse((i & 1) ? 0 : 1, 0, false));
    uint32_t gen = input_generation(); CHECK(!mouse(2, 0, true));
    CHECK(input_generation() != gen && reports.count == 0 && reports.recovering);
    return 0;
}
EXPORT int test_tap_pair(void)
{
    activate(MOUSE_MODE); CHECK(mouse(2, 0, true)); input_report_t r;
    CHECK(input_take_report(&r) && r.data.mouse.buttons == 2);
    CHECK(input_take_report(&r) && r.data.mouse.buttons == 0);
    CHECK(!input_take_report(&r)); return 0;
}
EXPORT int test_overflow_waits_for_lift_and_release_ack(void)
{
    activate(MOUSE_MODE);
    for (int i = 0; i < 32; ++i) CHECK(mouse((i & 1) ? 0 : 1, 0, false));
    CHECK(!mouse(1, 0, false)); CHECK(reports.stats.peak == 32);
    CHECK(!input_observe(input_generation(), false)); input_report_t r;
    CHECK(input_take_report(&r) && r.release); input_report_ack(&r);
    CHECK(!input_observe(input_generation(), false));
    CHECK(input_observe(input_generation(), true)); CHECK(mouse(1, 0, false)); return 0;
}
EXPORT int test_expiry_not_extended_by_new_motion(void)
{
    activate(MOUSE_MODE); CHECK(mouse(0, 1, false)); clock_ms = 99; CHECK(mouse(0, 2, false));
    clock_ms = 101; input_report_t r;
    CHECK(input_take_report(&r) && r.release && reports.count == 0);
    CHECK(reports.stats.longest_wait_ms == 101); return 0;
}
EXPORT int test_clock_wrap_and_100ms_boundary(void)
{
    activate(MOUSE_MODE); clock_ms = UINT32_MAX - 49; CHECK(mouse(0, 1, false));
    clock_ms = 50; input_report_t r; CHECK(input_take_report(&r) && !r.release);
    clock_ms = 51; CHECK(!input_report_current(&r)); return 0;
}
EXPORT int test_late_ack_cannot_release_new_session(void)
{
    reset_test(MOUSE_MODE); input_set_link(1); input_report_t old, next;
    CHECK(input_take_report(&old)); input_recover();
    input_report_ack(&old); CHECK(reports.release_mask == 1);
    CHECK(input_take_report(&next) && next.generation != old.generation); return 0;
}
EXPORT int test_raw_fifo_overflow_and_old_generation(void)
{
    activate(MOUSE_MODE); uint8_t bytes[64] = {0}; uint32_t gen = input_generation();
    for (unsigned i = 0; i < 17; ++i) input_capture(bytes, true, gen, i);
    CHECK(reports.stats.raw_overflows == 1); input_frame_t frame;
    CHECK(!input_next_frame(&frame) && raw_count == 0);
    input_capture(bytes, true, gen, 18); CHECK(!input_next_frame(&frame));
    input_capture(bytes, true, input_generation(), 19); CHECK(input_next_frame(&frame));
    CHECK(frame.time_ms == 19); return 0;
}
EXPORT int test_read_failure_recovers(void)
{
    activate(MOUSE_MODE); uint32_t gen = input_generation();
    input_capture(NULL, false, gen, 0);
    CHECK(input_generation() != gen && reports.stats.read_failures == 1); return 0;
}
EXPORT int test_queue_allocation_failure(void)
{
    reset_test(MOUSE_MODE); fail_alloc = true; input_pipeline_init();
    CHECK(check_error == ESP_ERR_NO_MEM && tp_data_queue == NULL); return 0;
}
EXPORT int test_modes_only_commit_on_success_and_retry(void)
{
    activate(MOUSE_MODE); current_mode = _2_4_MODE; mode_error = ESP_FAIL;
    input_request_mode(PTP_MODE); CHECK(input_mode() == MOUSE_MODE);
    CHECK(input_apply_mode_request()); CHECK(input_mode() == MOUSE_MODE && !mode_pending);
    mode_error = ESP_OK; input_request_mode(PTP_MODE); CHECK(input_apply_mode_request());
    CHECK(input_mode() == PTP_MODE && current_mode == _2_4_MODE && mode_writes == 2);
    input_request_mode(PTP_MODE); CHECK(!input_apply_mode_request()); return 0;
}
static void newer_mode(void) { mode_hook = NULL; input_request_mode(MOUSE_MODE); }
EXPORT int test_latest_mode_request_survives_io(void)
{
    activate(MOUSE_MODE); input_request_mode(PTP_MODE); mode_hook = newer_mode;
    CHECK(input_apply_mode_request() && mode_pending); CHECK(input_apply_mode_request());
    CHECK(input_mode() == MOUSE_MODE && !mode_pending); return 0;
}
EXPORT int test_wireless_callback_no_io_or_connection_mutation(void)
{
    activate(MOUSE_MODE); current_mode = _2_4_MODE; uint8_t cmd = 99;
    wifi_now_recv_cb(NULL, &cmd, 1); CHECK(!mode_pending);
    cmd = PTP_MODE; wifi_now_recv_cb(NULL, &cmd, 1);
    CHECK(mode_pending && mode_writes == 0 && current_mode == _2_4_MODE); return 0;
}
EXPORT int test_usb_get_report_bounds(void)
{
    uint8_t buffer[258];
    const uint8_t ids[] = {REPORTID_FEATURE, REPORTID_MAX_COUNT, REPORTID_PTPHQA,
        REPORTID_BUTTON_PRESS_THRESHOLD, REPORTID_HAPTIC_INTENSITY};
    for (unsigned id = 0; id < sizeof(ids); ++id) {
        CHECK(tud_hid_get_report_cb(0, ids[id], HID_REPORT_TYPE_FEATURE, NULL, 256) == 0);
        for (unsigned len = 0; len <= 256; ++len) {
            memset(buffer, 0xA5, sizeof(buffer));
            unsigned used = tud_hid_get_report_cb(0, ids[id], HID_REPORT_TYPE_FEATURE, buffer + 1, len);
            CHECK(used <= len && buffer[0] == 0xA5 && buffer[len + 1] == 0xA5);
        }
    }
    return 0;
}
static int hook_tick;
static void usb_hook(void)
{
    ++hook_tick;
    if (usb_busy[1]) usb_complete(1, true);
    if (usb_busy[2]) usb_complete(2, true);
    if (hook_tick == 1) { input_observe(input_generation(), true); mouse(1, 0, true); }
}
EXPORT int test_usb_busy_preserves_tap(void)
{
    reset_test(MOUSE_MODE); input_set_link(1); usb_ready = false;
    hook_tick = 0; wait_hook = usb_hook; test_steps = 2; usbhid_task(NULL);
    CHECK(sent_count == 0);
    usb_ready = true; hook_tick = 0; test_steps = 6; usbhid_task(NULL);
    CHECK(sent_count == 3 && sent[0].data.mouse.buttons == 0 && sent[1].data.mouse.buttons == 1 && sent[2].data.mouse.buttons == 0);
    return 0;
}
EXPORT int test_usb_submission_failure_retries_after_tick(void)
{
    reset_test(MOUSE_MODE); input_set_link(1); submit_errors = 1;
    hook_tick = -1; wait_hook = usb_hook; test_steps = 8; usbhid_task(NULL);
    CHECK(attempts == 4 && sent_count == 3 && delay_count >= 1); return 0;
}
static void usb_old_hook(void)
{
    ++hook_tick;
    if (hook_tick == 1) input_recover();
    if (usb_busy[2]) usb_complete(2, true);
}
EXPORT int test_usb_old_completion_requires_new_neutral(void)
{
    reset_test(MOUSE_MODE); input_set_link(1); hook_tick = 0;
    wait_hook = usb_old_hook; test_steps = 5; usbhid_task(NULL);
    CHECK(sent_count == 2 && reports.release_mask == 0); return 0;
}
static void ble_hook(void)
{
    ++hook_tick;
    if (hook_tick == 2) ble_input_connection(true, 7);
    if (hook_tick == 3) ble_input_subscription(7, true);
    if (hook_tick == 4) { input_observe(input_generation(), true); mouse(2, 0, true); }
}
EXPORT int test_ble_disconnected_input_then_subscribe(void)
{
    reset_test(MOUSE_MODE); hook_tick = 0; wait_hook = ble_hook; test_steps = 12;
    ble_hid_task(NULL);
    CHECK(sent_count == 3 && sent[1].data.mouse.buttons == 2 && sent[2].data.mouse.buttons == 0);
    return 0;
}
static void ble_congestion_hook(void)
{
    ++hook_tick;
    if (hook_tick == 2) ble_input_congestion(7, false);
}
EXPORT int test_ble_congestion_and_sync_failure(void)
{
    activate(MOUSE_MODE); ble_input_connection(true, 7); ble_input_subscription(7, true);
    ble_input_congestion(7, true); hook_tick = 0; wait_hook = ble_congestion_hook;
    submit_errors = 1; test_steps = 6; ble_hid_task(NULL);
    CHECK(sent_count == 1 && attempts == 2 && delay_count == 1); return 0;
}
EXPORT int test_ble_wrong_connection_events_ignored(void)
{
    reset_test(MOUSE_MODE); ble_input_connection(true, 7); ble_input_subscription(8, true);
    CHECK(!subscribed && ready_mask == 0); ble_input_subscription(7, true);
    ble_input_congestion(8, true); CHECK(!congested && ready_mask == 1); return 0;
}
static void wifi_hook(void)
{
    ++hook_tick;
    send_callback(NULL, ESP_NOW_SEND_SUCCESS);
    if (hook_tick == 2) { input_observe(input_generation(), true); mouse(1, 0, true); }
}
EXPORT int test_wireless_serial_mouse_pair(void)
{
    reset_test(MOUSE_MODE); input_set_link(1); hook_tick = 0; wait_hook = wifi_hook;
    test_steps = 8; wifi_send_task(NULL);
    CHECK(sent_count == 3 && wifi_packets[1].type == MOUSE_MODE);
    CHECK(wifi_packets[1].payload.mouse.buttons == 1 && wifi_packets[2].payload.mouse.buttons == 0);
    return 0;
}
static void wifi_fail_hook(void)
{
    ++hook_tick; send_callback(NULL, hook_tick == 1 ? ESP_NOW_SEND_FAIL : ESP_NOW_SEND_SUCCESS);
}
EXPORT int test_wireless_failure_recovers_without_replaying_delta(void)
{
    activate(MOUSE_MODE); CHECK(mouse(0, 42, false)); hook_tick = 0;
    wait_hook = wifi_fail_hook; test_steps = 5; wifi_send_task(NULL);
    CHECK(sent_count == 2 && wifi_packets[0].payload.mouse.x == 42 && wifi_packets[1].payload.mouse.x == 0);
    return 0;
}
EXPORT int test_wireless_sync_failure_keeps_report(void)
{
    activate(MOUSE_MODE); CHECK(mouse(0, 42, false)); hook_tick = 0;
    wait_hook = wifi_fail_hook; submit_errors = 1; test_steps = 5; wifi_send_task(NULL);
    CHECK(wifi_packets[0].payload.mouse.x == 42 && delay_count >= 1); return 0;
}
EXPORT int test_heartbeat_zero_initialization(void)
{
    wireless_msg_t p; memset(&p, 0xA5, sizeof(p)); clock_ms = 1234000;
    wireless_make_heartbeat(&p); CHECK(p.type == ALIVE_MODE && p.payload.alive.uptime == 1234);
    CHECK(p.payload.alive.battery_level == 75 && p.payload.alive.vbus_level == 0);
    const uint8_t *bytes = (const uint8_t *)&p.payload;
    for (unsigned i = sizeof(alive_msg_t); i < sizeof(p.payload); ++i) CHECK(bytes[i] == 0);
    return 0;
}
EXPORT int test_interrupts_stay_disabled_on_allocation_failure(void)
{
    for (int failure = 1; failure <= 4; ++failure) {
        init_failure = failure; task_creates = gpio_enables = check_error = 0;
        irq_int_init(); CHECK(check_error && gpio_enables == 0);
        if (failure == 1) CHECK(task_creates == 0);
    }
    init_failure = 0; task_creates = gpio_enables = 0;
    irq_int_init(); CHECK(task_creates == 1 && gpio_enables == 1); return 0;
}
EXPORT int test_button_interrupt_requires_task(void)
{
    init_failure = 2; task_creates = gpio_enables = check_error = 0;
    irq_func_btn_init(); CHECK(check_error && task_creates == 1 && gpio_enables == 0);
    init_failure = 0; return 0;
}
EXPORT int test_all_rotation_boundaries(void)
{
    void (*rotate[])(uint16_t, uint16_t, uint16_t *, uint16_t *) = {rotate_0, rotate_1, rotate_2, rotate_3};
    const uint16_t raw_x[] = {0, 1, 2301, 2302, 2303, 65535};
    const uint16_t raw_y[] = {0, 1, 1531, 1532, 1533, 65535};
    for (unsigned i = 0; i < 4; ++i) for (unsigned a = 0; a < 6; ++a) for (unsigned b = 0; b < 6; ++b) {
        uint16_t x, y; rotate[i](raw_x[a], raw_y[b], &x, &y);
        CHECK(x <= (i < 2 ? 2302 : 1532) && y <= (i < 2 ? 1532 : 2302));
    }
    uint16_t x, y;
    rotate_0(2303, 1533, &x, &y); CHECK(x == 2302 && y == 0);
    rotate_2(2303, 1533, &x, &y); CHECK(x == 0 && y == 0); return 0;
}
static void usb_failure_hook(void)
{
    ++hook_tick;
    if (usb_busy[2]) usb_complete(2, hook_tick != 1);
}
EXPORT int test_usb_failed_transfer_does_not_replay_motion(void)
{
    activate(MOUSE_MODE); CHECK(mouse(0, 42, false)); hook_tick = 0;
    wait_hook = usb_failure_hook; test_steps = 5; usbhid_task(NULL);
    CHECK(sent_count == 2 && sent[0].data.mouse.x == 42 && sent[1].data.mouse.x == 0);
    return 0;
}
EXPORT int test_usb_detach_discards_held_click(void)
{
    activate(MOUSE_MODE); CHECK(mouse(1, 0, false));
    tinyusb_event_t event = {.id = TINYUSB_EVENT_DETACHED}; tinyusb_event_cb(&event, NULL);
    CHECK(ready_mask == 0 && reports.count == 0 && reports.recovering);
    event.id = TINYUSB_EVENT_ATTACHED; tinyusb_event_cb(&event, NULL);
    CHECK(mode_pending && ready_mask == 3);
    input_apply_mode_request(); input_report_t r;
    CHECK(input_take_report(&r) && r.release && r.data.mouse.buttons == 0); return 0;
}
EXPORT int test_mode_change_releases_previous_and_new_device(void)
{
    activate(PTP_MODE); input_set_link(3); input_request_mode(MOUSE_MODE);
    input_apply_mode_request(); input_report_t first, second;
    CHECK(input_take_report(&first) && first.release); input_report_ack(&first);
    CHECK(input_take_report(&second) && second.release && second.mode != first.mode);
    input_report_ack(&second); CHECK(reports.release_mask == 0); return 0;
}
EXPORT int test_ble_old_disconnect_does_not_close_new_link(void)
{
    reset_test(MOUSE_MODE); ble_input_connection(true, 7); ble_input_subscription(7, true);
    ble_input_connection(false, 6); CHECK(connected && subscribed && ready_mask == 1); return 0;
}
