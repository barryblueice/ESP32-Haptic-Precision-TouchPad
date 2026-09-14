EXPORT int test_rstp_vectors_and_validation(void)
{
    device_config_t c; device_config_defaults(&c);
    CHECK(device_config_valid(&c)); CHECK(rstp_u32(c.bytes + 8) == 180000);
    uint8_t frame[64] = {'R','S','T','P',1,1,1,0}; rstp_request_t r;
    CHECK(rstp_decode(frame, 64, &r) && r.status == RSTP_OK);
    uint8_t out[64], info[12] = {0x3f,0,0,0,1,0,0,0,0,0,1,0};
    rstp_response(out, &r, RSTP_OK, info, 12);
    CHECK(out[8] == 12 && out[12] == 0x3f && out[16] == 1 && out[22] == 1);
    frame[5] = 3; frame[8] = 32; memcpy(frame + 12, c.bytes, 32); frame[12] = 75;
    CHECK(rstp_decode(frame, 64, &r) && !r.status && r.config.bytes[0] == 75);
    rstp_response(out, &r, RSTP_RESTART, NULL, 0);
    CHECK(out[10] == 7 && out[8] == 0 && out[5] == 3 && out[6] == 1);
    for (unsigned n = 0; n < 12; ++n) CHECK(!rstp_decode(frame, n, &r));
    for (unsigned n = 12; n < 64; ++n) CHECK(rstp_decode(frame, n, &r) && r.status == RSTP_LENGTH);
    CHECK(rstp_decode(frame, 65, &r) && r.status == RSTP_LENGTH);
    frame[63] = 1; CHECK(rstp_decode(frame, 64, &r) && r.status == RSTP_INVALID); frame[63] = 0;
    frame[4] = 2; CHECK(rstp_decode(frame, 64, &r) && r.status == RSTP_VERSION); frame[4] = 1;
    frame[5] = 99; CHECK(rstp_decode(frame, 64, &r) && r.status == RSTP_UNSUPPORTED); frame[5] = 3;
    frame[6] = 0; CHECK(!rstp_decode(frame, 64, &r)); frame[6] = 1;
    frame[10] = 1; CHECK(rstp_decode(frame, 64, &r) && r.status == RSTP_INVALID); frame[10] = 0;
    frame[0] = 0; CHECK(!rstp_decode(frame, 64, &r));
    return 0;
}
EXPORT int test_usb_interface_routing_and_padded_features(void)
{
    uint8_t b[257] = {75}; config_receives=legacy_receives=dfu_requests=0;
    tud_hid_set_report_cb(0,0,HID_REPORT_TYPE_OUTPUT,b,64); CHECK(config_receives==1);
    tud_hid_set_report_cb(0,0x41,HID_REPORT_TYPE_FEATURE,b,256);
    tud_hid_set_report_cb(2,0x41,HID_REPORT_TYPE_FEATURE,b,256);
    tud_hid_set_report_cb(1,0x41,HID_REPORT_TYPE_OUTPUT,b,256);
    CHECK(!legacy_receives && config_receives==1);
    tud_hid_set_report_cb(1,0x41,HID_REPORT_TYPE_FEATURE,b,256);
    CHECK(legacy_receives==1 && legacy_id==0x41 && legacy_setting==75);
    b[255]=1; tud_hid_set_report_cb(1,0x41,HID_REPORT_TYPE_FEATURE,b,256); CHECK(legacy_receives==1); b[255]=0;
    b[0]=0x41; b[1]=25; tud_hid_set_report_cb(1,0,HID_REPORT_TYPE_FEATURE,b,257);
    CHECK(legacy_receives==2 && legacy_setting==25);
    b[0]=3; b[1]=0; tud_hid_set_report_cb(1,0x40,HID_REPORT_TYPE_FEATURE,b,256);
    CHECK(legacy_receives==3 && legacy_id==0x40 && legacy_setting==3);
    b[0]=4; tud_hid_set_report_cb(1,0x40,HID_REPORT_TYPE_FEATURE,b,256); CHECK(legacy_receives==3);
    uint8_t value=0; CHECK(tud_hid_get_report_cb(0,0x41,HID_REPORT_TYPE_FEATURE,&value,1)==0);
    CHECK(tud_hid_get_report_cb(2,0x40,HID_REPORT_TYPE_FEATURE,&value,1)==0);
    return 0;
}
EXPORT int test_config_ranges_and_capabilities(void)
{
    device_config_t good; device_config_defaults(&good);
    for (unsigned field = 0; field < 32; ++field) for (unsigned value = 0; value < 256; ++value) {
        device_config_t c = good; c.bytes[field] = value;
        bool expected = true;
        if (field == 0) expected = value <= 100;
        else if (field == 1) expected = value >= 1 && value <= 3;
        else if (field == 2) expected = value >= 1 && value <= 100;
        else if (field == 3) expected = value >= 80 && value <= 130;
        else if (field == 4) expected = value >= 100;
        else if (field == 5) expected = value <= 3;
        else if (field == 6) expected = value <= 1;
        else if (field == 7) expected = value == 0;
        else if (field < 12) { uint32_t t = rstp_u32(c.bytes + 8); expected = t >= 1000 && t <= 3600000 && t % 1000 == 0; }
        else switch ((field - 12) % 5) {
            case 0: expected = value == 0; break;
            case 1: expected = value <= 4; break;
            case 2: expected = value <= 1; break;
            case 3: expected = value >= 1 && value <= 15; break;
            case 4: expected = value >= 1 && value <= 10; break;
        }
        CHECK(device_config_valid(&c) == expected);
        CHECK(device_config_supported(&good, &c, 0) == (value == good.bytes[field]));
        CHECK(device_config_supported(&good, &c, 0x3f));
    }
    return 0;
}
static tp_multi_msg_t edge_touch(unsigned id, unsigned x, unsigned y)
{
    tp_multi_msg_t m = {0}; m.fingers[id] = (tp_finger_t){.x=x,.y=y,.tip_switch=1,.confidence=1,.contact_id=id}; return m;
}
EXPORT int test_edges_all_sides_rotations_actions(void)
{
    for (unsigned rot = 0; rot < 4; ++rot) for (unsigned e = 0; e < 4; ++e)
    for (unsigned action = 1; action <= 4; ++action) for (unsigned rev = 0; rev < 2; ++rev) {
        device_config_t c; device_config_defaults(&c);
        uint8_t *cfg = c.bytes + 12 + e * 5; cfg[0]=1; cfg[1]=action; cfg[2]=rev;
        unsigned xmax = rot & 1 ? 1532 : 2302, ymax = rot & 1 ? 2302 : 1532;
        unsigned x = e == 2 ? 0 : e == 3 ? xmax : xmax/2;
        unsigned y = e == 0 ? 0 : e == 1 ? ymax : ymax/2;
        edge_gesture_t state = {0}; tp_multi_msg_t m = edge_touch(4,x,y);
        edge_result_t result = edge_gesture_update(&state,&c,&m,xmax,ymax);
        CHECK(result.suppress && !result.steps);
        unsigned delta = ((e < 2 ? xmax : ymax) * 2 + 99) / 100;
        if (e < 2) m.fingers[4].x += delta; else m.fingers[4].y -= delta;
        result = edge_gesture_update(&state,&c,&m,xmax,ymax);
        CHECK(result.suppress && result.steps == (rev ? -1 : 1) && result.action == action);
        result = edge_gesture_update(&state,&c,&m,xmax,ymax); CHECK(!result.steps);
        if (e < 2) m.fingers[4].x -= delta; else m.fingers[4].y += delta;
        result = edge_gesture_update(&state,&c,&m,xmax,ymax); CHECK(result.steps == (rev ? 1 : -1));
        m = (tp_multi_msg_t){0}; result = edge_gesture_update(&state,&c,&m,xmax,ymax);
        CHECK(!result.tap && !result.suppress);
    }
    return 0;
}
EXPORT int test_edge_candidates_corners_and_cancellation(void)
{
    device_config_t c; device_config_defaults(&c); c.bytes[12]=c.bytes[22]=1; c.bytes[13]=c.bytes[23]=2;
    edge_gesture_t state = {0}; tp_multi_msg_t m = edge_touch(1,0,0);
    CHECK(edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    m.fingers[1].x=40; m.fingers[1].y=20;
    CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).steps); // Normalized tie.
    m.fingers[1].x=41;
    CHECK(edge_gesture_update(&state,&c,&m,2000,1000).steps == 1);
    m.fingers[2]=m.fingers[1];
    CHECK(edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    m.fingers[2].tip_switch=0;
    CHECK(edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    m=(tp_multi_msg_t){0}; CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).tap);
    m=edge_touch(1,500,0); edge_gesture_update(&state,&c,&m,2000,1000);
    m=(tp_multi_msg_t){0}; edge_result_t tap=edge_gesture_update(&state,&c,&m,2000,1000);
    CHECK(tap.tap && tap.tap_down.fingers[1].tip_switch);
    m=edge_touch(1,500,500); CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    m.fingers[1].y=0; CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    edge_gesture_reset(&state); edge_gesture_update(&state,&c,&m,2000,1000);
    m.fingers[1].y=100; CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    m.fingers[1].y=0; CHECK(!edge_gesture_update(&state,&c,&m,2000,1000).suppress);
    return 0;
}
EXPORT int test_aux_release_survives_failure_expiry_and_generation(void)
{
    usb_aux_report_t r; usb_aux_reset(false);
    CHECK(usb_aux_steps(2,2,1,0)); CHECK(usb_aux_take(&r,1,0) && r.id==7 && r.data[0]==0xe9);
    usb_aux_unsubmitted(); CHECK(usb_aux_take(&r,1,0) && r.data[0]==0xe9);
    usb_aux_complete(true);
    CHECK(usb_aux_take(&r,2,200) && r.id==7 && r.data[0]==0); // Release survives expiry/reset.
    usb_aux_complete(false); CHECK(usb_aux_take(&r,2,201) && !r.data[0]); usb_aux_complete(true);
    CHECK(!usb_aux_take(&r,2,201));
    CHECK(usb_aux_steps(1,-1,2,201)); CHECK(usb_aux_take(&r,2,201) && r.data[0]==0x70);
    usb_aux_complete(false); CHECK(usb_aux_take(&r,2,202) && !r.data[0]); usb_aux_complete(true);
    CHECK(!usb_aux_take(&r,2,202));
    CHECK(usb_aux_steps(3,2,2,202)); CHECK(usb_aux_take(&r,2,202) && r.id==2 && r.data[3]==1);
    usb_aux_complete(true); CHECK(usb_aux_take(&r,2,202) && r.data[3]==1); usb_aux_complete(true);
    CHECK(!usb_aux_take(&r,2,202));
    CHECK(usb_aux_steps(4,-1,2,202)); CHECK(usb_aux_take(&r,2,202) && r.data[4]==255);
    usb_aux_complete(false); CHECK(!usb_aux_take(&r,2,202));
    usb_aux_reset(false); for (unsigned i=0;i<32;++i) CHECK(usb_aux_steps(2,1,3,202));
    CHECK(!usb_aux_steps(2,1,3,202)); CHECK(!usb_aux_take(&r,3,202));
    return 0;
}
static void reset_test(uint8_t mode)
{
    ptp_report_reset();
    reports = (report_buffer_t){0}; current_tp_mode = mode; current_mode = WIRED_MODE;
    parser = sender = NULL; ready_mask = 0; mode_pending = mode_applied = false;
    request_serial = 0; raw_count = 0; fail_alloc = false; check_error = 0;
    mode_error = mode_writes = cancellations = notifications = delay_count = 0;
    clock_ms = 0; wait_hook = NULL; mode_hook = NULL;
    attempts = sent_count = submit_errors = 0; test_steps = 0;
    usb_ready = mounted = true; usb_busy[1] = usb_busy[2] = false;
    usb_aux_reset(false); usb_aux_flight = false;
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
static void parser_frame(unsigned mask, unsigned x, unsigned y, unsigned pressure)
{
    uint8_t bytes[64]={0x40};
    for(unsigned id=0;id<5;++id) if(mask & (1U<<id)) {
        unsigned o=4+id*8; bytes[o]=1;
        bytes[o+1]=x; bytes[o+2]=x>>8;
        unsigned raw_y=1532-y; bytes[o+3]=raw_y; bytes[o+4]=raw_y>>8;
        bytes[o+5]=pressure; bytes[o+6]=bytes[o+7]=1;
    }
    clock_ms+=10; input_capture(bytes,true,input_generation(),clock_ms);
    parser_steps=1; i2c_queue_task(NULL);
}
static void aux_sender_hook(void)
{
    for(unsigned instance=1;instance<3;++instance) if(usb_busy[instance]) usb_complete(instance,true);
}
EXPORT int test_usb_sender_ptp_with_aux_and_submit_retry(void)
{
    activate(PTP_MODE); uint32_t generation=input_generation();
    CHECK(usb_aux_steps(2,2,generation,clock_ms)); CHECK(usb_aux_steps(4,-2,generation,clock_ms));
    submit_errors=1; wait_hook=aux_sender_hook; test_steps=10; usbhid_task(NULL);
    unsigned presses=0,releases=0; int pan=0;
    for(unsigned i=0;i<sent_count;++i) {
        CHECK(sent_instances[i]==2);
        if(sent_ids[i]==7) { if(sent[i].data.mouse.buttons==0xe9) ++presses; else { CHECK(!sent[i].data.mouse.buttons); ++releases; } }
        else { CHECK(sent_ids[i]==2 && !sent[i].data.mouse.buttons && !sent[i].data.mouse.x && !sent[i].data.mouse.y); pan+=sent[i].data.mouse.pan; }
    }
    CHECK(presses==2 && releases==2 && pan==-2 && !usb_aux_active());
    CHECK(input_mode()==PTP_MODE && attempts==sent_count+1);
    return 0;
}
EXPORT int test_usb_ptp_and_consumer_have_independent_flight(void)
{
    activate(PTP_MODE);
    input_report_t contact={.mode=PTP_MODE,.time_ms=clock_ms}; contact.data.ptp.contact_count=1;
    contact.data.ptp.fingers[0]=(finger_t){.tip_conf_id=3,.x=400,.y=300};
    CHECK(input_publish(input_generation(),&contact,false));
    CHECK(usb_aux_steps(2,1,input_generation(),clock_ms));
    test_steps=1; usbhid_task(NULL); CHECK(usb_busy[1] && usb_busy[2] && usb_aux_flight);
    CHECK(sent_count==2 && sent_ids[0]==7 && sent_ids[1]==1);
    usb_complete(1,true); CHECK(!usb_busy[1] && usb_busy[2]);
    usb_complete(2,true); CHECK(usb_aux_release_pending());
    test_steps=1; usbhid_task(NULL); CHECK(sent_ids[2]==7 && !sent[2].data.mouse.buttons);
    usb_complete(2,true); CHECK(!usb_aux_active()); return 0;
}
EXPORT int test_real_parser_edge_excludes_pressure_and_pointer(void)
{
    for(unsigned mode=0;mode<2;++mode) {
        activate(mode); reset_input_state(); device_config_defaults(&parser_config);
        parser_config.bytes[12]=1; parser_config.bytes[13]=3;
        haptic_button=false; haptic_presses=0;
        parser_frame(1,600,0,255); parser_frame(1,700,0,255); parser_frame(1,800,0,255);
        input_report_t report;
        while(input_take_report(&report)) {
            if(mode==PTP_MODE) CHECK(report.data.ptp.contact_count==0 && !report.data.ptp.buttons);
            else CHECK(!report.data.mouse.x && !report.data.mouse.y && !report.data.mouse.buttons && !report.data.mouse.wheel);
            input_report_ack(&report);
        }
        CHECK(haptic_presses==0 && !ptp_force_click_state.button_down);
        usb_aux_report_t aux; int steps=0;
        while(usb_aux_take(&aux,input_generation(),clock_ms)) { CHECK(aux.id==2 && aux.data[3]==1); ++steps; usb_aux_complete(true); }
        CHECK(steps==4);
        parser_frame(3,850,0,255); parser_frame(1,900,0,255);
        CHECK(!usb_aux_take(&aux,input_generation(),clock_ms));
        parser_frame(0,0,0,0); CHECK(!haptic_presses);
    }
    return 0;
}
EXPORT int test_real_parser_candidate_taps_both_modes(void)
{
    for(unsigned mode=0;mode<2;++mode) {
        activate(mode); reset_input_state(); device_config_defaults(&parser_config);
        parser_config.bytes[12]=1; parser_config.bytes[13]=2;
        parser_frame(2,600,0,0); parser_frame(0,0,0,0);
        input_report_t report; unsigned down=0,up=0;
        while(input_take_report(&report)) {
            if(mode==PTP_MODE) {
                if(report.data.ptp.contact_count) { if(report.data.ptp.fingers[0].tip_conf_id&2) ++down; else ++up; }
            } else { if(report.data.mouse.buttons) ++down; else if(down) ++up; }
            input_report_ack(&report);
        }
        CHECK(down==1 && up==1 && !usb_aux_active());
    }
    return 0;
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
    r.data.ptp.contact_count = 5;
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

/* Exercise the production encoder, including sparse hardware contact slots. */
static ptp_report_t encode_contacts(unsigned mask, unsigned position, unsigned confidence)
{
    tp_multi_msg_t msg = {.scan_time = (uint16_t)clock_ms};
    for (unsigned id = 0; id < 5; ++id) {
        msg.fingers[id] = (tp_finger_t){.contact_id = id,
            .tip_switch = !!(mask & (1U << id)), .confidence = !!(confidence & (1U << id)),
            .x = position + id, .y = position + 100 + id, .pressure_z = 42};
        msg.actual_count += msg.fingers[id].tip_switch;
    }
    if (!msg.actual_count) msg.actual_count = 1; /* Existing parser convention. */
    ptp_report_t report;
    memset(&report, 0xA5, sizeof(report));
    parse_ptp_report(&msg, &report);
    return report;
}
EXPORT int test_ptp_sparse_contacts_and_staggered_lift(void)
{
    ptp_report_reset();
    ptp_report_t r = encode_contacts(18, 200, 18);
    CHECK(r.contact_count == 2);
    CHECK(r.fingers[0].tip_conf_id == ((1 << 2) | 3));
    CHECK(r.fingers[1].tip_conf_id == ((4 << 2) | 3));
    CHECK(r.fingers[2].tip_conf_id == 0 && r.fingers[2].x == 0);
    r = encode_contacts(16, 300, 16);
    CHECK(r.contact_count == 2 && r.fingers[0].tip_conf_id == ((1 << 2) | 1));
    CHECK(r.fingers[0].x == 201 && r.fingers[0].y == 301 && r.fingers[0].pressure_z == 0);
    CHECK(r.fingers[1].x == 304 && (r.fingers[1].tip_conf_id & 2));
    r = encode_contacts(16, 400, 16);
    CHECK(r.contact_count == 1 && r.fingers[0].tip_conf_id == ((4 << 2) | 3));
    CHECK(r.fingers[1].tip_conf_id == 0);
    r = encode_contacts(0, 999, 0);
    CHECK(r.contact_count == 1 && r.fingers[0].tip_conf_id == ((4 << 2) | 1));
    CHECK(r.fingers[0].x == 404 && r.fingers[0].y == 504 && r.fingers[0].pressure_z == 0);
    r = encode_contacts(0, 999, 0);
    CHECK(r.contact_count == 0 && r.fingers[0].tip_conf_id == 0);
    return 0;
}

EXPORT int test_ptp_simultaneous_lifts_and_idle(void)
{
    const unsigned counts[] = {2, 3, 5};
    for (unsigned c = 0; c < 3; ++c) {
        ptp_report_reset();
        unsigned count = counts[c], mask = (1U << count) - 1;
        ptp_report_t r = encode_contacts(mask, 100, mask & ~2U);
        CHECK(r.contact_count == count);
        r = encode_contacts(0, 999, 31);
        CHECK(r.contact_count == count);
        for (unsigned id = 0; id < count; ++id) {
            CHECK(r.fingers[id].tip_conf_id == ((id << 2) | (id != 1)));
            CHECK(r.fingers[id].x == 100 + id && r.fingers[id].y == 200 + id);
            CHECK(r.fingers[id].pressure_z == 0);
        }
        r = encode_contacts(0, 999, 31);
        CHECK(r.contact_count == 0);
        for (unsigned id = 0; id < 5; ++id)
            CHECK(r.fingers[id].tip_conf_id == 0 && r.fingers[id].x == 0 && r.fingers[id].pressure_z == 0);
    }
    return 0;
}

EXPORT int test_ptp_five_contacts_lift_one_by_one(void)
{
    ptp_report_reset();
    encode_contacts(31, 100, 31);
    unsigned mask = 31;
    for (unsigned id = 0; id < 5; ++id) {
        mask &= ~(1U << id);
        ptp_report_t r = encode_contacts(mask, 200 + id * 10, 31);
        CHECK(r.contact_count == 5 - id);
        CHECK(r.fingers[0].tip_conf_id == ((id << 2) | 1));
        CHECK(r.fingers[0].x == (id == 0 ? 100 : 200 + (id - 1) * 10 + id));
        for (unsigned slot = 1; slot < r.contact_count; ++slot)
            CHECK(r.fingers[slot].tip_conf_id == (((id + slot) << 2) | 3));
    }
    CHECK(encode_contacts(0, 999, 31).contact_count == 0);
    return 0;
}

EXPORT int test_ptp_history_reset_and_reused_id(void)
{
    ptp_report_reset(); encode_contacts(18, 100, 18);
    ptp_report_reset();
    CHECK(encode_contacts(0, 999, 31).contact_count == 0);
    ptp_report_t r = encode_contacts(16, 300, 0);
    CHECK(r.contact_count == 1 && r.fingers[0].tip_conf_id == ((4 << 2) | 2));
    r = encode_contacts(0, 999, 31);
    CHECK(r.contact_count == 1 && r.fingers[0].tip_conf_id == (4 << 2));
    CHECK(r.fingers[0].x == 304);
    return 0;
}

EXPORT int test_ptp_count_change_is_an_edge(void)
{
    activate(PTP_MODE);
    input_report_t r = {.mode = PTP_MODE};
    CHECK(input_publish(input_generation(), &r, false));
    /* A non-confident ID 0 lift has zero flag bits, just like an unused slot. */
    r.data.ptp.contact_count = 1;
    CHECK(input_publish(input_generation(), &r, false));
    r.data.ptp.contact_count = 0;
    CHECK(input_publish(input_generation(), &r, false));
    CHECK(reports.count == 3 && reports.stats.merged == 0);
    CHECK(input_take_report(&r) && r.data.ptp.contact_count == 0);
    CHECK(input_take_report(&r) && r.data.ptp.contact_count == 1);
    CHECK(input_take_report(&r) && r.data.ptp.contact_count == 0);
    return 0;
}

EXPORT int test_ptp_only_valid_contacts_affect_merging(void)
{
    activate(PTP_MODE);
    input_report_t r = {.mode = PTP_MODE};
    r.data.ptp = encode_contacts(16, 100, 16);
    CHECK(input_publish(input_generation(), &r, false));
    r.data.ptp = encode_contacts(16, 200, 16);
    CHECK(input_publish(input_generation(), &r, false));
    r.data.ptp = encode_contacts(16, 300, 16);
    r.data.ptp.fingers[4].tip_conf_id = 0xFF; /* Outside Contact Count. */
    CHECK(input_publish(input_generation(), &r, false));
    CHECK(reports.count == 2 && reports.stats.merged == 1);
    CHECK(input_take_report(&r) && r.data.ptp.fingers[0].x == 104);
    CHECK(input_take_report(&r) && r.data.ptp.fingers[0].x == 304);
    return 0;
}

static bool publish_contacts(unsigned mask, unsigned position)
{
    input_report_t r = {.mode = PTP_MODE, .time_ms = clock_ms};
    r.data.ptp = encode_contacts(mask, position, 31);
    return input_publish(input_generation(), &r, false);
}
static void ptp_usb_hook(void)
{
    ++hook_tick;
    if (hook_tick >= 2) usb_ready = true;
    if (hook_tick >= 4 && usb_busy[1]) usb_complete(1, true);
}
EXPORT int test_ptp_usb_fast_taps_busy_retry_and_delayed_completion(void)
{
    const unsigned counts[] = {2, 3, 5};
    for (unsigned c = 0; c < 3; ++c) {
        activate(PTP_MODE);
        unsigned count = counts[c], mask = (1U << count) - 1;
        CHECK(publish_contacts(mask, 100)); CHECK(publish_contacts(0, 999));
        CHECK(publish_contacts(mask, 200)); CHECK(publish_contacts(0, 999));
        usb_ready = false; submit_errors = 1; hook_tick = 0;
        wait_hook = ptp_usb_hook; test_steps = 10; usbhid_task(NULL);
        CHECK(sent_count == 4 && attempts == 5 && reports.count == 0 && delay_count == 1);
        for (unsigned frame = 0; frame < 4; ++frame) {
            CHECK(sent[frame].mode == PTP_MODE && sent[frame].data.ptp.contact_count == count);
            for (unsigned id = 0; id < count; ++id) {
                const finger_t *finger = &sent[frame].data.ptp.fingers[id];
                CHECK(finger->tip_conf_id == ((id << 2) | ((frame & 1) ? 1 : 3)));
                CHECK(finger->x == 100 + (frame / 2) * 100 + id);
                CHECK(finger->pressure_z == ((frame & 1) ? 0 : 42));
            }
        }
    }
    return 0;
}

static bool complete_ptp_release(const input_report_t *r)
{
    if (!r->release || r->mode != PTP_MODE || r->data.ptp.contact_count != 5 || r->data.ptp.buttons)
        return false;
    for (unsigned id = 0; id < 5; ++id)
        if (r->data.ptp.fingers[id].tip_conf_id != ((id << 2) | 1) || r->data.ptp.fingers[id].pressure_z)
            return false;
    return true;
}
EXPORT int test_ptp_recovery_releases_all_ids(void)
{
    input_report_t r, old;
    for (unsigned reason = 0; reason < 4; ++reason) {
        activate(PTP_MODE); CHECK(publish_contacts(31, 100));
        if (reason == 0) {
            for (unsigned i = 0; i < 31; ++i) CHECK(publish_contacts((i & 1) ? 31 : 0, 100));
            CHECK(!publish_contacts(31, 100)); /* Capacity exhausted by edges. */
        } else if (reason == 1) {
            clock_ms = REPORT_MAX_AGE_MS + 1;
        } else if (reason == 2) {
            input_set_link(3); /* USB can send both logical modes. */
            input_request_mode(MOUSE_MODE); CHECK(input_apply_mode_request());
        } else {
            input_set_link(0); input_set_link(1U << PTP_MODE);
        }
        CHECK(input_take_report(&r));
        if (r.mode == MOUSE_MODE) { input_report_ack(&r); CHECK(input_take_report(&r)); }
        CHECK(complete_ptp_release(&r)); old = r;
        input_recover(); input_report_ack(&old);
        CHECK(reports.release_mask != 0); /* Old ack cannot release a new generation. */
    }
    return 0;
}
