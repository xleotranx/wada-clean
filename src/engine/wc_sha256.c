#include "wc_sha256.h"

#include <stdio.h>
#include <string.h>

#include "wc_platform.h"

#define ROTR32(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

static const uint32_t k_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

static void sha256_transform(wc_sha256_ctx *ctx, const uint8_t block[64]) {
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)block[j] << 24) | ((uint32_t)block[j + 1] << 16) |
               ((uint32_t)block[j + 2] << 8) | (uint32_t)block[j + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR32(m[i - 15], 7) ^ ROTR32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = ROTR32(m[i - 2], 17) ^ ROTR32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k_sha256_k[i] + m[i];
        uint32_t S0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void wc_sha256_init(wc_sha256_ctx *ctx) {
    if (!ctx) return;
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

void wc_sha256_update(wc_sha256_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || (!data && len > 0)) return;
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void wc_sha256_final(wc_sha256_ctx *ctx, uint8_t hash[32]) {
    if (!ctx || !hash) return;
    size_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80u;
        while (i < 56) ctx->data[i++] = 0x00u;
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64) ctx->data[i++] = 0x00u;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8ULL;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            hash[i + (size_t)j * 4] = (uint8_t)((ctx->state[j] >> (24 - i * 8)) & 0xffu);
        }
    }
}

bool wc_sha256_file_partial(const char *path, uint64_t file_size, uint8_t hash[32]) {
    if (!path || !path[0] || !hash) return false;
    FILE *fp = wc_plat_fopen_read(path);
    if (!fp) return false;

    wc_sha256_ctx ctx;
    wc_sha256_init(&ctx);

    uint8_t len_le[8];
    for (int i = 0; i < 8; i++) {
        len_le[i] = (uint8_t)((file_size >> (i * 8)) & 0xffu);
    }
    wc_sha256_update(&ctx, len_le, sizeof len_le);

    uint8_t buf[4096];
    size_t head_n = fread(buf, 1, sizeof buf, fp);
    if (ferror(fp)) {
        fclose(fp);
        return false;
    }
    wc_sha256_update(&ctx, buf, head_n);

    if (file_size > sizeof buf) {
        uint64_t tail_off = file_size - sizeof buf;
        if (wc_plat_fseek64(fp, tail_off)) {
            size_t tail_n = fread(buf, 1, sizeof buf, fp);
            if (ferror(fp)) {
                fclose(fp);
                return false;
            }
            wc_sha256_update(&ctx, buf, tail_n);
        }
    }

    fclose(fp);
    wc_sha256_final(&ctx, hash);
    return true;
}

bool wc_sha256_file_full(const char *path, const volatile int *cancel_flag, uint8_t hash[32]) {
    if (!path || !path[0] || !hash) return false;
    FILE *fp = wc_plat_fopen_read(path);
    if (!fp) return false;

    wc_sha256_ctx ctx;
    wc_sha256_init(&ctx);

    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
        if (cancel_flag && *cancel_flag) {
            fclose(fp);
            return false;
        }
        wc_sha256_update(&ctx, buf, n);
    }
    bool err = ferror(fp) != 0;
    fclose(fp);
    if (err) return false;

    wc_sha256_final(&ctx, hash);
    return true;
}
