#include "wc_safety.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "wc_types.h"

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int ascii_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static bool is_sep(char c) {
    return c == '\\' || c == '/';
}

bool wc_safety_normalize_path(const char *in, char *out, size_t cap) {
    if (!in || !in[0] || !out || cap < 2) return false;

    /* Reject UNC / network paths ("\\server\share" or "//server/share"). */
    if (is_sep(in[0]) && is_sep(in[1])) return false;

    char tmp[WC_PATH_CAP];
    size_t out_len = 0;
    const char *p = in;
#ifdef _WIN32
    char sep = '\\';
#else
    char sep = '/';
#endif

    if (isalpha((unsigned char)p[0]) && p[1] == ':') {
        if (!is_sep(p[2])) {
            /* Drive-relative path like "C:foo" is unsafe/ambiguous. */
            return false;
        }
        if (cap < 4 || 3 >= sizeof tmp) return false;
        sep = '\\';
        tmp[out_len++] = (char)toupper((unsigned char)p[0]);
        tmp[out_len++] = ':';
        tmp[out_len++] = '\\';
        p += 3;
    } else if (p[0] == '/') {
        sep = '/';
        tmp[out_len++] = '/';
        p += 1;
    }

    while (*p) {
        while (is_sep(*p)) p++;
        if (!*p) break;

        const char *seg_start = p;
        while (*p && !is_sep(*p)) p++;
        size_t seg_len = (size_t)(p - seg_start);

        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            return false;
        }
        if (seg_len == 1 && seg_start[0] == '.') {
            continue;
        }

        if (out_len > 0 && tmp[out_len - 1] != sep) {
            if (out_len + 1 >= sizeof tmp) return false;
            tmp[out_len++] = sep;
        }
        if (out_len + seg_len >= sizeof tmp) return false;
        memcpy(tmp + out_len, seg_start, seg_len);
        out_len += seg_len;
    }

    if (out_len == 0 || out_len >= cap) return false;
    tmp[out_len] = '\0';
    memcpy(out, tmp, out_len + 1);
    return true;
}

bool wc_safety_is_valid_target_root(const char *root) {
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;

    if (isalpha((unsigned char)norm[0]) && norm[1] == ':' && norm[2] == '\\') {
        if (norm[3] == '\0') return false; /* Drive root "C:\" */

        const char *rel = norm + 3;
        static const char *const protected_rel[] = {
            "Windows",
            "Windows\\System32",
            "Windows\\SysWOW64",
            "Windows\\WinSxS",
            "Windows\\Installer",
            "Users",
            "Program Files",
            "Program Files (x86)",
            "ProgramData",
        };
        for (size_t i = 0; i < sizeof protected_rel / sizeof protected_rel[0]; i++) {
            if (ascii_strcasecmp(rel, protected_rel[i]) == 0) return false;
        }

        static const char *const protected_prefixes[] = {
            "Program Files\\",
            "Program Files (x86)\\",
            "Windows\\System32\\",
            "Windows\\SysWOW64\\",
            "Windows\\WinSxS\\",
            "Windows\\Installer\\",
        };
        for (size_t i = 0; i < sizeof protected_prefixes / sizeof protected_prefixes[0]; i++) {
            size_t plen = strlen(protected_prefixes[i]);
            if (ascii_strncasecmp(rel, protected_prefixes[i], plen) == 0) return false;
        }

        /* Forbid wiping an entire user profile root like "C:\Users\alice". */
        if (ascii_strncasecmp(rel, "Users\\", 6) == 0) {
            const char *after_user = rel + 6;
            if (!after_user[0] || strchr(after_user, '\\') == NULL) {
                return false;
            }
        }
        return true;
    }

    if (norm[0] == '/') {
        if (norm[1] == '\0') return false; /* Root "/" */

        static const char *const protected_posix[] = {
            "/System",
            "/Library",
            "/Applications",
            "/Users",
            "/bin",
            "/sbin",
            "/usr",
            "/etc",
            "/var",
            "/private",
            "/private/var",
            "/private/etc",
            "/Volumes",
            "/cores",
            "/dev",
        };
        for (size_t i = 0; i < sizeof protected_posix / sizeof protected_posix[0]; i++) {
            if (ascii_strcasecmp(norm, protected_posix[i]) == 0) return false;
        }
        static const char *const protected_posix_prefixes[] = {
            "/System/",
            "/Applications/",
            "/bin/",
            "/sbin/",
            "/usr/",
            "/etc/",
            "/private/etc/",
            "/dev/",
        };
        for (size_t i = 0; i < sizeof protected_posix_prefixes / sizeof protected_posix_prefixes[0];
             i++) {
            size_t plen = strlen(protected_posix_prefixes[i]);
            if (ascii_strncasecmp(norm, protected_posix_prefixes[i], plen) == 0) return false;
        }

        /* Forbid wiping an entire home folder like "/Users/alice" or "/home/alice". */
        if (ascii_strncasecmp(norm, "/Users/", 7) == 0) {
            const char *after_user = norm + 7;
            if (!after_user[0] || strchr(after_user, '/') == NULL) return false;
        }
        if (ascii_strncasecmp(norm, "/home/", 6) == 0) {
            const char *after_user = norm + 6;
            if (!after_user[0] || strchr(after_user, '/') == NULL) return false;
        }
        return true;
    }

    return false;
}

bool wc_safety_path_within(const char *root, const char *candidate) {
    char nr[WC_PATH_CAP];
    char nc[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, nr, sizeof nr)) return false;
    if (!wc_safety_is_valid_target_root(nr)) return false;
    if (!wc_safety_normalize_path(candidate, nc, sizeof nc)) return false;

    char sep = (nr[0] == '/') ? '/' : '\\';
    size_t rlen = strlen(nr);
    size_t clen = strlen(nc);
    if (clen <= rlen + 1) return false;
    if (ascii_strncasecmp(nc, nr, rlen) != 0) return false;
    if (nc[rlen] != sep) return false;
    return nc[rlen + 1] != '\0';
}

bool wc_safety_is_thumbcache_file(const char *filename) {
    if (!filename || !filename[0]) return false;
    for (const char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') return false;
    }
    size_t len = strlen(filename);
    if (len <= 3 || ascii_strcasecmp(filename + len - 3, ".db") != 0) return false;
    if (len >= 14 && ascii_strncasecmp(filename, "thumbcache_", 11) == 0) return true;
    if (len >= 13 && ascii_strncasecmp(filename, "iconcache_", 10) == 0) return true;
    return false;
}

void wc_format_bytes(uint64_t bytes, char *out, size_t cap) {
    if (!out || cap == 0) return;
    const uint64_t kb = 1024ULL;
    const uint64_t mb = 1024ULL * 1024ULL;
    const uint64_t gb = 1024ULL * 1024ULL * 1024ULL;

    if (bytes < kb) {
        snprintf(out, cap, "%llu B", (unsigned long long)bytes);
    } else if (bytes < mb) {
        snprintf(out, cap, "%.1f KB", (double)bytes / (double)kb);
    } else if (bytes < gb) {
        snprintf(out, cap, "%.1f MB", (double)bytes / (double)mb);
    } else {
        snprintf(out, cap, "%.2f GB", (double)bytes / (double)gb);
    }
}
