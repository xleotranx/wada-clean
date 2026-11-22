#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"

static void add_exclusion_from_draft(wc_cleaner_state *st, wada_app *app) {
    if (!st->exclusion_draft[0] || !st->exclusions.open) return;

    st->exclusion_error[0] = '\0';
    wada_err err;
    wada_err_clear(&err);
    wada_status status = wc_exclusions_add(&st->exclusions, st->exclusion_draft, &err);
    if (status == WADA_OK) {
        st->exclusion_draft[0] = '\0';
        wc_exclusions_reload(st);
        wada_app_set_status(app, wada_tr("Exclusion added"));
    } else if (status == WADA_ERR_EXISTS) {
        snprintf(st->exclusion_error, sizeof st->exclusion_error, "%s",
                 wada_tr("Path is already excluded"));
    } else {
        snprintf(st->exclusion_error, sizeof st->exclusion_error, "%s",
                 wada_tr("Enter a valid Windows path (e.g. C:\\Folder\\File)"));
    }
}

static void add_cookie_from_draft(wc_cleaner_state *st, wada_app *app) {
    if (!st->cookie_draft[0] || !st->cookies.open) return;

    st->cookie_error[0] = '\0';
    wada_err err;
    wada_err_clear(&err);
    wada_status status = wc_cookies_add(&st->cookies, st->cookie_draft, &err);
    if (status == WADA_OK) {
        st->cookie_draft[0] = '\0';
        wc_cookies_reload(st);
        wada_app_set_status(app, wada_tr("Domain added to allowlist"));
    } else if (status == WADA_ERR_EXISTS) {
        snprintf(st->cookie_error, sizeof st->cookie_error, "%s",
                 wada_tr("Domain is already allowlisted"));
    } else {
        snprintf(st->cookie_error, sizeof st->cookie_error, "%s",
                 wada_tr("Enter a valid domain (e.g. google.com)"));
    }
}

static void start_cookie_job(wc_cleaner_state *st, wada_app *app) {
    if (st->cookie_job_running) return;
    bool dry_run = st->cookie_dry_run;
    if (!dry_run && !wada_ui_confirm_step(&st->cookie_confirm, 0, 300)) {
        wada_app_set_status(app, wada_tr("Click Confirm Cookie Clean to continue."));
        return;
    }
    wada_ui_confirm_reset(&st->cookie_confirm);
    if (wc_cookie_job_start(&st->cookie_job, dry_run, st->cookie_items, st->cookie_count)) {
        st->cookie_job_running = true;
        wada_app_set_status(
            app, wada_tr(dry_run ? "Previewing cookie stores…" : "Cleaning cookie stores…"));
    } else {
        wada_app_set_status(app, wada_tr("Could not start cookie cleanup"));
    }
}

static void draw_paths_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    char count_text[64];
    wada_trf(count_text, sizeof count_text, "%d protected paths", st->exclusion_count);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
        if (cui_input(ui, "excl-draft", st->exclusion_draft, sizeof st->exclusion_draft,
                      .placeholder = wada_tr("Folder or file path to exclude (e.g. C:\\Users\\...)"),
                      .width = CUI_FILL)) {
            st->exclusion_error[0] = '\0';
        }
#ifdef _WIN32
        if (cui_button(ui, wada_tr("Browse…"), .variant = CUI_BUTTON_OUTLINE,
                       .icon = CUI_ICON_FOLDER_OPEN)) {
            char picked[WC_PATH_CAP];
            if (wc_plat_pick_folder(picked, sizeof picked)) {
                snprintf(st->exclusion_draft, sizeof st->exclusion_draft, "%s", picked);
                st->exclusion_error[0] = '\0';
            }
        }
#endif
        if (cui_button(ui, wada_tr("Add"), .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_PLUS,
                       .disabled = !st->exclusion_draft[0] || st->running)) {
            add_exclusion_from_draft(st, app);
        }
        cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
    }

    if (st->exclusion_error[0]) {
        cui_text(ui, st->exclusion_error, .size = 12, .color = CUI_COLOR_DANGER);
    }

    if (st->exclusion_count == 0) {
        cui_empty_state(ui, wada_tr("No exclusions configured"),
                        .description = wada_tr("Add a file or folder path above to keep Junk Cleaner from touching it."));
    } else {
        wc_scroll_list(ui, "exclusions-list", 6) {
            for (int i = 0; i < st->exclusion_count; i++) {
                const wc_exclusion_item *item = &st->exclusion_items[i];
                cui_id_scope(ui, item->id) {
                    cui_card(ui, .width = CUI_FILL, .pad = 10) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                            cui_text(ui, item->path, .font = CUI_FONT_MONO, .size = 12,
                                     .nowrap = true);
                            if (cui_button(ui, wada_tr("Remove"), .variant = CUI_BUTTON_GHOST,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH_2,
                                           .disabled = st->running)) {
                                wada_err err;
                                wada_err_clear(&err);
                                if (wc_exclusions_remove(&st->exclusions, item->id, &err) ==
                                    WADA_OK) {
                                    wc_exclusions_reload(st);
                                    wada_app_set_status(app, wada_tr("Exclusion removed"));
                                } else {
                                    wada_ui_status_err(app, st->status, sizeof st->status, &err);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

static void draw_cookies_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    char count_text[64];
    wada_trf(count_text, sizeof count_text, "%d allowed domains", st->cookie_count);

    bool running = st->cookie_job_running;
    bool confirming = wada_ui_confirm_active(&st->cookie_confirm, 0);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
        if (cui_input(ui, "cookie-draft", st->cookie_draft, sizeof st->cookie_draft,
                      .placeholder = wada_tr("Domain to keep signed in (e.g. google.com)"),
                      .width = CUI_FILL, .disabled = running)) {
            st->cookie_error[0] = '\0';
        }
        if (cui_button(ui, wada_tr("Add"), .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_PLUS,
                       .disabled = !st->cookie_draft[0] || running)) {
            add_cookie_from_draft(st, app);
        }
    }

    if (st->cookie_error[0]) {
        cui_text(ui, st->cookie_error, .size = 12, .color = CUI_COLOR_DANGER);
    }

    cui_text(ui,
             wada_tr("Covers every Chrome, Edge and Brave profile, plus Firefox. Close a browser to include it."),
             .size = 12, .color = CUI_COLOR_TEXT_MUTED);

    if (st->cookie_count == 0) {
        cui_text(ui, wada_tr("No allowlisted domains: cleaning signs you out of every site."),
                 .size = 12, .color = CUI_COLOR_WARNING);
    }

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
            .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
            if (!running) {
                if (cui_switch(ui, wada_tr("Preview only"), &st->cookie_dry_run)) {
                    wada_ui_confirm_reset(&st->cookie_confirm);
                }
                const char *label = confirming
                                        ? "Confirm Cookie Clean"
                                        : (st->cookie_dry_run ? "Preview Cookie Cleanup"
                                                             : "Clean Browser Cookies");
                if (cui_button(ui, wada_tr(label),
                               .variant = confirming ? CUI_BUTTON_DANGER : CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_TRASH_2)) {
                    start_cookie_job(st, app);
                }
            } else {
                if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_X)) {
                    wc_cookie_job_cancel(&st->cookie_job);
                }
            }
        }
        cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
    }

    if (running) {
        char lbl[192];
        int total = st->cookie_snap.total_dbs;
        int done = st->cookie_snap.done_dbs;
        if (st->cookie_snap.current[0]) {
            wada_trf(lbl, sizeof lbl, "Checking %s (%d/%d)", st->cookie_snap.current, done, total);
        } else {
            snprintf(lbl, sizeof lbl, "%s", wada_tr("Finding cookie stores…"));
        }
        cui_progress(ui, total > 0 ? (float)done / (float)total : 0.0f, .width = CUI_FILL,
                     .label = lbl, .show_value = total > 0, .indeterminate = total == 0,
                     .tone = CUI_TONE_ACCENT);
    } else if (confirming) {
        cui_text(ui, wada_tr("Click Confirm Cookie Clean to continue."), .size = 12,
                 .color = CUI_COLOR_WARNING);
    }

    if (!running && st->cookie_snap.state == WC_JOB_DONE) {
        const wc_cookie_snapshot *snap = &st->cookie_snap;
        if (snap->total_dbs == 0) {
            cui_alert(ui, wada_tr("No browser cookie stores were found."),
                      .tone = CUI_TONE_NEUTRAL, .icon = CUI_ICON_INFO);
        } else if (snap->removed || snap->kept || snap->skipped_browsers || snap->errors) {
            char msg[224];
            if (snap->dry_run) {
                wada_trf(msg, sizeof msg,
                         "Preview: %llu cookies would be removed, %llu kept. Skipped: %d browser(s), errors: %d.",
                         (unsigned long long)snap->removed, (unsigned long long)snap->kept,
                         snap->skipped_browsers, snap->errors);
            } else {
                wada_trf(msg, sizeof msg,
                         "Removed %llu cookies, kept %llu. Skipped: %d browser(s), errors: %d.",
                         (unsigned long long)snap->removed, (unsigned long long)snap->kept,
                         snap->skipped_browsers, snap->errors);
            }
            cui_alert(ui, msg,
                      .tone = snap->errors ? CUI_TONE_DANGER
                                           : (snap->skipped_browsers ? CUI_TONE_WARNING
                                                                     : CUI_TONE_INFO),
                      .icon = snap->errors ? CUI_ICON_CIRCLE_ALERT : CUI_ICON_INFO);
        }
    }

    if (st->cookie_count == 0) {
        cui_empty_state(ui, wada_tr("No allowed domains yet"),
                        .description = wada_tr("Add domains like github.com or google.com to preserve logins during cookie cleanup."));
    } else {
        wc_scroll_list(ui, "cookies-list", 6) {
            for (int i = 0; i < st->cookie_count; i++) {
                const wc_cookie_allow_item *item = &st->cookie_items[i];
                cui_id_scope(ui, item->id) {
                    cui_card(ui, .width = CUI_FILL, .pad = 10) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                            cui_text(ui, item->domain, .font = CUI_FONT_MONO, .size = 12);
                            if (cui_button(ui, wada_tr("Remove"), .variant = CUI_BUTTON_GHOST,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH_2,
                                           .disabled = running)) {
                                wada_err err;
                                wada_err_clear(&err);
                                if (wc_cookies_remove(&st->cookies, item->id, &err) == WADA_OK) {
                                    wc_cookies_reload(st);
                                    wada_app_set_status(app, wada_tr("Domain removed"));
                                } else {
                                    wada_ui_status_err(app, st->status, sizeof st->status, &err);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void wc_view_exclusions_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);
    const char *tabs[2] = {wada_tr("Protected Paths"), wada_tr("Cookie Allowlist")};

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Exclusions"), .font = CUI_FONT_DISPLAY, .size = 24);
            cui_text(ui,
                     wada_tr("Protect specific files, folders, and trusted cookie domains from cleanup."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        cui_tabs(ui, "excl-tabs", tabs, 2, &st->excl_tab);

        if (st->excl_tab == 0) {
            draw_paths_tab(st, ui, app);
        } else {
            draw_cookies_tab(st, ui, app);
        }
    }
}
