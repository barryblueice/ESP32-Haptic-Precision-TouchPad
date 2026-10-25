static unsigned f12_actions, f12_buttons, f12_trusted_reports, f12_mouse_motion;
static unsigned f12_mouse_downs;
static unsigned f12_button_presses, f12_button_releases, f12_previous_buttons;
static input_report_t f12_last_report;
static void f12_drain(void)
{
    input_report_t r;
    while (input_take_report(&r)) {
        if (!r.release) {
            unsigned buttons=r.mode==PTP_MODE?r.data.ptp.buttons:r.data.mouse.buttons;
            if(buttons && !f12_previous_buttons) ++f12_button_presses;
            if(!buttons && f12_previous_buttons) ++f12_button_releases;
            f12_previous_buttons=buttons;
            f12_last_report=r; f12_buttons |= r.mode==PTP_MODE?r.data.ptp.buttons:r.data.mouse.buttons;
            if (r.mode==PTP_MODE) {
                for(unsigned i=0;i<5;++i) if((r.data.ptp.fingers[i].tip_conf_id & 3)==3) ++f12_trusted_reports;
            } else {
                if(r.data.mouse.x || r.data.mouse.y) ++f12_mouse_motion;
                if(r.data.mouse.buttons) ++f12_mouse_downs;
            }
        }
        input_report_ack(&r);
    }
    aux_output_event_t e;
    for (unsigned i=0;i<32 && aux_output_take_event(&e,input_generation(),now);++i) {
        if(e.action) ++f12_actions;
        aux_output_event_complete(true);
    }
}
static void f12_reset(bool corner)
{
    now=0;test_vbus=1;ptp_button_press_threshold=2;wifi_hook=parser_hook=NULL;
    current_mode=WIRED_MODE;current_tp_mode=PTP_MODE;reports=(report_buffer_t){0};
    ready_mask=0;mode_pending=false;mode_applied=true;requested_mode=PTP_MODE;request_serial=0;
    haptic=(surface_haptic_runtime_t){0};surface_runtime_state(&haptic,SURFACE_READY);
    device_config_defaults(&config);fail_mode=config_change=false;
    if(corner) { config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=5;config.bytes[CFG_POINTS+2]=15; }
    input_pipeline_init();reset_input_state();aux_output_reset(false);
    input_set_link(3);f12_drain();f12_actions=f12_buttons=f12_trusted_reports=f12_mouse_motion=0;f12_last_report=(input_report_t){0};
    f12_mouse_downs=0;
    f12_button_presses=f12_button_releases=f12_previous_buttons=0;
}
static void f12_packet(uint8_t p[64], uint8_t type, unsigned wx, unsigned wy, unsigned z)
{
    memset(p,0,64);p[0]=64;p[2]=0x0c;p[3]=0x18;
    if(type) { p[4]=type;p[5]=100;p[7]=0x98;p[8]=5;p[9]=z;p[10]=wx;p[11]=wy; } /* logical 100,100 */
}
static void f12_feed_packet(const uint8_t p[64])
{
    now+=10;
    input_capture(p,true,input_source_generation(),input_generation(),now);
    parser_budget=1;i2c_queue_task(NULL);f12_drain();
}
static void f12_feed(uint8_t type, unsigned wx, unsigned wy, unsigned z)
{ uint8_t p[64];f12_packet(p,type,wx,wy,z);f12_feed_packet(p); }

EXPORT int check_f12_captured_palm_labelled_finger_pad_clicks(void)
{
    const f12_captured_frame_t *traces[]={f12_captured_contact_3,f12_captured_contact_4};
    const unsigned lengths[]={sizeof(f12_captured_contact_3)/sizeof(*traces[0]),
                              sizeof(f12_captured_contact_4)/sizeof(*traces[0])};
    for(unsigned n=0;n<2;++n) for(unsigned mode=0;mode<=1;++mode)
    for(unsigned route=0;route<3;++route) for(unsigned level=2;level<=3;++level) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;current_mode=route;
        config.bytes[CFG_ROTATION]=2;ptp_button_press_threshold=level;
        for(unsigned i=0;i<lengths[n];++i) {
            const f12_captured_frame_t *f=traces[n]+i;
            uint8_t p[64];f12_packet(p,f->type,f->wx,f->wy,f->z);
            p[5]=f->x;p[6]=f->x>>8;p[7]=f->y;p[8]=f->y>>8;
            now=f->time-10;f12_feed_packet(p);
            CHECK(!knuckle_frame_count && !knuckle_state.pending && !f12_actions);
            if(f->type) CHECK(last_confidence[0]);
        }
        CHECK(!physical_active && !f12_previous_buttons);
        /* Level 2 exercises force clicks; level 3 leaves tap recognition to
         * the mouse parser or (for PTP) the host's trusted down/up stream. */
        if(mode==MOUSE_MODE || level==2) CHECK(f12_button_presses==1 && f12_button_releases==1);
        else CHECK(!f12_button_presses && f12_trusted_reports==lengths[n]-1);
        if(mode==PTP_MODE) CHECK((f12_last_report.data.ptp.fingers[0].tip_conf_id&3)==1);
    }
    return 0;
}

EXPORT int check_f12_legacy_major_minor_boundaries(void)
{
    /* Historical byte6/byte7 ordering, including asymmetry and zero minor. */
    const unsigned cases[][3]={{0,0,1},{0,16,1},{16,0,1},{4,11,1},{11,4,0},
        {8,4,1},{9,4,0},{1,1,1},{6,6,1},{13,13,1},{16,16,1},{17,16,0},{16,17,0}};
    for(unsigned type=0;type<256;++type) for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        uint8_t p[64];f12_packet(p,type,cases[i][0],cases[i][1],100);
        CHECK(tp_f12_touch_confident(p+4)==((type==1 || type==3 || type==6) && cases[i][2]));
        CHECK(tp_f12_confident(p+4)==((type==1 || type==6) && cases[i][2]));
    }
    uint8_t p[64];f12_packet(p,3,8,9,100);p[5]=255;p[6]=255;
    CHECK(!tp_f12_touch_confident(p+4));
    f12_packet(p,3,8,9,100);p[7]=255;p[8]=255;
    CHECK(!tp_f12_touch_confident(p+4));return 0;
}

EXPORT int check_f12_03_uses_full_legacy_size_range(void)
{
    const unsigned widths[][2]={{1,1},{6,6},{13,13},{16,16},{16,0},{0,16},{4,11}};
    for(unsigned mode=0;mode<=1;++mode) for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned j=0;j<4;++j) f12_feed(3,widths[i][0],widths[i][1],200);
        CHECK(last_confidence[0] && f12_button_presses==1);
        f12_feed(0,0,0,0);CHECK(f12_button_releases==1);
    }
    /* There is no pad-only single-contact restriction. */
    f12_reset(false);current_tp_mode=MOUSE_MODE;reports.mode=MOUSE_MODE;
    uint8_t p[64];f12_packet(p,3,13,13,200);memcpy(p+12,p+4,8);
    for(unsigned j=0;j<4;++j) f12_feed_packet(p);
    CHECK(last_confidence[0] && last_confidence[1] && f12_buttons==4);
    f12_feed(0,0,0,0);CHECK(f12_button_presses==1 && f12_button_releases==1);
    return 0;
}

EXPORT int check_f12_filtered_contact_cancels_click_until_physical_lift(void)
{
    for(unsigned mode=0;mode<=1;++mode) for(unsigned fault=0;fault<4;++fault) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned i=0;i<4;++i) f12_feed(3,8,9,200);
        CHECK(f12_button_presses==1 && ptp_force_click_state.button_down);
        uint8_t p[64];f12_packet(p,3,8,9,200);
        if(fault==0) p[10]=17;
        if(fault==1) { p[10]=11;p[11]=4; }
        if(fault==2) p[4]=4;
        if(fault==3) { p[5]=255;p[6]=255; }
        f12_feed_packet(p);
        CHECK(!ptp_force_click_state.button_down && f12_button_releases==1);
        for(unsigned i=0;i<4;++i) f12_feed(3,8,9,200);
        CHECK(last_confidence[0]==(fault!=2) && f12_button_presses==1 && !ptp_force_click_state.button_down);
        f12_feed(0,0,0,0);
        CHECK(f12_button_presses==1 && !physical_active);
        for(unsigned i=0;i<4;++i) f12_feed(3,8,9,200);
        CHECK(last_confidence[0] && f12_button_presses==2);
        f12_feed(0,0,0,0);CHECK(f12_button_releases==2);
    }
    return 0;
}

EXPORT int check_f12_pad_never_claims_special_gestures_or_knuckle(void)
{
    f12_reset(true);config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
    uint8_t p[64];f12_packet(p,3,8,9,55);p[7]=0xe8;
    f12_feed_packet(p);p[5]=200;f12_feed_packet(p);
    CHECK(last_confidence[0] && !f12_actions && !knuckle_frame_count && !knuckle_state.pending);
    f12_feed(0,0,0,0);CHECK(!f12_actions);
    /* An already owned 04 corner retains ownership when followed by a
     * size-valid 03: no host click may leak through. */
    f12_reset(true);f12_feed(4,8,9,200);
    for(unsigned i=0;i<5;++i) f12_feed(3,8,9,200);
    f12_feed(0,0,0,0);
    CHECK(f12_actions==1 && !f12_buttons && !f12_trusted_reports);
    return 0;
}

EXPORT int check_f12_finger_pad_type_transition_keeps_one_press(void)
{
    for(unsigned mode=0;mode<=1;++mode) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned i=0;i<4;++i) f12_feed(1,8,9,200);
        for(unsigned i=0;i<4;++i) f12_feed(3,8,9,200);
        for(unsigned i=0;i<4;++i) f12_feed(1,8,9,200);
        CHECK(f12_button_presses==1 && !f12_button_releases);
        f12_feed(0,0,0,0);CHECK(f12_button_presses==1 && f12_button_releases==1);
    }
    return 0;
}

EXPORT int check_f12_all_object_codes_are_not_tip_bits(void)
{
    for(unsigned type=1;type<256;++type) {
        f12_reset(true);f12_feed(type,4,4,200);
        CHECK(physical_active && slot_active[0]);
        CHECK(last_confidence[0] == (type==1 || type==3 || type==6));
        CHECK(f12_actions == (type==1 || type==4 || type==6));
        f12_feed(0,0,0,0);CHECK(!physical_active && !slot_active[0]);
    }
    return 0;
}
EXPORT int check_f12_captured_unclassified_contacts_stay_present(void)
{
    const uint8_t records[][8]={{4,109,0,194,0,82,4,10},{4,51,0,136,0,58,4,7}};
    for(unsigned k=0;k<2;++k) {
        f12_reset(false);
        uint8_t p[64]={64,0,12,24};memcpy(p+4,records[k],8);
        for(unsigned i=0;i<21;++i) f12_feed_packet(p);
        CHECK(physical_active && slot_active[0] && !last_confidence[0]);
        CHECK(!f12_actions && !f12_buttons && !knuckle_frame_count);
        CHECK(f12_last_report.data.ptp.contact_count==1);
        CHECK((f12_last_report.data.ptp.fingers[0].tip_conf_id & 3)==2);
        f12_feed(0,0,0,0);CHECK(!physical_active);
    }
    return 0;
}
EXPORT int check_f12_type_change_cancels_without_retrigger_or_false_lift(void)
{
    for(unsigned rejected=3;rejected<=3;++rejected) {
        f12_reset(true);f12_feed(1,4,4,200);CHECK(f12_actions==1);
        f12_feed(rejected,4,4,200);CHECK(physical_active && point_state.state==POINT_SUPPRESSED);
        f12_feed(1,4,4,200);CHECK(physical_active && f12_actions==1);
        f12_feed(0,0,0,0);CHECK(!physical_active && point_state.state==POINT_IDLE);
        f12_feed(1,4,4,200);CHECK(f12_actions==2);
    }
    return 0;
}
EXPORT int check_f12_waiting_classification_can_become_trusted_once(void)
{
    f12_reset(true);f12_feed(2,4,4,200);CHECK(point_state.state==POINT_WAITING && !f12_actions);
    f12_feed(1,4,4,200);CHECK(f12_actions==1);
    f12_feed(1,4,4,200);CHECK(f12_actions==1);return 0;
}
EXPORT int check_f12_legacy_axis_order_controls_gesture_admission(void)
{
    for(unsigned swap=0;swap<2;++swap) {
        f12_reset(true);f12_feed(1,swap?4:10,swap?10:4,200);
        CHECK(physical_active && last_confidence[0]==swap && f12_actions==swap);
        f12_feed(0,0,0,0);f12_feed(1,swap?4:8,swap?8:4,200);
        CHECK(last_confidence[0] && f12_actions==swap+1);
    }
    return 0;
}
EXPORT int check_f12_finger_pad_tap_survives_shape_changes(void)
{
    /* Legacy major/minor is directional: swapped elongated samples fail. */
    const unsigned widths[][2]={{4,4},{4,9},{4,11},{4,8},{4,5}};
    for(unsigned type=1;type<=6;type+=5) for(unsigned swap=0;swap<2;++swap)
    for(unsigned mode=0;mode<=1;++mode) for(unsigned round_start=0;round_start<2;++round_start) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned i=round_start?0:1;i<5;++i) {
            f12_feed(type,widths[i][swap],widths[i][1-swap],55);
            CHECK(last_confidence[0]==(widths[i][swap]<=2*widths[i][1-swap]) && !f12_buttons);
        }
        f12_feed(0,0,0,0);
        CHECK(!physical_active && !f12_actions && !f12_mouse_motion);
        if(swap) { CHECK(!f12_buttons && !f12_mouse_downs);continue; }
        if(mode==MOUSE_MODE) {
            CHECK(f12_buttons==1 && f12_mouse_downs==1);
            CHECK(!f12_last_report.data.mouse.buttons);
            f12_feed(0,0,0,0);CHECK(f12_mouse_downs==1);
        } else {
            CHECK(f12_trusted_reports && !f12_buttons);
            CHECK((f12_last_report.data.ptp.fingers[0].tip_conf_id & 3)==1);
        }
    }
    return 0;
}
EXPORT int check_f12_finger_pad_force_click_and_size_cancellation(void)
{
    for(unsigned mode=0;mode<=1;++mode) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned i=0;i<4;++i) f12_feed(1,4,11,200);
        CHECK(f12_buttons==1 && ptp_force_click_state.button_down);
        f12_feed(3,4,17,200);CHECK(!ptp_force_click_state.button_down);
        f12_buttons=0;
        for(unsigned i=0;i<4;++i) f12_feed(1,4,11,200);
        f12_feed(0,0,0,0);CHECK(!f12_buttons && !physical_active);
    }
    return 0;
}
EXPORT int check_f12_finger_pad_keeps_size_and_coordinate_limits(void)
{
    for(unsigned swap=0;swap<2;++swap) {
        uint8_t p[64];f12_packet(p,1,swap?4:17,swap?17:4,55);
        CHECK(!tp_f12_confident(p+4));
    }
    uint8_t p[64];f12_packet(p,1,4,11,55);p[5]=255;p[6]=255;
    CHECK(!tp_f12_confident(p+4));
    f12_packet(p,1,4,11,55);p[7]=255;p[8]=255;
    CHECK(!tp_f12_confident(p+4));return 0;
}
EXPORT int check_f12_unclassified_cannot_release_recovery_gate(void)
{
    f12_reset(false);f12_feed(1,4,4,200);input_source_recover("test");
    f12_feed(4,4,4,200);CHECK(source_wait_up && source_uncertain && physical_active);
    f12_feed(3,4,4,200);CHECK(source_wait_up && physical_active);
    f12_feed(0,0,0,0);CHECK(!source_wait_up && !physical_active);
    f12_feed(1,4,4,200);CHECK(slot_active[0]);return 0;
}
EXPORT int check_f12_palm_blocks_knuckle_and_does_not_finish_candidate(void)
{
    for(unsigned type=3;type<=4;++type) {
        knuckle_gesture_t s={0};uint8_t p[64];f12_packet(p,type,3,3,45);
        CHECK(!knuckle_gesture_update(&s,p,0).suppress);
        f12_packet(p,0,0,0,0);CHECK(!knuckle_gesture_update(&s,p,20).screenshot && !s.pending);
        knuckle_gesture_reset(&s);f12_packet(p,1,3,3,45);
        CHECK(knuckle_gesture_update(&s,p,100).suppress);
        p[4]=type;knuckle_result_t r=knuckle_gesture_update(&s,p,120);
        CHECK(r.replay && !r.screenshot && s.active && !s.pending);
        p[4]=1;CHECK(!knuckle_gesture_update(&s,p,130).suppress);
        f12_packet(p,0,0,0,0);CHECK(!knuckle_gesture_update(&s,p,140).screenshot && !s.pending);
    }
    return 0;
}
EXPORT int check_f12_force_click_rejects_types_and_cancels_until_lift(void)
{
    for(unsigned mode=0;mode<=1;++mode) for(unsigned type=2;type<=4;type+=2) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        for(unsigned i=0;i<6;++i) f12_feed(type,4,4,200);
        CHECK(!f12_buttons && !ptp_force_click_state.button_down);
        f12_feed(0,0,0,0);
        for(unsigned i=0;i<4;++i) f12_feed(1,4,4,200);
        CHECK(ptp_force_click_state.button_down);
        f12_feed(type,4,4,200);CHECK(!ptp_force_click_state.button_down);
        for(unsigned i=0;i<4;++i) f12_feed(1,4,4,200);
        CHECK(!ptp_force_click_state.button_down);
        f12_feed(0,0,0,0);
        for(unsigned i=0;i<4;++i) f12_feed(1,4,4,200);
        CHECK(ptp_force_click_state.button_down);
    }
    return 0;
}
EXPORT int check_f12_mixed_finger_palm_blocks_single_finger_actions(void)
{
    f12_reset(true);uint8_t p[64];f12_packet(p,1,4,4,200);memcpy(p+12,p+4,8);p[12]=3;
    for(unsigned i=0;i<4;++i) f12_feed_packet(p);
    CHECK(physical_active && slot_active[0] && slot_active[1]);
    CHECK(!f12_actions && !f12_buttons && !knuckle_frame_count);return 0;
}

EXPORT int check_f12_edge_capture_cancels_until_real_lift(void)
{
    f12_reset(false);config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
    uint8_t p[64];f12_packet(p,1,4,4,200);p[7]=0xe8; /* logical y=20 */
    f12_feed_packet(p);p[5]=200;f12_feed_packet(p);
    CHECK(f12_actions && edge_state.state==EDGE_CAPTURED);
    unsigned actions=f12_actions;
    p[4]=3;f12_feed_packet(p);CHECK(physical_active && edge_state.state==EDGE_SUPPRESSED);
    p[4]=1;p[5]=240;f12_feed_packet(p);CHECK(f12_actions==actions && !f12_buttons);
    f12_feed(0,0,0,0);CHECK(edge_state.state==EDGE_IDLE && !physical_active);
    return 0;
}

EXPORT int check_f12_unknown_transition_cannot_create_mouse_tap(void)
{
    f12_reset(false);current_tp_mode=MOUSE_MODE;reports.mode=MOUSE_MODE;
    f12_feed(1,4,4,20);f12_feed(1,4,4,20);
    f12_feed(4,4,4,20);CHECK(physical_active);
    f12_feed(1,4,4,20);f12_feed(1,4,4,20);
    f12_feed(0,0,0,0);CHECK(!f12_buttons && !physical_active);
    return 0;
}

EXPORT int check_f12_unclassified_corner_continues_through_palm_without_retrigger(void)
{
    f12_reset(true);f12_feed(4,4,10,200);
    CHECK(f12_actions==1 && point_state.state==POINT_ACTIVE && !last_confidence[0]);
    f12_feed(1,4,4,200);f12_feed(4,4,9,200);f12_feed(1,4,4,200);
    CHECK(f12_actions==1 && point_state.state==POINT_ACTIVE && !f12_buttons && !f12_trusted_reports);
    f12_feed(3,4,4,200);CHECK(point_state.state==POINT_ACTIVE);
    f12_feed(4,4,4,200);CHECK(f12_actions==1 && point_state.state==POINT_ACTIVE && physical_active);
    for(unsigned i=0;i<5;++i) f12_feed(4,4,4,200);
    CHECK(f12_actions==1 && !f12_buttons && !f12_trusted_reports);
    f12_feed(3,4,4,200);f12_feed(4,4,9,200);CHECK(f12_actions==1);
    f12_feed(0,0,0,0);f12_feed(4,4,4,200);CHECK(f12_actions==2);
    return 0;
}

EXPORT int check_f12_unclassified_edge_slides_but_never_replays_tap(void)
{
    for(unsigned mode=0;mode<=1;++mode) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        uint8_t p[64];f12_packet(p,4,4,10,200);p[7]=0xe8;
        f12_feed_packet(p);CHECK(edge_state.state==EDGE_CANDIDATE);
        p[5]=200;f12_feed_packet(p);CHECK(f12_actions && edge_state.state==EDGE_CAPTURED);
        p[4]=1;p[10]=4;p[11]=4;f12_feed_packet(p);
        CHECK(edge_state.state==EDGE_CAPTURED);
        f12_feed(0,0,0,0);CHECK(!f12_buttons && !f12_trusted_reports && !f12_mouse_motion);
        /* Too little motion: neither 04-only nor 04->01 may replay a host tap. */
        for(unsigned change=0;change<=1;++change) {
            f12_packet(p,4,4,10,200);p[7]=0xe8;f12_feed_packet(p);
            if(change) { p[4]=1;p[11]=4;f12_feed_packet(p); }
            f12_feed(0,0,0,0);
            CHECK(!f12_buttons && !f12_trusted_reports && !f12_mouse_motion && !gesture_only_seen);
        }
    }
    return 0;
}

EXPORT int check_f12_unclassified_outside_gestures_has_no_normal_output(void)
{
    for(unsigned mode=0;mode<=1;++mode) {
        f12_reset(true);current_tp_mode=mode;reports.mode=mode;
        uint8_t p[64];f12_packet(p,4,4,9,200);p[5]=0;p[6]=4;p[7]=0;p[8]=3;
        for(unsigned i=0;i<10;++i) { p[5]+=5;f12_feed_packet(p); }
        f12_feed(0,0,0,0);
        CHECK(!f12_actions && !f12_buttons && !f12_trusted_reports && !f12_mouse_motion);
    }
    uint8_t p[64];f12_packet(p,4,4,4,100);p[5]=255;p[6]=255;
    CHECK(!tp_f12_gesture_eligible(p+4));
    f12_reset(true);f12_feed_packet(p);CHECK(!f12_actions);
    return 0;
}

EXPORT int check_f12_unclassified_all_corners_and_rotations(void)
{
    for(unsigned rotation=0;rotation<4;++rotation) for(unsigned corner=0;corner<4;++corner) {
        f12_reset(false);config.bytes[CFG_ROTATION]=rotation;
        config.bytes[CFG_POINTS+4*corner]=1;config.bytes[CFG_POINTS+4*corner+1]=5;
        config.bytes[CFG_POINTS+4*corner+2]=15;
        unsigned xmax=(rotation&1)?1532:2302, ymax=(rotation&1)?2302:1532;
        unsigned x=(corner&1)?xmax-50:50, y=(corner&2)?ymax-50:50, rx,ry;
        switch(rotation) {
        case 1:rx=2302-y;ry=1532-x;break;
        case 2:rx=2302-x;ry=y;break;
        case 3:rx=y;ry=x;break;
        default:rx=x;ry=1532-y;break;
        }
        uint8_t p[64];f12_packet(p,4,4,10,55);p[5]=rx;p[6]=rx>>8;p[7]=ry;p[8]=ry>>8;
        f12_feed_packet(p);CHECK(f12_actions==1 && point_state.point==corner && !last_confidence[0]);
        f12_feed_packet(p);CHECK(f12_actions==1 && !f12_buttons && !f12_trusted_reports);
        p[4]=3;f12_feed_packet(p);p[4]=4;f12_feed_packet(p);
        CHECK(f12_actions==1 && point_state.state==POINT_ACTIVE && physical_active);
        f12_feed_packet(p);CHECK(f12_actions==1 && !f12_buttons && !f12_trusted_reports);
        f12_feed(0,0,0,0);CHECK(!physical_active);
    }
    return 0;
}

EXPORT int check_f12_unclassified_edge_continues_through_palm(void)
{
    for(unsigned mode=0;mode<=1;++mode) {
        f12_reset(false);current_tp_mode=mode;reports.mode=mode;
        config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        uint8_t p[64];f12_packet(p,4,4,9,200);p[7]=0xe8;
        f12_feed_packet(p);p[5]=200;f12_feed_packet(p);
        CHECK(f12_actions && edge_state.state==EDGE_CAPTURED);
        unsigned actions=f12_actions;
        p[4]=3;p[5]=230;f12_feed_packet(p);CHECK(edge_state.state==EDGE_CAPTURED && f12_actions>actions);
        actions=f12_actions;
        p[4]=4;f12_feed_packet(p);
        CHECK(edge_state.state==EDGE_CAPTURED && f12_actions==actions && physical_active);
        f12_feed_packet(p);CHECK(f12_actions==actions);
        p[5]=255;f12_feed_packet(p);CHECK(f12_actions>actions && edge_state.state==EDGE_CAPTURED);
        f12_feed(0,0,0,0);CHECK(!f12_buttons && !f12_trusted_reports && !f12_mouse_motion);
        /* A cancelled candidate must not replay a click. */
        f12_feed_packet(p);p[4]=3;f12_feed_packet(p);p[4]=4;f12_feed_packet(p);
        actions=f12_actions;f12_feed(0,0,0,0);
        CHECK(f12_actions==actions && !f12_buttons && !f12_trusted_reports && !f12_mouse_motion);
    }
    return 0;
}

EXPORT int check_f12_cancelled_contact_cannot_restart_by_region_or_slot(void)
{
    f12_reset(true);f12_feed(4,4,4,200);f12_feed(2,4,4,200);
    uint8_t p[64];f12_packet(p,4,4,4,200);p[5]=0;p[6]=4;p[7]=0;p[8]=3;
    f12_feed_packet(p);CHECK(f12_actions==1 && point_state.state==POINT_SUPPRESSED);
    CHECK(!f12_buttons && !f12_trusted_reports);
    /* A different slot, or a transition while another contact is present,
     * does not clear the already-cancelled gesture. */
    for(unsigned multi=0;multi<=1;++multi) {
        f12_reset(true);f12_feed(4,4,4,200);f12_feed(2,4,4,200);
        f12_packet(p,4,4,4,200);memcpy(p+12,p+4,8);
        if(!multi) memset(p+4,0,8);
        f12_feed_packet(p);CHECK(f12_actions==1 && point_state.state==POINT_SUPPRESSED);
        CHECK(!f12_buttons && !f12_trusted_reports);
    }
    return 0;
}

EXPORT int check_f12_point_repeat_survives_palm_but_multi_and_lift_stop_it(void)
{
    f12_reset(true);config.bytes[CFG_EDGE_REPEAT]|=0x10;
    f12_feed(4,4,9,55);CHECK(f12_actions==1 && point_gesture_repeating(&point_state));
    now+=600;f12_feed(3,4,9,55);CHECK(f12_actions==2 && point_gesture_repeating(&point_state));
    now+=200;f12_feed(4,4,9,55);CHECK(f12_actions==3);
    uint8_t p[64];f12_packet(p,3,4,9,55);memcpy(p+12,p+4,8);
    f12_feed_packet(p);CHECK(point_state.state==POINT_SUPPRESSED && !point_gesture_repeating(&point_state));
    now+=600;f12_feed(4,4,9,55);CHECK(f12_actions==3);
    f12_feed(0,0,0,0);CHECK(point_state.state==POINT_IDLE && !physical_active);
    CHECK(!f12_buttons && !f12_trusted_reports);
    f12_feed(3,4,9,55);CHECK(f12_actions==3 && !point_gesture_repeating(&point_state));
    CHECK(!f12_buttons && f12_trusted_reports==1);
    return 0;
}

EXPORT int check_f12_right_edge_accepts_captured_finger_pad_from_onset(void)
{
    /* Actual failing sequence: rotation 2, right edge 5%, 1% step.
     * Elongated type-01 samples must be trusted from the first frame. */
    f12_reset(false);config.bytes[CFG_ROTATION]=2;
    config.bytes[CFG_EDGES+15]=1;config.bytes[CFG_EDGES+16]=3;
    config.bytes[CFG_EDGE_REPEAT]=0x0b;
    const unsigned samples[][5]={{0,490,55,4,9},{31,490,70,4,11},
        {16,601,60,4,9},{12,620,58,4,8},{8,644,58,4,8},{0,845,10,4,5}};
    for(unsigned i=0;i<6;++i) {
        uint8_t p[64];f12_packet(p,1,samples[i][3],samples[i][4],samples[i][2]);
        p[5]=samples[i][0];p[6]=samples[i][0]>>8;p[7]=samples[i][1];p[8]=samples[i][1]>>8;
        if(i==3) now+=123;
        f12_feed_packet(p);
        CHECK(last_confidence[0]);
        if(i==0) CHECK(edge_state.state==EDGE_CANDIDATE && edge_state.start_y==490 && !f12_actions);
        if(i>=2) CHECK(edge_state.state==EDGE_CAPTURED && edge_state.edge==3 && f12_actions);
    }
    CHECK(edge_state.continue_outside && !f12_buttons);
    f12_feed(0,0,0,0);CHECK(edge_state.state==EDGE_IDLE && !physical_active);
    return 0;
}

EXPORT int check_f12_edge_waiting_does_not_rearm_after_identity_or_trusted_origin(void)
{
    for(unsigned change=0;change<4;++change) {
        f12_reset(false);config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        uint8_t p[64];f12_packet(p,1,4,17,55);p[7]=0xe8;
        f12_feed_packet(p);CHECK(edge_state.state==EDGE_WAITING);
        p[11]=4;
        if(change==0) { memcpy(p+12,p+4,8);memset(p+4,0,8); }
        if(change==1) memcpy(p+12,p+4,8);
        if(change==2) { p[7]=0;p[8]=3; } /* First trusted position is outside edges. */
        if(change==3) { f12_feed(0,0,0,0);CHECK(edge_state.state==EDGE_IDLE && !f12_actions);continue; }
        f12_feed_packet(p);CHECK(edge_state.state==EDGE_BLOCKED && !f12_actions);
        f12_packet(p,1,4,4,55);p[7]=0xe8;p[5]=200;f12_feed_packet(p);
        CHECK(edge_state.state==EDGE_BLOCKED && !f12_actions);
    }
    return 0;
}

EXPORT int check_f12_palm_continuation_keeps_edge_region_switch_and_identity_checks(void)
{
    for(unsigned outside=0;outside<=1;++outside) {
        f12_reset(false);config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        config.bytes[CFG_EDGE_REPEAT]=outside;
        uint8_t p[64];f12_packet(p,4,4,9,55);p[7]=0xe8;
        f12_feed_packet(p);p[5]=200;f12_feed_packet(p);unsigned actions=f12_actions;
        p[4]=3;p[5]=240;p[7]=0;p[8]=3;f12_feed_packet(p);
        CHECK(edge_gesture_captured(&edge_state)==(outside!=0));
        CHECK((f12_actions>actions)==(outside!=0));
        f12_feed(0,0,0,0);CHECK(!f12_buttons && !f12_trusted_reports && !f12_mouse_motion);
    }
    f12_reset(true);f12_feed(4,4,9,55);
    uint8_t p[64];f12_packet(p,3,4,9,55);memcpy(p+12,p+4,8);memset(p+4,0,8);
    f12_feed_packet(p);CHECK(point_state.state==POINT_SUPPRESSED && f12_actions==1);
    f12_reset(true);f12_feed(4,4,9,55);f12_packet(p,3,4,9,55);p[5]=255;p[6]=255;
    f12_feed_packet(p);CHECK(point_state.state==POINT_SUPPRESSED && f12_actions==1);
    return 0;
}
