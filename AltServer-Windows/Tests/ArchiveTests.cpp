#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "../AltSign/Archiver.hpp"
extern "C" {
#include "zip.h"
}
namespace fs = std::filesystem;
std::wstring WideStringFromString(std::string text) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
std::string StringFromWideString(std::wstring text) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::string replace_all(const std::string& input, const std::string& from, const std::string& to) {
    auto value = input;
    for (size_t offset = 0; (offset = value.find(from, offset)) != std::string::npos; offset += to.size()) value.replace(offset, from.size(), to);
    return value;
}
void check(bool value) { if (!value) throw std::runtime_error("Archive regression failed"); }
void fixture(const fs::path& file, const std::string& name, bool symlink = false) {
    auto zip = zipOpen(file.string().c_str(), APPEND_STATUS_CREATE);
    check(zip != nullptr);
    zip_fileinfo info{};
    if (symlink) info.external_fa = 0120777UL << 16;
    check(zipOpenNewFileInZip(zip, "Payload/", nullptr, nullptr, 0, nullptr, 0, nullptr, 0, 0) == ZIP_OK);
    check(zipCloseFileInZip(zip) == ZIP_OK);
    check(zipOpenNewFileInZip(zip, name.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, 0, 0) == ZIP_OK);
    const std::string content = "synthetic-fixture-content";
    check(zipWriteInFileInZip(zip, content.data(), static_cast<unsigned>(content.size())) == ZIP_OK);
    check(zipCloseFileInZip(zip) == ZIP_OK);
    check(zipClose(zip, nullptr) == ZIP_OK);
}
int main() {
    wchar_t temporary[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporary);
    auto temp = fs::canonical(temporary);
    auto root = temp / (L"AltForgeArchiveTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(root)) return 1;
    int result = 0;
    try {
        auto archive = root / "fixture.zip";
        auto out = root / "out";
        fs::create_directory(out);
        fixture(archive, "Payload/Phone.app/Info.plist");
        UnzipArchive(archive.string(), out.string());
        check(fs::is_regular_file(out / "Payload/Phone.app/Info.plist"));
        fixture(archive, u8"Payload/Phone.app/日本語.txt");
        UnzipArchive(archive.string(), out.string());
        check(fs::is_regular_file(out / L"Payload/Phone.app/日本語.txt"));
        for (auto name : {"../escape", "/escape", "Payload/../../escape", "Payload/CON.txt"}) {
            fixture(archive, name);
            bool rejected = false;
            try { UnzipArchive(archive.string(), out.string()); } catch (...) { rejected = true; }
            check(rejected && !fs::exists(root / "escape"));
        }
        fixture(archive, "Payload/link", true);
        bool rejected = false;
        try { UnzipArchive(archive.string(), out.string()); } catch (...) { rejected = true; }
        check(rejected);
        fixture(archive, "Payload/crc.txt");
        std::ifstream input(archive, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
        input.close();
        std::string marker = "synthetic-fixture-content";
        auto found = std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end());
        check(found != bytes.end()); *found ^= 1;
        std::ofstream output(archive, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), bytes.size()); output.close();
        rejected = false;
        try { UnzipArchive(archive.string(), out.string()); } catch (...) { rejected = true; }
        check(rejected);
        std::cout << "Actual ZIP extraction passed: directories, UTF-8 paths, traversal, aliases, symlinks and CRC rejection.\n";
    } catch (...) { result = 1; }
    // Only remove the uniquely created fixture root, directly inside temp.
    if (fs::canonical(root).parent_path() == temp && root.filename().wstring().find(L"AltForgeArchiveTests-") == 0) fs::remove_all(root);
    return result;
}
