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
    bool submit_frame(const std::uint8_t* frame,
                      std::size_t frame_bytes) noexcept;
    TransportState state() const noexcept;

private:
    void run() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::array<std::uint8_t, kFrameBytes> latest_frame_{};
    std::thread worker_;
    std::atomic<TransportState> state_{TransportState::stopped};
    bool running_ = false;
    bool has_frame_ = false;
    std::uint32_t next_sequence_ = 1;
};

}  // namespace tt::display
