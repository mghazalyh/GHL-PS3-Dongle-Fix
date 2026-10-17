#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../source/preflight.h"
static unsigned char d[64];
static GhlPreflight p;
static void init(void){
    ghl_preflight_reset(&p);
    memset(d,0,sizeof(d));
    d[0]=1;d[2]=d[3]=d[4]=0x80;d[5]=8;
}
static void feed(int stage){ghl_preflight_feed(&p,d,sizeof(d),stage);}
static void test_unpaired(){
    init();feed(0);assert(p.raw_reports==1 && p.connection_no==1);
    assert(strncmp(ghl_preflight_verdict(&p,1,1,1,1,0,1),"NO-GO",5)==0);
    d[25]=1;feed(0);
    assert(p.connection_yes==1);
}
static void test_stages(){
    const int bit5[]={0x20,0x40,0x80,0x10,0,0};
    const int bit6[]={0,0,0,0,1,2};
    int i;
    init();d[25]=1;
    for(i=0;i<6;i++){
        d[5]=8|bit5[i];d[6]=bit6[i];feed(i);
        assert(ghl_preflight_action_passed(&p,i));
    }
    d[5]=8;d[6]=0;d[2]=0;feed(6);assert(ghl_preflight_action_passed(&p,6));
    d[2]=255;feed(7);assert(ghl_preflight_action_passed(&p,7));
    assert(p.strum_directions==3);
    assert(ghl_preflight_stages_passed(&p)==8);
    assert(strcmp(ghl_preflight_verdict(&p,1,1,1,1,0,1),
                  "PREFLIGHT PASS: TRIAL POSSIBLE")==0);
    assert(strncmp(ghl_preflight_verdict(&p,1,0,1,1,0,1),"NO-GO",5)==0);
    assert(strncmp(ghl_preflight_verdict(&p,1,1,1,1,2,1),"INCONCLUSIVE",12)==0);
}
static void test_short_unknown_and_spurious(){
    init();d[25]=1;ghl_preflight_feed(&p,d,7,0);
    assert(p.invalid_reports==1 && p.usable_reports==0);
    d[0]=0x30;feed(0);assert(p.invalid_reports==2);
    d[0]=1;feed(0);assert(p.connection_yes==1);
    assert(!ghl_preflight_action_passed(&p,0));
}
int main(void){
    test_unpaired();test_stages();test_short_unknown_and_spurious();
    puts("PASS: receiver vs connected, input shape, stage matches, conservative decisions");
    return 0;
}
