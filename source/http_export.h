#ifndef GHL_HTTP_EXPORT_H
#define GHL_HTTP_EXPORT_H
int ghl_http_start(const char *telemetry_path,const char *summary_path,char *ip_out,unsigned ip_size);
void ghl_http_stop(void);
#endif
