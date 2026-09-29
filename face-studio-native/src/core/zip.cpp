#include "zip.h"

#include <cstring>

#include "deflate.h"

namespace tt {

namespace {
inline uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void wr16(Bytes& b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
inline void wr32(Bytes& b, uint32_t v) { wr16(b, v & 0xFFFF); wr16(b, v >> 16); }
constexpr size_t kMaxEntries = 4096;
}  // namespace

bool ZipReader::open(Bytes data, std::string& error) {
    data_ = std::move(data);
    entries_.clear();
    const size_t n = data_.size();
    if (n < 22) { error = "File is not a zip file"; return false; }
    // Locate the end-of-central-directory record (no archive comment expected).
    size_t eocd = std::string::npos;
    size_t lowest = n > 22 + 65535 ? n - 22 - 65535 : 0;
    for (size_t p = n - 22 + 1; p-- > lowest;) {
        if (rd32(&data_[p]) == 0x06054b50u) { eocd = p; break; }
    }
    if (eocd == std::string::npos) { error = "File is not a zip file"; return false; }
    const uint8_t* e = &data_[eocd];
    uint16_t disk = rd16(e + 4), cd_disk = rd16(e + 6), on_disk = rd16(e + 8), total = rd16(e + 10);
    uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (disk != 0 || cd_disk != 0 || on_disk != total) { error = "Multi-disk archives are not supported"; return false; }
    if (total == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) { error = "ZIP64 archives are not supported"; return false; }
    if (total > kMaxEntries) { error = "Archive has too many entries"; return false; }
    if (uint64_t(cd_off) + cd_size > eocd) { error = "Bad central directory"; return false; }

    size_t p = cd_off;
    for (uint16_t k = 0; k < total; ++k) {
        if (p + 46 > cd_off + size_t(cd_size) || rd32(&data_[p]) != 0x02014b50u) { error = "Bad central directory entry"; return false; }
        const uint8_t* c = &data_[p];
        ZipEntry ze;
        ze.flags = rd16(c + 8);
        ze.method = rd16(c + 10);
        ze.crc = rd32(c + 16);
        ze.compressed_size = rd32(c + 20);
        ze.size = rd32(c + 24);
        uint16_t nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32);
        ze.external_attr = rd32(c + 38);
        ze.local_offset = rd32(c + 42);
        if (p + 46 + nlen + xlen + clen > cd_off + size_t(cd_size)) { error = "Bad central directory entry"; return false; }
        if (ze.compressed_size == 0xFFFFFFFFu || ze.size == 0xFFFFFFFFu || ze.local_offset == 0xFFFFFFFFu) {
            error = "ZIP64 archives are not supported";
            return false;
        }
        ze.name.assign(reinterpret_cast<const char*>(c + 46), nlen);
        entries_.push_back(std::move(ze));
        p += 46u + nlen + xlen + clen;
    }
    return true;
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    for (size_t k = entries_.size(); k-- > 0;)
        if (entries_[k].name == name) return &entries_[k];
    return nullptr;
}

bool ZipReader::read(const ZipEntry& ze, Bytes& out, std::string& error) const {
    out.clear();
    if (ze.flags & 1) { error = "File '" + ze.name + "' is encrypted"; return false; }
    size_t lo = ze.local_offset;
    if (lo + 30 > data_.size() || rd32(&data_[lo]) != 0x04034b50u) { error = "Bad local header for '" + ze.name + "'"; return false; }
    size_t start = lo + 30 + rd16(&data_[lo + 26]) + rd16(&data_[lo + 28]);
    if (start > data_.size() || ze.compressed_size > data_.size() - start) { error = "Truncated data for '" + ze.name + "'"; return false; }
    const uint8_t* src = &data_[start];
    if (ze.method == 0) {
        if (ze.compressed_size != ze.size) { error = "Bad stored size for '" + ze.name + "'"; return false; }
        out.assign(src, src + ze.size);
    } else if (ze.method == 8) {
        if (!inflate_raw(src, ze.compressed_size, out, ze.size) || out.size() != ze.size) {
            out.clear();
            error = "Bad compressed data for '" + ze.name + "'";
            return false;
        }
    } else {
        error = "Unsupported compression method for '" + ze.name + "'";
        return false;
    }
    if (crc32(out.data(), out.size()) != ze.crc) {
        out.clear();
        error = "Bad CRC-32 for file '" + ze.name + "'";
        return false;
    }
    return true;
}

bool ZipReader::read_named(const std::string& name, Bytes& out, std::string& error) const {
    const ZipEntry* ze = find(name);
    if (!ze) { error = "There is no item named '" + name + "' in the archive"; return false; }
    return read(*ze, out, error);
}

void ZipWriter::add(const std::string& name, const Bytes& data) {
    Item it;
    it.name = name;
    it.crc = crc32(data.data(), data.size());
    it.size = static_cast<uint32_t>(data.size());
    Bytes packed = deflate_raw(data.data(), data.size());
    if (packed.size() < data.size()) { it.method = 8; it.payload = std::move(packed); }
    else { it.method = 0; it.payload = data; }
    items_.push_back(std::move(it));
}

Bytes ZipWriter::finish() const {
    Bytes out;
    std::vector<uint32_t> offsets;
    const uint32_t kDosDate = 0x0021;  // 1980-01-01, keeps archives deterministic
    for (const Item& it : items_) {
        offsets.push_back(static_cast<uint32_t>(out.size()));
        wr32(out, 0x04034b50u);
        wr16(out, 20);
        wr16(out, 0);
        wr16(out, it.method);
        wr16(out, 0);
        wr16(out, kDosDate);
        wr32(out, it.crc);
        wr32(out, static_cast<uint32_t>(it.payload.size()));
        wr32(out, it.size);
        wr16(out, static_cast<uint32_t>(it.name.size()));
        wr16(out, 0);
        out.insert(out.end(), it.name.begin(), it.name.end());
        out.insert(out.end(), it.payload.begin(), it.payload.end());
    }
    uint32_t cd_start = static_cast<uint32_t>(out.size());
    for (size_t k = 0; k < items_.size(); ++k) {
        const Item& it = items_[k];
        wr32(out, 0x02014b50u);
        wr16(out, (3 << 8) | 20);
        wr16(out, 20);
        wr16(out, 0);
        wr16(out, it.method);
        wr16(out, 0);
        wr16(out, kDosDate);
        wr32(out, it.crc);
        wr32(out, static_cast<uint32_t>(it.payload.size()));
        wr32(out, it.size);
        wr16(out, static_cast<uint32_t>(it.name.size()));
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0);
        wr32(out, 0600u << 16);
        wr32(out, offsets[k]);
        out.insert(out.end(), it.name.begin(), it.name.end());
    }
    uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_start;
    wr32(out, 0x06054b50u);
    wr16(out, 0);
    wr16(out, 0);
    wr16(out, static_cast<uint32_t>(items_.size()));
    wr16(out, static_cast<uint32_t>(items_.size()));
    wr32(out, cd_size);
    wr32(out, cd_start);
    wr16(out, 0);
    return out;
}

}  // namespace tt
