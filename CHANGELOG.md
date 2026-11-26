# Changelog

## 0.2.0 — "One Door"

Wada Clean now does one thing: it frees disk space on Windows. This release
removes far more than it adds, on purpose. The full accounting (what left, why,
and the evidence) is in [docs/PLAN-ONE-DOOR.md](docs/PLAN-ONE-DOOR.md).

### Added

- **One home screen** that scans automatically on launch, reports how much can be
  freed, and cleans with one button.
- **Three decisions** instead of a 27-row checklist: System junk, Browser junk,
  Recycle Bin (off by default, counted, irreversible).
- **Review** screen: group totals plus the largest sampled files.
- **Big apps**: up to three installed apps of 512 MB+ with one-click uninstall.
- **Honest arithmetic**: the amount promised excludes anything the cleaner will
  skip (open browsers, in-use files, non-elevated locations).
- Status bar reports scan results with counts and administrator/skip notes.

### Changed

- Navigation is **Free up space · Tools** (was nine destinations).
- Space view is read-only with two columns (largest folders, largest files).
- Developer caches are included quietly inside System junk.
- Recycle Bin shows item count and size, re-read when enabled and when cleaning.

### Removed

- **Deep Clean**: DISM/WinSxS, driver removal, orphaned MSI quarantine,
  Delivery Optimization, CompactOS, hibernation, VSS resize, VHDX compaction.
- **Startup manager**, **software updater (winget)**, **automation/system tray**
  (`--scheduled-clean` no longer exists; existing scheduled tasks will fail).
- **Exclusions** and the **cookie allowlist** (and their SQLite stores).
- **Residual leftovers**, **duplicate finder**, **stale dev artifact scanner**.
- **Quarantine vault** — junk deletion was never reversible; the app no longer
  pretends otherwise.
- **macOS backend** and the Apple build path.
- **Old installers in Downloads** rule (user data, not junk).

### Known limitations

- New screens are English-only; the Tiếng Việt table covers the strings inherited
  from v0.1.
- The status bar's right-hand label is drawn by the `wada-c` shell and shows the
  *framework* version (0.1.0), not Wada Clean's. The About dialog shows 0.2.0.
  Hiding the shell label needs an upstream change.
