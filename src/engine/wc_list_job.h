#ifndef WC_LIST_JOB_H
#define WC_LIST_JOB_H

#include <stdbool.h>

#include "wc_platform.h"

/* A one-shot background enumeration: fills a caller-owned array, then lets the
 * UI pick up the result at a frame boundary. The caller owns `items`; the job
 * never touches it after `done_before` is observed. */
typedef void (*wc_list_fill_fn)(void *items, int max_items, int *out_count);

typedef struct wc_list_job {
    wc_mutex *mu;
    wc_thread *thread;
    wc_list_fill_fn fill;
    void *items;
    int item_size;
    int max_items;
    int count;
    bool done;
    bool running;
} wc_list_job;

bool wc_list_job_init(wc_list_job *job);
void wc_list_job_destroy(wc_list_job *job);

bool wc_list_job_start(wc_list_job *job, void *items, int item_size, int max_items,
                       wc_list_fill_fn fill);
/* Returns true exactly once after a started job has finished; `out_count` gets
 * the number of items written. Returns false while idle or running. */
bool wc_list_job_poll(wc_list_job *job, int *out_count);

#endif /* WC_LIST_JOB_H */
