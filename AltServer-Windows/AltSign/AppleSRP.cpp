// SRP-6a calculations are adapted from js-srp-gsa, used under the ISC license.
// See Dependencies/js-srp-gsa-LICENSE.txt.

#include "AppleSRP.hpp"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
using Bytes = std::vector<unsigned char>;
using ByteRange = std::pair<const void*, size_t>;
using Bignum = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using BignumContext = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;

constexpr size_t GroupSize = 256;
constexpr char ModulusHex[] =
    "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050"
    "A37329CBB4A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50"
    "E8083969EDB767B0CF6095179A163AB3661A05FBD5FAAAE82918A9962F0B93B8"
    "55F97993EC975EEAA80D740ADBF4FF747359D041D5C33EA71D281E446B14773B"
    "CA97B43A23FB801676BD207A436C6481F1D2B9078717461A5B9D32E688F87748"
    "544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6"
    "AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB6"
    "94B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73";

Bytes SHA256(std::initializer_list<ByteRange> ranges)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    {
        throw std::runtime_error("Could not initialize SHA-256.");
    }

    for (const auto& range : ranges)
    {
        if (range.second > 0 && EVP_DigestUpdate(context.get(), range.first, range.second) != 1)
        {
            throw std::runtime_error("Could not update SHA-256.");
        }
    }

    Bytes digest(EVP_MD_size(EVP_sha256()));
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1)
    {
        throw std::runtime_error("Could not finalize SHA-256.");
    }
    digest.resize(length);
    return digest;
}

Bytes BignumBytes(const BIGNUM* number, bool padded = false)
{
    const int requiredSize = padded ? static_cast<int>(GroupSize) : BN_num_bytes(number);
    Bytes bytes(requiredSize > 0 ? requiredSize : 1, 0);
    const int result = padded
        ? BN_bn2binpad(number, bytes.data(), requiredSize)
        : BN_bn2bin(number, bytes.data());
    if (result < 0)
    {
        throw std::runtime_error("Could not serialize SRP value.");
    }
    return bytes;
}

Bignum BignumFromBytes(const Bytes& bytes)
{
    if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw std::runtime_error("Invalid SRP value length.");
    }
    BIGNUM* value = BN_bin2bn(bytes.data(), static_cast<int>(bytes.size()), nullptr);
    if (!value)
    {
        throw std::runtime_error("Could not parse SRP value.");
    }
    return Bignum(value, BN_free);
}

Bignum BignumFromDigest(const Bytes& digest)
{
    return BignumFromBytes(digest);
}
}

AppleSRP::AppleSRP()
{
    Bignum modulus(nullptr, BN_free);
    BIGNUM* rawModulus = nullptr;
    if (BN_hex2bn(&rawModulus, ModulusHex) == 0)
    {
        throw std::runtime_error("Could not initialize the SRP modulus.");
    }
    modulus.reset(rawModulus);

    Bignum generator(BN_new(), BN_free);
    Bignum privateKey(BN_new(), BN_free);
    Bignum publicKey(BN_new(), BN_free);
    BignumContext context(BN_CTX_new(), BN_CTX_free);
    if (!generator || !privateKey || !publicKey || !context || BN_set_word(generator.get(), 2) != 1)
    {
        throw std::runtime_error("Could not initialize SRP.");
    }

    do
    {
        if (BN_rand_range(privateKey.get(), modulus.get()) != 1)
        {
            throw std::runtime_error("Could not generate the SRP private value.");
        }
    } while (BN_is_zero(privateKey.get()));

    if (BN_mod_exp(publicKey.get(), generator.get(), privateKey.get(), modulus.get(), context.get()) != 1)
    {
        throw std::runtime_error("Could not generate the SRP public value.");
    }

    _modulus = modulus.release();
    _generator = generator.release();
    _privateKey = privateKey.release();
    _publicKey = publicKey.release();
}

AppleSRP::~AppleSRP()
{
    BN_free(_modulus);
    BN_free(_generator);
    BN_clear_free(_privateKey);
    BN_free(_publicKey);
    if (!_sessionKey.empty())
    {
        OPENSSL_cleanse(_sessionKey.data(), _sessionKey.size());
    }
}

Bytes AppleSRP::publicKey() const
{
    return BignumBytes(_publicKey, true);
}

std::optional<Bytes> AppleSRP::processChallenge(
    const std::string& username,
    const Bytes& password,
    const Bytes& salt,
    const Bytes& serverPublicKey)
{
    try
    {
        if (serverPublicKey.empty() || serverPublicKey.size() > GroupSize)
        {
            return std::nullopt;
        }
        Bignum serverKey = BignumFromBytes(serverPublicKey);
        BignumContext context(BN_CTX_new(), BN_CTX_free);
        Bignum remainder(BN_new(), BN_free);
        if (!context || !remainder || BN_nnmod(remainder.get(), serverKey.get(), _modulus, context.get()) != 1 || BN_is_zero(remainder.get()))
        {
            return std::nullopt;
        }

        Bytes paddedPublicKey = BignumBytes(_publicKey, true);
        Bytes paddedServerKey = BignumBytes(serverKey.get(), true);
        Bignum scrambling = BignumFromDigest(SHA256({
            { paddedPublicKey.data(), paddedPublicKey.size() },
            { paddedServerKey.data(), paddedServerKey.size() }
        }));
        if (BN_is_zero(scrambling.get()))
        {
            return std::nullopt;
        }

        const unsigned char separator = ':';
        Bytes passwordDigest = SHA256({
            { &separator, 1 },
            { password.data(), password.size() }
        });
        Bignum passwordExponent = BignumFromDigest(SHA256({
            { salt.data(), salt.size() },
            { passwordDigest.data(), passwordDigest.size() }
        }));

        Bytes modulusBytes = BignumBytes(_modulus);
        Bytes paddedGenerator = BignumBytes(_generator, true);
        Bignum multiplier = BignumFromDigest(SHA256({
            { modulusBytes.data(), modulusBytes.size() },
            { paddedGenerator.data(), paddedGenerator.size() }
        }));

        Bignum generatorPower(BN_new(), BN_free);
        Bignum product(BN_new(), BN_free);
        Bignum base(BN_new(), BN_free);
        Bignum exponent(BN_new(), BN_free);
        Bignum productUX(BN_new(), BN_free);
        Bignum premasterSecret(BN_new(), BN_free);
        if (!generatorPower || !product || !base || !exponent || !productUX || !premasterSecret ||
            BN_mod_exp(generatorPower.get(), _generator, passwordExponent.get(), _modulus, context.get()) != 1 ||
            BN_mod_mul(product.get(), multiplier.get(), generatorPower.get(), _modulus, context.get()) != 1 ||
            BN_mod_sub(base.get(), serverKey.get(), product.get(), _modulus, context.get()) != 1 ||
            BN_mul(productUX.get(), scrambling.get(), passwordExponent.get(), context.get()) != 1 ||
            BN_add(exponent.get(), _privateKey, productUX.get()) != 1 ||
            BN_mod_exp(premasterSecret.get(), base.get(), exponent.get(), _modulus, context.get()) != 1)
        {
            return std::nullopt;
        }

        Bytes secretBytes = BignumBytes(premasterSecret.get());
        _sessionKey = SHA256({ { secretBytes.data(), secretBytes.size() } });

        Bytes modulusDigest = SHA256({ { modulusBytes.data(), modulusBytes.size() } });
        Bytes generatorDigest = SHA256({ { paddedGenerator.data(), paddedGenerator.size() } });
        for (size_t index = 0; index < modulusDigest.size(); index++)
        {
            modulusDigest[index] ^= generatorDigest[index];
        }

        Bytes usernameDigest = SHA256({ { username.data(), username.size() } });
        Bytes unpaddedPublicKey = BignumBytes(_publicKey);
        Bytes unpaddedServerKey = BignumBytes(serverKey.get());
        Bytes clientProof = SHA256({
            { modulusDigest.data(), modulusDigest.size() },
            { usernameDigest.data(), usernameDigest.size() },
            { salt.data(), salt.size() },
            { unpaddedPublicKey.data(), unpaddedPublicKey.size() },
            { unpaddedServerKey.data(), unpaddedServerKey.size() },
            { _sessionKey.data(), _sessionKey.size() }
        });
        _serverProof = SHA256({
            { unpaddedPublicKey.data(), unpaddedPublicKey.size() },
            { clientProof.data(), clientProof.size() },
            { _sessionKey.data(), _sessionKey.size() }
        });
        return clientProof;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

bool AppleSRP::verifyServerProof(const Bytes& proof) const
{
    return proof.size() == _serverProof.size() &&
        CRYPTO_memcmp(proof.data(), _serverProof.data(), proof.size()) == 0;
}

const Bytes& AppleSRP::sessionKey() const
{
    return _sessionKey;
}
