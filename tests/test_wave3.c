#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc_disk.h"
#include "wc_leftovers.h"
#include "wc_platform.h"
#include "wc_sha256.h"

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
    int n = snprintf(out, cap, "%s\\%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static bool make_dir_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    return CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_pattern_age(const char *path, const uint8_t *data, size_t len, int age_days) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = (len == 0) || (WriteFile(h, data, (DWORD)len, &wr, NULL) && wr == (DWORD)len);
    if (ok && age_days > 0) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
        uint64_t sub = (uint64_t)age_days * 24ULL * 3600ULL * 10000000ULL;
        t = (t > sub) ? (t - sub) : 0;
        ft.dwLowDateTime = (DWORD)(t & 0xffffffffULL);
        ft.dwHighDateTime = (DWORD)(t >> 32);
        SetFileTime(h, &ft, &ft, &ft);
    }
    CloseHandle(h);
    return ok;
}

static void delete_tree(const char *dir) {
    wchar_t wpat[WC_PATH_CAP];
    char pat[WC_PATH_CAP];
    if (!join_path(pat, sizeof pat, dir, "*") || !to_wide(pat, wpat, WC_PATH_CAP)) return;
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            char name[WC_PATH_CAP], child[WC_PATH_CAP];
            WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, name, sizeof name, NULL, NULL);
            if (!join_path(child, sizeof child, dir, name)) continue;
            wchar_t wc[WC_PATH_CAP];
            if (!to_wide(child, wc, WC_PATH_CAP)) continue;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree(child);
                RemoveDirectoryW(wc);
            } else {
                SetFileAttributesW(wc, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(wc);
            }
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    wchar_t wd[WC_PATH_CAP];
    if (to_wide(dir, wd, WC_PATH_CAP)) RemoveDirectoryW(wd);
}

static void hex_encode(const uint8_t hash[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hex[(hash[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[hash[i] & 0xf];
    }
    out[64] = '\0';
}

static void test_sha256_vectors(void) {
    uint8_t hash[32];
    char hex[65];
    wc_sha256_ctx ctx;

    wc_sha256_init(&ctx);
    wc_sha256_final(&ctx, hash);
    hex_encode(hash, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    wc_sha256_init(&ctx);
    wc_sha256_update(&ctx, (const uint8_t *)"abc", 3);
    wc_sha256_final(&ctx, hash);
    hex_encode(hash, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static void test_duplicate_finder(const char *root) {
    char d1[WC_PATH_CAP], d2[WC_PATH_CAP];
    CHECK(join_path(d1, sizeof d1, root, "dups_a"));
    CHECK(join_path(d2, sizeof d2, root, "dups_b"));
    CHECK(make_dir_utf8(d1) && make_dir_utf8(d2));

    /* Two 12 KB files with identical head+tail (4 KB) but different middle bytes,
     * plus an exact duplicate of one of them, plus a unique-size file. */
    uint8_t buf_a[12288], buf_b[12288], buf_u[9000];
    memset(buf_a, 0x11, sizeof buf_a);
    memset(buf_b, 0x11, sizeof buf_b);
    buf_b[6000] = 0x99; /* differs only in middle (partial hash collides, full hash separates) */
    memset(buf_u, 0x22, sizeof buf_u);

    char p1[WC_PATH_CAP], p2[WC_PATH_CAP], p3[WC_PATH_CAP], p4[WC_PATH_CAP];
    CHECK(join_path(p1, sizeof p1, d1, "copy1.bin"));
    CHECK(join_path(p2, sizeof p2, d2, "copy2.bin"));
    CHECK(join_path(p3, sizeof p3, d2, "same_size_diff_mid.bin"));
    CHECK(join_path(p4, sizeof p4, d1, "unique.bin"));

    CHECK(write_pattern_age(p1, buf_a, sizeof buf_a, 0));
    CHECK(write_pattern_age(p2, buf_a, sizeof buf_a, 0));
    CHECK(write_pattern_age(p3, buf_b, sizeof buf_b, 0));
    CHECK(write_pattern_age(p4, buf_u, sizeof buf_u, 0));

    wc_dup_snapshot *snap = (wc_dup_snapshot *)calloc(1, sizeof *snap);
    CHECK(snap != NULL);
    volatile int cancel = 0;
    CHECK(wc_dup_find(root, 1024, &cancel, snap));
    CHECK(snap->group_count == 1);
    CHECK(snap->groups[0].file_count == 2);
    CHECK(snap->groups[0].file_bytes == 12288);
    CHECK(snap->total_wasted_bytes == 12288);
    CHECK(!snap->groups[0].files[0].selected);
    CHECK(!snap->groups[0].files[1].selected);
    free(snap);
}

static void test_stale_dev_artifacts(const char *root) {
    char proj_old[WC_PATH_CAP], proj_new[WC_PATH_CAP];
    char nm_old[WC_PATH_CAP], nm_new[WC_PATH_CAP], f_old[WC_PATH_CAP], f_new[WC_PATH_CAP];

    CHECK(join_path(proj_old, sizeof proj_old, root, "proj_old"));
    CHECK(join_path(proj_new, sizeof proj_new, root, "proj_new"));
    CHECK(join_path(nm_old, sizeof nm_old, proj_old, "node_modules"));
    CHECK(join_path(nm_new, sizeof nm_new, proj_new, "node_modules"));
    CHECK(join_path(f_old, sizeof f_old, nm_old, "dep.js"));
    CHECK(join_path(f_new, sizeof f_new, nm_new, "dep.js"));

    CHECK(make_dir_utf8(proj_old) && make_dir_utf8(proj_new));
    CHECK(make_dir_utf8(nm_old) && make_dir_utf8(nm_new));

    const uint8_t payload[64] = {1};
    CHECK(write_pattern_age(f_old, payload, sizeof payload, 45));
    CHECK(write_pattern_age(f_new, payload, sizeof payload, 2));

    wc_stale_snapshot *snap = (wc_stale_snapshot *)calloc(1, sizeof *snap);
    CHECK(snap != NULL);
    volatile int cancel = 0;
    CHECK(wc_stale_find(root, 30, 0, &cancel, snap));
    CHECK(snap->count == 1);
    CHECK(strcmp(snap->items[0].name, "node_modules") == 0);
    CHECK(snap->items[0].bytes == 64);
    CHECK(snap->items[0].age_days >= 44);
    CHECK(!snap->items[0].selected);
    free(snap);
}

static void test_leftovers_scanner(const char *root) {
    CHECK(wc_leftover_is_generic_name("Microsoft"));
    CHECK(wc_leftover_is_generic_name("Windows"));
    CHECK(wc_leftover_is_generic_name("app"));
    CHECK(!wc_leftover_is_generic_name("AcmeEdit"));

    CHECK(wc_leftover_match_name("AcmeEdit 2.4.1 (x64)", "AcmeCorp", "AcmeEdit") == 2);
    CHECK(wc_leftover_match_name("Acme Edit", "AcmeCorp", "AcmeEdit") == 1);
    CHECK(wc_leftover_match_name("Google Chrome", "Google", "Google") == 0);

    char appdata[WC_PATH_CAP], direct[WC_PATH_CAP], vendor[WC_PATH_CAP], vsub[WC_PATH_CAP];
    char other[WC_PATH_CAP], df[WC_PATH_CAP], vf[WC_PATH_CAP];
    CHECK(join_path(appdata, sizeof appdata, root, "FakeAppData"));
    CHECK(join_path(direct, sizeof direct, appdata, "AcmeEdit"));
    CHECK(join_path(vendor, sizeof vendor, appdata, "AcmeCorp"));
    CHECK(join_path(vsub, sizeof vsub, vendor, "AcmeEdit"));
    CHECK(join_path(other, sizeof other, appdata, "Microsoft"));
    CHECK(join_path(df, sizeof df, direct, "cache.dat"));
    CHECK(join_path(vf, sizeof vf, vsub, "prefs.json"));

    CHECK(make_dir_utf8(appdata) && make_dir_utf8(direct) && make_dir_utf8(vendor) &&
          make_dir_utf8(vsub) && make_dir_utf8(other));
    const uint8_t d[32] = {7};
    CHECK(write_pattern_age(df, d, 32, 0));
    CHECK(write_pattern_age(vf, d, 16, 0));

    wc_app_item app;
    memset(&app, 0, sizeof app);
    snprintf(app.name, sizeof app.name, "AcmeEdit 2.4 (x64)");
    snprintf(app.publisher, sizeof app.publisher, "AcmeCorp");

    const char *roots[1] = {appdata};
    wc_leftover_item found[8];
    int count = 0;
    volatile int cancel = 0;
    CHECK(wc_leftover_scan_roots(&app, roots, 1, &cancel, found, 8, &count));
    CHECK(count == 2);
}

int main(void) {
    test_sha256_vectors();

    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64], root[WC_PATH_CAP];
    snprintf(leaf, sizeof leaf, "wada_wave3_test_%lu", (unsigned long)GetCurrentProcessId());
    CHECK(join_path(root, sizeof root, temp_base, leaf));
    delete_tree(root);
    CHECK(make_dir_utf8(root));

    char dup_root[WC_PATH_CAP], stale_root[WC_PATH_CAP], lo_root[WC_PATH_CAP];
    CHECK(join_path(dup_root, sizeof dup_root, root, "dups"));
    CHECK(join_path(stale_root, sizeof stale_root, root, "stale"));
    CHECK(join_path(lo_root, sizeof lo_root, root, "leftovers"));
    CHECK(make_dir_utf8(dup_root) && make_dir_utf8(stale_root) && make_dir_utf8(lo_root));

    test_duplicate_finder(dup_root);
    test_stale_dev_artifacts(stale_root);
    test_leftovers_scanner(lo_root);

    delete_tree(root);

    if (g_failures > 0) {
        fprintf(stderr, "wave3 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("wave3 tests: %d checks passed\n", g_checks);
    return 0;
}
