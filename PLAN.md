# wada-clean — Project Plan

## 1. Overview & Goals

**wada-clean** is an open-source, MIT-licensed, free-to-distribute native PC cleaner and optimizer built in **C99** on top of [`wada-c`](https://github.com/xleotranx/wada-c) (CUI + SQLite).

- **Platform strategy:** Windows-first (Win32 APIs), macOS-ready (`src/platform/macos/` in Phase 5, leveraging `wada_shell`'s existing Cocoa/QuartzCore/OpenGL support).
- **Core values:** Tiny native footprint, fast direct filesystem/registry scanning, zero telemetry, zero ads or upsells, and safety-first cleaning.
- **Built-in Wada foundations:** Event loop, product/scope lifecycle, SQLite storage (`wada_store_ops`), settings persistence, EN/VI localization (`wada_tr`), and non-blocking frame-boundary UI updates.

---

## 2. Repository & Build Strategy (Git Submodule)

`wada-clean` lives in its own standalone repository (`c:\Users\leotr\project\wada-clean`) with `wada-c` consumed as a Git submodule:

- **Submodule:** `vendor/wada-c` → `https://github.com/xleotranx/wada-c.git` (pinned to a known commit for reproducible builds).
- **Local iteration override:** `CMakeLists.txt` defaults to `vendor/wada-c` and supports `-DWADA_C_DIR=../wada-c` so local changes to `wada-c` can be tested without pushing first.
- **Prerequisite in `wada-c/CMakeLists.txt`:** Add `WADA_BUILD_APPS` and `WADA_BUILD_TESTS` CMake options (defaulting to `ON` when `wada-c` is the top-level project, and `OFF` when included via `add_subdirectory`) so `wada-clean` builds only `wada_core`, `wada_sqlite`, and `wada_shell`.
- **Submodule helper:** `build.ps1` verifies `vendor/wada-c` is populated (running `git submodule update --init` if needed) before configuring/building.
- **License:** MIT (`LICENSE`).

---

## 3. Architecture & Directory Layout

```text
wada-clean/
├── CMakeLists.txt
├── build.ps1
├── LICENSE                        # MIT
├── PLAN.md                        # This plan
├── README.md
├── vendor/
│   └── wada-c/                    # Git submodule -> xleotranx/wada-c
├── src/
│   ├── main.c                     # wada_boot_run + product registration
│   ├── engine/                    # Pure C99, zero OS headers (100% unit-testable)
│   │   ├── wc_types.h             # Targets, scan items, findings, job state
│   │   ├── wc_rules.c             # Built-in cleaner rules (OS + browsers)
│   │   ├── wc_safety.c            # Path boundary & reparse-point guards
│   │   ├── wc_cookies.c           # Chromium/Firefox SQLite cookie domain filter
│   │   ├── wc_history.c           # Scan/clean audit log & exclusions (wada_store_ops)
│   │   └── wc_worker.c            # Background job runner (never blocks UI thread)
│   ├── platform/                  # Clean OS abstraction boundary
│   │   ├── wc_platform.h          # Platform API (fs walk, trash, startup, apps, updater)
│   │   ├── win32/                 # Phases 1–4 implementation
│   │   │   ├── wc_win_fs.c        # FindFirstFileExW, Recycle Bin, symlink/junction skip
│   │   │   ├── wc_win_proc.c      # Running browser detection, UAC elevation check/relaunch
│   │   │   ├── wc_win_startup.c   # HKCU/HKLM Run + StartupApproved + Startup folder + Tasks
│   │   │   ├── wc_win_apps.c      # Uninstall registry enumeration + uninstaller launch
│   │   │   ├── wc_win_updater.c   # winget wrapper (list outdated + upgrade)
│   │   │   └── wc_win_sched.c     # Scheduled Task registration + system tray (Tier 2)
│   │   └── macos/                 # Phase 5: ~/Library/Caches, Trash, LaunchAgents, /Applications, brew
│   └── ui/                        # Wada product views over CUI
│       ├── wc_product.c           # Shell integration, navigation, commands, EN/VI i18n
│       ├── wc_view_health.c       # Health Check dashboard
│       ├── wc_view_cleaner.c      # Junk & Browser cleaner (Analyze -> Preview -> Clean)
│       ├── wc_view_startup.c      # Startup manager
│       ├── wc_view_apps.c         # Installed apps / uninstaller
│       ├── wc_view_disk.c         # Disk space analyzer / large files
│       └── wc_view_updater.c      # Software updater (winget)
└── tests/                         # Fixture-based tests against temp directories only
```

---

## 4. Feature Scope (Tier 1 + Tier 2)

### Tier 1 — Core Cleaner & Utilities
1. **Junk & Temp Cleaner (Analyze → Preview → Clean):**
   - **Windows targets:** `%TEMP%`, `C:\Windows\Temp`, Recycle Bin (`SHQueryRecycleBin` / `SHEmptyRecycleBin`), Prefetch, Thumbnail cache, Crash dumps, Windows Update download cache.
   - **Browser targets:** Cache, history, cookies, and session files for Chrome, Edge, Brave, and Firefox (with running-browser detection and user warning).
2. **Startup Manager:**
   - Enumerate and toggle entries in `HKCU\...\Run`, `HKLM\...\Run`, Startup folders, and optionally Windows Scheduled Tasks.
3. **Installed Apps / Uninstaller:**
   - Enumerate Windows `Uninstall` registry keys, display install metadata/sizes/dates, and launch native uninstallers.
4. **Disk Space Analyzer / Large File Finder:**
   - Fast multi-threaded `FindFirstFileExW` traversal showing top space-consuming folders and largest files.
5. **PC Health Check (Dashboard):**
   - One-click overview summarizing reclaimable junk size, enabled startup count, disk free space, and past cleaning history.

### Tier 2 — Advanced Maintenance
6. **Software Updater:**
   - Wrap Windows' built-in `winget` CLI to detect outdated installed applications and trigger upgrades safely without maintaining a custom software catalog.
7. **Browser Cookie Allowlist:**
   - Query and selectively purge Chromium (`Cookies` SQLite DB) and Firefox (`cookies.sqlite`) rows by host domain while preserving logins for user-allowlisted domains.
8. **Scheduled Cleaning & System Tray Mode:**
   - Periodic reminders or background scheduled cleanup registered via Windows Task Scheduler, plus optional system tray integration.

### Explicit Non-Goals for v1
To protect system stability and keep scope realistic for an independent open-source release, the following features are intentionally **excluded** from v1:
- **Registry Cleaner:** Minimal real-world benefit on Windows 10/11 and high risk of breaking installed applications.
- **Driver Updater:** Requires a large curated driver database, server infrastructure, and carries BSOD risk; Windows Update and OEM utilities are safer.
- **Performance Optimizer ("App Sleep Mode"):** Intercepting process launches and dynamically toggling Windows services/tasks is fragile and invasive.
- **Cloud Drive Cleaner:** Requires OAuth flows and third-party cloud APIs (Google Drive, OneDrive, Dropbox).

---

## 5. Safety & Trust Principles

1. **Strict Analyze → Preview → Clean Separation:** Never delete files without first presenting exact categories, file counts, and byte totals. Include a `--dry-run` mode for CLI verification and tests.
2. **Hardened Path & Reparse-Point Guards (`wc_safety.c`):**
   - Always validate resolved paths stay within declared target roots.
   - Skip `FILE_ATTRIBUTE_REPARSE_POINT` (symlinks and directory junctions) during both scanning and deletion so a junction inside `%TEMP%` cannot escape to user data.
   - Rule-driven target definitions only; never perform blind recursive deletes on arbitrary user-supplied paths.
3. **Graceful Locked-File & Browser Handling:** Skip in-use/locked files without aborting the run, report them clearly as *"Skipped (in use)"*, and detect running browsers before touching browser profiles.
4. **Non-Destructive Startup Toggling:** Use Windows' native `StartupApproved` mechanism (matching Task Manager) so disabling/re-enabling startup entries is completely reversible without losing original command lines.
5. **Least Privilege by Default:** Run as a standard user (covering `%TEMP%`, browser profiles, Recycle Bin, `HKCU` startup). Clearly badge admin-required targets (`C:\Windows\Temp`, `HKLM\Run`, Windows Update cache) and offer an explicit **"Restart as Administrator"** action instead of forcing UAC on launch.
6. **Transparent Audit Log & Exclusions:** Persist scan/clean batch summaries (timestamp, categories, bytes freed, skipped counts) and user path/cookie exclusions in SQLite via `wada_store_ops`.
7. **Non-Blocking UI (Wada Rule 5):** Run all disk, browser, registry, and `winget` work on background worker threads (`wc_worker.c`), supporting cancellation and publishing progress snapshots back to the UI only at frame boundaries.
8. **Isolated Fixture Testing:** Unit tests operate exclusively against controlled temporary fixture directories under `tests/`, never touching real user files or host registry hives.

---

## 6. Phased Implementation Roadmap

| Phase | Scope | Key Deliverables |
|---|---|---|
| **Phase 1 — Foundation & Cleaner** | `wada-c` CMake `WADA_BUILD_APPS` / `WADA_BUILD_TESTS` guard; `wada-clean` repo scaffold (`LICENSE`, `CMakeLists.txt`, `build.ps1`, `vendor/wada-c` submodule); `wc_platform.h` + Win32 FS & process detection; `wc_worker.c`; `wc_safety.c`; System & Browser Junk Cleaner (Analyze → Preview → Clean); SQLite audit log; fixture-based unit tests | Working `wada_clean.exe` that safely analyzes and cleans Windows & browser junk with passing unit tests |
| **Phase 2 — Core Tier 1 Utilities** | Reversible Startup Manager (`StartupApproved` + `Run` + Startup folder + Tasks); Installed Apps / Uninstaller view; Multi-threaded Disk Space Analyzer & Large File Finder | Complete Tier 1 utility suite |
| **Phase 3 — Health Check & Polish** | One-click PC Health Check dashboard; Exclusions management UI; full EN/VI `wada_tr` localization; optional UAC "Restart as Administrator" relaunch | Cohesive v0.1 Windows release |
| **Phase 4 — Tier 2 Features** | `winget` Software Updater (`wc_win_updater.c` + UI); Browser Cookie Allowlist (`wc_cookies.c`); Scheduled Cleaning (`wc_win_sched.c`) & System Tray mode | Feature-complete v1.0 Windows release |
| **Phase 5 — macOS & Distribution** | `src/platform/macos/` backend (`~/Library/Caches`, Trash, `LaunchAgents`, `/Applications`, `brew`); GitHub Actions CI for Windows & macOS; portable ZIP + installer packaging | Cross-platform open-source release |
