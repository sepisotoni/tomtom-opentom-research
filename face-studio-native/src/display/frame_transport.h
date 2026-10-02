#pragma once

#include "frame_converter.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace tt::display {

enum class TransportState {
    stopped,
    waiting_for_frame,
    connecting,
    streaming,
    unavailable
};

class FrameTransport {
public:
    FrameTransport() = default;
    ~FrameTransport();

    FrameTransport(const FrameTransport&) = delete;
    FrameTransport& operator=(const FrameTransport&) = delete;

    bool start() noexcept;
    void stop() noexcept;
    // Returns false (and drops the frame) when the transport is stopped, paused, or the size is wrong.
    bool submit_frame(const std::uint8_t* frame,
                      std::size_t frame_bytes) noexcept;
    TransportState state() const noexcept;

    // Pausing makes the worker close its TCP connection to the receiver (so another client, e.g. the
    // Studio's mirror, can use the one-client receiver), discards any pending frame, and rejects new
    // frames until resumed. The flag survives stop()/start() and may be set while stopped. A frame that
    // is already being sent completes first (bounded by the 2 s I/O deadline). While paused, state()
    // reports waiting_for_frame; the TransportState enum is unchanged on purpose.
    void set_paused(bool paused) noexcept;
    bool paused() const noexcept;

    // Frames the receiver has acknowledged as accepted since construction. Never reset by stop()/start().
    std::uint64_t frames_sent() const noexcept;

private:
    void run() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::array<std::uint8_t, kFrameBytes> latest_frame_{};
    std::thread worker_;
    std::atomic<TransportState> state_{TransportState::stopped};
    std::atomic<std::uint64_t> frames_sent_{0};
    bool running_ = false;
    bool has_frame_ = false;
    bool paused_ = false;
    std::uint32_t next_sequence_ = 1;
};

}  // namespace tt::display
