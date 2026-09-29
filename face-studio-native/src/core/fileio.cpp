#include "fileio.h"

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tt {

#ifdef _WIN32
static std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
}
static FILE* open_file(const std::string& path, bool write) {
    return _wfopen(widen(path).c_str(), write ? L"wb" : L"rb");
}
static void remove_file(const std::string& path) { DeleteFileW(widen(path).c_str()); }
#else
static FILE* open_file(const std::string& path, bool write) { return std::fopen(path.c_str(), write ? "wb" : "rb"); }
static void remove_file(const std::string& path) { ::unlink(path.c_str()); }
#endif

bool file_exists(const std::string& utf8_path) {
#ifdef _WIN32
    return GetFileAttributesW(widen(utf8_path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return ::stat(utf8_path.c_str(), &st) == 0;
#endif
}

bool read_file(const std::string& utf8_path, Bytes& out, size_t max_bytes, std::string& error) {
    out.clear();
    FILE* f = open_file(utf8_path, false);
    if (!f) {
        error = "Cannot open '" + utf8_path + "'.";
        return false;
    }
    // Bounded chunked read: an oversized file is rejected without being loaded.
    uint8_t chunk[65536];
    bool ok = true;
    while (true) {
        size_t n = std::fread(chunk, 1, sizeof chunk, f);
        if (n == 0) break;
        if (out.size() + n > max_bytes) {
            error = "File '" + utf8_path + "' is larger than the permitted " + std::to_string(max_bytes) + " bytes.";
            ok = false;
            break;
        }
        out.insert(out.end(), chunk, chunk + n);
    }
    if (ok && std::ferror(f)) {
        error = "Cannot read '" + utf8_path + "'.";
        ok = false;
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

bool write_file(const std::string& utf8_path, const Bytes& data, std::string& error) {
    const std::string tmp_path = utf8_path + ".tmp";
    FILE* f = open_file(tmp_path, true);
    if (!f) {
        error = "Cannot create '" + utf8_path + "'.";
        return false;
    }
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
        error = "Cannot write '" + utf8_path + "'.";
        remove_file(tmp_path);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(widen(tmp_path).c_str(), widen(utf8_path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (std::rename(tmp_path.c_str(), utf8_path.c_str()) != 0) {
#endif
        error = "Cannot replace '" + utf8_path + "'.";
        remove_file(tmp_path);
        return false;
    }
    return true;
}

}  // namespace tt
