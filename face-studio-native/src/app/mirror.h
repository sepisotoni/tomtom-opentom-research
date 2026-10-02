// Screen / window mirroring to the TomTom's experimental display receiver.
//
// Captures a monitor or a single window with GDI, scales it to the 320x240 panel,
// converts it to RGB565 and hands it to tt::display::FrameTransport, which sends at
// most ~9 frames per second to the TomTom's USB address only (192.168.101.115:18745,
// hard-coded in the transport; no host name, no LAN target). Nothing is recorded or
// stored. The TomTom-side receiver must already be running with the normal watchface
// renderer stopped - both write /dev/fb0 (see docs/DISPLAY_STREAM.md).
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../display/frame_transport.h"
#include "../display/mirror_lease.h"

namespace app {

enum class MirrorFit { Letterbox, Fill, Stretch };

struct MirrorSource {
    bool is_window = false;
    HMONITOR monitor = nullptr;
    HWND window = nullptr;
    std::wstring label;
};

// Monitors first ("Screen 1 (1920x1080) - primary"), then visible titled top-level windows.
std::vector<MirrorSource> list_mirror_sources();

class Mirror {
public:
    ~Mirror();
    bool start(const MirrorSource& source, MirrorFit fit, std::string& error);
    void stop();
    bool running() const { return running_; }
    // One line for the UI: capture problem, or the transport's connection state.
    std::string status_text() const;
    unsigned long long frames_captured() const { return frames_; }

private:
    void run();
    void set_note(const std::string& s);

    MirrorSource source_;
    MirrorFit fit_ = MirrorFit::Letterbox;
    tt::display::FrameTransport transport_;
    tt::display::MirrorLease lease_;  // keeps the IDD driver off the receiver's single TCP client slot while we mirror
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<unsigned long long> frames_{0};
    mutable std::mutex mutex_;
    std::string note_;
};

}  // namespace app
