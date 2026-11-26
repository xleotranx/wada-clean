#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wada_log.h"
#include "wc_platform.h"
#include "wc_rules.h"
#include "wc_safety.h"

#ifdef _WIN32
#define WC_SYS_DRIVE "C:\\"
#define WC_HOME_ENV "USERPROFILE"
#else
#define WC_SYS_DRIVE "/"
#define WC_HOME_ENV "HOME"
#endif

static bool g_initial_dry_run = false;
static bool g_relaunch_admin = false;

void wc_cleaner_set_initial_dry_run(bool dry_run) {
    g_initial_dry_run = dry_run;
}

bool wc_cleaner_take_relaunch_admin(void) {
    bool v = g_relaunch_admin;
    g_relaunch_admin = false;
    return v;
}

void wc_relaunch_admin(wc_cleaner_state *st, wada_app *app) {
    (void)st;
    g_relaunch_admin = true;
    wada_app_quit(app);
}

static void reload_history(wc_cleaner_state *st) {
    if (!st->history.open) return;
    wada_err err;
    wada_err_clear(&err);
    int count = 0;
    if (wc_history_list(&st->history, st->history_entries, WC_HISTORY_UI_CAP, &count, &err) == WADA_OK) {
        st->history_count = count;
    }
}

void wc_exclusions_reload(wc_cleaner_state *st) {
    if (!st || !st->exclusions.open) return;
    wada_err err;
    wada_err_clear(&err);
    int count = 0;
    if (wc_exclusions_list(&st->exclusions, st->exclusion_items, WC_MAX_EXCLUSIONS, &count, &err) ==
        WADA_OK) {
        st->exclusion_count = count;
        wc_worker_set_exclusions(&st->worker, st->exclusion_items, count);
    }
}

void wc_quarantine_reload(wc_cleaner_state *st) {
    if (!st || !st->quarantine.open) return;
    wada_err err;
    wada_err_clear(&err);
    int count = 0;
    if (wc_quarantine_list(&st->quarantine, st->quarantine_items, WC_MAX_QUARANTINE, &count,
                           &err) == WADA_OK) {
        st->quarantine_count = count;
    }
}

static void report_scan_status(wc_cleaner_state *st, wada_app *app);

/* 0 = system/app/dev junk, 1 = browsers, 2 = Recycle Bin. */
int wc_target_group(const wc_target_def *def) {
    if (!def) return 0;
    if (def->kind == WC_TARGET_RECYCLE_BIN) return 2;
    if (def->category == WC_CAT_BROWSER) return 1;
    return 0;
}

void wc_home_apply_group(wc_cleaner_state *st, int group) {
    if (!st || group < 0 || group > 2) return;
    for (int i = 0; i < st->target_count; i++) {
        if (wc_target_group(&st->defs[i]) != group) continue;
        /* User-data targets that can only be quarantined are never auto-selected. */
        if (st->defs[i].quarantine) continue;
        st->selected[i] = st->group_on[group];
    }
    wada_ui_confirm_reset(&st->confirm);
    /* The status bar quotes the same number as the hero, so it must follow a
     * group toggle rather than wait for the next scan. */
    if (st->app && st->has_analyzed) report_scan_status(st, st->app);
}

void wc_home_refresh_recycle(wc_cleaner_state *st) {
    if (!st) return;
    st->recycle_known = wc_plat_query_recycle_bin(&st->recycle_files, &st->recycle_bytes);
    if (!st->recycle_known) {
        st->recycle_files = 0;
        st->recycle_bytes = 0;
    }
}

void wc_home_scan(wc_cleaner_state *st) {
    if (!st || st->running) return;
    st->scan_started = true;
    st->sys_drive_ok =
        wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free, &st->sys_drive_total);
    wc_home_refresh_recycle(st);
    wc_cleaner_start_analyze(st);
}

void wc_home_clean(wc_cleaner_state *st) {
    if (!st) return;
    /* The count shown on the Recycle Bin row may be minutes old; re-read it
     * right before the irreversible decision is armed. */
    if (st->group_on[2]) wc_home_refresh_recycle(st);
    wc_cleaner_start_clean(st);
}

static int count_flagged(const wc_worker_snapshot *snap, bool admin) {
    int n = 0;
    for (int i = 0; i < snap->target_count; i++) {
        if (admin ? snap->results[i].admin_blocked : snap->results[i].browser_running) n++;
    }
    return n;
}

uint64_t wc_home_selected_bytes(const wc_cleaner_state *st) {
    uint64_t total = 0;
    if (!st) return 0;
    for (int i = 0; i < st->target_count; i++) {
        if (!st->selected[i]) continue;
        if (st->defs[i].kind == WC_TARGET_RECYCLE_BIN) {
            if (st->recycle_known) total += st->recycle_bytes;
            continue;
        }
        const wc_target_result *r = &st->snap.results[i];
        if (r->browser_running) continue;
        if (r->analyzed) total += r->bytes_found;
    }
    return total;
}

/* The status bar is the only place a non-blocking job reports back, so it has
 * to state the same number the hero does, in plain words. */
static void report_scan_status(wc_cleaner_state *st, wada_app *app) {
    uint64_t bytes = wc_home_selected_bytes(st);
    char sz[32];
    wc_format_bytes(bytes, sz, sizeof sz);
    char msg[224];
    if (bytes == 0) {
        snprintf(msg, sizeof msg, "%s", wada_tr("Nothing to clean"));
    } else {
        wada_trf(msg, sizeof msg, "Found %s you can free", sz);
    }

    int admin = count_flagged(&st->snap, true);
    int browsers = count_flagged(&st->snap, false);
    size_t used = strlen(msg);
    if (admin == 1 && used < sizeof msg) {
        wada_trf(msg + used, sizeof msg - used, " · 1 location needs administrator");
    } else if (admin > 1 && used < sizeof msg) {
        wada_trf(msg + used, sizeof msg - used, " · %d locations need administrator", admin);
    }
    used = strlen(msg);
    if (browsers == 1 && used < sizeof msg) {
        wada_trf(msg + used, sizeof msg - used, " · 1 cache skipped (in use)");
    } else if (browsers > 1 && used < sizeof msg) {
        wada_trf(msg + used, sizeof msg - used, " · %d caches skipped (in use)", browsers);
    }
    wada_app_set_status(app, msg);
}

static void report_clean_status(wc_cleaner_state *st, wada_app *app) {
    char sz[32];
    wc_format_bytes(st->snap.total_bytes_cleaned, sz, sizeof sz);
    char msg[192];
    if (st->snap.dry_run) {
        wada_trf(msg, sizeof msg, "Preview: would free %s across %llu files", sz,
                 (unsigned long long)st->snap.total_files_cleaned);
    } else if (st->snap.total_skipped > 0) {
        wada_trf(msg, sizeof msg, "Freed %s across %llu files · %llu skipped", sz,
                 (unsigned long long)st->snap.total_files_cleaned,
                 (unsigned long long)st->snap.total_skipped);
    } else {
        wada_trf(msg, sizeof msg, "Freed %s across %llu files", sz,
                 (unsigned long long)st->snap.total_files_cleaned);
    }
    wada_app_set_status(app, msg);
}

const wc_history_entry *wc_last_clean_run(const wc_cleaner_state *st) {
    if (!st) return NULL;
    for (int i = 0; i < st->history_count; i++) {
        if (!st->history_entries[i].dry_run) return &st->history_entries[i];
    }
    return NULL;
}

void wc_cleaner_start_analyze(wc_cleaner_state *st) {    if (!st || st->running) return;
    wada_ui_confirm_reset(&st->confirm);
    st->preview_open = false;
    st->preview_ui_count = 0;
    st->history_open = false;
    if (wc_worker_start(&st->worker, WC_JOB_ANALYZE, false, st->defs, st->selected,
                        st->target_count)) {
        st->running = true;
        wada_app_set_status(st->app, wada_tr("Checking for junk…"));
    }
}

void wc_cleaner_start_clean(wc_cleaner_state *st) {
    if (!st || st->running) return;
    if (!st->dry_run && !st->has_analyzed) {
        wada_app_set_status(st->app, wada_tr("Run Analyze before cleaning."));
        return;
    }
    if (!st->dry_run && !wada_ui_confirm_step(&st->confirm, 0, 300)) {
        wada_app_set_status(st->app, wada_tr("Click again to confirm — nothing has been deleted yet."));
        return;
    }
    wada_ui_confirm_reset(&st->confirm);
    st->preview_open = false;
    st->preview_ui_count = 0;
    st->history_open = false;
    if (wc_worker_start(&st->worker, WC_JOB_CLEAN, st->dry_run, st->defs, st->selected,
                        st->target_count)) {
        st->running = true;
        wada_app_set_status(st->app,
                            wada_tr(st->dry_run ? "Previewing…" : "Freeing space…"));
    }
}

void wc_cleaner_cancel_job(wc_cleaner_state *st) {
    if (!st || !st->running) return;
    wc_worker_cancel(&st->worker);
}

static void cmd_analyze(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_HOME;
    st->preview_open = false;
    st->history_open = false;
    wc_home_scan(st);
}

static void cmd_preview_files(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_HOME;
    st->history_open = false;
    if (st->has_analyzed && st->preview_ui_count > 0) st->preview_open = true;
}

static void cmd_view_history(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_TOOLS;
    st->preview_open = false;
    if (st->history_count > 0) {
        st->history_selected = 0;
        st->history_open = true;
    }
}

static void cmd_about(void *user) {
    ((wc_cleaner_state *)user)->about_open = true;
}

static void cmd_view_home(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_HOME;
    st->preview_open = false;
}

static void cmd_view_tools(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_TOOLS;
    st->preview_open = false;
    st->history_open = false;
}

static void cmd_view_apps(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_APPS;
}

static void cmd_view_disk(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_DISK;
}

static void record_clean_history(wc_cleaner_state *st) {
    if (!st->history.open) return;

    int ran_targets = 0;
    for (int i = 0; i < st->snap.target_count; i++) {
        if (st->snap.results[i].selected) ran_targets++;
    }

    char size_buf[32];
    wc_format_bytes(st->snap.total_bytes_cleaned, size_buf, sizeof size_buf);

    wc_history_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.unix_sec = (long long)time(NULL);
    entry.dry_run = st->snap.dry_run;
    entry.files_cleaned = st->snap.total_files_cleaned;
    entry.bytes_cleaned = st->snap.total_bytes_cleaned;
    entry.skipped_count = st->snap.total_skipped;
    snprintf(entry.summary, sizeof entry.summary, "%s (%d targets)", size_buf, ran_targets);

    for (int i = 0; i < st->snap.target_count && i < st->target_count; i++) {
        const wc_target_result *res = &st->snap.results[i];
        if (!res->selected || !res->id[0]) continue;
        const char *label = res->id;
        for (int d = 0; d < st->target_count; d++) {
            if (strcmp(st->defs[d].id, res->id) == 0) {
                label = st->defs[d].label;
                break;
            }
        }
        wc_history_append_detail(&entry, res->id, label, res->files_cleaned,
                                 res->bytes_cleaned, res->skipped_count, res->note);
    }

    wada_err err;
    wada_err_clear(&err);
    if (wc_history_append(&st->history, &entry, &err) == WADA_OK) {
        reload_history(st);
    } else {
        wada_ui_status_err(st->app, st->status, sizeof st->status, &err);
    }
}

static void *wc_cleaner_enter(wada_app *app, wada_scope *scope, const char *data_dir, wada_err *err) {
    wc_cleaner_state *st = (wc_cleaner_state *)calloc(1, sizeof *st);
    if (!st) {
        wada_err_set(err, WADA_ERR_NOMEM, "cleaner");
        return NULL;
    }
    st->app = app;
    snprintf(st->data_dir, sizeof st->data_dir, "%s", data_dir ? data_dir : "");
    st->active_view = WC_VIEW_HOME;
    st->is_admin = wc_plat_is_admin();
    st->dry_run = g_initial_dry_run;
    st->app_selected = -1;
    st->apps_last_sort = -1;
    st->apps_last_version = -1;
    st->group_on[0] = true;
    st->group_on[1] = true;
    st->group_on[2] = false;
    wada_ui_confirm_init(&st->confirm);
    wada_ui_confirm_init(&st->app_confirm);

    st->sys_drive_ok =
        wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free, &st->sys_drive_total);

    st->app_items = (wc_app_item *)calloc(WC_MAX_APPS, sizeof *st->app_items);
    st->leftover_snap = (wc_leftover_snapshot *)calloc(1, sizeof *st->leftover_snap);
    if (!st->app_items || !st->leftover_snap) {
        free(st->app_items);
        free(st->leftover_snap);
        free(st);
        wada_err_set(err, WADA_ERR_NOMEM, "cleaner buffers");
        return NULL;
    }

    if (!wc_worker_init(&st->worker) || !wc_disk_worker_init(&st->disk_worker) ||
        !wc_list_job_init(&st->app_job) || !wc_leftover_worker_init(&st->leftover_worker)) {
        wc_leftover_worker_destroy(&st->leftover_worker);
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
        free(st->leftover_snap);
        free(st);
        wada_err_set(err, WADA_ERR, "worker init");
        return NULL;
    }

    if (!wc_plat_get_env(WC_HOME_ENV, st->disk_root, sizeof st->disk_root) ||
        !st->disk_root[0]) {
        snprintf(st->disk_root, sizeof st->disk_root, "%s", WC_SYS_DRIVE);
    }

    int count = wc_rules_count();
    if (count > WC_MAX_TARGETS) count = WC_MAX_TARGETS;
    st->target_count = count;
    for (int i = 0; i < count; i++) {
        const wc_target_def *def = wc_rules_get(i);
        st->defs[i] = *def;
        st->selected[i] = !def->quarantine && st->group_on[wc_target_group(def)];
        if (def->kind == WC_TARGET_RECYCLE_BIN) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "Recycle Bin");
        } else if (!wc_rules_resolve_path(def, st->resolved_paths[i], sizeof st->resolved_paths[i])) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "%s", def->subpath);
        }
    }

    const wada_store_ops *ops = wada_ui_store_ops(app, "cleaner", err);
    if (!ops) {
        wc_leftover_worker_destroy(&st->leftover_worker);
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
        free(st->leftover_snap);
        free(st);
        return NULL;
    }

    char db_path[640];
    wada_ui_db_path(data_dir, db_path, sizeof db_path);
    if (wc_history_open(&st->history, ops, db_path, err) != WADA_OK ||
        wc_exclusions_open(&st->exclusions, ops, db_path, err) != WADA_OK) {
        wc_exclusions_close(&st->exclusions);
        wc_history_close(&st->history);
        wc_leftover_worker_destroy(&st->leftover_worker);
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
        free(st->leftover_snap);
        free(st);
        return NULL;
    }
    reload_history(st);
    wc_exclusions_reload(st);

    if (data_dir && data_dir[0]) {
        char vault_dir[WC_PATH_CAP];
        snprintf(vault_dir, sizeof vault_dir, "%s" WADA_PATH_SEP "quarantine", data_dir);
        wada_err qerr;
        wada_err_clear(&qerr);
        if (wc_quarantine_open(&st->quarantine, ops, db_path, vault_dir, &qerr) == WADA_OK) {
            int purged = 0;
            wc_quarantine_purge_older_than(&st->quarantine, WC_QUARANTINE_DEFAULT_TTL_SEC, 0,
                                           &purged, &qerr);
            wc_quarantine_reload(st);
            wc_worker_set_quarantine(&st->worker, &st->quarantine);
            wc_leftover_worker_set_quarantine(&st->leftover_worker, &st->quarantine);
        }
    }

    wada_app_command(app, scope, "cleaner.view_home", "Free up space", NULL, cmd_view_home, st);
    wada_app_command(app, scope, "cleaner.view_tools", "Tools", NULL, cmd_view_tools, st);
    wada_app_command(app, scope, "cleaner.view_apps", "Installed Apps", NULL, cmd_view_apps, st);
    wada_app_command(app, scope, "cleaner.view_disk", "Space breakdown", NULL, cmd_view_disk, st);
    wada_app_command(app, scope, "cleaner.analyze", "Scan for reclaimable space", NULL, cmd_analyze, st);
    wada_app_command(app, scope, "cleaner.preview_files", "Review what will be removed", NULL, cmd_preview_files, st);
    wada_app_command(app, scope, "cleaner.view_history", "View latest clean run", NULL, cmd_view_history, st);
    wada_app_command(app, scope, "app.about", "About", NULL, cmd_about, st);
    return st;
}

static void wc_cleaner_leave(void *state) {
    wc_cleaner_state *st = (wc_cleaner_state *)state;
    if (!st) return;
    wc_leftover_worker_destroy(&st->leftover_worker);
    wc_list_job_destroy(&st->app_job);
    wc_disk_worker_destroy(&st->disk_worker);
    wc_worker_destroy(&st->worker);
    wc_exclusions_close(&st->exclusions);
    wc_quarantine_close(&st->quarantine);
    wc_history_close(&st->history);
    free(st->app_items);
    free(st->leftover_snap);
    free(st);
}

static void wc_cleaner_frame(void *state, wada_app *app) {
    wc_cleaner_state *st = (wc_cleaner_state *)state;
    st->app = app;
    cui_ctx *ui = wada_app_ui(app);

    /* Poll junk cleaner worker */
    bool now_running = wc_worker_poll(&st->worker, &st->snap);
    if (st->running && !now_running) {
        st->running = false;
        if (st->snap.state == WC_JOB_DONE) {
            if (st->snap.kind == WC_JOB_ANALYZE) {
                st->has_analyzed = true;
                st->preview_ui_count =
                    wc_worker_preview_copy(&st->worker, st->preview_items, WC_MAX_PREVIEW);
                st->preview_ui_truncated = wc_worker_preview_truncated(&st->worker);
                wc_home_refresh_recycle(st);
                report_scan_status(st, app);
            } else if (st->snap.kind == WC_JOB_CLEAN) {
                record_clean_history(st);
                wc_quarantine_reload(st);
                wc_home_refresh_recycle(st);
                st->sys_drive_ok = wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free,
                                                           &st->sys_drive_total);
                report_clean_status(st, app);
            }
        } else if (st->snap.state == WC_JOB_CANCELLED) {
            wada_app_set_status(app, wada_tr(st->snap.kind == WC_JOB_CLEAN
                                                 ? "Cancelled — nothing was deleted."
                                                 : "Scan cancelled."));
        } else if (st->snap.state == WC_JOB_ERROR && st->snap.error_msg[0]) {
            wada_app_set_status(app, st->snap.error_msg);
        }
    } else if (now_running) {
        st->running = true;
    }

    /* Poll disk analyzer worker */
    bool disk_now = wc_disk_worker_poll(&st->disk_worker, &st->disk_snap);
    if (st->disk_running && !disk_now) {
        st->disk_running = false;
        if (st->disk_snap.state == WC_JOB_DONE) {
            char sz[32], msg[160];
            wc_format_bytes(st->disk_snap.total_bytes, sz, sizeof sz);
            wada_trf(msg, sizeof msg, "Scan complete — %s across %llu files", sz,
                     (unsigned long long)st->disk_snap.total_files);
            wada_app_set_status(app, msg);
        } else if (st->disk_snap.state == WC_JOB_CANCELLED) {
            wada_app_set_status(app, wada_tr("Scan cancelled."));
        } else if (st->disk_snap.state == WC_JOB_ERROR && st->disk_snap.error_msg[0]) {
            wada_app_set_status(app, st->disk_snap.error_msg);
        }
    } else if (disk_now) {
        st->disk_running = true;
    }

    int listed = 0;
    if (st->apps_loading && wc_list_job_poll(&st->app_job, &listed)) {
        st->apps_loading = false;
        st->apps_loaded = true;
        st->app_count = listed;
        st->apps_version++;
        st->app_selected = listed > 0 ? 0 : -1;
        wada_ui_confirm_reset(&st->app_confirm);
    }

    bool leftover_now =
        st->leftover_snap ? wc_leftover_worker_poll(&st->leftover_worker, st->leftover_snap)
                          : false;
    if (st->leftover_running && !leftover_now) {
        st->leftover_running = false;
        if (st->leftover_snap->state == WC_JOB_DONE) {
            wc_quarantine_reload(st);
            if (st->leftover_snap->output_summary[0]) {
                wada_app_set_status(app, st->leftover_snap->output_summary);
            }
        } else if (st->leftover_snap->state == WC_JOB_ERROR &&
                   st->leftover_snap->error_msg[0]) {
            wada_app_set_status(app, st->leftover_snap->error_msg);
        }
    } else if (leftover_now) {
        st->leftover_running = true;
    }

    if (st->running || st->disk_running || st->leftover_running || st->apps_loading) {
        cui_request_wakeup(ui, 0.05f);
    }

    wada_ui_confirm_tick(&st->confirm, ui, 0);

    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL) {
        /* Left navigation sidebar */
        cui_column(ui, .width = CUI_PX(196), .height = CUI_FILL,
                   .bg = CUI_COLOR_SURFACE_MUTED, .pad = 12, .gap = 4) {
            cui_text(ui, wada_tr("Wada Clean"), .font = CUI_FONT_DISPLAY, .size = 18);
            cui_text(ui, wada_tr("Free up disk space"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            if (cui_nav_item(ui, wada_tr("Free up space"), .icon = CUI_ICON_SPARKLES,
                             .selected = st->active_view == WC_VIEW_HOME,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_HOME;
                st->preview_open = false;
            }
            bool in_tools = st->active_view == WC_VIEW_TOOLS || st->active_view == WC_VIEW_APPS ||
                            st->active_view == WC_VIEW_DISK;
            if (cui_nav_item(ui, wada_tr("Tools"), .icon = CUI_ICON_SETTINGS, .selected = in_tools,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_TOOLS;
                st->preview_open = false;
                st->history_open = false;
            }

            cui_box(ui, .height = CUI_FILL) {}
        }

        cui_separator(ui);

        /* Active view */
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL) {
            switch (st->active_view) {
            case WC_VIEW_HOME:
                wc_view_home_draw(st, app);
                break;
            case WC_VIEW_TOOLS:
                wc_view_tools_draw(st, app);
                break;
            case WC_VIEW_APPS:
                wc_view_apps_draw(st, app);
                break;
            case WC_VIEW_DISK:
                wc_view_disk_draw(st, app);
                break;
            }
        }
    }

    if (st->about_open) {
        cui_dialog(ui, wada_tr("About"), &st->about_open, .width = 480,
                   .description = wada_tr("App information.")) {
            cui_text(ui, "Wada Clean " WC_APP_VERSION, .font = CUI_FONT_DISPLAY, .size = 20);
            cui_text(ui, wada_tr("Native Windows cleaner built with wada-c. MIT licensed."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            if (st->data_dir[0]) {
                cui_text(ui, st->data_dir, .font = CUI_FONT_MONO, .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
            }
            cui_dialog_footer(ui) {
                if (cui_button(ui, wada_tr("Open data folder"),
                               .variant = CUI_BUTTON_OUTLINE)) {
                    if (st->data_dir[0]) wc_plat_open_in_explorer(st->data_dir, false);
                }
                if (cui_button(ui, wada_tr("Close"), .variant = CUI_BUTTON_PRIMARY,
                               .width = CUI_PX(110))) {
                    st->about_open = false;
                }
            }
        }
    }
}

static const wada_i18n_pair wc_cleaner_strings[] = {
    /* Shell & navigation */
    {"Wada Clean", "Wada Clean"},
    {"System Utility", "Tiện ích hệ thống"},
    {"Cleanup", "Dọn dẹp"},
    {"Manage", "Quản lý"},
    {"Settings", "Cài đặt"},
    {"Health Check", "Sức khỏe PC"},
    {"Cleaner", "Dọn rác"},
    {"Startup", "Khởi động"},
    {"Apps", "Ứng dụng"},
    {"Disk Analyzer", "Phân tích ổ đĩa"},
    {"Updater", "Cập nhật"},
    {"Exclusions", "Loại trừ"},
    {"Automation", "Tự động hóa"},
    {"Restart as Admin", "Mở lại quyền Admin"},
    {"Administrator", "Quản trị viên"},
    {"Standard User", "Người dùng chuẩn"},
    {"Open PC Health Check", "Mở Tổng quan sức khỏe"},
    {"Open Junk Cleaner", "Mở Dọn rác"},
    {"Open Startup Manager", "Mở Quản lý khởi động"},
    {"Open Installed Apps", "Mở Ứng dụng đã cài"},
    {"Open Disk Space Analyzer", "Mở Phân tích ổ đĩa"},
    {"Open Software Updater", "Mở Cập nhật phần mềm"},
    {"Open Exclusions", "Mở Danh sách loại trừ"},
    {"Open Automation & Tray", "Mở Tự động hóa & Khay hệ thống"},
    {"Refresh", "Làm mới"},
    {"System", "Hệ thống"},
    {"Admin", "Admin"},

    /* Health Check view */
    {"PC Health Check", "Kiểm tra sức khỏe PC"},
    {"One-click overview of disk space, reclaimable junk, startup impact, and clean history.",
     "Tổng quan một chạm về dung lượng ổ đĩa, rác có thể dọn, khởi động và lịch sử dọn."},
    {"System Ready", "Hệ thống sẵn sàng"},
    {"Not checked yet", "Chưa kiểm tra"},
    {"Attention Suggested", "Nên kiểm tra"},
    {"Analyzing System…", "Đang phân tích hệ thống…"},
    {"Click Run Health Check to scan junk targets and startup impact.",
     "Bấm Kiểm tra sức khỏe để quét rác và các mục khởi động."},
    {"Scanning system and browser targets…", "Đang quét các mục hệ thống và trình duyệt…"},
    {"Review the recommendations below to keep your PC clean and responsive.",
     "Xem các gợi ý bên dưới để giữ máy tính sạch sẽ và mượt mà."},
    {"Your system looks clean and responsive.", "Hệ thống của bạn đang sạch sẽ và mượt mà."},
    {"Run Health Check", "Kiểm tra sức khỏe"},
    {"Go to Cleaner", "Mở Dọn rác"},
    {"System Drive (C:)", "Ổ đĩa hệ thống (C:)"},
    {"%s free of %s", "Còn trống %s / %s"},
    {"Unavailable", "Không khả dụng"},
    {"Reclaimable Junk", "Rác có thể dọn"},
    {"Run checkup to analyze", "Chạy kiểm tra để phân tích"},
    {"%s across %llu files", "%s trong %llu tệp"},
    {"Startup Programs", "Ứng dụng khởi động"},
    {"Last Clean", "Lần dọn gần nhất"},
    {"Never", "Chưa từng"},
    {"%s freed", "Đã giải phóng %s"},
    {"Recommendations", "Đề xuất"},
    {"Reclaimable junk detected (%s). Open Junk Cleaner to preview and remove it.",
     "Phát hiện %s rác có thể dọn. Mở Dọn rác để xem trước và xóa."},
    {"%d startup items are enabled. Disabling unused ones can speed up boot times.",
     "Có %d mục khởi động đang bật. Tắt bớt ứng dụng không cần thiết giúp khởi động nhanh hơn."},
    {"Running as Standard User. Restart as Administrator to scan protected system folders.",
     "Đang chạy với quyền chuẩn. Mở lại bằng Quản trị viên để quét thư mục hệ thống."},
    {"Everything looks good! Check back periodically or configure Exclusions to protect paths.",
     "Mọi thứ đều ổn! Kiểm tra định kỳ hoặc cấu hình Loại trừ để bảo vệ thư mục quan trọng."},
    {"Review Junk Cleaner", "Xem Dọn rác"},
    {"Manage Startup Items", "Quản lý khởi động"},
    {"Manage Exclusions", "Quản lý loại trừ"},
    {"Restart as Administrator", "Mở lại quyền Quản trị viên"},

    /* Cleaner view */
    {"Junk Cleaner", "Dọn rác hệ thống"},
    {"Analyze first, preview exact sizes, and safely clean system & browser junk.",
     "Phân tích trước, xem dung lượng chính xác và dọn rác hệ thống & trình duyệt an toàn."},
    {"Dry run (preview only)", "Chạy thử (chỉ xem trước)"},
    {"Preview only", "Chỉ xem trước"},
    {"Analyze", "Phân tích"},
    {"Clean Selected", "Dọn mục đã chọn"},
    {"Confirm Clean", "Xác nhận dọn"},
    {"Cancel", "Hủy"},
    {"Select All", "Chọn tất cả"},
    {"Clear", "Bỏ chọn"},
    {"Ready to Analyze", "Sẵn sàng phân tích"},
    {"Ready to Clean", "Sẵn sàng dọn dẹp"},
    {"Analyzing…", "Đang phân tích…"},
    {"Cleaning…", "Đang dọn dẹp…"},
    {"Previewing…", "Đang xem trước…"},
    {"Clean Complete", "Đã dọn xong"},
    {"Preview Complete", "Đã xem trước"},
    {"Preview Clean", "Xem trước dọn"},
    {"Clean %s", "Dọn %s"},
    {"Selected to Clean", "Đã chọn để dọn"},
    {"%llu files across %d targets", "%llu tệp trong %d mục"},
    {"%d of %d targets selected", "Đã chọn %d / %d mục"},
    {"Click Analyze to scan selected targets and preview reclaimable space.",
     "Bấm Phân tích để quét các mục đã chọn và xem dung lượng có thể giải phóng."},
    {"Open folder", "Mở thư mục"},
    {"Cleaned", "Đã dọn"},
    {"Previewed", "Đã xem thử"},
    {"Run Analyze before cleaning.", "Hãy chạy Phân tích trước khi dọn."},
    {"Click Confirm Clean to confirm.", "Bấm Xác nhận dọn để tiếp tục."},
    {"Scanning: %s", "Đang quét: %s"},
    {"Cleaning: %s", "Đang dọn: %s"},
    {"Previewing: %s", "Đang xem trước: %s"},
    {"Browsers", "Trình duyệt"},
    {"Running", "Đang chạy"},
    {"Not scanned", "Chưa quét"},
    {"%s (%llu files)", "%s (%llu tệp)"},
    {"Cleaned %s (%llu files)", "Đã dọn %s (%llu tệp)"},
    {"Would clean %s (%llu files)", "Sẽ dọn %s (%llu tệp)"},
    {"Cleaned %s across %llu files.", "Đã dọn %s trong %llu tệp."},
    {"Summary", "Tổng quan"},
    {"Reclaimable", "Có thể giải phóng"},
    {"Files Found", "Tệp tìm thấy"},
    {"Last Freed", "Đã giải phóng"},
    {"Last Skipped", "Bỏ qua (lần trước)"},
    {"Skipped", "Đã bỏ qua"},
    {"Dry-run preview mode is enabled. No files will be deleted.",
     "Chế độ chạy thử đang bật. Không có tệp nào bị xóa."},
    {"Close running browsers before cleaning to avoid skipped profiles.",
     "Hãy đóng trình duyệt đang chạy trước khi dọn để tránh bị bỏ qua."},
    {"Recent History", "Lịch sử gần đây"},
    {"No clean runs recorded yet.", "Chưa có lần dọn nào được ghi lại."},
    {"Dry Run", "Chạy thử"},
    {"Clean", "Dọn dẹp"},
    {"%s | %llu files | %llu skipped", "%s | %llu tệp | %llu bỏ qua"},
    {"Analyze all junk targets", "Phân tích toàn bộ mục rác"},
    {"Select all cleaner targets", "Chọn tất cả mục dọn"},
    {"Deselect all cleaner targets", "Bỏ chọn tất cả mục dọn"},
    {"Analysis started", "Đã bắt đầu phân tích"},
    {"Analysis complete", "Phân tích hoàn tất"},
    {"Clean complete", "Dọn dẹp hoàn tất"},
    {"Dry-run preview complete", "Xem trước hoàn tất"},
    {"Job cancelled", "Đã hủy tác vụ"},
    {"User Temp Files", "Tệp tạm người dùng"},
    {"Emptied permanently — this cannot be undone.",
     "Bị xóa vĩnh viễn — không thể hoàn tác."},
    {"Recycle Bin", "Thùng rác"},
    {"Thumbnail Cache", "Bộ nhớ đệm hình thu nhỏ"},
    {"Crash Dumps", "Bản ghi lỗi hệ thống"},
    {"Windows Temp", "Tệp tạm Windows"},
    {"Windows Update Cache", "Bộ nhớ đệm Windows Update"},
    {"Chrome Cache", "Bộ nhớ đệm Chrome"},
    {"Edge Cache", "Bộ nhớ đệm Edge"},
    {"Brave Cache", "Bộ nhớ đệm Brave"},
    {"Firefox Cache", "Bộ nhớ đệm Firefox"},
    {"User Caches", "Bộ nhớ đệm người dùng"},
    {"Trash", "Thùng rác"},
    {"User Logs", "Nhật ký người dùng"},
    {"Crash Reports", "Báo cáo lỗi"},
    {"System Caches", "Bộ nhớ đệm hệ thống"},
    {"Safari Cache", "Bộ nhớ đệm Safari"},
    {"Could not query Recycle Bin", "Không thể kiểm tra Thùng rác"},
    {"Dry-run preview", "Xem trước chạy thử"},
    {"Failed to empty Recycle Bin", "Không thể làm trống Thùng rác"},
    {"Unavailable path", "Đường dẫn không khả dụng"},
    {"Browser is running", "Trình duyệt đang chạy"},
    {"Requires Administrator", "Cần quyền Quản trị viên"},
    {"Access denied", "Bị từ chối truy cập"},
    {"Skipped (browser running)", "Đã bỏ qua (trình duyệt đang chạy)"},
    {"Some files skipped (in use)", "Một số tệp bị bỏ qua (đang được dùng)"},

    /* Startup Manager view */
    {"Startup Manager", "Quản lý khởi động"},
    {"Disable or re-enable startup programs safely without deleting them.",
     "Tắt hoặc bật lại chương trình khởi động an toàn mà không xóa mục gốc."},
    {"Startup list refreshed", "Đã làm mới danh sách khởi động"},
    {"Search startup programs…", "Tìm chương trình khởi động…"},
    {"%d enabled / %d total", "%d đang bật / %d tổng"},
    {"No startup items found", "Không tìm thấy mục khởi động nào"},
    {"Try clearing your search filter or refreshing.", "Hãy thử xóa bộ lọc tìm kiếm hoặc làm mới."},
    {"HKCU Run", "HKCU Run"},
    {"HKLM Run", "HKLM Run"},
    {"User Startup Folder", "Thư mục Startup người dùng"},
    {"Common Startup Folder", "Thư mục Startup hệ thống"},
    {"Enabled", "Đang bật"},
    {"Disabled", "Đã tắt"},
    {"Updated startup item", "Đã cập nhật mục khởi động"},
    {"Could not update startup item", "Không thể cập nhật mục khởi động"},

    /* Installed Apps view */
    {"Installed Apps", "Ứng dụng đã cài"},
    {"Browse installed software and launch official uninstallers.",
     "Xem phần mềm đã cài và mở trình gỡ cài đặt chính thức."},
    {"Applications list refreshed", "Đã làm mới danh sách ứng dụng"},
    {"Search apps or publishers…", "Tìm ứng dụng hoặc nhà phát hành…"},
    {"Name", "Tên"},
    {"Size", "Dung lượng"},
    {"%d apps", "%d ứng dụng"},
    {"No matching applications", "Không có ứng dụng phù hợp"},
    {"Current User", "Người dùng hiện tại"},
    {"Application Details", "Chi tiết ứng dụng"},
    {"Select an application from the list to view details.",
     "Chọn một ứng dụng trong danh sách để xem chi tiết."},
    {"Publisher", "Nhà phát hành"},
    {"Version", "Phiên bản"},
    {"Install Date", "Ngày cài đặt"},
    {"Estimated Size", "Dung lượng ước tính"},
    {"Click Confirm Uninstall to launch the uninstaller.",
     "Bấm Xác nhận gỡ cài đặt để chạy trình gỡ."},
    {"Confirm Uninstall", "Xác nhận gỡ cài đặt"},
    {"Uninstall", "Gỡ cài đặt"},
    {"Uninstaller launched", "Đã mở trình gỡ cài đặt"},
    {"Failed to launch uninstaller", "Không thể mở trình gỡ cài đặt"},

    /* Disk Analyzer view */
    {"Disk Space Analyzer", "Phân tích dung lượng ổ đĩa"},
    {"Find large folders and files without modifying anything on disk.",
     "Tìm các thư mục và tệp lớn mà không thay đổi dữ liệu trên ổ đĩa."},
    {"Disk analysis started", "Đã bắt đầu phân tích ổ đĩa"},
    {"Disk analysis complete", "Phân tích ổ đĩa hoàn tất"},
    {"Disk analysis cancelled", "Đã hủy phân tích ổ đĩa"},
    {"Invalid folder path", "Đường dẫn thư mục không hợp lệ"},
    {"Folder path to analyze…", "Đường dẫn thư mục cần phân tích…"},
    {"Scan", "Quét"},
    {"Quick scan:", "Quét nhanh:"},
    {"Home", "Thư mục cá nhân"},
    {"Downloads", "Tải xuống"},
    {"AppData", "AppData"},
    {"Scanning %llu files (%s)…", "Đang quét %llu tệp (%s)…"},
    {"Total Size", "Tổng dung lượng"},
    {"Files", "Tệp"},
    {"Folders", "Thư mục"},
    {"Largest Folders", "Thư mục lớn nhất"},
    {"Largest Files", "Tệp lớn nhất"},
    {"Ready to analyze", "Sẵn sàng phân tích"},
    {"Choose a folder above and click Scan to see what uses disk space.",
     "Chọn một thư mục ở trên và bấm Quét để xem dung lượng sử dụng."},
    {"No subfolders found", "Không tìm thấy thư mục con"},
    {"Try scanning a parent folder or checking Largest Files.",
     "Hãy thử quét thư mục cha hoặc xem mục Tệp lớn nhất."},
    {"Open", "Mở"},
    {"No files found", "Không tìm thấy tệp"},
    {"No regular files were found in this folder.", "Không có tệp thông thường nào trong thư mục này."},
    {"Reveal", "Hiện trong Explorer"},

    /* Software Updater view */
    {"Software Updater", "Cập nhật phần mềm"},
    {"Detect outdated Windows applications with winget and install updates safely.",
     "Phát hiện ứng dụng Windows lỗi thời bằng winget và cập nhật an toàn."},
    {"%d updates available", "Có %d bản cập nhật"},
    {"Check for Updates", "Kiểm tra cập nhật"},
    {"Checking for updates…", "Đang kiểm tra cập nhật…"},
    {"Update All", "Cập nhật tất cả"},
    {"Updating all packages…", "Đang cập nhật toàn bộ gói…"},
    {"Querying winget for updates…", "Đang truy vấn winget…"},
    {"Installing updates via winget…", "Đang cài đặt bản cập nhật qua winget…"},
    {"winget is not installed or unavailable on this PC.",
     "winget chưa được cài đặt hoặc không khả dụng trên máy này."},
    {"Check for app updates", "Kiểm tra bản cập nhật ứng dụng"},
    {"Click Check for Updates to scan installed packages with winget.",
     "Bấm Kiểm tra cập nhật để quét các gói đã cài bằng winget."},
    {"All apps are up to date", "Tất cả ứng dụng đều đã mới nhất"},
    {"No pending updates were reported by winget.", "Không có bản cập nhật nào từ winget."},
    {"Update", "Cập nhật"},
    {"Updating package…", "Đang cập nhật gói…"},
    {"Updates check complete", "Kiểm tra cập nhật hoàn tất"},
    {"Package update complete", "Cập nhật gói hoàn tất"},
    {"Updates cancelled", "Đã hủy cập nhật"},

    /* Exclusions & Cookie Allowlist view */
    {"Protect specific files, folders, and trusted cookie domains from cleanup.",
     "Bảo vệ các tệp, thư mục và tên miền cookie tin cậy khỏi việc dọn dẹp."},
    {"Protected Paths", "Đường dẫn bảo vệ"},
    {"Cookie Allowlist", "Cookie tin cậy"},
    {"%d protected paths", "%d đường dẫn được bảo vệ"},
    {"Folder or file path to exclude (e.g. C:\\Users\\...)",
     "Đường dẫn thư mục hoặc tệp cần loại trừ (vd: C:\\Users\\...)"},
    {"Add", "Thêm"},
    {"Browse…", "Chọn thư mục…"},
    {"No exclusions configured", "Chưa có đường dẫn loại trừ nào"},
    {"Add a file or folder path above to keep Junk Cleaner from touching it.",
     "Thêm đường dẫn tệp hoặc thư mục ở trên để Dọn rác không đụng vào."},
    {"Remove", "Xóa bỏ"},
    {"Exclusion added", "Đã thêm mục loại trừ"},
    {"Path is already excluded", "Đường dẫn đã có trong danh sách loại trừ"},
    {"Enter a valid Windows path (e.g. C:\\Folder\\File)",
     "Nhập đường dẫn Windows hợp lệ (vd: C:\\Folder\\File)"},
    {"Exclusion removed", "Đã xóa mục loại trừ"},
    {"Domain added to allowlist", "Đã thêm tên miền tin cậy"},
    {"Domain is already allowlisted", "Tên miền đã có trong danh sách tin cậy"},
    {"Enter a valid domain (e.g. google.com)", "Nhập tên miền hợp lệ (vd: google.com)"},
    {"Cookie preview complete", "Xem trước dọn cookie hoàn tất"},
    {"Cookie cleanup complete", "Dọn cookie hoàn tất"},
    {"%d allowed domains", "%d tên miền tin cậy"},
    {"Domain to keep signed in (e.g. google.com)", "Tên miền giữ đăng nhập (vd: google.com)"},
    {"Preview Cookie Cleanup", "Xem trước dọn cookie"},
    {"Clean Browser Cookies", "Dọn cookie trình duyệt"},
    {"Preview: %llu cookies would be removed, %llu kept (%d browsers skipped).",
     "Xem trước: sẽ xóa %llu cookie, giữ %llu (bỏ qua %d trình duyệt)."},
    {"Cleaned %llu cookies, kept %llu allowlisted (%d browsers skipped).",
     "Đã xóa %llu cookie, giữ %llu tin cậy (bỏ qua %d trình duyệt)."},
    {"No allowed domains yet", "Chưa có tên miền tin cậy nào"},
    {"Add domains like github.com or google.com to preserve logins during cookie cleanup.",
     "Thêm tên miền như github.com hoặc google.com để giữ đăng nhập khi dọn cookie."},
    {"Domain removed", "Đã xóa tên miền"},

    /* Automation & Tray view */
    {"Automation & Tray", "Tự động hóa & Khay hệ thống"},
    {"Configure Windows Task Scheduler cleanups and system tray notifications.",
     "Cấu hình lịch dọn tự động qua Windows Task Scheduler và thông báo khay hệ thống."},
    {"Scheduled Cleaning", "Dọn tự động theo lịch"},
    {"Active", "Đang bật"},
    {"Off", "Tắt"},
    {"Daily", "Hằng ngày"},
    {"Weekly", "Hằng tuần"},
    {"Next run: %s", "Lần chạy tới: %s"},
    {"Runs safe non-admin temp cleanups silently via Windows Task Scheduler.",
     "Chạy dọn rác tạm an toàn (không cần Admin) qua Windows Task Scheduler."},
    {"Run Hour (24h)", "Giờ chạy (24h)"},
    {"Save Schedule", "Lưu lịch"},
    {"Schedule updated", "Đã cập nhật lịch"},
    {"Failed to update schedule", "Không thể cập nhật lịch"},
    {"System Tray", "Khay hệ thống"},
    {"Show a Wada Clean notification icon in the Windows taskbar tray.",
     "Hiển thị biểu tượng thông báo Wada Clean trên khay hệ thống Windows."},
    {"Show tray icon", "Hiện biểu tượng khay"},
    {"Tray icon enabled", "Đã bật biểu tượng khay"},
    {"Failed to show tray icon", "Không thể hiện biểu tượng khay"},
    {"Tray icon hidden", "Đã ẩn biểu tượng khay"},
    {"Send Test Notification", "Gửi thông báo thử"},
    {"System tray notifications are working.", "Thông báo khay hệ thống đang hoạt động."},

    /* Second UI pass additions */
    {"Ctrl K  Commands", "Ctrl K  Lệnh"},
    {"Covers every Chrome, Edge and Brave profile, plus Firefox. Close a browser to include it.",
     "Áp dụng cho mọi hồ sơ Chrome, Edge, Brave và Firefox. Đóng trình duyệt để tính cả."},
    {"No allowlisted domains: cleaning signs you out of every site.",
     "Chưa có tên miền tin cậy: dọn cookie sẽ đăng xuất khỏi mọi trang."},
    {"Confirm Cookie Clean", "Xác nhận dọn cookie"},
    {"Click Confirm Cookie Clean to continue.", "Bấm Xác nhận dọn cookie để tiếp tục."},
    {"Cookie cleanup cancelled", "Đã hủy dọn cookie"},
    {"Could not start cookie cleanup", "Không thể bắt đầu dọn cookie"},
    {"Previewing cookie stores…", "Đang xem trước cookie…"},
    {"Cleaning cookie stores…", "Đang dọn cookie…"},
    {"Checking %s (%d/%d)", "Đang kiểm tra %s (%d/%d)"},
    {"Finding cookie stores…", "Đang tìm kho cookie…"},
    {"Preview: %llu cookies would be removed, %llu kept. Skipped: %d browser(s), errors: %d.",
     "Xem trước: sẽ xóa %llu cookie, giữ %llu. Bỏ qua: %d trình duyệt, lỗi: %d."},
    {"Removed %llu cookies, kept %llu. Skipped: %d browser(s), errors: %d.",
     "Đã xóa %llu cookie, giữ %llu. Bỏ qua: %d trình duyệt, lỗi: %d."},
    {"No browser cookie stores were found.", "Không tìm thấy kho cookie trình duyệt nào."},
    {"Show the Wada Clean icon while the app is open. It is removed when Wada Clean closes.",
     "Hiện biểu tượng Wada Clean khi ứng dụng đang mở. Biểu tượng sẽ mất khi đóng ứng dụng."},
    {"Loading…", "Đang tải…"},
    {"Loading startup items…", "Đang tải mục khởi động…"},
    {"Loading installed apps…", "Đang tải ứng dụng đã cài…"},

    /* Per-file preview */
    {"Preview files", "Xem trước tệp"},
    {"Back", "Quay lại"},
    {"Preview sampled files", "Xem tệp mẫu"},
    {"%d files sampled from %d targets", "%d tệp từ %d mục"},
    {"Sampled from the last analysis. Up to %d files are listed.",
     "Lấy mẫu từ lần phân tích gần nhất. Tối đa %d tệp."},
    {"No files were sampled. Run Analyze again.",
     "Chưa lấy được tệp mẫu. Hãy chạy Phân tích lại."},
    {"Showing a sample of %d files. The Cleaner summary has the exact totals.",
     "Đang hiện mẫu %d tệp. Tổng quan ở Dọn rác có số liệu chính xác."},
    {"Recycle Bin contents cannot be listed; it is emptied entirely.",
     "Không thể liệt kê Thùng rác; thùng sẽ bị làm trống toàn bộ."},
    {"What will be removed", "Nội dung sẽ bị xóa"},
    {"Locked or in-use files are skipped when cleaning.",
     "Tệp đang bị khóa hoặc đang dùng sẽ bị bỏ qua khi dọn."},
    {"Analysis samples files from each selected target.",
     "Phân tích lấy mẫu tệp từ từng mục đã chọn."},

    /* History detail */
    {"View", "Xem"},
    {"Clean run", "Lần dọn"},
    {"Freed", "Đã giải phóng"},
    {"Targets", "Mục"},
    {"Copy details", "Sao chép chi tiết"},
    {"Per-target results", "Kết quả từng mục"},
    {"%s · %llu files · %llu skipped", "%s · %llu tệp · %llu bỏ qua"},
    {"This run predates detailed history; only totals are available.",
     "Lần dọn này chưa có chi tiết; chỉ có số liệu tổng."},
    {"View latest clean run", "Xem lần dọn gần nhất"},
    {"Preview run", "Lần xem trước"},
    {"Preview: %s across %llu files.", "Xem trước: %s với %llu tệp."},
    {"Freed %s across %llu files.", "Đã giải phóng %s với %llu tệp."},

    /* About & shortcuts */
    {"About & shortcuts", "Giới thiệu & phím tắt"},
    {"Keyboard shortcuts and app information.", "Phím tắt và thông tin ứng dụng."},
    {"Native Windows cleaner built with wada-c. MIT licensed.",
     "Trình dọn Windows gốc, xây dựng bằng wada-c. Giấy phép MIT."},
    {"Keyboard shortcuts", "Phím tắt"},
    {"Command palette", "Bảng lệnh"},
    {"Switch section", "Chuyển mục"},
    {"Analyze junk", "Phân tích rác"},
    {"Latest clean run", "Lần dọn gần nhất"},
    {"Toggle theme", "Đổi giao diện"},
    {"Open data folder", "Mở thư mục dữ liệu"},
    {"Close", "Đóng"},

    /* Wave 1: Expanded rules, risk tiers & quarantine badges */
    {"Moderate", "Trung bình"},
    {"Deep", "Sâu"},
    {"Quarantine", "Cách ly"},
    {"Developer Caches", "Bộ đệm lập trình"},
    {"Moved to Quarantine", "Đã chuyển vào Kho cách ly"},
    {"DirectX Shader Cache", "Bộ đệm Shader DirectX"},
    {"NVIDIA Shader Cache", "Bộ đệm Shader NVIDIA"},
    {"AMD Shader Cache", "Bộ đệm Shader AMD"},
    {"Windows Error Reporting", "Báo cáo lỗi Windows"},
    {"Old Installers in Downloads (30d+)", "Bộ cài cũ trong Tải xuống (30 ngày+)"},
    {"Chrome Cache (All Profiles)", "Bộ đệm Chrome (Mọi hồ sơ)"},
    {"Edge Cache (All Profiles)", "Bộ đệm Edge (Mọi hồ sơ)"},
    {"Brave Cache (All Profiles)", "Bộ đệm Brave (Mọi hồ sơ)"},
    {"VS Code Cache", "Bộ đệm VS Code"},
    {"Discord Cache", "Bộ đệm Discord"},
    {"Slack Cache", "Bộ đệm Slack"},
    {"Spotify Cache", "Bộ đệm Spotify"},
    {"npm Cache", "Bộ đệm npm"},
    {"pnpm Store Cache", "Kho bộ đệm pnpm"},
    {"pip Cache", "Bộ đệm pip"},
    {"uv Python Cache", "Bộ đệm Python uv"},
    {"NuGet Packages Cache", "Bộ đệm gói NuGet"},
    {"Cargo Registry Cache", "Bộ đệm Cargo Registry"},
    {"Gradle Cache", "Bộ đệm Gradle"},
    {"Go Build Cache", "Bộ đệm Go Build"},

    /* Wave 2 & 3: Deep Clean view */
    {"Deep Clean", "Dọn sâu hệ thống"},
    {"Open Deep Clean", "Mở Dọn sâu hệ thống"},
    {"Clean WinSxS, old drivers, orphaned installers, storage policies, and virtual disks.",
     "Dọn WinSxS, driver cũ, bộ cài mồ côi, chính sách lưu trữ và đĩa ảo WSL/Docker."},
    {"Restore point", "Điểm khôi phục"},
    {"Standard User mode: Restart as Administrator for WinSxS, DriverStore, and Storage Advisor changes.",
     "Đang chạy quyền chuẩn: Mở lại bằng Quản trị viên để thay đổi WinSxS, DriverStore và Lưu trữ."},
    {"Running deep maintenance task…", "Đang chạy tác vụ bảo trì sâu…"},
    {"Deep maintenance complete", "Bảo trì sâu hoàn tất"},
    {"Deep task cancelled", "Đã hủy tác vụ sâu"},
    {"OS & WinSxS", "Hệ điều hành & WinSxS"},
    {"DriverStore", "Kho Driver"},
    {"Storage Advisor", "Tối ưu lưu trữ"},
    {"Quarantine Vault", "Kho cách ly"},
    {"WSL / Docker", "WSL / Docker"},
    {"WinSxS Component Store (DISM)", "Kho thành phần WinSxS (DISM)"},
    {"Safely remove superseded Windows Update component backups via DISM.",
     "Xóa an toàn bản sao lưu Windows Update đã cũ thông qua DISM."},
    {"Cleanup Recommended", "Nên dọn dẹp"},
    {"Healthy", "Ổn định"},
    {"Actual Size", "Dung lượng thực"},
    {"Backups & Disabled", "Sao lưu & Tính năng tắt"},
    {"Reclaimable Packages", "Gói có thể thu hồi"},
    {"Deep /ResetBase (cannot uninstall current Windows updates afterward)",
     "Dọn sâu /ResetBase (sau đó không thể gỡ bản cập nhật hiện tại)"},
    {"Analyze WinSxS & OS", "Phân tích WinSxS & OS"},
    {"Analyzing deep OS targets…", "Đang phân tích mục hệ thống sâu…"},
    {"Clean WinSxS", "Dọn WinSxS"},
    {"Confirm WinSxS Clean", "Xác nhận dọn WinSxS"},
    {"Click Confirm WinSxS Clean to run DISM.", "Bấm Xác nhận dọn WinSxS để chạy DISM."},
    {"Running DISM component cleanup…", "Đang chạy dọn WinSxS qua DISM…"},
    {"Delivery Optimization Cache", "Bộ nhớ đệm Delivery Optimization"},
    {"Cached Windows Update files used for peer delivery on local networks.",
     "Tệp cập nhật Windows được đệm để chia sẻ trong mạng nội bộ."},
    {"Clear Cache", "Xóa bộ đệm"},
    {"Clearing Delivery Optimization cache…", "Đang xóa đệm Delivery Optimization…"},
    {"Orphaned Installers (C:\\Windows\\Installer)", "Bộ cài mồ côi (C:\\Windows\\Installer)"},
    {"Unreferenced .msi/.msp files verified against Windows Installer API; moved to Quarantine first.",
     "Tệp .msi/.msp không còn liên kết đã đối chiếu API Windows Installer; chuyển vào Kho cách ly trước."},
    {"%d selected (%s)", "Đã chọn %d (%s)"},
    {"Quarantine Selected", "Cách ly mục đã chọn"},
    {"Confirm Quarantine", "Xác nhận cách ly"},
    {"Click Confirm Quarantine to move orphaned installers.",
     "Bấm Xác nhận cách ly để di chuyển bộ cài mồ côi."},
    {"Moving orphaned installers to Quarantine…", "Đang chuyển bộ cài mồ côi vào Kho cách ly…"},
    {"No orphaned .msi/.msp files detected (run Analyze WinSxS & OS to scan).",
     "Không thấy tệp .msi/.msp mồ côi (bấm Phân tích WinSxS & OS để quét)."},
    {"Scan Drivers", "Quét Driver"},
    {"Scanning DriverStore packages…", "Đang quét gói trong DriverStore…"},
    {"Unused / superseded only", "Chỉ driver thừa / cũ"},
    {"Select Superseded", "Chọn bản cũ"},
    {"Remove Selected", "Gỡ mục đã chọn"},
    {"Confirm Remove Drivers", "Xác nhận gỡ Driver"},
    {"Click Confirm Remove Drivers to proceed.", "Bấm Xác nhận gỡ Driver để tiếp tục."},
    {"Removing selected drivers…", "Đang gỡ driver đã chọn…"},
    {"No drivers scanned yet", "Chưa quét driver"},
    {"Click Scan Drivers to list third-party OEM packages in DriverStore.",
     "Bấm Quét Driver để liệt kê gói OEM bên thứ ba trong DriverStore."},
    {"In Use", "Đang dùng"},
    {"Superseded", "Bản cũ"},
    {"Unused", "Không dùng"},
    {"Adjust OS-level compression, hibernation, and shadow copy storage limits.",
     "Điều chỉnh nén hệ điều hành, ngủ đông và giới hạn dung lượng Shadow Copy."},
    {"Querying storage configuration…", "Đang kiểm tra cấu hình lưu trữ…"},
    {"CompactOS System Compression", "Nén hệ thống CompactOS"},
    {"Unknown", "Chưa rõ"},
    {"Enable CompactOS", "Bật CompactOS"},
    {"Disable CompactOS", "Tắt CompactOS"},
    {"Updating CompactOS state…", "Đang cập nhật trạng thái CompactOS…"},
    {"Compresses Windows system binaries on SSDs (typically saves 2-4 GB with negligible CPU cost).",
     "Nén tệp hệ thống Windows trên SSD (thường tiết kiệm 2-4 GB với tải CPU rất thấp)."},
    {"Hibernation File (hiberfil.sys)", "Tệp ngủ đông (hiberfil.sys)"},
    {"Disable Hibernation", "Tắt ngủ đông"},
    {"Enable Hibernation", "Bật ngủ đông"},
    {"Confirm Disable", "Xác nhận tắt"},
    {"Click Confirm Disable to turn off hibernation.", "Bấm Xác nhận tắt để tắt chế độ ngủ đông."},
    {"Updating hibernation setting…", "Đang cập nhật thiết lập ngủ đông…"},
    {"Reclaims RAM-sized space if you only use Sleep (disables Fast Startup).",
     "Giải phóng dung lượng bằng kích thước RAM nếu bạn chỉ dùng Sleep (tắt Fast Startup)."},
    {"Volume Shadow Copy Storage (Restore Points)", "Lưu trữ Shadow Copy (Điểm khôi phục)"},
    {"Cap to 5%", "Giới hạn 5%"},
    {"Cap to 10%", "Giới hạn 10%"},
    {"Used", "Đã dùng"},
    {"Allocated", "Đã cấp phát"},
    {"Max Limit", "Giới hạn tối đa"},
    {"Click Refresh (requires Administrator) to query shadow storage.",
     "Bấm Làm mới (cần Quản trị viên) để kiểm tra lưu trữ Shadow Copy."},
    {"%d items (%s)", "%d mục (%s)"},
    {"Purge >14 days", "Xóa mục >14 ngày"},
    {"Expired quarantine items purged", "Đã xóa các mục cách ly quá hạn"},
    {"Quarantine Vault is empty", "Kho cách ly đang trống"},
    {"Items removed safely by Old Downloads, Orphaned Installers, or Leftovers appear here for 14 days.",
     "Các mục được dọn an toàn từ Tải xuống cũ, Bộ cài mồ côi hoặc Dư thừa sẽ lưu ở đây 14 ngày."},
    {"Restore", "Khôi phục"},
    {"Item restored", "Đã khôi phục mục"},
    {"Delete", "Xóa vĩnh viễn"},
    {"Quarantine item deleted", "Đã xóa vĩnh viễn mục cách ly"},
    {"Compacting runs 'wsl --shutdown' and stops running WSL/Docker containers.",
     "Thu gọn sẽ chạy 'wsl --shutdown' và dừng các container WSL/Docker đang chạy."},
    {"Scan Virtual Disks", "Quét đĩa ảo"},
    {"Scanning WSL/Docker virtual disks…", "Đang quét đĩa ảo WSL/Docker…"},
    {"Compact Selected", "Thu gọn mục chọn"},
    {"Confirm Compact", "Xác nhận thu gọn"},
    {"Click Confirm Compact to shut down WSL and shrink disks.",
     "Bấm Xác nhận thu gọn để tắt WSL và nén đĩa ảo."},
    {"Compacting virtual hard disks…", "Đang thu gọn đĩa cứng ảo…"},
    {"No WSL/Docker virtual disks found", "Không tìm thấy đĩa ảo WSL/Docker"},
    {"Click Scan Virtual Disks to discover Docker Desktop and WSL2 .vhdx files.",
     "Bấm Quét đĩa ảo để tìm các tệp .vhdx của Docker Desktop và WSL2."},

    /* Wave 3: Leftover scanner, Duplicates & Stale Dev Artifacts */
    {"Residual Leftovers", "Tệp dư thừa"},
    {"Scan Leftovers", "Quét dư thừa"},
    {"Scanning for leftover files…", "Đang quét tệp dư thừa…"},
    {"Scanning AppData & ProgramData…", "Đang quét AppData & ProgramData…"},
    {"No leftovers found.", "Không tìm thấy tệp dư thừa."},
    {"High", "Cao"},
    {"Medium", "Trung bình"},
    {"Move to Quarantine", "Chuyển vào Kho cách ly"},
    {"Duplicates", "Tệp trùng lặp"},
    {"Stale Dev Artifacts", "Thư mục build cũ"},
    {"Scanning for duplicate files…", "Đang quét tệp trùng lặp…"},
    {"Scanning for stale dev artifacts…", "Đang quét thư mục build cũ…"},
    {"Duplicate scan complete", "Quét tệp trùng lặp hoàn tất"},
    {"Stale artifacts scan complete", "Quét thư mục build cũ hoàn tất"},
    {"Hashing duplicate candidates…", "Đang băm SHA-256 kiểm tra trùng lặp…"},
    {"%d duplicate groups (%s reclaimable)", "%d nhóm trùng lặp (có thể thu hồi %s)"},
    {"Scan Duplicates (1 MB+)", "Quét trùng lặp (1 MB+)"},
    {"Select Extra Copies", "Chọn bản dư"},
    {"Move Selected to Quarantine", "Chuyển mục chọn vào Kho cách ly"},
    {"Find exact duplicate files", "Tìm tệp trùng lặp chính xác"},
    {"Click Scan Duplicates (1 MB+) to match files by size and SHA-256 while keeping one copy safe.",
     "Bấm Quét trùng lặp (1 MB+) để đối chiếu kích thước và SHA-256, luôn giữ an toàn 1 bản gốc."},
    {"No duplicate files found", "Không có tệp trùng lặp"},
    {"No exact duplicates (1 MB+) were found under this folder.",
     "Không tìm thấy tệp trùng lặp (1 MB+) trong thư mục này."},
    {"%s · %d copies", "%s · %d bản"},
    {"Keep", "Giữ lại"},
    {"Scanning for stale dev build folders…", "Đang quét thư mục build lập trình cũ…"},
    {"%d stale folders (%s)", "%d thư mục cũ (%s)"},
    {"Scan Stale Folders (30d+)", "Quét thư mục cũ (30d+)"},
    {"Find untouched developer build folders", "Tìm thư mục build lập trình bỏ quên"},
    {"Finds node_modules, target, .venv, __pycache__, and build directories untouched for 30+ days.",
     "Tìm node_modules, target, .venv, __pycache__ và build không đụng tới trong 30+ ngày."},
    {"No stale dev artifacts found", "Không thấy thư mục build cũ"},
    {"No untouched build/dependency directories (30d+) were found here.",
     "Không có thư mục build/thư viện nào bị bỏ quên 30+ ngày ở đây."},
    {"%lldd old", "%lld ngày"},
};

static const char *const wc_cleaner_walk[] = {
    "cleaner.view_cleaner",
    "cleaner.view_deep",
    "cleaner.view_startup",
    "cleaner.view_apps",
    "cleaner.view_disk",
    "cleaner.view_updater",
    "cleaner.view_exclusions",
    "cleaner.view_automation",
    "cleaner.view_health",
};

static const char *const wc_cleaner_prefixes[] = {
    "clean_log/", "excl/", "cookie_allow/", "quarantine/"};

const wada_product wc_cleaner_product = {
    "cleaner",
    "Wada Clean",
    wc_cleaner_enter,
    wc_cleaner_leave,
    wc_cleaner_frame,
    wc_cleaner_strings,
    (int)(sizeof wc_cleaner_strings / sizeof wc_cleaner_strings[0]),
    wc_cleaner_walk,
    (int)(sizeof wc_cleaner_walk / sizeof wc_cleaner_walk[0]),
    wc_cleaner_prefixes,
    (int)(sizeof wc_cleaner_prefixes / sizeof wc_cleaner_prefixes[0]),
};
