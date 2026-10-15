#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cpprest/json.h>
#include "Device.hpp"

namespace RemoteIPA {
inline std::filesystem::path Directory() {
    wchar_t folder[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, folder)))
        throw std::runtime_error("Remote settings unavailable.");
    return std::filesystem::path(folder) / L"AltServerDirectIPA" / L"Remote";
}
inline std::shared_ptr<Device> RegisteredDevice() {
    auto file = Directory() / L"config.json";
    if (std::filesystem::file_size(file) > 16384) throw std::runtime_error("Invalid remote settings.");
    std::ifstream input(file, std::ios::binary);
    auto json = web::json::value::parse(utility::conversions::to_string_t(std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>())));
    auto text = [&](const wchar_t* key) { return utility::conversions::to_utf8string(json.at(key).as_string()); };
    auto id = text(L"udid");
    auto name = text(L"name");
    auto type = json.at(L"device_type").as_integer();
    if (id.empty() || id.size() > 128 || name.size() > 512 || (type != Device::iPhone && type != Device::iPad))
        throw std::runtime_error("Invalid registered phone.");
    auto device = std::make_shared<Device>(name, id, static_cast<Device::Type>(type));
    device->setOSVersion(OperatingSystemVersion(text(L"version")));
    return device;
}
inline std::wstring Quote(const std::wstring& value) {
    // Full Windows argv quoting, including backslashes before quotes/end.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
inline void RunWorker(const std::vector<std::wstring>& arguments, DWORD timeout) {
    wchar_t module[32768]{};
    if (!GetModuleFileNameW(nullptr, module, 32768)) throw std::runtime_error("Remote worker unavailable.");
    auto executable = std::filesystem::path(module).parent_path() / L"altforge-remote.exe";
    std::wstring command = Quote(executable.wstring());
    for (auto& argument : arguments) command += L" " + Quote(argument);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, executable.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("Remote worker could not start.");
    CloseHandle(process.hThread);
    auto waited = WaitForSingleObject(process.hProcess, timeout);
    DWORD code = 1;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
    CloseHandle(process.hProcess);
    if (code != 0) throw std::runtime_error("Remote installation failed. Check the iPhone VPN and registration.");
}
inline void Verify(const std::shared_ptr<Device>& device) {
    if (RegisteredDevice()->identifier() != device->identifier()) throw std::runtime_error("Remote target changed.");
    RunWorker({L"probe"}, 45000);
}
inline void Install(const std::string& app, const std::shared_ptr<Device>& device) {
    RunWorker({L"install", utility::conversions::to_string_t(app),
        utility::conversions::to_string_t(device->identifier())}, 950000);
}
}
