#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdint>
namespace ArchiveSafety {
constexpr uint64_t MaxExpanded = 4ULL * 1024 * 1024 * 1024;
constexpr uint64_t MaxFile = 1024ULL * 1024 * 1024;
constexpr uint64_t MaxEntries = 50000;
inline bool SafeName(const std::string& path) {
    if (path.empty() || path.size() >= 512 || path[0] == '/' || path[0] == '\\' ||
        path.find_first_of("\\:\0", 0, 3) != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? path.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        for (unsigned char c : part) if (c < 32 || c == 127 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        auto base = part.substr(0, part.find('.'));
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$" ||
            (base.size() == 4 && (base.substr(0,3) == "COM" || base.substr(0,3) == "LPT") && base[3] >= '1' && base[3] <= '9')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
inline bool SafeSize(uint64_t entries, uint64_t file, uint64_t expanded) {
    return entries <= MaxEntries && file <= MaxFile && expanded <= MaxExpanded && file <= MaxExpanded - expanded;
}
}
