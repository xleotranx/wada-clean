#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"

static void add_exclusion_from_draft(wc_cleaner_state *st, wada_app *app) {
    if (!st->exclusion_draft[0] || !st->exclusions.open) return;

    wada_err err;
    wada_err_clear(&err);
    wada_status status = wc_exclusions_add(&st->exclusions, st->exclusion_draft, &err);
    if (status == WADA_OK) {
        st->exclusion_draft[0] = '\0';
        wc_exclusions_reload(st);
        wada_app_set_status(app, wada_tr("Exclusion added"));
    } else if (status == WADA_ERR_EXISTS) {
        wada_app_set_status(app, wada_tr("Path is already excluded"));
    } else {
        wada_app_set_status(app, wada_tr("Enter a valid Windows path (e.g. C:\\Folder\\File)"));
    }
}

static void add_cookie_from_draft(wc_cleaner_state *st, wada_app *app) {
    if (!st->cookie_draft[0] || !st->cookies.open) return;

    wada_err err;
    wada_err_clear(&err);
    wada_status status = wc_cookies_add(&st->cookies, st->cookie_draft, &err);
    if (status == WADA_OK) {
        st->cookie_draft[0] = '\0';
        wc_cookies_reload(st);
        wada_app_set_status(app, wada_tr("Domain added to allowlist"));
    } else if (status == WADA_ERR_EXISTS) {
        wada_app_set_status(app, wada_tr("Domain is already allowlisted"));
    } else {
        wada_app_set_status(app, wada_tr("Enter a valid domain (e.g. google.com)"));
    }
}

static void run_cookie_prune(wc_cleaner_state *st, wada_app *app) {
    char base_dir[WC_PATH_CAP];
#ifdef _WIN32
    if (!wc_plat_get_env("LOCALAPPDATA", base_dir, sizeof base_dir) || !base_dir[0]) return;
#else
    if (!wc_plat_get_env("HOME", base_dir, sizeof base_dir) || !base_dir[0]) return;
#endif

    const char *allow_ptrs[WC_MAX_COOKIE_ALLOW];
    for (int i = 0; i < st->cookie_count; i++) {
        allow_ptrs[i] = st->cookie_items[i].domain;
    }

    static const struct {
        const char *proc;
        const char *rel1;
        const char *rel2;
    } browsers[] = {
#ifdef _WIN32
        {"chrome.exe", "Google\\Chrome\\User Data\\Default\\Network\\Cookies",
         "Google\\Chrome\\User Data\\Default\\Cookies"},
        {"msedge.exe", "Microsoft\\Edge\\User Data\\Default\\Network\\Cookies",
         "Microsoft\\Edge\\User Data\\Default\\Cookies"},
        {"brave.exe", "BraveSoftware\\Brave-Browser\\User Data\\Default\\Network\\Cookies",
         "BraveSoftware\\Brave-Browser\\User Data\\Default\\Cookies"},
#else
        {"Google Chrome", "Library/Application Support/Google/Chrome/Default/Cookies",
         "Library/Application Support/Google/Chrome/Default/Network/Cookies"},
        {"Microsoft Edge", "Library/Application Support/Microsoft Edge/Default/Cookies",
         "Library/Application Support/Microsoft Edge/Default/Network/Cookies"},
        {"Brave Browser",
         "Library/Application Support/BraveSoftware/Brave-Browser/Default/Cookies",
         "Library/Application Support/BraveSoftware/Brave-Browser/Default/Network/Cookies"},
#endif
    };

    uint64_t total_removed = 0;
    uint64_t total_kept = 0;
    int skipped_browsers = 0;

    for (size_t i = 0; i < sizeof browsers / sizeof browsers[0]; i++) {
        if (!st->dry_run && wc_plat_process_running(browsers[i].proc)) {
            skipped_browsers++;
            continue;
        }
        char db_path[WC_PATH_CAP];
        uint64_t rem = 0, kept = 0;
        bool ok = false;
        int n1 = snprintf(db_path, sizeof db_path, "%s" WADA_PATH_SEP "%s", base_dir, browsers[i].rel1);
        if (n1 > 0 && (size_t)n1 < sizeof db_path) {
            ok = wc_plat_prune_cookie_db(db_path, false, allow_ptrs, st->cookie_count,
                                         st->dry_run, &rem, &kept);
        }
        if (!ok) {
            int n2 = snprintf(db_path, sizeof db_path, "%s" WADA_PATH_SEP "%s", base_dir,
                              browsers[i].rel2);
            if (n2 > 0 && (size_t)n2 < sizeof db_path) {
                ok = wc_plat_prune_cookie_db(db_path, false, allow_ptrs, st->cookie_count,
                                             st->dry_run, &rem, &kept);
            }
        }
        if (ok) {
            total_removed += rem;
            total_kept += kept;
        }
    }

    st->cookie_has_report = true;
    st->cookie_last_removed = total_removed;
    st->cookie_last_kept = total_kept;
    st->cookie_last_skipped_browsers = skipped_browsers;
    wada_app_set_status(app, wada_tr(st->dry_run ? "Cookie preview complete"
                                                 : "Cookie cleanup complete"));
}

static void draw_paths_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    char count_text[64];
    wada_trf(count_text, sizeof count_text, "%d protected paths", st->exclusion_count);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
        cui_input(ui, "excl-draft", st->exclusion_draft, sizeof st->exclusion_draft,
                  .placeholder = wada_tr("Folder or file path to exclude (e.g. C:\\Users\\...)"),
                  .width = CUI_FILL);
        if (cui_button(ui, wada_tr("Add"), .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_PLUS,
                       .disabled = !st->exclusion_draft[0] || st->running)) {
            add_exclusion_from_draft(st, app);
        }
        cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
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

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
        cui_input(ui, "cookie-draft", st->cookie_draft, sizeof st->cookie_draft,
                  .placeholder = wada_tr("Domain to keep signed in (e.g. google.com)"),
                  .width = CUI_FILL);
        if (cui_button(ui, wada_tr("Add"), .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_PLUS,
                       .disabled = !st->cookie_draft[0])) {
            add_cookie_from_draft(st, app);
        }
    }

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
            .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
        if (cui_button(ui,
                       wada_tr(st->dry_run ? "Preview Cookie Cleanup" : "Clean Browser Cookies"),
                       .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_TRASH_2)) {
            run_cookie_prune(st, app);
        }
        cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
    }

    if (st->cookie_has_report) {
        char msg[160];
        wada_trf(msg, sizeof msg,
                 st->dry_run ? "Preview: %llu cookies would be removed, %llu kept (%d browsers skipped)."
                             : "Cleaned %llu cookies, kept %llu allowlisted (%d browsers skipped).",
                 (unsigned long long)st->cookie_last_removed,
                 (unsigned long long)st->cookie_last_kept, st->cookie_last_skipped_browsers);
        cui_alert(ui, msg,
                  .tone = st->cookie_last_skipped_browsers > 0 ? CUI_TONE_WARNING : CUI_TONE_INFO,
                  .icon = CUI_ICON_INFO);
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
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH_2)) {
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
