#ifndef GHL_PREFLIGHT_H
#define GHL_PREFLIGHT_H
#include <stddef.h>
#include <stdint.h>
#define GHL_PREFLIGHT_STAGES 12
#define GHL_PREFLIGHT_REQUIRED_STAGES 7
typedef struct {
    unsigned raw_reports;             /* all successful nonempty IN transfers */
    unsigned usable_reports;          /* report id 0x01, length >= 26 */
    unsigned invalid_reports;
    unsigned connection_yes;         /* reported PS4 guitar isConnected = 1 at byte25 */
    unsigned connection_no;          /* same field = 0 */
    unsigned connection_unknown;     /* other field value / short report */
    unsigned stage_frames[GHL_PREFLIGHT_STAGES];
    unsigned stage_actions[GHL_PREFLIGHT_STAGES];
    unsigned fret_mask;
    unsigned strum_directions;        /* bit 0 up, bit 1 down */
    unsigned whammy_events;
    unsigned tilt_events;
    unsigned max_report_len;
    unsigned min_report_len;
    uint8_t previous[64];
    int previous_length;
    int previous_stage;
} GhlPreflight;
void ghl_preflight_reset(GhlPreflight *p);
void ghl_preflight_feed(GhlPreflight *p,const uint8_t *data,size_t n,int stage);
unsigned ghl_preflight_stages_passed(const GhlPreflight *p);
int ghl_preflight_action_passed(const GhlPreflight *p,int stage);
const char *ghl_preflight_verdict(const GhlPreflight *p,int usb_claim,
     int descriptor_shape,int config_ok,int keepalive_ok,unsigned errors,int finished);
/* Verdict means only preflight evidence for a TRIAL, never game compatibility. */
#endif
