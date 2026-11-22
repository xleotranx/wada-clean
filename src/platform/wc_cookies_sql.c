#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#include "wc_cookies.h"
#include "wc_platform.h"

bool wc_plat_prune_cookie_db(const char *db_path, bool is_firefox, const char *const *allowlist,
                             int allow_count, bool dry_run, uint64_t *out_removed,
                             uint64_t *out_kept) {
    if (out_removed) *out_removed = 0;
    if (out_kept) *out_kept = 0;
    if (!db_path || !db_path[0]) return false;

    sqlite3 *db = NULL;
    int flags = dry_run ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (sqlite3_open_v2(db_path, &db, flags, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 1000);

    const char *select_sql = is_firefox ? "SELECT rowid, host FROM moz_cookies"
                                        : "SELECT rowid, host_key FROM cookies";
    const char *del_sql = is_firefox ? "DELETE FROM moz_cookies WHERE rowid = ?1"
                                     : "DELETE FROM cookies WHERE rowid = ?1";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    sqlite3_int64 *del_ids = NULL;
    size_t del_count = 0;
    size_t del_cap = 0;
    uint64_t kept = 0;
    uint64_t removed = 0;

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        sqlite3_int64 rowid = sqlite3_column_int64(stmt, 0);
        const char *host = (const char *)sqlite3_column_text(stmt, 1);

        if (wc_cookie_domain_allowed(host, allowlist, allow_count)) {
            kept++;
        } else {
            removed++;
            if (!dry_run) {
                if (del_count == del_cap) {
                    size_t next = del_cap ? del_cap * 2 : 64;
                    sqlite3_int64 *p = (sqlite3_int64 *)realloc(del_ids, next * sizeof *p);
                    if (!p) {
                        free(del_ids);
                        sqlite3_finalize(stmt);
                        sqlite3_close(db);
                        return false;
                    }
                    del_ids = p;
                    del_cap = next;
                }
                del_ids[del_count++] = rowid;
            }
        }
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        free(del_ids);
        sqlite3_close(db);
        return false;
    }

    if (!dry_run && del_count > 0) {
        if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) {
            free(del_ids);
            sqlite3_close(db);
            return false;
        }

        sqlite3_stmt *del_stmt = NULL;
        if (sqlite3_prepare_v2(db, del_sql, -1, &del_stmt, NULL) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            free(del_ids);
            sqlite3_close(db);
            return false;
        }

        bool del_ok = true;
        for (size_t i = 0; i < del_count; i++) {
            sqlite3_reset(del_stmt);
            sqlite3_bind_int64(del_stmt, 1, del_ids[i]);
            if (sqlite3_step(del_stmt) != SQLITE_DONE) {
                del_ok = false;
                break;
            }
        }
        sqlite3_finalize(del_stmt);

        if (!del_ok || sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            free(del_ids);
            sqlite3_close(db);
            return false;
        }
    }

    free(del_ids);
    sqlite3_close(db);

    if (out_removed) *out_removed = removed;
    if (out_kept) *out_kept = kept;
    return true;
}
