#include "platform_internal.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Standalone SHA-256 (FIPS 180-4) + HMAC-SHA256
 * ============================================================================ */

static const uint32_t k_sha256_k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k_sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
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

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void mbedtls_sha256_init(mbedtls_sha256_context *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void mbedtls_sha256_free(mbedtls_sha256_context *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224) {
    if (!ctx || is224 != 0) {
        return -1;
    }
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
    ctx->total_bits = 0;
    ctx->buffer_len = 0;
    return 0;
}

int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *input, size_t ilen) {
    if (!ctx || (!input && ilen > 0)) {
        return -1;
    }
    ctx->total_bits += (uint64_t)ilen * 8ULL;
    size_t offset = 0;
    if (ctx->buffer_len > 0) {
        size_t need = 64 - ctx->buffer_len;
        size_t take = (ilen < need) ? ilen : need;
        memcpy(ctx->buffer + ctx->buffer_len, input, take);
        ctx->buffer_len += take;
        offset += take;
        if (ctx->buffer_len == 64) {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
    while (offset + 64 <= ilen) {
        sha256_transform(ctx->state, input + offset);
        offset += 64;
    }
    if (offset < ilen) {
        ctx->buffer_len = ilen - offset;
        memcpy(ctx->buffer, input + offset, ctx->buffer_len);
    }
    return 0;
}

int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32]) {
    if (!ctx || !output) {
        return -1;
    }
    ctx->buffer[ctx->buffer_len++] = 0x80;
    if (ctx->buffer_len > 56) {
        memset(ctx->buffer + ctx->buffer_len, 0, 64 - ctx->buffer_len);
        sha256_transform(ctx->state, ctx->buffer);
        ctx->buffer_len = 0;
    }
    memset(ctx->buffer + ctx->buffer_len, 0, 56 - ctx->buffer_len);
    for (int i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)((ctx->total_bits >> ((7 - i) * 8)) & 0xFFU);
    }
    sha256_transform(ctx->state, ctx->buffer);
    ctx->buffer_len = 0;

    for (int i = 0; i < 8; i++) {
        output[i * 4 + 0] = (uint8_t)((ctx->state[i] >> 24) & 0xFFU);
        output[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFU);
        output[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFFU);
        output[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFFU);
    }
    return 0;
}

int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char output[32], int is224) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, is224) != 0) return -1;
    if (mbedtls_sha256_update(&ctx, input, ilen) != 0) return -1;
    if (mbedtls_sha256_finish(&ctx, output) != 0) return -1;
    mbedtls_sha256_free(&ctx);
    return 0;
}

void platform_hmac_sha256(const uint8_t *key, size_t key_len,
                          const uint8_t *msg, size_t msg_len,
                          uint8_t out_digest[32]) {
    uint8_t k_block[64];
    memset(k_block, 0, sizeof(k_block));
    if (key_len > 64) {
        mbedtls_sha256(key, key_len, k_block, 0);
    } else if (key && key_len > 0) {
        memcpy(k_block, key, key_len);
    }

    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    for (int i = 0; i < 64; i++) {
        k_ipad[i] = k_block[i] ^ 0x36U;
        k_opad[i] = k_block[i] ^ 0x5CU;
    }

    uint8_t inner[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, k_ipad, 64);
    if (msg && msg_len > 0) {
        mbedtls_sha256_update(&ctx, msg, msg_len);
    }
    mbedtls_sha256_finish(&ctx, inner);

    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, k_opad, 64);
    mbedtls_sha256_update(&ctx, inner, 32);
    mbedtls_sha256_finish(&ctx, out_digest);
    mbedtls_sha256_free(&ctx);
}

/* ============================================================================
 * Standalone Base64 Encode / Decode
 * ============================================================================ */

static const char k_b64_enc[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen) {
    size_t req = 4 * ((slen + 2) / 3);
    if (!olen) return -1;
    if (!dst || dlen < req + 1) {
        /* Note: utils.c checks dlen >= req without +1 when dlen == req */
        if (!dst || dlen < req) {
            *olen = req + 1;
            return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
        }
    }
    size_t o = 0;
    for (size_t i = 0; i < slen; i += 3) {
        uint32_t v = ((uint32_t)src[i]) << 16;
        if (i + 1 < slen) v |= ((uint32_t)src[i + 1]) << 8;
        if (i + 2 < slen) v |= ((uint32_t)src[i + 2]);

        dst[o++] = (unsigned char)k_b64_enc[(v >> 18) & 0x3F];
        dst[o++] = (unsigned char)k_b64_enc[(v >> 12) & 0x3F];
        dst[o++] = (unsigned char)((i + 1 < slen) ? k_b64_enc[(v >> 6) & 0x3F] : '=');
        dst[o++] = (unsigned char)((i + 2 < slen) ? k_b64_enc[v & 0x3F] : '=');
    }
    if (o < dlen) {
        dst[o] = '\0';
    }
    *olen = o;
    return 0;
}

static int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
                          const unsigned char *src, size_t slen) {
    if (!olen) return -1;
    /* Count valid base64 chars ignoring whitespace */
    size_t valid = 0;
    size_t pad = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = src[i];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') {
            pad++;
            valid++;
        } else if (b64_val(c) >= 0) {
            if (pad > 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
            valid++;
        } else {
            return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        }
    }
    if (valid % 4 != 0 || pad > 2) {
        return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    }
    size_t out_req = (valid / 4) * 3 - pad;
    if (!dst || dlen < out_req) {
        *olen = out_req;
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }

    size_t o = 0;
    uint32_t acc = 0;
    int group = 0;
    int group_pad = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = src[i];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        acc <<= 6;
        if (c == '=') {
            group_pad++;
        } else {
            acc |= (uint32_t)b64_val(c);
        }
        group++;
        if (group == 4) {
            dst[o++] = (unsigned char)((acc >> 16) & 0xFFU);
            if (group_pad < 2) dst[o++] = (unsigned char)((acc >> 8) & 0xFFU);
            if (group_pad < 1) dst[o++] = (unsigned char)(acc & 0xFFU);
            acc = 0;
            group = 0;
            group_pad = 0;
        }
    }
    *olen = o;
    return 0;
}
