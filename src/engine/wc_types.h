#ifndef WC_TYPES_H
#define WC_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define WC_MAX_TARGETS 32
#define WC_MAX_EXCLUSIONS 128
#define WC_MAX_STARTUP 256
#define WC_MAX_APPS 512
#define WC_MAX_UPDATES 128
#define WC_MAX_COOKIE_ALLOW 256
#define WC_DISK_TOP_DIRS 25
#define WC_DISK_TOP_FILES 25
#define WC_PATH_CAP 1024
#define WC_ID_CAP 64
#define WC_LABEL_CAP 96
#define WC_SUMMARY_CAP 256

typedef enum wc_category {
    WC_CAT_SYSTEM = 0,
    WC_CAT_BROWSER = 1
} wc_category;

typedef enum wc_target_kind {
    WC_TARGET_DIR_CONTENTS = 0,
    WC_TARGET_RECYCLE_BIN = 1,
    WC_TARGET_THUMB_CACHE = 2,
    WC_TARGET_FIREFOX_PROFILES = 3
} wc_target_kind;

typedef enum wc_job_kind {
    WC_JOB_NONE = 0,
    WC_JOB_ANALYZE = 1,
    WC_JOB_CLEAN = 2
} wc_job_kind;

typedef enum wc_job_state {
    WC_JOB_IDLE = 0,
    WC_JOB_RUNNING = 1,
    WC_JOB_DONE = 2,
    WC_JOB_CANCELLED = 3,
    WC_JOB_ERROR = 4
} wc_job_state;

typedef struct wc_target_def {
    char id[WC_ID_CAP];
    char label[WC_LABEL_CAP];
    wc_category category;
    wc_target_kind kind;
    char env_var[32];      /* e.g. "TEMP", "LOCALAPPDATA", or "" for literal subpath */
    char subpath[WC_PATH_CAP];
    char process_name[64]; /* e.g. "chrome.exe", or "" */
    bool default_enabled;
    bool needs_admin;
} wc_target_def;

typedef struct wc_target_result {
    char id[WC_ID_CAP];
    char resolved_path[WC_PATH_CAP];
    bool selected;
    bool analyzed;
    bool cleaned;
    bool browser_running;
    bool admin_blocked;
    uint64_t files_found;
    uint64_t bytes_found;
    uint64_t files_cleaned;
    uint64_t bytes_cleaned;
    uint64_t skipped_count;
    char note[128];
} wc_target_result;

typedef struct wc_history_entry {
    char id[48];
    long long unix_sec;
    bool dry_run;
    uint64_t files_cleaned;
    uint64_t bytes_cleaned;
    uint64_t skipped_count;
    char summary[WC_SUMMARY_CAP];
} wc_history_entry;

typedef struct wc_worker_snapshot {
    wc_job_kind kind;
    wc_job_state state;
    bool dry_run;
    int total_selected;
    int completed_targets;
    int current_index;
    char current_label[WC_LABEL_CAP];
    int target_count;
    wc_target_result results[WC_MAX_TARGETS];
    uint64_t total_files_found;
    uint64_t total_bytes_found;
    uint64_t total_files_cleaned;
    uint64_t total_bytes_cleaned;
    uint64_t total_skipped;
    char error_msg[192];
} wc_worker_snapshot;

typedef enum wc_startup_source {
    WC_STARTUP_HKCU_RUN = 0,
    WC_STARTUP_HKLM_RUN = 1,
    WC_STARTUP_USER_FOLDER = 2,
    WC_STARTUP_COMMON_FOLDER = 3
} wc_startup_source;

typedef struct wc_startup_item {
    char name[128];
    char command[WC_PATH_CAP];
    wc_startup_source source;
    bool enabled;
    bool needs_admin;
} wc_startup_item;

typedef struct wc_app_item {
    char name[192];
    char publisher[128];
    char version[64];
    char install_date[16];
    uint64_t estimated_size_bytes;
    char uninstall_cmd[WC_PATH_CAP];
    bool is_user_install;
} wc_app_item;

typedef struct wc_disk_entry {
    char path[WC_PATH_CAP];
    char name[256];
    uint64_t bytes;
    uint64_t file_count;
} wc_disk_entry;

typedef struct wc_disk_snapshot {
    wc_job_state state;
    char root_path[WC_PATH_CAP];
    uint64_t total_bytes;
    uint64_t total_files;
    uint64_t total_dirs;
    uint64_t skipped_count;
    int top_dir_count;
    wc_disk_entry top_dirs[WC_DISK_TOP_DIRS];
    int top_file_count;
    wc_disk_entry top_files[WC_DISK_TOP_FILES];
    char current_dir[WC_PATH_CAP];
    char error_msg[192];
} wc_disk_snapshot;

typedef struct wc_exclusion_item {
    char id[48];
    char path[WC_PATH_CAP];
} wc_exclusion_item;

typedef struct wc_update_item {
    char name[160];
    char id[160];
    char current_version[64];
    char available_version[64];
    char source[32];
} wc_update_item;

typedef enum wc_updater_op {
    WC_UPD_NONE = 0,
    WC_UPD_CHECK = 1,
    WC_UPD_UPGRADE_ONE = 2,
    WC_UPD_UPGRADE_ALL = 3
} wc_updater_op;

typedef struct wc_updater_snapshot {
    wc_updater_op op;
    wc_job_state state;
    bool winget_available;
    int count;
    wc_update_item items[WC_MAX_UPDATES];
    char active_id[160];
    char output_summary[256];
    char error_msg[192];
} wc_updater_snapshot;

typedef struct wc_cookie_allow_item {
    char id[48];
    char domain[192];
} wc_cookie_allow_item;

typedef enum wc_schedule_freq {
    WC_SCHED_OFF = 0,
    WC_SCHED_DAILY = 1,
    WC_SCHED_WEEKLY = 2
} wc_schedule_freq;

typedef struct wc_schedule_info {
    wc_schedule_freq freq;
    int hour;
    int minute;
    bool active;
    char next_run[64];
} wc_schedule_info;

#endif /* WC_TYPES_H */
