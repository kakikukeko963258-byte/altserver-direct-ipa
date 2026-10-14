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
    int type() const;
    std::string identifier() const;
    std::string name() const;
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
