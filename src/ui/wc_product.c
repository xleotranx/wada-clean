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
    if (!st->app_items) {
        free(st);
        wada_err_set(err, WADA_ERR_NOMEM, "cleaner buffers");
        return NULL;
    }

    if (!wc_worker_init(&st->worker) || !wc_disk_worker_init(&st->disk_worker) ||
        !wc_list_job_init(&st->app_job)) {
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
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
        st->selected[i] = st->group_on[wc_target_group(def)];
        if (def->kind == WC_TARGET_RECYCLE_BIN) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "Recycle Bin");
        } else if (!wc_rules_resolve_path(def, st->resolved_paths[i], sizeof st->resolved_paths[i])) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "%s", def->subpath);
        }
    }

    const wada_store_ops *ops = wada_ui_store_ops(app, "cleaner", err);
    if (!ops) {
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
        free(st);
        return NULL;
    }

    char db_path[640];
    wada_ui_db_path(data_dir, db_path, sizeof db_path);
    if (wc_history_open(&st->history, ops, db_path, err) != WADA_OK) {
        wc_history_close(&st->history);
        wc_list_job_destroy(&st->app_job);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->app_items);
        free(st);
        return NULL;
    }
    reload_history(st);

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
    wc_list_job_destroy(&st->app_job);
    wc_disk_worker_destroy(&st->disk_worker);
    wc_worker_destroy(&st->worker);
    wc_history_close(&st->history);
    free(st->app_items);
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


    if (st->running || st->disk_running || st->apps_loading) {
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
    {"Apps", "Ứng dụng"},
    {"Administrator", "Quản trị viên"},
    {"Refresh", "Làm mới"},
    {"System", "Hệ thống"},

    /* Health Check view */
    {"%s freed", "Đã giải phóng %s"},

    /* Cleaner view */
    {"Analyze", "Phân tích"},
    {"Cancel", "Hủy"},
    {"Previewing…", "Đang xem trước…"},
    {"Run Analyze before cleaning.", "Hãy chạy Phân tích trước khi dọn."},
    {"Scanning: %s", "Đang quét: %s"},
    {"Cleaning: %s", "Đang dọn: %s"},
    {"%s (%llu files)", "%s (%llu tệp)"},
    {"Skipped", "Đã bỏ qua"},
    {"No clean runs recorded yet.", "Chưa có lần dọn nào được ghi lại."},
    {"Dry Run", "Chạy thử"},
    {"Clean", "Dọn dẹp"},
    {"%s | %llu files | %llu skipped", "%s | %llu tệp | %llu bỏ qua"},
    {"User Temp Files", "Tệp tạm người dùng"},
    {"Recycle Bin", "Thùng rác"},
    {"Thumbnail Cache", "Bộ nhớ đệm hình thu nhỏ"},
    {"Crash Dumps", "Bản ghi lỗi hệ thống"},
    {"Windows Temp", "Tệp tạm Windows"},
    {"Windows Update Cache", "Bộ nhớ đệm Windows Update"},
    {"Firefox Cache", "Bộ nhớ đệm Firefox"},
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
    {"Try clearing your search filter or refreshing.", "Hãy thử xóa bộ lọc tìm kiếm hoặc làm mới."},

    /* Installed Apps view */
    {"Installed Apps", "Ứng dụng đã cài"},
    {"Browse installed software and launch official uninstallers.",
     "Xem phần mềm đã cài và mở trình gỡ cài đặt chính thức."},
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
    {"Open", "Mở"},
    {"Reveal", "Hiện trong Explorer"},

    /* Software Updater view */

    /* Exclusions & Cookie Allowlist view */
    {"Browse…", "Chọn thư mục…"},

    /* Automation & Tray view */

    /* Second UI pass additions */
    {"Loading…", "Đang tải…"},
    {"Loading installed apps…", "Đang tải ứng dụng đã cài…"},

    /* Per-file preview */
    {"Back", "Quay lại"},

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
    {"Native Windows cleaner built with wada-c. MIT licensed.",
     "Trình dọn Windows gốc, xây dựng bằng wada-c. Giấy phép MIT."},
    {"Open data folder", "Mở thư mục dữ liệu"},
    {"Close", "Đóng"},

    /* Wave 1: Expanded rules, risk tiers & quarantine badges */
    {"DirectX Shader Cache", "Bộ đệm Shader DirectX"},
    {"NVIDIA Shader Cache", "Bộ đệm Shader NVIDIA"},
    {"AMD Shader Cache", "Bộ đệm Shader AMD"},
    {"Windows Error Reporting", "Báo cáo lỗi Windows"},
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

    /* Wave 3: Leftover scanner, Duplicates & Stale Dev Artifacts */
};

static const char *const wc_cleaner_walk[] = {
    "cleaner.view_home",
    "cleaner.view_tools",
    "cleaner.view_apps",
    "cleaner.view_disk",
};

static const char *const wc_cleaner_prefixes[] = {
    "clean_log/",
};

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
