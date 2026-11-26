#ifndef WC_PRODUCT_H
#define WC_PRODUCT_H

#include <stdbool.h>
#include <stdint.h>

#include "wada_product.h"
#include "wada_ui.h"
#include "wc_disk.h"
#include "wc_exclusions.h"
#include "wc_history.h"
#include "wc_leftovers.h"
#include "wc_list_job.h"
#include "wc_quarantine.h"
#include "wc_types.h"
#include "wc_worker.h"

#define WC_HISTORY_UI_CAP 16
#define WC_SCROLL_PAD_BOTTOM 16.0f

/* Shared scrollable list container: measures true item heights and reserves
 * bottom breathing room so the last item is never clipped at the scroll edge. */
#define wc_scroll_list(ui, list_id, item_gap)                                           \
    cui_column((ui), .id = (list_id), .width = CUI_FILL, .height = CUI_FILL,            \
               .scroll_y = true, .gap = (item_gap), .pad_bottom = WC_SCROLL_PAD_BOTTOM)

/* Step 2 of the One Door plan: one home that answers "how much can I get
 * back?", and a single Tools door for everything else. */
typedef enum wc_view_id {
    WC_VIEW_HOME = 0,
    WC_VIEW_TOOLS = 1,
    WC_VIEW_APPS = 2,
    WC_VIEW_DISK = 3
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

    wc_quarantine quarantine;
    wc_quarantine_item quarantine_items[WC_MAX_QUARANTINE];
    int quarantine_count;

    wada_ui_confirm confirm;

    /* Exclusions are no longer editable in the UI, but rules already persisted
     * must keep being honoured by the worker until the engine is removed. */
    wc_exclusions exclusions;
    wc_exclusion_item exclusion_items[WC_MAX_EXCLUSIONS];
    int exclusion_count;

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
    wc_leftover_worker leftover_worker;
    wc_leftover_snapshot *leftover_snap;
    bool leftover_running;

    /* Space view */
    wc_disk_worker disk_worker;
    wc_disk_snapshot disk_snap;
    bool disk_running;
    char disk_root[WC_PATH_CAP];

    /* Home: three user-facing decisions. 0 = system/app/dev junk, 1 = browsers,
     * 2 = Recycle Bin. The internal rules never surface as checkboxes. */
    bool group_on[3];
    bool scan_started;
    uint64_t recycle_files;
    uint64_t recycle_bytes;
    bool recycle_known;

    char data_dir[WC_PATH_CAP];
    char status[128];
} wc_cleaner_state;

void wc_cleaner_set_initial_dry_run(bool dry_run);
bool wc_cleaner_take_relaunch_admin(void);
void wc_relaunch_admin(wc_cleaner_state *st, wada_app *app);

/* Home screen actions. */
void wc_home_scan(wc_cleaner_state *st);
void wc_home_clean(wc_cleaner_state *st);
void wc_home_apply_group(wc_cleaner_state *st, int group);
void wc_home_refresh_recycle(wc_cleaner_state *st);

/* Bytes the cleaner can actually free right now: selected, analysed, and not
 * skipped because a browser holds them open. Shared by the headline and the
 * status bar so the two can never disagree. */
uint64_t wc_home_selected_bytes(const wc_cleaner_state *st);

/* 0 = system/app/dev junk, 1 = browsers, 2 = Recycle Bin. */
int wc_target_group(const wc_target_def *def);

void wc_cleaner_start_analyze(wc_cleaner_state *st);
void wc_cleaner_start_clean(wc_cleaner_state *st);

/* Newest completed (non-preview) run, or NULL when only dry runs exist. */
const wc_history_entry *wc_last_clean_run(const wc_cleaner_state *st);
void wc_cleaner_cancel_job(wc_cleaner_state *st);

void wc_exclusions_reload(wc_cleaner_state *st);
void wc_quarantine_reload(wc_cleaner_state *st);
void wc_apps_refresh(wc_cleaner_state *st);
void wc_disk_start_scan(wc_cleaner_state *st);

void wc_view_home_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_tools_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_apps_draw(wc_cleaner_state *st, wada_app *app);
void wc_view_disk_draw(wc_cleaner_state *st, wada_app *app);

extern const wada_product wc_cleaner_product;

#endif /* WC_PRODUCT_H */
