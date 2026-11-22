#ifndef WC_DISK_H
#define WC_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wc_platform.h"
#include "wc_types.h"

typedef struct wc_disk_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
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

#endif /* WC_DISK_H */
