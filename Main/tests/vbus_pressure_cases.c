/* VBUS selects a route; pressure always follows the selected transport. */
EXPORT int check_transport_thresholds_independent_of_vbus(void)
{
    const unsigned transports[]={WIRED_MODE,BLE_MODE,_2_4_MODE};
    const unsigned thresholds[2][3]={{60,80,100},{80,100,130}};
    for(unsigned mode=0;mode<3;++mode)for(unsigned high=0;high<2;++high)for(unsigned level=1;level<=3;++level) {
        reset_test(transports[mode],PTP_MODE);test_vbus=high;ptp_button_press_threshold=level;
        unsigned t=thresholds[transports[mode]==WIRED_MODE][level-1];
        const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(t-1),TOUCH(t-1),TOUCH(t-1),UP,
            TOUCH(t),TOUCH(t),TOUCH(t),TOUCH(t),TOUCH(t),UP};
        RUN(sequence);CHECK(played_press==1&&played_release==1&&host_down==1&&host_up==1);
        CHECK(ptp_map_button_press_threshold(level)==t);
    }
    return 0;
}
EXPORT int check_wireless_100_is_not_255(void)
{
    reset_test(_2_4_MODE,PTP_MODE);config.bytes[48]=config.bytes[49]=config.bytes[50]=100;
    const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(99),TOUCH(99),TOUCH(99),UP,
        TOUCH(100),TOUCH(100),TOUCH(100),TOUCH(100),UP};
    RUN(sequence);CHECK(played_press==1&&host_down==1&&played_release==1);
    CHECK(ptp_map_button_press_threshold(2)==100);
    return 0;
}
EXPORT int check_vbus_alone_does_not_reset_manual_input(void)
{
    reset_test(_2_4_MODE,PTP_MODE);
    const step_t sequence[]={EVENT(LINK,3),UP,TOUCH(150),TOUCH(150),TOUCH(150),
        EVENT(VBUS,0),TOUCH(150),TOUCH(150),EVENT(VBUS,1),TOUCH(150),UP};
    RUN(sequence);CHECK(played_press==1&&played_release==1&&host_down==1&&host_up==1);
    return 0;
}
