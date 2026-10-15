#pragma once
#include <zlib.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Do not assume cpprestsdk was built with its optional GZIP provider. Decode
// through the explicitly linked zlib dependency and reject incomplete/big data.
inline bool DecompressAppleResponse(const uint8_t* input, size_t inputSize,
    std::vector<uint8_t>& output) {
    constexpr size_t Limit = 4 * 1024 * 1024;
    output.clear();
    if (!input || inputSize == 0 || inputSize > Limit) return false;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(inputSize);
    if (inflateInit2(&stream, 15 + 32) != Z_OK) return false;
    struct Cleanup { z_stream* stream; ~Cleanup() { inflateEnd(stream); } } cleanup{&stream};
    std::array<uint8_t, 16384> buffer{};
    try {
        for (;;) {
            stream.next_out = buffer.data();
            stream.avail_out = static_cast<uInt>(buffer.size());
            auto before = stream.avail_in;
            int status = inflate(&stream, Z_NO_FLUSH);
            size_t length = buffer.size() - stream.avail_out;
            if (length > Limit - output.size()) break;
            output.insert(output.end(), buffer.data(), buffer.data() + length);
            if (status == Z_STREAM_END) {
                if (stream.avail_in == 0 && !output.empty()) return true;
                break;
            }
            if (status != Z_OK || (length == 0 && stream.avail_in == before)) break;
        }
    } catch (...) { /* Fail closed; do not leave partial data. */ }
    output.clear();
    return false;
}
