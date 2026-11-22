# wada-clean

Open-source, MIT-licensed native PC cleaner and optimizer built in C99 on top of [`wada-c`](https://github.com/xleotranx/wada-c).

Tiny native binary, zero telemetry, zero ads or upsells, with English and Tiếng Việt localization (`wada_tr`).

## Features

- **PC Health Check:** One-click summary of system drive free space, reclaimable junk, startup impact, and past clean history.
- **Junk Cleaner (Analyze → Preview → Clean):** Safely scans and cleans temp files, Recycle Bin / Trash (opt-in), thumbnail caches, crash dumps, and browser caches (Chrome, Edge, Brave, Firefox, Safari) with a sampled per-file preview, two-click confirmation, and `--dry-run`.
- **Per-target history:** Each run records files, bytes, skips, and reasons per target; open a run from Recent History or `Ctrl+H` and copy the details.
- **Startup Manager:** Search and toggle startup items reversibly via Windows `StartupApproved` or macOS `LaunchAgents` without deleting original entries.
- **Installed Apps / Uninstaller:** Browse installed applications, sort by name or size, and launch official uninstallers (or reveal `.app` bundles on macOS).
- **Disk Space Analyzer:** Read-only scanner showing top folders and largest files while skipping symlinks and junctions.
- **Software Updater:** Checks and upgrades outdated packages via `winget` (Windows) or `brew` (macOS).
- **Exclusions & Cookie Allowlist:** Protect specific files/folders from cleaning and keep trusted login cookies while clearing tracking cookies in Chromium/Firefox SQLite databases.
- **Automation & System Tray:** Schedule silent `--scheduled-clean` runs via Task Scheduler / `launchctl`, plus optional in-app tray notifications while Wada Clean is open (scheduled runs do not notify).

## Build & Run

Clone with submodules:

```powershell
git clone --recurse-submodules https://github.com/xleotranx/wada-clean.git
cd wada-clean
```

### Windows (`w64devkit`)

`build.ps1` finds `w64devkit` in `.tools\w64devkit\w64devkit\bin`, `..\c-desktop\.tools\w64devkit\w64devkit\bin`, `..\wada-c\.tools\w64devkit\w64devkit\bin`, or `$env:WADA_W64DEVKIT`:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

If iterating locally against a sibling `..\wada-c` checkout:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -LocalWada
```

Package a stripped portable Windows ZIP (`dist\wada-clean-v0.1.0-win64.zip` + `.sha256`):

```powershell
powershell -ExecutionPolicy Bypass -File .\package.ps1 -LocalWada
```

### macOS (Xcode CLT + CMake + Ninja)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/wada_clean
```

### CLI Options

```text
wada_clean.exe --dry-run            # Launch in preview-only mode
wada_clean.exe --lang vi            # Launch in Tiếng Việt (or en)
wada_clean.exe --scheduled-clean    # Run non-admin cleanup headlessly and log to SQLite
wada_clean.exe --doctor             # Run storage & translation diagnostics
```

## License

[MIT](LICENSE)
