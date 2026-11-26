# Wada Clean — "One Door"

**Status:** direction approved, awaiting sign-off on step 1 · **Target release:** v0.2.0
**Supersedes:** the feature set described in `PLAN.md` §4 and `README.md` Features.

---

## 0. Decision log

| # | Decision | Owner call |
|---|---|---|
| 1 | Audience: **student + office worker** | confirmed |
| 2 | Promise: **more disk space. Only.** | confirmed |
| 3 | Uninstall apps: **in scope** | confirmed |
| 4 | macOS: **out of scope** | confirmed |
| 5 | Deep Clean: **unhook now, delete after one release** | recommended |
| 6 | Targets: **zero checkboxes** — 3 group switches + 1 explicit Recycle Bin row | recommended |
| 7 | Dev caches: **kept**, silently inside System junk | corrected from first review |
| 8 | "Old installers in Downloads": **removed** (user data, not junk) | confirmed |
| 9 | Teams cache: **add**, gated on path verification on a real install | recommended |

Two corrections to the first review, recorded so they are not silently dropped:

- **No undo.** The first review promised "undo/restore" in the done state. That was wrong. Junk cleaning deletes files; moving them to a quarantine vault would reclaim *no* space, which defeats the product's only promise. Quarantine only ever had producers in the features being removed (leftovers, orphaned MSIs, duplicate/stale scans, and the `win.old_downloads` rule). With those gone, the vault has no producer and its UI is removed. The done state must therefore be honest: *caches regenerate; personal files were never touched* — plus the explicit Recycle Bin decision, which is the one irreversible action.
- **Dev caches stay.** A CS student's `npm`/`pip`/`gradle` caches are real gigabytes and regenerate on demand. They are the biggest non-obvious win for this audience; they simply never appear as a decision.

---

## 1. The promise

> **Wada Clean frees up disk space on a Windows PC in one click, and never touches your files.**

Success is a student or office worker with a full drive who gets several GB back in under a minute without reading a single explanation, and who never has to learn a Windows concept to do it.

Explicitly **not** our job anymore: speed, startup, drivers, updates, privacy, registry, "optimization".

## 2. Audience and the sentence we answer

The user's sentence, verbatim: *"Windows says my disk is full and I need space before I can install / save / update something."*

They know: their drive letter, "GB", "Downloads", "Recycle Bin", the names of their apps.
They do **not** know: `%TEMP%`, WinSxS, DISM, `/ResetBase`, DriverStore, VSS, CompactOS, StartupApproved, winget, SQLite cookie stores, npm caches.

Every screen must be readable by the second group without a manual. If a label requires a footnote, the label is wrong.

## 3. Scorecard

| Quick Diagnostic row | v0.1.0 | v0.2.0 target |
|---|---|---|
| One Thing statable in one sentence | ✗ | ✓ |
| New user reaches core value in ≤3 steps | ✗ (4–5 actions, 27 checkboxes) | ✓ (scan → 1 click) |
| Reviewer experienced it cold | ✓ | ✓ |
| Working demo on the real device | ✓ | ✓ |
| Anything removed this cycle | ✗ | ✓ (~7,500 LOC) |
| Error/empty/edge states at hero quality | ✗ | ✓ (3 honest states) |
| Team would sign it and use it daily | ✗ | ✓ |
| **Score** | **3/10 — not done** | **9/10 — ships** |

## 4. What ships

### Screen map

```
Free up space  (home, the only default screen)
├─ scanning state        "Checking safe locations…"          [Cancel]
├─ ready state           "12.4 GB can be freed"      [Free up 12.4 GB]  [Review]
│   ├─ System junk      8.1 GB   [on]     (admin extras noted inline)
│   ├─ Browser junk     3.9 GB   [on]     Chrome · Edge · Firefox
│   └─ Recycle Bin      412 MB   [off]    18 items · cannot be undone
├─ done state            "2.9 GB freed"
│   ├─ What was cleaned  (per-group totals, optional detail)
│   └─ honest footer     "Caches rebuild as needed. Your files were not touched."
├─ Big apps              3 apps use 48 GB → [Uninstall]
├─ What's using space?   top folders · largest files        [Open]
└─ Last clean            2.1 GB · 3 days ago                [Details]

Tools  (one door)
├─ Apps          full list, search, sort, uninstall
├─ Space         read-only breakdown: top folders, largest files
├─ History       previous runs
└─ About         version, data folder, licence
```

**Navigation: 9 destinations → 2.** No sidebar sections, no command palette, no `Ctrl+1–9`, no theme switch, no admin badge, no "Preview only" switch.

### The only flow

1. **Scan on launch** — automatically, no `Analyze` button. Progress is honest ("Checking 24 safe locations…", real cancel). First number on screen in ≤ 8 s on a typical laptop.
2. **One decision** — the number, one primary button, one `Review` link. `Review` opens group sizes plus the sampled file list (read-only, the old preview panel, kept because it is the trust feature).
3. **One confirmation** — the button restates the amount: `Free up 12.4 GB`. Recycle Bin is a separate, named, off-by-default row.
4. **Result** — freed amount as the hero. No celebration animation, no score, no "PC is 98% clean".

### Three states, no others

`scanning` → `ready` → `done`. There is no green "System Ready" before data exists (the v0.1 fix for that is preserved by never showing a state without data). Returning to the screen after `done` shows the last result until a new scan runs.

## 5. Cleaning policy

User-facing decisions: **3 group switches** (System junk on, Browser junk on, Recycle Bin off).

| Group | Rules folded in (silent) | Notes |
|---|---|---|
| **System junk** | User temp · Windows temp (admin) · Windows Update cache (admin) · Thumbnail cache · Crash dumps · WER reports · D3D/NVIDIA/AMD shader caches · npm/pnpm/pip/uv/NuGet/Cargo/Gradle/Go caches · Teams cache (new, gated) | Locked files are skipped and reported as a count, never an error. Admin-only locations are skipped silently when not elevated, with one quiet inline line: *"2 system locations need administrator — restart as administrator to include them."* |
| **Browser junk** | Chrome · Edge · Brave · Firefox cache | If a browser is running, its cache is skipped with the reason on the row ("Chrome is open — quit it to include this"). Never prompt to close it. |
| **Recycle Bin** | Recycle Bin | Off by default. Own row. Copy: *"Empties 18 items · 412 MB. This cannot be undone."* Never bundled into "clean everything". |

**Removed rule:** `win.old_downloads` ("Old Installers in Downloads, 30d+") — that is the user's own file cabinet.

**Added rule (gated):** Teams cache. Not installed on the dev machine, so paths must be confirmed on a real install before this ships:

- new Teams: `%LOCALAPPDATA%\Packages\MSTeams_8wekyb3d8bbwe\LocalCache\Microsoft\MSTeams\EBWebView\Default\Cache`
- classic Teams: `%APPDATA%\Microsoft\Teams\Cache`

Implement as two rules under the same label; the engine already skips unresolvable paths. If neither path can be verified, the target does not ship this cycle.

## 6. What leaves

### Surfaces removed outright

| Removed | Files | ~LOC | Why |
|---|---|---|---|
| Deep Clean (12 OS operations) | `src/ui/wc_view_deep.c` | 644 | Admin, irreversible, outside the audience's competence. Windows Settings already exposes this. |
| Startup manager | `src/ui/wc_view_startup.c`, `src/platform/win32/wc_win_startup.c` | 327 | Not space. Task Manager does it. |
| Software updater | `src/ui/wc_view_updater.c`, `src/engine/wc_updater.c`, `src/platform/win32/wc_win_updater.c` | 689 | Updating ≠ space. Requires winget. |
| Automation & tray | `src/ui/wc_view_schedule.c`, `src/platform/win32/wc_win_sched.c`, `src/platform/win32/wc_win_dialog.c` | 388 | Storage Sense exists; our version was session-only and didn't notify. |
| Exclusions + cookie allowlist | `src/ui/wc_view_exclusions.c`, `src/engine/wc_cookies.c`, `src/engine/wc_cookie_job.c`, `src/platform/wc_cookies_sql.c`, `src/platform/win32/wc_win_cookies.c` | 895 | Expert forms; cookie pruning logs people out of their bank. |
| Leftovers / duplicate / stale scans | `src/engine/wc_leftovers.c`, `src/engine/wc_sha256.c`, ~170 lines of duplicate/stale workers embedded in `wc_disk.c` | ~795 | Slow hashing; expert judgment about which copy to keep. |
| Quarantine vault | `src/engine/wc_quarantine.c` | 286 | No producers remain; see §0 correction. |
| macOS backend | `src/platform/macos/*` (8 files) | 1,710 | Out of scope; halves QA and CMake surface. |
| Power chrome | palette (13 commands), `Ctrl+1–9`, `Ctrl+K/P/H/T`, theme switch, admin badges, shortcut sheet | — | The audience will never open them. |
| i18n for all of the above | ~150 pairs of 408 in `wc_product.c` §strings | — | Delete with their screens. |

**Total: ≈ 7,500 lines** — the surfaced modules above (≈5,700), plus roughly 1,000 lines of the `wc_product.c` shell and i18n table, ~900 lines of tests, and ~250 lines of docs. That is ~45% of the 17,000-line codebase (src + tests), and the largest deletion in the project's history.

### Kept on purpose

- Engine + safety model (`wc_safety`, `wc_rules`, `wc_worker`, `wc_history`, `wc_disk` core, `wc_platform` fs/process/apps).
- The sampled file preview (demoted to `Review`).
- Per-group sizes, per-run history.
- EN/VI localization, portable ZIP, zero telemetry, MIT.
- `--dry-run` (used by tests and by cautious users). `--scheduled-clean` is removed with the Automation UI.

### Retired, not deleted, for one release

`wc_deep.c`, `wc_win_deep.c`, `wc_deep.h` stay in the tree but are **excluded from the `wada_clean` target** behind `option(WADA_CLEAN_DEEP_TOOLS "Build the separate deep-maintenance tool" OFF)`. Nothing links or exposes them; tests for the parsers may still run. Delete outright in v0.3 if no issue asks for it. This keeps one release of insurance and costs the shipped binary nothing.

## 7. Implementation steps

Each step builds, keeps `ctest` green, and is shippable on its own. **Step 1 touches UI only** — no engine or platform file changes — so the cut can be reviewed as pure subtraction.

| Step | Work | Exit criteria |
|---|---|---|
| **1. Subtract** | Remove 5 screens (Deep, Startup, Updater, Exclusions, Automation) from nav, routes, command registrations, `CMakeLists.txt` app sources, and the i18n table. Nav becomes Health · Cleaner · Apps · Disk. | App builds; 4 destinations; no dead command in the palette-removed build; `ctest` unchanged and green. |
| **2. Merge + door** | New `wc_view_home.c` replaces Health + Cleaner: auto-scan on first paint, one primary button, 3 group rows, `Review` panel. New `wc_view_tools.c` hub (Apps · Space · History · About). Nav: **Free up space · Tools**. Old health/cleaner views deleted. | Cold launch → number → clean, 2 actions; no `Analyze`; Tools reachable in one click. |
| **3. Harden the one destructive decision** | Recycle Bin row with count + irreversibility copy; confirmation restates the amount; done state with honest footer; one admin path (inline "restart as administrator" link, no second CTA); remove `Preview only` switch. | Recycle Bin cannot be cleaned without reading its consequence; no irreversible action in a bundle; status bar shows scan state, not "dark 0.1.0". |
| **4. Space actions** | Big-apps strip on home (top 3 by size, launch vendor uninstaller); simplify `wc_view_disk` to top folders + largest files (drop tabs, duplicates, stale); add Teams rule behind the verification gate; confirm dev caches are inside System junk. | Uninstalling a 40 GB game is reachable in 2 clicks from home; breakdown is read-only and loads without hashing. |
| **5. Sweep** | Delete engines/platforms listed in §6, their tests and strings, macOS, `--scheduled-clean` path, README/PLAN rewrite, version 0.2.0, changelog with the removals named. | `wc_engine`/`wc_platform` contain no unreachable modules; test suite covers only shipped behaviour; docs describe only what exists. |

Estimated: **5–6 days** total; step 1 alone is ~3 hours.

## 8. Step 1 diff plan (the immediate work)

**`src/ui/wc_product.h`**
- `wc_view_id`: `{ WC_VIEW_HEALTH, WC_VIEW_CLEANER, WC_VIEW_APPS, WC_VIEW_DISK }` — drop STARTUP, UPDATER, EXCLUSIONS, AUTOMATION, DEEP.
- Remove from `wc_cleaner_state`: `startup_*` (`wc_startup_item`, list job, search), `app_job`? (keep — Apps stays), `leftover_*` (Apps view still compiles; keep for step 1, removed in step 5), `updater_*`, `sched_*`, `tray_enabled`, `excl_*`, `cookies*`, `cookie_*`, `deep_*`.
- Remove includes for `wc_cookies.h`, `wc_cookie_job.h`, `wc_deep.h`, `wc_updater.h`.
- Remove declarations of the 5 removed `wc_view_*_draw` functions.

**`src/ui/wc_product.c`**
- Lines 445–453: delete 6 command registrations (deep, startup, updater, exclusions, automation, plus renumber `Ctrl 1–4`); keep about/history/analyze/preview. Result: ≤ 7 commands, not 13. Also delete every `Ctrl …` keybinding so the palette cannot advertise a destination that no longer exists.
- Lines 665–775: nav block → one section with 4 items; delete the `Ctrl K Commands` hint and the duplicate admin button (keep one `Restart as Admin`).
- Lines 745–773: switch → 4 cases.
- Lines 777–807 (About): shrink to version, licence, data folder. The shortcut sheet goes with the keybindings.
- `deep_now`, `cookie_now`, `updater_running`, `sched_*` poll/status blocks: delete.
- Destructor/teardown: delete free/close calls for removed state.
- **i18n pruning deferred to step 5.** Hand-deleting ~150 pairs risks removing a pair that a surviving screen still uses, which would silently downgrade Vietnamese to English. Step 5 will prune mechanically: every pair must be referenced by a string literal in the surviving sources.

**`CMakeLists.txt`**
- Lines 92–100: drop `wc_view_startup.c`, `wc_view_updater.c`, `wc_view_exclusions.c`, `wc_view_schedule.c`, `wc_view_deep.c`.
- Leave engine/platform sources intact this step (static-lib objects for unused modules are not linked into the exe; tests keep needing them).
- Add nothing yet — no new screen until step 2.

**Not touched in step 1:** `src/engine/*`, `src/platform/*`, `tests/*`, `main.c`. This is deliberate: if anything breaks in step 1, it is a UI wiring error, not a behaviour change.

## 9. Tests

| Suite | Fate |
|---|---|
| `test_phase1` (safety, rules, worker) | **Stays**, extended with rule-grouping assertions |
| `test_phase2` (startup codec, disk) | Keep disk parts; startup codec test deleted in step 5 |
| `test_phase3`, `phase5` (exclusions, history, paths) | Prune to history/paths |
| `test_phase4` (cookies, updater) | Deleted in step 5 |
| `wave1` (quarantine) | Deleted in step 5 |
| `wave2` (deep) | Kept while the `WADA_CLEAN_DEEP_TOOLS` option exists, deleted in v0.3 |
| `wave3` (leftovers/dups/stale) | Deleted in step 5 |
| **New** | Home-flow test: group roll-up totals, Recycle Bin excluded by default, admin-skip accounting, empty-state ("nothing to clean") |

Rule: **no test is deleted before its engine is deleted**, and CI stays green at every step.

## 10. Acceptance demo (the only definition of done)

Cold launch on a laptop with < 10 GB free:

1. First number on screen in ≤ 8 s.
2. Exactly one primary action.
3. Zero checkboxes.
4. Zero admin prompts unless the user asks for admin-only locations.
5. Either ≥ 1 GB freed or a plain "nothing to clean".
6. A non-technical person describes what happened correctly after one use, unprompted.

Any failure = not done. No "polish it a bit".

## 11. Non-goals this cycle

- No new screens, settings, or preferences.
- No startup/updater/automation return. If positioning changes later, they come back as separate products, not tabs.
- No design-system work, no icon redesign, no theme feature.
- No telemetry, ads, upsells, accounts, or cloud.

## 12. Risks and rollback

| Risk | Mitigation |
|---|---|
| Existing users lose features | Tag v0.1.0 stays; changelog names every removal; README says plainly what the app now does and does not do |
| Merge regressions in step 2 | Worker/engine untouched; home reuses the same calls; step 2 is revertable independently |
| Admin-skip confuses users | One inline sentence with a working link, not a badge or a second CTA |
| Teams paths wrong | Rule is gated on verification; a wrong path fails closed (target skipped), never deletes elsewhere |
| macOS users | Out of scope by decision; git history and tags preserve the code |
| Deleting too much in step 5 | Step 5 is a separate PR after a release candidate has been demoed |

## 13. Measurement without telemetry

- In-app history is the only instrument: freed GB per run, per group.
- Maintainer-side: download counts, issue reports, and the acceptance demo.
- Explicitly no analytics, no pings, no crash upload.

## 14. Open questions

1. **Teams cache** — need one person with Teams installed to confirm the two cache paths before step 4 ships.
2. **Vietnamese copy** — new home strings need a native review; the existing VI table is good and should be matched in tone (short, calm, no "tối ưu").
3. **Window size** — 1120×740 was sized for 9 destinations. Home likely reads better at ~980×700; decide during step 2 with a real render.
4. **Version** — propose 0.2.0 (behaviour-breaking removal), not 0.1.1.

---

## 15. Step log

### Step 1 — done (2026-11-26)

**Shipped:** five screens removed from build, routing, commands and shell state — Deep Clean, Startup, Updater, Exclusions, Automation. Nav is now Health Check · Cleaner · Apps · Disk Analyzer. The About dialog lost its shortcut sheet; the command palette lost every keybinding and every vanished destination.

**Evidence**
- Build clean, `ctest` green: 661 checks across 8 suites, no test source edited.
- Binary, stripped, same flags: **4,159,364 → 3,209,728 bytes (−949,636, −22.8%)** — the removed surfaces were already being linked in.
- First-run render: `docs/ux/images/one-door-step1-health.png` (real CUI capture, `CUI_SCREENSHOT`, scratch data dir).
- `src/ui` is 4,263 → 2,857 lines.

**Deviations from the plan, recorded deliberately**
1. **Startup lost its health card in this step**, not in step 5. The startup screen owned `wc_startup_refresh()`, which the health view called; keeping the card would have kept the deleted file alive. The card was not about disk space, so it went with the screen. Startup platform code is still compiled and its tests still pass.
2. **i18n table not pruned** — deferred to step 5 for the mechanical safety reason above. Roughly 150 dead pairs remain in the binary.
3. **Engines and platform implementations untouched**, as planned. `wc_deep`, `wc_updater`, `wc_cookies`, `wc_cookie_job`, `wc_win_startup`, `wc_win_sched` etc. still compile and are still tested; nothing links them into the app.
4. **Exclusions state kept, UI removed** — persisted user rules keep being honoured by the cleanup worker until the engine retires.

**Known interim states (accepted, not bugs)**
- Health still runs cleanup targets; the merge into one home screen is step 2.
- The status bar still prints `dark 0.1.0`; step 3.
- `--scheduled-clean` still exists with no UI to schedule it; step 5 removes the path.
- The full-width "Last Clean" card looks sparse; the home redesign in step 2 removes the card grid entirely.

---

### Step 2 — done (2026-11-26)

**Shipped:** Health and Cleaner merged into one `Free up space` home. It scans on first paint — there is no `Analyze` button, no target checklist and no `Preview only` switch. Three rows (System junk, Browser junk, Recycle Bin) carry the whole decision; the 27 internal rules are grouped behind them. Nav is now **Free up space · Tools**. `Tools` is a door to Apps, Space and History; Apps and Disk gained a `Back to Tools` control. The sampled-file panel became the `Review` screen.

**Selections and defaults**
- Group membership is by rule category: System = SYSTEM + APPS + DEV caches, Browser = the four browser-cache rules, Recycle Bin = the one rule.
- Defaults: System on, Browser on, Recycle Bin off. Dev caches are now **on by default** inside System junk (plan §0 correction) — a deliberate behaviour change from v0.1, where they were opt-in.
- Targets flagged `quarantine` (the 30-day installers rule) are never auto-selected by a group switch. That rule is removed in step 5.
- Recycle Bin size and item count are queried on every scan, even while the group is off, so the row can state the consequence before it is enabled.

**Trust fix found during the capture pass (worth naming):** the first build of the home headline added up every selected target, including browser caches that the cleaner skips while the browser is open. It promised 1017.9 MB and could only deliver 462 MB. `selected_bytes()` now excludes any target that will be skipped, the browser row says `browser open — skipped`, and the Review totals carry the same note. The done state states how many items were skipped, and the hero copy claims only that nothing personal was touched.

**Evidence**
- Build clean, `ctest` green: 661 checks across 8 suites, still no test source edited.
- Renders: `one-door-step2-home.png`, `one-door-step2-tools.png`, `one-door-step2-review.png` (real CUI captures, scratch data dir).
- `src/ui`: 2,857 → **2,580 lines** — smaller than step 1 even with two brand-new screens. The two biggest single files (health 226, cleaner 814) became home 384 + tools 241, and the product shell shrank from 1,229 to 1,120.
- Stripped binary: 4,159,364 → **3,200,512 bytes (−23.1%)**.

**Deviations / interim states**
1. **Vietnamese copy for new strings is not written yet.** New labels fall back to English under `--lang vi`. Listed as open question 2; a native pass is needed before release.
2. **New strings are not yet in the i18n table** — same reason; step 5 prunes, then adds.
3. **Big-apps strip and simplified Space view are step 4.** `Space` still shows the four-tab analyzer including duplicates and stale dev artifacts.
4. **Status bar still prints `dark 0.1.0`** and `Analysis complete`; step 3.
5. **Group switches after a scan don't rescore the rows.** Sizes describe what was found, not the current selection; the headline does track selection. Acceptable now, worth a second look in step 3.
6. **Clean confirmation is still the two-click button.** Step 3 owns the final copy and the Recycle Bin consequence line.

---

### Step 3 — done (2026-11-26)

**Shipped**
- **Recycle Bin states its consequence.** The item count and size are re-read the moment the group is switched on, and again when the clean is armed (so a stale number can't be used to justify an irreversible action). Enabling it shows a danger alert naming count and size: *"18 items (412 MB) will be deleted permanently. This cannot be undone."*
- **One confirmation that restates everything.** The primary button turns red and reads `Confirm free up 462.2 MB`; an inline alert repeats the amount and what is permanent. The first click deletes nothing, any group toggle resets the confirmation, and the status bar says so.
- **One admin path.** The sidebar `Restart as Admin` button and the `Standard User` badge are gone. A single inline `Restart as administrator` appears on the System junk row only when admin-only locations were actually skipped. Sidebar subtitle is now `Free up disk space`.
- **Status bar states facts on the left**: `Checking for junk…`, then `Found 462.2 MB you can free · 1 location needs administrator · 1 cache skipped (in use)`, `Freed 462.2 MB across 2,451 files · 3 skipped`, `Cancelled — nothing was deleted.` It follows group toggles instead of waiting for the next scan.
- **Dry-run is visibly different**: `Previewing…`, `Would free 462.2 MB`, `Preview only. Nothing was deleted.`
- **One arithmetic source.** `wc_home_selected_bytes()` feeds both the hero and the status bar, so the two can no longer disagree.

**Bugs the capture pass found, fixed before commit**
1. Two contradictory numbers: the status bar reported all bytes *found* (1018.3 MB) while the hero promised bytes *freeable* (462.2 MB).
2. Grammar: "1 locations need administrator", "1 caches skipped".

**Vendor limitation, recorded rather than faked**
The right side of the status bar (`dark  0.1.0`) is drawn by the shell at `vendor/wada-c/src/shell/wada_app.c:438-442`; there is no product API for it. Removing it needs an upstream `wada-c` change, which is a submodule bump, not a local edit. Step 5 decides: upstream the option, or stop advertising a theme toggle so `dark` stops being meaningful. The version string is corrected by the 0.2.0 bump regardless.

**Evidence**
- `ctest` green: 662 checks across 8 suites (phase2's count varies 121/122 with the number of startup entries on this machine — it is an environment count, not a failure). No test source edited.
- Renders: `one-door-step3-home.png`, `one-door-step3-confirm.png`, `one-door-step3-recycle.png`.
- The confirm state was captured with a temporary forced flag and then reverted; the real click path runs the same code.

**Interim**
- Group switches still don't rescore the rows (a row's size is "what was found"; the headline and status track the selection).
- Version string and packaging are step 5.

---

### Step 4 — done (2026-11-26)

**Shipped**
- **Big apps on the home screen.** A card lists up to three installed apps of **512 MB or more** that expose an uninstall command, sorted by size, with the total (`3 apps use 3.34 GB`), a per-row `Uninstall` that launches the vendor uninstaller, and `Open Apps`. The list is enumerated on the existing worker thread, so the home screen never blocks; the card simply doesn't exist until there is something worth uninstalling. This is the feature that keeps the app from being "too simple": removing a 40 GB game is the honest way to get 40 GB.
- **Space view simplified.** The four tabs are gone. `Space` is now two live columns — *Largest folders* (with share-of-scan bars) and *Largest files* — plus the path bar, quick presets and totals. No duplicates, no stale-dev scanning, no quarantine buttons: it is read-only, and the subtitle says so. Title matches the Tools card.
- **Disk extras left the product layer.** `disk_extra_worker`, duplicate/stale snapshots and their poll/quarantine wiring are removed from the app; the engine code stays compiled and tested until step 5.
- Disk scan completion now reports facts (`Scan complete — 5.4 MB across 40 files`) instead of `Disk analysis complete`.

**Teams cache — deliberately not shipped (gate not met)**
The plan gated this on confirming the cache paths against a real Teams install. Teams is not installed on this machine, and the two candidate locations differ in a way that matters:
- classic: `%APPDATA%\Microsoft\Teams\Cache`
- new: `%LOCALAPPDATA%\Packages\MSTeams_8wekyb3d8bbwe\LocalCache\Microsoft\MSTeams\EBWebView\Default\Cache`

The parent `...\MSTeams` directory holds the profile; WebView2 cache subfolders also vary (`Cache` vs `Code Cache`). Guessing the exact leaf risks deleting profile state instead of cache, and even a correct guess cannot be demonstrated here — the rule would resolve to nothing, and we would be shipping a feature with zero evidence. It ships the moment someone with Teams confirms the path; the rule is a three-line addition (env var + subpath) plus a group membership that already exists.

**Evidence**
- `ctest` green: 662 checks, 8 suites, no test source edited.
- Renders: `one-door-step4-home.png`, `one-door-step4-space.png`.
- UI 2,580 → **2,496 lines**; stripped binary **3,197,952 bytes** (76.9% of v0.1.0).

**Interim / known**
- On a machine with three big apps the home card extends past the first fold; the column scrolls, but the first screen now ends mid-card. Worth a spacing pass in step 5.
- Apps view still carries the *Residual Leftovers* section; it leaves with the engine in step 5.
- Duplicate/stale engines remain compiled and tested though nothing calls them; step 5 deletes them with their tests.

---

### Step 5 — done (2026-11-26)

**Shipped**
- **Deleted the retired engines and backends:** `wc_deep`, `wc_updater`, `wc_cookies`, `wc_cookie_job`, `wc_leftovers`, `wc_quarantine`, `wc_sha256`, `wc_exclusions`; Windows startup/updater/scheduler/deep/cookie backends; the whole `src/platform/macos` tree. `wc_platform.h` and `wc_types.h` now declare only what exists.
- **Quarantine removed end to end** — engine, worker callback, product state, UI, safety helpers, type fields, and the `wc_target_def.quarantine` column. The _Old installers in Downloads_ rule went with it; rules are 26.
- **Tests: 8 suites / 662 checks → 3 suites / 307 checks.** Every suite died with its engine; no test was deleted ahead of its subject. `phase1` (safety, rules, worker, history), `phase2` (apps, disk) and `wave1` (safety, browser profile discovery) remain.
- **i18n table: 474 → 105 pairs.** Pruned mechanically against every string literal in `src/`, with continuation-aware handling for wrapped entries.
- **Docs rewritten to match the product:** `README.md`, `PLAN.md`, new `CHANGELOG.md`; the v0.1 UX review carries a "superseded" banner instead of dead links passing as current.
- **Version 0.2.0** in the app and in `package.ps1`.

**Numbers**
| | v0.1.0 | v0.2.0 |
|---|---|---|
| `src/` lines | 15,189 | **4,766** (−69%) |
| `src/ui` lines | 4,263 | **1,906** |
| Test suites / checks | 8 / 662 | **3 / 307** |
| i18n pairs | 474 | **105** |
| Stripped exe | 4,159,364 B | **3,119,616 B** (−25%) |

The exe fell less than the source because SQLite, the shell and the immediate-mode UI toolkit dominate the binary; the part we control is 69% smaller.

**Two mistakes made and fixed in this step (kept here on purpose)**
1. `wc_win_dialog.c` looked like tray code, so it was deleted with the automation backend. It was actually the native folder picker; the link failed and it came back as `wc_win_folder.c`. Verify before deleting.
2. The first i18n prune was self-referential (the table's own lines counted as references), then a second attempt orphaned the translations of multi-line entries. The build caught both; the final script strips the table before scanning and consumes wrapped entries as blocks.

**Known limitations**
- New screens are English-only; `--lang vi` falls back to English there.
- The status bar's right side (`dark 0.1.0`) is shell-owned and shows the **wada-c framework** version, not Wada Clean's. About shows 0.2.0. Needs an upstream change.
- `dist/` still contains v0.1.0 packaging; `package.ps1` regenerates it as v0.2.0.

---

*v0.2.0 is complete. Remaining release work: run `package.ps1 -BuildType Release`, spot-check the ZIP on a clean profile, and commission the Tiếng Việt pass for the new screens.*
