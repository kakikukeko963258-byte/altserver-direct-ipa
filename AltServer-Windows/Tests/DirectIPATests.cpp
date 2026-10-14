// Run without a connected phone or real Apple account. No signing is invoked.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <pplx/pplxtasks.h>
#include <memory>
#include <vector>
#include <optional>
#include <codecvt>
#include <locale>
#include <iostream>
#include <fstream>
#include <filesystem>

inline std::wstring WideStringFromString(std::string text) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(text);
}
inline std::string StringFromWideString(std::wstring text) {
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(text);
}
// Declarations isolate the direct bridge from Apple's signing operations.
struct Application {};
struct Device {
    enum Type { iPhone = 2, iPad = 4 };
    int fixtureType;
    std::string fixtureID;
    Device(int kind, std::string id) : fixtureType(kind), fixtureID(id) {}
    int type() const { return fixtureType; }
    std::string identifier() const { return fixtureID; }
    std::string name() const { return "Synthetic device"; }
};
struct DeviceManager {
    static DeviceManager* instance();
    std::vector<std::shared_ptr<Device>> availableDevices();
};
struct AltServerApp {
    static AltServerApp* instance();
    pplx::task<std::shared_ptr<Application>> InstallApplication(std::optional<std::string>,
        std::shared_ptr<Device>, std::string, std::string);
};
inline std::shared_ptr<Device> _selectedDevice;
inline std::optional<std::string> _ipaFilepath;
inline HINSTANCE hInst;
constexpr int ID_LOGIN = 1;
BOOL CALLBACK LoginDlgProc(HWND, UINT, WPARAM, LPARAM);
#include "../AltServer/DirectIPA.h"
#include "../AltSign/AuthPlist.h"
#include "../AltSign/GSAHttpClient.h"
#include <openssl/pem.h>
#include <openssl/sha.h>

int main() {
    auto check = [](bool condition, const char* label) {
        if (!condition) { std::cerr << "FAIL: " << label << std::endl; throw std::runtime_error(label); }
    };
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporary);
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t guidText[40]{};
    StringFromGUID2(guid, guidText, 40);
    auto id = StringFromWideString(guidText);
    auto path = std::filesystem::path(temporary) / (std::wstring(L"日本語 空白 ") + guidText + L".IPA");
    auto target = DirectIPA::CredentialTarget("test-" + id);
    HWND sender = nullptr;
    HWND receiver = nullptr;
    try {
        check(GSAHttpClient::IsSafePath("/grandslam/GsService2"), "allow fixed-host authentication path");
        check(!GSAHttpClient::IsSafePath("//untrusted.invalid/auth"), "reject alternate host");
        check(!GSAHttpClient::IsSafePath("https://untrusted.invalid/auth"), "reject absolute URL");
        check(!GSAHttpClient::IsSafePath("/auth\r\ninjected"), "reject URL header injection");
        const std::string xml = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>Response</key><dict><key>Status</key><dict><key>ec</key><integer>0</integer></dict></dict></dict></plist>";
        auto xmlPlist = ParseAuthPlist(std::vector<unsigned char>(xml.begin(), xml.end()));
        check(xmlPlist && plist_get_node_type(xmlPlist) == PLIST_DICT, "parse XML authentication fixture");
        char* binaryBytes = nullptr;
        uint32_t binaryLength = 0;
        plist_to_bin(xmlPlist, &binaryBytes, &binaryLength);
        auto binaryPlist = ParseAuthPlist(std::vector<unsigned char>(binaryBytes, binaryBytes + binaryLength));
        check(binaryPlist && plist_get_node_type(binaryPlist) == PLIST_DICT, "parse binary authentication fixture");
        plist_free(binaryPlist); plist_free(xmlPlist); free(binaryBytes);
        check(ParseAuthPlist({}) == nullptr, "reject empty authentication response");
        auto pem = BIO_new_mem_buf(AppleRootCAPem, static_cast<int>(sizeof(AppleRootCAPem) - 1));
        auto root = PEM_read_bio_X509(pem, nullptr, nullptr, nullptr);
        check(root != nullptr, "parse embedded Apple trust anchor");
        unsigned char digest[SHA256_DIGEST_LENGTH]{};
        unsigned int digestLength = 0;
        check(X509_digest(root, EVP_sha256(), digest, &digestLength) == 1, "hash Apple trust anchor");
        std::string hex;
        const char* digits = "0123456789abcdef";
        for (unsigned int i = 0; i < digestLength; ++i) {hex += digits[digest[i] >> 4]; hex += digits[digest[i] & 15];}
        check(hex == "b0b1730ecbc7ff4505142c49f1295e6eda6bcaed7e2c68c5be91b5a11001f024", "verify official Apple root fingerprint");
        X509_free(root); BIO_free(pem);
        check(DirectIPA::CandidateDevices({}).empty(), "zero devices");
        auto phone = std::make_shared<Device>(Device::iPhone, "synthetic-one");
        auto duplicate = std::make_shared<Device>(Device::iPhone, "synthetic-one");
        auto tablet = std::make_shared<Device>(Device::iPad, "synthetic-two");
        auto television = std::make_shared<Device>(8, "synthetic-tv");
        check(DirectIPA::CandidateDevices({phone}).size() == 1, "one device");
        check(DirectIPA::CandidateDevices({phone, tablet}).size() == 2, "two devices");
        check(DirectIPA::CandidateDevices({phone, duplicate, tablet, television, nullptr}).size() == 2, "deduplicate and filter devices");
        std::ofstream(path, std::ios::binary) << "synthetic fixture";
        check(DirectIPA::ValidPath(path.wstring()), "unicode absolute IPA");
        check(!DirectIPA::ValidPath(L"relative.ipa"), "reject relative paths");
        check(!DirectIPA::ValidPath(L"\\\\server\\share\\test.ipa"), "reject network paths");
        check(!DirectIPA::ValidPath(path.parent_path().wstring()), "reject directories");
        check(!DirectIPA::ValidPath(path.wstring() + L".exe"), "reject extension");
        auto malformed = path.wstring();
        malformed.push_back(L'\0'); malformed += L"other";
        check(!DirectIPA::ValidPath(malformed), "reject embedded NUL");
        DirectIPA::Credentials credential;
        check(!DirectIPA::ReadCredentials("test-" + id, credential), "missing credential");
        check(DirectIPA::SaveCredentials("test-" + id, L"fixture@example.invalid", L"synthetic-password"), "save fixture credential");
        check(DirectIPA::ReadCredentials("test-" + id, credential), "read fixture credential");
        check(credential.user == L"fixture@example.invalid" && credential.password == L"synthetic-password", "credential round trip");
        credential.Clear();
        check(credential.password.empty(), "clear credential buffer");
        check(!DirectIPA::UserSID(GetCurrentProcessId()).empty(), "identify current Windows user");
        check(DirectIPA::UserSID(0).empty(), "reject invalid sender process");
        sender = CreateWindowExW(0, L"STATIC", L"Test sender", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        receiver = CreateWindowExW(0, L"STATIC", L"Test receiver", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        check(sender && receiver, "create test endpoints");
        auto file = path.wstring();
        COPYDATASTRUCT data{DirectIPA::Protocol, static_cast<DWORD>((file.size() + 1) * sizeof(wchar_t)), file.data()};
        check(!DirectIPA::AcceptRequest(receiver, 0, reinterpret_cast<LPARAM>(&data)), "reject missing sender");
        auto bad = data; bad.dwData = 0;
        check(!DirectIPA::AcceptRequest(receiver, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&bad)), "reject wrong protocol");
        bad = data; bad.cbData -= sizeof(wchar_t);
        check(!DirectIPA::AcceptRequest(receiver, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&bad)), "reject nonterminated payload");
        check(DirectIPA::AcceptRequest(receiver, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data)), "accept direct IPA path");
        check(DirectIPA::pending && *DirectIPA::pending == file, "preserve unicode IPA path");
        check(!DirectIPA::AcceptRequest(receiver, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data)), "reject second queued request");
        DirectIPA::pending.reset(); DirectIPA::busy = true;
        check(!DirectIPA::AcceptRequest(receiver, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&data)), "reject busy receiver");
        DirectIPA::busy = false;
        std::cout << "Direct IPA bridge checks passed." << std::endl;
    } catch (...) {
        CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
        if (sender) DestroyWindow(sender);
        if (receiver) DestroyWindow(receiver);
        std::filesystem::remove(path);
        return 1;
    }
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
    if (sender) DestroyWindow(sender);
    if (receiver) DestroyWindow(receiver);
    std::filesystem::remove(path);
    return 0;
}
