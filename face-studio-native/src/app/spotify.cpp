#include "spotify.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace app {

namespace {

std::string utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

bool is_spotify_process(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t path[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(sizeof path / sizeof path[0]);
    bool ok = QueryFullProcessImageNameW(h, 0, path, &len) != 0;
    CloseHandle(h);
    if (!ok) return false;
    std::wstring p(path, len);
    size_t slash = p.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? p : p.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name == L"spotify.exe";
}

struct Search {
    NowPlaying result;
    std::wstring title;  // best candidate title
};

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lp) {
    Search* s = reinterpret_cast<Search*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !is_spotify_process(pid)) return TRUE;
    s->result.running = true;
    int n = GetWindowTextLengthW(hwnd);
    if (n <= 0 || n > 512) return TRUE;
    std::wstring t(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, &t[0], n + 1);
    t.resize(static_cast<size_t>(n));
    // Prefer a title that looks like a track over the idle "Spotify ..." caption.
    if (t.find(L" - ") != std::wstring::npos) s->title = t;
    else if (s->title.empty()) s->title = t;
    return TRUE;
}

}  // namespace

NowPlaying read_spotify_now_playing() {
    Search s;
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&s));
    NowPlaying np = s.result;
    size_t sep = s.title.find(L" - ");
    if (np.running && sep != std::wstring::npos && sep > 0 && sep + 3 < s.title.size()) {
        std::wstring artist = s.title.substr(0, sep), title = s.title.substr(sep + 3);
        if (artist != L"Spotify" && title != L"Premium" && title != L"Free") {
            np.playing = true;
            np.artist = utf8(artist);
            np.title = utf8(title);
        }
    }
    return np;
}

}  // namespace app
