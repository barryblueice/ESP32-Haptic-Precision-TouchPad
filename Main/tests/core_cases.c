static tp_multi_msg_t contact(unsigned x,unsigned y)
{
    tp_multi_msg_t m={0};m.fingers[0]=(tp_finger_t){.tip_switch=1,.confidence=1,.x=x,.y=y};return m;
}
static device_config_t configured(void)
{
    device_config_t c;device_config_defaults(&c);
    for(unsigned i=0;i<4;++i){c.bytes[32+i*5]=1;c.bytes[33+i*5]=3;}
    return c;
}
EXPORT int check_config_masks(void)
{
    device_config_t c;device_config_defaults(&c);CHECK(sizeof(c)==52);
    for(unsigned mask=0;mask<16;++mask)for(unsigned sleep=0;sleep<2;++sleep){
        c.bytes[6]=sleep|(mask<<1);CHECK(device_config_valid(&c));CHECK(device_config_sleep(&c)==(bool)sleep);
        for(unsigned p=0;p<4;++p)CHECK(device_config_point_to_edge(&c,p)==((mask&(1U<<p))!=0));
    }
    for(unsigned bit=5;bit<8;++bit){c.bytes[6]=1U<<bit;CHECK(!device_config_valid(&c));}
    c=configured();for(unsigned i=0;i<4;++i){
        unsigned b=32+i*5;c.bytes[b+1]=13;CHECK(!device_config_valid(&c));c.bytes[b+1]=3;
        c.bytes[b+3]=0;CHECK(!device_config_valid(&c));
        for(unsigned radius=1;radius<=30;++radius){c.bytes[b+3]=radius;CHECK(device_config_valid(&c));}
        c.bytes[b+3]=31;CHECK(!device_config_valid(&c));c.bytes[b+3]=30;
        c.bytes[b+4]=0;CHECK(!device_config_valid(&c));c.bytes[b+4]=10;
        for(unsigned reserved=1;reserved<=255;++reserved){
            c.bytes[b+2]=reserved;CHECK(!device_config_valid(&c));
        }
        c.bytes[b+2]=0;
    }
    return 0;
}
EXPORT int check_capabilities(void)
{
    device_config_t a,b;device_config_defaults(&a);b=a;
    CHECK(!(rstp_capabilities_normalize(0x2ff)&0x200));
    CHECK(!(rstp_capabilities_normalize(0x3df)&0x2c0));
    b.bytes[32]=1;b.bytes[33]=9;b.bytes[7]=0x10;
    CHECK(device_config_supported(&a,&b,0x100));CHECK(!device_config_supported(&a,&b,0xff));
    b.bytes[6]|=2;CHECK(!device_config_supported(&a,&b,0x1ff));CHECK(device_config_supported(&a,&b,0x3ff));
    b.bytes[32]=0;CHECK(device_config_valid(&b));CHECK(device_config_point_to_edge(&b,0));
    return 0;
}
EXPORT int check_storage_records(void)
{
    device_config_t c=configured(),out;uint8_t record[60];c.bytes[6]=0x0b;c.bytes[7]=0xa5;
    for(unsigned i=0;i<4;++i)c.bytes[35+i*5]=30;
    device_config_store_record(record,&c);CHECK(device_config_load_record(&out,record,60));CHECK(!memcmp(&c,&out,52));
    record[4]=1;record[6]=32;record[14]=1;record[15]=5;
    CHECK(device_config_load_record(&out,record,40));CHECK(out.bytes[6]==1&&out.bytes[7]==5);
    for(unsigned i=32;i<52;i+=5)CHECK(!out.bytes[i]&&out.bytes[i+3]==5&&out.bytes[i+4]==2);
    record[14]=3;CHECK(!device_config_load_record(&out,record,40));record[14]=1;
    record[4]=3;CHECK(!device_config_load_record(&out,record,40));return 0;
}
EXPORT int check_protocol_errors(void)
{
    uint8_t b[64]={0};memcpy(b,"RSTP",4);b[4]=1;b[5]=3;b[6]=1;b[8]=52;
    device_config_t c=configured();for(unsigned i=0;i<4;++i)c.bytes[35+i*5]=30;
    memcpy(b+12,c.bytes,52);rstp_request_t r;
    CHECK(rstp_decode(b,64,&r)&&!r.status);b[8]=32;CHECK(rstp_decode(b,64,&r)&&r.status==RSTP_LENGTH);
    b[8]=52;b[18]=0x20;CHECK(rstp_decode(b,64,&r)&&r.status==RSTP_INVALID);
    b[4]=2;CHECK(rstp_decode(b,64,&r)&&r.status==RSTP_VERSION);
    b[6]=0;CHECK(!rstp_decode(b,64,&r));return 0;
}
EXPORT int check_vector(const uint8_t *bytes)
{
    rstp_request_t r;CHECK(rstp_decode(bytes,64,&r)&&r.status==0);
    CHECK(r.config.bytes[6]==0x0b&&r.config.bytes[7]==0xa5);
    CHECK(device_config_point_to_edge(&r.config,0)&&!device_config_point_to_edge(&r.config,1));
    CHECK(device_config_point_to_edge(&r.config,2)&&!device_config_point_to_edge(&r.config,3));return 0;
}
EXPORT int check_geometry(void)
{
    for(unsigned p=0;p<4;++p){
        unsigned x=(p&1)?2000:0,y=(p&2)?1000:0;
        CHECK(point_gesture_inside(p,10,x,y,2000,1000,200,100));
        unsigned dx=(p&1)?x-100:x+100;
        CHECK(point_gesture_inside(p,10,dx,y,2000,1000,200,100));
        CHECK(!point_gesture_inside(p,10,dx,(p&2)?y-1:y+1,2000,1000,200,100));
        dx=(p&1)?x-300:x+300;
        CHECK(point_gesture_inside(p,30,dx,y,2000,1000,200,100));
        CHECK(!point_gesture_inside(p,30,(p&1)?dx-1:dx+1,y,2000,1000,200,100));
    }
    CHECK(!point_gesture_inside(0,15,0,1001,2000,1000,200,100));
    CHECK(!point_gesture_inside(0,15,2001,0,2000,1000,200,100));
    CHECK(point_gesture_inside(0,10,50,0,1000,2000,100,200));
    /* Unequal coordinate scales still describe the same physical circle. */
    CHECK(point_gesture_inside(0,10,100,0,2000,1000,200,100));
    CHECK(!point_gesture_inside(0,10,101,0,2000,1000,200,100));return 0;
}
EXPORT int check_actions_and_repeat(void)
{
    for(unsigned a=1;a<=12;++a){
        device_config_t c=configured();c.bytes[33]=a;c.bytes[7]=0x10;
        point_gesture_t s={0};tp_multi_msg_t m=contact(0,0);
        point_result_t r=point_gesture_update(&s,&c,&m,2302,1532,1149,766,100);
        CHECK(r.suppress&&r.action==(a+1)/2&&r.steps==((a&1)?1:-1));
        CHECK(!point_gesture_tick(&s,499).steps);CHECK(point_gesture_tick(&s,500).steps==r.steps);
        CHECK(!point_gesture_tick(&s,599).steps);CHECK(point_gesture_tick(&s,600).steps==r.steps);
        CHECK(point_gesture_tick(&s,2000).steps==r.steps);CHECK(!point_gesture_tick(&s,2000).steps);
        m=(tp_multi_msg_t){0};CHECK(point_gesture_update(&s,&c,&m,2302,1532,1149,766,2001).cancel);
        CHECK(!point_gesture_tick(&s,3000).steps);
    }return 0;
}
EXPORT int check_conversion_independence(void)
{
    for(unsigned mask=0;mask<16;++mask)for(unsigned p=0;p<4;++p){
        device_config_t c=configured();c.bytes[6]=1|(mask<<1);c.bytes[7]=0xf0;
        point_gesture_t s={0};tp_multi_msg_t m=contact((p&1)?2000:0,(p&2)?1000:0);
        CHECK(point_gesture_update(&s,&c,&m,2000,1000,200,100,0).steps);
        m.fingers[0].x=(p&1)?1961:39;
        CHECK(!point_gesture_update(&s,&c,&m,2000,1000,200,100,1).handoff);
        m.fingers[0].x=(p&1)?1960:40;
        point_result_t r=point_gesture_update(&s,&c,&m,2000,1000,200,100,2);
        CHECK(r.handoff==((mask&(1U<<p))!=0));
        if(r.handoff)CHECK(!point_gesture_tick(&s,500).steps);
        else {m.fingers[0].x=(p&1)?0:2000;CHECK(point_gesture_update(&s,&c,&m,2000,1000,200,100,3).suppress);}
    }
    device_config_t c=configured();c.bytes[6]=31;point_gesture_t s={0};tp_multi_msg_t m=contact(1000,500);
    CHECK(!point_gesture_update(&s,&c,&m,2000,1000,200,100,0).suppress);
    m=contact(0,0);CHECK(!point_gesture_update(&s,&c,&m,2000,1000,200,100,10).steps);
    return 0;
}
EXPORT int check_cancel_and_time_wrap(void)
{
    device_config_t c=configured();c.bytes[7]=0x10;point_gesture_t s={0};tp_multi_msg_t m=contact(0,0);
    point_gesture_update(&s,&c,&m,2000,1000,200,100,0xffffff00U);
    CHECK(!point_gesture_tick(&s,0x8f).steps);CHECK(point_gesture_tick(&s,0x90).steps);
    m.fingers[1]=m.fingers[0];point_result_t r=point_gesture_update(&s,&c,&m,2000,1000,200,100,150);
    CHECK(r.suppress&&r.cancel);CHECK(!point_gesture_tick(&s,1000).steps);
    m.fingers[1].tip_switch=0;CHECK(point_gesture_update(&s,&c,&m,2000,1000,200,100,2000).suppress);return 0;
}
EXPORT int check_edge_handoff(void)
{
    device_config_t c=configured();c.bytes[6]=3;c.bytes[12]=1;c.bytes[13]=2;
    point_gesture_t p={0};edge_gesture_t e={0};tp_multi_msg_t m=contact(0,0);
    point_gesture_update(&p,&c,&m,2000,1000,200,100,0);m.fingers[0].x=40;
    CHECK(point_gesture_update(&p,&c,&m,2000,1000,200,100,1).handoff);
    CHECK(!edge_gesture_update(&e,&c,&m,2000,1000).steps);m.fingers[0].x=80;
    edge_result_t r=edge_gesture_update(&e,&c,&m,2000,1000);CHECK(r.action==2&&r.steps==1);return 0;
}
EXPORT int check_aux_release_and_cancel(void)
{
    aux_output_report_t r;aux_output_reset(false);aux_output_cancel();
    CHECK(aux_output_steps(5,1,test_generation,0));CHECK(aux_output_take(&r,test_generation,0));
    CHECK(r.id==8&&r.data[2]==0x52);aux_output_cancel();aux_output_complete(true);
    CHECK(aux_output_take(&r,test_generation,1)&&r.release&&r.id==8);CHECK(!r.data[2]);aux_output_complete(true);
    CHECK(!aux_output_take(&r,test_generation,1));
    CHECK(aux_output_steps(2,1,test_generation,0));CHECK(!aux_output_take(&r,test_generation,101));
    for(unsigned i=0;i<32;++i)CHECK(aux_output_steps(1,1,test_generation,200));
    CHECK(!aux_output_steps(1,1,test_generation,200));return 0;
}
EXPORT int check_wire_protocol(void)
{
    uint8_t b[38];wire_surface_t s={1,3,123},out;
    wire_surface_encode(b,WIRE_SURFACE,&s);CHECK(wire_surface_decode(b,38,WIRE_SURFACE,&out)&&out.rotation==3);
    b[10]=1;CHECK(!wire_surface_decode(b,38,WIRE_SURFACE,&out));
    wire_action_t a={123,1,6,-1},action;wire_action_encode(b,&a);CHECK(wire_action_decode(b,38,&action)&&action.steps==-1);
    CHECK(!wire_action_decode(b,37,&action));b[12]=7;CHECK(!wire_action_decode(b,38,&action));return 0;
}
EXPORT int check_fast_lift_preserves_first_action(void)
{
    aux_output_reset(false);aux_output_report_t r;
    CHECK(aux_output_once(2,1,test_generation,0));
    CHECK(aux_output_steps(2,1,test_generation,0));
    aux_output_cancel_gesture();
    CHECK(aux_output_take(&r,test_generation,1)&&r.data[0]==0xe9);
    /* A lift racing a submitted first action must not duplicate it. */
    aux_output_cancel_gesture();aux_output_complete(true);
    CHECK(aux_output_take(&r,test_generation,2)&&r.release);aux_output_complete(true);
    CHECK(!aux_output_take(&r,test_generation,3));return 0;
}
EXPORT int check_all_rotations(void)
{
    initialized=true;device_config_defaults(&active);
    for(unsigned rotation=0;rotation<4;++rotation) {
        active.bytes[5]=rotation;
        uint16_t xmax=device_config_x_max(),ymax=device_config_y_max();
        bool seen[4]={0};
        for(unsigned raw=0;raw<4;++raw) {
            uint16_t x,y;tp_rotate_coordinates((raw&1)?2302:0,(raw&2)?1532:0,&x,&y);
            unsigned corner=(x==xmax?1:0)|(y==ymax?2:0);
            CHECK(!seen[corner]);seen[corner]=true;
            CHECK(point_gesture_inside(corner,5,x,y,xmax,ymax,rotation&1?766:1149,rotation&1?1149:766));
        }
    }
    return 0;
}
EXPORT int check_migration_commit_failure(void)
{
    device_config_t c;device_config_defaults(&c);c.bytes[0]=72;
    device_config_store_record(nvs_blob,&c);nvs_blob[4]=1;nvs_blob[6]=32;nvs_size=40;
    fail_commit=true;CHECK(device_config_init()==ESP_OK);device_config_t out;device_config_get(&out);
    CHECK(out.bytes[0]==72&&out.bytes[6]==1&&nvs_size==40&&nvs_blob[4]==1);
    fail_commit=false;CHECK(device_config_init()==ESP_OK);CHECK(nvs_size==60&&nvs_blob[4]==2);
    device_config_get(&out);CHECK(out.bytes[0]==72&&out.bytes[35]==5);return 0;
}
EXPORT int check_save_failure(void)
{
    nvs_size=0;fail_commit=false;CHECK(device_config_init()==ESP_OK);
    device_config_t before,next;device_config_get(&before);next=before;next.bytes[6]=0x0b;
    fail_commit=true;CHECK(device_config_save(&next)==RSTP_STORAGE);
    device_config_t after;device_config_get(&after);CHECK(!memcmp(&before,&after,52));
    CHECK(!halted&&!saved_restart);fail_commit=false;
    CHECK(device_config_save(&next)==RSTP_RESTART);CHECK(halted&&nvs_size==60&&nvs_blob[14]==0x0b);
    CHECK(device_config_load_record(&after,nvs_blob,nvs_size)&&!memcmp(&after,&next,52));return 0;
}
