#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "frame_transport.h"

#include "../../../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <system_error>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tt::display {
namespace {

#ifdef TT_DISPLAY_TRANSPORT_TEST_LOOPBACK
// Defined ONLY when building tests/test_transport_robustness.cpp (never for the app or the driver) so a
// test can talk to a fake receiver on this machine. There is no runtime way to change the address.
constexpr char kTomTomUsbAddress[] = "127.0.0.1";
#else
constexpr char kTomTomUsbAddress[] = "192.168.101.115";
#endif
constexpr unsigned short kTomTomDisplayPort = 18745;
constexpr int kIoTimeoutMs = 2000;
constexpr auto kFrameInterval = std::chrono::milliseconds(110);
constexpr auto kIdleDisconnect = std::chrono::seconds(5);
constexpr auto kRetryInitial = std::chrono::milliseconds(500);  // after a failed attempt ...
constexpr auto kRetryMax = std::chrono::seconds(2);             // ... doubling up to this while the receiver is away
constexpr int kCancelPollMs = 50;                               // how often blocking waits look at the abort flag

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
constexpr int kInterruptedError = WSAEINTR;

bool initialize_sockets() noexcept {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

void close_socket(Socket socket) noexcept {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}

int last_socket_error() noexcept {
    return WSAGetLastError();
}

bool would_block(int error) noexcept {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
constexpr int kInterruptedError = EINTR;

bool initialize_sockets() noexcept {
    return true;
}

void close_socket(Socket socket) noexcept {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}

int last_socket_error() noexcept {
    return errno;
}

bool would_block(int error) noexcept {
    return error == EINPROGRESS || error == EWOULDBLOCK ||
           error == EAGAIN;
}
#endif

int remaining_timeout_ms(
    std::chrono::steady_clock::time_point deadline) noexcept {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return 0;
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
    return static_cast<int>(std::max<std::int64_t>(
        1, milliseconds.count()));
}

// Waits for the socket in short slices so `cancel` (stop/pause) is honoured within ~50 ms.
bool wait_socket(Socket socket, bool writable, int timeout_ms,
                 const std::atomic<bool>& cancel) noexcept {
    int remaining = timeout_ms;
    for (;;) {
        if (cancel.load()) {
            return false;
        }
        const int slice = std::min(remaining, kCancelPollMs);
#ifdef _WIN32
        fd_set descriptors;
        fd_set failed;
        FD_ZERO(&descriptors);
        FD_ZERO(&failed);
        FD_SET(socket, &descriptors);
        FD_SET(socket, &failed);
        timeval timeout{};
        timeout.tv_sec = slice / 1000;
        timeout.tv_usec = (slice % 1000) * 1000;
        // Winsock reports a FAILED non-blocking connect in exceptfds, not writefds: watching it lets a
        // refused connection (receiver stopped, USB link up) fail at once instead of after the full
        // timeout. The caller still checks SO_ERROR, so "failed" and "connected" are told apart there.
        const int ready = select(0, writable ? nullptr : &descriptors,
                                 writable ? &descriptors : nullptr,
                                 writable ? &failed : nullptr, &timeout);
        if (ready > 0) {
            return true;
        }
        if (ready < 0) {
            return false;
        }
#else
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = writable ? POLLOUT : POLLIN;
        const int ready = poll(&descriptor, 1, slice);
        if (ready > 0) {
            return (descriptor.revents & (writable ? POLLOUT : POLLIN)) != 0;
        }
        if (ready < 0 && errno != EINTR) {
            return false;
        }
#endif
        remaining -= slice;
        if (remaining <= 0) {
            return false;
        }
    }
}

Socket connect_device(const std::atomic<bool>& cancel) noexcept {
    if (!initialize_sockets()) {
        return kInvalidSocket;
    }

    Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) {
        return kInvalidSocket;
    }

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
        close_socket(socket);
        return kInvalidSocket;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        close_socket(socket);
        return kInvalidSocket;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kTomTomDisplayPort);
    if (inet_pton(AF_INET, kTomTomUsbAddress, &address.sin_addr) != 1) {
        close_socket(socket);
        return kInvalidSocket;
    }

    const int result = ::connect(
        socket, reinterpret_cast<const sockaddr*>(&address),
        static_cast<int>(sizeof(address)));
    if (result != 0) {
        const int error = last_socket_error();
        if (!would_block(error) || !wait_socket(socket, true, kIoTimeoutMs, cancel)) {
            close_socket(socket);
            return kInvalidSocket;
        }

        int socket_error = 0;
#ifdef _WIN32
        int error_length = sizeof(socket_error);
#else
        socklen_t error_length = sizeof(socket_error);
#endif
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&socket_error),
                       &error_length) != 0 ||
            socket_error != 0) {
            close_socket(socket);
            return kInvalidSocket;
        }
    }
    return socket;
}

bool send_all(Socket socket, const std::uint8_t* bytes,
              std::size_t length,
              std::chrono::steady_clock::time_point deadline,
              const std::atomic<bool>& cancel) noexcept {
    std::size_t sent = 0;
    while (sent < length) {
        const int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms == 0 || !wait_socket(socket, true, timeout_ms, cancel)) {
            return false;
        }
#ifdef _WIN32
        const int result = send(
            socket, reinterpret_cast<const char*>(bytes + sent),
            static_cast<int>(std::min(
                length - sent,
                static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            0);
#else
        const auto result = send(socket, bytes + sent, length - sent,
#ifdef MSG_NOSIGNAL
                                 MSG_NOSIGNAL
#else
                                 0
#endif
        );
#endif
        if (result < 0) {
            const int error = last_socket_error();
            if (would_block(error) || error == kInterruptedError) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool receive_all(Socket socket, std::uint8_t* bytes,
                 std::size_t length,
                 std::chrono::steady_clock::time_point deadline,
                 const std::atomic<bool>& cancel) noexcept {
    std::size_t received = 0;
    while (received < length) {
        const int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms == 0 || !wait_socket(socket, false, timeout_ms, cancel)) {
            return false;
        }
#ifdef _WIN32
        const int result = recv(
            socket, reinterpret_cast<char*>(bytes + received),
            static_cast<int>(std::min(
                length - received,
                static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            0);
#else
        const auto result = recv(socket, bytes + received, length - received, 0);
#endif
        if (result < 0) {
            const int error = last_socket_error();
            if (would_block(error) || error == kInterruptedError) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

std::uint32_t read_u32_le(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

bool send_frame(Socket socket, const std::uint8_t* frame,
                std::uint32_t sequence,
                const std::atomic<bool>& cancel) noexcept {
    std::uint8_t header[TOMTOM_DISPLAY_HEADER_SIZE]{};
    tomtom_display_make_header(header, sequence);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kIoTimeoutMs);

    if (!send_all(socket, header, sizeof(header), deadline, cancel) ||
        !send_all(socket, frame, kFrameBytes, deadline, cancel)) {
        return false;
    }

    std::uint8_t acknowledgement[TOMTOM_DISPLAY_ACK_SIZE]{};
    return receive_all(socket, acknowledgement, sizeof(acknowledgement),
                       deadline, cancel) &&
           acknowledgement[0] == 'T' && acknowledgement[1] == 'T' &&
           acknowledgement[2] == 'A' && acknowledgement[3] == '1' &&
           read_u32_le(acknowledgement + 4) == sequence &&
           acknowledgement[8] == TOMTOM_DISPLAY_ACK_OK;
}

}  // namespace

FrameTransport::~FrameTransport() {
    stop();
}

bool FrameTransport::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    try {
        has_frame_ = false;
        running_ = true;
        abort_io_.store(paused_);
        reset_pacing_.store(true);
        state_.store(TransportState::waiting_for_frame);
        worker_ = std::thread(&FrameTransport::run, this);
    } catch (const std::system_error&) {
        running_ = false;
        abort_io_.store(true);
        state_.store(TransportState::unavailable);
        return false;
    } catch (const std::bad_alloc&) {
        running_ = false;
        abort_io_.store(true);
        state_.store(TransportState::unavailable);
        return false;
    }
    return true;
}

void FrameTransport::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
        abort_io_.store(true);  // interrupt a connect / send / ACK wait in progress
    }
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    state_.store(TransportState::stopped);
}

bool FrameTransport::submit_frame(const std::uint8_t* frame,
                                  std::size_t frame_bytes) noexcept {
    if (frame == nullptr || frame_bytes != kFrameBytes) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || paused_) {
            return false;
        }
        std::copy_n(frame, kFrameBytes, latest_frame_.begin());
        has_frame_ = true;
    }
    wake_.notify_one();
    return true;
}

TransportState FrameTransport::state() const noexcept {
    return state_.load();
}

void FrameTransport::set_paused(bool paused) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused_ == paused) {
            return;
        }
        paused_ = paused;
        abort_io_.store(paused_ || !running_);
        if (paused) {
            has_frame_ = false;  // a frame captured before the pause must not leak out after resume
        } else {
            reset_pacing_.store(true);  // resuming: do not sit out a long retry backoff
        }
    }
    wake_.notify_all();
}

bool FrameTransport::paused() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

std::uint64_t FrameTransport::frames_sent() const noexcept {
    return frames_sent_.load();
}

void FrameTransport::run() noexcept {
    Socket socket = kInvalidSocket;
    auto next_send = std::chrono::steady_clock::now();
    auto last_activity = next_send;
    auto retry_delay = kRetryInitial;

    for (;;) {
        std::array<std::uint8_t, kFrameBytes> frame;
        bool drop_for_pause = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (socket == kInvalidSocket) {
                wake_.wait(lock, [this] { return !running_ || has_frame_; });
            } else {
                const auto idle_deadline = last_activity + kIdleDisconnect;
                const bool awakened = wake_.wait_until(
                    lock, idle_deadline,
                    [this] { return !running_ || has_frame_ || paused_; });
                // Idle for five seconds, or paused: release the one-client receiver.
                if (running_ && (paused_ || (!awakened && !has_frame_))) {
                    lock.unlock();
                    close_socket(socket);
                    socket = kInvalidSocket;
                    state_.store(TransportState::waiting_for_frame);
                    continue;
                }
            }
            if (!running_) {
                break;
            }
            if (reset_pacing_.exchange(false)) {
                next_send = std::chrono::steady_clock::now();
                retry_delay = kRetryInitial;
            }
            if (std::chrono::steady_clock::now() < next_send) {
                // The pacing wait releases the lock, so a pause can land here: wake on it.
                wake_.wait_until(lock, next_send,
                                 [this] { return !running_ || paused_; });
                if (!running_) {
                    break;
                }
            }
            if (paused_) {
                // Never send a frame captured before the pause; release the receiver instead.
                has_frame_ = false;
                drop_for_pause = true;
            } else {
                frame = latest_frame_;
                has_frame_ = false;
            }
        }

        if (drop_for_pause) {
            close_socket(socket);
            socket = kInvalidSocket;
            state_.store(TransportState::waiting_for_frame);
            continue;
        }

        bool sent = false;
        for (int attempt = 0; attempt < 2 && !sent; ++attempt) {
            const bool reused = socket != kInvalidSocket;
            if (!reused) {
                state_.store(TransportState::connecting);
                socket = connect_device(abort_io_);
            }
            if (socket == kInvalidSocket) {
                break;
            }
            sent = send_frame(socket, frame.data(), next_sequence_++, abort_io_);
            if (sent) {
                break;
            }
            close_socket(socket);
            socket = kInvalidSocket;
            // A failure on a REUSED connection usually means the receiver already dropped it (its idle
            // timeout is 2 s, ours is 5 s, or it restarted): retry this frame once on a fresh connection
            // instead of losing it. A failure on a fresh connection is a real failure.
            if (!reused || abort_io_.load()) {
                break;
            }
        }

        if (sent) {
            frames_sent_.fetch_add(1);
            last_activity = std::chrono::steady_clock::now();
            next_send = last_activity + kFrameInterval;
            retry_delay = kRetryInitial;
            state_.store(TransportState::streaming);
        } else if (abort_io_.load()) {
            // Interrupted by stop() or a pause, not a receiver failure: no backoff; the top of the loop
            // decides what happens next (exit, or wait while paused).
            state_.store(TransportState::waiting_for_frame);
        } else {
            state_.store(TransportState::unavailable);
            next_send = std::chrono::steady_clock::now() + retry_delay;
            retry_delay = std::min<std::chrono::milliseconds>(retry_delay * 2, kRetryMax);
        }
    }

    close_socket(socket);
    state_.store(TransportState::stopped);
}

}  // namespace tt::display
