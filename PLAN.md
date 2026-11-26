# wada-clean — project plan

**Current direction: v0.2 "One Door".** The app promises disk space and nothing
else: one home screen, one automatic scan, one button, plus a single Tools door.

- Full rationale, screen map, deletion ledger and step log:
  [docs/PLAN-ONE-DOOR.md](docs/PLAN-ONE-DOOR.md)
- Change history for v0.2.0: [CHANGELOG.md](CHANGELOG.md)
- Architecture notes below describe what actually exists.

## Architecture

```text
wada_clean.exe
├── src/main.c                      boot, --dry-run, --doctor, --lang
├── src/ui/
│   ├── wc_product.c                shell: nav, commands, worker polling, history
│   ├── wc_view_home.c              Free up space: scan, groups, review, big apps
│   ├── wc_view_tools.c             Tools hub and run history
│   ├── wc_view_apps.c              installed apps and uninstallers
│   └── wc_view_disk.c              read-only space breakdown
├── src/engine/                     pure C99, no OS headers
│   ├── wc_rules.c                  26 cache rules grouped into three decisions
│   ├── wc_worker.c                 background scan/clean, preview sampling
│   ├── wc_disk.c                   background folder-size analyzer
│   ├── wc_history.c                SQLite run history
│   ├── wc_safety.c                 path normalization and target-root guards
│   └── wc_list_job.c               background enumeration for the apps list
└── src/platform/win32/             filesystem walk, processes, uninstallers,
                                    folder picker
```

The engine has no OS includes; everything platform-specific lives behind
`wc_platform.h`. UI work never blocks: scans, cleans, app enumeration and disk
analysis all run on worker threads and publish frame-boundary snapshots.

## Non-goals (kept out on purpose)

Registry cleaning, driver updates, startup management, software updates,
scheduled cleaning, cookie/tracking cleanup, cloud-drive cleanup, macOS support.
Each was removed in v0.2 with the reasoning recorded in the One Door plan.
