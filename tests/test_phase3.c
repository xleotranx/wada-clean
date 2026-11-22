#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_store_sqlite.h"
#include "wc_exclusions.h"
#include "wc_platform.h"
#include "wc_types.h"
#include "wc_worker.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        g_checks++;                                                                             \
        if (!(cond)) {                                                                          \
            g_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                       \
    } while (0)

static bool to_wide(const char *s, wchar_t *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap) > 0;
}

static bool join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + 1 + nlen + 1 > cap) return false;
    memcpy(out, dir, dlen);
    out[dlen] = '\\';
    memcpy(out + dlen + 1, name, nlen + 1);
    return true;
}

static bool make_dir_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    return CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_bytes_utf8(const char *path, size_t nbytes) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    if (nbytes > 0) {
        char *buf = (char *)malloc(nbytes);
        if (!buf) {
            CloseHandle(h);
            return false;
        }
        memset(buf, 'C', nbytes);
        DWORD written = 0;
        ok = WriteFile(h, buf, (DWORD)nbytes, &written, NULL) && written == (DWORD)nbytes;
        free(buf);
    }
    CloseHandle(h);
    return ok;
}

static bool file_exists_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(w);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void delete_tree_for_cleanup(const char *dir) {
    wchar_t wpat[WC_PATH_CAP];
    char pat[WC_PATH_CAP];
    if (!join_path(pat, sizeof pat, dir, "*")) return;
    if (!to_wide(pat, wpat, WC_PATH_CAP)) return;

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            char name[WC_PATH_CAP];
            WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, name, sizeof name, NULL, NULL);
            char child[WC_PATH_CAP];
            if (!join_path(child, sizeof child, dir, name)) continue;
            wchar_t wchild[WC_PATH_CAP];
            if (!to_wide(child, wchild, WC_PATH_CAP)) continue;

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RemoveDirectoryW(wchild);
            } else if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree_for_cleanup(child);
                RemoveDirectoryW(wchild);
            } else {
                SetFileAttributesW(wchild, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(wchild);
            }
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    wchar_t wdir[WC_PATH_CAP];
    if (to_wide(dir, wdir, WC_PATH_CAP)) {
        RemoveDirectoryW(wdir);
    }
}

static void test_exclusion_matching(void) {
    const char *patterns[2] = {
        "C:\\Temp\\keep_dir",
        "C:\\Temp\\important.log",
    };
    CHECK(wc_exclusion_matches("C:\\Temp\\keep_dir", patterns, 2));
    CHECK(wc_exclusion_matches("c:/temp/KEEP_DIR/sub/file.txt", patterns, 2));
    CHECK(wc_exclusion_matches("C:\\Temp\\important.log", patterns, 2));
    CHECK(!wc_exclusion_matches("C:\\Temp\\keep_dir2\\file.txt", patterns, 2));
    CHECK(!wc_exclusion_matches("C:\\Temp\\other.tmp", patterns, 2));

    const size_t lens[2] = {strlen(patterns[0]), strlen(patterns[1])};
    CHECK(wc_exclusion_matches_norm("C:\\Temp\\keep_dir", patterns, lens, 2));
    CHECK(wc_exclusion_matches_norm("c:\\temp\\KEEP_DIR\\sub\\file.txt", patterns, lens, 2));
    CHECK(wc_exclusion_matches_norm("C:\\Temp\\important.log", patterns, NULL, 2));
    CHECK(!wc_exclusion_matches_norm("C:\\Temp\\keep_dir2\\file.txt", patterns, lens, 2));
    CHECK(!wc_exclusion_matches_norm("C:\\Temp\\other.tmp", patterns, lens, 2));
}

static void test_exclusions_store(const char *root) {
    char db_path[WC_PATH_CAP];
    CHECK(join_path(db_path, sizeof db_path, root, "excl_test.db"));

    wc_exclusions ex;
    wada_err err;
    wada_err_clear(&err);
    CHECK(wc_exclusions_open(&ex, wada_store_sqlite(), db_path, &err) == WADA_OK);

    CHECK(wc_exclusions_add(&ex, "C:/Temp/One/", &err) == WADA_OK);
    CHECK(wc_exclusions_add(&ex, "C:\\Temp\\Two\\file.txt", &err) == WADA_OK);
    CHECK(wc_exclusions_add(&ex, "c:\\temp\\one", &err) == WADA_ERR_EXISTS);
    CHECK(wc_exclusions_add(&ex, "C:\\", &err) == WADA_ERR_INVALID);

    wc_exclusion_item items[8];
    int count = 0;
    CHECK(wc_exclusions_list(&ex, items, 8, &count, &err) == WADA_OK);
    CHECK(count == 2);
    CHECK(strcmp(items[0].path, "C:\\Temp\\One") == 0);
    CHECK(strcmp(items[1].path, "C:\\Temp\\Two\\file.txt") == 0);

    CHECK(wc_exclusions_remove(&ex, items[0].id, &err) == WADA_OK);
    CHECK(wc_exclusions_list(&ex, items, 8, &count, &err) == WADA_OK);
    CHECK(count == 1);
    CHECK(strcmp(items[0].path, "C:\\Temp\\Two\\file.txt") == 0);

    wc_exclusions_close(&ex);
}

typedef struct excl_list_ctx {
    const char *ptrs[2];
    int count;
} excl_list_ctx;

static bool test_excl_cb(const char *path, void *user) {
    const excl_list_ctx *ctx = (const excl_list_ctx *)user;
    return wc_exclusion_matches(path, ctx->ptrs, ctx->count);
}

static void test_fs_clean_respects_exclusions(const char *root) {
    char target[WC_PATH_CAP], keep_dir[WC_PATH_CAP];
    char f_del[WC_PATH_CAP], f_keep1[WC_PATH_CAP], f_keep2[WC_PATH_CAP];

    CHECK(join_path(target, sizeof target, root, "excl_fs"));
    CHECK(join_path(keep_dir, sizeof keep_dir, target, "keep_dir"));
    CHECK(join_path(f_del, sizeof f_del, target, "delete_me.tmp"));
    CHECK(join_path(f_keep1, sizeof f_keep1, keep_dir, "keep.tmp"));
    CHECK(join_path(f_keep2, sizeof f_keep2, target, "keep_file.tmp"));

    CHECK(make_dir_utf8(target));
    CHECK(make_dir_utf8(keep_dir));
    CHECK(write_bytes_utf8(f_del, 100));
    CHECK(write_bytes_utf8(f_keep1, 200));
    CHECK(write_bytes_utf8(f_keep2, 50));

    excl_list_ctx ctx;
    ctx.ptrs[0] = keep_dir;
    ctx.ptrs[1] = f_keep2;
    ctx.count = 2;

    volatile int cancel = 0;
    uint64_t files = 0, bytes = 0, skipped = 0;
    CHECK(wc_plat_scan_dir_ex(target, &cancel, test_excl_cb, &ctx, &files, &bytes));
    CHECK(files == 1);
    CHECK(bytes == 100);

    CHECK(wc_plat_clean_dir_ex(target, false, &cancel, test_excl_cb, &ctx, &files, &bytes,
                               &skipped));
    CHECK(files == 1);
    CHECK(bytes == 100);
    CHECK(skipped == 0);
    CHECK(!file_exists_utf8(f_del));
    CHECK(file_exists_utf8(f_keep1));
    CHECK(file_exists_utf8(f_keep2));
}

static void test_worker_exclusions(const char *root) {
    char wdir[WC_PATH_CAP], f_inc[WC_PATH_CAP], f_exc[WC_PATH_CAP];
    CHECK(join_path(wdir, sizeof wdir, root, "worker_excl"));
    CHECK(join_path(f_inc, sizeof f_inc, wdir, "inc.tmp"));
    CHECK(join_path(f_exc, sizeof f_exc, wdir, "exc.tmp"));

    CHECK(make_dir_utf8(wdir));
    CHECK(write_bytes_utf8(f_inc, 40));
    CHECK(write_bytes_utf8(f_exc, 90));

    wc_target_def def;
    memset(&def, 0, sizeof def);
    snprintf(def.id, sizeof def.id, "test.excl_worker");
    snprintf(def.label, sizeof def.label, "Worker Excl Target");
    def.category = WC_CAT_SYSTEM;
    def.kind = WC_TARGET_DIR_CONTENTS;
    snprintf(def.subpath, sizeof def.subpath, "%s", wdir);
    def.default_enabled = true;

    wc_exclusion_item item;
    memset(&item, 0, sizeof item);
    snprintf(item.id, sizeof item.id, "1");
    snprintf(item.path, sizeof item.path, "%s", f_exc);

    wc_worker *w = (wc_worker *)calloc(1, sizeof *w);
    CHECK(w != NULL);
    CHECK(wc_worker_init(w));
    wc_worker_set_exclusions(w, &item, 1);

    bool selected[1] = {true};
    CHECK(wc_worker_start(w, WC_JOB_ANALYZE, false, &def, selected, 1));

    wc_worker_snapshot snap;
    memset(&snap, 0, sizeof snap);
    for (int i = 0; i < 500; i++) {
        if (!wc_worker_poll(w, &snap)) break;
        Sleep(10);
    }

    CHECK(snap.state == WC_JOB_DONE);
    CHECK(snap.total_files_found == 1);
    CHECK(snap.total_bytes_found == 40);

    wc_worker_destroy(w);
    free(w);
}

static void test_disk_free_space(void) {
    uint64_t free_b = 0, total_b = 0;
    CHECK(wc_plat_disk_free_space("C:\\", &free_b, &total_b));
    CHECK(total_b > 0);
    CHECK(free_b <= total_b);
}

int main(void) {
    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64];
    snprintf(leaf, sizeof leaf, "wada_p3_test_%lu", (unsigned long)GetCurrentProcessId());
    char test_root[WC_PATH_CAP];
    CHECK(join_path(test_root, sizeof test_root, temp_base, leaf));
    delete_tree_for_cleanup(test_root);
    CHECK(make_dir_utf8(test_root));

    test_exclusion_matching();
    test_exclusions_store(test_root);
    test_fs_clean_respects_exclusions(test_root);
    test_worker_exclusions(test_root);
    test_disk_free_space();

    delete_tree_for_cleanup(test_root);

    if (g_failures > 0) {
        fprintf(stderr, "phase3 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("phase3 tests: %d checks passed\n", g_checks);
    return 0;
}
