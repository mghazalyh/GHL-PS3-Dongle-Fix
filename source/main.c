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
static int ctrl_pipe = -1, int_pipe = -1, running = 1, interface_no = 0;
static int in_size = USB_PACKET;
static unsigned char in_data[USB_PACKET] __attribute__((aligned(16)));
static unsigned char out_data[16] __attribute__((aligned(16))) =
    {0x30,0x02,0x08,0x0a,0,0,0,0,0};
static usbDeviceRequest out_req;
static FILE *logfile;
static unsigned report_count, usb_errors, attach_count, control_count, detach_count;
static unsigned long long started_ms;
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
    "pair_guitar", "black_frets_1_2_3", "white_frets_1_2_3",
    "strum_up_down", "fret_chords_and_strum", "whammy",
    "tilt", "menu_and_hero_power", "repeat_inputs", "finish"
};
static const int stage_start[] = {0,15,35,55,75,95,115,135,155,175};
static int stage_for(unsigned long long tms) {
    int i; int sec = (int)(tms/1000);
    for (i=9;i>0;i--) if (sec>=stage_start[i]) return i;
    return 0;
}
static void emit(const char *event, const char *extra) {
    if (!logfile) return;
    fprintf(logfile, "{\"t_ms\":%llu,\"event\":\"%s\",\"stage\":\"%s\"%s}\n",
            elapsed(), event, stages[stage_for(elapsed())], extra ? extra : "");
    fflush(logfile);
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
    in_flight=0;
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
            int n=e.count;
            int i;
            char hex[USB_PACKET*2+1];
            if(n<0) n=0; if(n>USB_PACKET) n=USB_PACKET;
            for(i=0;i<n;i++) snprintf(hex+2*i,sizeof(hex)-2*(size_t)i,"%02x",e.data[i]);
            hex[2*n]='\0';
            snprintf(extra,sizeof(extra),",\"rc\":%d,\"length\":%d,\"hex\":\"%s\"",
                    e.result,e.count,hex);
            emit("interrupt_in",extra);
            if(e.result==0) report_count++; else usb_errors++;
        } else {
            const char *kind=e.kind=='A'?"attach":e.kind=='D'?"detach":
                             e.kind=='C'?"set_report_complete":"probe";
            snprintf(extra,sizeof(extra),",\"device\":%d,\"rc\":%d,\"length\":%d",
                     e.device,e.result,e.count);
            emit(kind,extra);
            if(e.kind=='A') attach_count++;
            if(e.kind=='D') detach_count++;
            if(e.kind=='C') {control_count++; if(e.result)usb_errors++;}
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
    } else emit("device_descriptor_missing",NULL);
    usbConfigDescriptor *cd=(usbConfigDescriptor*)usbScanStaticDescriptor(dev,NULL,USB_DESCRIPTOR_TYPE_CONFIG);
    if(cd) {
        snprintf(s,sizeof(s),",\"interfaces\":%u,\"total_length\":%u,\"configuration\":%u",
                (unsigned)cd->bNumInterfaces,(unsigned)cd->wTotalLength,(unsigned)cd->bConfigurationValue);
        emit("config_descriptor",s);
    }
    usbInterfaceDescriptor *id=(usbInterfaceDescriptor*)usbScanStaticDescriptor(dev,NULL,USB_DESCRIPTOR_TYPE_INTERFACE);
    if(id) {
        interface_no=id->bInterfaceNumber;
        snprintf(s,sizeof(s),",\"number\":%u,\"class\":%u,\"subclass\":%u,\"protocol\":%u,\"endpoints\":%u",
                (unsigned)id->bInterfaceNumber,(unsigned)id->bInterfaceClass,(unsigned)id->bInterfaceSubClass,
                (unsigned)id->bInterfaceProtocol,(unsigned)id->bNumEndpoints);
        emit("interface_descriptor",s);
    }
    usbEndpointDescriptor *ep=(usbEndpointDescriptor*)usbScanStaticDescriptor(dev,NULL,USB_DESCRIPTOR_TYPE_ENDPOINT);
    if(ep) {
        snprintf(s,sizeof(s),",\"address\":%u,\"attributes\":%u,\"max_packet\":%u,\"interval\":%u",
                (unsigned)ep->bEndpointAddress,(unsigned)ep->bmAttributes,
                (unsigned)ep->wMaxPacketSize,(unsigned)ep->bInterval);
        emit("endpoint_descriptor",s);
        if((ep->bEndpointAddress&0x80) && ((ep->bmAttributes&3)==USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT)) {
            in_size=ep->wMaxPacketSize;
            if(in_size<1||in_size>USB_PACKET) in_size=USB_PACKET;
            int_pipe=usbOpenPipe(dev,ep);
            emit_rc("open_interrupt_pipe",int_pipe);
        }
    } else emit("endpoint_descriptor_missing",NULL);
    u8 speed=255;
    emit_rc("get_speed",usbGetDeviceSpeed(dev,&speed));
    snprintf(s,sizeof(s),",\"speed\":%u",(unsigned)speed);
    emit("device_speed",s);
}
static void send_keepalive(void) {
    if(ctrl_pipe<0 || out_flight) return;
    memset(&out_req,0,sizeof(out_req));
    out_req.bmRequestType=0x21;  /* host-to-device class interface */
    out_req.bRequest=0x09;      /* HID SET_REPORT */
    out_req.wValue=0x0230;      /* output report ID 0x30 */
    out_req.wIndex=(u16)interface_no;
    out_req.wLength=9;
    out_flight=1;
    int rc=usbControlTransfer(ctrl_pipe,&out_req,out_data,on_control,NULL);
    emit_rc("set_report_submit",rc);
    if(rc) {out_flight=0; usb_errors++;}
}
static void listen_once(void) {
    if(int_pipe<0 || in_flight) return;
    in_flight=1;
    int rc=usbInterruptTransfer(int_pipe,in_data,in_size,on_interrupt,NULL);
    if(rc) {
        in_flight=0;usb_errors++;
        emit_rc("interrupt_submit_error",rc);
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
    (void)argc; (void)argv;
    sys_mutex_attr_t mattr;
    int registered=0, attached_dev=-1, previous_stage=-1;
    int rc, last_keepalive=-100;
    unsigned long long deadline;
    FILE *summary;
    started_ms=milliseconds();
    prepare_log();
    emit("start",",\"version\":\"v0.4-fresh\",\"target_vid\":\"1430\",\"target_pid\":\"07bb\"");
    sysMutexAttrInitialize(mattr);
    rc=sysMutexCreate(&q_mutex,&mattr);
    mutex_ok=(rc==0);
    emit_rc("mutex_create",rc);
    rc=sysModuleLoad(SYSMODULE_USB);
    emit_rc("usb_module_load",rc);
    rc=usbInit();
    emit_rc("usb_init",rc);
    if(rc==USB_OK) {
        rc=usbRegisterExtraLdd(&ldd,GHL_VID,GHL_PID);
        emit_rc("register_receiver_ldd",rc);
        registered=(rc==USB_OK);
    }
    rc=sysModuleLoad(SYSMODULE_SYSUTIL);
    emit_rc("sysutil_module_load",rc);
    rc=msgDialogOpen2((msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_SINGLE_PROGRESSBAR),
        "GHL PS4 USB probe: plug dongle in, pair guitar, follow stages. Capture: 180 seconds.",
        NULL,NULL,NULL);
    emit_rc("dialog_open",rc);
    deadline=started_ms+(unsigned long long)DURATION_SEC*1000;
    while(running && milliseconds()<deadline) {
        unsigned long long t=elapsed();
        int s=stage_for(t);
        if(s!=previous_stage) {
            char msg[128];
            snprintf(msg,sizeof(msg),"Stage: %s",stages[s]);
            msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0,msg);
            emit("stage",NULL);
            previous_stage=s;
        }
        if(attached_dev<0 && device_id>=0) {
            attached_dev=device_id;
            emit("receiver_detected",NULL);
            inspect_descriptors(attached_dev);
            ctrl_pipe=usbOpenPipe(attached_dev,NULL);
            emit_rc("open_control_pipe",ctrl_pipe);
            if(ctrl_pipe>=0) send_keepalive();
        }
        if(detached) {
            pump_events();
            emit("receiver_detached",NULL);
            /* Stop transfers; do not close an in-flight pipe from callback. */
            if(!in_flight && !out_flight) {
                if(int_pipe>=0) usbClosePipe(int_pipe);
                if(ctrl_pipe>=0) usbClosePipe(ctrl_pipe);
                int_pipe=-1;ctrl_pipe=-1;attached_dev=-1;detached=0;
            }
        }
        if(attached_dev>=0 && !detached) {
            if(((int)(t/1000)-last_keepalive)>=7 && !out_flight) {
                send_keepalive();last_keepalive=(int)(t/1000);
            }
            listen_once();
        }
        pump_events();
        sysUtilCheckCallback();
        usleep(12000);
    }
    pump_events();
    emit("capture_complete",NULL);
    summary=fopen(sum_path,"wb");
    if(summary) {
        fprintf(summary,"GHL PS4 dongle probe v0.4-fresh\n");
        fprintf(summary,"VID:PID 1430:07bb, duration %d seconds\n",DURATION_SEC);
        fprintf(summary,"USB reports: %u\nUSB error events: %u\n",report_count,usb_errors);
        fprintf(summary,"Attaches: %u Detaches: %u SET_REPORT completions: %u\n",
                attach_count,detach_count,control_count);
        fprintf(summary,"Event queue dropped: %u\n",dropped);
        fprintf(summary,"Telemetry: %s\n",log_path);
        fclose(summary);
    }
    msgDialogAbort();
    if(registered) usbUnregisterExtraLdd(&ldd);
    /* A homebrew USB driver is unregistered before leaving. */
    if(int_pipe>=0 && !in_flight) usbClosePipe(int_pipe);
    if(ctrl_pipe>=0 && !out_flight) usbClosePipe(ctrl_pipe);
    if(mutex_ok) sysMutexDestroy(q_mutex);
    usbEnd();
    if(logfile) fclose(logfile);
    return 0;
}
