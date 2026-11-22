#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"
#include "wc_safety.h"

/* Compact byte string for the narrow summary metric cards: keeps "276 MB"
 * inside a 1120pt window instead of clipping "276.2 MB" with an ellipsis. */
static void format_metric_bytes(uint64_t bytes, char *out, size_t cap) {
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    double value = (double)bytes;
    if (value < kb) {
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    } else if (value < 10.0 * mb) {
        snprintf(out, cap, "%.1f MB", value / mb);
    } else if (value < gb) {
        snprintf(out, cap, "%.0f MB", value / mb);
    } else if (value < 10.0 * gb) {
        snprintf(out, cap, "%.2f GB", value / gb);
    } else {
        snprintf(out, cap, "%.1f GB", value / gb);
    }
}

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

    float ratio = 0.0f;
    if (res->analyzed && st->snap.total_bytes_found > 0) {
        ratio = (float)((double)res->bytes_found / (double)st->snap.total_bytes_found);
        if (ratio > 1.0f) ratio = 1.0f;
    }

    bool highlight = st->selected[idx] && res->analyzed && res->bytes_found > 0;

    cui_id_scope(ui, def->id) {
        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 5, .selected = highlight) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                if (cui_checkbox(ui, wada_tr(def->label), &st->selected[idx],
                                 .disabled = st->running)) {
                    wada_ui_confirm_reset(&st->confirm);
                }

                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    if (def->needs_admin && !st->is_admin) {
                        cui_badge(ui, wada_tr("Admin"), .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                    }
                    if (res->browser_running) {
                        cui_badge(ui, wada_tr("Running"), .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                    }
                    if (res->cleaned) {
                        cui_badge(ui, wada_tr(st->snap.dry_run ? "Previewed" : "Cleaned"),
                                  .tone = st->snap.dry_run ? CUI_TONE_INFO : CUI_TONE_SUCCESS,
                                  .size = CUI_SIZE_SM);
                    }
                    cui_text(ui, meta,
                             .font = (res->analyzed && res->bytes_found > 0) ? CUI_FONT_MEDIUM
                                                                            : CUI_FONT_BODY,
                             .size = 12,
                             .color = (res->analyzed || res->cleaned) ? CUI_COLOR_TEXT_SECONDARY
                                                                      : CUI_COLOR_TEXT_MUTED,
                             .tabular = true);
                    if (def->kind != WC_TARGET_RECYCLE_BIN && st->resolved_paths[idx][0]) {
                        if (cui_icon_button(ui, CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM,
                                            .tooltip = wada_tr("Open folder"))) {
                            wc_plat_open_in_explorer(st->resolved_paths[idx], false);
                        }
                    }
                }
            }

            if (res->analyzed && res->bytes_found > 0 && st->snap.total_bytes_found > 0) {
                cui_progress(ui, ratio, .width = CUI_FILL, .height = 5.0f,
                             .tone = st->selected[idx] ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL);
            }

            if (def->kind == WC_TARGET_RECYCLE_BIN) {
                cui_text(ui, wada_tr("Emptied permanently — this cannot be undone."), .size = 12,
                         .color = CUI_COLOR_WARNING);
            }

            if (def->kind != WC_TARGET_RECYCLE_BIN && st->resolved_paths[idx][0]) {
                cui_text(ui, st->resolved_paths[idx], .size = 12, .color = CUI_COLOR_TEXT_MUTED,
                         .nowrap = true);
            }
            if (res->note[0]) {
                cui_color note_color = (res->browser_running || res->admin_blocked)
                                           ? CUI_COLOR_WARNING
                                           : CUI_COLOR_TEXT_MUTED;
                cui_text(ui, wada_tr(res->note), .size = 12, .color = note_color);
            }
        }
    }
}

static void draw_category_group(wc_cleaner_state *st, cui_ctx *ui, wc_category cat,
                                const char *heading, const char *icon) {
    int cat_total = 0;
    int cat_selected = 0;
    int cat_analyzed_count = 0;
    uint64_t cat_bytes = 0;
    for (int i = 0; i < st->target_count; i++) {
        if (st->defs[i].category != cat) continue;
        cat_total++;
        if (st->selected[i]) cat_selected++;
        if (st->snap.results[i].analyzed) {
            cat_analyzed_count++;
            cat_bytes += st->snap.results[i].bytes_found;
        }
    }

    char cat_badge[32];
    cui_tone cat_tone = CUI_TONE_NEUTRAL;
    if (cat_analyzed_count > 0) {
        wc_format_bytes(cat_bytes, cat_badge, sizeof cat_badge);
        cat_tone = cat_bytes > 0 ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL;
    } else {
        snprintf(cat_badge, sizeof cat_badge, "%d/%d", cat_selected, cat_total);
    }

    cui_id_scope(ui, heading) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                cui_icon(ui, icon, 14.0f, CUI_COLOR_TEXT_SECONDARY);
                cui_text(ui, wada_tr(heading), .font = CUI_FONT_MEDIUM, .size = 13,
                         .color = CUI_COLOR_TEXT_SECONDARY);
                cui_badge(ui, cat_badge, .tone = cat_tone, .size = CUI_SIZE_SM);
            }
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 4) {
                if (cat_selected < cat_total) {
                    if (cui_button(ui, wada_tr("Select All"), .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM, .disabled = st->running)) {
                        wc_cleaner_select_category(st, cat, true);
                    }
                }
                if (cat_selected > 0) {
                    if (cui_button(ui, wada_tr("Clear"), .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM, .disabled = st->running)) {
                        wc_cleaner_select_category(st, cat, false);
                    }
                }
            }
        }
    }

    for (int i = 0; i < st->target_count; i++) {
        if (st->defs[i].category == cat) {
            draw_target_row(st, ui, i);
        }
    }
}

static int history_target_count(const wc_history_entry *entry) {
    int count = 0;
    const char *p = entry->details;
    while (p && *p) {
        count++;
        const char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : p + strlen(p);
    }
    return count;
}

static void draw_history_panel(wc_cleaner_state *st, cui_ctx *ui) {
    if (st->history_selected < 0 || st->history_selected >= st->history_count) {
        st->history_open = false;
        return;
    }
    const wc_history_entry *entry = &st->history_entries[st->history_selected];

    char when[48];
    wada_ui_format_when(entry->unix_sec, when, sizeof when);
    char freed[32];
    wc_format_bytes(entry->bytes_cleaned, freed, sizeof freed);
    char files_str[32];
    snprintf(files_str, sizeof files_str, "%llu",
             (unsigned long long)entry->files_cleaned);
    char skipped_str[32];
    snprintf(skipped_str, sizeof skipped_str, "%llu",
             (unsigned long long)entry->skipped_count);
    int targets = history_target_count(entry);
    char targets_str[32];
    if (targets > 0) {
        snprintf(targets_str, sizeof targets_str, "%d", targets);
    } else {
        snprintf(targets_str, sizeof targets_str, "—");
    }

    char status[96];
    wada_trf(status, sizeof status, "%s · %s", when,
             wada_tr(entry->dry_run ? "Dry Run" : "Clean"));

    char summary_line[192];
    if (entry->dry_run) {
        wada_trf(summary_line, sizeof summary_line, "Preview: %s across %llu files.", freed,
                 (unsigned long long)entry->files_cleaned);
    } else {
        wada_trf(summary_line, sizeof summary_line, "Freed %s across %llu files.", freed,
                 (unsigned long long)entry->files_cleaned);
    }

    char copy[WC_HISTORY_DETAIL_CAP + 512];
    int cn = snprintf(copy, sizeof copy, "%s — %s\n%s\n%s: %s\n%s: %llu\n%s: %llu\n",
                      wada_tr(entry->dry_run ? "Preview run" : "Clean run"), status,
                      summary_line, wada_tr("Freed"), freed, wada_tr("Files"),
                      (unsigned long long)entry->files_cleaned, wada_tr("Skipped"),
                      (unsigned long long)entry->skipped_count);
    if (cn > 0 && (size_t)cn < sizeof copy && entry->details[0]) {
        size_t used = (size_t)cn;
        snprintf(copy + used, sizeof copy - used, "%s", entry->details);
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 10) {
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 10) {
                if (cui_button(ui, wada_tr("Back"), .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_CHEVRON_LEFT)) {
                    st->history_open = false;
                }
                cui_column(ui, .gap = 2) {
                    cui_text(ui, wada_tr(entry->dry_run ? "Preview run" : "Clean run"),
                             .font = CUI_FONT_DISPLAY, .size = 24);
                    cui_text(ui, status, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }
            }
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                cui_copy_button(ui, "history-copy", copy);
                cui_text(ui, wada_tr("Copy details"), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, wada_tr("Freed"), freed, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Files"), files_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Skipped"), skipped_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Targets"), targets_str, .width = CUI_GROW(1.0f));
        }

        if (summary_line[0]) {
            cui_text(ui, summary_line, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        if (entry->details[0] == '\0') {
            cui_alert(ui,
                      wada_tr("This run predates detailed history; only totals are available."),
                      .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
        } else {
            cui_text(ui, wada_tr("Per-target results"), .font = CUI_FONT_MEDIUM, .size = 15);
            wc_scroll_list(ui, "history-detail", 6) {
                const char *p = entry->details;
                while (p && *p) {
                    const char *nl = strchr(p, '\n');
                    size_t len = nl ? (size_t)(nl - p) : strlen(p);
                    char line[WC_HISTORY_DETAIL_CAP];
                    if (len >= sizeof line) len = sizeof line - 1;
                    memcpy(line, p, len);
                    line[len] = '\0';
                    p = nl ? nl + 1 : p + strlen(p);

                    char *fields[6];
                    int nf = 0;
                    char *cur = line;
                    while (nf < 6) {
                        fields[nf++] = cur;
                        char *bar = strchr(cur, '|');
                        if (!bar) break;
                        *bar = '\0';
                        cur = bar + 1;
                    }
                    if (nf < 5) continue;

                    const char *label = fields[1][0] ? fields[1] : fields[0];
                    uint64_t f = strtoull(fields[2], NULL, 10);
                    uint64_t b = strtoull(fields[3], NULL, 10);
                    uint64_t s = strtoull(fields[4], NULL, 10);
                    const char *note = nf >= 6 ? fields[5] : "";
                    char size_str[32];
                    wc_format_bytes(b, size_str, sizeof size_str);
                    char meta[160];
                    wada_trf(meta, sizeof meta, "%s · %llu files · %llu skipped", size_str,
                             (unsigned long long)f, (unsigned long long)s);

                    cui_id_scope(ui, fields[0]) {
                        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4, .muted = true) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                cui_text(ui, wada_tr(label), .font = CUI_FONT_MEDIUM,
                                         .size = 13);
                                cui_text(ui, meta, .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                            }
                            if (note[0]) {
                                bool warn = strstr(note, "skipped") != NULL ||
                                            strstr(note, "Administrator") != NULL ||
                                            strstr(note, "Access") != NULL ||
                                            strstr(note, "Failed") != NULL ||
                                            strstr(note, "Browser") != NULL;
                                cui_text(ui, wada_tr(note), .size = 12,
                                         .color = warn ? CUI_COLOR_WARNING
                                                       : CUI_COLOR_TEXT_MUTED);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void draw_preview_panel(wc_cleaner_state *st, cui_ctx *ui) {
    int target_count = 0;
    int shown_total = 0;
    for (int i = 0; i < st->target_count; i++) {
        if (!st->selected[i]) continue;
        int shown = 0;
        for (int p = 0; p < st->preview_ui_count; p++) {
            if (strcmp(st->preview_items[p].target_id, st->defs[i].id) == 0) shown++;
        }
        if (shown > 0) {
            target_count++;
            shown_total += shown;
        }
    }

    char sub[160];
    wada_trf(sub, sizeof sub, "%d files sampled from %d targets", shown_total, target_count);

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 10) {
            if (cui_button(ui, wada_tr("Back"), .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_CHEVRON_LEFT)) {
                st->preview_open = false;
            }
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Preview files"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, sub, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        if (shown_total == 0) {
            cui_empty_state(ui, wada_tr("No files were sampled. Run Analyze again."),
                            .description = wada_tr("Analysis samples files from each selected target."));
        } else {
            if (st->preview_ui_truncated) {
                char msg[190];
                wada_trf(msg, sizeof msg,
                         "Showing a sample of %d files. The Cleaner summary has the exact totals.",
                         shown_total);
                cui_alert(ui, msg, .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
            }

            cui_text(ui, wada_tr("What will be removed"), .font = CUI_FONT_MEDIUM, .size = 15);
            cui_text(ui, wada_tr("Locked or in-use files are skipped when cleaning."), .size = 12,
                     .color = CUI_COLOR_TEXT_MUTED);

            wc_scroll_list(ui, "preview-files", 2) {
                for (int i = 0; i < st->target_count; i++) {
                    if (!st->selected[i]) continue;
                    const wc_target_def *def = &st->defs[i];
                    int shown = 0;
                    for (int p = 0; p < st->preview_ui_count; p++) {
                        if (strcmp(st->preview_items[p].target_id, def->id) == 0) shown++;
                    }

                    if (def->kind == WC_TARGET_RECYCLE_BIN) {
                        cui_text(ui,
                                 wada_tr("Recycle Bin contents cannot be listed; it is emptied entirely."),
                                 .size = 12, .color = CUI_COLOR_WARNING);
                        continue;
                    }
                    if (shown == 0) continue;

                    char head[160];
                    wada_trf(head, sizeof head, "%s (%d)", wada_tr(def->label), shown);
                    cui_text(ui, head, .font = CUI_FONT_MEDIUM, .size = 13,
                             .color = CUI_COLOR_TEXT_SECONDARY);

                    for (int p = 0; p < st->preview_ui_count; p++) {
                        const wc_preview_item *item = &st->preview_items[p];
                        if (strcmp(item->target_id, def->id) != 0) continue;
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
            }
        }
    }
}

void wc_view_cleaner_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);
    if (st->preview_open) {
        draw_preview_panel(st, ui);
        return;
    }
    if (st->history_open) {
        draw_history_panel(st, ui);
        return;
    }

    int sel_count = 0;
    uint64_t sel_bytes_found = 0;
    uint64_t sel_files_found = 0;
    uint64_t sys_bytes_found = 0;
    uint64_t browser_bytes_found = 0;
    bool any_browser_running = false;

    for (int i = 0; i < st->target_count; i++) {
        const wc_target_result *res = &st->snap.results[i];
        if (res->browser_running) any_browser_running = true;
        if (st->selected[i]) {
            sel_count++;
            if (res->analyzed) {
                sel_bytes_found += res->bytes_found;
                sel_files_found += res->files_found;
            }
        }
        if (res->analyzed) {
            if (st->defs[i].category == WC_CAT_SYSTEM) {
                sys_bytes_found += res->bytes_found;
            } else {
                browser_bytes_found += res->bytes_found;
            }
        }
    }

    bool confirming = wada_ui_confirm_active(&st->confirm, 0);
    char sel_bytes_str[32];
    wc_format_bytes(sel_bytes_found, sel_bytes_str, sizeof sel_bytes_str);

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
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                if (cui_switch(ui, wada_tr("Preview only"), &st->dry_run,
                               .disabled = st->running)) {
                    wada_ui_confirm_reset(&st->confirm);
                }
            }
        }

        /* Hero Command Center card */
        const char *hero_badge = "Ready to Analyze";
        cui_tone hero_tone = CUI_TONE_NEUTRAL;
        char hero_headline[64];
        char hero_sub[160];

        if (st->running) {
            if (st->snap.kind == WC_JOB_CLEAN) {
                hero_badge = st->snap.dry_run ? "Previewing…" : "Cleaning…";
            } else {
                hero_badge = "Analyzing…";
            }
            hero_tone = CUI_TONE_INFO;
            if (st->has_analyzed && sel_bytes_found > 0) {
                snprintf(hero_headline, sizeof hero_headline, "%s", sel_bytes_str);
            } else {
                wada_trf(hero_headline, sizeof hero_headline, "%d of %d targets selected",
                         sel_count, st->target_count);
            }
            const char *fmt = "Scanning: %s";
            if (st->snap.kind == WC_JOB_CLEAN) {
                fmt = st->snap.dry_run ? "Previewing: %s" : "Cleaning: %s";
            }
            const char *cur = st->snap.current_label[0] ? wada_tr(st->snap.current_label) : "";
            wada_trf(hero_sub, sizeof hero_sub, fmt, cur);
        } else if (st->snap.kind == WC_JOB_CLEAN && st->snap.state == WC_JOB_DONE) {
            hero_badge = st->snap.dry_run ? "Preview Complete" : "Clean Complete";
            hero_tone = st->snap.dry_run ? CUI_TONE_INFO : CUI_TONE_SUCCESS;
            wc_format_bytes(st->snap.total_bytes_cleaned, hero_headline, sizeof hero_headline);
            wada_trf(hero_sub, sizeof hero_sub,
                     st->snap.dry_run ? "Would clean %s (%llu files)" : "Cleaned %s (%llu files)",
                     hero_headline, (unsigned long long)st->snap.total_files_cleaned);
        } else if (st->has_analyzed) {
            hero_badge = "Ready to Clean";
            hero_tone = sel_bytes_found > 0 ? CUI_TONE_WARNING : CUI_TONE_SUCCESS;
            snprintf(hero_headline, sizeof hero_headline, "%s", sel_bytes_str);
            wada_trf(hero_sub, sizeof hero_sub, "%llu files across %d targets",
                     (unsigned long long)sel_files_found, sel_count);
        } else {
            wada_trf(hero_headline, sizeof hero_headline, "%d of %d targets selected",
                     sel_count, st->target_count);
            snprintf(hero_sub, sizeof hero_sub, "%s",
                     wada_tr("Click Analyze to scan selected targets and preview reclaimable space."));
        }

        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
                cui_column(ui, .width = CUI_GROW(1.0f), .gap = 4) {
                    cui_badge(ui, wada_tr(hero_badge), .tone = hero_tone, .dot = true);
                    cui_text(ui, hero_headline, .font = CUI_FONT_DISPLAY, .size = 24,
                             .tabular = true);
                    cui_text(ui, hero_sub, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }

                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                    if (cui_button(ui, wada_tr("Analyze"),
                                   .variant = st->has_analyzed ? CUI_BUTTON_OUTLINE
                                                               : CUI_BUTTON_PRIMARY,
                                   .icon = CUI_ICON_REFRESH_CW,
                                   .disabled = st->running || sel_count == 0,
                                   .loading = st->running && st->snap.kind == WC_JOB_ANALYZE)) {
                        wc_cleaner_start_analyze(st);
                    }

                    if (cui_button(ui, wada_tr("Preview files"), .variant = CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_EYE,
                                   .disabled = !st->has_analyzed || st->running)) {
                        st->preview_open = true;
                    }

                    bool clean_disabled = st->running || sel_count == 0 ||
                                          (!st->dry_run && !st->has_analyzed);
                    char clean_label[64];
                    if (confirming) {
                        snprintf(clean_label, sizeof clean_label, "%s", wada_tr("Confirm Clean"));
                    } else if (st->dry_run) {
                        snprintf(clean_label, sizeof clean_label, "%s", wada_tr("Preview Clean"));
                    } else if (st->has_analyzed && sel_bytes_found > 0) {
                        wada_trf(clean_label, sizeof clean_label, "Clean %s", sel_bytes_str);
                    } else {
                        snprintf(clean_label, sizeof clean_label, "%s", wada_tr("Clean Selected"));
                    }
                    char clean_btn[80];
                    snprintf(clean_btn, sizeof clean_btn, "%s###clean-action", clean_label);
                    cui_button_variant clean_variant =
                        confirming ? CUI_BUTTON_DANGER
                                   : (st->has_analyzed ? CUI_BUTTON_PRIMARY : CUI_BUTTON_OUTLINE);

                    if (cui_button(ui, clean_btn, .variant = clean_variant,
                                   .icon = st->dry_run ? CUI_ICON_EYE : CUI_ICON_TRASH_2,
                                   .disabled = clean_disabled,
                                   .loading = st->running && st->snap.kind == WC_JOB_CLEAN)) {
                        wc_cleaner_start_clean(st);
                    }

                    if (st->running) {
                        if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_GHOST,
                                       .icon = CUI_ICON_X)) {
                            wc_cleaner_cancel_job(st);
                        }
                    }
                }
            }

            if (st->running) {
                float frac = st->snap.total_selected > 0
                                 ? (float)st->snap.completed_targets /
                                       (float)st->snap.total_selected
                                 : 0.0f;
                cui_progress(ui, frac, .width = CUI_FILL, .label = hero_sub, .show_value = true,
                             .tone = CUI_TONE_ACCENT);
            } else if (confirming) {
                cui_text(ui, wada_tr("Click Confirm Clean to confirm."), .size = 12,
                         .color = CUI_COLOR_WARNING);
            }
        }

        /* Two-column main content */
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 16) {
            /* Left: Cleaner targets */
            cui_column(ui, .width = CUI_GROW(1.45f), .height = CUI_FILL, .gap = 6) {
                wc_scroll_list(ui, "cleaner-targets", 6) {
                    draw_category_group(st, ui, WC_CAT_SYSTEM, "System", CUI_ICON_CPU);
                    cui_separator(ui);
                    draw_category_group(st, ui, WC_CAT_BROWSER, "Browsers", CUI_ICON_GLOBE);
                }
            }

            /* Right: Summary + Recent History */
            cui_column(ui, .width = CUI_GROW(1.0f), .height = CUI_FILL, .gap = 12) {
                cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 10) {
                    cui_text(ui, wada_tr("Summary"), .font = CUI_FONT_MEDIUM, .size = 15);

                    char reclaim_str[32], freed_str[32], skip_str[32];
                    char sel_metric[32];
                    bool measured = st->has_analyzed || st->snap.state == WC_JOB_DONE;
                    if (measured) {
                        format_metric_bytes(st->snap.total_bytes_found, reclaim_str,
                                            sizeof reclaim_str);
                        format_metric_bytes(sel_bytes_found, sel_metric, sizeof sel_metric);
                    } else {
                        snprintf(reclaim_str, sizeof reclaim_str, "—");
                        snprintf(sel_metric, sizeof sel_metric, "—");
                    }
                    const wc_history_entry *last_clean = wc_last_clean_run(st);
                    if (last_clean) {
                        format_metric_bytes(last_clean->bytes_cleaned, freed_str,
                                            sizeof freed_str);
                        snprintf(skip_str, sizeof skip_str, "%llu",
                                 (unsigned long long)last_clean->skipped_count);
                    } else {
                        snprintf(freed_str, sizeof freed_str, "—");
                        snprintf(skip_str, sizeof skip_str, "—");
                    }

                    cui_row(ui, .width = CUI_FILL, .gap = 12) {
                        cui_metric(ui, wada_tr("Selected to Clean"), sel_metric,
                                   .width = CUI_GROW(1.0f));
                        cui_metric(ui, wada_tr("Reclaimable"), reclaim_str,
                                   .width = CUI_GROW(1.0f));
                    }
                    cui_row(ui, .width = CUI_FILL, .gap = 12) {
                        cui_metric(ui, wada_tr("Last Freed"), freed_str, .width = CUI_GROW(1.0f));
                        cui_metric(ui, wada_tr("Last Skipped"), skip_str,
                                   .width = CUI_GROW(1.0f));
                    }

                    if (st->has_analyzed && st->snap.total_bytes_found > 0) {
                        char sys_str[32], browser_str[32];
                        wc_format_bytes(sys_bytes_found, sys_str, sizeof sys_str);
                        wc_format_bytes(browser_bytes_found, browser_str, sizeof browser_str);
                        float sys_ratio = (float)((double)sys_bytes_found /
                                                  (double)st->snap.total_bytes_found);
                        float browser_ratio = (float)((double)browser_bytes_found /
                                                      (double)st->snap.total_bytes_found);
                        if (sys_ratio > 1.0f) sys_ratio = 1.0f;
                        if (browser_ratio > 1.0f) browser_ratio = 1.0f;

                        cui_column(ui, .width = CUI_FILL, .gap = 6) {
                            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_BETWEEN) {
                                cui_text(ui, wada_tr("System"), .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY);
                                cui_text(ui, sys_str, .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                            }
                            cui_progress(ui, sys_ratio, .width = CUI_FILL, .height = 5.0f,
                                         .tone = CUI_TONE_ACCENT);

                            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_BETWEEN) {
                                cui_text(ui, wada_tr("Browsers"), .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY);
                                cui_text(ui, browser_str, .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                            }
                            cui_progress(ui, browser_ratio, .width = CUI_FILL, .height = 5.0f,
                                         .tone = CUI_TONE_TEAL);
                        }
                    }

                    if (st->dry_run) {
                        cui_alert(ui, wada_tr("Dry-run preview mode is enabled. No files will be deleted."),
                                  .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
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
                        cui_empty_state(ui, wada_tr("No clean runs recorded yet."),
                                        .icon = CUI_ICON_HISTORY);
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
                                            cui_text(ui, when, .size = 12,
                                                     .color = CUI_COLOR_TEXT_MUTED);
                                            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                                                cui_badge(ui,
                                                          wada_tr(e->dry_run ? "Dry Run"
                                                                             : "Clean"),
                                                          .tone = e->dry_run ? CUI_TONE_INFO
                                                                             : CUI_TONE_SUCCESS,
                                                          .size = CUI_SIZE_SM);
                                                if (cui_button(ui, wada_tr("View"),
                                                               .variant = CUI_BUTTON_GHOST,
                                                               .size = CUI_SIZE_SM)) {
                                                    st->history_selected = i;
                                                    st->history_open = true;
                                                }
                                            }
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
