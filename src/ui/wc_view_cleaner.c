#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_safety.h"

static void draw_target_row(wc_cleaner_state *st, cui_ctx *ui, int idx) {
    const wc_target_def *def = &st->defs[idx];
    const wc_target_result *res = &st->snap.results[idx];

    char size_str[32];
    char meta[128];
    if (res->cleaned) {
        wc_format_bytes(res->bytes_cleaned, size_str, sizeof size_str);
        wada_trf(meta, sizeof meta,
                 st->snap.dry_run ? "Would clean %s (%llu files)" : "Cleaned %s (%llu files)",
                 size_str, (unsigned long long)res->files_cleaned);
    } else if (res->analyzed) {
        wc_format_bytes(res->bytes_found, size_str, sizeof size_str);
        wada_trf(meta, sizeof meta, "%s (%llu files)", size_str,
                 (unsigned long long)res->files_found);
    } else {
        snprintf(meta, sizeof meta, "%s", wada_tr("Not scanned"));
    }

    cui_id_scope(ui, def->id) {
        cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 3) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_checkbox(ui, wada_tr(def->label), &st->selected[idx],
                             .disabled = st->running);

                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    if (def->needs_admin && !st->is_admin) {
                        cui_badge(ui, wada_tr("Admin"), .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                    }
                    if (res->browser_running) {
                        cui_badge(ui, wada_tr("Running"), .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                    }
                    cui_text(ui, meta, .size = 12,
                             .color = res->analyzed || res->cleaned ? CUI_COLOR_TEXT_SECONDARY
                                                                    : CUI_COLOR_TEXT_MUTED,
                             .tabular = true);
                }
            }

            if (def->kind != WC_TARGET_RECYCLE_BIN && st->resolved_paths[idx][0]) {
                cui_text(ui, st->resolved_paths[idx], .size = 11, .color = CUI_COLOR_TEXT_MUTED,
                         .nowrap = true);
            }
            if (res->note[0]) {
                cui_color note_color = (res->browser_running || res->admin_blocked)
                                           ? CUI_COLOR_WARNING
                                           : CUI_COLOR_TEXT_MUTED;
                cui_text(ui, wada_tr(res->note), .size = 11, .color = note_color);
            }
        }
    }
}

static void draw_category_group(wc_cleaner_state *st, cui_ctx *ui, wc_category cat,
                                const char *heading, bool show_select_actions) {
    if (show_select_actions) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
            cui_text(ui, wada_tr(heading), .font = CUI_FONT_MEDIUM, .size = 13,
                     .color = CUI_COLOR_TEXT_SECONDARY);
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                if (cui_button(ui, wada_tr("Select All"), .variant = CUI_BUTTON_GHOST,
                               .size = CUI_SIZE_SM, .disabled = st->running)) {
                    wc_cleaner_select_all(st, true);
                }
                if (cui_button(ui, wada_tr("Clear"), .variant = CUI_BUTTON_GHOST,
                               .size = CUI_SIZE_SM, .disabled = st->running)) {
                    wc_cleaner_select_all(st, false);
                }
            }
        }
    } else {
        cui_text(ui, wada_tr(heading), .font = CUI_FONT_MEDIUM, .size = 13,
                 .color = CUI_COLOR_TEXT_SECONDARY);
    }
    for (int i = 0; i < st->target_count; i++) {
        if (st->defs[i].category == cat) {
            draw_target_row(st, ui, i);
        }
    }
}

void wc_view_cleaner_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);

    int sel_count = 0;
    for (int i = 0; i < st->target_count; i++) {
        if (st->selected[i]) sel_count++;
    }
    bool confirming = wada_ui_confirm_active(&st->confirm, 0);

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        /* Header */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Junk Cleaner"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui,
                         wada_tr("Analyze first, preview exact sizes, and safely clean system & browser junk."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
            cui_checkbox(ui, wada_tr("Dry run (preview only)"), &st->dry_run,
                         .disabled = st->running);
        }

        /* Action bar */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
            if (cui_button(ui, wada_tr("Analyze"), .variant = CUI_BUTTON_PRIMARY,
                           .icon = CUI_ICON_REFRESH_CW,
                           .disabled = st->running || sel_count == 0,
                           .loading = st->running && st->snap.kind == WC_JOB_ANALYZE)) {
                wc_cleaner_start_analyze(st);
            }

            bool clean_disabled = st->running || sel_count == 0 ||
                                  (!st->dry_run && !st->has_analyzed);
            if (cui_button(ui, wada_tr(confirming ? "Confirm Clean" : "Clean Selected"),
                           .variant = confirming ? CUI_BUTTON_DANGER : CUI_BUTTON_OUTLINE,
                           .icon = CUI_ICON_TRASH_2,
                           .disabled = clean_disabled,
                           .loading = st->running && st->snap.kind == WC_JOB_CLEAN)) {
                wc_cleaner_start_clean(st);
            }

            if (st->running) {
                if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_X)) {
                    wc_cleaner_cancel_job(st);
                }
            } else if (!st->has_analyzed && !st->dry_run) {
                cui_text(ui, wada_tr("Run Analyze before cleaning."), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
            } else if (confirming) {
                cui_text(ui, wada_tr("Click Clean Selected again to confirm."), .size = 12,
                         .color = CUI_COLOR_WARNING);
            }
        }

        /* Progress bar while worker runs */
        if (st->running) {
            float frac = st->snap.total_selected > 0
                             ? (float)st->snap.completed_targets / (float)st->snap.total_selected
                             : 0.0f;
            char prog_label[160];
            const char *fmt = "Scanning: %s";
            if (st->snap.kind == WC_JOB_CLEAN) {
                fmt = st->snap.dry_run ? "Previewing: %s" : "Cleaning: %s";
            }
            const char *cur = st->snap.current_label[0] ? wada_tr(st->snap.current_label) : "";
            wada_trf(prog_label, sizeof prog_label, fmt, cur);
            cui_progress(ui, frac, .width = CUI_FILL, .label = prog_label, .show_value = true,
                         .tone = CUI_TONE_ACCENT);
        }

        /* Two-column main content */
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 16) {
            /* Left: Cleaner targets */
            cui_column(ui, .width = CUI_GROW(1.45f), .height = CUI_FILL, .gap = 6) {
                wc_scroll_list(ui, "cleaner-targets", 6) {
                    draw_category_group(st, ui, WC_CAT_SYSTEM, "System", true);
                    cui_separator(ui);
                    draw_category_group(st, ui, WC_CAT_BROWSER, "Browsers", false);
                }
            }

            /* Right: Summary + Recent History */
            cui_column(ui, .width = CUI_GROW(1.0f), .height = CUI_FILL, .gap = 12) {
                cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 10) {
                    cui_text(ui, wada_tr("Summary"), .font = CUI_FONT_MEDIUM, .size = 15);

                    char reclaim_str[32], freed_str[32], files_str[32], skip_str[32];
                    wc_format_bytes(st->snap.total_bytes_found, reclaim_str, sizeof reclaim_str);
                    wc_format_bytes(st->snap.total_bytes_cleaned, freed_str, sizeof freed_str);
                    snprintf(files_str, sizeof files_str, "%llu",
                             (unsigned long long)st->snap.total_files_found);
                    snprintf(skip_str, sizeof skip_str, "%llu",
                             (unsigned long long)st->snap.total_skipped);

                    cui_row(ui, .width = CUI_FILL, .gap = 12) {
                        cui_metric(ui, wada_tr("Reclaimable"), reclaim_str, .width = CUI_GROW(1.0f));
                        cui_metric(ui, wada_tr("Files Found"), files_str, .width = CUI_GROW(1.0f));
                    }
                    cui_row(ui, .width = CUI_FILL, .gap = 12) {
                        cui_metric(ui, wada_tr("Last Freed"), freed_str, .width = CUI_GROW(1.0f));
                        cui_metric(ui, wada_tr("Skipped"), skip_str, .width = CUI_GROW(1.0f));
                    }

                    if (st->dry_run) {
                        cui_alert(ui, wada_tr("Dry-run preview mode is enabled. No files will be deleted."),
                                  .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
                    }

                    bool any_browser_running = false;
                    for (int i = 0; i < st->target_count; i++) {
                        if (st->snap.results[i].browser_running) {
                            any_browser_running = true;
                            break;
                        }
                    }
                    if (any_browser_running) {
                        cui_alert(ui,
                                  wada_tr("Close running browsers before cleaning to avoid skipped profiles."),
                                  .tone = CUI_TONE_WARNING, .icon = CUI_ICON_CIRCLE_ALERT);
                    }
                }

                cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 8) {
                    cui_text(ui, wada_tr("Recent History"), .font = CUI_FONT_MEDIUM, .size = 15);
                    if (st->history_count == 0) {
                        cui_text(ui, wada_tr("No clean runs recorded yet."), .size = 12,
                                 .color = CUI_COLOR_TEXT_MUTED);
                    } else {
                        wc_scroll_list(ui, "cleaner-history", 8) {
                            for (int i = 0; i < st->history_count; i++) {
                                const wc_history_entry *e = &st->history_entries[i];
                                char when[32];
                                char size_str[32];
                                char detail[96];
                                wada_ui_format_when(e->unix_sec, when, sizeof when);
                                wc_format_bytes(e->bytes_cleaned, size_str, sizeof size_str);
                                wada_trf(detail, sizeof detail, "%s | %llu files | %llu skipped",
                                         size_str, (unsigned long long)e->files_cleaned,
                                         (unsigned long long)e->skipped_count);

                                cui_id_scope(ui, e->id) {
                                    cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 3, .muted = true) {
                                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 6) {
                                            cui_text(ui, when, .size = 11,
                                                     .color = CUI_COLOR_TEXT_MUTED);
                                            cui_badge(ui, wada_tr(e->dry_run ? "Dry Run" : "Clean"),
                                                      .tone = e->dry_run ? CUI_TONE_INFO
                                                                         : CUI_TONE_SUCCESS,
                                                      .size = CUI_SIZE_SM);
                                        }
                                        cui_text(ui, detail, .size = 12, .tabular = true);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
