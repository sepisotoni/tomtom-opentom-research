// Decides whether a staged driver INF (%WINDIR%\INF\oemNN.inf) is OUR package. Platform-neutral so it
// is unit-tested on every platform (tests/test_idd_ipc.cpp). Used by `TomTomDisplayControl.exe uninstall`
// so it never has to parse the localized text output of `pnputil /enum-drivers`.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tt::idd {

// Staged INFs are usually UTF-16LE with a BOM, but plain ANSI/UTF-8 is accepted too. Non-ASCII
// characters are irrelevant for the ASCII needles below and are replaced by '?'.
inline std::string inf_to_lower_ascii(std::string_view bytes) {
    std::string out;
    const bool utf16le = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
                         static_cast<unsigned char>(bytes[1]) == 0xFE;
    auto lower = [](unsigned char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    };
    if (utf16le) {
        out.reserve(bytes.size() / 2);
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const unsigned char low = static_cast<unsigned char>(bytes[i]);
            const unsigned char high = static_cast<unsigned char>(bytes[i + 1]);
            out += (high == 0 && low < 0x80) ? lower(low) : '?';
        }
    } else {
        out.reserve(bytes.size());
        for (const char c : bytes) {
            const unsigned char u = static_cast<unsigned char>(c);
            out += u < 0x80 ? lower(u) : '?';
        }
    }
    return out;
}

// True only if the INF names both our hardware ID and our driver binary. Either alone is not enough,
// so an unrelated package that merely mentions one of them is never selected for deletion.
inline bool inf_belongs_to_tomtom_idd(std::string_view bytes) {
    const std::string text = inf_to_lower_ascii(bytes);
    return text.find("root\\tomtomindirectdisplay") != std::string::npos &&
           text.find("tomtomidd.dll") != std::string::npos;
}

}  // namespace tt::idd
