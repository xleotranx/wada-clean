#ifndef WC_SAFETY_H
#define WC_SAFETY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Normalizes a Windows path:
 * - converts '/' to '\'
 * - collapses duplicate separators
 * - strips trailing '\' (except on drive roots like "C:\")
 * - rejects empty paths, UNC paths, and ".." traversal components. */
bool wc_safety_normalize_path(const char *in, char *out, size_t cap);

/* Returns true if `root` is a safe directory root for cleaning:
 * - valid normalized "<drive>:\<subpath>"
 * - not a drive root ("C:\") or protected OS/system directory. */
bool wc_safety_is_valid_target_root(const char *root);

/* Returns true if `candidate` is strictly deeper than `root` (case-insensitive)
 * on a directory boundary, and `root` is itself a valid target root. */
bool wc_safety_path_within(const char *root, const char *candidate);

/* Returns true if `filename` (single path component) is a Windows Explorer
 * thumbnail or icon cache database (`thumbcache_*.db` or `iconcache_*.db`). */
bool wc_safety_is_thumbcache_file(const char *filename);

/* Returns true if `filename` (single path component) has an installer/image
 * extension targeted by Old Downloads cleanup (`.exe`, `.msi`, `.msix`, `.iso`). */
bool wc_safety_is_old_installer_ext(const char *filename);

/* Returns true if `path` is permissible for quarantine move/restore (normalized
 * non-root path, not inside WinSxS/System32/SysWOW64/DriverStore/Program Files). */
bool wc_safety_is_quarantine_safe_path(const char *path);

/* Formats byte sizes into human-readable "B", "KB", "MB", or "GB" strings. */
void wc_format_bytes(uint64_t bytes, char *out, size_t cap);

#endif /* WC_SAFETY_H */
