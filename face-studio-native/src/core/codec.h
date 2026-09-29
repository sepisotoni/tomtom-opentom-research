// Small self-contained hashing / encoding helpers (no third-party code).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tt {

using Bytes = std::vector<uint8_t>;

std::string sha256_hex(const uint8_t* data, size_t size);
inline std::string sha256_hex(const Bytes& b) { return sha256_hex(b.data(), b.size()); }
inline std::string sha256_hex(const std::string& s) {
    return sha256_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed = 0);
uint32_t adler32(const uint8_t* data, size_t size);

std::string base64_encode(const Bytes& data);
// Whitespace is ignored; any other non-alphabet character or bad padding fails.
bool base64_decode(const std::string& text, Bytes& out);

inline Bytes to_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline std::string to_string(const Bytes& b) { return std::string(b.begin(), b.end()); }

}  // namespace tt
