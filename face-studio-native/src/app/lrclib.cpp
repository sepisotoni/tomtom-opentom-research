#include "lrclib.h"

#include <windows.h>
#include <winhttp.h>

#include "json.h"

namespace app {

namespace {

constexpr size_t kMaxResponseBytes = 1024 * 1024;

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

}  // namespace

bool fetch_synced_lyrics(const std::string& artist, const std::string& title, std::string& lrc, std::string& error) {
    lrc.clear();
    if (artist.empty() || title.empty()) { error = "No track to look up."; return false; }
    Handle session, connection, request;
    session.h = WinHttpOpen(L"TomTomFaceStudio/1.0 (+https://github.com/sepisotoni/tomtom-opentom-research)",
                            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { error = "Could not start the HTTP client."; return false; }
    WinHttpSetTimeouts(session.h, 4000, 4000, 5000, 6000);
    connection.h = WinHttpConnect(session.h, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection.h) { error = "Could not reach lrclib.net."; return false; }
    std::wstring path = widen("/api/get?artist_name=" + url_encode(artist) + "&track_name=" + url_encode(title));
    request.h = WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   WINHTTP_FLAG_SECURE);
    if (!request.h) { error = "Could not create the lyrics request."; return false; }
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "The lyrics service did not answer (offline?).";
        return false;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status == 404) { error = "No lyrics found for this track."; return false; }
    if (status != 200) { error = "The lyrics service returned HTTP " + std::to_string(status) + "."; return false; }

    std::string body;
    while (true) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail) || avail == 0) break;
        if (body.size() + avail > kMaxResponseBytes) { error = "The lyrics response was too large."; return false; }
        std::string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(request.h, &chunk[0], avail, &got) || got == 0) break;
        body.append(chunk, 0, got);
    }
    tt::Json j;
    std::string perr;
    if (!tt::Json::parse(body, j, perr) || !j.is_object()) { error = "The lyrics response was not understood."; return false; }
    const tt::Json& synced = j.get("syncedLyrics");
    if (synced.is_string() && !synced.as_string().empty()) { lrc = synced.as_string(); return true; }
    error = j.get("instrumental").truthy() ? "This track is instrumental." : "No time-synced lyrics are available for this track.";
    return false;
}

}  // namespace app
