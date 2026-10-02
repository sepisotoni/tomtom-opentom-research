// Now-playing state from the Windows System Media Transport Controls (SMTC), the same
// source the Windows volume flyout uses. Unlike the window-title reader in spotify.h it
// provides the playback POSITION and DURATION, so lyric sync does not have to be guessed.
//
// Everything stays on this PC: nothing is sent anywhere, nothing is written to disk.
// No WinRT types appear in this header. The implementation (media_session.cpp) is real
// only under MSVC (C++/WinRT); under any other compiler the same file builds a stub that
// always returns available=false, so the caller simply falls back to read_spotify_now_playing().
#pragma once
#include <string>

namespace app {

struct MediaState {
    bool available = false;   // a session was read recently; false = use the window-title fallback
    bool playing = false;     // PlaybackStatus == Playing
    std::string app_id;       // SourceAppUserModelId, e.g. "Spotify.exe" (UTF-8)
    std::string artist;       // UTF-8
    std::string title;        // UTF-8
    long long position_ms = 0;   // extrapolated to the moment of this call while playing
    long long duration_ms = 0;   // 0 when the source does not report one
    unsigned long long sampled_tick_ms = 0;  // GetTickCount64() of the last real SMTC sample
};

// Non-blocking: copies a mutex-guarded cache (microseconds). The first call starts a
// background refresh thread (about every 500 ms); until its first sample lands the result
// is available=false. Prefers a session whose app id contains "spotify", else the
// system's current session. Safe to call from the UI thread.
MediaState read_media_state();

// Optional: stops and joins the refresh thread (waits at most a few seconds). After this,
// read_media_state() returns available=false for the rest of the process.
void shutdown_media_session();

}  // namespace app
