#ifndef GHL_LIVE_DASHBOARD_H
#define GHL_LIVE_DASHBOARD_H
typedef struct {
    int receiver;           /* Device attach callback received */
    int usb_ready;
    int configuration_ok;          /* Descriptor and both required pipes present */
    int init_sent;          /* At least one SET_REPORT submitted */
    int init_ok;            /* SET_REPORT completion callback was success */
    int init_error;
    int input_reports;      /* Number of successful nonempty input reports */
    int input_changes;      /* Count of payload changes, not inferred buttons */
    int input_error;
    int network_ready;
    int input_timeout_warned;
    int control_timeout_warned;
    char network_ip[32];
    int total_errors;
    int dropped_events;
    int elapsed_seconds;
    int testing_seconds;
    int stage;
    int finished;
    int usb_start_error;
    int last_error;
    char last_operation[48];
    char latest_report[90];
    char latest_length[24];
    char reason[90];
    char log_path[100];
} GhlDashboardState;

int ghl_ui_init(void);
void ghl_ui_draw(const GhlDashboardState *s);
void ghl_ui_shutdown(void);
#endif
