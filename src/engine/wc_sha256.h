#ifndef WC_SHA256_H
#define WC_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct wc_sha256_ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} wc_sha256_ctx;

void wc_sha256_init(wc_sha256_ctx *ctx);
void wc_sha256_update(wc_sha256_ctx *ctx, const uint8_t *data, size_t len);
void wc_sha256_final(wc_sha256_ctx *ctx, uint8_t hash[32]);

/* Fast partial fingerprint: hashes 64-bit file size + first 4 KB + last 4 KB. */
bool wc_sha256_file_partial(const char *path, uint64_t file_size, uint8_t hash[32]);

/* Full file SHA-256 streaming in 64 KB chunks. */
bool wc_sha256_file_full(const char *path, const volatile int *cancel_flag, uint8_t hash[32]);

#endif /* WC_SHA256_H */
