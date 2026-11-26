#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_safety.h"

/* How many sampled files the Review panel lists. The sample itself is capped by
 * the worker; this is a reading limit, not a scan limit. */
#define HOME_SAMPLE_ROWS 10

static void group_totals(const wc_cleaner_state *st, int group, uint64_t *bytes, uint64_t *files,
                         int *targets, int *browsers_running, int *admin_skipped) {
    *bytes = 0;
    *files = 0;
    *targets = 0;
    *browsers_running = 0;
    *admin_skipped = 0;
    for (int i = 0; i < st->target_count; i++) {
        if (wc_target_group(&st->defs[i]) != group) continue;
        (*targets)++;
        const wc_target_result *r = &st->snap.results[i];
        if (r->analyzed) {
            *bytes += r->bytes_found;
            *files += r->files_found;
        }
        if (r->browser_running) (*browsers_running)++;
        if (r->admin_blocked) (*admin_skipped)++;
    }
    if (group == 2) {
        *bytes = st->recycle_bytes;
        *files = st->recycle_files;
    }
}

static bool id_selected(const wc_cleaner_state *st, const char *id) {
    for (int i = 0; i < st->target_count; i++) {
        if (strcmp(st->defs[i].id, id) == 0) return st->selected[i];
    }
    return false;
}

static void group_meta(const wc_cleaner_state *st, int group, uint64_t files, int browsers,
                       char *out, size_t cap) {
    if (group == 2) {
        if (st->recycle_known && files > 0) {
            wada_trf(out, cap, "%llu items · emptied permanently", (unsigned long long)files);
        } else if (st->recycle_known) {
            snprintf(out, cap, "%s", wada_tr("Empty"));
        } else {
            snprintf(out, cap, "%s", wada_tr("Contents cannot be listed"));
        }
        return;
    }

    const char *base = (group == 0) ? wada_tr("Temporary files, caches, Windows Update")
                                     : wada_tr("Chrome, Edge, Firefox, Brave");

    char extra[64];
    extra[0] = '\0';
    if (browsers > 0) snprintf(extra, sizeof extra, "%s", wada_tr("browser open — skipped"));

    if (st->has_analyzed) {
        if (extra[0]) {
            wada_trf(out, cap, "%llu files · %s · %s", (unsigned long long)files, base, extra);
        } else {
            wada_trf(out, cap, "%llu files · %s", (unsigned long long)files, base);
        }
    } else if (extra[0]) {
        wada_trf(out, cap, "%s · %s", base, extra);
    } else {
        snprintf(out, cap, "%s", base);
    }
}

static void draw_group_row(wc_cleaner_state *st, wada_app *app, cui_ctx *ui, int group,
                           const char *label, const char *icon) {
    uint64_t bytes = 0, files = 0;
    int targets = 0, browsers = 0, admin = 0;
    group_totals(st, group, &bytes, &files, &targets, &browsers, &admin);
    if (targets == 0) return;

    char size_str[32];
    bool known = st->has_analyzed || (group == 2 && st->recycle_known);
    if (known) {
        wc_format_bytes(bytes, size_str, sizeof size_str);
    } else {
        snprintf(size_str, sizeof size_str, "%s", wada_tr("—"));
    }

    char meta[224];
    group_meta(st, group, files, browsers, meta, sizeof meta);

    char switch_id[24];
    snprintf(switch_id, sizeof switch_id, "##group%d", group);

    cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8, .selected = st->group_on[group]) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 10) {
                cui_icon(ui, icon, 16.0f, CUI_COLOR_TEXT_SECONDARY);
                cui_column(ui, .gap = 2) {
                    cui_text(ui, label, .font = CUI_FONT_MEDIUM, .size = 15);
                    cui_text(ui, meta, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }
            }
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 12) {
                cui_text(ui, size_str, .font = CUI_FONT_DISPLAY, .size = 18, .tabular = true);
                if (cui_switch(ui, switch_id, &st->group_on[group], .disabled = st->running)) {
                    /* The count is the consequence; re-read it the moment the
                     * Recycle Bin is opted into. */
                    if (group == 2 && st->group_on[2]) wc_home_refresh_recycle(st);
                    wc_home_apply_group(st, group);
                }
            }
        }
        if (group == 0 && !st->is_admin && admin > 0) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
                cui_text(ui, wada_tr("Some system locations need administrator access."), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
                if (cui_button(ui, wada_tr("Restart as administrator"),
                               .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                               .icon = CUI_ICON_SHIELD)) {
                    wc_relaunch_admin(st, app);
                }
            }
        }
        if (group == 2 && st->group_on[2]) {
            char warn[192];
            if (st->recycle_known && files > 0) {
                wada_trf(warn, sizeof warn,
                         "%llu items (%s) will be deleted permanently. This cannot be undone.",
                         (unsigned long long)files, size_str);
            } else {
                snprintf(warn, sizeof warn, "%s",
                         wada_tr("The Recycle Bin will be emptied permanently. This cannot be undone."));
            }
            cui_alert(ui, warn, .tone = CUI_TONE_DANGER, .icon = CUI_ICON_CIRCLE_ALERT);
        }
    }
}

static void draw_review(wc_cleaner_state *st, cui_ctx *ui) {
    int order[WC_MAX_PREVIEW];
    int n = 0;
    for (int p = 0; p < st->preview_ui_count; p++) {
        if (!id_selected(st, st->preview_items[p].target_id)) continue;
        order[n++] = p;
    }
    int shown = n < HOME_SAMPLE_ROWS ? n : HOME_SAMPLE_ROWS;
    for (int a = 0; a < shown; a++) {
        int best = a;
        for (int b = a + 1; b < n; b++) {
            if (st->preview_items[order[b]].bytes > st->preview_items[order[best]].bytes) best = b;
        }
        int t = order[a];
        order[a] = order[best];
        order[best] = t;
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_START, .gap = 10) {
            if (cui_button(ui, wada_tr("Back"), .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_CHEVRON_LEFT)) {
                st->preview_open = false;
            }
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Review"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, wada_tr("Totals are what was found. The file list is a sample."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            for (int g = 0; g < 3; g++) {
                uint64_t bytes = 0, files = 0;
                int targets = 0, browsers = 0, admin = 0;
                group_totals(st, g, &bytes, &files, &targets, &browsers, &admin);
                if (targets == 0) continue;
                if (g == 2 && !st->recycle_known) continue;

                char size_str[32];
                wc_format_bytes(bytes, size_str, sizeof size_str);
                const char *label =
                    (g == 0) ? wada_tr("System junk")
                             : (g == 1) ? wada_tr("Browser junk") : wada_tr("Recycle Bin");
                char detail[96];
                if (g == 2) {
                    wada_trf(detail, sizeof detail, "%llu items, emptied entirely",
                             (unsigned long long)files);
                } else if (g == 1 && browsers > 0) {
                    wada_trf(detail, sizeof detail, "%llu files · browser open, skipped",
                             (unsigned long long)files);
                } else {
                    wada_trf(detail, sizeof detail, "%llu files", (unsigned long long)files);
                }
                cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_BETWEEN,
                        .align = CUI_ALIGN_CENTER) {
                    cui_text(ui, label, .size = 13);
                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 10) {
                        cui_text(ui, detail, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        cui_text(ui, size_str, .size = 13, .tabular = true);
                    }
                }
            }
            if (st->group_on[2]) {
                cui_alert(ui,
                          wada_tr("The Recycle Bin cannot be previewed or restored after cleaning."),
                          .tone = CUI_TONE_WARNING, .icon = CUI_ICON_INFO);
            }
        }

        if (n == 0) {
            cui_empty_state(ui, wada_tr("No files were sampled."),
                            .description = wada_tr("Scan again to sample the files that will be removed."));
        } else {
            cui_text(ui, wada_tr("Largest files in the sample"), .font = CUI_FONT_MEDIUM,
                     .size = 15);
            wc_scroll_list(ui, "home-review-files", 2) {
                for (int a = 0; a < shown; a++) {
                    const wc_preview_item *item = &st->preview_items[order[a]];
                    char size_str[32];
                    wc_format_bytes(item->bytes, size_str, sizeof size_str);
                    cui_id_scope(ui, item->path) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                            cui_text(ui, item->path, .font = CUI_FONT_MONO, .size = 12,
                                     .color = CUI_COLOR_TEXT_SECONDARY, .nowrap = true,
                                     .width = CUI_GROW(1.0f));
                            cui_text(ui, size_str, .size = 12, .color = CUI_COLOR_TEXT_MUTED,
                                     .tabular = true);
                        }
                    }
                    cui_separator(ui);
                }
            }
            if (n > shown) {
                char more[128];
                wada_trf(more, sizeof more, "%d more files were sampled and will be removed.", n - shown);
                cui_text(ui, more, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }
}

void wc_view_home_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);

    /* One scan, automatically. The user never presses "Analyze". */
    if (!st->scan_started && !st->running) wc_home_scan(st);

    if (st->preview_open) {
        draw_review(st, ui);
        return;
    }

    bool cleaning = st->running && st->snap.kind == WC_JOB_CLEAN;
    bool scanning = st->running && !cleaning;
    bool done = !st->running && st->snap.kind == WC_JOB_CLEAN && st->snap.state == WC_JOB_DONE;
    uint64_t sel_bytes = wc_home_selected_bytes(st);
    bool confirming = wada_ui_confirm_active(&st->confirm, 0);

    char amount[32];
    wc_format_bytes(sel_bytes, amount, sizeof amount);

    char freed[32];
    wc_format_bytes(st->snap.total_bytes_cleaned, freed, sizeof freed);

    const char *badge = "Not scanned yet";
    cui_tone tone = CUI_TONE_NEUTRAL;
    char headline[128];
    char sub[256];
    if (cleaning) {
        badge = st->snap.dry_run ? "Previewing…" : "Freeing space…";
        tone = CUI_TONE_INFO;
        snprintf(headline, sizeof headline, "%s",
                 wada_tr(st->snap.dry_run ? "Previewing…" : "Freeing space…"));
        snprintf(sub, sizeof sub, "%s",
                 wada_tr(st->snap.dry_run ? "Preview only. Nothing is deleted."
                                          : "Temporary files are deleted, not moved."));
    } else if (scanning) {
        badge = "Checking…";
        tone = CUI_TONE_INFO;
        snprintf(headline, sizeof headline, "%s", wada_tr("Checking your PC…"));
        snprintf(sub, sizeof sub, "%s", wada_tr("Only temporary files and caches are scanned."));
    } else if (done) {
        badge = st->snap.dry_run ? "Preview complete" : "Done";
        tone = st->snap.dry_run ? CUI_TONE_INFO : CUI_TONE_SUCCESS;
        if (st->snap.dry_run) {
            wada_trf(headline, sizeof headline, "Would free %s", freed);
            snprintf(sub, sizeof sub, "%s", wada_tr("Preview only. Nothing was deleted."));
        } else {
            wada_trf(headline, sizeof headline, "%s freed", freed);
            if (st->snap.total_skipped > 0) {
                wada_trf(sub, sizeof sub,
                         "Nothing personal was touched. %llu items were skipped because they were in use.",
                         (unsigned long long)st->snap.total_skipped);
            } else {
                snprintf(sub, sizeof sub, "%s",
                         wada_tr("Caches rebuild as needed. Your documents, photos and downloads were not touched."));
            }
        }
    } else if (!st->has_analyzed) {
        badge = "Not scanned yet";
        tone = CUI_TONE_NEUTRAL;
        snprintf(headline, sizeof headline, "%s", wada_tr("See how much space you can get back"));
        snprintf(sub, sizeof sub, "%s",
                 wada_tr("Wada Clean removes temporary files and caches only."));
    } else if (sel_bytes == 0) {
        badge = "All clean";
        tone = CUI_TONE_SUCCESS;
        snprintf(headline, sizeof headline, "%s", wada_tr("Nothing to clean"));
        snprintf(sub, sizeof sub, "%s", wada_tr("No reclaimable junk was found in the selected groups."));
    } else {
        badge = "Ready to free up";
        tone = CUI_TONE_WARNING;
        wada_trf(headline, sizeof headline, "%s can be freed", amount);
        snprintf(sub, sizeof sub, "%s", wada_tr("Only caches and temporary files. Nothing personal."));
    }

    char primary[64];
    if (st->running) {
        snprintf(primary, sizeof primary, "%s", wada_tr("Cancel"));
    } else if (done || !st->has_analyzed || sel_bytes == 0) {
        snprintf(primary, sizeof primary, "%s", wada_tr(st->has_analyzed ? "Scan again" : "Scan now"));
    } else if (confirming) {
        wada_trf(primary, sizeof primary, "Confirm free up %s", amount);
    } else {
        wada_trf(primary, sizeof primary, "Free up %s", amount);
    }

    cui_column(ui, .id = "home-scroll", .width = CUI_FILL, .height = CUI_FILL, .scroll_y = true,
               .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Free up space"), .font = CUI_FONT_DISPLAY, .size = 26);
            cui_text(ui, wada_tr("Reclaim disk space without touching your files."), .size = 12,
                     .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Hero */
        cui_card(ui, .width = CUI_FILL, .pad = 18, .gap = 12) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 16) {
                cui_column(ui, .width = CUI_GROW(1.0f), .gap = 6) {
                    cui_badge(ui, wada_tr(badge), .tone = tone, .dot = true);
                    cui_text(ui, headline, .font = CUI_FONT_DISPLAY, .size = 28, .tabular = true);
                    cui_text(ui, sub, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                    if (st->running) {
                        if (cui_button(ui, primary, .variant = CUI_BUTTON_OUTLINE,
                                       .icon = CUI_ICON_X)) {
                            wc_cleaner_cancel_job(st);
                        }
                    } else {
                        if (cui_button(ui, primary,
                                       .variant = (done || sel_bytes == 0 || !st->has_analyzed)
                                                      ? CUI_BUTTON_OUTLINE
                                                      : (confirming ? CUI_BUTTON_DANGER
                                                                    : CUI_BUTTON_PRIMARY),
                                       .icon = (done || sel_bytes == 0 || !st->has_analyzed)
                                                   ? CUI_ICON_REFRESH_CW
                                                   : CUI_ICON_TRASH_2)) {
                            if (done || sel_bytes == 0 || !st->has_analyzed) {
                                wc_home_scan(st);
                            } else {
                                wc_home_clean(st);
                            }
                        }
                        bool can_review = st->has_analyzed && !done && st->preview_ui_count > 0;
                        if (cui_button(ui, wada_tr("Review"), .variant = CUI_BUTTON_OUTLINE,
                                       .icon = CUI_ICON_EYE, .disabled = !can_review)) {
                            st->preview_open = true;
                        }
                    }
                }
            }

            if (st->running) {
                float frac = st->snap.total_selected > 0
                                 ? (float)st->snap.completed_targets /
                                       (float)st->snap.total_selected
                                 : 0.0f;
                const char *cur = st->snap.current_label[0] ? wada_tr(st->snap.current_label) : "";
                char lbl[160];
                wada_trf(lbl, sizeof lbl, cleaning ? "Cleaning: %s" : "Scanning: %s", cur);
                cui_progress(ui, frac, .width = CUI_FILL, .label = lbl, .show_value = true,
                             .tone = CUI_TONE_ACCENT);
            } else if (confirming) {
                char confirm_msg[256];
                if (st->group_on[2] && st->recycle_known && st->recycle_files > 0) {
                    wada_trf(confirm_msg, sizeof confirm_msg,
                             "Click again to free %s. Temporary files are deleted, and the Recycle Bin is emptied permanently.",
                             amount);
                } else {
                    wada_trf(confirm_msg, sizeof confirm_msg,
                             "Click again to free %s. Temporary files are deleted permanently.",
                             amount);
                }
                cui_alert(ui, confirm_msg, .tone = CUI_TONE_WARNING, .icon = CUI_ICON_CIRCLE_ALERT);
            }
        }

        /* Three decisions, never twenty-seven checkboxes. */
        draw_group_row(st, app, ui, 0, wada_tr("System junk"), CUI_ICON_CPU);
        draw_group_row(st, app, ui, 1, wada_tr("Browser junk"), CUI_ICON_GLOBE);
        draw_group_row(st, app, ui, 2, wada_tr("Recycle Bin"), CUI_ICON_TRASH_2);

        /* Footer */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 10) {
            const wc_history_entry *last = wc_last_clean_run(st);
            char last_line[160];
            if (last) {
                char sz[32], when[32];
                wc_format_bytes(last->bytes_cleaned, sz, sizeof sz);
                wada_ui_format_when(last->unix_sec, when, sizeof when);
                wada_trf(last_line, sizeof last_line, "Last clean: %s freed · %s", sz, when);
            } else {
                snprintf(last_line, sizeof last_line, "%s", wada_tr("No clean runs recorded yet."));
            }
            cui_text(ui, last_line, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            if (st->history_count > 0 && !st->running) {
                if (cui_button(ui, wada_tr("Details"), .variant = CUI_BUTTON_GHOST,
                               .size = CUI_SIZE_SM)) {
                    st->history_selected = 0;
                    st->history_open = true;
                    st->active_view = WC_VIEW_TOOLS;
                }
            }
        }
    }
}
