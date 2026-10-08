typedef struct { unsigned x,y,tips,delay,axis; bool recover; unsigned pressure,minor; } sample_t;
static const sample_t *samples;
static unsigned sample_index, point_haptics, edge_haptics;
static void drain_sender(void) { sender_budget=64;ble_hid_task(NULL); }
static void drain_haptic(void)
{
    surface_haptic_event_t e;
    while(surface_runtime_pop(&haptic,now,&e)) {
        if(e.gesture) {
            if(e.pair.press_index==SURFACE_GESTURE_POINT_WAVE)++point_haptics;else ++edge_haptics;
        }
    }
}
static void capture_sample(void)
{
    drain_sender();drain_haptic();
    const sample_t *s=&samples[sample_index++];now+=s->delay?s->delay:10;
    if(s->recover)input_recover();
    uint8_t bytes[64]={0x40};
    for(unsigned i=0;i<5;++i) if(s->tips&(1U<<i)) {
        uint16_t raw_y=1532-s->y;
        unsigned off=4+8*i,x=s->x+100*i;
        bytes[off]=1;bytes[off+1]=x;bytes[off+2]=x>>8;bytes[off+3]=raw_y;bytes[off+4]=raw_y>>8;
        bytes[off+5]=s->pressure?s->pressure:20;bytes[off+6]=s->axis?s->axis:5;
        bytes[off+7]=s->minor?s->minor:bytes[off+6];
    }
    input_capture(bytes,true,input_source_generation(),input_generation(),now);
}
static void run_samples(const sample_t *data,unsigned size)
{
    samples=data;sample_index=0;parser_hook=capture_sample;parser_budget=size;
    i2c_queue_task(NULL);drain_sender();drain_haptic();
}
#define RUN(a) run_samples(a,sizeof(a)/sizeof(a[0]))
static void reset_ble(void)
{
    route_mutex=NULL;
    now=0;wifi_hook=NULL;raw_count=0;test_vbus=1;
    current_mode=BLE_MODE;current_tp_mode=MOUSE_MODE;reports=(report_buffer_t){0};
    ready_mask=0;mode_pending=false;mode_applied=true;requested_mode=MOUSE_MODE;request_serial=0;
    haptic=(surface_haptic_runtime_t){0};surface_runtime_state(&haptic,SURFACE_READY);
    device_config_defaults(&config);fail_mode=config_change=false;
    input_pipeline_init();reset_input_state();
    ble_input_connection(true,1);ble_input_subscription(1,true);ble_input_aux_subscription(1,AUX_OUTPUT_ALL);
    point_haptics=edge_haptics=send_count=0;
    const sample_t up[]={{0,0,0,10},{0,0,0,10}};RUN(up);send_count=0;
}
EXPORT int check_auto_ble_connection_manager_releases_before_restart(void)
{
    reset_ble();test_vbus=false;reset_reason=ESP_RST_SW;restart_count=0;
    handoff.magic=HANDOFF_MAGIC;handoff.inverse=~HANDOFF_MAGIC;
    handoff.mode=BLE_MODE;handoff.reason=HANDOFF_AUTO;handoff.high=0;handoff.wait_up=0;
    connection_init(0);connection_poll();input_apply_mode_request();
    ble_input_connection(true,1);ble_input_subscription(1,true);ble_input_aux_subscription(1,AUX_OUTPUT_ALL);
    drain_sender();CHECK(route==BLE_MODE&&links[BLE_MODE]&&!restart_count);
    input_report_t pressed={.mode=MOUSE_MODE,.time_ms=now};pressed.data.mouse.buttons=1;
    CHECK(input_publish(input_generation(),&pressed,false));drain_sender();
    CHECK(aux_output_hold(5,1,input_generation(),now));drain_sender();
    unsigned before=send_count;test_vbus=true;now+=10;connection_poll();now+=300;connection_poll();
    CHECK(retiring&&!restart_count&&transport_paused);
    drain_sender();now+=10;connection_poll();
    CHECK(restart_count==1&&handoff.mode==WIRED_MODE&&handoff.wait_up&&send_count>before);
    CHECK(input_transport_drained()&&aux_output_drained(false));
    return 0;
}
static unsigned usages(uint8_t id,uint8_t value)
{
    unsigned n=0;for(unsigned i=0;i<send_count&&i<512;++i)
        if(sent[i].id==id&&sent[i].data[id==8?2:0]==value)++n;
    return n;
}
EXPORT int check_auto_ble_aux_only_subscription_releases_before_restart(void)
{
    reset_ble();test_vbus=false;reset_reason=ESP_RST_SW;restart_count=0;
    handoff.magic=HANDOFF_MAGIC;handoff.inverse=~HANDOFF_MAGIC;
    handoff.mode=BLE_MODE;handoff.reason=HANDOFF_AUTO;handoff.high=0;handoff.wait_up=0;
    connection_init(0);connection_poll();input_apply_mode_request();
    ble_input_connection(true,1);ble_input_aux_subscription(1,AUX_OUTPUT_KEYBOARD);
    drain_sender();CHECK(!links[BLE_MODE]);
    CHECK(aux_output_hold(5,1,input_generation(),now));drain_sender();CHECK(usages(8,0x52)==1);
    unsigned releases=usages(8,0);
    test_vbus=true;now+=10;connection_poll();now+=300;connection_poll();
    CHECK(retiring&&!restart_count);
    drain_sender();now+=10;connection_poll();
    CHECK(restart_count==1&&handoff.mode==WIRED_MODE&&usages(8,0)>releases);
    return 0;
}
static bool quiet_mouse(void)
{
    for(unsigned i=0;i<send_count&&i<512;++i)
        if(sent[i].id==1&&(sent[i].data[0]||sent[i].data[1]||sent[i].data[2]))return false;
    return true;
}
EXPORT int check_ble_knuckle_shortcuts_no_clicks(void)
{
    for(unsigned rotation=0;rotation<4;++rotation) {
        reset_ble();config.bytes[CFG_ROTATION]=rotation;
        const sample_t pair[]={{500,600,1,10,3,false,45},{0,0,0,20},
                              {510,610,1,150,3,false,45},{0,0,0,20}};RUN(pair);
        CHECK(quiet_mouse()&&usages(8,0x46)==1&&usages(8,0)>=1&&!usages(8,0x15));
        CHECK(point_haptics==1);
        bool modifiers=false;
        for(unsigned i=0;i<send_count;++i)if(sent[i].id==8&&sent[i].data[2]==0x46)
            modifiers=sent[i].data[0]==0x08;
        CHECK(modifiers);
    }
    return 0;
}
EXPORT int check_ble_knuckle_corner_and_recovery(void)
{
    reset_ble();config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=14;
    const sample_t first[]={{0,0,1,10,3,false,45},{0,0,0,20}};RUN(first);
    CHECK(!usages(7,0xcd)&&quiet_mouse());
    const sample_t second[]={{0,0,1,150,3,false,45},{0,0,0,20}};RUN(second);
    CHECK(usages(8,0x46)==1&&!usages(7,0xcd)&&quiet_mouse());
    reset_ble();
    const sample_t interrupted[]={{0,0,1,10,3,false,45},{0,0,0,20},{0,0,0,10,0,true},
                                 {0,0,1,150,3,false,45},{0,0,0,20}};RUN(interrupted);
    CHECK(!usages(8,0x46));
    return 0;
}
EXPORT int check_ble_knuckle_low_pressure_preserves_click(void)
{
    reset_ble();
    const sample_t tap[]={{500,600,1,10,3,false,20},{0,0,0,20}};RUN(tap);
    unsigned clicks=0;
    for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[0]==1)++clicks;
    CHECK(clicks==1&&!usages(8,0x46)&&!point_haptics);
    const sample_t second[]={{500,600,1,150,3,false,20},{0,0,0,20}};RUN(second);
    CHECK(!usages(8,0x46)&&!usages(8,0x15)&&!point_haptics);
    return 0;
}
EXPORT int check_ble_knuckle_two_contacts_cancel(void)
{
    reset_ble();
    const sample_t double_finger[]={{500,600,3,10,3,false,45},{0,0,0,20},
                                   {500,600,3,150,3,false,45},{0,0,0,20}};RUN(double_finger);
    CHECK(!usages(8,0x46)&&!usages(8,0x15)&&!point_haptics);
    reset_ble();
    const sample_t interrupted[]={{500,600,1,10,3,false,45},{500,600,3,10,3,false,45},
                                 {500,600,1,10,3,false,45},{0,0,0,20},
                                 {500,600,1,150,3,false,45},{0,0,0,20}};RUN(interrupted);
    CHECK(!usages(8,0x46)&&!usages(8,0x15)&&!point_haptics);
    return 0;
}
static int mouse_axis_sum(unsigned axis)
{
    int total=0;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1)total+=(int8_t)sent[i].data[axis];
    return total;
}
static unsigned mouse_clicks(unsigned button)
{
    unsigned total=0;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[0]==button)++total;
    return total;
}
EXPORT int check_ble_knuckle_swipe_replays_original_path(void)
{
    int control_x=0,control_y=0;
    for(unsigned candidate=0;candidate<2;++candidate) {
        reset_ble();
        sample_t swipe[]={{500,600,1},{515,600,1},{540,600,1},{590,600,1},{690,600,1},{0,0,0}};
        for(unsigned i=0;i<5;++i) {swipe[i].axis=3;swipe[i].pressure=candidate?45:20;}
        RUN(swipe);
        CHECK(!usages(8,0x46)&&!mouse_clicks(1));
        if(!candidate) {control_x=mouse_axis_sum(1);control_y=mouse_axis_sum(2);CHECK(control_x!=0);}
        else CHECK(mouse_axis_sum(1)==control_x&&mouse_axis_sum(2)==control_y&&!knuckle_frame_count);
    }
    return 0;
}
EXPORT int check_ble_knuckle_prefix_preserves_four_edge_gestures(void)
{
    const sample_t paths[4][4]={
        {{1100,0,1},{1200,0,1},{1000,0,1},{0,0,0}},
        {{1100,1532,1},{1200,1532,1},{1000,1532,1},{0,0,0}},
        {{0,700,1},{0,600,1},{0,800,1},{0,0,0}},
        {{2302,700,1},{2302,600,1},{2302,800,1},{0,0,0}}};
    for(unsigned edge=0;edge<4;++edge) {
        reset_ble();uint8_t *cfg=config.bytes+CFG_EDGES+5*edge;cfg[0]=1;cfg[1]=edge+1;
        sample_t path[4];memcpy(path,paths[edge],sizeof(path));
        for(unsigned i=0;i<3;++i) {path[i].axis=3;path[i].pressure=45;}
        RUN(path);CHECK(quiet_mouse()&&!usages(8,0x46)&&!knuckle_frame_count);
        if(edge<2)CHECK(usages(7,edge==0?0x6f:0xe9)>0&&usages(7,edge==0?0x70:0xea)>0);
        else {bool wheel=false;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[edge==2?3:4])wheel=true;CHECK(wheel);}
    }
    return 0;
}
EXPORT int check_ble_knuckle_rejected_tap_replayed_once(void)
{
    reset_ble();
    const sample_t tap[]={{500,600,1,10,3,false,45},{500,600,1,10,5,false,75},{0,0,0,20},{0,0,0,10}};
    RUN(tap);CHECK(mouse_clicks(1)==1&&!usages(8,0x46)&&!knuckle_frame_count);
    return 0;
}
EXPORT int check_ble_knuckle_handoff_preserves_two_finger_scroll(void)
{
    int control=0;
    for(unsigned candidate=0;candidate<2;++candidate) {
        reset_ble();
        sample_t scroll[]={{500,600,1},{500,600,3},{500,650,3},{500,750,3},{500,850,3},{0,0,0}};
        for(unsigned i=0;i<5;++i) {scroll[i].axis=3;scroll[i].pressure=candidate?45:20;}
        RUN(scroll);CHECK(!usages(8,0x46)&&!mouse_clicks(1)&&!mouse_clicks(2));
        if(!candidate) {control=mouse_axis_sum(3);CHECK(control!=0);}
        else CHECK(mouse_axis_sum(3)==control&&!knuckle_frame_count);
    }
    return 0;
}
static void idle_parser_hook(void) { }
EXPORT int check_ble_knuckle_idle_timeout_and_scan_time(void)
{
    reset_ble();const sample_t hold[]={{500,600,1,10,3,false,45}};RUN(hold);
    CHECK(knuckle_frame_count==1&&quiet_mouse());
    now+=125;parser_hook=idle_parser_hook;parser_budget=1;i2c_queue_task(NULL);drain_sender();
    CHECK(!knuckle_frame_count&&m_state.tap_active&&!reports.recovering);
    /* A long hold must not collapse into a zero-duration click on replay. */
    const sample_t up[]={{0,0,0,500}};RUN(up);
    CHECK(!mouse_clicks(1)&&!usages(8,0x46));
    return 0;
}
EXPORT int check_ble_knuckle_buffer_capacity_and_recovery(void)
{
    reset_ble();sample_t dense[18]={0};
    for(unsigned i=0;i<17;++i)dense[i]=(sample_t){500,600,1,1,3,false,45};
    dense[17].delay=10;RUN(dense);
    CHECK(!knuckle_frame_count&&mouse_clicks(1)==1&&!usages(8,0x46)&&!reports.recovering);
    reset_ble();
    const sample_t interrupted[]={{500,600,1,10,3,false,45},{540,600,1,10,3,true,45},{0,0,0,20}};
    RUN(interrupted);CHECK(!knuckle_frame_count&&quiet_mouse()&&!usages(8,0x46));
    return 0;
}
EXPORT int check_ble_parser_four_edges(void)
{
    const sample_t swipe[4][4]={
        {{1100,0,1},{1200,0,1},{1000,0,1},{0,0,0}},
        {{1100,1532,1},{1200,1532,1},{1000,1532,1},{0,0,0}},
        {{0,700,1},{0,600,1},{0,800,1},{0,0,0}},
        {{2302,700,1},{2302,600,1},{2302,800,1},{0,0,0}}};
    for(unsigned edge=0;edge<4;++edge)for(unsigned invert=0;invert<2;++invert) {
        reset_ble();uint8_t *r=config.bytes+CFG_EDGES+5*edge;r[0]=1;r[1]=edge+1;r[2]=invert;
        run_samples(swipe[edge],4);CHECK(quiet_mouse());
        if(edge<2) {CHECK(usages(7,edge==0?0x6f:0xe9)>0&&usages(7,edge==0?0x70:0xea)>0);}
        else {
            int first=0,last=0;unsigned axis=edge==2?3:4;
            for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[axis]) {
                if(!first)first=(int8_t)sent[i].data[axis];last=(int8_t)sent[i].data[axis];
            }
            CHECK(first*(invert?-1:1)>0&&last*(invert?-1:1)<0);
        }
    }
    return 0;
}
EXPORT int check_ble_parser_four_corners_and_shortcuts(void)
{
    const unsigned x[]={0,2302,0,2302},y[]={0,0,1532,1532};
    const uint8_t actions[]={14,30,42,9},ids[]={7,8,8,8},keys[]={0xcd,0x3a,6,0x52};
    for(unsigned p=0;p<4;++p) {
        reset_ble();config.bytes[CFG_POINTS+4*p]=1;config.bytes[CFG_POINTS+4*p+1]=actions[p];
        sample_t tap[]={{x[p],y[p],1},{x[p],y[p],1},{0,0,0}};RUN(tap);
        CHECK(quiet_mouse()&&usages(ids[p],keys[p])==1&&usages(ids[p],0)>=1&&point_haptics==1);
        if(p==2) {bool ctrl=false;for(unsigned i=0;i<send_count;++i)if(sent[i].id==8&&sent[i].data[2]==6)ctrl=sent[i].data[0]==1;CHECK(ctrl);}
    }
    return 0;
}
EXPORT int check_ble_parser_hold_repeat_and_release(void)
{
    for(unsigned action=1;action<=2;++action) {
        reset_ble();config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=action==1?1:30;config.bytes[7]|=0x10;
        const sample_t hold[]={{0,0,1,10},{0,0,1,650},{0,0,1,220},{0,0,0,10}};RUN(hold);
        CHECK(quiet_mouse());
        CHECK(usages(action==1?7:8,action==1?0x6f:0x3a)==(action==1?1:3));
        CHECK(usages(action==1?7:8,0)>0);
    }
    return 0;
}
EXPORT int check_ble_parser_point_to_edge_no_extra_click(void)
{
    reset_ble();config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=14;
    config.bytes[CFG_SLEEP]|=2;config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
    const sample_t handoff[]={{0,0,1},{100,0,1,80},{180,0,1},{240,0,1},{0,0,0}};RUN(handoff);
    CHECK(quiet_mouse()&&usages(7,0xcd)==1);
    bool wheel=false;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[3])wheel=true;
    CHECK(wheel&&point_haptics==1&&edge_haptics>0);return 0;
}
EXPORT int check_ble_parser_rotation(void)
{
    /* Physical top-left maps to these logical corners under each rotation. */
    const unsigned corner[]={0,2,3,1};
    for(unsigned rotation=0;rotation<4;++rotation) {
        reset_ble();config.bytes[CFG_ROTATION]=rotation;
        config.bytes[CFG_POINTS+4*corner[rotation]]=1;config.bytes[CFG_POINTS+4*corner[rotation]+1]=42;
        const sample_t tap[]={{0,0,1},{0,0,0}};RUN(tap);
        CHECK(quiet_mouse()&&usages(8,6)==1);
    }
    return 0;
}
EXPORT int check_ble_parser_haptics_switch(void)
{
    for(unsigned enabled=0;enabled<2;++enabled) {
        reset_ble();if(!enabled)config.bytes[51]&=~CFG_FLAG_CUSTOM_GESTURE_HAPTICS;
        config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=14;
        config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        const sample_t sequence[]={{0,0,1},{0,0,0},{1100,0,1,100},{1200,0,1},{0,0,0}};RUN(sequence);
        CHECK(quiet_mouse()&&usages(7,0xcd)==1);
        CHECK(point_haptics==enabled&&edge_haptics==enabled);
    }
    return 0;
}
EXPORT int check_ble_parser_disabled_and_candidate_tap_fallback(void)
{
    for(unsigned enabled=0;enabled<2;++enabled) {
        reset_ble();config.bytes[CFG_EDGES]=enabled;config.bytes[CFG_EDGES+1]=3;
        const sample_t tap[]={{1100,0,1},{1100,0,1},{0,0,0}};RUN(tap);
        unsigned clicks=0;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[0]==1)++clicks;
        CHECK(clicks==1&&sent[send_count-1].id==1&&!sent[send_count-1].data[0]);
        CHECK(!point_haptics&&!edge_haptics);
    }
    return 0;
}
EXPORT int check_ble_parser_continue_outside_and_width(void)
{
    for(unsigned outside=0;outside<2;++outside) {
        reset_ble();config.bytes[CFG_EDGES]=1;config.bytes[CFG_EDGES+1]=3;
        config.bytes[CFG_EDGES+3]=10;config.bytes[CFG_EDGES+4]=2;
        config.bytes[CFG_EDGE_REPEAT]=outside;
        const sample_t start[]={{1100,100,1},{1130,100,1},{1200,100,1}};RUN(start);
        unsigned first=0;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[3])++first;
        CHECK(first==1&&quiet_mouse());send_count=0;
        const sample_t leave[]={{1300,400,1},{1400,400,1},{0,0,0}};RUN(leave);
        unsigned extra=0;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[3])++extra;
        CHECK(extra==(outside?2:0)&&quiet_mouse());
    }
    return 0;
}
EXPORT int check_ble_parser_reconnect_waits_for_lift(void)
{
    reset_ble();config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=14;config.bytes[7]|=0x10;
    const sample_t down[]={{0,0,1}};RUN(down);CHECK(usages(7,0xcd)==1);
    ble_input_connection(false,1);ble_input_connection(true,1);
    ble_input_subscription(1,true);ble_input_aux_subscription(1,AUX_OUTPUT_ALL);send_count=0;
    const sample_t held[]={{0,0,1,650},{0,0,1,220},{0,0,0}};RUN(held);
    CHECK(!usages(7,0xcd)&&quiet_mouse());
    RUN(down);CHECK(usages(7,0xcd)==1&&quiet_mouse());return 0;
}
EXPORT int check_ble_parser_corner_clears_double_tap_drag(void)
{
    reset_ble();
    const sample_t ordinary[]={{0,0,1},{0,0,0}};RUN(ordinary);
    bool click=false;for(unsigned i=0;i<send_count;++i)if(sent[i].id==1&&sent[i].data[0])click=true;CHECK(click);
    config.bytes[CFG_POINTS]=1;config.bytes[CFG_POINTS+1]=42;send_count=0;
    const sample_t corner[]={{0,0,1},{20,0,1},{20,0,1,200},{0,0,0}};RUN(corner);
    CHECK(quiet_mouse()&&usages(8,6)==1);return 0;
}


