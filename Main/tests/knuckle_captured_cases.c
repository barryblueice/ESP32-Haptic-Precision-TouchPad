/* User-labelled traces, 2026-09-27. Times are relative to first tip-down.
 * Only slot 0 is active; remaining packet bytes in the supplied logs are 0.
 * The repeated knock below is a synthetic pair, not a captured double knock. */
typedef struct { uint32_t dt; uint16_t x,y; uint8_t tip,z,a,b; } captured_knock_frame_t;
static const captured_knock_frame_t captured_knock[] = {
    {0,876,557,1,45,2,4}, {7,876,557,1,48,2,4}, {14,876,557,1,48,2,4},
    {22,876,557,1,48,3,4}, {29,876,557,1,47,3,4}, {36,856,557,1,39,5,3}, {43,0,0,0,0,0,0}
};
static const captured_knock_frame_t captured_light_1[] = {
    {0,1117,688,1,66,6,4}, {7,1113,688,1,65,6,5}, {14,1111,689,1,63,5,4},
    {21,1111,689,1,61,5,4}, {29,1111,689,1,56,5,4}, {36,1111,689,1,45,5,4}, {43,0,0,0,0,0,0}
};
static const captured_knock_frame_t captured_light_2[] = {
    {0,1062,655,1,72,5,6}, {7,1062,655,1,73,5,6}, {14,1062,655,1,73,5,7},
    {22,1062,655,1,74,5,7}, {29,1062,655,1,76,6,7}, {36,1062,655,1,77,6,7},
    {43,1062,655,1,76,6,7}, {51,1062,655,1,75,6,7}, {58,1062,655,1,73,5,7},
    {65,1062,655,1,66,5,6}, {72,1062,655,1,47,5,5}, {80,0,0,0,0,0,0}
};
static knuckle_result_t captured_knock_step(knuckle_gesture_t *s,const captured_knock_frame_t *f,uint32_t base)
{
    uint8_t packet[64]={0x40,0,0x0c,0x18};
    packet[4]=f->tip;packet[5]=f->x;packet[6]=f->x>>8;packet[7]=f->y;packet[8]=f->y>>8;
    packet[9]=f->z;packet[10]=f->a;packet[11]=f->b;
    return knuckle_gesture_update(s,packet,base+f->dt);
}
EXPORT int check_captured_knock_and_synthetic_pair(void)
{
    knuckle_gesture_t s={0};
    for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<7;++i) {
        knuckle_result_t r=captured_knock_step(&s,captured_knock+i,14356+200*pass);
        CHECK(r.suppress&&r.screenshot==(pass==1&&i==6));
    }
    CHECK(!s.pending);return 0;
}
EXPORT int check_captured_light_touches_pass_through(void)
{
    const captured_knock_frame_t *traces[]={captured_light_1,captured_light_2};
    const unsigned sizes[]={7,12};
    for(unsigned n=0;n<2;++n) {
        knuckle_gesture_t s={0};
        /* Replay twice at a valid double-knock spacing: still no screenshot. */
        for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<sizes[n];++i) {
            knuckle_result_t r=captured_knock_step(&s,traces[n]+i,1000+200*pass);
            CHECK(!r.suppress&&!r.screenshot&&!s.pending);
        }
    }
    return 0;
}
