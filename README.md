# wada-clean

**Free up disk space on Windows in one click.** A tiny native utility (C99, no
telemetry, no ads, no upsells) that answers one question: *how many gigabytes can
I get back, and how do I get them now?*

## What it does

- **Free up space.** One screen, one automatic scan, one button. Three groups:
  *System junk* (temp files, caches, Windows Update downloads, developer caches),
  *Browser junk* (Chrome, Edge, Brave, Firefox caches) and *Recycle Bin*
  (off by default, counted, and marked irreversible). `Review` shows the largest
  files that will go; after cleaning the app reports exactly what was freed.
- **Big apps.** Lists up to three installed apps of 512 MB or more with a working
  uninstall command, so a 40 GB game is two clicks away.
- **Space.** A read-only breakdown of any folder: largest subfolders and largest
  files. Nothing is deleted from this screen.
- **History.** Every clean run is recorded with per-target detail you can copy.

## What it deliberately does not do

No registry cleaner, driver updater, startup manager, software updater,
scheduled cleaning, cookie pruning or "optimizer" toggles. v0.2 removed them:
they were either dangerous for non-technical users (`DISM /ResetBase`, driver
removal), someone else's job (updates), or a different promise (boot speed).
The full rationale and the deletion ledger are in
[docs/PLAN-ONE-DOOR.md](docs/PLAN-ONE-DOOR.md).

## Safety

- Only caches and temporary files are targeted by rules; documents, photos,
  downloads and profile data are never touched.
- The Recycle Bin is the one irreversible action, and it is off by default.
- Files held open by a running browser are skipped and reported, never forced.
- Locations that need administrator access are skipped unless the app is
  elevated; there is one inline "Restart as administrator" action when relevant.
- No telemetry, no network access, no accounts.

## Build

Windows only (MinGW-w64 via `w64devkit`):

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

`build.ps1` finds `w64devkit` in `.tools\w64devkit\w64devkit\bin`,
`..\c-desktop\.tools\w64devkit\w64devkit\bin`, `..\wada-c\.tools\w64devkit\w64devkit\bin`,
or `$env:WADA_W64DEVKIT`. Pass `-LocalWada` to build against a sibling `..\wada-c`
checkout.

Package a stripped portable ZIP (`dist\wada-clean-v0.2.0-win64.zip` + `.sha256`):

```powershell
powershell -ExecutionPolicy Bypass -File .\package.ps1 -BuildType Release
```

## CLI

```text
wada_clean.exe --dry-run   # Scan and preview without deleting anything
wada_clean.exe --lang vi   # Tiếng Việt (newer screens fall back to English)
wada_clean.exe --doctor    # Storage and translation diagnostics
```

## Localisation

English and Tiếng Việt. The strings from v0.1 are translated; the new home,
Tools, big-apps and space screens are English-only until a native review lands
(tracked in [docs/PLAN-ONE-DOOR.md](docs/PLAN-ONE-DOOR.md), open questions).

## License

[MIT](LICENSE)
