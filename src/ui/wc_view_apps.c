#include "wc_product.h"

#include <cui.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"
#include "wc_safety.h"

static const wc_app_item *g_sort_items = NULL;

static int cmp_idx_by_size_desc(const void *pa, const void *pb) {
    int ia = *(const int *)pa;
    int ib = *(const int *)pb;
    uint64_t sa = g_sort_items[ia].estimated_size_bytes;
    uint64_t sb = g_sort_items[ib].estimated_size_bytes;
    if (sb > sa) return 1;
    if (sb < sa) return -1;
    return ia - ib;
}

static bool app_matches_query(const wc_app_item *item, const char *query) {
    if (!query || !query[0]) return true;
    size_t nlen = strlen(query);
    const char *fields[2] = {item->name, item->publisher};
    for (int f = 0; f < 2; f++) {
        for (const char *p = fields[f]; *p; p++) {
            size_t i = 0;
            while (i < nlen && p[i] &&
                   tolower((unsigned char)p[i]) == tolower((unsigned char)query[i])) {
                i++;
            }
            if (i == nlen) return true;
        }
    }
    return false;
}

static void refresh_app_filter(wc_cleaner_state *st) {
    if (strcmp(st->apps_search, st->apps_last_query) == 0 &&
        st->apps_sort == st->apps_last_sort &&
        st->apps_version == st->apps_last_version) {
        return;
    }

    snprintf(st->apps_last_query, sizeof st->apps_last_query, "%s", st->apps_search);
    st->apps_last_sort = st->apps_sort;
    st->apps_last_version = st->apps_version;

    st->filtered_app_count = 0;
    for (int i = 0; i < st->app_count && st->filtered_app_count < WC_MAX_APPS; i++) {
        if (app_matches_query(&st->app_items[i], st->apps_search)) {
            st->filtered_apps[st->filtered_app_count++] = i;
        }
    }

    if (st->apps_sort == 1 && st->filtered_app_count > 1) {
        g_sort_items = st->app_items;
        qsort(st->filtered_apps, (size_t)st->filtered_app_count, sizeof st->filtered_apps[0],
              cmp_idx_by_size_desc);
        g_sort_items = NULL;
    }
}

static void fill_app_list(void *items, int max_items, int *out_count) {
    wc_plat_list_apps((wc_app_item *)items, max_items, out_count);
}

void wc_apps_refresh(wc_cleaner_state *st) {
    if (!st || !st->app_items || st->apps_loading) return;
    if (wc_list_job_start(&st->app_job, st->app_items, sizeof *st->app_items, WC_MAX_APPS,
                          fill_app_list)) {
        st->apps_loading = true;
        st->apps_loaded = false;
        st->app_count = 0;
    }
}

void wc_view_apps_draw(wc_cleaner_state *st, wada_app *app) {
    if (!st->apps_loaded && !st->apps_loading) wc_apps_refresh(st);
    refresh_app_filter(st);

    cui_ctx *ui = wada_app_ui(app);
    wada_ui_confirm_tick(&st->app_confirm, ui, st->app_selected);

    char count_text[48];
    if (st->apps_loading) {
        snprintf(count_text, sizeof count_text, "%s", wada_tr("Loading…"));
    } else {
        wada_trf(count_text, sizeof count_text, "%d apps", st->filtered_app_count);
    }

    const char *sort_labels[2] = {wada_tr("Name"), wada_tr("Size")};

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Installed Apps"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, wada_tr("Browse installed software and launch official uninstallers."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
            if (cui_button(ui, wada_tr("Refresh"), .variant = CUI_BUTTON_OUTLINE,
                           .icon = CUI_ICON_REFRESH_CW, .disabled = st->apps_loading)) {
                wc_apps_refresh(st);
            }
        }

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 10) {
            cui_search(ui, "apps-search", st->apps_search, sizeof st->apps_search,
                       .placeholder = wada_tr("Search apps or publishers…"), .width = CUI_FILL);
            cui_segmented(ui, "apps-sort", sort_labels, 2, &st->apps_sort, .size = CUI_SIZE_SM);
            cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }

        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 16) {
            /* Left: virtualized app list */
            cui_column(ui, .width = CUI_GROW(1.4f), .height = CUI_FILL) {
                if (st->apps_loading) {
                    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                               .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                        cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_PX(260),
                                     .label = wada_tr("Loading installed apps…"),
                                     .tone = CUI_TONE_ACCENT);
                    }
                } else if (st->filtered_app_count == 0) {
                    cui_empty_state(ui, wada_tr("No matching applications"),
                                    .description = wada_tr("Try clearing your search filter or refreshing."));
                } else {
                    wc_scroll_list(ui, "apps-list", 0) {
                        for (int i = 0; i < st->filtered_app_count; i++) {
                            int idx = st->filtered_apps[i];
                            const wc_app_item *it = &st->app_items[idx];

                            char size_str[32];
                            if (it->estimated_size_bytes > 0) {
                                wc_format_bytes(it->estimated_size_bytes, size_str, sizeof size_str);
                            } else {
                                snprintf(size_str, sizeof size_str, "—");
                            }

                            char sub[224];
                            if (it->publisher[0] && it->version[0]) {
                                snprintf(sub, sizeof sub, "%s · %s", it->publisher, it->version);
                            } else if (it->publisher[0]) {
                                snprintf(sub, sizeof sub, "%s", it->publisher);
                            } else if (it->version[0]) {
                                snprintf(sub, sizeof sub, "%s", it->version);
                            } else {
                                snprintf(sub, sizeof sub, "%s",
                                         wada_tr(it->is_user_install ? "Current User" : "System"));
                            }

                            char row_id[32];
                            snprintf(row_id, sizeof row_id, "app-%d", idx);
                            cui_id_scope(ui, row_id) {
                                if (cui_list_item(ui, it->name, .subtitle = sub, .trailing = size_str,
                                                  .selected = (idx == st->app_selected))) {
                                    st->app_selected = idx;
                                    wada_ui_confirm_reset(&st->app_confirm);
                                    if (!st->leftover_running && st->leftover_snap) {
                                        st->leftover_snap->state = WC_JOB_IDLE;
                                        st->leftover_snap->count = 0;
                                        st->leftover_snap->output_summary[0] = '\0';
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* Right: selected app details */
            cui_card(ui, .width = CUI_PX(360), .height = CUI_FILL, .pad = 16, .gap = 8) {
                if (st->apps_loading) {
                    cui_text(ui, wada_tr("Application Details"), .font = CUI_FONT_MEDIUM,
                             .size = 15);
                    cui_text(ui, wada_tr("Loading installed apps…"), .size = 12,
                             .color = CUI_COLOR_TEXT_MUTED);
                } else if (st->app_selected < 0 || st->app_selected >= st->app_count) {
                    cui_text(ui, wada_tr("Application Details"), .font = CUI_FONT_MEDIUM, .size = 15);
                    cui_text(ui, wada_tr("Select an application from the list to view details."),
                             .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                } else {
                    const wc_app_item *sel = &st->app_items[st->app_selected];
                    bool confirming = wada_ui_confirm_active(&st->app_confirm, st->app_selected);

                    char size_str[32];
                    if (sel->estimated_size_bytes > 0) {
                        wc_format_bytes(sel->estimated_size_bytes, size_str, sizeof size_str);
                    } else {
                        snprintf(size_str, sizeof size_str, "—");
                    }

                    cui_text(ui, sel->name, .font = CUI_FONT_MEDIUM, .size = 16);
                    cui_badge(ui, wada_tr(sel->is_user_install ? "Current User" : "System"),
                              .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);

                    cui_separator(ui);

                    cui_text(ui, wada_tr("Publisher"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, sel->publisher[0] ? sel->publisher : "—", .size = 13);

                    cui_text(ui, wada_tr("Version"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, sel->version[0] ? sel->version : "—", .size = 13);

                    cui_text(ui, wada_tr("Install Date"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, sel->install_date[0] ? sel->install_date : "—", .size = 13);

                    cui_text(ui, wada_tr("Estimated Size"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, size_str, .size = 13, .tabular = true);

                    cui_separator(ui);

                    /* Leftover scanner section */
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                            .justify = CUI_JUSTIFY_BETWEEN, .gap = 6) {
                        cui_text(ui, wada_tr("Residual Leftovers"), .font = CUI_FONT_MEDIUM,
                                 .size = 13);
                        if (cui_button(ui, wada_tr("Scan Leftovers"),
                                       .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM,
                                       .icon = CUI_ICON_SEARCH, .disabled = st->leftover_running)) {
                            wc_leftover_worker_start_scan(&st->leftover_worker, sel);
                            wada_app_set_status(app, wada_tr("Scanning for leftover files…"));
                        }
                    }

                    bool match_app = st->leftover_snap &&
                                     strcmp(st->leftover_snap->app_name, sel->name) == 0;
                    if (st->leftover_running) {
                        cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL,
                                     .label = wada_tr("Scanning AppData & ProgramData…"),
                                     .tone = CUI_TONE_ACCENT);
                    } else if (match_app && st->leftover_snap->state == WC_JOB_DONE) {
                        if (st->leftover_snap->count == 0) {
                            cui_text(ui, wada_tr("No leftovers found."), .size = 12,
                                     .color = CUI_COLOR_TEXT_MUTED);
                        } else {
                            const char *qpaths[WC_MAX_LEFTOVERS];
                            int qcnt = 0;
                            wc_scroll_list(ui, "app-leftovers", 4) {
                                for (int li = 0; li < st->leftover_snap->count; li++) {
                                    wc_leftover_item *lf = &st->leftover_snap->items[li];
                                    if (lf->selected) qpaths[qcnt++] = lf->path;
                                    char lsz[32];
                                    wc_format_bytes(lf->bytes, lsz, sizeof lsz);
                                    cui_id_scope(ui, lf->path) {
                                        cui_card(ui, .width = CUI_FILL, .pad = 6, .gap = 2,
                                                 .muted = true) {
                                            cui_row(ui, .width = CUI_FILL,
                                                    .align = CUI_ALIGN_CENTER,
                                                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 4) {
                                                cui_checkbox(ui,
                                                             wada_tr(lf->confidence >= 2 ? "High"
                                                                                         : "Medium"),
                                                             &lf->selected);
                                                cui_text(ui, lsz, .size = 12,
                                                         .color = CUI_COLOR_TEXT_SECONDARY,
                                                         .tabular = true);
                                            }
                                            cui_text(ui, lf->path, .font = CUI_FONT_MONO,
                                                     .size = 12, .color = CUI_COLOR_TEXT_MUTED,
                                                     .nowrap = true);
                                        }
                                    }
                                }
                            }
                            if (cui_button(ui, wada_tr("Move to Quarantine"),
                                           .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM,
                                           .icon = CUI_ICON_ARCHIVE, .width = CUI_FILL,
                                           .disabled = qcnt == 0 || !st->quarantine.open)) {
                                wc_leftover_worker_start_quarantine(&st->leftover_worker, qpaths,
                                                                    qcnt);
                            }
                        }
                    } else {
                        cui_box(ui, .height = CUI_FILL) {}
                    }

                    if (confirming) {
                        cui_text(ui, wada_tr("Click Confirm Uninstall to launch the uninstaller."),
                                 .size = 12, .color = CUI_COLOR_WARNING);
                    }
                    if (cui_button(ui, wada_tr(confirming ? "Confirm Uninstall" : "Uninstall"),
                                   .variant = confirming ? CUI_BUTTON_DANGER : CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_TRASH_2, .width = CUI_FILL)) {
                        if (!wada_ui_confirm_step(&st->app_confirm, st->app_selected, 300)) {
                            wada_app_set_status(app,
                                                wada_tr("Click Confirm Uninstall to launch the uninstaller."));
                        } else if (wc_plat_launch_uninstaller(sel->uninstall_cmd)) {
                            wada_app_set_status(app, wada_tr("Uninstaller launched"));
                        } else {
                            wada_app_set_status(app, wada_tr("Failed to launch uninstaller"));
                        }
                    }
                }
            }
        }
    }
}
