#pragma once
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <vector>

// AltSign's GSA transcript ends before np. np is the expected MAC, never
// transcript input. Keep both this verification and the preceding SRP M2 check.
inline bool VerifyAppleNegotiationProof(EVP_MD_CTX* transcript,
    const std::vector<unsigned char>& sessionKey,
    const std::vector<unsigned char>& proof) {
    if (!transcript || sessionKey.size() != 32 || proof.size() != 32) return false;
    unsigned char digest[32]{};
    unsigned char key[32]{};
    unsigned char expected[32]{};
    unsigned int length = 0;
    bool valid = false;
    if (EVP_DigestFinal_ex(transcript, digest, &length) == 1 && length == 32 &&
        HMAC(EVP_sha256(), sessionKey.data(), static_cast<int>(sessionKey.size()),
            reinterpret_cast<const unsigned char*>("HMAC key:"), 9, key, &length) && length == 32 &&
        HMAC(EVP_sha256(), key, 32, digest, 32, expected, &length) && length == 32) {
        valid = CRYPTO_memcmp(expected, proof.data(), 32) == 0;
    }
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(expected, sizeof(expected));
    return valid;
}
