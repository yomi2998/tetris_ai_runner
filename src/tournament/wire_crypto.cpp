#include "tournament/wire.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace tournament_wire
{
    namespace
    {
        constexpr std::size_t kSeedSize = 32;
        constexpr std::size_t kSignatureSize = 64;
    }

    KeyPair generate_keypair()
    {
        std::array<unsigned char, kSeedSize> seed{};
        if (RAND_bytes(seed.data(), static_cast<int>(seed.size())) != 1)
        {
            throw std::runtime_error("RAND_bytes failed");
        }

        EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        if (pkey == nullptr)
        {
            throw std::runtime_error("EVP_PKEY_new_raw_private_key failed");
        }

        std::array<unsigned char, kSeedSize> public_bytes{};
        size_t public_length = public_bytes.size();
        bool const extracted = EVP_PKEY_get_raw_public_key(pkey, public_bytes.data(), &public_length) == 1
            && public_length == public_bytes.size();
        EVP_PKEY_free(pkey);
        if (!extracted)
        {
            throw std::runtime_error("EVP_PKEY_get_raw_public_key failed");
        }

        KeyPair keys;
        keys.public_key.assign(public_bytes.begin(), public_bytes.end());
        keys.secret_key.assign(seed.begin(), seed.end());
        return keys;
    }

    Signature sign_result(KeyPair const &keys, ResultBatch const &batch)
    {
        if (keys.secret_key.size() != kSeedSize)
        {
            throw std::invalid_argument("Ed25519 seed must be 32 bytes");
        }

        std::string const payload = encode_result_payload(batch);

        EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                      keys.secret_key.data(), keys.secret_key.size());
        if (pkey == nullptr)
        {
            throw std::runtime_error("EVP_PKEY_new_raw_private_key failed");
        }

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        Signature signature;
        bool ok = ctx != nullptr && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) == 1;
        size_t signature_length = 0;
        if (ok)
        {
            ok = EVP_DigestSign(ctx, nullptr, &signature_length,
                                reinterpret_cast<unsigned char const *>(payload.data()), payload.size()) == 1;
        }
        if (ok)
        {
            signature.resize(signature_length);
            ok = EVP_DigestSign(ctx, signature.data(), &signature_length,
                                reinterpret_cast<unsigned char const *>(payload.data()), payload.size()) == 1;
        }
        if (ok)
        {
            signature.resize(signature_length);
        }

        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);

        if (!ok)
        {
            throw std::runtime_error("EVP_DigestSign failed");
        }
        return signature;
    }

    bool verify_result(PublicKey const &public_key, ResultBatch const &batch, Signature const &signature)
    {
        if (public_key.size() != kSeedSize || signature.size() != kSignatureSize)
        {
            return false;
        }

        std::string const payload = encode_result_payload(batch);

        EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                     public_key.data(), public_key.size());
        if (pkey == nullptr)
        {
            return false;
        }

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (ctx == nullptr)
        {
            EVP_PKEY_free(pkey);
            return false;
        }

        bool const verified = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1
            && EVP_DigestVerify(ctx, signature.data(), signature.size(),
                                reinterpret_cast<unsigned char const *>(payload.data()), payload.size()) == 1;

        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return verified;
    }
}
