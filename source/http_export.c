/*
 * Minimal read-only PS3 HTTP export service.
 * Based on PS3DEV/PSL1GHT networking APIs used by samples/network/ps3load.
 * No login or encryption. Use ONLY on trusted local LAN, never port-forward.
 *
 * GET /                -> simple instructions
 * GET /telemetry.jsonl  -> original raw telemetry log
 * GET /summary.txt      -> final summary
 *
 * Separate PPU thread prevents browser downloads blocking live USB/UI tasks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>
#include <net/net.h>
#include <net/netctl.h>
#include <arpa/inet.h>
#include "http_export.h"

#define GHL_HTTP_PORT 8895
static volatile int server_active;
static int listen_fd=-1;
static int net_started,ctl_started,thread_started;
static sys_ppu_thread_t thread_id;
static char raw_path[160],summary_path[160];

static void send_all(int fd,const void *buf,size_t n) {
    const char *p=(const char*)buf;
    while(n && server_active) {
        int sent=(int)netSend(fd,p,n,0);
        if(sent<=0)break;
        n-=(size_t)sent;
        p+=sent;
    }
}
static void send_text(int fd,const char *status,const char *ctype,const char *data) {
    char hdr[320];
    size_t n=strlen(data);
    int len=snprintf(hdr,sizeof(hdr),
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
      "Connection: close\r\nCache-Control: no-store\r\n\r\n",
      status,ctype,(unsigned long)n);
    if(len>0 && len<(int)sizeof(hdr))send_all(fd,hdr,(size_t)len);
    send_all(fd,data,n);
}
static void send_file(int fd,const char *path,const char *filename) {
    char hdr[340];
    unsigned char data[4096];
    FILE *f=fopen(path,"rb");
    long length;
    if(!f) {
        send_text(fd,"404 Not Found","text/plain","Log not available yet. Capture must start first.\n");
        return;
    }
    if(fseek(f,0,SEEK_END)!=0 || (length=ftell(f))<0 || fseek(f,0,SEEK_SET)!=0) {
        fclose(f);
        send_text(fd,"500 Internal Server Error","text/plain","Unable to determine file size.\n");
        return;
    }
    int n=snprintf(hdr,sizeof(hdr),
      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
      "Content-Disposition: attachment; filename=\"%s\"\r\n"
      "Content-Length: %ld\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
      filename,length);
    if(n>0 && n<(int)sizeof(hdr))send_all(fd,hdr,(size_t)n);
    while(server_active) {
        size_t rd=fread(data,1,sizeof(data),f);
        if(!rd) break;
        send_all(fd,data,rd);
    }
    fclose(f);
}
static void server_proc(void *arg) {
    (void)arg;
    while(server_active) {
        struct sockaddr_in remote;
        socklen_t rlen=sizeof(remote);
        int client=netAccept(listen_fd,(struct sockaddr*)&remote,&rlen);
        if(client<0) {
            if(!server_active)break;
            usleep(30000);
            continue;
        }
        if(server_active) {
            char req[512]={0};
            int recvbytes=(int)netRecv(client,req,sizeof(req)-1,0);
            if(recvbytes>0 && strncmp(req,"GET /telemetry.jsonl ",21)==0) {
                send_file(client,raw_path,"telemetry.jsonl");
            } else if(recvbytes>0 && strncmp(req,"GET /summary.txt ",17)==0) {
                send_file(client,summary_path,"summary.txt");
            } else if(recvbytes>0 && strncmp(req,"GET / ",6)==0) {
                send_text(client,"200 OK","text/html; charset=utf-8",
                    "<!doctype html><title>GHL PS3 diagnostic</title>"
                    "<meta name=\"viewport\" content=\"width=device-width\">"
                    "<h2>GHL PS4 Dongle Diagnostic</h2>"
                    "<p>Read-only LAN export. Wait for logs to appear, then download.</p>"
                    "<p><a href=\"/telemetry.jsonl\">Download raw telemetry.jsonl</a></p>"
                    "<p><a href=\"/summary.txt\">Download summary.txt</a></p>"
                    "<p>No device data has been uploaded to an external service.</p>");
            } else {
                send_text(client,"404 Not Found","text/plain","Endpoint not found.\n");
            }
        }
        netClose(client);
    }
    sysThreadExit(0);
}
int ghl_http_start(const char *telemetry,const char *summary,char *ip_out,unsigned ip_size) {
    struct sockaddr_in addr;
    int rc;
    union net_ctl_info info;
    if(ip_out && ip_size)snprintf(ip_out,ip_size,"PS3_IP");
    snprintf(raw_path,sizeof(raw_path),"%s",telemetry);
    snprintf(summary_path,sizeof(summary_path),"%s",summary);
    rc=sysModuleLoad(SYSMODULE_NET);
    if(rc!=0 && rc!=SYSMODULE_ERR_DUPLICATE)return rc;
    rc=netInitialize();
    if(rc!=0)return rc;
    net_started=1;
    rc=netCtlInit();
    if(rc==0) {
        ctl_started=1;
        memset(&info,0,sizeof(info));
        if(netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS,&info)==0 && ip_out && ip_size) {
            snprintf(ip_out,ip_size,"%s",info.ip_address);
        }
    }
    listen_fd=netSocket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listen_fd<0)return listen_fd;
    memset(&addr,0,sizeof(addr));
    addr.sin_len=sizeof(addr);addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(GHL_HTTP_PORT);
    rc=netBind(listen_fd,(struct sockaddr*)&addr,sizeof(addr));
    if(rc<0)return rc;
    rc=netListen(listen_fd,2);
    if(rc<0)return rc;
    server_active=1;
    rc=sysThreadCreate(&thread_id,server_proc,NULL,1200,0x20000,THREAD_JOINABLE,"GHL HTTP export");
    if(rc!=0) {server_active=0;return rc;}
    thread_started=1;
    return 0;
}
void ghl_http_stop(void) {
    server_active=0;
    if(listen_fd>=0) {
        netShutdown(listen_fd,SHUT_RDWR);
        netClose(listen_fd);
        listen_fd=-1;
    }
    if(thread_started) {
        u64 result=0;
        sysThreadJoin(thread_id,&result);
        thread_started=0;
    }
    if(ctl_started){netCtlTerm();ctl_started=0;}
    if(net_started){netDeinitialize();net_started=0;}
}
