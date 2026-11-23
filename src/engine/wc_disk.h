#ifndef WC_DISK_H
#define WC_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wc_platform.h"
#include "wc_quarantine.h"
#include "wc_types.h"

#define WC_DUP_MAX_GROUPS 32
#define WC_DUP_MAX_FILES_PER_GROUP 16
#define WC_STALE_MAX_ITEMS 128

typedef struct wc_dup_file {
    char path[WC_PATH_CAP];
    bool selected; /* Never set true for index 0 (protected Keep copy) */
} wc_dup_file;

typedef struct wc_dup_group {
    uint64_t file_bytes;
    int file_count;
    wc_dup_file files[WC_DUP_MAX_FILES_PER_GROUP];
    uint8_t sha256[32];
} wc_dup_group;

typedef struct wc_dup_snapshot {
    wc_job_state state;
    char root_path[WC_PATH_CAP];
    uint64_t scanned_files;
    uint64_t hashed_files;
    uint64_t total_wasted_bytes;
    int group_count;
    wc_dup_group groups[WC_DUP_MAX_GROUPS];
    char output_summary[192];
    char error_msg[192];
} wc_dup_snapshot;

typedef struct wc_stale_item {
    char path[WC_PATH_CAP];
    char name[64];
    uint64_t bytes;
    long long age_days;
    bool selected;
} wc_stale_item;

typedef struct wc_stale_snapshot {
    wc_job_state state;
    char root_path[WC_PATH_CAP];
    uint64_t total_bytes;
    int count;
    wc_stale_item items[WC_STALE_MAX_ITEMS];
    char output_summary[192];
    char error_msg[192];
} wc_stale_snapshot;

typedef enum wc_disk_extra_op {
    WC_DEXTRA_NONE = 0,
    WC_DEXTRA_DUPS = 1,
    WC_DEXTRA_STALE = 2,
    WC_DEXTRA_QUARANTINE = 3
} wc_disk_extra_op;

typedef struct wc_disk_extra_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;
    wc_disk_extra_op op;
    char job_root[WC_PATH_CAP];
    uint64_t job_min_bytes;
    int job_min_age_days;
    char job_source_tag[64];
    char (*job_paths)[WC_PATH_CAP];
    int job_path_count;
    wc_quarantine *quarantine;
    wc_dup_snapshot *dup_snap;
    wc_stale_snapshot *stale_snap;
} wc_disk_extra_worker;

typedef struct wc_disk_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;
    char job_root[WC_PATH_CAP];
    wc_disk_snapshot snap;
} wc_disk_worker;

/* Pure top-N descending insertion helper (by entry->bytes). */
void wc_disk_insert_top(wc_disk_entry *arr, int *count, int cap, const wc_disk_entry *cand);

/* Pure StartupApproved REG_BINARY codec helpers. */
bool wc_startup_approved_is_enabled(const uint8_t *data, size_t len);
void wc_startup_approved_encode(bool enabled, uint8_t out[12]);

/* Async disk analyzer worker. */
bool wc_disk_worker_init(wc_disk_worker *w);
void wc_disk_worker_destroy(wc_disk_worker *w);
bool wc_disk_worker_start(wc_disk_worker *w, const char *root_path);
void wc_disk_worker_cancel(wc_disk_worker *w);
bool wc_disk_worker_poll(wc_disk_worker *w, wc_disk_snapshot *out);

/* 3-stage exact duplicate finder and stale dev artifact scanner. */
bool wc_dup_find(const char *root, uint64_t min_bytes, const volatile int *cancel_flag,
                 wc_dup_snapshot *out);
bool wc_stale_find(const char *root, int min_age_days, long long now_sec,
                   const volatile int *cancel_flag, wc_stale_snapshot *out);

bool wc_disk_extra_init(wc_disk_extra_worker *w);
void wc_disk_extra_destroy(wc_disk_extra_worker *w);
void wc_disk_extra_set_quarantine(wc_disk_extra_worker *w, wc_quarantine *q);
void wc_disk_extra_cancel(wc_disk_extra_worker *w);
bool wc_disk_extra_poll(wc_disk_extra_worker *w, wc_dup_snapshot *out_dup,
                        wc_stale_snapshot *out_stale, wc_disk_extra_op *out_op);
bool wc_disk_extra_start_dups(wc_disk_extra_worker *w, const char *root_path, uint64_t min_bytes);
bool wc_disk_extra_start_stale(wc_disk_extra_worker *w, const char *root_path, int min_age_days);
bool wc_disk_extra_start_quarantine(wc_disk_extra_worker *w, const char *const *paths, int count,
                                    const char *source_tag);

#endif /* WC_DISK_H */
