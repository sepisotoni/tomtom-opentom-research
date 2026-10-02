// Driver-side control channel: the single place where the IDD decides whether it may hold the
// TomTom receiver's one TCP connection, and the server for `TomTomDisplayControl.exe status|pause|resume`.
//
// Rule (details in windows-idd/README.md): the driver streams only when
//   - no explicit PAUSE is in effect, and
//   - no Studio mirror lease event (tt::idd_ipc::kMirrorLeaseEventName) exists.
// When either holds it drops its TCP connection (FrameTransport::set_paused) and stops submitting frames.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "../src/display/frame_transport.h"

namespace tt::idd {

class ControlChannel {
public:
    ControlChannel() noexcept = default;
    ~ControlChannel();

    ControlChannel(const ControlChannel&) = delete;
    ControlChannel& operator=(const ControlChannel&) = delete;

    // Evaluates the gate once, creates the control pipe (it exists when start() returns) and starts the
    // service thread. Idempotent.
    // Returns false only if the thread could not be started; a pipe that cannot be created (for
    // example a squatted name) is logged and only disables status/pause, never the lease rule.
    bool start() noexcept;
    void stop() noexcept;

    // The active transport is driven by the gate (set_paused) while attached. Detach before the
    // transport is destroyed; its frame count is folded into the running total.
    void attach(tt::display::FrameTransport* transport) noexcept;
    void detach(tt::display::FrameTransport* transport) noexcept;

    // Lock-free; true when the driver may submit frames right now.
    bool streaming_allowed() const noexcept { return allowed_.load(); }

    // Poll interval for the lease probe, and I/O deadline for one control-pipe exchange.
    static constexpr DWORD kGatePollMs = 200;
    static constexpr DWORD kPipeIoTimeoutMs = 500;

private:
    void run() noexcept;
    HANDLE create_pipe() noexcept;
    void serve(HANDLE pipe) noexcept;
    void evaluate_gate() noexcept;
    void apply_gate_locked() noexcept;
    bool lease_present() const noexcept;

    std::mutex mutex_;
    tt::display::FrameTransport* transport_ = nullptr;
    std::uint64_t frames_base_ = 0;
    bool paused_ = false;
    bool lease_ = false;
    // Set once in start(): whether ACCESS_DENIED from OpenEventW really means "the object exists".
    bool denied_means_exists_ = true;
    std::atomic<bool> allowed_{true};

    std::thread thread_;
    HANDLE stop_event_ = nullptr;
    HANDLE connect_event_ = nullptr;
    HANDLE io_event_ = nullptr;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;  // created in start(), served and closed by the service thread's owner
};

}  // namespace tt::idd
