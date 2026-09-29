// Bounded whole-file read / atomic-ish write helpers using UTF-8 paths
// (std::filesystem converts to the native wide API on Windows).
#pragma once
#include <string>

#include "codec.h"

namespace tt {

// Fails if the file cannot be opened or is larger than max_bytes.
bool read_file(const std::string& utf8_path, Bytes& out, size_t max_bytes, std::string& error);
// Writes to "<path>.tmp" then renames over the destination.
bool write_file(const std::string& utf8_path, const Bytes& data, std::string& error);
bool file_exists(const std::string& utf8_path);

}  // namespace tt
