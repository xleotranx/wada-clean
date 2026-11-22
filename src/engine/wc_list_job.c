#include "wc_list_job.h"

#include <string.h>

static void list_job_entry(void *user) {
    wc_list_job *job = (wc_list_job *)user;
    int count = 0;
    job->fill(job->items, job->max_items, &count);

    wc_mutex_lock(job->mu);
    job->count = count;
    job->done = true;
    job->running = false;
    wc_mutex_unlock(job->mu);
}

bool wc_list_job_init(wc_list_job *job) {
    if (!job) return false;
    memset(job, 0, sizeof *job);
    job->mu = wc_mutex_create();
    return job->mu != NULL;
}

void wc_list_job_destroy(wc_list_job *job) {
    if (!job) return;
    wc_thread *thread = NULL;
    if (job->mu) {
        wc_mutex_lock(job->mu);
        thread = job->thread;
        job->thread = NULL;
        wc_mutex_unlock(job->mu);
    }
    if (thread) wc_thread_join(thread);
    if (job->mu) {
        wc_mutex_destroy(job->mu);
        job->mu = NULL;
    }
}

bool wc_list_job_start(wc_list_job *job, void *items, int item_size, int max_items,
                       wc_list_fill_fn fill) {
    if (!job || !job->mu || !items || item_size <= 0 || max_items <= 0 || !fill) return false;

    wc_mutex_lock(job->mu);
    if (job->running) {
        wc_mutex_unlock(job->mu);
        return false;
    }
    wc_thread *old_thread = job->thread;
    job->thread = NULL;
    wc_mutex_unlock(job->mu);
    if (old_thread) wc_thread_join(old_thread);

    wc_mutex_lock(job->mu);
    if (job->running) {
        wc_mutex_unlock(job->mu);
        return false;
    }
    job->fill = fill;
    job->items = items;
    job->item_size = item_size;
    job->max_items = max_items;
    job->count = 0;
    job->done = false;
    job->running = true;

    wc_thread *thread = wc_thread_start(list_job_entry, job);
    if (!thread) {
        job->running = false;
        wc_mutex_unlock(job->mu);
        return false;
    }
    job->thread = thread;
    wc_mutex_unlock(job->mu);
    return true;
}

bool wc_list_job_poll(wc_list_job *job, int *out_count) {
    if (!job || !job->mu) return false;

    wc_thread *done_thread = NULL;
    bool finished = false;
    int count = 0;

    wc_mutex_lock(job->mu);
    if (job->done && !job->running) {
        done_thread = job->thread;
        job->thread = NULL;
        job->done = false;
        finished = true;
        count = job->count;
    }
    wc_mutex_unlock(job->mu);

    if (done_thread) wc_thread_join(done_thread);
    if (out_count) *out_count = count;
    return finished;
}
