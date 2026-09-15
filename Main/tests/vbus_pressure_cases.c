EXPORT int check_vbus_raw_thresholds_all_transports(void)
{
    const unsigned transports[]={WIRED_MODE,BLE_MODE,_2_4_MODE};
    const unsigned thresholds[2][3]={{20,35,70},{80,100,130}};
    for(unsigned mode=0;mode<3;++mode)for(unsigned high=0;high<2;++high)for(unsigned level=1;level<=3;++level) {
        reset_test(transports[mode],PTP_MODE);test_vbus=high;ptp_button_press_threshold=level;
        unsigned t=thresholds[high][level-1];
        const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(t-1),TOUCH(t-1),TOUCH(t-1),UP,
            TOUCH(t),TOUCH(t),TOUCH(t),TOUCH(t),TOUCH(t),UP};
        RUN(sequence);CHECK(played_press==1&&played_release==1&&host_down==1&&host_up==1);
        CHECK(ptp_map_button_press_threshold(level)==t);
    }
    return 0;
}
EXPORT int check_vbus_100_is_not_255(void)
{
    reset_test(_2_4_MODE,PTP_MODE);test_vbus=0;config.bytes[48]=config.bytes[49]=config.bytes[50]=100;
    const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(99),TOUCH(99),TOUCH(99),UP,
        TOUCH(100),TOUCH(100),TOUCH(100),TOUCH(100),UP};
    RUN(sequence);CHECK(played_press==1&&host_down==1&&played_release==1);
    CHECK(ptp_map_button_press_threshold(2)==100);
    reset_test(_2_4_MODE,PTP_MODE);test_vbus=0;config.bytes[48]=config.bytes[49]=config.bytes[50]=1;
    const step_t minimum[]={EVENT(LINK,3),UP,TOUCH(0),TOUCH(0),TOUCH(0),UP,
        TOUCH(2),TOUCH(2),TOUCH(2),UP};
    RUN(minimum);CHECK(played_press==1&&host_down==1&&played_release==1);
    return 0;
}
EXPORT int check_vbus_change_releases_drag_and_waits_for_lift(void)
{
    const unsigned transports[]={WIRED_MODE,BLE_MODE,_2_4_MODE};
    for(unsigned transport=0;transport<3;++transport)for(unsigned initial=0;initial<2;++initial) {
        reset_test(transports[transport],PTP_MODE);test_vbus=initial;
        const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(150),TOUCH(150),TOUCH(150),AT(150,1300,700),
            EVENT(VBUS,!initial),TOUCH(150),TOUCH(150),TOUCH(150),TOUCH(150),TOUCH(150),
            TOUCH(150),TOUCH(150),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP};
        RUN(sequence);CHECK(played_press==2&&played_release==1&&host_down==2&&!last_buttons);
        CHECK(pressure_vbus_high==!initial&&cancellations==(transports[transport]==_2_4_MODE?1:2)); /* USB/BLE also reset on link startup. */
        CHECK(!haptic.down&&!source_wait_up&&!output_wait_up);
    }
    return 0;
}
EXPORT int check_vbus_lowering_does_not_click_existing_contact(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(84),TOUCH(84),TOUCH(84),EVENT(VBUS,0),
        TOUCH(84),TOUCH(84),TOUCH(84),TOUCH(84),TOUCH(84),UP,
        TOUCH(84),TOUCH(84),TOUCH(84),UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&host_down==1);
    return 0;
}
EXPORT int check_vbus_corner_cancellation(void)
{
    reset_test(_2_4_MODE,PTP_MODE);config.bytes[32]=1;config.bytes[33]=9;config.bytes[7]=0x10;
    const step_t sequence[]={EVENT(LINK,3),UP,AT(150,0,0),EVENT(VBUS,0),AT(150,0,0),
        AT(150,0,0),AT(150,0,0),AT(150,0,0),EVENT(AGE,1000),AT(150,0,0),UP,AT(150,0,0),UP};
    RUN(sequence);CHECK(host_actions==2&&!played_press&&!host_down&&!aux_output_active());
    return 0;
}
EXPORT int check_vbus_debounce_idle_and_time_wrap(void)
{
    reset_test(_2_4_MODE,PTP_MODE);pressure_vbus_init();
    uint32_t source=input_source_generation();
    test_vbus=0;now=10;pressure_vbus_poll();CHECK(pressure_vbus_high);
    now=20;test_vbus=1;pressure_vbus_poll();
    now=30;test_vbus=0;pressure_vbus_poll();
    now=59;pressure_vbus_poll();CHECK(pressure_vbus_high);
    now=60;pressure_vbus_poll();CHECK(pressure_vbus_high); /* rate limited */
    now=69;pressure_vbus_poll();CHECK(!pressure_vbus_high&&input_source_generation()==source+1);
    now=200;pressure_vbus_poll();CHECK(input_source_generation()==source+1);
    now=0xfffffff0U;test_vbus=1;pressure_vbus_init();
    test_vbus=0;now=0xfffffffaU;pressure_vbus_poll();now=23;pressure_vbus_poll();CHECK(pressure_vbus_high);
    now=33;pressure_vbus_poll();CHECK(!pressure_vbus_high);
    /* No incoming frames: the parser's timed wait still detects supply changes. */
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t idle[]={EVENT(LINK,3),UP,EVENT(VBUS,0),EVENT(AGE,0),EVENT(AGE,0),EVENT(AGE,0),
        UP,TOUCH(35),TOUCH(35),TOUCH(35),UP};
    RUN(idle);CHECK(!pressure_vbus_high&&played_press==1&&host_down==1);
    return 0;
}
EXPORT int check_vbus_sleep_zero_and_fault_guards(void)
{
    reset_test(_2_4_MODE,PTP_MODE);test_vbus=0;
    const step_t sequence[]={EVENT(LINK,3),UP,EVENT(STRENGTH,0),TOUCH(84),TOUCH(84),TOUCH(84),UP,
        EVENT(STRENGTH,63),EVENT(SLEEP,0),EVENT(VBUS,1),EVENT(AGE,0),EVENT(AGE,0),EVENT(AGE,0),
        EVENT(READY,0),UP,TOUCH(150),TOUCH(150),TOUCH(150),UP,
        EVENT(FAULT,0),TOUCH(150),TOUCH(150),TOUCH(150),UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&haptic.state==SURFACE_FAULT&&pressure_vbus_high);
    return 0;
}
