/*
 * RSX framebuffer dashboard, using ps3dev/PSL1GHT videoTest rsxutil.c (GPL-3)
 * and Daniel Hepper's public-domain 8x8 bitmap font.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include <unistd.h>
#include <rsx/rsx.h>
#include "rsxutil.h"
#include "dashboard.h"
#include "font8x8_basic.h"

#define BUFFER_COUNT 2
#define DARK       0x00111620u
#define CARD       0x00202C3Bu
#define STROKE     0x003A4758u
#define WHITE      0x00F6F8FCu
#define MUTED      0x00ADBDCFu
#define BLUE       0x003A85FFu
#define GREEN      0x0046D69Cu
#define YELLOW     0x00FFC86Eu
#define RED        0x00FF7880u

static gcmContextData *context;
static void *host_memory;
static rsxBuffer buffers[BUFFER_COUNT];
static int have_buffers, active;
static u16 screen_w, screen_h;
static uint32_t *pixels;

static int map_x(int x) { return x * (int)screen_w / 1280; }
static int map_y(int y) { return y * (int)screen_h / 720; }

static void rect_px(int x, int y, int w, int h, uint32_t color) {
    int a,b;
    if(x<0){w+=x;x=0;}
    if(y<0){h+=y;y=0;}
    if(x+w>(int)screen_w) w=(int)screen_w-x;
    if(y+h>(int)screen_h) h=(int)screen_h-y;
    if(w<=0 || h<=0 || !pixels) return;
    for(b=y;b<y+h;b++) {
        uint32_t *row=pixels+(size_t)b*screen_w+x;
        for(a=0;a<w;a++) row[a]=color;
    }
}
static void rect(int x,int y,int w,int h,uint32_t col) {
    rect_px(map_x(x),map_y(y),map_x(w),map_y(h),col);
}
static void glyph(int x,int y,unsigned char ch,uint32_t col,int scale) {
    int j,k,dx,dy;
    if(ch>=128) ch='?';
    for(j=0;j<8;j++) for(k=0;k<8;k++) {
        if(((unsigned char)font8x8_basic[ch][j]>>(unsigned)k)&1u) {
            int xx=x+k*scale, yy=y+j*scale;
            for(dy=0;dy<scale;dy++) for(dx=0;dx<scale;dx++) {
                int px=xx+dx,py=yy+dy;
                if(px>=0&&py>=0&&px<screen_w&&py<screen_h)
                    pixels[(size_t)py*screen_w+px]=col;
            }
        }
    }
}
static void text_at(int x,int y,uint32_t color,const char *str) {
    int x0=map_x(x),y0=map_y(y);
    int scale=(screen_h>=650)?2:1;
    int ox=x0;
    if(!str) return;
    for(;*str;str++) {
        if(*str=='\n'){ox=x0;y0+=11*scale;continue;}
        if(ox+8*scale>=screen_w) break;
        glyph(ox,y0,(unsigned char)*str,color,scale);
        ox+=9*scale;
    }
}
static void card(int x,int y,int w,int h) {
    rect(x,y,w,h,STROKE);
    rect(x+2,y+2,w-4,h-4,CARD);
}
static void status(int y,const char *name,const char *state,int level) {
    uint32_t color=level==2?GREEN:(level==3?RED:(level==1?YELLOW:MUTED));
    text_at(78,y,WHITE,name);
    rect(635,y-3,11,19,color);
    text_at(663,y,color,state);
}
int ghl_ui_init(void) {
    int i;
    host_memory=memalign(1024*1024,HOST_SIZE);
    if(!host_memory) return 0;
    context=initScreen(host_memory,HOST_SIZE);
    if(!context){host_memory=NULL;return 0;} /* helper frees on error */
    if(!getResolution(&screen_w,&screen_h)) return 0;
    for(i=0;i<BUFFER_COUNT;i++) {
        if(!makeBuffer(&buffers[i],screen_w,screen_h,i)) return 0;
        have_buffers++;
    }
    gcmResetFlipStatus();
    active=0;
    return 1;
}
void ghl_ui_draw(const GhlDashboardState *s) {
    int n,i;
    char line[160];
    if(!context || have_buffers<BUFFER_COUNT || !s) return;
    /* Two RSX display buffers, rendering each image to the inactive one. */
    waitFlip();
    pixels=buffers[active].ptr;
    n=(int)screen_w*(int)screen_h;
    for(i=0;i<n;i++)pixels[i]=DARK;

    rect(0,0,1280,9,BLUE);
    text_at(75,35,WHITE,"GHL PS4 DONGLE PREFLIGHT  v0.7");
    text_at(75,67,s->finished?(strncmp(s->verdict,"PREFLIGHT PASS",14)==0?GREEN:RED):YELLOW,
            s->verdict[0]?s->verdict:"WAIT: INITIALIZING USB");

    card(55,95,1170,235);
    status(119,"USB RECEIVER",
        s->receiver?"DETECTED | VID 1430 PID 07BB":
                    "WAITING | PLUG IN PS4 RECEIVER",
        s->receiver?2:1);
    status(161,"USB CONFIG",
        s->configuration_ok?"SET_CONFIGURATION CALLBACK OK":
        s->receiver?"PENDING / SEE ERROR":"NOT AVAILABLE",
        s->configuration_ok?2:1);
    status(203,"GHL GAME HID SHAPE",
        s->game_shape_ok?"ONE HID + INTERRUPT IN/OUT":
        s->receiver?"INCOMPATIBLE OR INCOMPLETE":"NOT AVAILABLE",
        s->game_shape_ok?2:(s->receiver?3:1));
    status(245,"PS4 INIT (0x30)",
        s->keepalive_acks>=2?"TWO+ 0x30 CALLBACKS OK":
        s->init_ok?"1ST ACK - WAIT 2ND":
        s->init_error?"SET_REPORT ERROR":
        s->init_sent?"SENT - AWAITING CALLBACK":"NOT SENT",
        s->init_ok?2:(s->init_error?3:1));
    status(287,"GUITAR INPUT",
        s->guitar_connected ? "ISCONNECTED=1 + USB REPORTS":
        s->input_reports>0 ? "RAW REPORTS - NOT CONNECTED":
        "NO REPORTS / NO PAIRING EVIDENCE",
        s->guitar_connected?2:1);

    card(55,343,1170,95);
    snprintf(line,sizeof(line),"REPORTS  %d",s->input_reports);text_at(90,366,WHITE,line);
    snprintf(line,sizeof(line),"CHANGES  %d",s->input_changes);text_at(455,366,WHITE,line);
    snprintf(line,sizeof(line),"ERRORS  %d",s->total_errors);text_at(833,366,s->total_errors?RED:GREEN,line);
    snprintf(line,sizeof(line),"STAGES  %d/12    VALID %d",s->passed_stages,
             s->compatible_reports);
    text_at(833,406,MUTED,line);

    card(55,449,1170,169);
    if(s->finished) {
        text_at(81,476,GREEN,"CAPTURE COMPLETE - FINAL PREFLIGHT");
        snprintf(line,sizeof(line),"ACTION STAGES %d/12  |  %s",s->passed_stages,s->verdict);
        text_at(81,514,WHITE,line);
        text_at(81,541,MUTED,"Release START, then press START again to exit.");
    } else if(s->stage<0) {
        text_at(81,475,YELLOW,"WAITING: TEST TIMER IS PAUSED");
        text_at(81,508,WHITE,s->reason);
        text_at(81,541,MUTED,"Do not interpret dongle detection as guitar pairing.");
    } else {
        text_at(81,475,GREEN,"CURRENT TEST");
        text_at(81,508,WHITE,s->reason);
        snprintf(line,sizeof(line),"STAGE %d/12   ACTIVE TEST %d / 180 SECONDS",
                 s->stage+1,s->testing_seconds);
        text_at(81,541,MUTED,line);
    }
    snprintf(line,sizeof(line),"LATEST REPORT: %s",s->latest_report[0]?s->latest_report:"(none)");
    text_at(81,583,WHITE,line);

    if(s->last_error) {
        snprintf(line,sizeof(line),"ERROR: %s  code=0x%08X (%d)",
                 s->last_operation,(unsigned)s->last_error,s->last_error);
        text_at(65,639,RED,line);
    } else text_at(65,639,MUTED,"ERROR: NONE RECORDED (NOT EVIDENCE OF COMPATIBILITY)");
    if(s->network_ready) {
        snprintf(line,sizeof(line),"LAN LOGS: http://%s:8895/  |  PS3 + PC on same LAN",
                 s->network_ip[0]?s->network_ip:"PS3_IP");
        text_at(65,668,GREEN,line);
    } else text_at(65,668,MUTED,"Logs in /dev_hdd0/tmp/GHLPROBE/  |  LAN export unavailable");
    flip(context,buffers[active].id);
    active=(active+1)%BUFFER_COUNT;
}
void ghl_ui_shutdown(void) {
    int i;
    if(context) {
        gcmSetWaitFlip(context);
        for(i=0;i<have_buffers;i++) if(buffers[i].ptr) rsxFree(buffers[i].ptr);
        rsxFinish(context,1);
    }
    if(host_memory) free(host_memory);
    context=NULL;host_memory=NULL;have_buffers=0;
}
