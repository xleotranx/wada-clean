#ifndef WC_PRODUCT_H
#define WC_PRODUCT_H

#include <stdbool.h>
#include <stdint.h>

#include "wada_product.h"
#include "wada_ui.h"
#include "wc_cookies.h"
#include "wc_cookie_job.h"
#include "wc_disk.h"
#include "wc_exclusions.h"
#include "wc_history.h"
#include "wc_list_job.h"
#include "wc_types.h"
#include "wc_updater.h"
#include "wc_worker.h"

#define WC_HISTORY_UI_CAP 16
#define WC_SCROLL_PAD_BOTTOM 16.0f

/* Shared scrollable list container: measures true item heights and reserves
 * bottom breathing room so the last item is never clipped at the scroll edge. */
#define wc_scroll_list(ui, list_id, item_gap)                                           \
    cui_column((ui), .id = (list_id), .width = CUI_FILL, .height = CUI_FILL,            \
               .scroll_y = true, .gap = (item_gap), .pad_bottom = WC_SCROLL_PAD_BOTTOM)

typedef enum wc_view_id {
    WC_VIEW_HEALTH = 0,
    WC_VIEW_CLEANER = 1,
    WC_VIEW_STARTUP = 2,
    WC_VIEW_APPS = 3,
    WC_VIEW_DISK = 4,
    WC_VIEW_UPDATER = 5,
    WC_VIEW_EXCLUSIONS = 6,
    WC_VIEW_AUTOMATION = 7
} wc_view_id;

#define WC_APP_VERSION "0.1.0"

typedef struct wc_cleaner_state {
    wada_app *app;
    wc_view_id active_view;
    bool is_admin;
    bool dry_run;
    bool running;
    bool has_analyzed;
    bool preview_open;
    wc_preview_item preview_items[WC_MAX_PREVIEW];
    int preview_ui_count;
    bool preview_ui_truncated;

    /* System drive summary */
    uint64_t sys_drive_free;
    uint64_t sys_drive_total;
    bool sys_drive_ok;

    /* Cleaner view */
    int target_count;
    wc_target_def defs[WC_MAX_TARGETS];
    bool selected[WC_MAX_TARGETS];
    char resolved_paths[WC_MAX_TARGETS][WC_PATH_CAP];

    wc_worker worker;
    wc_worker_snapshot snap;

    wc_history history;
    wc_history_entry history_entries[WC_HISTORY_UI_CAP];
    int history_count;
    bool history_open;
    int history_selected;
    bool about_open;

    wada_ui_confirm confirm;

    /* Exclusions & Cookie Allowlist */
    int excl_tab;
    wc_exclusions exclusions;
    wc_exclusion_item exclusion_items[WC_MAX_EXCLUSIONS];
    int exclusion_count;
    char exclusion_draft[WC_PATH_CAP];
    char exclusion_error[160];

    wc_cookies cookies;
    wc_cookie_allow_item cookie_items[WC_MAX_COOKIE_ALLOW];
    int cookie_count;
    char cookie_draft[192];
    char cookie_error[160];
    bool cookie_dry_run;
    wc_cookie_job cookie_job;
    wc_cookie_snapshot cookie_snap;
    bool cookie_job_running;
    wada_ui_confirm cookie_confirm;

    /* Startup Manager view */
    wc_startup_item *startup_items;
    int startup_count;
    bool startup_loaded;
    bool startup_loading;
    wc_list_job startup_job;
    char startup_search[64];

    /* Installed Apps view */
    wc_app_item *app_items;
    int app_count;
    bool apps_loaded;
    bool apps_loading;
    wc_list_job app_job;
    char apps_search[64];
    char apps_last_query[64];
    int apps_sort;
    int apps_last_sort;
    int apps_version;
    int apps_last_version;
    int filtered_apps[WC_MAX_APPS];
    int filtered_app_count;
    int app_selected;
    wada_ui_confirm app_confirm;

    /* Disk Analyzer view */
    wc_disk_worker disk_worker;
    wc_disk_snapshot disk_snap;
    bool disk_running;
    char disk_root[WC_PATH_CAP];
    int disk_tab;

    /* Software Updater view */
    wc_updater_worker updater_worker;
    wc_updater_snapshot updater_snap;
    bool updater_running;

    /* Automation & System Tray view */
    wc_schedule_info sched_info;
    bool sched_loaded;
    int sched_freq_idx;
    double sched_hour;
    bool tray_enabled;

    char data_dir[WC_PATH_CAP];
    char status[128];
} wc_cleaner_state;

void wc_cleaner_set_initial_dry_run(bool dry_run);
bool wc_cleaner_take_relaunch_admin(void);
void wc_relaunch_admin(wc_cleaner_state *st, wada_app *app);

void wc_health_run_checkup(wc_cleaner_state *st);
void wc_cleaner_start_analyze(wc_cleaner_state *st);
void wc_cleaner_start_clean(wc_cleaner_state *st);

/* Newest completed (non-preview) run, or NULL when only dry runs exist. */
const wc_history_entry *wc_last_clean_run(const wc_cleaner_state *st);
void wc_cleaner_cancel_job(wc_cleaner_state *st);
void wc_cleaner_select_all(wc_cleaner_state *st, bool select);
void wc_cleaner_select_category(wc_cleaner_state *st, wc_category cat, bool select);

void wc_exclusions_reload(wc_cleaner_state *st);
void wc_cookies_reload(wc_cleaner_state *st);
void wc_startup_refresh(wc_cleaner_state *st);
void wc_apps_refresh(wc_cleaner_state *st);
void wc_disk_start_scan(wc_cleaner_state *st);

void wc_view_health_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_cleaner_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_startup_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_apps_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_disk_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_updater_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_exclusions_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_schedule_draw(wc_cleaner_state *st, wada_app *app);

extern const wada_product wc_cleaner_product;

#endif /* WC_PRODUCT_H */
