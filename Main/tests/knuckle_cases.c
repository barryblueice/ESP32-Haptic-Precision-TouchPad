static void knock_packet(uint8_t p[64], unsigned tips, unsigned x, unsigned axis, unsigned pressure)
{
    memset(p,0,64);p[0]=0x40;
    for(unsigned i=0;i<5;++i)if(tips&(1U<<i)) {
        unsigned off=4+8*i,px=x+i*100;
        p[off]=1;p[off+1]=px;p[off+2]=px>>8;p[off+3]=0xf4;p[off+4]=1;
        p[off+5]=pressure;p[off+6]=p[off+7]=axis;
    }
}
static knuckle_result_t knock_sample_z(knuckle_gesture_t *s,unsigned tips,unsigned x,unsigned axis,unsigned z,uint32_t time)
{ uint8_t p[64];knock_packet(p,tips,x,axis,z);return knuckle_gesture_update(s,p,time); }
static knuckle_result_t knock_sample(knuckle_gesture_t *s,unsigned tips,unsigned x,unsigned axis,uint32_t time)
{ return knock_sample_z(s,tips,x,axis,45,time); }
EXPORT int check_knuckle_single_pairs_and_wrap(void)
{
    for(unsigned slot=0;slot<5;++slot)for(unsigned wrap=0;wrap<2;++wrap) {
        knuckle_gesture_t s={0};uint32_t t=wrap?0xfffffff0U:0;unsigned mask=1U<<slot;
        CHECK(knock_sample(&s,mask,500,3,t).suppress);
        knuckle_result_t r=knock_sample(&s,0,0,0,t+20);CHECK(r.suppress&&!r.screenshot);
        CHECK(knock_sample(&s,mask,520,3,t+150).suppress);
        r=knock_sample(&s,0,0,0,t+170);CHECK(r.suppress&&r.screenshot);
        CHECK(!knock_sample(&s,0,0,0,t+180).screenshot);
        CHECK(!knock_sample(&s,0,0,0,t+180).suppress);
        knock_sample(&s,mask,500,3,t+250);
        CHECK(!knock_sample(&s,0,0,0,t+270).screenshot);
    }
    return 0;
}
EXPORT int check_knuckle_learned_admission_and_unknowns(void)
{
    const unsigned cases[][3]={{45,3,1},{49,3,1},{70,3,0},{45,6,0},{20,3,0},{255,3,0},{45,0,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        knuckle_gesture_t s={0};unsigned z=cases[i][0],axis=cases[i][1];bool expected=cases[i][2];
        CHECK(knock_sample_z(&s,1,500,axis,z,0).suppress==expected);
        knock_sample(&s,0,0,0,20);knock_sample_z(&s,1,500,axis,z,150);
        CHECK(knock_sample(&s,0,0,0,170).screenshot==expected);
    }
    for(unsigned first=0;first<2;++first) {
        knuckle_gesture_t s={0};
        knock_sample_z(&s,1,500,3,first?45:20,0);knock_sample(&s,0,0,0,20);
        knock_sample_z(&s,1,500,3,first?20:45,150);
        CHECK(!knock_sample(&s,0,0,0,170).screenshot);
    }
    return 0;
}
EXPORT int check_knuckle_pressure_ramp_passthrough_and_decay(void)
{
    knuckle_gesture_t s={0};
    CHECK(!knock_sample_z(&s,1,500,3,20,0).suppress);
    CHECK(!knock_sample_z(&s,1,500,3,45,20).suppress);
    CHECK(!knock_sample(&s,0,0,0,30).screenshot&&!s.pending);
    CHECK(!knock_sample(&s,1,500,5,50).suppress);
    CHECK(!knock_sample(&s,1,500,3,70).suppress);
    CHECK(!knock_sample(&s,0,0,0,80).screenshot&&!s.pending);
    for(unsigned i=0;i<2;++i) {
        uint32_t t=100+i*150;
        CHECK(knock_sample(&s,1,500,3,t).suppress);
        CHECK(knock_sample_z(&s,1,500,3,1,t+10).suppress);
        CHECK(knock_sample(&s,0,0,0,t+20).screenshot==(i==1));
    }
    return 0;
}
EXPORT int check_knuckle_high_peak_rejected_at_lift(void)
{
    knuckle_gesture_t s={0};
    CHECK(!knock_sample_z(&s,1,500,3,255,0).suppress);
    CHECK(!knock_sample_z(&s,1,500,3,45,10).suppress);
    CHECK(!knock_sample(&s,0,0,0,20).screenshot&&!s.pending);
    CHECK(knock_sample(&s,1,500,3,100).suppress);
    CHECK(knock_sample_z(&s,1,500,3,255,110).suppress);
    CHECK(knock_sample(&s,1,500,3,120).suppress);
    CHECK(!knock_sample(&s,0,0,0,130).screenshot&&!s.pending);
    knock_sample(&s,1,500,3,200);
    CHECK(!knock_sample(&s,0,0,0,220).screenshot);return 0;
}
EXPORT int check_knuckle_cancel_replays_once(void)
{
    for(unsigned fault=0;fault<6;++fault) {
        knuckle_gesture_t s={0};knock_sample(&s,1,500,3,0);
        knuckle_result_t r;
        if(fault==0)r=knock_sample(&s,1,570,3,20);
        else if(fault==1)r=knock_sample(&s,1,500,16,20);
        else if(fault==2)r=knock_sample(&s,3,500,3,20);
        else if(fault==3)r=knock_sample(&s,1,500,3,80);
        else if(fault==4)r=knock_sample(&s,2,400,3,20);
        else {knock_sample(&s,1,500,3,50);knock_sample(&s,1,500,3,100);r=knock_sample(&s,1,500,3,130);}
        CHECK(!r.screenshot);
        CHECK(fault==1 ? r.suppress&&!r.replay : !r.suppress&&r.replay);
        r=knock_sample(&s,1,500,3,140);
        CHECK(!r.suppress&&r.replay==(fault==1));
        r=knock_sample(&s,0,0,0,150);CHECK(!r.suppress&&!r.replay&&!r.screenshot&&!s.pending);
        knock_sample(&s,1,500,3,220);CHECK(!knock_sample(&s,0,0,0,240).screenshot);
    }
    return 0;
}
EXPORT int check_knuckle_failed_classification_replays_and_recovery_discards(void)
{
    knuckle_gesture_t s={0};knock_sample(&s,1,500,3,0);
    knock_sample_z(&s,1,500,3,75,10);
    knuckle_result_t r=knock_sample(&s,0,0,0,30);
    CHECK(r.replay&&!r.suppress&&!r.screenshot&&!s.pending);
    knock_sample(&s,1,500,3,150);
    s.rejected=s.cancelled=true;
    r=knock_sample(&s,1,600,3,170);CHECK(r.suppress&&!r.replay);
    r=knock_sample(&s,0,0,0,180);CHECK(r.suppress&&!r.replay&&!r.screenshot);
    return 0;
}
EXPORT int check_knuckle_pair_rejections_and_reset(void)
{
    for(unsigned fault=0;fault<7;++fault) {
        knuckle_gesture_t s={0};knock_sample(&s,1,500,3,0);knock_sample(&s,0,0,0,20);
        uint32_t t=fault==0?50:fault==1?400:150;
        if(fault==4)knuckle_gesture_reset(&s);
        if(fault==5){knock_sample_z(&s,1,500,3,20,70);knock_sample(&s,0,0,0,90);}
        knock_sample(&s,fault==2?3:1,fault==3?900:500,3,t);
        CHECK(!knock_sample(&s,0,0,0,t+(fault==6?1:20)).screenshot);
    }
    knuckle_gesture_t s={0};knock_sample(&s,1,500,3,0);knock_sample(&s,0,0,0,20);
    knock_sample(&s,1,500,3,370);CHECK(knock_sample(&s,0,0,0,390).screenshot);
    return 0;
}
EXPORT int check_knuckle_multiple_contacts_never_trigger(void)
{
    for(unsigned tips=1;tips<32;++tips)if(tips&(tips-1)) {
        knuckle_gesture_t s={0};
        CHECK(!knock_sample(&s,tips,500,3,0).suppress);
        CHECK(!knock_sample(&s,0,0,0,20).screenshot&&!s.pending);
        CHECK(!knock_sample(&s,tips,500,3,150).suppress);
        CHECK(!knock_sample(&s,0,0,0,170).screenshot&&!s.pending);
    }
    return 0;
}
EXPORT int check_knuckle_shortcut_output_and_radio(void)
{
    aux_output_reset(false);aux_output_report_t r;
    CHECK(!aux_output_once(49,1,test_generation,0));
    CHECK(aux_output_once(48,1,test_generation,0));
    CHECK(aux_output_take(&r,test_generation,0)&&r.id==8&&!r.release);
    CHECK(r.data[0]==0x08&&r.data[2]==0x46);
    aux_output_complete(true);
    CHECK(aux_output_take(&r,test_generation,1)&&r.id==8&&r.release);
    for(unsigned i=0;i<8;++i)CHECK(!r.data[i]);
    aux_output_complete(true);CHECK(!aux_output_active());
    wire_action_t w={123,1,48,1,false},out;uint8_t packet[38];
    wire_action_encode(packet,&w);CHECK(wire_action_decode(packet,38,&out)&&out.action==48);
    packet[12]=49;CHECK(!wire_action_decode(packet,38,&out));packet[12]=48;
    packet[15]=1;CHECK(!wire_action_decode(packet,38,&out));
    aux_output_reset(false);CHECK(aux_output_once(48,1,test_generation,0));
    CHECK(aux_output_take(&r,test_generation,0));aux_output_complete(false);
    CHECK(aux_output_take(&r,test_generation,1)&&r.release);aux_output_complete(true);
    CHECK(!aux_output_active());
    return 0;
}


