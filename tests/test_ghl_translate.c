#include "../bridge/ghl_translate.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

static uint8_t raw[64], ps3[27];
static void reset(void){
    memset(raw,0,sizeof(raw));
    raw[0]=0x01; raw[2]=0x80; raw[3]=0x80; raw[4]=0x80;
    raw[5]=0x08; /* neutral D-pad on PS4 */
    memset(ps3,0xEE,sizeof(ps3));
}
static void expected(int value, const char *label){
    (void)label;assert(ghl_ps4_to_ps3(raw,sizeof(raw),ps3)==0);
    assert(ps3[0]==value);
}
static void test_frets(void){
    const struct {unsigned pos;uint8_t mask;unsigned bit;} cases[] = {
        {5,0x10,0x01}, {5,0x20,0x02}, {5,0x40,0x04},
        {5,0x80,0x08}, {6,0x01,0x10}, {6,0x02,0x20}
    };
    unsigned i;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        reset();raw[cases[i].pos]|=cases[i].mask;
        expected((int)cases[i].bit,"single fret");
    }
    reset();raw[5]|=0xf0;raw[6]|=0x03;
    expected(0x3f,"all six frets");
}
static void test_buttons(void){
    reset();raw[6]=0xe0;raw[7]=0x01;
    assert(ghl_ps4_to_ps3(raw,sizeof(raw),ps3)==0);
    assert(ps3[1] == 0x17); /* hero+start+GHTV+center */
}
static void test_axes(void){
    reset();assert(ghl_ps4_to_ps3(raw,sizeof(raw),ps3)==0);
    assert(ps3[2]==0x0f && ps3[3]==0x80 && ps3[4]==0x80);
    assert(ps3[5]==0x80 && ps3[6]==0x80);
    assert(ps3[19]==0x00 && ps3[20]==0x02);
    raw[2]=0;raw[4]=0xff;raw[3]=0xff;raw[5]=0x04;
    assert(ghl_ps4_to_ps3(raw,sizeof(raw),ps3)==0);
    assert(ps3[2]==4 && ps3[4]==0 && ps3[6]==0xff);
    assert(ps3[19]==0xfc && ps3[20]==0x03);
}
static void test_failure_does_not_modify_output(void){
    reset();uint8_t saved[27];
    memcpy(saved,ps3,sizeof(saved));
    assert(ghl_ps4_to_ps3(raw,7,ps3)==-1);
    assert(memcmp(saved,ps3,27)==0);
    raw[0]=0x30;
    assert(ghl_ps4_to_ps3(raw,sizeof(raw),ps3)==-2);
    assert(memcmp(saved,ps3,27)==0);
    assert(ghl_ps4_to_ps3(NULL,64,ps3)==-3);
}
int main(void){
    test_frets();test_buttons();test_axes();
    test_failure_does_not_modify_output();
    puts("PASS: PS4-to-PS3 GHL mapping 6 frets, buttons, strum, whammy, D-pad, tilt, invalid lengths");
    return 0;
}
