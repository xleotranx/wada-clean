#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wc_platform.h"

struct wc_mutex {
    pthread_mutex_t mu;
};

struct wc_thread {
    pthread_t th;
    wc_thread_fn fn;
    void *user;
};

bool wc_plat_get_env(const char *name, char *out, size_t cap) {
    if (!name || !name[0] || !out || cap == 0) return false;
    out[0] = '\0';
    const char *val = getenv(name);
    if (!val || !val[0]) return false;
    int n = snprintf(out, cap, "%s", val);
    return n > 0 && (size_t)n < cap;
}

bool wc_plat_is_admin(void) {
    return geteuid() == 0;
}

bool wc_plat_relaunch_as_admin(void) {
    return false;
}

bool wc_plat_process_running(const char *exe_name) {
    if (!exe_name || !exe_name[0]) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        char *argv[] = {"pgrep", "-x", (char *)exe_name, NULL};
        execvp("pgrep", argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void wc_plat_processes_running(const char *const *exe_names, int count, bool *out_running) {
    if (!out_running || count <= 0) return;
    for (int i = 0; i < count; i++) out_running[i] = false;
    if (!exe_names) return;

    for (int i = 0; i < count; i++) {
        if (!exe_names[i] || !exe_names[i][0]) continue;
        bool seen = false;
        for (int j = 0; j < i; j++) {
            if (exe_names[j] && strcmp(exe_names[i], exe_names[j]) == 0) {
                out_running[i] = out_running[j];
                seen = true;
                break;
            }
        }
        if (!seen) {
            out_running[i] = wc_plat_process_running(exe_names[i]);
        }
    }
}

wc_mutex *wc_mutex_create(void) {
    wc_mutex *m = (wc_mutex *)calloc(1, sizeof *m);
    if (!m) return NULL;
    if (pthread_mutex_init(&m->mu, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

void wc_mutex_destroy(wc_mutex *m) {
    if (!m) return;
    pthread_mutex_destroy(&m->mu);
    free(m);
}

void wc_mutex_lock(wc_mutex *m) {
    if (m) pthread_mutex_lock(&m->mu);
}

void wc_mutex_unlock(wc_mutex *m) {
    if (m) pthread_mutex_unlock(&m->mu);
}

static void *mac_thread_trampoline(void *param) {
    wc_thread *t = (wc_thread *)param;
    if (t && t->fn) t->fn(t->user);
    return NULL;
}

wc_thread *wc_thread_start(wc_thread_fn fn, void *user) {
    if (!fn) return NULL;
    wc_thread *t = (wc_thread *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->user = user;
    if (pthread_create(&t->th, NULL, mac_thread_trampoline, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void wc_thread_join(wc_thread *t) {
    if (!t) return;
    pthread_join(t->th, NULL);
    free(t);
}
