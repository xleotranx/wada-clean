#ifndef WC_DEEP_H
#define WC_DEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wc_quarantine.h"
#include "wc_types.h"

#define WC_MAX_DRIVERS 256
#define WC_MAX_ORPHAN_MSI 256
#define WC_MAX_VHDX 32

typedef struct wc_winsxs_info {
    bool analyzed;
    bool cleanup_recommended;
    char actual_size_str[64];
    char backups_size_str[64];
    int reclaimable_packages;
    char raw_summary[256];
} wc_winsxs_info;

typedef struct wc_driver_item {
    char published_name[32];
    char original_name[128];
    char provider[96];
    char class_name[64];
    char version[64];
    char date[32];
    bool in_use;
    bool unused_superseded;
    bool selected;
} wc_driver_item;

typedef struct wc_orphan_msi_item {
    char path[WC_PATH_CAP];
    char name[128];
    uint64_t bytes;
    bool selected;
} wc_orphan_msi_item;

typedef struct wc_storage_advisor {
    bool compact_os_enabled;
    bool compact_os_known;
    uint64_t hiberfil_bytes;
    bool hiberfil_present;
    char vss_used_str[64];
    char vss_alloc_str[64];
    char vss_max_str[64];
    bool vss_known;
    uint64_t delivery_opt_bytes;
    uint64_t delivery_opt_files;
} wc_storage_advisor;

typedef struct wc_vhdx_item {
    char path[WC_PATH_CAP];
    char label[96];
    uint64_t bytes;
    bool selected;
} wc_vhdx_item;

typedef enum wc_deep_op {
    WC_DEEP_NONE = 0,
    WC_DEEP_SCAN_ALL = 1,
    WC_DEEP_CLEAN_WINSXS = 2,
    WC_DEEP_CLEAN_DELIVERY_OPT = 3,
    WC_DEEP_QUARANTINE_MSI = 4,
    WC_DEEP_SCAN_DRIVERS = 5,
    WC_DEEP_REMOVE_DRIVERS = 6,
    WC_DEEP_QUERY_STORAGE = 7,
    WC_DEEP_SET_COMPACT_OS = 8,
    WC_DEEP_SET_HIBERNATE = 9,
    WC_DEEP_RESIZE_VSS = 10,
    WC_DEEP_SCAN_VHDX = 11,
    WC_DEEP_COMPACT_VHDX = 12
} wc_deep_op;

typedef struct wc_deep_snapshot {
    wc_deep_op op;
    wc_job_state state;
    bool dry_run;
    bool reset_base;
    bool create_restore_point;

    wc_winsxs_info winsxs;

    int driver_count;
    wc_driver_item drivers[WC_MAX_DRIVERS];

    int msi_count;
    wc_orphan_msi_item msis[WC_MAX_ORPHAN_MSI];

    wc_storage_advisor advisor;

    int vhdx_count;
    wc_vhdx_item vhdxs[WC_MAX_VHDX];

    char output_summary[256];
    char error_msg[192];
} wc_deep_snapshot;

typedef struct wc_mutex wc_mutex;
typedef struct wc_thread wc_thread;

typedef struct wc_deep_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;

    wc_deep_op job_op;
    bool job_dry_run;
    bool job_bool_arg;
    bool job_restore_point;
    int job_int_arg;
    char job_names[WC_MAX_DRIVERS][32];
    int job_name_count;
    char job_paths[WC_MAX_ORPHAN_MSI][WC_PATH_CAP];
    int job_path_count;
    wc_quarantine *quarantine;

    wc_deep_snapshot *snap;
} wc_deep_worker;

/* Pure C99 validators and output parsers (unit-testable offline). */
bool wc_deep_is_safe_oem_inf(const char *name);
bool wc_deep_parse_dism_analyze(const char *output, wc_winsxs_info *out);
int wc_deep_parse_pnputil_drivers(const char *drivers_out, const char *devices_out,
                                  wc_driver_item *out, int max_items);
bool wc_deep_parse_compact_query(const char *output, bool *out_enabled);
bool wc_deep_parse_vss_storage(const char *output, wc_storage_advisor *out);
int wc_deep_filter_orphan_msi(const char *const *all_files, int all_count,
                              const char *const *referenced, int ref_count,
                              int *out_orphan_indices, int max_out);

/* Async deep clean worker lifecycle. */
bool wc_deep_worker_init(wc_deep_worker *w);
void wc_deep_worker_destroy(wc_deep_worker *w);
void wc_deep_worker_set_quarantine(wc_deep_worker *w, wc_quarantine *q);
void wc_deep_worker_cancel(wc_deep_worker *w);
bool wc_deep_worker_poll(wc_deep_worker *w, wc_deep_snapshot *out);

bool wc_deep_worker_start_scan_all(wc_deep_worker *w);
bool wc_deep_worker_start_clean_winsxs(wc_deep_worker *w, bool reset_base, bool dry_run,
                                       bool create_restore_point);
bool wc_deep_worker_start_clean_delivery_opt(wc_deep_worker *w, bool dry_run);
bool wc_deep_worker_start_quarantine_msi(wc_deep_worker *w, const char *const *paths, int count,
                                         bool dry_run, bool create_restore_point);
bool wc_deep_worker_start_scan_drivers(wc_deep_worker *w);
bool wc_deep_worker_start_remove_drivers(wc_deep_worker *w, const char *const *oem_names,
                                         int count, bool dry_run, bool create_restore_point);
bool wc_deep_worker_start_query_storage(wc_deep_worker *w);
bool wc_deep_worker_start_set_compact_os(wc_deep_worker *w, bool enable, bool dry_run);
bool wc_deep_worker_start_set_hibernate(wc_deep_worker *w, bool enable, bool dry_run);
bool wc_deep_worker_start_resize_vss(wc_deep_worker *w, int max_percent, bool dry_run);
bool wc_deep_worker_start_scan_vhdx(wc_deep_worker *w);
bool wc_deep_worker_start_compact_vhdx(wc_deep_worker *w, const char *const *paths, int count,
                                       bool dry_run);

#endif /* WC_DEEP_H */
