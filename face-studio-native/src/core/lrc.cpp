#include "lrc.h"

#include <algorithm>
#include <cctype>

#include "notify.h"

namespace tt {

namespace {

// Parses "mm:ss", "mm:ss.f", "mm:ss.ff" or "mm:ss.fff" starting at s[pos] up to (not including) ']'.
bool parse_timestamp(const std::string& s, size_t pos, size_t close, int64_t& ms) {
    size_t colon = s.find(':', pos);
    if (colon == std::string::npos || colon >= close || colon == pos || colon - pos > 3) return false;
    int64_t minutes = 0;
    for (size_t i = pos; i < colon; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        minutes = minutes * 10 + (s[i] - '0');
    }
    size_t sec_start = colon + 1;
    size_t dot = s.find_first_of(".:", sec_start);
    size_t sec_end = (dot == std::string::npos || dot > close) ? close : dot;
    if (sec_end == sec_start || sec_end - sec_start > 2) return false;
    int64_t seconds = 0;
    for (size_t i = sec_start; i < sec_end; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        seconds = seconds * 10 + (s[i] - '0');
    }
    if (seconds >= 60) return false;
    int64_t frac_ms = 0;
    if (sec_end < close) {
        size_t f = sec_end + 1;
        size_t digits = close - f;
        if (digits < 1 || digits > 3) return false;
        int64_t v = 0;
        for (size_t i = f; i < close; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
            v = v * 10 + (s[i] - '0');
        }
        frac_ms = digits == 1 ? v * 100 : digits == 2 ? v * 10 : v;
    }
    ms = (minutes * 60 + seconds) * 1000 + frac_ms;
    return true;
}

}  // namespace

std::vector<LyricLine> parse_lrc(const std::string& lrc) {
    std::vector<LyricLine> out;
    size_t pos = 0;
    while (pos < lrc.size() && out.size() < kMaxLyricLines) {
        size_t eol = lrc.find('\n', pos);
        if (eol == std::string::npos) eol = lrc.size();
        std::string line = lrc.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

        std::vector<int64_t> stamps;
        size_t p = 0;
        while (p < line.size() && line[p] == '[') {
            size_t close = line.find(']', p);
            if (close == std::string::npos) break;
            int64_t ms;
            if (parse_timestamp(line, p + 1, close, ms)) stamps.push_back(ms);
            else if (stamps.empty()) { p = line.size(); break; }  // metadata tag such as [ar:Artist]
            else break;
            p = close + 1;
        }
        if (stamps.empty()) continue;
        std::string text = line.substr(p);
        size_t a = 0;
        while (a < text.size() && text[a] == ' ') ++a;
        text = text.substr(a);
        if (text.size() > kMaxLyricLineBytes) text.resize(kMaxLyricLineBytes);
        for (int64_t ms : stamps) {
            if (out.size() >= kMaxLyricLines) break;
            out.push_back(LyricLine{ms, text});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const LyricLine& a, const LyricLine& b) { return a.start_ms < b.start_ms; });
    return out;
}

int find_lyric_line(const std::vector<LyricLine>& lines, int64_t position_ms) {
    int lo = 0, hi = static_cast<int>(lines.size()) - 1, best = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (lines[static_cast<size_t>(mid)].start_ms <= position_ms) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}

std::vector<std::string> split_for_display(const std::string& text, size_t max_chars) {
    std::vector<std::string> chunks;
    if (max_chars == 0) return chunks;
    std::string ascii = to_printable_ascii(text, 1024);
    size_t i = 0;
    while (i < ascii.size()) {
        while (i < ascii.size() && ascii[i] == ' ') ++i;
        if (i >= ascii.size()) break;
        size_t remaining = ascii.size() - i;
        if (remaining <= max_chars) {
            chunks.push_back(ascii.substr(i));
            break;
        }
        // Break at the last space within the window; hard-break overlong words.
        size_t cut = ascii.rfind(' ', i + max_chars);
        if (cut == std::string::npos || cut <= i) cut = i + max_chars;
        std::string piece = ascii.substr(i, cut - i);
        while (!piece.empty() && piece.back() == ' ') piece.pop_back();
        if (!piece.empty()) chunks.push_back(piece);
        i = cut;
    }
    return chunks;
}

}  // namespace tt
