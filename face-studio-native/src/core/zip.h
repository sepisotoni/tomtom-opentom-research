// Minimal ZIP container support: what .ttface / .ttgallery packages need.
// Reads STORED and DEFLATE entries through the central directory (no ZIP64,
// no encryption, no multi-disk); writes deterministic archives.
#pragma once
#include <string>
#include <vector>

#include "codec.h"

namespace tt {

struct ZipEntry {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compressed_size = 0;
    uint32_t size = 0;
    uint32_t external_attr = 0;
    uint32_t local_offset = 0;
    bool is_dir() const { return !name.empty() && name.back() == '/'; }
    bool is_symlink() const { return ((external_attr >> 16) & 0170000u) == 0120000u; }
};

class ZipReader {
public:
    // Takes ownership of the archive bytes.
    bool open(Bytes data, std::string& error);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    // Extracts one entry after checking method, sizes and CRC-32.
    bool read(const ZipEntry& entry, Bytes& out, std::string& error) const;
    // Last entry with this name (Python zipfile semantics); nullptr if absent.
    const ZipEntry* find(const std::string& name) const;
    bool read_named(const std::string& name, Bytes& out, std::string& error) const;

private:
    Bytes data_;
    std::vector<ZipEntry> entries_;
};

class ZipWriter {
public:
    // Deflates the entry when that is smaller than storing it.
    void add(const std::string& name, const Bytes& data);
    Bytes finish() const;

private:
    struct Item {
        std::string name;
        uint32_t crc, size;
        uint16_t method;
        Bytes payload;
    };
    std::vector<Item> items_;
};

}  // namespace tt
