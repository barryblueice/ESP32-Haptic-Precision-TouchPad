static void reset_all(void)
{
    reports = (report_buffer_t){0};
    sender = worker = NULL;
    usb_ready = link_online = false;
    mode = TP_MOUSE_MODE;
    link_seen_at = fake_now = notifications = 0;
    requested_serial = flight_serial = retry_at = control_failures = 0;
    control_pending = control_in_flight = control_done = control_success = retry_wait = false;
    flight_mode = 0;
    radio_result = radio_count = 0;
    usb_pending = usb_flight = (input_report_t){0};
    usb_have_pending = usb_busy = dfu_requested = false;
    usb_mutex = (void *)1;
    mounted = endpoint_ready = usb_accept = true;
    suspended = false;
    usb_count = dfu_writes = restarts = 0;
    button_press_threshold = haptic_click_intensity = 2;
    init_error = sdk_calls = fail_at = lock_error = mutex_depth = 0;
    peer_added = send_registered = recv_registered = false;
    last_seen_timestamp = 0;
    gpio_level = gpio_writes = 0;
    nvs_init_result = nvs_erase_result = nvs_open_result = nvs_get_result = nvs_set_result = nvs_commit_result = 0;
    nvs_inits = nvs_erases = nvs_opens = nvs_sets = nvs_commits = nvs_closes = 0;
    input_init();
}
static input_report_t sample(report_kind_t kind, uint8_t buttons, bool tip)
{
    input_report_t r = {.kind = kind, .generation = input_generation(), .time_ms = fake_now};
    if (kind == REPORT_MOUSE) r.data.mouse.buttons = buttons;
    if (kind == REPORT_HAPTIC) {
        r.data.haptic_ptp.buttons = buttons; r.data.haptic_ptp.contact_count = 1;
        for (unsigned i = 0; i < 5; ++i) r.data.haptic_ptp.fingers[i].tip_conf_id = (i << 2) | 1;
        r.data.haptic_ptp.fingers[0].tip_conf_id |= tip ? 2 : 0;
        r.data.haptic_ptp.fingers[0].pressure_z = tip ? 80 : 0;
    }
    if (kind == REPORT_LEGACY) {
        r.data.legacy_ptp.buttons = buttons; r.data.legacy_ptp.contact_count = 1;
        for (unsigned i = 0; i < 5; ++i) r.data.legacy_ptp.fingers[i].tip_conf_id = (i << 2) | 1;
        r.data.legacy_ptp.fingers[0].tip_conf_id |= tip ? 2 : 0;
    }
    return r;
}
static void ready_for(report_kind_t kind)
{
    reset_all();
    input_set_mode(kind == REPORT_MOUSE ? TP_MOUSE_MODE : TP_PTP_MODE);
    input_set_usb(true);
    input_link_seen(0);
    input_report_t r;
    while (input_take(&r)) input_complete(&r, true);
    r = sample(kind, 0, false);
    report_buffer_push(&reports, &r);
}
static void feed(input_report_t r)
{
    wireless_msg_t p = {.type = r.kind == REPORT_MOUSE ? MOUSE_MODE :
                               r.kind == REPORT_LEGACY ? LEGACY_PTP_MODE : HAPTIC_PTP_MODE,
                        .payload = r.data};
    input_receive(&p, r.generation, r.time_ms);
}
static void event(int id)
{
    tinyusb_event_t e = {.id = id};
    if (id == TINYUSB_EVENT_ATTACHED) mounted = true;
    if (id == TINYUSB_EVENT_DETACHED) mounted = false;
    if (id == TINYUSB_EVENT_SUSPENDED) suspended = true;
    if (id == TINYUSB_EVENT_RESUMED) suspended = false;
    tinyusb_event_cb(&e, NULL);
}

EXPORT int check_wire_compatibility(void)
{
    reset_all();
    /* Independent byte fixture matching Main: enum 4, packed five 6-byte fingers. */
    uint8_t bytes[38] = {4,0,0,0, 3,0x34,0x12,0x78,0x56,80};
    bytes[34] = 0xcd; bytes[35] = 0xab; bytes[36] = 1; bytes[37] = 1;
    wireless_msg_t p;
    CHECK(wireless_decode(bytes, 38, &p));
    CHECK(p.type == HAPTIC_PTP_MODE && p.payload.haptic_ptp.fingers[0].x == 0x1234);
    CHECK(p.payload.haptic_ptp.fingers[0].y == 0x5678 && p.payload.haptic_ptp.fingers[0].pressure_z == 80);
    CHECK(p.payload.haptic_ptp.scan_time == 0xabcd && p.payload.haptic_ptp.buttons == 1);
    const unsigned sizes[] = {9, 33, 5, 10, 38};
    for (unsigned type = 0; type <= 4; ++type) {
        memset(bytes, 0, sizeof(bytes)); bytes[0] = type;
        for (int len = 0; len < (int)sizes[type]; ++len) CHECK(!wireless_decode(bytes, len, &p));
        CHECK(wireless_decode(bytes, sizes[type], &p));
        CHECK(wireless_decode(bytes, 38, &p) && p.type == type);
    }
    CHECK(!wireless_decode(NULL, 38, &p) && !wireless_decode(bytes, -1, &p));
    CHECK(!wireless_decode(bytes, 38, NULL));
    bytes[0] = 99; CHECK(!wireless_decode(bytes, 38, &p));
    bytes[0] = 1; CHECK(!wireless_decode(bytes, 1, &p));
    return 0;
}
EXPORT int check_mouse_edges_and_accumulation(void)
{
    ready_for(REPORT_MOUSE);
    input_report_t r = sample(REPORT_MOUSE, 1, false), out;
    r.data.mouse.x = 7; feed(r);
    r.data.mouse.x = 100; r.data.mouse.y = -100; r.data.mouse.wheel = 80; r.data.mouse.pan = -80;
    feed(r); feed(r); feed(r);
    r.data.mouse = (mouse_hid_report_t){0}; feed(r);
    CHECK(reports.count == 3 && reports.stats.merged == 2);
    CHECK(input_take(&out) && out.data.mouse.buttons == 1 && out.data.mouse.x == 7);
    int x = 0, y = 0, wheel = 0, pan = 0;
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(input_take(&out) && out.data.mouse.buttons == 1);
        x += out.data.mouse.x; y += out.data.mouse.y; wheel += out.data.mouse.wheel; pan += out.data.mouse.pan;
    }
    CHECK(x == 300 && y == -300 && wheel == 240 && pan == -240);
    CHECK(input_take(&out) && out.data.mouse.buttons == 0 && !input_take(&out));
    return 0;
}
EXPORT int check_ptp_edges_and_latest_coordinates(void)
{
    for (report_kind_t k = REPORT_HAPTIC; k <= REPORT_LEGACY; ++k) {
        ready_for(k);
        input_report_t r = sample(k, 1, true), out;
        if (k == REPORT_HAPTIC) r.data.haptic_ptp.fingers[0].x = 10;
        else r.data.legacy_ptp.fingers[0].x = 10;
        feed(r);
        for (unsigned x = 20; x <= 40; x += 10) {
            if (k == REPORT_HAPTIC) { r.data.haptic_ptp.fingers[0].x = x; r.data.haptic_ptp.scan_time = x; }
            else { r.data.legacy_ptp.fingers[0].x = x; r.data.legacy_ptp.scan_time = x; }
            feed(r);
        }
        r = sample(k, 0, false); feed(r);
        CHECK(reports.count == 3);
        CHECK(input_take(&out));
        CHECK((k == REPORT_HAPTIC ? out.data.haptic_ptp.fingers[0].x : out.data.legacy_ptp.fingers[0].x) == 10);
        CHECK(input_take(&out));
        CHECK((k == REPORT_HAPTIC ? out.data.haptic_ptp.fingers[0].x : out.data.legacy_ptp.fingers[0].x) == 40);
        CHECK(input_take(&out) && report_all_up(&out));
    }
    return 0;
}
EXPORT int check_contact_count_and_id_edges(void)
{
    for (report_kind_t k = REPORT_HAPTIC; k <= REPORT_LEGACY; ++k) {
        ready_for(k);
        input_report_t r = sample(k, 0, true);
        feed(r);
        if (k == REPORT_HAPTIC) ++r.data.haptic_ptp.contact_count;
        else ++r.data.legacy_ptp.contact_count;
        feed(r);
        if (k == REPORT_HAPTIC) r.data.haptic_ptp.fingers[0].tip_conf_id = 7;
        else r.data.legacy_ptp.fingers[0].tip_conf_id = 7;
        feed(r);
        CHECK(reports.count == 3 && reports.stats.merged == 0);
    }
    return 0;
}
EXPORT int check_queue_overflow_and_neutral_gate(void)
{
    ready_for(REPORT_MOUSE);
    uint32_t generation = input_generation();
    for (unsigned i = 0; i < 33; ++i) feed(sample(REPORT_MOUSE, (i & 1) ? 0 : 1, false));
    CHECK(reports.count == 0 && input_generation() != generation && reports.recovering);
    input_report_t out;
    while (input_take(&out)) { CHECK(out.release); input_complete(&out, true); }
    feed(sample(REPORT_MOUSE, 1, false));
    CHECK(!input_take(&out) && reports.recovering);
    feed(sample(REPORT_MOUSE, 0, false));
    CHECK(!reports.recovering && !input_take(&out));
    feed(sample(REPORT_MOUSE, 1, false));
    CHECK(input_take(&out) && out.data.mouse.buttons == 1);
    return 0;
}
EXPORT int check_expiry_boundaries_and_wrap(void)
{
    ready_for(REPORT_MOUSE);
    feed(sample(REPORT_MOUSE, 1, false));
    fake_now = 100;
    input_report_t out;
    CHECK(input_take(&out) && !out.release && input_current(&out));
    fake_now = 101; CHECK(!input_current(&out) && reports.recovering);
    ready_for(REPORT_MOUSE);
    fake_now = UINT32_MAX - 50; feed(sample(REPORT_MOUSE, 1, false));
    fake_now = 49; CHECK(input_take(&out) && !out.release);
    fake_now = 50; CHECK(!input_current(&out));
    ready_for(REPORT_MOUSE);
    input_report_t r = sample(REPORT_MOUSE, 0, false);
    feed(r); feed(r);
    fake_now = 90; r.time_ms = 90; feed(r);
    CHECK(reports.entries[(reports.head + 1) % 32].report.time_ms == 0);
    fake_now = 101; CHECK(input_take(&out) && out.release);
    return 0;
}
EXPORT int check_accumulator_overflow_and_ring_wrap(void)
{
    ready_for(REPORT_MOUSE);
    input_report_t r = sample(REPORT_MOUSE, 0, false), out;
    feed(r); feed(r);
    reports.entries[1].x = INT32_MAX;
    r.data.mouse.x = 1; feed(r);
    CHECK(reports.recovering && !reports.count);
    ready_for(REPORT_MOUSE);
    for (unsigned i = 0; i < 100; ++i) {
        feed(sample(REPORT_MOUSE, i & 1, false));
        CHECK(input_take(&out) && out.data.mouse.buttons == (i & 1));
    }
    CHECK(!reports.count);
    return 0;
}
EXPORT int check_release_ids_and_old_ack(void)
{
    for (report_kind_t k = REPORT_HAPTIC; k <= REPORT_LEGACY; ++k) {
        ready_for(k);
        input_report_t r = sample(k, 1, true), out;
        if (k == REPORT_HAPTIC) r.data.haptic_ptp.fingers[0].tip_conf_id = 31;
        else r.data.legacy_ptp.fingers[0].tip_conf_id = 31;
        input_submitted(&r);
        input_recover();
        CHECK(input_take(&out) && out.release);
        uint8_t mask = reports.release_mask;
        input_report_t old = out; --old.generation;
        input_complete(&old, true);
        CHECK(reports.release_mask == mask);
        while (out.kind != k) { input_complete(&out, true); CHECK(input_take(&out)); }
        CHECK(report_all_up(&out));
        CHECK((k == REPORT_HAPTIC ? out.data.haptic_ptp.fingers[0].tip_conf_id :
                                   out.data.legacy_ptp.fingers[0].tip_conf_id) == 29);
        if (k == REPORT_HAPTIC) CHECK(out.data.haptic_ptp.fingers[0].pressure_z == 0);
    }
    return 0;
}
EXPORT int check_ptp_subtype_switch(void)
{
    ready_for(REPORT_HAPTIC);
    feed(sample(REPORT_HAPTIC, 1, true));
    uint32_t before = input_generation();
    feed(sample(REPORT_LEGACY, 1, true));
    CHECK(reports.active == REPORT_LEGACY && reports.recovering && !reports.count);
    CHECK(input_generation() != before);
    return 0;
}
EXPORT int check_usb_busy_submit_and_completion(void)
{
    for (report_kind_t k = REPORT_HAPTIC; k <= REPORT_MOUSE; ++k) {
        ready_for(k);
        input_report_t r = sample(k, 1, true);
        feed(r);
        endpoint_ready = false; usbhid_step();
        CHECK(usb_have_pending && !usb_busy && usb_count == 0);
        endpoint_ready = true; usb_accept = false; usbhid_step();
        CHECK(usb_have_pending && !usb_busy && usb_count == 1);
        usb_accept = true; usbhid_step();
        CHECK(usb_busy && !usb_have_pending && usb_count == 2);
        CHECK(usb_instance == k && usb_id == k && usb_size == report_size(k));
        CHECK(!memcmp(usb_bytes, &r.data, usb_size));
        fake_now = 200; usbhid_step();
        CHECK(usb_count == 2 && usb_busy);
        tud_hid_report_complete_cb(k, NULL, 0);
        CHECK(!usb_busy && !lock_error);
    }
    return 0;
}
EXPORT int check_usb_failed_transfer_and_stale_callbacks(void)
{
    ready_for(REPORT_MOUSE);
    input_report_t r = sample(REPORT_MOUSE, 1, false); r.data.mouse.x = 30; feed(r);
    usbhid_step(); CHECK(usb_busy);
    input_recover(); uint32_t generation = input_generation();
    tud_hid_report_failed_cb(REPORT_MOUSE, HID_REPORT_TYPE_OUTPUT, NULL, 0);
    CHECK(usb_busy);
    tud_hid_report_failed_cb(REPORT_MOUSE, HID_REPORT_TYPE_INPUT, NULL, 0);
    CHECK(!usb_busy && input_generation() == generation);
    usbhid_step(); CHECK(usb_flight.release && usb_instance == REPORT_HAPTIC);
    tud_hid_report_complete_cb(REPORT_MOUSE, NULL, 0);
    CHECK(usb_busy);
    tud_hid_report_failed_cb(REPORT_HAPTIC, HID_REPORT_TYPE_INPUT, NULL, 0);
    CHECK(!usb_busy && input_generation() != generation);
    return 0;
}
EXPORT int check_usb_mode_switch_in_flight(void)
{
    ready_for(REPORT_MOUSE);
    feed(sample(REPORT_MOUSE, 1, false)); usbhid_step();
    uint8_t ptp = 3;
    tud_hid_set_report_cb(REPORT_HAPTIC, REPORTID_HAPTIC_FEATURE, HID_REPORT_TYPE_FEATURE, &ptp, 1);
    CHECK(input_mode() == TP_PTP_MODE && usb_busy);
    usbhid_step(); CHECK(usb_count == 1);
    tud_hid_report_complete_cb(REPORT_MOUSE, NULL, 0);
    CHECK(reports.release_mask == REPORT_RELEASE_MASK);
    usbhid_step(); CHECK(usb_flight.release);
    return 0;
}
EXPORT int check_usb_detach_suspend_resume(void)
{
    ready_for(REPORT_MOUSE);
    feed(sample(REPORT_MOUSE, 1, false)); usbhid_step();
    event(TINYUSB_EVENT_SUSPENDED); usbhid_step();
    CHECK(usb_busy && usb_count == 1);
    event(TINYUSB_EVENT_RESUMED); usbhid_step();
    CHECK(usb_busy && usb_count == 1);
    tud_hid_report_complete_cb(REPORT_MOUSE, NULL, 0);
    usbhid_step(); CHECK(usb_flight.release);
    event(TINYUSB_EVENT_DETACHED);
    CHECK(!usb_busy && !usb_have_pending && !usb_ready);
    tud_hid_report_complete_cb(REPORT_HAPTIC, NULL, 0);
    event(TINYUSB_EVENT_ATTACHED);
    CHECK(input_mode() == TP_MOUSE_MODE && usb_ready);
    usbhid_step(); CHECK(usb_flight.release && !lock_error);
    return 0;
}
EXPORT int check_usb_bus_reset_reconfigure(void)
{
    ready_for(REPORT_MOUSE);
    feed(sample(REPORT_MOUSE, 1, false)); usbhid_step();
    CHECK(usb_busy);
    /* TinyUSB resets classes on BUS_RESET without calling tud_umount_cb. */
    event(TINYUSB_EVENT_ATTACHED);
    CHECK(!usb_busy && !usb_have_pending && reports.recovering);
    usbhid_step();
    CHECK(usb_busy && usb_flight.release && usb_instance == REPORT_HAPTIC);
    return 0;
}
EXPORT int check_get_report_bounds_and_interface_collision(void)
{
    reset_all();
    const unsigned lengths[] = {0,1,14,15,255,256};
    uint8_t bytes[260];
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        unsigned len = lengths[i];
        memset(bytes, 0xaa, sizeof(bytes));
        unsigned got = tud_hid_get_report_cb(REPORT_LEGACY, 6, HID_REPORT_TYPE_FEATURE, bytes + 1, len);
        CHECK(got == len && bytes[0] == 0xaa && bytes[len + 1] == 0xaa);
        for (unsigned j = 0; j < len; ++j) CHECK(bytes[j + 1] == 0);
        memset(bytes, 0xaa, sizeof(bytes));
        got = tud_hid_get_report_cb(REPORT_HAPTIC, 6, HID_REPORT_TYPE_FEATURE, bytes + 1, len);
        CHECK(got == (len ? 1 : 0) && bytes[got + 1] == 0xaa);
        if (len) CHECK(bytes[1] == 3);
        memset(bytes, 0xaa, sizeof(bytes));
        got = tud_hid_get_report_cb(REPORT_HAPTIC, 0x42, HID_REPORT_TYPE_FEATURE, bytes + 1, len);
        CHECK(got == (len < 15 ? len : 15) && bytes[0] == 0xaa && bytes[got + 1] == 0xaa);
        if (got >= 2) CHECK(bytes[1] == 1 && bytes[2] == 16);
    }
    CHECK(!tud_hid_get_report_cb(0, 6, HID_REPORT_TYPE_FEATURE, bytes, 256));
    CHECK(!tud_hid_get_report_cb(REPORT_HAPTIC, 5, HID_REPORT_TYPE_INPUT, bytes, 256));
    CHECK(!tud_hid_get_report_cb(REPORT_HAPTIC, 5, HID_REPORT_TYPE_FEATURE, NULL, 256));
    CHECK(!tud_hid_get_report_cb(REPORT_LEGACY, 0x41, HID_REPORT_TYPE_FEATURE, bytes, 256));
    return 0;
}
EXPORT int check_set_report_routing_and_dfu(void)
{
    reset_all();
    uint8_t data[] = {6,3};
    tud_hid_set_report_cb(REPORT_LEGACY, 0, HID_REPORT_TYPE_FEATURE, data, 2);
    CHECK(input_mode() == TP_MOUSE_MODE);
    tud_hid_set_report_cb(REPORT_HAPTIC, 0, HID_REPORT_TYPE_FEATURE, data, 1);
    CHECK(input_mode() == TP_MOUSE_MODE);
    tud_hid_set_report_cb(REPORT_HAPTIC, 0, HID_REPORT_TYPE_FEATURE, data, 2);
    CHECK(input_mode() == TP_PTP_MODE && control_pending);
    data[0] = 0xff;
    tud_hid_set_report_cb(REPORT_HAPTIC, 0x40, HID_REPORT_TYPE_FEATURE, data, 1);
    CHECK(button_press_threshold == 3 && !dfu_requested);
    tud_hid_set_report_cb(REPORT_HAPTIC, 0x41, HID_REPORT_TYPE_FEATURE, data, 1);
    CHECK(haptic_click_intensity == 4);
    data[0] = 0;
    tud_hid_set_report_cb(REPORT_HAPTIC, 0x40, HID_REPORT_TYPE_FEATURE, data, 1);
    CHECK(button_press_threshold == 1);
    data[0] = 0xff;
    tud_hid_set_report_cb(REPORT_MOUSE, 0, HID_REPORT_TYPE_OUTPUT, data, 1);
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_FEATURE, data, 1);
    CHECK(!dfu_requested);
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, data, 1);
    CHECK(dfu_requested && !dfu_writes);
    usbhid_step(); CHECK(dfu_writes == 1 && restarts == 1 && !lock_error);
    return 0;
}
EXPORT int check_radio_serialization_and_retry(void)
{
    reset_all();
    wireless_request_mode(); wireless_request_mode(); wireless_control_step(0);
    CHECK(radio_count == 1 && radio_byte == 0 && control_in_flight);
    input_set_mode(TP_PTP_MODE); wireless_request_mode();
    wireless_control_step(10);
    CHECK(radio_count == 1 && *radio_pointer == 0);
    mode_send_complete(NULL, ESP_NOW_SEND_SUCCESS); wireless_control_step(11);
    CHECK(radio_count == 2 && radio_byte == 1);
    mode_send_complete(NULL, ESP_FAIL); wireless_control_step(12);
    wireless_control_step(31); CHECK(radio_count == 2);
    radio_result = ESP_FAIL; wireless_control_step(32);
    CHECK(radio_count == 3 && !control_in_flight && control_pending);
    wireless_control_step(51); CHECK(radio_count == 3);
    radio_result = ESP_OK; wireless_control_step(52); CHECK(radio_count == 4);
    mode_send_complete(NULL, ESP_NOW_SEND_SUCCESS); wireless_control_step(53);
    CHECK(!control_pending && !control_in_flight);
    return 0;
}
EXPORT int check_receive_validation_fifo_overflow(void)
{
    ready_for(REPORT_MOUSE);
    receive_queue = xQueueCreate(16, sizeof(receive_frame_t));
    esp_now_recv_info_t info = {0};
    wireless_msg_t p = {.type = ALIVE_MODE};
    int writes = gpio_writes;
    wifi_now_recv_cb(&info, (uint8_t *)&p, 4); wireless_receive_step();
    CHECK(reports.stats.invalid_packets == 1 && gpio_writes == writes);
    p.type = MOUSE_MODE; p.payload.mouse.buttons = 1;
    uint32_t old = input_generation();
    for (unsigned i = 0; i < 17; ++i) wifi_now_recv_cb(&info, (uint8_t *)&p, sizeof(p));
    CHECK(reports.stats.rx_overflows == 1 && input_generation() != old);
    wireless_receive_step(); CHECK(!reports.count && reports.recovering && !qcount);
    return 0;
}
EXPORT int check_heartbeat_is_not_neutral_and_gpio_unchanged(void)
{
    ready_for(REPORT_MOUSE);
    receive_queue = xQueueCreate(16, sizeof(receive_frame_t));
    input_recover();
    esp_now_recv_info_t info = {0};
    wireless_msg_t p = {.type = ALIVE_MODE};
    p.payload.alive.vbus_level = 1;
    fake_now = 100; wifi_now_recv_cb(&info, (uint8_t *)&p, sizeof(p)); wireless_receive_step();
    CHECK(gpio_level == 1 && last_seen_timestamp == 100 && reports.recovering && !reports.all_up);
    test_steps = 1; monitor_link_task(NULL);
    CHECK(gpio_level == 0);
    fake_now = 5101; test_steps = 1; monitor_link_task(NULL);
    CHECK(gpio_level == 1);
    return 0;
}
EXPORT int check_link_loss_recovery(void)
{
    ready_for(REPORT_MOUSE);
    feed(sample(REPORT_MOUSE, 1, false));
    uint32_t gen = input_generation();
    input_check_link(5000); CHECK(input_generation() == gen);
    input_check_link(5001); CHECK(input_generation() != gen && !link_online && !reports.count);
    gen = input_generation(); input_check_link(6000); CHECK(input_generation() == gen);
    input_link_seen(6000); CHECK(link_online && reports.recovering);
    return 0;
}
EXPORT int check_nvs_first_boot_and_errors(void)
{
    reset_all(); nvs_open_result = ESP_ERR_NVS_NOT_FOUND;
    CHECK(nvs_mode_init() == ESP_OK && nvs_inits == 1 && nvs_sets == 1 && nvs_commits == 1);
    reset_all(); nvs_get_result = ESP_ERR_NVS_NOT_FOUND; nvs_commit_result = ESP_FAIL;
    CHECK(nvs_mode_init() == ESP_FAIL && nvs_closes == 2);
    reset_all(); nvs_get_result = ESP_ERR_NVS_NOT_FOUND; nvs_set_result = ESP_FAIL;
    CHECK(nvs_mode_init() == ESP_FAIL && !nvs_commits);
    reset_all(); nvs_init_result = ESP_FAIL;
    CHECK(nvs_mode_init() == ESP_FAIL && !nvs_opens);
    reset_all(); nvs_init_result = ESP_ERR_NVS_NO_FREE_PAGES; nvs_erase_result = ESP_FAIL;
    CHECK(nvs_mode_init() == ESP_FAIL && !nvs_opens);
    reset_all(); nvs_init_result = ESP_ERR_NVS_NEW_VERSION_FOUND;
    CHECK(nvs_mode_init() == ESP_OK && nvs_erases == 1 && nvs_inits == 2);
    CHECK(nvs_mode_read(NULL) == ESP_ERR_INVALID_ARG);
    return 0;
}
EXPORT int check_initialization_resource_failures(void)
{
    reset_all(); wireless_init();
    CHECK(!init_error && recv_registered && peer_added && !lock_error && !nvs_inits);
    int total = sdk_calls;
    for (int failure = 1; failure <= total; ++failure) {
        reset_all(); fail_at = failure; wireless_init();
        CHECK(init_error && sdk_calls == failure);
    }
    reset_all(); usbhid_init();
    CHECK(!init_error && usb_mutex);
    total = sdk_calls;
    for (int failure = 1; failure <= total; ++failure) {
        reset_all(); fail_at = failure; usbhid_init();
        CHECK(init_error && sdk_calls == failure);
    }
    return 0;
}
