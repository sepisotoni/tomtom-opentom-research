#include "notify.h"

#include <cstring>

#include "device_client.h"
#include "net_compat.h"

namespace tt {

namespace {

// Decodes one UTF-8 code point; invalid bytes decode as U+FFFD and advance by one.
unsigned next_codepoint(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || i + n > s.size()) { ++i; return 0xFFFD; }
    unsigned cp = n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t k = 1; k < n; ++k) {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    i += n;
    return cp;
}

// ASCII approximation for a code point; empty string means "no mapping".
const char* ascii_for(unsigned cp) {
    switch (cp) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0x100: case 0x102: case 0x104: return "A";
        case 0xC6: return "AE";
        case 0xC7: case 0x106: case 0x10C: return "C";
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0x112: case 0x118: case 0x11A: return "E";
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0x12A: case 0x130: return "I";
        case 0xD0: case 0x110: return "D";
        case 0xD1: case 0x143: case 0x147: return "N";
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: case 0x150: return "O";
        case 0x152: return "OE";
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0x16A: case 0x16E: case 0x170: return "U";
        case 0xDD: return "Y";
        case 0xDF: return "ss";
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0x101: case 0x103: case 0x105: return "a";
        case 0xE6: return "ae";
        case 0xE7: case 0x107: case 0x10D: return "c";
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0x113: case 0x119: case 0x11B: return "e";
        case 0xEC: case 0xED: case 0xEE: case 0xEF: case 0x12B: case 0x131: return "i";
        case 0xF1: case 0x144: case 0x148: return "n";
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: case 0x151: return "o";
        case 0x153: return "oe";
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0x16B: case 0x16F: case 0x171: return "u";
        case 0xFD: case 0xFF: return "y";
        case 0x141: return "L";
        case 0x142: return "l";
        case 0x15A: case 0x160: return "S";
        case 0x15B: case 0x161: return "s";
        case 0x179: case 0x17B: case 0x17D: return "Z";
        case 0x17A: case 0x17C: case 0x17E: return "z";
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
        case 0x2026: return "...";
        case 0x2022: case 0xB7: return "*";
        case 0xA0: case 0x2009: case 0x200A: case 0x202F: return " ";
        default: return "";
    }
}

}  // namespace

std::string to_printable_ascii(const std::string& utf8, size_t limit) {
    std::string out;
    bool last_space = true;  // trims leading whitespace
    bool last_unknown = false;
    size_t i = 0;
    auto put = [&](char c) {
        if (c == ' ') {
            if (!last_space) out.push_back(' ');
            last_space = true;
        } else {
            out.push_back(c);
            last_space = false;
        }
        last_unknown = false;
    };
    while (i < utf8.size() && out.size() < limit) {
        unsigned cp = next_codepoint(utf8, i);
        if (cp < 0x80) {
            if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n') put(' ');
            else if (cp == '|') put('/');
            else if (cp >= 0x21 && cp <= 0x7E) put(static_cast<char>(cp));
            // other control characters are dropped
            continue;
        }
        const char* mapped = ascii_for(cp);
        if (*mapped) {
            for (const char* p = mapped; *p; ++p) put(*p);
        } else if (!last_unknown) {  // collapse runs of unmappable characters (emoji, CJK, ...)
            put('?');
            last_unknown = true;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string sanitize_notification_text(const std::string& utf8) {
    std::string out = to_printable_ascii(utf8, 512);
    if (static_cast<int>(out.size()) > kNotifyMaxChars) {
        out.resize(static_cast<size_t>(kNotifyMaxChars) - 3);
        while (!out.empty() && out.back() == ' ') out.pop_back();
        out += "...";
    }
    return out;
}

bool has_displayable_text(const std::string& sanitized) {
    for (char c : sanitized)
        if (c != '?' && c != ' ') return true;
    return false;
}

bool build_notification_packet(int ttl_seconds, const std::string& text, std::string& packet, std::string& error) {
    std::string clean = sanitize_notification_text(text);
    if (!has_displayable_text(clean)) {
        error = "The message has no characters the TomTom can display (plain Latin text only).";
        return false;
    }
    int ttl = ttl_seconds < kNotifyMinTtl ? kNotifyMinTtl : (ttl_seconds > kNotifyMaxTtl ? kNotifyMaxTtl : ttl_seconds);
    packet = "OT1|N|" + std::to_string(ttl) + "|" + clean;
    return true;
}

bool send_notification(const std::string& host_text, int ttl_seconds, const std::string& text, std::string& error, int port) {
    std::string host;
    if (!parse_device_host(host_text, host, error)) return false;
    std::string packet;
    if (!build_notification_packet(ttl_seconds, text, packet, error)) return false;
    if (port < 1 || port > 65535) {
        error = "Invalid UDP port.";
        return false;
    }
    net_init();
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        error = "Invalid IPv4 address.";
        return false;
    }
    SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSocket) {
        error = "Could not create a network socket.";
        return false;
    }
    int sent = static_cast<int>(::sendto(s, packet.data(), static_cast<int>(packet.size()), 0,
                                         reinterpret_cast<sockaddr*>(&addr), sizeof addr));
    net_close(s);
    if (sent != static_cast<int>(packet.size())) {
        error = "Could not send the notification (is the USB network link up?).";
        return false;
    }
    return true;
}

}  // namespace tt
