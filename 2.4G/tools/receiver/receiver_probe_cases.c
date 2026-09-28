static const uint8_t probe_test_mac[6] = {2,3,4,5,6,7};
static void probe_feed(const uint8_t mac[6], uint32_t session, uint32_t round)
{
    esp_now_recv_info_t info = {0};
    uint8_t packet[38]; wire_probe_t token = {session, round};
    memcpy(info.src_addr, mac, 6);
    wire_probe_encode(packet, WIRE_PROBE, &token);
    wifi_now_recv_cb(&info, packet, sizeof(packet));
}
static void probe_finish(int status, uint32_t now)
{
    fake_now = now;
    mode_send_complete(NULL, status);
    wireless_control_step(now);
}

EXPORT int check_probe_document_fixture_and_usb_independence(void)
{
    reset_all(); wireless_init();
    mounted = usb_ready = false; suspended = true;
    const uint8_t expected[38] = {11,0,0,0,1,0x78,0x56,0x34,0x12,4,3,2,1};
    uint32_t generation = input_generation();
    probe_feed(probe_test_mac, 0x12345678, 0x01020304);
    CHECK(probe_count == 1 && !qcount && !radio_count && !heartbeat_online);
    wireless_receive_step();
    CHECK(radio_count == 1 && flight_probe && control_in_flight && radio_size == 38);
    CHECK(!memcmp(radio_bytes, expected, 38) && !memcmp(radio_destination, probe_test_mac, 6));
    CHECK(radio_peer_count == 2 && probe_peer_temporary);
    CHECK(radio_peers[1].channel == 1 && radio_peers[1].ifidx == WIFI_IF_STA && !radio_peers[1].encrypt);
    CHECK(!requested.session && !applied.session && !ready && !pending_apply && !ack_pending);
    CHECK(!target_session && !transaction_pending && !control_pending && !link_online);
    CHECK(!usb_count && input_generation() == generation);
    probe_finish(ESP_NOW_SEND_SUCCESS, 1);
    CHECK(!control_in_flight && !probe_peer_temporary && peer_deletes == 1 && radio_peer_count == 1);
    CHECK(esp_now_is_peer_exist(broadcast_mac) && !esp_now_is_peer_exist(probe_test_mac));
    return 0;
}

EXPORT int check_probe_strict_validation(void)
{
    reset_all(); wireless_init(); wireless_led_init();
    esp_now_recv_info_t info = {0}; memcpy(info.src_addr, probe_test_mac, 6);
    wire_probe_t token = {0x12345678, 0x01020304}; uint8_t packet[39];
    wire_probe_encode(packet, WIRE_PROBE, &token); packet[38] = 0;
    int writes = gpio_writes;
    for (int len = -1; len < 38; ++len) wifi_now_recv_cb(&info, packet, len);
    wifi_now_recv_cb(&info, packet, 39);
    wifi_now_recv_cb(NULL, packet, 38); wifi_now_recv_cb(&info, NULL, 38);
    for (unsigned i = 13; i < 38; ++i) {
        packet[i] = 1; wifi_now_recv_cb(&info, packet, 38); packet[i] = 0;
    }
    packet[4] = 2; wifi_now_recv_cb(&info, packet, 38); packet[4] = 1;
    wire_put32(packet + 5, 0); wifi_now_recv_cb(&info, packet, 38);
    wire_put32(packet + 5, token.session); wire_put32(packet + 9, 0);
    wifi_now_recv_cb(&info, packet, 38); wire_put32(packet + 9, token.round);
    packet[1] = 1; wifi_now_recv_cb(&info, packet, 38); packet[1] = 0;
    packet[0] = WIRE_PROBE_ACK; wifi_now_recv_cb(&info, packet, 38);
    wireless_receive_step();
    CHECK(!probe_count && !qcount && !radio_count && !heartbeat_online && !link_online);
    CHECK(gpio_writes == writes && gpio_level == 1 && reports.stats.invalid_packets > 0);
    packet[0] = WIRE_PROBE; wifi_now_recv_cb(&info, packet, 38);
    CHECK(probe_count == 1);
    return 0;
}

EXPORT int check_probe_fifo_copies_and_duplicate_requests(void)
{
    reset_all(); wireless_init();
    esp_now_recv_info_t info = {0}; uint8_t packet[38], original_mac[6];
    wire_probe_t token = {77, 88}; memcpy(info.src_addr, probe_test_mac, 6);
    memcpy(original_mac, info.src_addr, 6); wire_probe_encode(packet, WIRE_PROBE, &token);
    wifi_now_recv_cb(&info, packet, 38); wifi_now_recv_cb(&info, packet, 38);
    info.src_addr[5] = 99; token.round = 89; wire_probe_encode(packet, WIRE_PROBE, &token);
    wifi_now_recv_cb(&info, packet, 38);
    memset(packet, 0xff, sizeof(packet)); memset(info.src_addr, 0xff, 6);
    for (unsigned i = 0; i < 3; ++i) {
        if (i) probe_finish(ESP_NOW_SEND_SUCCESS, i); else wireless_control_step(0);
        CHECK(radio_count == (int)i + 1 && flight_probe);
        CHECK(wire_u32(radio_bytes + 5) == 77 && wire_u32(radio_bytes + 9) == (i == 2 ? 89U : 88U));
        if (i == 2) original_mac[5] = 99;
        CHECK(!memcmp(radio_destination, original_mac, 6));
    }
    probe_finish(ESP_NOW_SEND_SUCCESS, 3);
    CHECK(!control_in_flight && !probe_count && peer_deletes == 3 && radio_peer_count == 1);
    return 0;
}

EXPORT int check_probe_overflow_preserves_input(void)
{
    ready_for(REPORT_MOUSE);
    receive_queue = xQueueCreate(RECEIVE_CAPACITY, sizeof(receive_frame_t));
    input_report_t drag = sample(REPORT_MOUSE, 1, false); drag.data.mouse.x = 20; feed(drag);
    report_buffer_t before = reports;
    for (unsigned i = 0; i < 12; ++i) probe_feed(probe_test_mac, 7, i + 1);
    CHECK(probe_count == 8 && !qcount && !memcmp(&reports, &before, sizeof(reports)));
    for (unsigned i = 0; i < 8; ++i) {
        if (i) probe_finish(ESP_NOW_SEND_SUCCESS, i); else wireless_control_step(0);
        CHECK(wire_u32(radio_bytes + 9) == i + 1);
    }
    probe_finish(ESP_NOW_SEND_SUCCESS, 8);
    CHECK(!probe_count && !control_in_flight && radio_count == 8);
    CHECK(!memcmp(&reports, &before, sizeof(reports)) && !reports.stats.rx_overflows);
    return 0;
}

EXPORT int check_probe_expiry_and_clock_wrap(void)
{
    for (unsigned wrap = 0; wrap < 2; ++wrap) {
        uint32_t start = wrap ? UINT32_MAX - 400U : 0;
        reset_all(); wireless_init(); fake_now = start;
        probe_feed(probe_test_mac, 1, 1); wireless_control_step(start + 749U);
        CHECK(radio_count == 1);
        probe_finish(ESP_NOW_SEND_SUCCESS, start + 750U);
        reset_all(); wireless_init(); fake_now = start;
        probe_feed(probe_test_mac, 1, 1); fake_now = start + 500U;
        probe_feed(probe_test_mac, 1, 2); wireless_control_step(start + 750U);
        CHECK(radio_count == 1 && wire_u32(radio_bytes + 9) == 2 && !probe_count);
        probe_finish(ESP_NOW_SEND_SUCCESS, start + 751U);
        fake_now = start + 1000U; probe_feed(probe_test_mac, 1, 3);
        wireless_control_step(start + 1750U);
        CHECK(radio_count == 1 && !probe_count && !control_in_flight);
    }
    return 0;
}

EXPORT int check_probe_conn_off_never_lights(void)
{
    for (unsigned failure = 0; failure < 4; ++failure) {
        reset_all(); wireless_init(); wireless_led_init();
        int writes = gpio_writes; uint32_t stamp = last_seen_timestamp;
        fake_now = 100; probe_feed(probe_test_mac, 123, 1);
        CHECK(gpio_writes == writes && gpio_level == 1 && !heartbeat_online);
        if (failure == 2) radio_result = ESP_FAIL;
        if (failure == 3) peer_add_result = ESP_FAIL;
        wireless_receive_step();
        CHECK(gpio_writes == writes && gpio_level == 1 && !heartbeat_online);
        if (failure < 2) probe_finish(failure ? ESP_FAIL : ESP_NOW_SEND_SUCCESS, 101);
        CHECK(gpio_writes == writes && gpio_level == 1 && last_seen_timestamp == stamp);
        CHECK(!heartbeat_online && !link_online && !control_pending && !retry_wait);
    }
    return 0;
}

EXPORT int check_probe_enqueue_after_worker_clock_sample(void)
{
    for (unsigned wrap = 0; wrap < 2; ++wrap) {
        reset_all(); wireless_init();
        uint32_t sampled_at = wrap ? UINT32_MAX : 100;
        fake_now = sampled_at + 1;
        probe_feed(probe_test_mac, 1, 1);
        wireless_control_step(sampled_at);
        CHECK(radio_count == 1 && flight_probe);
        probe_finish(ESP_NOW_SEND_SUCCESS, fake_now + 1);
        CHECK(!control_in_flight && !probe_count);
    }
    return 0;
}

EXPORT int check_probe_many_sources_do_not_fill_peer_table(void)
{
    reset_all(); wireless_init();
    uint8_t mac[6]; memcpy(mac, probe_test_mac, 6);
    for (unsigned i = 0; i < 40; ++i) {
        mac[5] = (uint8_t)i; fake_now = i * 2;
        probe_feed(mac, 1, i + 1); wireless_control_step(fake_now);
        CHECK(radio_peer_count == 2 && !memcmp(radio_destination, mac, 6));
        probe_finish(ESP_NOW_SEND_SUCCESS, fake_now + 1);
        CHECK(radio_peer_count == 1 && !probe_peer_temporary);
    }
    CHECK(radio_count == 40 && peer_deletes == 40 && !probe_failures);
    return 0;
}

EXPORT int check_probe_conn_on_does_not_refresh_or_extinguish(void)
{
    reset_all(); wireless_init(); wireless_led_init();
    fake_now = 100; wireless_heartbeat_seen(fake_now); input_link_seen(fake_now);
    int writes = gpio_writes;
    for (unsigned i = 0; i < 5; ++i) {
        fake_now = 101 + i * 1000; probe_feed(probe_test_mac, 123, i + 1);
        wireless_receive_step(); probe_finish(i & 1 ? ESP_FAIL : ESP_NOW_SEND_SUCCESS, fake_now + 1);
        CHECK(gpio_level == 0 && gpio_writes == writes && heartbeat_online);
        CHECK(last_seen_timestamp == 100 && link_seen_at == 100 && link_online);
    }
    fake_now = 5100; test_steps = 1; monitor_link_task(NULL);
    CHECK(gpio_level == 0 && heartbeat_online);
    fake_now = 5101; probe_feed(probe_test_mac, 123, 6); wireless_receive_step();
    CHECK(!link_online && gpio_level == 0 && last_seen_timestamp == 100);
    test_steps = 1; monitor_link_task(NULL);
    CHECK(gpio_level == 1 && !heartbeat_online);
    writes = gpio_writes; probe_finish(ESP_NOW_SEND_SUCCESS, 5102);
    CHECK(gpio_level == 1 && gpio_writes == writes && last_seen_timestamp == 100);
    return 0;
}

EXPORT int check_probe_peer_failure_cleanup_and_reuse(void)
{
    reset_all(); wireless_init();
    peer_add_result = ESP_FAIL; probe_feed(probe_test_mac, 1, 1); wireless_control_step(0);
    CHECK(!control_in_flight && !radio_count && !peer_deletes && probe_failures == 1 && !retry_wait);
    peer_add_result = ESP_OK; radio_result = ESP_FAIL;
    probe_feed(probe_test_mac, 1, 2); wireless_control_step(1);
    CHECK(!control_in_flight && radio_count == 1 && peer_deletes == 1 && radio_peer_count == 1);
    CHECK(probe_failures == 2 && !retry_wait && !control_failures);
    radio_result = ESP_OK; probe_feed(probe_test_mac, 1, 3); wireless_control_step(2);
    probe_finish(ESP_FAIL, 3);
    CHECK(peer_deletes == 2 && radio_peer_count == 1 && probe_failures == 3 && !control_failures);
    esp_now_peer_info_t existing = {.channel = 1, .ifidx = WIFI_IF_STA};
    memcpy(existing.peer_addr, probe_test_mac, 6); CHECK(esp_now_add_peer(&existing) == ESP_OK);
    unsigned adds = peer_adds;
    probe_feed(probe_test_mac, 1, 4); wireless_control_step(4);
    CHECK(!probe_peer_temporary && peer_adds == adds);
    probe_finish(ESP_NOW_SEND_SUCCESS, 5);
    CHECK(peer_deletes == 2 && esp_now_is_peer_exist(probe_test_mac));
    return 0;
}

EXPORT int check_probe_serialization_and_completion_isolation(void)
{
    reset_all(); wireless_init(); settings_connect(777); wireless_request_mode();
    for (unsigned i = 1; i <= 3; ++i) probe_feed(probe_test_mac, 123, i);
    wireless_control_step(0);
    CHECK(flight_probe && ack_pending && control_pending && !transaction_pending);
    uint8_t saved[38]; memcpy(saved, radio_pointer, 38);
    wireless_control_step(0);
    CHECK(radio_count == 1 && !memcmp(saved, radio_pointer, 38));
    probe_finish(ESP_NOW_SEND_SUCCESS, 1);
    CHECK(flight_ack && !flight_probe && ack_pending && control_pending && !transaction_pending);
    probe_finish(ESP_NOW_SEND_SUCCESS, 2);
    CHECK(flight_probe && !ack_pending && control_pending && !transaction_pending);
    probe_finish(ESP_FAIL, 3);
    CHECK(!flight_probe && !flight_ack && radio_size == 1 && control_pending && !retry_wait);
    probe_finish(ESP_NOW_SEND_SUCCESS, 4);
    CHECK(flight_probe && !control_pending && !transaction_pending);
    probe_finish(ESP_NOW_SEND_SUCCESS, 5);
    CHECK(flight_settings && !flight_probe && transaction_pending && radio_count == 6);
    memcpy(saved, radio_pointer, 38);
    probe_feed(probe_test_mac, 123, 4); wireless_control_step(5);
    CHECK(radio_count == 6 && !memcmp(saved, radio_pointer, 38));
    probe_finish(ESP_NOW_SEND_SUCCESS, 6);
    CHECK(flight_probe && transaction_pending && !control_pending && !ack_pending);
    probe_finish(ESP_FAIL, 7);
    CHECK(!control_in_flight && transaction_pending && !retry_wait && !control_failures);
    wireless_control_step(255);
    CHECK(flight_settings && !memcmp(saved, radio_pointer, 38) && !lock_error);
    return 0;
}

EXPORT int check_probe_preserves_control_retry_deadline(void)
{
    reset_all(); wireless_init(); wireless_request_mode(); wireless_control_step(0);
    mode_send_complete(NULL, ESP_FAIL); wireless_control_step(1);
    CHECK(retry_wait && retry_at == 1 && control_pending && !control_in_flight);
    fake_now = 2; probe_feed(probe_test_mac, 7, 1); wireless_control_step(2);
    CHECK(flight_probe && retry_wait && retry_at == 1);
    probe_finish(ESP_FAIL, 3);
    CHECK(retry_wait && retry_at == 1 && control_pending && !control_in_flight);
    wireless_control_step(20); CHECK(radio_count == 2);
    wireless_control_step(21); CHECK(radio_count == 3 && !flight_probe && control_pending);
    probe_finish(ESP_NOW_SEND_SUCCESS, 22); CHECK(!control_pending);
    return 0;
}

EXPORT int check_probe_during_active_drag_and_hold(void)
{
    ready_for(REPORT_HAPTIC);
    receive_queue = xQueueCreate(RECEIVE_CAPACITY, sizeof(receive_frame_t));
    input_report_t drag = sample(REPORT_HAPTIC, 1, true); feed(drag);
    CHECK(aux_output_hold(5, 1, input_generation(), 0));
    aux_output_report_t out;
    CHECK(aux_output_take(&out, input_generation(), 0) && !out.release && out.id == 8);
    aux_output_complete(true);
    CHECK(held_mask && !aux_output_release_pending());
    uint8_t held_before = held_mask;
    report_buffer_t before = reports;
    unsigned queued = count; uint32_t generation = input_generation();
    for (unsigned i = 1; i <= 3; ++i) {
        fake_now = i * 10; probe_feed(probe_test_mac, 99, i);
        wireless_receive_step(); probe_finish(ESP_NOW_SEND_SUCCESS, fake_now + 1);
        CHECK(!memcmp(&reports, &before, sizeof(reports)) && count == queued);
        CHECK(input_generation() == generation && !usb_count && !requested.session && !target_session);
        CHECK(held_mask == held_before && !aux_output_release_pending());
    }
    CHECK(!aux_output_take(&out, generation, fake_now));
    aux_output_cancel();
    CHECK(aux_output_take(&out, generation, fake_now) && out.release && out.id == 8);
    aux_output_complete(true);
    CHECK(!held_mask && !aux_output_release_pending());
    return 0;
}
