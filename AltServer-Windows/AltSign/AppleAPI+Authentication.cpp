//  Heavily based on sample code provided by Kabir Oberai (https://github.com/kabiroberai)

#include "AppleAPI.hpp"

#include "AnisetteData.h"
#include "AppleSRP.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <ostream>

using namespace std;
using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

extern std::string make_uuid();

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

extern bool decompress(const uint8_t* input, size_t input_size, std::vector<uint8_t>& output);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

static const char ALTHexCharacters[] = "0123456789abcdef";

std::vector<unsigned char> DataFromBytes(const char* bytes, size_t count)
{
	if (count == 0)
	{
		return {};
	}

	if (bytes == nullptr)
	{
		throw std::invalid_argument("Cannot copy a null byte buffer.");
	}

	const auto first = reinterpret_cast<const unsigned char*>(bytes);
	return std::vector<unsigned char>(first, first + count);
}

void ALTDigestUpdateString(EVP_MD_CTX* context, const std::string& string)
{
	EVP_DigestUpdate(context, string.data(), string.size());
}

void ALTDigestUpdateData(EVP_MD_CTX* context, const std::vector<unsigned char>& data)
{
	uint32_t data_len = (uint32_t)data.size(); // 4 bytes for length
	EVP_DigestUpdate(context, &data_len, sizeof(data_len));
	EVP_DigestUpdate(context, data.data(), data.size());
}

std::vector<unsigned char> ALTHMACSHA256(
	const std::vector<unsigned char>& key,
	std::initializer_list<std::pair<const void*, size_t>> ranges)
{
	std::unique_ptr<HMAC_CTX, decltype(&HMAC_CTX_free)> context(HMAC_CTX_new(), HMAC_CTX_free);
	std::vector<unsigned char> output(EVP_MD_size(EVP_sha256()));
	unsigned int outputLength = 0;
	if (key.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		!context || HMAC_Init_ex(context.get(), key.data(), static_cast<int>(key.size()), EVP_sha256(), nullptr) != 1)
	{
		return {};
	}

	for (const auto& range : ranges)
	{
		if (range.second > 0 && HMAC_Update(context.get(), static_cast<const unsigned char*>(range.first), range.second) != 1)
		{
			return {};
		}
	}

	if (HMAC_Final(context.get(), output.data(), &outputLength) != 1)
	{
		return {};
	}
	output.resize(outputLength);
	return output;
}

std::optional<std::vector<unsigned char>> ALTPBKDF2SRP(bool isS2k, const std::string& password, const std::vector<unsigned char>& salt, uint64_t iterations)
{
	std::vector<unsigned char> passwordDigest(EVP_MD_size(EVP_sha256()));
	unsigned int passwordDigestLength = 0;
	if (EVP_Digest(password.data(), password.size(), passwordDigest.data(), &passwordDigestLength, EVP_sha256(), nullptr) != 1)
	{
		return std::nullopt;
	}
	passwordDigest.resize(passwordDigestLength);

	std::vector<unsigned char> derivedPassword;
	if (isS2k)
	{
		derivedPassword = passwordDigest;
	}
	else
	{
		derivedPassword.resize(passwordDigest.size() * 2);
		for (size_t index = 0; index < passwordDigest.size(); index++)
		{
			const unsigned char byte = passwordDigest[index];
			derivedPassword[index * 2] = ALTHexCharacters[(byte >> 4) & 0x0F];
			derivedPassword[index * 2 + 1] = ALTHexCharacters[byte & 0x0F];
		}
	}

	std::vector<unsigned char> output(EVP_MD_size(EVP_sha256()));
	if (iterations == 0 || iterations > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
		salt.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		derivedPassword.size() > static_cast<size_t>(std::numeric_limits<int>::max()) || PKCS5_PBKDF2_HMAC(
		reinterpret_cast<const char*>(derivedPassword.data()),
		static_cast<int>(derivedPassword.size()),
		salt.data(),
		static_cast<int>(salt.size()),
		static_cast<int>(iterations),
		EVP_sha256(),
		static_cast<int>(output.size()),
		output.data()) != 1)
	{
		return std::nullopt;
	}
	return output;
}

std::vector<unsigned char> ALTCreateSessionKey(const AppleSRP& srpContext, const char* keyName)
{
	const auto& sessionKey = srpContext.sessionKey();
	return ALTHMACSHA256(sessionKey, { { keyName, strlen(keyName) } });
}

std::optional<std::vector<unsigned char>> ALTDecryptDataCBC(const AppleSRP& srpContext, const std::vector<unsigned char>& spd)
{
	auto key = ALTCreateSessionKey(srpContext, "extra data key:");
	auto iv = ALTCreateSessionKey(srpContext, "extra data iv:");
	if (key.size() != 32 || iv.size() < 16 || spd.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		return std::nullopt;
	}

	std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	std::vector<unsigned char> decrypted(spd.size() + EVP_MAX_BLOCK_LENGTH);
	int firstLength = 0;
	int finalLength = 0;
	if (!context ||
		EVP_DecryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1 ||
		EVP_DecryptUpdate(context.get(), decrypted.data(), &firstLength, spd.data(), static_cast<int>(spd.size())) != 1 ||
		EVP_DecryptFinal_ex(context.get(), decrypted.data() + firstLength, &finalLength) != 1)
	{
		return std::nullopt;
	}
	decrypted.resize(firstLength + finalLength);
	return decrypted;
}

std::optional<std::vector<unsigned char>> ALTDecryptDataGCM(const std::vector<unsigned char>& sk, const std::vector<unsigned char>& encryptedData)
{
	if (sk.size() != 32 || encryptedData.size() < 35)
	{
		odslog("ERROR: Encrypted token too short.");
		return std::nullopt;
	}

	if (CRYPTO_memcmp(encryptedData.data(), "XYZ", 3) != 0)
	{
		odslog("ERROR: Encrypted token wrong version!");
		return std::nullopt;
	}

	const size_t encryptedLength = encryptedData.size() - 35;
	if (encryptedLength > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		return std::nullopt;
	}
	std::vector<unsigned char> decrypted(encryptedLength + EVP_MAX_BLOCK_LENGTH);
	std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	int outputLength = 0;
	int finalLength = 0;
	if (!context ||
		EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
		EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, 16, nullptr) != 1 ||
		EVP_DecryptInit_ex(context.get(), nullptr, nullptr, sk.data(), encryptedData.data() + 3) != 1 ||
		EVP_DecryptUpdate(context.get(), nullptr, &outputLength, encryptedData.data(), 3) != 1 ||
		EVP_DecryptUpdate(context.get(), decrypted.data(), &outputLength, encryptedData.data() + 19, static_cast<int>(encryptedLength)) != 1 ||
		EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(encryptedData.data() + 19 + encryptedLength)) != 1 ||
		EVP_DecryptFinal_ex(context.get(), decrypted.data() + outputLength, &finalLength) != 1)
	{
		odslog("ERROR: Invalid encrypted token tag.");
		return std::nullopt;
	}
	decrypted.resize(outputLength + finalLength);
	return decrypted;
}

std::vector<unsigned char> ALTCreateAppTokensChecksum(const std::vector<unsigned char>& sk, const std::string& adsid, const std::vector<std::string>& apps)
{
	std::vector<unsigned char> input;
	const std::string prefix = "apptokens";
	input.insert(input.end(), prefix.begin(), prefix.end());
	input.insert(input.end(), adsid.begin(), adsid.end());
	for (const auto& app : apps)
	{
		input.insert(input.end(), app.begin(), app.end());
	}
	return ALTHMACSHA256(sk, { { input.data(), input.size() } });
}

pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>> AppleAPI::Authenticate(
	std::string unsanitizedAppleID,
	std::string password,
	std::shared_ptr<AnisetteData> anisetteData,
	std::optional<std::function <pplx::task<std::optional<std::string>>(void)>> verificationHandler)
{
	// Authenticating only works with lowercase email address, even if Apple ID contains capital letters.
	auto sanitizedAppleID = unsanitizedAppleID;
	std::transform(sanitizedAppleID.begin(), sanitizedAppleID.end(), sanitizedAppleID.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	auto adsidValue = std::make_shared<std::string>("");
	auto sessionValue = std::make_shared<AppleAPISession>();

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = anisetteData->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<std::string, plist_t> clientDictionary = {
		{ "bootstrap", plist_new_bool(true) },
		{ "icscrec", plist_new_bool(true) },
		{ "loc", plist_new_string(anisetteData->locale().c_str()) },
		{ "pbe", plist_new_bool(false) },
		{ "prkgen", plist_new_bool(true) },
		{ "svct", plist_new_string("iCloud") },
		{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
		{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
		{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
		{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
		{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
		{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
		{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
		{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
		{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
	};

	/* Begin SRP-6a negotiation. */
	auto srpContext = std::make_shared<AppleSRP>();
	auto digestContext = std::shared_ptr<EVP_MD_CTX>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	if (!digestContext || EVP_DigestInit_ex(digestContext.get(), EVP_sha256(), nullptr) != 1)
	{
		throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
	}

	std::vector<std::string> ps = { "s2k", "s2k_fo" };
	ALTDigestUpdateString(digestContext.get(), ps[0]);
	ALTDigestUpdateString(digestContext.get(), ",");
	ALTDigestUpdateString(digestContext.get(), ps[1]);

	auto A_data = srpContext->publicKey();

	ALTDigestUpdateString(digestContext.get(), "|");

	auto psPlist = plist_new_array();
	for (auto value : ps)
	{
		plist_array_append_item(psPlist, plist_new_string(value.c_str()));
	}

	auto cpd = plist_new_dict();
	for (auto pair : clientDictionary)
	{
		plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
	}

	std::map<std::string, plist_t> parameters = {
		{ "A2k", plist_new_data((const char *)A_data.data(), A_data.size()) },
		{ "ps", psPlist },
		{ "cpd", cpd },
		{ "u", plist_new_string(sanitizedAppleID.c_str()) },
		{ "o", plist_new_string("init") }
	};

	auto task = this->SendAuthenticationRequest(parameters, anisetteData)
		.then([=](plist_t plist) {

		auto spNode = plist_dict_get_item(plist, "sp");
		if (spNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* sp = nullptr;
		plist_get_string_val(spNode, &sp);

		bool isS2K = (std::string(sp) == "s2k");

		ALTDigestUpdateString(digestContext.get(), "|");

		if (sp)
		{
			ALTDigestUpdateString(digestContext.get(), sp);
		}

		auto cNode = plist_dict_get_item(plist, "c");
		auto saltNode = plist_dict_get_item(plist, "s");
		auto iterationsNode = plist_dict_get_item(plist, "i");
		auto bNode = plist_dict_get_item(plist, "B");

		if (cNode == nullptr || saltNode == nullptr || iterationsNode == nullptr || bNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* c = nullptr;
		plist_get_string_val(cNode, &c);

		char* saltBytes = nullptr;
		uint64_t saltSize = 0;
		plist_get_data_val(saltNode, &saltBytes, &saltSize);

		uint64_t iterations = 0;
		plist_get_uint_val(iterationsNode, &iterations);

		char* B_bytes = nullptr;
		uint64_t B_size = 0;
		plist_get_data_val(bNode, &B_bytes, &B_size);

		auto salt = DataFromBytes((const char*)saltBytes, saltSize);
		auto B_data = DataFromBytes((const char*)B_bytes, B_size);

		auto passwordKey = ALTPBKDF2SRP(isS2K, password, salt, iterations);
		if (passwordKey == ::nullopt)
		{
			throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
		}

		auto clientProof = srpContext->processChallenge(sanitizedAppleID, *passwordKey, salt, B_data);
		if (!clientProof)
		{
			throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
		}

		time_t time;
		struct tm* tm;
		char dateString[64];

		time = anisetteData->date().tv_sec;
		tm = localtime(&time);

		strftime(dateString, sizeof dateString, "%FT%T%z", tm);

		std::map<std::string, plist_t> clientDictionary = {
		{ "bootstrap", plist_new_bool(true) },
		{ "icscrec", plist_new_bool(true) },
		{ "loc", plist_new_string(anisetteData->locale().c_str()) },
		{ "pbe", plist_new_bool(false) },
		{ "prkgen", plist_new_bool(true) },
		{ "svct", plist_new_string("iCloud") },
		{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
		{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
		{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
		{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
		{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
		{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
		{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
		{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
		{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
		};

		auto cpd = plist_new_dict();
		for (auto pair : clientDictionary)
		{
			plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
		}

		std::map<std::string, plist_t> parameters = {
			{ "c", plist_new_string(c) },
			{ "M1", plist_new_data((const char*)clientProof->data(), clientProof->size()) },
			{ "cpd", cpd },
			{ "u", plist_new_string(sanitizedAppleID.c_str()) },
			{ "o", plist_new_string("complete") }
		};

		return this->SendAuthenticationRequest(parameters, anisetteData);
			}).then([=](plist_t plist) {

				auto M2_node = plist_dict_get_item(plist, "M2");
				if (M2_node == nullptr)
				{
					odslog("ERROR: M2 data not found!");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* M2_bytes = nullptr;
				uint64_t M2_size = 0;
				plist_get_data_val(M2_node, &M2_bytes, &M2_size);

				auto serverProof = DataFromBytes(M2_bytes, M2_size);
				if (!srpContext->verifyServerProof(serverProof))
				{
					odslog("ERROR: Failed to verify session.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				ALTDigestUpdateString(digestContext.get(), "|");

				std::vector<unsigned char> spd;
				auto spdNode = plist_dict_get_item(plist, "spd");
				if (spdNode != nullptr)
				{
					char* spdBytes = nullptr;
					uint64_t spdSize = 0;
					plist_get_data_val(spdNode, &spdBytes, &spdSize);

					spd = DataFromBytes(spdBytes, spdSize);
					ALTDigestUpdateData(digestContext.get(), spd);
				}

				ALTDigestUpdateString(digestContext.get(), "|");

				auto scNode = plist_dict_get_item(plist, "sc");
				if (scNode != nullptr)
				{
					char* scBytes = nullptr;
					uint64_t scSize = 0;
					plist_get_data_val(scNode, &scBytes, &scSize);

					auto sc = DataFromBytes(scBytes, scSize);
					ALTDigestUpdateData(digestContext.get(), sc);
				}

				ALTDigestUpdateString(digestContext.get(), "|");

				auto npNode = plist_dict_get_item(plist, "np");
				if (npNode == nullptr)
				{
					odslog("ERROR: Missing np dictionary.");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* npBytes = nullptr;
				uint64_t npSize = 0;
				plist_get_data_val(npNode, &npBytes, &npSize);

				auto np = DataFromBytes(npBytes, npSize);
				ALTDigestUpdateData(digestContext.get(), np);

				size_t digest_len = EVP_MD_size(EVP_sha256());
				if (np.size() != digest_len)
				{
					odslog("ERROR: Neg proto hash is too short.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				std::vector<unsigned char> digest(digest_len);
				unsigned int finalDigestLength = 0;
				if (EVP_DigestFinal_ex(digestContext.get(), digest.data(), &finalDigestLength) != 1)
				{
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}
				digest.resize(finalDigestLength);

				auto hmacKey = ALTCreateSessionKey(*srpContext, "HMAC key:");
				auto hmacOutput = ALTHMACSHA256(hmacKey, { { digest.data(), digest.size() } });
				if (hmacOutput.size() != digest_len)
				{
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				if (CRYPTO_memcmp(hmacOutput.data(), np.data(), digest_len) != 0)
				{
					odslog("ERROR: Invalid neg prot hmac.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				auto decryptedData = ALTDecryptDataCBC(*srpContext, spd);
				if (decryptedData == ::nullopt)
				{
					odslog("ERROR: Could not decrypt login response.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				plist_t decryptedPlist = nullptr;
				plist_from_xml((const char *)decryptedData->data(), (int)decryptedData->size(), &decryptedPlist);

				if (decryptedPlist == nullptr)
				{
					odslog("ERROR: Could not parse decrypted login response plist!");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto adsidNode = plist_dict_get_item(decryptedPlist, "adsid");
				auto idmsTokenNode = plist_dict_get_item(decryptedPlist, "GsIdmsToken");

				if (adsidNode == nullptr || idmsTokenNode == nullptr)
				{
					odslog("ERROR: adsid and /or idmsToken is nil.");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* adsid = nullptr;
				plist_get_string_val(adsidNode, &adsid);

				char* idmsToken = nullptr;
				plist_get_string_val(idmsTokenNode, &idmsToken);

				auto statusDictionary = plist_dict_get_item(plist, "Status");
				if (statusDictionary == nullptr)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				std::optional<std::string> authType = std::nullopt;

				auto authTypeNode = plist_dict_get_item(statusDictionary, "au");
				if (authTypeNode != nullptr)
				{
					char* rawAuthType = nullptr;
					plist_get_string_val(authTypeNode, &rawAuthType);

					authType = rawAuthType;
				}

				if (authType == "trustedDeviceSecondaryAuth")
				{
					odslog("Requires trusted device two factor...");

					if (verificationHandler.has_value())
					{
						return this->RequestTrustedDeviceTwoFactorCode(adsid, idmsToken, anisetteData, *verificationHandler)
						.then([=](bool success) {
							return this->Authenticate(unsanitizedAppleID, password, anisetteData, std::nullopt);
						});
					}
					else
					{
						throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
					}
				}
				else if (authType == "secondaryAuth")
				{
					odslog("Requires SMS two factor...");

					if (verificationHandler.has_value())
					{
						return this->RequestSMSTwoFactorCode(adsid, idmsToken, anisetteData, *verificationHandler)
							.then([=](bool success) {
							return this->Authenticate(unsanitizedAppleID, password, anisetteData, std::nullopt);
						});
					}
					else
					{
						throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
					}
				}
				else
				{
					auto skNode = plist_dict_get_item(decryptedPlist, "sk");
					auto cNode = plist_dict_get_item(decryptedPlist, "c");

					if (skNode == nullptr || cNode == nullptr)
					{
						odslog("ERROR: No ak and /or c data.");
						throw APIError(APIErrorCode::InvalidResponse);
					}

					char* skBytes = nullptr;
					uint64_t skSize = 0;
					plist_get_data_val(skNode, &skBytes, &skSize);

					char* cBytes = nullptr;
					uint64_t cSize = 0;
					plist_get_data_val(cNode, &cBytes, &cSize);

					auto sk = DataFromBytes((const char*)skBytes, skSize);

					auto appsNode = plist_new_array();
					plist_array_append_item(appsNode, plist_new_string("com.apple.gs.xcode.auth"));

					auto checksum = ALTCreateAppTokensChecksum(sk, adsid, { "com.apple.gs.xcode.auth" });

					time_t time;
					struct tm* tm;
					char dateString[64];

					time = anisetteData->date().tv_sec;
					tm = localtime(&time);

					strftime(dateString, sizeof dateString, "%FT%T%z", tm);

					std::map<std::string, plist_t> clientDictionary = {
					{ "bootstrap", plist_new_bool(true) },
					{ "icscrec", plist_new_bool(true) },
					{ "loc", plist_new_string(anisetteData->locale().c_str()) },
					{ "pbe", plist_new_bool(false) },
					{ "prkgen", plist_new_bool(true) },
					{ "svct", plist_new_string("iCloud") },
					{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
					{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
					{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
					{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
					{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
					{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
					{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
					{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
					{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
					};

					auto cpd = plist_new_dict();
					for (auto pair : clientDictionary)
					{
						plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
					}

					std::map<std::string, plist_t> parameters = {
						{ "u", plist_new_string(adsid) },
						{ "app", appsNode },
						{ "c", plist_new_data((const char *)cBytes, cSize) },
						{ "t", plist_new_string(idmsToken) },
						{ "checksum", plist_new_data((const char*)checksum.data(), checksum.size()) },
						{ "cpd", cpd },
						{ "o", plist_new_string("apptokens") }
					};

					*adsidValue = std::string(adsid);
					return this->FetchAuthToken(parameters, sk, anisetteData)
					.then([=](std::string token) {
						auto session = std::make_shared<AppleAPISession>(*adsidValue, token, anisetteData);
						*sessionValue = *session;

						return this->FetchAccount(session);
					})
					.then([=](std::shared_ptr<Account> account) -> std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>> {
						return std::make_pair(account, sessionValue);
					});
				}
			});

	return task;
}

pplx::task<std::string> AppleAPI::FetchAuthToken(std::map<std::string, plist_t> requestParameters, std::vector<unsigned char> sk, std::shared_ptr<AnisetteData> anisetteData)
{
	auto apps = requestParameters["app"];
	auto appNode = plist_array_get_item(apps, 0);

	char* appName = nullptr;
	plist_get_string_val(appNode, &appName);

	std::string app(appName);

	return this->SendAuthenticationRequest(requestParameters, anisetteData)
	.then([=](plist_t plist) {

		auto encryptedTokenNode = plist_dict_get_item(plist, "et");
		if (encryptedTokenNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* encryptedTokenBytes = nullptr;
		uint64_t encryptedTokenSize = 0;
		plist_get_data_val(encryptedTokenNode, &encryptedTokenBytes, &encryptedTokenSize);

		auto sk_copy = sk;

		auto encryptedToken = DataFromBytes(encryptedTokenBytes, encryptedTokenSize);
		auto decryptedToken = ALTDecryptDataGCM(sk_copy, encryptedToken);

		if (decryptedToken == ::nullopt)
		{
			odslog("ERROR: Failed to decrypt apptoken.");
			throw APIError(APIErrorCode::InvalidResponse);
		}

		plist_t decryptedTokenPlist = nullptr;
		plist_from_xml((const char *)decryptedToken->data(), decryptedToken->size(), &decryptedTokenPlist);

		if (decryptedTokenPlist == nullptr)
		{
			odslog("ERROR: Could not parse decrypted apptoken plist.");
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokensNode = plist_dict_get_item(decryptedTokenPlist, "t");
		if (tokensNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokenDictionary = plist_dict_get_item(tokensNode, app.c_str());
		if (tokenDictionary == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokenNode = plist_dict_get_item(tokenDictionary, "token");
		if (tokenNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* token = nullptr;
		plist_get_string_val(tokenNode, &token);

		odslog("Received app token for " << app << ".");

		return std::string(token);
	});
}

pplx::task<bool> AppleAPI::RequestTrustedDeviceTwoFactorCode(
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData,
	const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler)
{
	std::string requestURL = "/auth/verify/trusteddevice";
	std::string verifyURL = "/grandslam/GsService2/validate";

	auto request = this->MakeTwoFactorCodeRequest(requestURL, dsid, idmsToken, anisetteData);

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received 2FA response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> decompressedData)
					{
						return verificationHandler();
					})
				.then([=](std::optional<std::string> verificationCode) {
						if (!verificationCode.has_value())
						{
							throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
						}

						// Send verification code request.
						auto request = this->MakeTwoFactorCodeRequest(verifyURL, dsid, idmsToken, anisetteData);
						request.headers().add(L"security-code", WideStringFromString(*verificationCode));

						return this->gsaClient().request(request);
					})
				.then([=](http_response response)
					{
						return response.content_ready();
					})
				.then([=](http_response response)
					{
						odslog("Received 2FA response status code: " << response.status_code());
						return response.extract_vector();
					})
				.then([=](std::vector<unsigned char> compressedData)
					{
						std::vector<uint8_t> decompressedData;

						if (compressedData.size() > 2 && compressedData[0] == '<' && compressedData[1] == '?')
						{
							// Already decompressed
							decompressedData = compressedData;
						}
						else
						{
							decompress((const uint8_t*)compressedData.data(), (size_t)compressedData.size(), decompressedData);
						}

						std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());

						plist_t plist = nullptr;
						plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

						if (plist == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						return plist;
					})
				.then([this](plist_t plist)
					{
						// Handle verification code response.
						return this->ProcessTwoFactorResponse<bool>(plist, [](auto plist) {
							auto node = plist_dict_get_item(plist, "ec");
							if (node)
							{
								uint64_t errorCode = 0;
								plist_get_uint_val(node, &errorCode);

								if (errorCode != 0)
								{
									throw APIError(APIErrorCode::InvalidResponse);
								}
							}

							return true;
						}, [=](auto resultCode) -> optional<APIError>
						{
							switch (resultCode)
							{
							case -21669:
								return std::make_optional<APIError>(APIErrorCode::IncorrectVerificationCode);

							default:
								return std::nullopt;
							}
						});
					});

			return task;
}

pplx::task<bool> AppleAPI::RequestSMSTwoFactorCode(
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData,
	const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler)
{
	auto requestURL = "/auth/verify/phone/put?mode=sms";
	auto verifyURL = "/auth/verify/phone/securitycode?referrer=/auth/verify/phone/put";

	auto request = this->MakeTwoFactorCodeRequest(requestURL, dsid, idmsToken, anisetteData);
	request.set_method(web::http::methods::POST);

	auto phoneNumberNode = plist_new_string("1");

	auto serverInfoNode = plist_new_dict();
	plist_dict_set_item(serverInfoNode, "phoneNumber.id", phoneNumberNode);

	auto bodyPlist = plist_new_dict();
	plist_dict_set_item(bodyPlist, "serverInfo", serverInfoNode);

	char* bodyXML = NULL;
	uint32_t length = 0;
	plist_to_xml(bodyPlist, &bodyXML, &length);

	request.set_body(bodyXML);

	free(bodyXML);
	plist_free(bodyPlist);

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received 2FA response status code: " << response.status_code());
				return response.extract_vector();
			})
		.then([=](std::vector<unsigned char> decompressedData)
			{
				return verificationHandler();
			})
		.then([=](std::optional<std::string> verificationCode)
			{
				if (!verificationCode.has_value())
				{
					throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
				}

				auto request = this->MakeTwoFactorCodeRequest(verifyURL, dsid, idmsToken, anisetteData);
				request.set_method(web::http::methods::POST);

				auto securityCodeNode = plist_new_string(verificationCode->c_str());
				auto modeNode = plist_new_string("sms");
				auto phoneNumberNode = plist_new_string("1");

				auto serverInfoNode = plist_new_dict();
				plist_dict_set_item(serverInfoNode, "mode", modeNode);
				plist_dict_set_item(serverInfoNode, "phoneNumber.id", phoneNumberNode);

				auto bodyPlist = plist_new_dict();
				plist_dict_set_item(bodyPlist, "securityCode.code", securityCodeNode);
				plist_dict_set_item(bodyPlist, "serverInfo", serverInfoNode);

				char* bodyXML = NULL;
				uint32_t length = 0;
				plist_to_xml(bodyPlist, &bodyXML, &length);

				request.set_body(bodyXML);

				free(bodyXML);
				plist_free(bodyPlist);

				return this->gsaClient().request(request);
			})
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received verify 2FA response status code: " << response.status_code());

				if (response.status_code() != 200 || !response.headers().has(L"X-Apple-PE-Token"))
				{
					// PE token is included in headers if we sent correct verification code.
					throw APIError(APIErrorCode::IncorrectVerificationCode);
				}

				return true;
			});

	return task;
}

pplx::task<std::shared_ptr<Account>> AppleAPI::FetchAccount(std::shared_ptr<AppleAPISession> session)
{
	std::map<std::string, std::string> parameters = {};
	auto task = this->SendRequest("viewDeveloper.action", parameters, session, nullptr)
		.then([=](plist_t plist)->std::shared_ptr<Account>
		{
			auto account = this->ProcessResponse<shared_ptr<Account>>(plist, [](auto plist)
				{
					auto node = plist_dict_get_item(plist, "developer");
					if (node == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					auto account = make_shared<Account>(node);
					return account;

				}, [=](auto resultCode) -> optional<APIError>
				{
					return nullopt;
				});

			return account;
		});

	return task;
}

pplx::task<plist_t> AppleAPI::SendAuthenticationRequest(std::map<std::string, plist_t> requestParameters,
	std::shared_ptr<AnisetteData> anisetteData)
{
	auto header = plist_new_dict();
	plist_dict_set_item(header, "Version", plist_new_string("1.0.1"));

	auto requestDictionary = plist_new_dict();
	for (auto& parameter : requestParameters)
	{
		plist_dict_set_item(requestDictionary, parameter.first.c_str(), parameter.second);
	}

	std::map<std::string, plist_t> parameters = {
		{ "Header", header },
		{ "Request", requestDictionary }
	};

	auto plist = plist_new_dict();
	for (auto& parameter : parameters)
	{
		plist_dict_set_item(plist, parameter.first.c_str(), parameter.second);
	}

	char* plistXML = nullptr;
	uint32_t length = 0;
	plist_to_xml(plist, &plistXML, &length);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Content-Type", L"text/x-xml-plist"},
		{L"X-Mme-Client-Info", WideStringFromString(anisetteData->deviceDescription())},
		{L"Accept", L"*/*"},
		{L"User-Agent", L"akd/1.0 CFNetwork/978.0.7 Darwin/18.7.0"}
	};

	uri_builder builder(U("/grandslam/GsService2"));

	http_request request(methods::POST);
	request.set_request_uri(builder.to_string());
	request.set_body(plistXML);

	for (auto& pair : headers)
	{
		if (request.headers().has(pair.first))
		{
			request.headers().remove(pair.first);
		}

		request.headers().add(pair.first, pair.second);
	}

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received auth response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> compressedData)
					{
						std::vector<uint8_t> decompressedData = compressedData;

						std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());

						plist_t plist = nullptr;
						plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

						if (plist == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						return plist;
					})
		.then([=](plist_t plist)
          {
				auto dictionary = plist_dict_get_item(plist, "Response");
				if (dictionary == NULL)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto statusNode = plist_dict_get_item(dictionary, "Status");
				if (statusNode == NULL)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto node = plist_dict_get_item(statusNode, "ec");
				int64_t resultCode = 0;

				auto type = plist_get_node_type(node);
				switch (type)
				{
				case PLIST_STRING:
				{
					char* value = nullptr;
					plist_get_string_val(node, &value);

					resultCode = atoi(value);
					break;
				}

				case PLIST_UINT:
				{
					uint64_t value = 0;
					plist_get_uint_val(node, &value);

					resultCode = (int64_t)value;
					break;
				}

				case PLIST_REAL:
				{
					double value = 0;
					plist_get_real_val(node, &value);

					resultCode = (int64_t)value;
					break;
				}

				default:
					break;
				}

				switch (resultCode)
				{
				case 0: return dictionary;
				case -22406: throw APIError(APIErrorCode::IncorrectCredentials);
				case -29004: throw APIError(APIErrorCode::InvalidAnisetteData);
				default:
				{
					auto descriptionNode = plist_dict_get_item(statusNode, "em");
					if (descriptionNode == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					char* errorDescription = nullptr;
					plist_get_string_val(descriptionNode, &errorDescription);

					if (errorDescription == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					std::stringstream ss;
					ss << errorDescription << " (" << resultCode << ")";

					throw LocalizedAPIError(resultCode, ss.str());
				}
				}
          });

		free(plistXML);
		plist_free(plist);

		return task;
}

web::http::http_request AppleAPI::MakeTwoFactorCodeRequest(
	std::string url,
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData)
{
	auto encodedURI = web::uri::encode_uri(WideStringFromString(url));
	uri_builder builder(encodedURI);

	uri requestURI = builder.to_string();

	std::string identityToken = dsid + ":" + idmsToken;

	std::vector<unsigned char> identityTokenData(identityToken.begin(), identityToken.end());
	auto encodedIdentityToken = utility::conversions::to_base64(identityTokenData);

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = anisetteData->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Accept", L"application/x-buddyml"},
		{L"Accept-Language", L"en-us"},
		{L"Content-Type", L"application/x-plist"},
		{L"User-Agent", L"Xcode"},
		{L"X-Apple-App-Info", L"com.apple.gs.xcode.auth"},
		{L"X-Xcode-Version", L"11.2 (11B41)"},

		{L"X-Apple-Identity-Token", encodedIdentityToken},
		{L"X-Apple-I-MD-M", WideStringFromString(anisetteData->machineID()) },
		{L"X-Apple-I-MD", WideStringFromString(anisetteData->oneTimePassword()) },
		{L"X-Apple-I-MD-LU", WideStringFromString(anisetteData->localUserID()) },
		{L"X-Apple-I-MD-RINFO", WideStringFromString(std::to_string(anisetteData->routingInfo())) },

		{L"X-Mme-Device-Id", WideStringFromString(anisetteData->deviceUniqueIdentifier()) },
		{L"X-Mme-Client-Info", WideStringFromString(anisetteData->deviceDescription()) },
		{L"X-Apple-I-Client-Time", WideStringFromString(dateString) },
		{L"X-Apple-Locale", WideStringFromString(anisetteData->locale()) },
		{L"X-Apple-I-TimeZone", WideStringFromString(anisetteData->timeZone()) },
	};

	http_request request(methods::GET);
	request.set_request_uri(requestURI);

	for (auto& pair : headers)
	{
		if (request.headers().has(pair.first))
		{
			request.headers().remove(pair.first);
		}

		request.headers().add(pair.first, pair.second);
	}

	return request;
}
