// Notification packets for the TomTom renderer's placeholder notification
// receiver (see docs/ARCHITECTURE.md): one UDP datagram to the TomTom's USB
// address, port 45872, shaped
//
//     OT1|N|<seconds>|<printable ASCII message>
//
// TTL is 1-60 seconds and the text at most 32 printable ASCII characters. The
// renderer only accepts packets from the USB subnet; this sender additionally
// refuses to send anywhere except private / link-local / loopback IPv4 addresses.
#pragma once
#include <string>

namespace tt {

constexpr int kNotifyPort = 45872;
constexpr int kNotifyMaxChars = 32;
constexpr int kNotifyMinTtl = 1;
constexpr int kNotifyMaxTtl = 60;

// Same transliteration without the 32-character limit (stops after `limit` characters).
std::string to_printable_ascii(const std::string& utf8, size_t limit = 512);

// UTF-8 in, printable ASCII (0x20-0x7E) out, at most 32 characters. Common Latin
// accents are transliterated (e -> e), typographic punctuation is mapped, anything
// else becomes '?', whitespace is collapsed and '|' is replaced. Text longer than
// the limit is shortened with "...".
std::string sanitize_notification_text(const std::string& utf8);

// True when a sanitised string has something worth showing (not empty, not only '?' placeholders
// left behind by characters the TomTom cannot display, e.g. an all-emoji or all-CJK message).
bool has_displayable_text(const std::string& sanitized);

// Sanitises `text`, clamps `ttl_seconds` to 1-60 and builds the datagram.
// Fails (with a message) when nothing printable remains.
bool build_notification_packet(int ttl_seconds, const std::string& text, std::string& packet, std::string& error);

// Builds and sends one datagram to <host>:45872 (host as accepted by parse_device_host).
// `port` is overridable for loopback testing only. Never blocks on the network.
bool send_notification(const std::string& host, int ttl_seconds, const std::string& text, std::string& error,
                       int port = kNotifyPort);

}  // namespace tt
