#pragma once

#include <optional>
#include <string>
#include <vector>

typedef struct bignum_st BIGNUM;

class AppleSRP
{
public:
    AppleSRP();
    ~AppleSRP();

    AppleSRP(const AppleSRP&) = delete;
    AppleSRP& operator=(const AppleSRP&) = delete;

    std::vector<unsigned char> publicKey() const;
    std::optional<std::vector<unsigned char>> processChallenge(
        const std::string& username,
        const std::vector<unsigned char>& password,
        const std::vector<unsigned char>& salt,
        const std::vector<unsigned char>& serverPublicKey);
    bool verifyServerProof(const std::vector<unsigned char>& proof) const;
    const std::vector<unsigned char>& sessionKey() const;

private:
    BIGNUM* _modulus = nullptr;
    BIGNUM* _generator = nullptr;
    BIGNUM* _privateKey = nullptr;
    BIGNUM* _publicKey = nullptr;
    std::vector<unsigned char> _sessionKey;
    std::vector<unsigned char> _serverProof;
};
