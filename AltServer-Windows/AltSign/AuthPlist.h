#pragma once
#include <plist/plist.h>
#include <vector>
#include <cstring>
#include <limits>

inline plist_t ParseAuthPlist(const std::vector<unsigned char>& data) {
    if (data.empty() || data.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) return nullptr;
    plist_t result = nullptr;
    if (data.size() >= 8 && std::memcmp(data.data(), "bplist00", 8) == 0)
        plist_from_bin(reinterpret_cast<const char*>(data.data()), static_cast<uint32_t>(data.size()), &result);
    else plist_from_xml(reinterpret_cast<const char*>(data.data()), static_cast<uint32_t>(data.size()), &result);
    return result;
}
