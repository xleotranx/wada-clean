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

void wc_cookies_reload(wc_cleaner_state *st) {
    if (!st || !st->cookies.open) return;
    wada_err err;
    wada_err_clear(&err);
    int count = 0;
    if (wc_cookies_list(&st->cookies, st->cookie_items, WC_MAX_COOKIE_ALLOW, &count, &err) ==
        WADA_OK) {
        st->cookie_count = count;
    }
}

void wc_health_run_checkup(wc_cleaner_state *st) {
    if (!st || st->running) return;
    st->sys_drive_ok =
        wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free, &st->sys_drive_total);
    wc_startup_refresh(st);
    wc_cleaner_start_analyze(st);
}

void wc_cleaner_start_analyze(wc_cleaner_state *st) {
    if (!st || st->running) return;
    wada_ui_confirm_reset(&st->confirm);
    if (wc_worker_start(&st->worker, WC_JOB_ANALYZE, false, st->defs, st->selected,
                        st->target_count)) {
        st->running = true;
        wada_app_set_status(st->app, wada_tr("Analysis started"));
    }
}

void wc_cleaner_start_clean(wc_cleaner_state *st) {
    if (!st || st->running) return;
    if (!st->dry_run && !st->has_analyzed) {
        wada_app_set_status(st->app, wada_tr("Run Analyze before cleaning."));
        return;
    }
    if (!st->dry_run && !wada_ui_confirm_step(&st->confirm, 0, 300)) {
        wada_app_set_status(st->app, wada_tr("Click Clean Selected again to confirm."));
        return;
    }
    wada_ui_confirm_reset(&st->confirm);
    if (wc_worker_start(&st->worker, WC_JOB_CLEAN, st->dry_run, st->defs, st->selected,
                        st->target_count)) {
        st->running = true;
        wada_app_set_status(st->app, wada_tr(st->dry_run ? "Dry-run preview" : "Clean"));
    }
}

void wc_cleaner_cancel_job(wc_cleaner_state *st) {
    if (!st || !st->running) return;
    wc_worker_cancel(&st->worker);
}

void wc_cleaner_select_all(wc_cleaner_state *st, bool select) {
    if (!st || st->running) return;
    wada_ui_confirm_reset(&st->confirm);
    for (int i = 0; i < st->target_count; i++) {
        st->selected[i] = select;
    }
}

void wc_cleaner_select_category(wc_cleaner_state *st, wc_category cat, bool select) {
    if (!st || st->running) return;
    wada_ui_confirm_reset(&st->confirm);
    for (int i = 0; i < st->target_count; i++) {
        if (st->defs[i].category == cat) {
            st->selected[i] = select;
        }
    }
}

static void cmd_analyze(void *user) {
    wc_cleaner_state *st = (wc_cleaner_state *)user;
    st->active_view = WC_VIEW_CLEANER;
    wc_cleaner_start_analyze(st);
}

static void cmd_select_all(void *user) {
    wc_cleaner_select_all((wc_cleaner_state *)user, true);
}

static void cmd_select_none(void *user) {
    wc_cleaner_select_all((wc_cleaner_state *)user, false);
}

static void cmd_view_health(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_HEALTH;
}

static void cmd_view_cleaner(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_CLEANER;
}

static void cmd_view_startup(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_STARTUP;
}

static void cmd_view_apps(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_APPS;
}

static void cmd_view_disk(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_DISK;
}

static void cmd_view_updater(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_UPDATER;
}

static void cmd_view_exclusions(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_EXCLUSIONS;
}

static void cmd_view_automation(void *user) {
    ((wc_cleaner_state *)user)->active_view = WC_VIEW_AUTOMATION;
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
    st->active_view = WC_VIEW_HEALTH;
    st->is_admin = wc_plat_is_admin();
    st->dry_run = g_initial_dry_run;
    st->app_selected = -1;
    st->apps_last_sort = -1;
    st->apps_last_version = -1;
    st->sched_hour = 3.0;
    st->updater_snap.winget_available = true;
    wada_ui_confirm_init(&st->confirm);
    wada_ui_confirm_init(&st->app_confirm);

    st->sys_drive_ok =
        wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free, &st->sys_drive_total);

    st->startup_items = (wc_startup_item *)calloc(WC_MAX_STARTUP, sizeof *st->startup_items);
    st->app_items = (wc_app_item *)calloc(WC_MAX_APPS, sizeof *st->app_items);
    if (!st->startup_items || !st->app_items) {
        free(st->startup_items);
        free(st->app_items);
        free(st);
        wada_err_set(err, WADA_ERR_NOMEM, "cleaner buffers");
        return NULL;
    }

    if (!wc_worker_init(&st->worker) || !wc_disk_worker_init(&st->disk_worker) ||
        !wc_updater_worker_init(&st->updater_worker)) {
        wc_updater_worker_destroy(&st->updater_worker);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->startup_items);
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
        st->selected[i] = def->default_enabled;
        if (def->kind == WC_TARGET_RECYCLE_BIN) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "Recycle Bin");
        } else if (!wc_rules_resolve_path(def, st->resolved_paths[i], sizeof st->resolved_paths[i])) {
            snprintf(st->resolved_paths[i], sizeof st->resolved_paths[i], "%s", def->subpath);
        }
    }

    const wada_store_ops *ops = wada_ui_store_ops(app, "cleaner", err);
    if (!ops) {
        wc_updater_worker_destroy(&st->updater_worker);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->startup_items);
        free(st->app_items);
        free(st);
        return NULL;
    }

    char db_path[640];
    wada_ui_db_path(data_dir, db_path, sizeof db_path);
    if (wc_history_open(&st->history, ops, db_path, err) != WADA_OK ||
        wc_exclusions_open(&st->exclusions, ops, db_path, err) != WADA_OK ||
        wc_cookies_open(&st->cookies, ops, db_path, err) != WADA_OK) {
        wc_cookies_close(&st->cookies);
        wc_exclusions_close(&st->exclusions);
        wc_history_close(&st->history);
        wc_updater_worker_destroy(&st->updater_worker);
        wc_disk_worker_destroy(&st->disk_worker);
        wc_worker_destroy(&st->worker);
        free(st->startup_items);
        free(st->app_items);
        free(st);
        return NULL;
    }
    reload_history(st);
    wc_exclusions_reload(st);
    wc_cookies_reload(st);

    wada_app_command(app, scope, "cleaner.view_health", "Open PC Health Check", "Ctrl 1", cmd_view_health, st);
    wada_app_command(app, scope, "cleaner.view_cleaner", "Open Junk Cleaner", "Ctrl 2", cmd_view_cleaner, st);
    wada_app_command(app, scope, "cleaner.view_startup", "Open Startup Manager", "Ctrl 3", cmd_view_startup, st);
    wada_app_command(app, scope, "cleaner.view_apps", "Open Installed Apps", "Ctrl 4", cmd_view_apps, st);
    wada_app_command(app, scope, "cleaner.view_disk", "Open Disk Space Analyzer", "Ctrl 5", cmd_view_disk, st);
    wada_app_command(app, scope, "cleaner.view_updater", "Open Software Updater", "Ctrl 6", cmd_view_updater, st);
    wada_app_command(app, scope, "cleaner.view_exclusions", "Open Exclusions", "Ctrl 7", cmd_view_exclusions, st);
    wada_app_command(app, scope, "cleaner.view_automation", "Open Automation & Tray", "Ctrl 8", cmd_view_automation, st);
    wada_app_command(app, scope, "cleaner.analyze", "Analyze all junk targets", "Ctrl R", cmd_analyze, st);
    wada_app_command(app, scope, "cleaner.select_all", "Select all cleaner targets", NULL, cmd_select_all, st);
    wada_app_command(app, scope, "cleaner.select_none", "Deselect all cleaner targets", NULL, cmd_select_none, st);
    return st;
}

static void wc_cleaner_leave(void *state) {
    wc_cleaner_state *st = (wc_cleaner_state *)state;
    if (!st) return;
    wc_plat_tray_hide();
    wc_updater_worker_destroy(&st->updater_worker);
    wc_disk_worker_destroy(&st->disk_worker);
    wc_worker_destroy(&st->worker);
    wc_cookies_close(&st->cookies);
    wc_exclusions_close(&st->exclusions);
    wc_history_close(&st->history);
    free(st->startup_items);
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
                wada_app_set_status(app, wada_tr("Analysis complete"));
            } else if (st->snap.kind == WC_JOB_CLEAN) {
                record_clean_history(st);
                st->sys_drive_ok = wc_plat_disk_free_space(WC_SYS_DRIVE, &st->sys_drive_free,
                                                           &st->sys_drive_total);
                wada_app_set_status(
                    app, wada_tr(st->snap.dry_run ? "Dry-run preview complete" : "Clean complete"));
                if (st->tray_enabled && !st->snap.dry_run) {
                    char sz[32], msg[128];
                    wc_format_bytes(st->snap.total_bytes_cleaned, sz, sizeof sz);
                    wada_trf(msg, sizeof msg, "Cleaned %s across %llu files.", sz,
                             (unsigned long long)st->snap.total_files_cleaned);
                    wc_plat_tray_notify(wada_tr("Wada Clean"), msg);
                }
            }
        } else if (st->snap.state == WC_JOB_CANCELLED) {
            wada_app_set_status(app, wada_tr("Job cancelled"));
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
            wada_app_set_status(app, wada_tr("Disk analysis complete"));
        } else if (st->disk_snap.state == WC_JOB_CANCELLED) {
            wada_app_set_status(app, wada_tr("Disk analysis cancelled"));
        } else if (st->disk_snap.state == WC_JOB_ERROR && st->disk_snap.error_msg[0]) {
            wada_app_set_status(app, st->disk_snap.error_msg);
        }
    } else if (disk_now) {
        st->disk_running = true;
    }

    /* Poll winget updater worker */
    bool upd_now = wc_updater_worker_poll(&st->updater_worker, &st->updater_snap);
    if (st->updater_running && !upd_now) {
        st->updater_running = false;
        if (st->updater_snap.state == WC_JOB_DONE) {
            wada_app_set_status(app,
                                wada_tr(st->updater_snap.op == WC_UPD_CHECK
                                            ? "Updates check complete"
                                            : "Package update complete"));
        } else if (st->updater_snap.state == WC_JOB_CANCELLED) {
            wada_app_set_status(app, wada_tr("Updates cancelled"));
        } else if (st->updater_snap.state == WC_JOB_ERROR && st->updater_snap.error_msg[0]) {
            wada_app_set_status(app, st->updater_snap.error_msg);
        }
    } else if (upd_now) {
        st->updater_running = true;
    }

    if (st->running || st->disk_running || st->updater_running) {
        cui_request_wakeup(ui, 0.05f);
    }

    wada_ui_confirm_tick(&st->confirm, ui, 0);

    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL) {
        /* Left navigation sidebar */
        cui_column(ui, .width = CUI_PX(196), .height = CUI_FILL,
                   .bg = CUI_COLOR_SURFACE_MUTED, .pad = 12, .gap = 4) {
            cui_text(ui, wada_tr("Wada Clean"), .font = CUI_FONT_DISPLAY, .size = 18);
            cui_text(ui, wada_tr("System Utility"), .size = 11, .color = CUI_COLOR_TEXT_MUTED);

            cui_nav_section(ui, wada_tr("Tools"));
            if (cui_nav_item(ui, wada_tr("Health Check"), .icon = CUI_ICON_SHIELD,
                             .selected = st->active_view == WC_VIEW_HEALTH,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_HEALTH;
            }
            if (cui_nav_item(ui, wada_tr("Cleaner"), .icon = CUI_ICON_SPARKLES,
                             .selected = st->active_view == WC_VIEW_CLEANER,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_CLEANER;
            }
            if (cui_nav_item(ui, wada_tr("Startup"), .icon = CUI_ICON_ROCKET,
                             .selected = st->active_view == WC_VIEW_STARTUP,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_STARTUP;
            }
            if (cui_nav_item(ui, wada_tr("Apps"), .icon = CUI_ICON_PACKAGE,
                             .selected = st->active_view == WC_VIEW_APPS,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_APPS;
            }
            if (cui_nav_item(ui, wada_tr("Disk Analyzer"), .icon = CUI_ICON_FOLDER_OPEN,
                             .selected = st->active_view == WC_VIEW_DISK,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_DISK;
            }
            if (cui_nav_item(ui, wada_tr("Updater"), .icon = CUI_ICON_DOWNLOAD,
                             .selected = st->active_view == WC_VIEW_UPDATER,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_UPDATER;
            }
            if (cui_nav_item(ui, wada_tr("Exclusions"), .icon = CUI_ICON_LOCK,
                             .selected = st->active_view == WC_VIEW_EXCLUSIONS,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_EXCLUSIONS;
            }
            if (cui_nav_item(ui, wada_tr("Automation"), .icon = CUI_ICON_CLOCK,
                             .selected = st->active_view == WC_VIEW_AUTOMATION,
                             .group = "wc-nav")) {
                st->active_view = WC_VIEW_AUTOMATION;
            }

            cui_box(ui, .height = CUI_FILL) {}

            if (!st->is_admin) {
                if (cui_button(ui, wada_tr("Restart as Admin"), .variant = CUI_BUTTON_OUTLINE,
                               .size = CUI_SIZE_SM, .icon = CUI_ICON_SHIELD, .full_width = true)) {
                    wc_relaunch_admin(st, app);
                }
            }
            cui_badge(ui, wada_tr(st->is_admin ? "Administrator" : "Standard User"),
                      .tone = st->is_admin ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL);
        }

        cui_separator(ui);

        /* Active view */
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL) {
            switch (st->active_view) {
            case WC_VIEW_HEALTH:
                wc_view_health_draw(st, app);
                break;
            case WC_VIEW_CLEANER:
                wc_view_cleaner_draw(st, app);
                break;
            case WC_VIEW_STARTUP:
                wc_view_startup_draw(st, app);
                break;
            case WC_VIEW_APPS:
                wc_view_apps_draw(st, app);
                break;
            case WC_VIEW_DISK:
                wc_view_disk_draw(st, app);
                break;
            case WC_VIEW_UPDATER:
                wc_view_updater_draw(st, app);
                break;
            case WC_VIEW_EXCLUSIONS:
                wc_view_exclusions_draw(st, app);
                break;
            case WC_VIEW_AUTOMATION:
                wc_view_schedule_draw(st, app);
                break;
            }
        }
    }
}

static const wada_i18n_pair wc_cleaner_strings[] = {
    /* Shell & navigation */
    {"Wada Clean", "Wada Clean"},
    {"System Utility", "Tiện ích hệ thống"},
    {"Tools", "Công cụ"},
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
    {"Click Clean Selected again to confirm.", "Bấm Dọn mục đã chọn lần nữa để xác nhận."},
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
    {"Click Uninstall again to launch uninstaller.", "Bấm Gỡ cài đặt lần nữa để chạy trình gỡ."},
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
};

static const char *const wc_cleaner_walk[] = {
    "cleaner.view_cleaner",
    "cleaner.view_startup",
    "cleaner.view_apps",
    "cleaner.view_disk",
    "cleaner.view_updater",
    "cleaner.view_exclusions",
    "cleaner.view_automation",
    "cleaner.view_health",
};

static const char *const wc_cleaner_prefixes[] = {"clean_log/", "excl/", "cookie_allow/"};

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
