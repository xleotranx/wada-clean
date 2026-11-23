#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"
#include "wc_safety.h"

void wc_disk_start_scan(wc_cleaner_state *st) {
    if (!st || st->disk_running || !st->disk_root[0]) return;
    if (wc_disk_worker_start(&st->disk_worker, st->disk_root)) {
        st->disk_running = true;
        wada_app_set_status(st->app, wada_tr("Disk analysis started"));
    } else {
        wada_app_set_status(st->app, wada_tr("Invalid folder path"));
    }
}

static void pick_preset_folder(wc_cleaner_state *st, const char *env_var, const char *sub) {
    char base[WC_PATH_CAP];
    if (!wc_plat_get_env(env_var, base, sizeof base) || !base[0]) return;
    if (sub && sub[0]) {
        char sep = (base[0] == '/') ? '/' : '\\';
        size_t blen = strlen(base);
        size_t slen = strlen(sub);
        if (blen + 1 + slen + 1 > sizeof st->disk_root) return;
        memcpy(st->disk_root, base, blen);
        st->disk_root[blen] = sep;
        memcpy(st->disk_root + blen + 1, sub, slen + 1);
    } else {
        snprintf(st->disk_root, sizeof st->disk_root, "%s", base);
    }
    wc_disk_start_scan(st);
}

void wc_view_disk_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);

    char total_buf[32], files_buf[32], dirs_buf[32];
    const char *total_str = "—";
    const char *files_str = "—";
    const char *dirs_str = "—";
    if (st->disk_snap.state != WC_JOB_IDLE) {
        wc_format_bytes(st->disk_snap.total_bytes, total_buf, sizeof total_buf);
        snprintf(files_buf, sizeof files_buf, "%llu",
                 (unsigned long long)st->disk_snap.total_files);
        snprintf(dirs_buf, sizeof dirs_buf, "%llu",
                 (unsigned long long)st->disk_snap.total_dirs);
        total_str = total_buf;
        files_str = files_buf;
        dirs_str = dirs_buf;
    }

    const char *tab_labels[4] = {
        wada_tr("Largest Folders"),
        wada_tr("Largest Files"),
        wada_tr("Duplicates"),
        wada_tr("Stale Dev Artifacts"),
    };
    bool any_busy = st->disk_running || st->disk_extra_running;

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Disk Space Analyzer"), .font = CUI_FONT_DISPLAY, .size = 24);
            cui_text(ui, wada_tr("Find large folders and files without modifying anything on disk."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Path bar + Scan button */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
            cui_input(ui, "disk-root-input", st->disk_root, sizeof st->disk_root,
                      .placeholder = wada_tr("Folder path to analyze…"), .width = CUI_FILL,
                      .disabled = any_busy);
#ifdef _WIN32
            if (!any_busy) {
                if (cui_button(ui, wada_tr("Browse…"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_FOLDER_OPEN)) {
                    char picked[WC_PATH_CAP];
                    if (wc_plat_pick_folder(picked, sizeof picked)) {
                        snprintf(st->disk_root, sizeof st->disk_root, "%s", picked);
                    }
                }
            }
#endif
            if (!any_busy) {
                if (cui_button(ui, wada_tr("Scan"), .variant = CUI_BUTTON_PRIMARY,
                               .icon = CUI_ICON_SEARCH, .disabled = !st->disk_root[0])) {
                    if (st->disk_tab == 2) {
                        wc_disk_extra_start_dups(&st->disk_extra_worker, st->disk_root,
                                                 1024ULL * 1024ULL);
                        wada_app_set_status(app, wada_tr("Scanning for duplicate files…"));
                    } else if (st->disk_tab == 3) {
                        wc_disk_extra_start_stale(&st->disk_extra_worker, st->disk_root, 30);
                        wada_app_set_status(app, wada_tr("Scanning for stale dev artifacts…"));
                    } else {
                        wc_disk_start_scan(st);
                    }
                }
            } else {
                if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_X)) {
                    wc_disk_worker_cancel(&st->disk_worker);
                    wc_disk_extra_cancel(&st->disk_extra_worker);
                }
            }
        }

        /* Quick folder presets */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 6) {
            cui_text(ui, wada_tr("Quick scan:"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
#ifdef _WIN32
            if (cui_button(ui, wada_tr("Home"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = any_busy)) {
                pick_preset_folder(st, "USERPROFILE", NULL);
            }
            if (cui_button(ui, wada_tr("Downloads"), .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .disabled = any_busy)) {
                pick_preset_folder(st, "USERPROFILE", "Downloads");
            }
            if (cui_button(ui, wada_tr("AppData"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = any_busy)) {
                pick_preset_folder(st, "LOCALAPPDATA", NULL);
            }
#else
            if (cui_button(ui, wada_tr("Home"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = any_busy)) {
                pick_preset_folder(st, "HOME", NULL);
            }
            if (cui_button(ui, wada_tr("Downloads"), .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .disabled = any_busy)) {
                pick_preset_folder(st, "HOME", "Downloads");
            }
            if (cui_button(ui, wada_tr("AppData"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = any_busy)) {
                pick_preset_folder(st, "HOME", "Library/Caches");
            }
#endif
        }

        if (st->disk_running) {
            char scanning_lbl[128];
            wada_trf(scanning_lbl, sizeof scanning_lbl, "Scanning %llu files (%s)…",
                     (unsigned long long)st->disk_snap.total_files, total_str);
            cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL, .label = scanning_lbl,
                         .tone = CUI_TONE_ACCENT);
            if (st->disk_snap.current_dir[0]) {
                cui_text(ui, st->disk_snap.current_dir, .size = 12, .color = CUI_COLOR_TEXT_MUTED,
                         .nowrap = true);
            }
        }

        /* Summary metrics */
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, wada_tr("Total Size"), total_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Files"), files_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Folders"), dirs_str, .width = CUI_GROW(1.0f));
        }

        cui_tabs(ui, "disk-tabs", tab_labels, 4, &st->disk_tab);

        if (st->disk_tab == 2 && st->dup_snap) {
            wc_dup_snapshot *ds = st->dup_snap;
            if (st->disk_extra_running) {
                cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL,
                             .label = wada_tr("Hashing duplicate candidates…"),
                             .tone = CUI_TONE_ACCENT);
            } else if (ds->output_summary[0]) {
                cui_alert(ui, ds->output_summary, .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
            }

            const char *qpaths[WC_STALE_MAX_ITEMS];
            int qcnt = 0;
            for (int g = 0; g < ds->group_count; g++) {
                for (int f = 1; f < ds->groups[g].file_count; f++) {
                    if (ds->groups[g].files[f].selected && qcnt < WC_STALE_MAX_ITEMS) {
                        qpaths[qcnt++] = ds->groups[g].files[f].path;
                    }
                }
            }

            char wasted_sz[32], meta[128];
            wc_format_bytes(ds->total_wasted_bytes, wasted_sz, sizeof wasted_sz);
            wada_trf(meta, sizeof meta, "%d duplicate groups (%s reclaimable)", ds->group_count,
                     wasted_sz);

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_text(ui, meta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    if (cui_button(ui, wada_tr("Scan Duplicates (1 MB+)"),
                                   .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM,
                                   .icon = CUI_ICON_SEARCH,
                                   .disabled = any_busy || !st->disk_root[0])) {
                        wc_disk_extra_start_dups(&st->disk_extra_worker, st->disk_root,
                                                 1024ULL * 1024ULL);
                    }
                    if (cui_button(ui, wada_tr("Select Extra Copies"),
                                   .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                                   .disabled = any_busy || ds->group_count == 0)) {
                        for (int g = 0; g < ds->group_count; g++) {
                            ds->groups[g].files[0].selected = false;
                            for (int f = 1; f < ds->groups[g].file_count; f++) {
                                ds->groups[g].files[f].selected = true;
                            }
                        }
                    }
                    if (cui_button(ui, wada_tr("Clear"), .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM, .disabled = any_busy || qcnt == 0)) {
                        for (int g = 0; g < ds->group_count; g++) {
                            for (int f = 0; f < ds->groups[g].file_count; f++) {
                                ds->groups[g].files[f].selected = false;
                            }
                        }
                    }
                    if (cui_button(ui, wada_tr("Move Selected to Quarantine"),
                                   .variant = CUI_BUTTON_PRIMARY, .size = CUI_SIZE_SM,
                                   .icon = CUI_ICON_ARCHIVE,
                                   .disabled = any_busy || qcnt == 0 || !st->quarantine.open)) {
                        wc_disk_extra_start_quarantine(&st->disk_extra_worker, qpaths, qcnt,
                                                       "duplicate");
                    }
                }
            }

            if (ds->state == WC_JOB_IDLE) {
                cui_empty_state(ui, wada_tr("Find exact duplicate files"),
                                .description = wada_tr("Click Scan Duplicates (1 MB+) to match files by size and SHA-256 while keeping one copy safe."));
            } else if (ds->group_count == 0) {
                cui_empty_state(ui, wada_tr("No duplicate files found"),
                                .description = wada_tr("No exact duplicates (1 MB+) were found under this folder."));
            } else {
                wc_scroll_list(ui, "disk-dups", 8) {
                    for (int g = 0; g < ds->group_count; g++) {
                        wc_dup_group *grp = &ds->groups[g];
                        char gsz[32], gtitle[96];
                        wc_format_bytes(grp->file_bytes, gsz, sizeof gsz);
                        wada_trf(gtitle, sizeof gtitle, "%s · %d copies", gsz, grp->file_count);
                        char gid[32];
                        snprintf(gid, sizeof gid, "dupg-%d", g);
                        cui_id_scope(ui, gid) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4) {
                                cui_text(ui, gtitle, .font = CUI_FONT_MEDIUM, .size = 13);
                                /* Copy 0 is permanently protected ("Keep"). */
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 6) {
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                                        cui_badge(ui, wada_tr("Keep"), .tone = CUI_TONE_SUCCESS,
                                                  .size = CUI_SIZE_SM);
                                        cui_text(ui, grp->files[0].path, .font = CUI_FONT_MONO,
                                                 .size = 12, .nowrap = true);
                                    }
                                    if (cui_button(ui, wada_tr("Reveal"),
                                                   .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM)) {
                                        wc_plat_open_in_explorer(grp->files[0].path, true);
                                    }
                                }
                                for (int f = 1; f < grp->file_count; f++) {
                                    char fid[32];
                                    snprintf(fid, sizeof fid, "f-%d", f);
                                    cui_id_scope(ui, fid) {
                                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 6) {
                                            cui_checkbox(ui, grp->files[f].path,
                                                         &grp->files[f].selected,
                                                         .disabled = any_busy);
                                            if (cui_button(ui, wada_tr("Reveal"),
                                                           .variant = CUI_BUTTON_GHOST,
                                                           .size = CUI_SIZE_SM)) {
                                                wc_plat_open_in_explorer(grp->files[f].path, true);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (st->disk_tab == 3 && st->stale_snap) {
            wc_stale_snapshot *ss = st->stale_snap;
            if (st->disk_extra_running) {
                cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL,
                             .label = wada_tr("Scanning for stale dev build folders…"),
                             .tone = CUI_TONE_ACCENT);
            } else if (ss->output_summary[0]) {
                cui_alert(ui, ss->output_summary, .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
            }

            const char *qpaths[WC_STALE_MAX_ITEMS];
            int qcnt = 0;
            for (int i = 0; i < ss->count; i++) {
                if (ss->items[i].selected) qpaths[qcnt++] = ss->items[i].path;
            }

            char tot_sz[32], meta[128];
            wc_format_bytes(ss->total_bytes, tot_sz, sizeof tot_sz);
            wada_trf(meta, sizeof meta, "%d stale folders (%s)", ss->count, tot_sz);

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_text(ui, meta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    if (cui_button(ui, wada_tr("Scan Stale Folders (30d+)"),
                                   .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM,
                                   .icon = CUI_ICON_SEARCH,
                                   .disabled = any_busy || !st->disk_root[0])) {
                        wc_disk_extra_start_stale(&st->disk_extra_worker, st->disk_root, 30);
                    }
                    if (cui_button(ui, wada_tr("Select All"), .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM,
                                   .disabled = any_busy || ss->count == 0)) {
                        for (int i = 0; i < ss->count; i++) ss->items[i].selected = true;
                    }
                    if (cui_button(ui, wada_tr("Clear"), .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM, .disabled = any_busy || qcnt == 0)) {
                        for (int i = 0; i < ss->count; i++) ss->items[i].selected = false;
                    }
                    if (cui_button(ui, wada_tr("Move Selected to Quarantine"),
                                   .variant = CUI_BUTTON_PRIMARY, .size = CUI_SIZE_SM,
                                   .icon = CUI_ICON_ARCHIVE,
                                   .disabled = any_busy || qcnt == 0 || !st->quarantine.open)) {
                        wc_disk_extra_start_quarantine(&st->disk_extra_worker, qpaths, qcnt,
                                                       "stale_dev");
                    }
                }
            }

            if (ss->state == WC_JOB_IDLE) {
                cui_empty_state(ui, wada_tr("Find untouched developer build folders"),
                                .description = wada_tr("Finds node_modules, target, .venv, __pycache__, and build directories untouched for 30+ days."));
            } else if (ss->count == 0) {
                cui_empty_state(ui, wada_tr("No stale dev artifacts found"),
                                .description = wada_tr("No untouched build/dependency directories (30d+) were found here."));
            } else {
                wc_scroll_list(ui, "disk-stale", 6) {
                    for (int i = 0; i < ss->count; i++) {
                        wc_stale_item *it = &ss->items[i];
                        char isz[32], age_str[32];
                        wc_format_bytes(it->bytes, isz, sizeof isz);
                        wada_trf(age_str, sizeof age_str, "%lldd old", it->age_days);
                        cui_id_scope(ui, it->path) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 3) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                    cui_checkbox(ui, it->name, &it->selected, .disabled = any_busy);
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                                        cui_badge(ui, age_str, .tone = CUI_TONE_WARNING,
                                                  .size = CUI_SIZE_SM);
                                        cui_text(ui, isz, .size = 12,
                                                 .color = CUI_COLOR_TEXT_SECONDARY,
                                                 .tabular = true);
                                        if (cui_button(ui, wada_tr("Open"),
                                                       .variant = CUI_BUTTON_GHOST,
                                                       .size = CUI_SIZE_SM)) {
                                            wc_plat_open_in_explorer(it->path, false);
                                        }
                                    }
                                }
                                cui_text(ui, it->path, .font = CUI_FONT_MONO, .size = 12,
                                         .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                            }
                        }
                    }
                }
            }
        } else if (st->disk_snap.state == WC_JOB_IDLE) {
            cui_empty_state(ui, wada_tr("Ready to analyze"),
                            .description = wada_tr("Choose a folder above and click Scan to see what uses disk space."));
        } else if (st->disk_tab == 0) {
            if (st->disk_snap.top_dir_count == 0) {
                cui_empty_state(ui, wada_tr("No subfolders found"),
                                .description = wada_tr("Try scanning a parent folder or checking Largest Files."));
            } else {
                wc_scroll_list(ui, "disk-dirs", 6) {
                    for (int i = 0; i < st->disk_snap.top_dir_count; i++) {
                        const wc_disk_entry *e = &st->disk_snap.top_dirs[i];
                        float ratio = st->disk_snap.total_bytes > 0
                                          ? (float)((double)e->bytes /
                                                    (double)st->disk_snap.total_bytes)
                                          : 0.0f;
                        char sz[32], info[96];
                        wc_format_bytes(e->bytes, sz, sizeof sz);
                        wada_trf(info, sizeof info, "%s (%llu files)", sz,
                                 (unsigned long long)e->file_count);

                        cui_id_scope(ui, e->path) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 6) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                    cui_text(ui, e->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                        cui_text(ui, info, .size = 12,
                                                 .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                                        if (cui_button(ui, wada_tr("Open"),
                                                       .variant = CUI_BUTTON_GHOST,
                                                       .size = CUI_SIZE_SM,
                                                       .icon = CUI_ICON_EXTERNAL_LINK)) {
                                            wc_plat_open_in_explorer(e->path, false);
                                        }
                                    }
                                }
                                cui_progress(ui, ratio, .width = CUI_FILL, .height = 6.0f,
                                             .tone = CUI_TONE_ACCENT);
                            }
                        }
                    }
                }
            }
        } else {
            if (st->disk_snap.top_file_count == 0) {
                cui_empty_state(ui, wada_tr("No files found"),
                                .description = wada_tr("No regular files were found in this folder."));
            } else {
                wc_scroll_list(ui, "disk-files", 6) {
                    for (int i = 0; i < st->disk_snap.top_file_count; i++) {
                        const wc_disk_entry *e = &st->disk_snap.top_files[i];
                        char sz[32];
                        wc_format_bytes(e->bytes, sz, sizeof sz);

                        cui_id_scope(ui, e->path) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 3) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                    cui_text(ui, e->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                        cui_text(ui, sz, .size = 12,
                                                 .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                                        if (cui_button(ui, wada_tr("Reveal"),
                                                       .variant = CUI_BUTTON_GHOST,
                                                       .size = CUI_SIZE_SM,
                                                       .icon = CUI_ICON_EXTERNAL_LINK)) {
                                            wc_plat_open_in_explorer(e->path, true);
                                        }
                                    }
                                }
                                cui_text(ui, e->path, .size = 12, .color = CUI_COLOR_TEXT_MUTED,
                                         .nowrap = true);
                            }
                        }
                    }
                }
            }
        }
    }
}
