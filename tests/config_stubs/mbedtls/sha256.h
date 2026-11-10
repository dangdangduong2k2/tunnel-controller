#pragma once
// Host test uses Windows' SHA-256 implementation; firmware uses mbedTLS.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cassert>
#pragma comment(lib, "bcrypt.lib")
struct mbedtls_sha256_context {
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
};
inline void mbedtls_sha256_init(mbedtls_sha256_context *c) {
    *c = {};
}
inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int) {
    assert(BCryptOpenAlgorithmProvider(&c->provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0);
    assert(BCryptCreateHash(c->provider, &c->hash, nullptr, 0, nullptr, 0, 0) == 0);
    return 0;
}
inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *p, size_t n) {
    return BCryptHashData(c->hash, (PUCHAR)p, (ULONG)n, 0);
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *p) {
    return BCryptFinishHash(c->hash, p, 32, 0);
}
inline void mbedtls_sha256_free(mbedtls_sha256_context *c) {
    if (c->hash)
        BCryptDestroyHash(c->hash);
    if (c->provider)
        BCryptCloseAlgorithmProvider(c->provider, 0);
}
