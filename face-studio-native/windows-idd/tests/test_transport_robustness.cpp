// Robustness of FrameTransport against a receiver that is absent, stalled, drops mid-stream, or has
// already dropped an idle connection - the situations in which neither the driver nor the Studio may
// block, leak, or lose the last frame.
//
// Build note: this file must be compiled together with src/display/frame_transport.cpp built with
// -DTT_DISPLAY_TRANSPORT_TEST_LOOPBACK, which makes the transport connect to 127.0.0.1:18745 instead of the
// TomTom's USB address. The fake receiver below listens there and follows the rules of the real
// tomtom-display-receiver: one client at a time (backlog 1), 22-byte header + 153,600-byte payload, 9-byte
// ACK, a client that is silent for too long is dropped. The port is fixed (18745): do not run two copies
// of this test at once.
#include "../../src/display/frame_transport.h"
#include "../../../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
constexpr sock_t kBadSock = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
constexpr sock_t kBadSock = -1;
static void close_sock(sock_t s) { ::close(s); }
#endif

namespace {

using Clock = std::chrono::steady_clock;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL " << what << "\n";
    }
}

long long ms_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

template <typename Predicate>
bool wait_for(Predicate predicate, int timeout_ms) {
    const auto start = Clock::now();
    while (ms_since(start) < timeout_ms) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

bool readable(sock_t s, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &tv) > 0;
}

// ---- fake receiver ------------------------------------------------------------------------------

class FakeReceiver {
public:
    // behaviour knobs (may be changed while running)
    std::atomic<bool> stall{false};          // read the frame but never ACK (a hung receiver)
    std::atomic<int> drop_after{-1};         // close the connection after N ACKed frames on it
    std::atomic<int> idle_close_ms{2000};    // drop a client that sends nothing for this long

    // observations
    std::atomic<int> connections{0};
    std::atomic<int> frames{0};              // fully received AND acknowledged
    std::atomic<int> received{0};            // fully received (ACKed or not)
    std::atomic<bool> client_connected{false};

    ~FakeReceiver() { stop(); }

    bool start() {
#ifdef _WIN32
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == kBadSock) return false;
        int one = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(18745);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listener_, 1) != 0) {
            close_sock(listener_);
            listener_ = kBadSock;
            return false;
        }
        stopping_ = false;
        thread_ = std::thread([this] { serve(); });
        return true;
    }

    void stop() {
        stopping_ = true;
        if (thread_.joinable()) thread_.join();
        if (listener_ != kBadSock) {
            close_sock(listener_);
            listener_ = kBadSock;
        }
    }

private:
    bool recv_exact(sock_t s, std::uint8_t* out, std::size_t n, int idle_ms) {
        std::size_t got = 0;
        while (got < n) {
            if (stopping_) return false;
            if (!readable(s, idle_ms)) return false;  // idle timeout
            const auto r = ::recv(s, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
            if (r <= 0) return false;
            got += static_cast<std::size_t>(r);
        }
        return true;
    }

    void handle_client(sock_t client) {
        client_connected = true;
        ++connections;
        int acked_here = 0;
        std::array<std::uint8_t, TOMTOM_DISPLAY_FRAME_BYTES> payload;
        for (;;) {
            std::uint8_t header[TOMTOM_DISPLAY_HEADER_SIZE];
            if (!recv_exact(client, header, sizeof(header), idle_close_ms.load())) break;
            tomtom_display_frame frame{};
            if (!tomtom_display_parse_header(header, sizeof(header), &frame)) break;  // 1 = valid, like the real receiver
            if (!recv_exact(client, payload.data(), payload.size(), 2000)) break;
            ++received;
            if (stall) {
                // A hung receiver: keep the connection open and say nothing until the client goes away.
                while (!stopping_ && stall) {
                    if (readable(client, 20)) {
                        char b;
                        if (::recv(client, &b, 1, 0) <= 0) break;
                    }
                }
                break;
            }
            std::uint8_t ack[TOMTOM_DISPLAY_ACK_SIZE];
            tomtom_display_make_ack(ack, frame.sequence, TOMTOM_DISPLAY_ACK_OK);
            if (::send(client, reinterpret_cast<const char*>(ack), sizeof(ack), 0) != static_cast<int>(sizeof(ack))) break;
            ++frames;
            ++acked_here;
            if (drop_after.load() >= 0 && acked_here >= drop_after.load()) break;
        }
        close_sock(client);
        client_connected = false;
    }

    void serve() {
        while (!stopping_) {
            if (!readable(listener_, 50)) continue;
            sock_t client = ::accept(listener_, nullptr, nullptr);
            if (client == kBadSock) continue;
            handle_client(client);  // one client at a time, like the real receiver
        }
    }

    sock_t listener_ = kBadSock;
    std::atomic<bool> stopping_{true};
    std::thread thread_;
};

using tt::display::FrameTransport;
using tt::display::TransportState;

std::array<std::uint8_t, tt::display::kFrameBytes> make_frame(std::uint8_t fill) {
    std::array<std::uint8_t, tt::display::kFrameBytes> frame{};
    frame.fill(fill);
    return frame;
}

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(150)); }

// ---- scenarios ----------------------------------------------------------------------------------

void normal_streaming() {
    FakeReceiver receiver;
    check(receiver.start(), "fake receiver listens on 127.0.0.1:18745");
    FrameTransport transport;
    check(transport.start(), "transport starts");
    const auto frame = make_frame(0x11);
    for (int i = 0; i < 5; ++i) {
        transport.submit_frame(frame.data(), frame.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    check(wait_for([&] { return receiver.frames == 5; }, 2000), "normal: five frames delivered");
    check(transport.frames_sent() == 5, "normal: frames_sent counts ACKed frames");
    check(receiver.connections == 1, "normal: one connection reused");
    transport.stop();
    std::cout << "ok   normal streaming\n";
}

void stop_is_prompt_when_receiver_hangs() {
    FakeReceiver receiver;
    receiver.stall = true;
    receiver.start();
    FrameTransport transport;
    transport.start();
    const auto frame = make_frame(0x22);
    transport.submit_frame(frame.data(), frame.size());
    check(wait_for([&] { return receiver.received >= 1; }, 2000), "hung: receiver got the frame (transport now waits for an ACK)");
    const auto t0 = Clock::now();
    transport.stop();
    const long long took = ms_since(t0);
    std::cout << "     stop() with a hung receiver took " << took << " ms\n";
    check(took < 700, "stop() must not wait out the 2 s ACK deadline (took " + std::to_string(took) + " ms)");
}

void pause_is_prompt_when_receiver_hangs() {
    FakeReceiver receiver;
    receiver.stall = true;
    receiver.start();
    FrameTransport transport;
    transport.start();
    const auto frame = make_frame(0x33);
    transport.submit_frame(frame.data(), frame.size());
    check(wait_for([&] { return receiver.received >= 1; }, 2000), "hung+pause: frame received");
    const auto t0 = Clock::now();
    transport.set_paused(true);
    const bool released = wait_for([&] { return !receiver.client_connected; }, 1500);
    const long long took = ms_since(t0);
    std::cout << "     pause released a hung connection after " << took << " ms\n";
    check(released && took < 700, "pause must release the receiver without waiting for the ACK deadline");
    transport.set_paused(false);
    transport.stop();
}

void pause_resume_cycle() {
    FakeReceiver receiver;
    receiver.start();
    FrameTransport transport;
    transport.start();
    const auto frame = make_frame(0x44);
    transport.submit_frame(frame.data(), frame.size());
    check(wait_for([&] { return receiver.frames == 1 && receiver.client_connected; }, 2000), "cycle: streaming");
    const auto t0 = Clock::now();
    transport.set_paused(true);
    check(wait_for([&] { return !receiver.client_connected; }, 1000), "cycle: pause closes the idle connection");
    std::cout << "     pause released an idle connection after " << ms_since(t0) << " ms\n";
    check(!transport.submit_frame(frame.data(), frame.size()), "cycle: paused transport refuses frames");
    settle();
    check(receiver.connections == 1, "cycle: no reconnect while paused");
    transport.set_paused(false);
    transport.submit_frame(frame.data(), frame.size());
    check(wait_for([&] { return receiver.frames == 2; }, 2000), "cycle: frame delivered after resume");
    check(receiver.connections == 2, "cycle: resume used a fresh connection");
    transport.stop();
    std::cout << "ok   pause/resume\n";
}

void mid_stream_drop() {
    FakeReceiver receiver;
    receiver.drop_after = 2;  // hang up after two frames on every connection
    receiver.start();
    FrameTransport transport;
    transport.start();
    const auto frame = make_frame(0x55);
    const auto t0 = Clock::now();
    while (ms_since(t0) < 4000) {
        transport.submit_frame(frame.data(), frame.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    std::cout << "     mid-stream drop: " << receiver.frames << " frames over " << receiver.connections << " connections\n";
    check(receiver.connections >= 3, "drop: transport keeps reconnecting");
    check(receiver.frames >= 8, "drop: streaming continues across reconnects");
    check(transport.frames_sent() == static_cast<std::uint64_t>(receiver.frames.load()), "drop: frames_sent matches what the receiver ACKed");
    const auto s0 = Clock::now();
    transport.stop();
    check(ms_since(s0) < 700, "drop: stop() prompt");
    std::cout << "ok   mid-stream drop\n";
}

void stale_connection_does_not_lose_the_frame() {
    FakeReceiver receiver;
    receiver.idle_close_ms = 300;  // the real receiver does this after 2 s; the transport keeps sockets for 5 s
    receiver.start();
    FrameTransport transport;
    transport.start();
    const auto a = make_frame(0x61);
    const auto b = make_frame(0x62);
    transport.submit_frame(a.data(), a.size());
    check(wait_for([&] { return receiver.frames == 1; }, 2000), "stale: first frame delivered");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));  // receiver drops the idle client
    check(!receiver.client_connected, "stale: receiver dropped the idle connection");
    transport.submit_frame(b.data(), b.size());  // ... and NOTHING is submitted after this
    const auto t0 = Clock::now();
    const bool delivered = wait_for([&] { return receiver.frames == 2; }, 1500);
    std::cout << "     frame on a stale connection " << (delivered ? "delivered after " : "LOST, waited ") << ms_since(t0) << " ms\n";
    check(delivered, "a frame submitted on a connection the receiver already dropped must still arrive");
    transport.stop();
}

void receiver_absent_then_present() {
    FrameTransport transport;
    transport.start();
    const auto frame = make_frame(0x77);
    const auto t0 = Clock::now();
    while (ms_since(t0) < 1500) {  // nobody is listening: every attempt is refused
        transport.submit_frame(frame.data(), frame.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    check(transport.state() == TransportState::unavailable || transport.state() == TransportState::connecting,
          "absent: transport reports unavailable");
    FakeReceiver receiver;
    check(receiver.start(), "absent: receiver appears");
    const auto appeared = Clock::now();
    bool delivered = false;
    while (ms_since(appeared) < 7000 && !delivered) {
        transport.submit_frame(frame.data(), frame.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        delivered = receiver.frames >= 1;
    }
    std::cout << "     first frame " << ms_since(appeared) << " ms after the receiver appeared\n";
    check(delivered, "absent: streaming starts by itself once the receiver is there");
    const auto s0 = Clock::now();
    transport.stop();
    check(ms_since(s0) < 700, "absent: stop() prompt");
    std::cout << "ok   receiver absent then present\n";
}

}  // namespace

int main() {
    normal_streaming();
    stop_is_prompt_when_receiver_hangs();
    pause_is_prompt_when_receiver_hangs();
    pause_resume_cycle();
    mid_stream_drop();
    stale_connection_does_not_lose_the_frame();
    receiver_absent_then_present();

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " transport robustness checks FAILED\n";
        return 1;
    }
    std::cout << "transport robustness: " << g_checks << " checks passed\n";
    return 0;
}
