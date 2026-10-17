/* Pure, host-testable PS4 1430:07BB preflight report assessment.
 * Layout: TheNathannator/PlasticBand Docs/Instruments/6-Fret Guitar/PS4.md
 * isConnected at byte 25, 1 = receiver claims guitar connection.
 * Explicit user-action correlations are stronger evidence than packet count.
 */
#include "preflight.h"
#include <string.h>
static int pop6(unsigned x){int n=0;while(x){n+=(x&1U);x>>=1;}return n;}
void ghl_preflight_reset(GhlPreflight *p){
    memset(p,0,sizeof(*p));
    p->previous_length=-1;
    p->previous_stage=-999;
}
static int stage_action(const uint8_t *d,int stage){
    int frets=(int)((d[5]>>4)&15u) + (int)((d[6]&3u)<<4);
    switch(stage){
    case 0:return (d[5]&0x20u)!=0;
    case 1:return (d[5]&0x40u)!=0;
    case 2:return (d[5]&0x80u)!=0;
    case 3:return (d[5]&0x10u)!=0;
    case 4:return (d[6]&0x01u)!=0;
    case 5:return (d[6]&0x02u)!=0;
    case 6:return d[2]<0x40u;
    case 7:return d[2]>0xc0u;
    case 8:return pop6((unsigned)frets)>=2 && (d[2]<0x40u || d[2]>0xc0u);
    case 9:return d[4]>0x98u || d[4]<0x68u;
    case 10:return d[3]>0xa0u || d[3]<0x60u;
    case 11:return (d[6]&0xe0u)!=0 || (d[7]&0x01u)!=0;
    default:return 0;
    }
}
void ghl_preflight_feed(GhlPreflight *p,const uint8_t *data,size_t n,int stage){
    int action=0,previous_action=0;
    if(!p || !data || !n)return;
    p->raw_reports++;
    if(!p->min_report_len || n<p->min_report_len)p->min_report_len=(unsigned)n;
    if(n>p->max_report_len)p->max_report_len=(unsigned)n;
    if(n<26 || data[0]!=0x01){
        p->invalid_reports++;
        p->connection_unknown++;
        p->previous_length=-1;
        return;
    }
    p->usable_reports++;
    if(data[25]==1)p->connection_yes++;
    else if(data[25]==0)p->connection_no++;
    else p->connection_unknown++;
    p->fret_mask|=((unsigned)data[5]>>4)&0xfu;
    p->fret_mask|=((unsigned)data[6]&3u)<<4;
    if(data[2]<0x40u)p->strum_directions|=1u;
    if(data[2]>0xc0u)p->strum_directions|=2u;
    if(data[4]>0x98u || data[4]<0x68u)p->whammy_events++;
    if(data[3]>0xa0u || data[3]<0x60u)p->tilt_events++;
    if(stage>=0 && stage<GHL_PREFLIGHT_STAGES){
        p->stage_frames[stage]++;
        action=stage_action(data,stage);
        if(p->previous_length>=26 && p->previous_stage==stage)
            previous_action=stage_action(p->previous,stage);
        /* A newly observed press (or held press at start of stage) is
         * stage evidence. Stage cannot pass on unrelated idle reports. */
        if(action && !previous_action)p->stage_actions[stage]++;
    }
    memcpy(p->previous,data,n<64?n:64);
    p->previous_length=(int)(n<64?n:64);
    p->previous_stage=stage;
}
int ghl_preflight_action_passed(const GhlPreflight *p,int stage){
    return p && stage>=0 && stage<GHL_PREFLIGHT_STAGES &&
        p->stage_actions[stage]>0;
}
unsigned ghl_preflight_stages_passed(const GhlPreflight *p){
    unsigned n=0;int st;
    if(!p)return 0;
    for(st=0;st<GHL_PREFLIGHT_STAGES;st++)
        n+=(unsigned)ghl_preflight_action_passed(p,st);
    return n;
}
const char *ghl_preflight_verdict(const GhlPreflight *p,int usb_claim,
     int descriptor_shape,int config_ok,int keepalive_ok,unsigned errors,int finished){
    if(!p)return "INCONCLUSIVE - NO DATA";
    if(!usb_claim)return finished?"NO-GO: RECEIVER NOT CLAIMED":"WAIT: USB RECEIVER";
    if(!descriptor_shape)return finished?"NO-GO: GAME HID SHAPE":"WAIT: GAME HID SHAPE";
    if(!config_ok)return finished?"NO-GO: USB CONFIG":"WAIT: SET CONFIG";
    if(!keepalive_ok)return finished?"NO-GO: PS4 INIT":"WAIT: PS4 INIT";
    if(p->usable_reports==0)return finished?"NO-GO: NO PS4 INPUT":"WAIT: PS4 REPORTS";
    if(p->connection_yes==0)return finished?"NO-GO: NO CONNECTION FLAG":"WAIT: PAIR GUITAR";
    if(!finished)return "TEST: PRESS REQUESTED CONTROLS";
    if(errors>0)return "INCONCLUSIVE: USB ERRORS";
    if(ghl_preflight_stages_passed(p)<GHL_PREFLIGHT_REQUIRED_STAGES)
        return "INCONCLUSIVE: TOO FEW ACTIONS";
    if(p->strum_directions!=3)return "INCONCLUSIVE: STRUM NOT BOTH WAYS";
    return "PREFLIGHT PASS: TRIAL POSSIBLE";
}
