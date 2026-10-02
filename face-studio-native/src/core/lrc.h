// Parser for time-synced lyrics in LRC format ("[mm:ss.xx] text"), plus helpers
// to fit a line onto the TomTom's 32-character notification slot. Lyrics are
// held in memory only and are never written to disk by this module.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tt {

struct LyricLine {
    int64_t start_ms = 0;
    std::string text;  // UTF-8; empty text marks an instrumental gap
};

constexpr size_t kMaxLyricLines = 2000;
constexpr size_t kMaxLyricLineBytes = 240;

// Lines are returned sorted by start time. Metadata tags ([ar:], [ti:], ...),
// malformed timestamps and lines beyond the limits are skipped. A line with
// several timestamps ("[00:10.00][01:10.00]text") is expanded.
std::vector<LyricLine> parse_lrc(const std::string& lrc);

// Index of the last line whose start time is <= position_ms, or -1 before the first line.
int find_lyric_line(const std::vector<LyricLine>& lines, int64_t position_ms);

// Splits `text` into chunks of at most `max_chars` printable-ASCII characters at word
// boundaries (text is first passed through sanitize_notification_text rules, without
// its overall truncation). Returns an empty vector for blank text.
std::vector<std::string> split_for_display(const std::string& text, size_t max_chars = 32);

}  // namespace tt
