// See media_session.h. WRITTEN, NOT YET COMPILED OR VERIFIED ON WINDOWS.
#include "media_session.h"

#ifdef _MSC_VER
// ---------------------------------------------------------------------------------------
// MSVC only: C++/WinRT headers from the Windows SDK (10.0.18362 or newer for
// Windows.Media.Control). The MinGW cross build takes the stub in the #else branch.
// ---------------------------------------------------------------------------------------
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>  // needed to iterate GetSessions() (IVectorView)
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace app {
namespace {

namespace wmc = winrt::Windows::Media::Control;
using Session = wmc::GlobalSystemMediaTransportControlsSession;
using Manager = wmc::GlobalSystemMediaTransportControlsSessionManager;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

constexpr auto kPollInterval = std::chrono::milliseconds(500);
constexpr auto kRetryInterval = std::chrono::milliseconds(2000);
constexpr unsigned long long kMaxSampleAgeMs = 5000;  // worker stalled -> report unavailable

bool contains_spotify(const std::string& s) {
    std::string l(s);
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return l.find("spotify") != std::string::npos;
}

// Bounded wait for an async WinRT operation; throws on timeout so the loop re-initialises.
template <typename Op>
auto get_with_timeout(Op const& op, std::chrono::milliseconds timeout) {
    if (op.wait_for(timeout) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        op.Cancel();
        throw std::runtime_error("SMTC call timed out");
    }
    return op.GetResults();
}

class Worker {
public:
    // Intentionally leaked: a detached-lifetime singleton avoids static-destruction races
    // with the refresh thread during process exit.
    static Worker& instance() {
        static Worker* w = new Worker();
        return *w;
    }

    MediaState read() {
        std::call_once(once_, [this] { thread_ = std::thread([this] { run(); }); });
        std::lock_guard<std::mutex> lk(m_);
        MediaState s = state_;
        if (!s.available) return s;
        unsigned long long age = GetTickCount64() - s.sampled_tick_ms;
        if (age > kMaxSampleAgeMs) {
            s.available = false;
            return s;
        }
        if (s.playing) {
            s.position_ms += static_cast<long long>(age);
            if (s.duration_ms > 0 && s.position_ms > s.duration_ms) s.position_ms = s.duration_ms;
        }
        return s;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(cv_m_);
            stop_ = true;
        }
        cv_.notify_all();
        std::call_once(once_, [] {});  // if never started, make sure it never will
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lk(m_);
        state_ = MediaState();
    }

private:
    void publish(const MediaState& s) {
        std::lock_guard<std::mutex> lk(m_);
        state_ = s;
    }

    bool wait_for_next(std::chrono::milliseconds d) {  // false when stopping
        std::unique_lock<std::mutex> lk(cv_m_);
        cv_.wait_for(lk, d, [this] { return stop_; });
        return !stop_;
    }

    void run() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            publish(MediaState());  // WinRT unavailable: caller falls back to the window title
            return;
        }
        Manager mgr{nullptr};
        for (;;) {
            bool ok = false;
            try {
                if (!mgr) mgr = get_with_timeout(Manager::RequestAsync(), std::chrono::seconds(3));
                sample(mgr);
                ok = true;
            } catch (...) {
                mgr = nullptr;
                publish(MediaState());
            }
            if (!wait_for_next(ok ? kPollInterval : kRetryInterval)) break;
        }
        mgr = nullptr;
        winrt::uninit_apartment();
    }

    void sample(Manager const& mgr) {
        Session chosen{nullptr};
        for (auto const& s : mgr.GetSessions()) {
            if (contains_spotify(winrt::to_string(s.SourceAppUserModelId()))) {
                chosen = s;
                break;
            }
        }
        if (!chosen) chosen = mgr.GetCurrentSession();
        if (!chosen) {
            publish(MediaState());  // nothing is registered with SMTC
            return;
        }

        auto props = get_with_timeout(chosen.TryGetMediaPropertiesAsync(), std::chrono::seconds(2));
        auto info = chosen.GetPlaybackInfo();
        auto tl = chosen.GetTimelineProperties();

        MediaState s;
        s.app_id = winrt::to_string(chosen.SourceAppUserModelId());
        s.artist = winrt::to_string(props.Artist());
        s.title = winrt::to_string(props.Title());
        s.playing = info.PlaybackStatus() ==
                    wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

        double rate = 1.0;
        if (auto r = info.PlaybackRate()) {
            double v = r.Value();
            if (v > 0.0 && v <= 4.0) rate = v;
        }

        long long start_ms = duration_cast<milliseconds>(tl.StartTime()).count();
        long long end_ms = duration_cast<milliseconds>(tl.EndTime()).count();
        long long pos = duration_cast<milliseconds>(tl.Position()).count() - start_ms;
        s.duration_ms = end_ms > start_ms ? end_ms - start_ms : 0;

        // Track-change bookkeeping: right after a change some players still carry the
        // previous track's timeline for a moment. If the timeline was last updated before
        // this track was first seen, do not trust it; estimate from the time since the change.
        const auto now_c = winrt::clock::now();
        const std::string key = s.artist + "\n" + s.title;
        if (key != key_) {
            key_ = key;
            changed_at_ = now_c;
            watched_change_ = have_key_;  // false for the very first track seen at startup
            have_key_ = true;
        }
        const auto upd = tl.LastUpdatedTime();
        const bool stale = watched_change_ && upd < changed_at_ - std::chrono::seconds(1);
        if (stale) {
            pos = s.playing ? duration_cast<milliseconds>(now_c - changed_at_).count() : 0;
        } else if (s.playing) {
            auto age = duration_cast<milliseconds>(now_c - upd).count();
            if (age < 0 || age > 6LL * 3600 * 1000) age = 0;  // clock oddity: ignore
            pos += static_cast<long long>(static_cast<double>(age) * rate);
        }
        if (pos < 0) pos = 0;
        if (s.duration_ms > 0 && pos > s.duration_ms) pos = s.duration_ms;

        s.position_ms = pos;
        s.sampled_tick_ms = GetTickCount64();
        s.available = true;
        publish(s);
    }

    std::once_flag once_;
    std::thread thread_;
    std::mutex m_;
    MediaState state_;
    std::mutex cv_m_;
    std::condition_variable cv_;
    bool stop_ = false;
    // refresh-thread only:
    std::string key_;
    bool have_key_ = false;
    bool watched_change_ = false;
    winrt::clock::time_point changed_at_{};
};

}  // namespace

MediaState read_media_state() { return Worker::instance().read(); }
void shutdown_media_session() { Worker::instance().stop(); }

}  // namespace app

#else  // ---------------------------------------------------------------------------------
// Not MSVC (e.g. the MinGW cross build): no SMTC, so the app uses the window-title reader.
namespace app {
MediaState read_media_state() { return MediaState(); }
void shutdown_media_session() {}
}  // namespace app
#endif
