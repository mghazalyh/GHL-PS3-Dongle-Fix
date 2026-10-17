/*
 * GHL PS4 receiver diagnostic for physical PS3 CFW.
 * Captures descriptors, init responses and raw reports. Does not alter games.
 * Built against PS3DEV/PSL1GHT - untested on user's hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/process.h>
#include <sys/mutex.h>
#include <sysmodule/sysmodule.h>
#include <sysutil/sysutil.h>
#include <sysutil/msg.h>
#include <usb/usb.h>
#include <io/pad.h>
#include "dashboard.h"
#include "http_export.h"

#define GHL_VID 0x1430
#define GHL_PID 0x07bb
#define DURATION_SEC 180
#define USB_PACKET 64
#define QUEUE_SIZE 256

typedef struct {
    char kind;
    int result, count, device;
    unsigned char data[USB_PACKET];
} Event;
static Event events[QUEUE_SIZE];
static unsigned q_head, q_tail, dropped;
static sys_mutex_t q_mutex;
static volatile int mutex_ok, device_id = -1, detached, in_flight, out_flight;
static int ctrl_pipe = -1, int_pipe = -1, interface_no = 0;
static int in_size = USB_PACKET;
/* DMA-safe buffers: successful PS3 guitar projects call usbAllocateMemory. */
static unsigned char *in_data, *out_data;
static usbDeviceRequest out_req,cfg_req;
static int cfg_in_flight, cfg_ok, cfg_sent;
static int config_value=1, config_error;
static usbEndpointDescriptor *candidate_ep;
static int dma_buffers_ok;
static FILE *logfile;
static unsigned report_count, usb_errors, attach_count, control_count, detach_count;
static unsigned receiver_session_reports;
static unsigned stage_report_count[12], stage_change_count[12];
static unsigned stage_byte_flips[12][64];
static unsigned long long last_flush_ms;
static unsigned long long started_ms;
static unsigned long long test_started_ms, next_in_retry_ms;
static int have_device_desc, have_config_desc, have_interface_desc, have_interrupt_ep;
static unsigned char previous_report[USB_PACKET];
static int previous_length = -1;
static unsigned long long last_input_submit_ms,last_control_submit_ms;
static GhlDashboardState ui;
static int http_active;
static volatile int completed;
static int sysutil_exit_requested;
static const char *log_path = "/dev_hdd0/tmp/GHLPROBE/telemetry.jsonl";
static const char *sum_path = "/dev_hdd0/tmp/GHLPROBE/summary.txt";

static unsigned long long milliseconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
}
static unsigned long long elapsed(void) {
    unsigned long long now = milliseconds();
    return now >= started_ms ? now-started_ms : 0;
}
static const char *stages[] = {
    "black_fret_1", "black_fret_2", "black_fret_3",
    "white_fret_1", "white_fret_2", "white_fret_3",
    "strum_up", "strum_down", "chords_and_strum",
    "whammy", "tilt", "menu_hero_power"
};
static const char *instructions[] = {
    "Press BLACK fret 1 repeatedly",
    "Press BLACK fret 2 repeatedly",
    "Press BLACK fret 3 repeatedly",
    "Press WHITE fret 1 repeatedly",
    "Press WHITE fret 2 repeatedly",
    "Press WHITE fret 3 repeatedly",
    "Strum UP repeatedly without frets",
    "Strum DOWN repeatedly without frets",
    "Hold fret chords and strum",
    "Move the whammy bar repeatedly",
    "Tilt the guitar up and down",
    "Press Hero Power and menu buttons"
};
static int stage_for(unsigned long long tms) {
    int i=(int)(tms / 15000ULL);
    return i>11?11:i;
}
static unsigned long long test_time_ms(void) {
    return test_started_ms ? milliseconds()-test_started_ms : 0;
}
static void emit(const char *event, const char *extra);
static void last_error(const char *op,int rc) {
    if(rc==0) return;
    ui.total_errors++;
    ui.last_error=rc;
    snprintf(ui.last_operation,sizeof(ui.last_operation),"%s",op);
    char buf[160];snprintf(buf,sizeof(buf),
      ",\"operation\":\"%s\",\"rc\":%d,\"rc_hex\":\"0x%08X\"",
      op,rc,(unsigned)rc);
    emit("error",buf);
}
static void sysutil_callback(u64 status,u64 param,void *user) {
    (void)param;(void)user;
    if(status==SYSUTIL_EXIT_GAME) sysutil_exit_requested=1;
}
static int controller_start(void) {
    padInfo info;padData data;int i;
    if(ioPadGetInfo(&info)!=PAD_OK) return 0;
    for(i=0;i<7;i++) if(info.status[i]) {
        if(ioPadGetData(i,&data)==PAD_OK && data.BTN_START) return 1;
    }
    return 0;
}
static void emit(const char *event, const char *extra) {
    if (!logfile) return;
    fprintf(logfile, "{\"t_ms\":%llu,\"event\":\"%s\",\"stage\":\"%s\"%s}\n",
            elapsed(), event, test_started_ms ? stages[stage_for(test_time_ms())] : "waiting", extra ? extra : "");
    /* Buffered writes protect USB timing; main loop flushes every 250ms. */
}
static void emit_rc(const char *name, int rc) {
    char buf[100]; snprintf(buf,sizeof(buf),",\"rc\":%d",rc);
    emit(name,buf);
}
static void enqueue(char kind, int device, int result, int count, const unsigned char *data) {
    if (!mutex_ok) return;
    sysMutexLock(q_mutex,0);
    unsigned next=(q_head+1U)%QUEUE_SIZE;
    if (next==q_tail) ++dropped;
    else {
        Event *e=&events[q_head];
        e->kind=kind; e->device=device; e->result=result; e->count=count;
        if(data && count>0) {
            int len=count<USB_PACKET?count:USB_PACKET;
            memcpy(e->data,data,(size_t)len);
        }
        q_head=next;
    }
    sysMutexUnlock(q_mutex);
}
static int pop_event(Event *e) {
    int ok=0;
    if(!mutex_ok) return 0;
    sysMutexLock(q_mutex,0);
    if(q_tail!=q_head) {
        *e=events[q_tail]; q_tail=(q_tail+1U)%QUEUE_SIZE; ok=1;
    }
    sysMutexUnlock(q_mutex);
    return ok;
}
static int probe(s32 dev) { enqueue('P',dev,0,0,NULL); return USB_PROBE_SUCCEEDED; }
static int attach(s32 dev) {
    device_id=dev; detached=0;
    enqueue('A',dev,0,0,NULL);
    return USB_ATTACH_SUCCEEDED;
}
static int detach(s32 dev) {
    detached=1; device_id=-1;
    enqueue('D',dev,0,0,NULL);
    return USB_DETACH_SUCCEEDED;
}
static usbLddOps ldd={ "GHL PS4 receiver probe", probe, attach, detach };
static void on_interrupt(s32 result, s32 count, void *arg) {
    (void)arg;
    enqueue('I',device_id,result,count, result==0?in_data:NULL);
    if(result!=0) next_in_retry_ms=milliseconds()+450;
    in_flight=0;
}
static void on_configuration(s32 result, s32 count, void *arg) {
    (void)arg;
    enqueue('F',device_id,result,count,NULL);
    cfg_in_flight=0;
}
static void on_control(s32 result, s32 count, void *arg) {
    (void)arg;
    enqueue('C',device_id,result,count,NULL);
    out_flight=0;
}
static void pump_events(void) {
    Event e;
    while(pop_event(&e)) {
        char extra[USB_PACKET*2+150];
        if(e.kind=='I') {
            int n=e.count, i;
            char hex[USB_PACKET*2+1];
            if(n<0)n=0;
            if(n>USB_PACKET)n=USB_PACKET;
            for(i=0;i<n;i++) snprintf(hex+2*i,sizeof(hex)-2*(size_t)i,"%02x",e.data[i]);
            hex[2*n]='\0';
            snprintf(extra,sizeof(extra),",\"rc\":%d,\"length\":%d,\"hex\":\"%s\"",
                    e.result,e.count,hex);
            emit("interrupt_in",extra);
            if(e.result==0 && e.count>0) {
                report_count++;
                receiver_session_reports++;
                ui.input_reports=(int)report_count;
                if(previous_length>=0 && (previous_length!=n || memcmp(previous_report,e.data,(size_t)n)!=0)) {
                    ui.input_changes++;
                    emit("input_change",extra);
                }
                if(test_started_ms) {
                    int st=stage_for(test_time_ms());
                    stage_report_count[st]++;
                    if(previous_length>=0 &&
                       (previous_length!=n || memcmp(previous_report,e.data,(size_t)n)!=0)) {
                        stage_change_count[st]++;
                        for(i=0;i<n;i++) {
                            if(i>=previous_length || previous_report[i]!=e.data[i])
                                stage_byte_flips[st][i]++;
                        }
                    }
                }
                memcpy(previous_report,e.data,(size_t)n);
                previous_length=n;
                {
                    int len=n>22?22:n;
                    int off=0;
                    for(i=0;i<len;i++) {
                        off+=snprintf(ui.latest_report+off,sizeof(ui.latest_report)-(size_t)off,
                                      "%02X%s",e.data[i],i==len-1?"":" ");
                    }
                    if(n>len)snprintf(ui.latest_report+off,sizeof(ui.latest_report)-(size_t)off," ...");
                }
            } else if(e.result!=0) {
                usb_errors++;
                ui.input_error=e.result;
                last_error("interrupt transfer callback",e.result);
            }
        } else {
            const char *kind=e.kind=='A'?"attach":e.kind=='D'?"detach":
                             e.kind=='C'?"set_report_complete":
                             e.kind=='F'?"set_configuration_complete":"probe";
            snprintf(extra,sizeof(extra),",\"device\":%d,\"rc\":%d,\"length\":%d",
                     e.device,e.result,e.count);
            emit(kind,extra);
            if(e.kind=='A'){attach_count++;ui.receiver=1;}
            if(e.kind=='D') {
                detach_count++;ui.receiver=0;ui.usb_ready=0;
                ui.init_ok=0;ui.init_sent=0;
                receiver_session_reports=0;
                test_started_ms=0;previous_length=-1;
            }
            if(e.kind=='F') {
                if(e.result==0){cfg_ok=1;ui.configuration_ok=1;}
                else {config_error=e.result;cfg_ok=0;ui.configuration_ok=0;last_error("SET_CONFIGURATION completion",e.result);}
            }
            if(e.kind=='C') {
                control_count++;
                if(e.result==0)ui.init_ok=1;
                else {
                    ui.init_error=e.result;ui.init_ok=0;
                    usb_errors++;last_error("HID SET_REPORT completion",e.result);
                }
            }
        }
    }
}
static void inspect_descriptors(int dev) {
    usbDeviceDescriptor *dd=(usbDeviceDescriptor*)usbScanStaticDescriptor(dev,NULL,USB_DESCRIPTOR_TYPE_DEVICE);
    char s[256];
    if(dd) {
        snprintf(s,sizeof(s),",\"vendor\":%u,\"product\":%u,\"usb_release\":%u,\"device_release\":%u,\"device_class\":%u,\"num_configs\":%u",
                 (unsigned)dd->idVendor,(unsigned)dd->idProduct,
                 (unsigned)dd->bcdUSB,(unsigned)dd->bcdDevice,
                 (unsigned)dd->bDeviceClass,(unsigned)dd->bNumConfigurations);
        emit("device_descriptor",s);
        /* USB descriptor multi-byte values can be exposed as raw wire-LE or host-order
           on big-endian PowerPC; record and accept both representations. */
        have_device_desc=((dd->idVendor==GHL_VID && dd->idProduct==GHL_PID) ||
             (dd->idVendor==0x3014 && dd->idProduct==0xBB07));
        snprintf(s,sizeof(s),",\"raw_vendor_field\":%u,\"raw_product_field\":%u,\"matched\":%s",
                 (unsigned)dd->idVendor,(unsigned)dd->idProduct,
                 have_device_desc?"true":"false");
        emit("device_field_endian_check",s);
    } else emit("device_descriptor_missing",NULL);
    usbConfigDescriptor *cd=(usbConfigDescriptor*)usbScanStaticDescriptor(dev,NULL,USB_DESCRIPTOR_TYPE_CONFIG);
    if(cd) {
        snprintf(s,sizeof(s),",\"interfaces\":%u,\"total_length\":%u,\"configuration\":%u",
                (unsigned)cd->bNumInterfaces,(unsigned)cd->wTotalLength,(unsigned)cd->bConfigurationValue);
        emit("config_descriptor",s);
        have_config_desc=1;
        config_value=(int)cd->bConfigurationValue;
    }
    /* Walk every interface/endpoint, like the hardware-tested PS3 Riffmaster
       plugin. The first USB endpoint may be OUT; do not assume it is IN. */
    {
        usbInterfaceDescriptor *ifd=NULL;
        int iface_idx=0;
        for(iface_idx=0;iface_idx<12;iface_idx++) {
            ifd=(usbInterfaceDescriptor*)usbScanStaticDescriptor(
                dev,ifd,USB_DESCRIPTOR_TYPE_INTERFACE);
            if(!ifd)break;
            have_interface_desc=1;
            snprintf(s,sizeof(s),
                 ",\"number\":%u,\"class\":%u,\"subclass\":%u,\"protocol\":%u,\"endpoints\":%u",
                 (unsigned)ifd->bInterfaceNumber,(unsigned)ifd->bInterfaceClass,
                 (unsigned)ifd->bInterfaceSubClass,(unsigned)ifd->bInterfaceProtocol,
                 (unsigned)ifd->bNumEndpoints);
            emit("interface_descriptor",s);
            {
                void *ptr=ifd;
                int ep_idx;
                for(ep_idx=0;ep_idx<ifd->bNumEndpoints && ep_idx<16;ep_idx++) {
                    usbEndpointDescriptor *ep=(usbEndpointDescriptor*)
                        usbScanStaticDescriptor(dev,ptr,USB_DESCRIPTOR_TYPE_ENDPOINT);
                    if(!ep)break;
                    ptr=ep;
                    snprintf(s,sizeof(s),
                         ",\"interface\":%u,\"address\":%u,\"attributes\":%u,\"max_packet_raw\":%u,\"interval\":%u",
                         (unsigned)ifd->bInterfaceNumber,
                         (unsigned)ep->bEndpointAddress,(unsigned)ep->bmAttributes,
                         (unsigned)ep->wMaxPacketSize,(unsigned)ep->bInterval);
                    emit("endpoint_descriptor",s);
                    if(!candidate_ep && (ep->bEndpointAddress&0x80) &&
                       ((ep->bmAttributes&3)==USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT)) {
                        candidate_ep=ep;
                        have_interrupt_ep=1;
                        interface_no=ifd->bInterfaceNumber;
                        in_size=ep->wMaxPacketSize;
                        if(in_size>255 && ((in_size&0xff)==0))
                            in_size>>=8;
                        if(in_size<1 || in_size>USB_PACKET) in_size=USB_PACKET;
                        snprintf(s,sizeof(s),
                          ",\"interface\":%d,\"packet_size\":%d",interface_no,in_size);
                        emit("selected_interrupt_endpoint",s);
                    }
                }
            }
        }
        if(!candidate_ep)emit("no_interrupt_in_endpoint",NULL);
    }
    {
        unsigned char *hid=(unsigned char*)usbScanStaticDescriptor(
            dev,NULL,USB_DESCRIPTOR_TYPE_HID);
        if(hid) {
            unsigned n=hid[0],i;
            char hex[80];
            if(n>32)n=32;
            for(i=0;i<n;i++)snprintf(hex+2*i,sizeof(hex)-2*i,"%02x",hid[i]);
            hex[2*n]='\0';
            snprintf(s,sizeof(s),",\"hex\":\"%s\",\"length\":%u",hex,n);
            emit("hid_descriptor",s);
        } else emit("hid_descriptor_not_available",NULL);
    }
    u8 speed=255;
    emit_rc("get_speed",usbGetDeviceSpeed(dev,&speed));
    snprintf(s,sizeof(s),",\"speed\":%u",(unsigned)speed);
    emit("device_speed",s);
}
static void submit_configuration(void) {
    if(ctrl_pipe<0 || cfg_sent || cfg_in_flight)return;
    memset(&cfg_req,0,sizeof(cfg_req));
    cfg_req.bmRequestType=0x00;
    cfg_req.bRequest=USB_REQUEST_SET_CONFIGURATION;
    cfg_req.wValue=(u16)config_value;
    cfg_req.wIndex=0;
    cfg_req.wLength=0;
    cfg_in_flight=1;
    cfg_sent=1;
    int rc=usbControlTransfer(ctrl_pipe,&cfg_req,NULL,on_configuration,NULL);
    emit_rc("set_configuration_submit",rc);
    if(rc!=0) {
        cfg_in_flight=0;config_error=rc;
        last_error("SET_CONFIGURATION submit",rc);
    }
}
static void send_keepalive(void) {
    if(ctrl_pipe<0 || out_flight || !out_data || !cfg_ok) return;
    memset(&out_req,0,sizeof(out_req));
    out_req.bmRequestType=0x21;  /* host-to-device class interface */
    out_req.bRequest=0x09;      /* HID SET_REPORT */
    out_req.wValue=0x0230;      /* output report ID 0x30 */
    out_req.wIndex=(u16)interface_no;
    out_req.wLength=9;
    out_flight=1;
    last_control_submit_ms=milliseconds();
    int rc=usbControlTransfer(ctrl_pipe,&out_req,out_data,on_control,NULL);
    emit_rc("set_report_submit",rc);
    if(rc==0)ui.init_sent=1;
    else {out_flight=0; usb_errors++;ui.init_error=rc;last_error("HID SET_REPORT submission",rc);}
}
static void listen_once(void) {
    if(int_pipe<0 || in_flight || !in_data || !cfg_ok ||
       milliseconds()<next_in_retry_ms) return;
    in_flight=1;
    last_input_submit_ms=milliseconds();
    int rc=usbInterruptTransfer(int_pipe,in_data,in_size,on_interrupt,NULL);
    if(rc) {
        in_flight=0;usb_errors++;
        emit_rc("interrupt_submit_error",rc);
        next_in_retry_ms=milliseconds()+450;
        last_error("interrupt transfer submit",rc);
    }
}
static void prepare_log(void) {
    mkdir("/dev_hdd0/tmp/GHLPROBE",0777);
    rename(log_path,"/dev_hdd0/tmp/GHLPROBE/telemetry.previous.jsonl");
    rename(sum_path,"/dev_hdd0/tmp/GHLPROBE/summary.previous.txt");
    logfile=fopen(log_path,"wb");
    if(!logfile) {
        log_path="/dev_hdd0/tmp/ghl_probe_telemetry.jsonl";
        sum_path="/dev_hdd0/tmp/ghl_probe_summary.txt";
        logfile=fopen(log_path,"wb");
    }
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    sys_mutex_attr_t mattr;
    int registered=0, attached_dev=-1, previous_stage=-1;
    int rc, usb_initialized=0;
    int pad_ready=0, graphics_ok=0, final_written=0;
    int start_was_down=0;
    unsigned long long last_keepalive_ms=0, last_draw_ms=0;
    FILE *summary;

    started_ms=milliseconds();
    last_flush_ms=started_ms;
    memset(&ui,0,sizeof(ui));
    ui.stage=-1;
    prepare_log();
    snprintf(ui.log_path,sizeof(ui.log_path),"%s",log_path);
    emit("start",",\"version\":\"v0.6-onepass\",\"target_vid\":\"1430\",\"target_pid\":\"07bb\"");
    sysMutexAttrInitialize(mattr);
    rc=sysMutexCreate(&q_mutex,&mattr);
    mutex_ok=(rc==0);
    emit_rc("mutex_create",rc);
    if(rc!=0)last_error("create telemetry mutex",rc);

    rc=sysModuleLoad(SYSMODULE_SYSUTIL);
    emit_rc("load_sysutil",rc);
    sysUtilRegisterCallback(0,sysutil_callback,NULL);
    pad_ready=(ioPadInit(7)==PAD_OK);
    emit("pad_init",pad_ready?",\"available\":true":",\"available\":false");
    graphics_ok=ghl_ui_init();
    emit("graphics_init",graphics_ok?",\"available\":true":",\"available\":false");
    if(!graphics_ok) {
        /* Visible emergency fallback if RSX display allocation fails. */
        msgDialogOpen2((msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_DOUBLE_PROGRESSBAR),
            "GHL v0.5 LIVE: RSX unavailable. USB/live counters shown below.",
            NULL,NULL,NULL);
        last_error("RSX initialization unavailable",-1);
    }

    /* Read-only HTTP export runs independently from USB/UI. LAN only. */
    rc=ghl_http_start(log_path,sum_path,ui.network_ip,sizeof(ui.network_ip));
    http_active=(rc==0);
    ui.network_ready=http_active;
    if(http_active)emit("lan_export_started",",\"port\":8895");
    else {
        emit_rc("lan_export_failed",rc);
        /* Network is OPTIONAL; do not mark USB functionality as failing. */
    }

    rc=sysModuleLoad(SYSMODULE_USB);
    emit_rc("load_usb_module",rc);
    if(rc!=0 && rc!=SYSMODULE_ERR_DUPLICATE)last_error("load USB module",rc);
    rc=usbInit();
    emit_rc("usb_init",rc);
    usb_initialized=(rc==USB_OK || rc==USB_ERR_ALREADY_INITIALIZED);
    if(!usb_initialized) {
        ui.usb_start_error=rc;
        last_error("USB subsystem init",rc);
    }
    if(usb_initialized) {
        void *inmem=NULL,*outmem=NULL;
        int rc_in=usbAllocateMemory(&inmem,USB_PACKET);
        int rc_out=usbAllocateMemory(&outmem,USB_PACKET);
        emit_rc("usbd_dma_alloc_input",rc_in);
        emit_rc("usbd_dma_alloc_output",rc_out);
        if(rc_in==0 && rc_out==0 && inmem && outmem) {
            in_data=(unsigned char*)inmem;
            out_data=(unsigned char*)outmem;
            memset(in_data,0,USB_PACKET);
            memset(out_data,0,USB_PACKET);
            {const unsigned char magic[]={0x30,0x02,0x08,0x0a,0,0,0,0,0};
             memcpy(out_data,magic,sizeof(magic));}
            dma_buffers_ok=1;
        } else {
            last_error("USBD DMA allocation failed",rc_in?rc_in:(rc_out?rc_out:-1));
        }
    }
    if(usb_initialized && dma_buffers_ok && mutex_ok) {
        rc=usbRegisterExtraLdd(&ldd,GHL_VID,GHL_PID);
        emit_rc("receiver_driver_register",rc);
        registered=(rc==USB_OK);
        if(!registered)last_error("extra LDD register",rc);
    }
    /* Work forever while waiting for USB, but allow START to finish.
     * Capture runs exactly 180s of actual testing after stream readiness.
     */
    while(!sysutil_exit_requested) {
        unsigned long long now=milliseconds();
        unsigned long long t=elapsed();
        int stage=-1;

        pump_events();
        if(!completed && attached_dev<0 && device_id>=0 && !detached) {
            attached_dev=device_id;
            ui.receiver=1;
            have_device_desc=have_config_desc=have_interface_desc=have_interrupt_ep=0;
            ui.usb_ready=0; ui.init_ok=0;ui.init_sent=0;ui.init_error=0;
            cfg_ok=cfg_sent=cfg_in_flight=0;config_error=0;ui.configuration_ok=0;
            candidate_ep=NULL;int_pipe=-1;
            previous_length=-1;
            receiver_session_reports=0;
            emit("receiver_detected",NULL);
            inspect_descriptors(attached_dev);
            ctrl_pipe=usbOpenPipe(attached_dev,NULL);
            emit_rc("open_control_pipe",ctrl_pipe);
            if(ctrl_pipe<0)last_error("open USB control endpoint",ctrl_pipe);
            if(ctrl_pipe>=0 && have_config_desc && have_interrupt_ep)
                submit_configuration();
            last_keepalive_ms=now;
        }
        if(detached) {
            pump_events();
            if(!in_flight && !out_flight) {
                if(int_pipe>=0)usbClosePipe(int_pipe);
                if(ctrl_pipe>=0)usbClosePipe(ctrl_pipe);
                int_pipe=-1;ctrl_pipe=-1;attached_dev=-1;detached=0;
                cfg_ok=cfg_sent=cfg_in_flight=0;candidate_ep=NULL;ui.configuration_ok=0;
                test_started_ms=0;previous_length=-1;
                ui.receiver=ui.usb_ready=ui.init_ok=ui.init_sent=0;
                ui.stage=-1;
            }
        }
        /* Timeouts are evidence about stalled callbacks; do not reuse
         * buffers or close active pipes until the driver releases them. */
        if(in_flight && now-last_input_submit_ms>3500ULL && !ui.input_timeout_warned) {
            ui.input_timeout_warned=1;
            ui.total_errors++;
            ui.last_error=-9001;
            snprintf(ui.last_operation,sizeof(ui.last_operation),"USB interrupt callback stalled");
            emit("interrupt_callback_stalled",",\"threshold_ms\":3500");
        }
        if(out_flight && now-last_control_submit_ms>3500ULL && !ui.control_timeout_warned) {
            ui.control_timeout_warned=1;
            ui.total_errors++;
            ui.last_error=-9002;
            snprintf(ui.last_operation,sizeof(ui.last_operation),"USB control callback stalled");
            emit("control_callback_stalled",",\"threshold_ms\":3500");
        }
        if(!completed && attached_dev>=0 && !detached) {
            if(cfg_ok && int_pipe<0 && candidate_ep) {
                int_pipe=usbOpenPipe(attached_dev,candidate_ep);
                emit_rc("open_interrupt_pipe_after_configuration",int_pipe);
                if(int_pipe<0)last_error("open interrupt endpoint",int_pipe);
                candidate_ep=NULL;
                ui.usb_ready=(int_pipe>=0 && ctrl_pipe>=0 && have_device_desc &&
                    have_config_desc && have_interface_desc && have_interrupt_ep && cfg_ok);
                if(ui.usb_ready)send_keepalive();
            }
            if(now-last_keepalive_ms>=7000 && !out_flight) {
                send_keepalive();last_keepalive_ms=now;
            }
            listen_once();
        }
        pump_events();

        if(!completed) {
            if(ui.usb_ready && ui.init_ok && receiver_session_reports>0) {
                if(!test_started_ms) {
                    test_started_ms=milliseconds();
                    emit("guided_testing_started",NULL);
                }
            }
            if(test_started_ms && now-test_started_ms>=180000ULL) {
                completed=1;ui.finished=1;emit("guided_testing_finished",NULL);
            }
            if(!completed && test_started_ms) {
                stage=stage_for(test_time_ms());
                ui.stage=stage;
                ui.testing_seconds=(int)(test_time_ms()/1000ULL);
                snprintf(ui.reason,sizeof(ui.reason),"%s",instructions[stage]);
                if(stage!=previous_stage){
                    emit("stage",NULL);
                    previous_stage=stage;
                }
            } else if(!completed) {
                ui.stage=-1;
                if(!usb_initialized)snprintf(ui.reason,sizeof(ui.reason),"USB driver initialization failed. Read error below.");
                else if(!registered)snprintf(ui.reason,sizeof(ui.reason),"Receiver driver registration failed.");
                else if(!ui.receiver)snprintf(ui.reason,sizeof(ui.reason),"Plug in PS4 dongle (1430:07BB). Timer paused.");
                else if(!cfg_ok && !config_error)
                    snprintf(ui.reason,sizeof(ui.reason),"Waiting for USB SET_CONFIGURATION callback.");
                else if(config_error)
                    snprintf(ui.reason,sizeof(ui.reason),"USB SET_CONFIGURATION FAILED. Error shown below.");
                else if(!ui.usb_ready)
                    snprintf(ui.reason,sizeof(ui.reason),"USB descriptors or interrupt pipe unavailable.");
                else if(!ui.init_ok)snprintf(ui.reason,sizeof(ui.reason),"Waiting for HID SET_REPORT success. Timer paused.");
                else snprintf(ui.reason,sizeof(ui.reason),"Pair guitar, press frets: waiting for raw input reports.");
            }
        }

        ui.dropped_events=(int)dropped;
        ui.elapsed_seconds=(int)(t/1000ULL);

        {
            int start_down=pad_ready && controller_start();
            /* Require an explicit new press to dismiss the summary. */
            if(start_down && !start_was_down) {
                if(!completed) {
                    completed=1;
                    ui.finished=1;
                    emit("capture_stopped_by_start",NULL);
                } else if(final_written) break;
            }
            start_was_down=start_down;
        }
        if(completed && !final_written) {
            pump_events();
            emit("capture_complete",NULL);
            summary=fopen(sum_path,"wb");
            if(summary) {
                fprintf(summary,"GHL PS4 dongle diagnostic v0.6-onepass\n");
                fprintf(summary,"Target USB VID:PID 1430:07BB\n");
                fprintf(summary,"Receiver detected: %d\nUSB interface ready: %d\n",ui.receiver,ui.usb_ready);
                fprintf(summary,"HID SET_REPORT success callback observed: %d\n",ui.init_ok);
                fprintf(summary,"USB reports: %u\nRaw report changes: %d\nUSB error events: %u\n",
                        report_count,ui.input_changes,usb_errors);
                fprintf(summary,"Attaches: %u Detaches: %u SET_REPORT completions: %u\n",
                        attach_count,detach_count,control_count);
                fprintf(summary,"Event queue dropped: %u\nLast error: %s 0x%08X\n",
                        dropped,ui.last_operation,(unsigned)ui.last_error);
                fprintf(summary,"Stage report/change byte summaries (byte indices are zero-based):\n");
                {
                    int st,b;
                    for(st=0;st<12;st++) {
                        fprintf(summary,"%02d %-24s reports=%u changed=%u  hotspots=",
                                st+1,stages[st],stage_report_count[st],stage_change_count[st]);
                        for(b=0;b<64;b++) if(stage_byte_flips[st][b]>0)
                            fprintf(summary," %d:%u",b,stage_byte_flips[st][b]);
                        fputc('\n',summary);
                    }
                }
                fprintf(summary,"Telemetry: %s\n",log_path);
                fprintf(summary,"Raw report changes are NOT proof of successful guitar pairing.\n");
                fclose(summary);
            }
            if(logfile){fflush(logfile);fclose(logfile);logfile=NULL;}
            final_written=1;
        }
        if(now-last_draw_ms>=450) {
            if(graphics_ok) {
                ghl_ui_draw(&ui);
            } else {
                char a[260],b[260];
                snprintf(a,sizeof(a),"Dongle:%s USB:%s SET_REPORT:%s  Reports:%d",
                         ui.receiver?"YES":"WAIT",ui.usb_ready?"READY":"WAIT",
                         ui.init_ok?"ACK":"WAIT/ERROR",ui.input_reports);
                snprintf(b,sizeof(b),"Changes:%d Errors:%d  %s",
                         ui.input_changes,ui.total_errors,ui.finished?"DONE - START TO EXIT":ui.reason);
                msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0,a);
                msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1,b);
            }
            last_draw_ms=now;
        }
        if(logfile && now-last_flush_ms>=250ULL) {
            fflush(logfile);
            last_flush_ms=now;
        }
        sysUtilCheckCallback();
        usleep(16000);
    }

    if(!final_written) {
        emit("exit_before_complete",NULL);
        summary=fopen(sum_path,"wb");
        if(summary){fprintf(summary,"Interrupted by PS3 system exit. Reports: %u Errors: %u\n",
                            report_count,usb_errors);fclose(summary);}
    }
    if(http_active)ghl_http_stop();
    if(graphics_ok)ghl_ui_shutdown();
    else msgDialogAbort();
    if(registered)usbUnregisterExtraLdd(&ldd);
    if(int_pipe>=0 && !in_flight)usbClosePipe(int_pipe);
    if(ctrl_pipe>=0 && !out_flight)usbClosePipe(ctrl_pipe);
    if(usb_initialized) {
        if(!in_flight && in_data)usbFreeMemory(in_data);
        if(!out_flight && out_data)usbFreeMemory(out_data);
        usbEnd();
    }
    if(mutex_ok)sysMutexDestroy(q_mutex);
    if(logfile)fclose(logfile);
    if(pad_ready)ioPadEnd();
    sysUtilUnregisterCallback(0);
    return 0;
}
