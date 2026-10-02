// Spotify desktop now-playing, read from the Spotify window title (no account,
// no API key, nothing is sent anywhere). While a track plays the title is
// "Artist - Title"; when paused or idle Spotify shows its own name instead.
#pragma once
#include <string>

namespace app {

struct NowPlaying {
    bool running = false;  // a Spotify.exe window was found
    bool playing = false;  // the title looks like "Artist - Title"
    std::string artist;    // UTF-8
    std::string title;     // UTF-8
};

NowPlaying read_spotify_now_playing();

}  // namespace app
