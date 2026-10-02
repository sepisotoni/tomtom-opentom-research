// Time-synced lyrics lookup against LRCLIB (https://lrclib.net), a free community
// lyrics database that needs no account or key. Sends only the artist and track
// name over HTTPS; the response is held in memory and never stored. Blocking:
// call it from a worker thread.
#pragma once
#include <string>

namespace app {

// On success `lrc` holds the synced lyrics in LRC format.
bool fetch_synced_lyrics(const std::string& artist, const std::string& title, std::string& lrc, std::string& error);

}  // namespace app
