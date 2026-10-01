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

constexpr char kTomTomUsbAddress[] = "192.168.101.115";
constexpr unsigned short kTomTomDisplayPort = 18745;
constexpr int kIoTimeoutMs = 2000;
constexpr auto kFrameInterval = std::chrono::milliseconds(100);
constexpr auto kIdleDisconnect = std::chrono::seconds(5);

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

bool wait_socket(Socket socket, bool writable, int timeout_ms) noexcept {
#ifdef _WIN32
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(socket, &descriptors);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, writable ? nullptr : &descriptors,
                  writable ? &descriptors : nullptr, nullptr, &timeout) > 0;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = writable ? POLLOUT : POLLIN;
    return poll(&descriptor, 1, timeout_ms) > 0 &&
           (descriptor.revents & (writable ? POLLOUT : POLLIN)) != 0;
#endif
}

Socket connect_device() noexcept {
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
        if (!would_block(error) || !wait_socket(socket, true, kIoTimeoutMs)) {
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
              std::chrono::steady_clock::time_point deadline) noexcept {
    std::size_t sent = 0;
    while (sent < length) {
        const int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms == 0 || !wait_socket(socket, true, timeout_ms)) {
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
                 std::chrono::steady_clock::time_point deadline) noexcept {
    std::size_t received = 0;
    while (received < length) {
        const int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms == 0 || !wait_socket(socket, false, timeout_ms)) {
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
                std::uint32_t sequence) noexcept {
    std::uint8_t header[TOMTOM_DISPLAY_HEADER_SIZE]{};
    tomtom_display_make_header(header, sequence);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kIoTimeoutMs);

    if (!send_all(socket, header, sizeof(header), deadline) ||
        !send_all(socket, frame, kFrameBytes, deadline)) {
        return false;
    }

    std::uint8_t acknowledgement[TOMTOM_DISPLAY_ACK_SIZE]{};
    return receive_all(socket, acknowledgement, sizeof(acknowledgement),
                       deadline) &&
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
        state_.store(TransportState::waiting_for_frame);
        worker_ = std::thread(&FrameTransport::run, this);
    } catch (const std::system_error&) {
        running_ = false;
        state_.store(TransportState::unavailable);
        return false;
    } catch (const std::bad_alloc&) {
        running_ = false;
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
        if (!running_) {
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

void FrameTransport::run() noexcept {
    Socket socket = kInvalidSocket;
    auto next_send = std::chrono::steady_clock::now();
    auto last_activity = next_send;

    for (;;) {
        std::array<std::uint8_t, kFrameBytes> frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (socket == kInvalidSocket) {
                wake_.wait(lock, [this] { return !running_ || has_frame_; });
            } else {
                const auto idle_deadline = last_activity + kIdleDisconnect;
                const bool awakened = wake_.wait_until(
                    lock, idle_deadline,
                    [this] { return !running_ || has_frame_; });
                if (!awakened && running_ && !has_frame_) {
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
            if (std::chrono::steady_clock::now() < next_send) {
                wake_.wait_until(lock, next_send,
                                 [this] { return !running_; });
                if (!running_) {
                    break;
                }
            }
            frame = latest_frame_;
            has_frame_ = false;
        }

        if (socket == kInvalidSocket) {
            state_.store(TransportState::connecting);
            socket = connect_device();
        }

        bool sent = false;
        if (socket != kInvalidSocket) {
            sent = send_frame(socket, frame.data(), next_sequence_++);
        }
        if (sent) {
            last_activity = std::chrono::steady_clock::now();
            next_send = last_activity + kFrameInterval;
            state_.store(TransportState::streaming);
        } else {
            if (socket != kInvalidSocket) {
                close_socket(socket);
                socket = kInvalidSocket;
            }
            state_.store(TransportState::unavailable);
            next_send = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(500);
        }
    }

    close_socket(socket);
    state_.store(TransportState::stopped);
}

}  // namespace tt::display
