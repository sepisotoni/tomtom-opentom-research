// Protocol / timeout / validation tests for the USB device client, driven by a
// fake TCP server on the loopback interface (no real hardware involved).
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "testing.h"

#include "../src/core/device_client.h"
#include "../src/core/net_compat.h"

using namespace tt;
using Clock = std::chrono::steady_clock;

namespace {

bool send_all(SocketHandle s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = static_cast<int>(::send(s, data.data() + sent, static_cast<int>(data.size() - sent), kSendFlags));
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Reads until '\n' (included) or timeout / close.
std::string read_line(SocketHandle s, int timeout_ms) {
    std::string out;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.find('\n') == std::string::npos) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) break;
        int ev = net_poll(s, POLLIN, static_cast<int>(left));
        if (ev <= 0) break;
        char c;
        int n = static_cast<int>(::recv(s, &c, 1, 0));
        if (n <= 0) break;
        out.push_back(c);
    }
    return out;
}

// Accepts connections until destroyed; runs `handler` for each one.
class FakeServer {
public:
    using Handler = std::function<void(SocketHandle, const std::string& request)>;

    explicit FakeServer(Handler handler, int backlog = 8) : handler_(std::move(handler)) {
        net_init();
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listener_, backlog);
#ifdef _WIN32
        int len = sizeof addr;
#else
        socklen_t len = sizeof addr;
#endif
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { run(); });
    }
    ~FakeServer() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        net_close(listener_);
    }
    int port() const { return port_; }
    int accepted() const { return accepted_; }
    std::vector<std::string> requests() {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

private:
    void run() {
        while (!stop_) {
            int ev = net_poll(listener_, POLLIN, 20);
            if (ev <= 0) continue;
            SocketHandle client = ::accept(listener_, nullptr, nullptr);
            if (client == kInvalidSocket) continue;
            ++accepted_;
            std::string request = read_line(client, 1500);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                requests_.push_back(request);
            }
            handler_(client, request);
            net_close(client);
        }
    }

    Handler handler_;
    SocketHandle listener_;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> accepted_{0};
    std::mutex mutex_;
    std::vector<std::string> requests_;
    std::thread thread_;
};

DeviceOptions options_for(const FakeServer& s, int connect_ms = 1000, int read_ms = 1000) {
    DeviceOptions o;
    o.host = "127.0.0.1";
    o.port = s.port();
    o.connect_timeout_ms = connect_ms;
    o.read_timeout_ms = read_ms;
    return o;
}

// Emulates the real service's replies.
FakeServer::Handler tomtom_like(std::atomic<int>* face) {
    return [face](SocketHandle c, const std::string& req) {
        if (req == "PING\n") send_all(c, "OK TOMTOM_CONTROL 1\n");
        else if (req == "STATUS\n") send_all(c, "OK FACE " + std::to_string(face->load()) + "\n");
        else if (req.rfind("SET_FACE ", 0) == 0 && req.size() == 11 && req[9] >= '0' && req[9] <= '8' && req[10] == '\n') {
            face->store(req[9] - '0');
            send_all(c, "OK FACE " + std::to_string(face->load()) + "\n");
        } else send_all(c, "ERR UNKNOWN_COMMAND\n");
    };
}

FakeServer::Handler reply_with(const std::string& bytes) {
    return [bytes](SocketHandle c, const std::string&) { send_all(c, bytes); };
}

long long millis_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

}  // namespace

// ------------------------------------------------------------- pure logic
TEST(device_face_table_matches_firmware) {
    const char* names[9] = {"Blue Outline", "Aqua Wave", "Lavender", "Sunset", "Weather Preview",
                            "Numerals Duo", "Roboto", "Ubuntu", "Nunito"};
    for (int i = 0; i < 9; ++i) {
        CHECK(is_valid_face_id(i));
        CHECK_EQ(std::string(face_name(i)), std::string(names[i]));
    }
    CHECK(!is_valid_face_id(-1));
    CHECK(!is_valid_face_id(9));
    CHECK(face_name(9) == nullptr);
    CHECK_EQ(kDefaultDevicePort, 18743);
    CHECK_EQ(std::string(kDefaultDeviceHost), std::string("192.168.101.115"));
}

TEST(device_host_validation) {
    std::string host, err;
    CHECK(parse_device_host("192.168.101.115", host, err) && host == "192.168.101.115");
    CHECK(parse_device_host("  192.168.101.115 \t", host, err) && host == "192.168.101.115");
    CHECK(parse_device_host("10.0.0.5", host, err));
    CHECK(parse_device_host("172.16.0.1", host, err) && parse_device_host("172.31.255.254", host, err));
    CHECK(parse_device_host("169.254.10.20", host, err));
    CHECK(parse_device_host("127.0.0.1", host, err));
    CHECK(is_usb_link_address("192.168.101.115") && !is_usb_link_address("192.168.1.10"));
    // Public addresses, host names and malformed literals are refused up front.
    CHECK(!parse_device_host("8.8.8.8", host, err));
    CHECK_CONTAINS(err, "USB");
    CHECK(!parse_device_host("172.32.0.1", host, err));
    CHECK(!parse_device_host("172.15.0.1", host, err));
    CHECK(!parse_device_host("0.0.0.0", host, err));
    CHECK(!parse_device_host("255.255.255.255", host, err));
    CHECK(!parse_device_host("224.0.0.1", host, err));
    CHECK(!parse_device_host("tomtom.local", host, err));
    CHECK(!parse_device_host("", host, err));
    CHECK(!parse_device_host("192.168.101", host, err));
    CHECK(!parse_device_host("192.168.101.115.1", host, err));
    CHECK(!parse_device_host("192.168.101.256", host, err));
    CHECK(!parse_device_host("192.168.101.-1", host, err));
    CHECK(!parse_device_host("192.168.101.0x73", host, err));
    CHECK(!parse_device_host("192.168.101.015", host, err));  // leading zeros are ambiguous (octal)
    CHECK(!parse_device_host("192.168.101.115:18743", host, err));
    CHECK(!parse_device_host("192.168.101.115\n", host, err));
    CHECK(!parse_device_host("192.168..1", host, err));
}

TEST(device_ping_reply_parsing) {
    CHECK(parse_ping_reply("OK TOMTOM_CONTROL 1").ok());
    for (const char* bad : {"", "OK", "OK TOMTOM_CONTROL 2", "OK TOMTOM_CONTROL 1 ", "ok tomtom_control 1", "OK TOMTOM_CONTROL",
                            "OK FACE 3", "OK TOMTOM_CONTROL 1\r"})
        CHECK_EQ(static_cast<int>(parse_ping_reply(bad).status), static_cast<int>(DeviceStatus::BadReply));
}

TEST(device_status_reply_parsing_and_face_id_validation) {
    for (int id = 0; id < 9; ++id) {
        DeviceResult r = parse_status_reply("OK FACE " + std::to_string(id));
        CHECK(r.ok());
        CHECK_EQ(r.face_id, id);
        CHECK_CONTAINS(r.message, face_name(id));
    }
    // Out of range, malformed and unexpected shapes are all rejected.
    for (const char* bad : {"OK FACE 9", "OK FACE 10", "OK FACE 99", "OK FACE -1", "OK FACE 07", "OK FACE 00", "OK FACE  7",
                            "OK FACE 7 ", "OK FACE ", "OK FACE", "OK FACE a", "OK FACE 7x", "OK FACE 1.0", "OK FACE +1",
                            "OK  FACE 7", "OK FACE\t7", "OK FACES 7", "", "FACE 7", "OK FACE 4294967297", "OK FACE 0x7"}) {
        DeviceResult r = parse_status_reply(bad);
        CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::BadReply));
        CHECK_EQ(r.face_id, -1);
    }
    DeviceResult err = parse_status_reply("ERR NO_FACE");
    CHECK_EQ(static_cast<int>(err.status), static_cast<int>(DeviceStatus::DeviceRejected));
    CHECK_EQ(err.device_code, std::string("NO_FACE"));
    // Error codes are validated too (no arbitrary device text is echoed).
    CHECK_EQ(static_cast<int>(parse_status_reply("ERR bad code!").status), static_cast<int>(DeviceStatus::BadReply));
    CHECK_EQ(static_cast<int>(parse_status_reply("ERR ").status), static_cast<int>(DeviceStatus::BadReply));
    CHECK_EQ(static_cast<int>(parse_status_reply("ERR " + std::string(33, 'A')).status), static_cast<int>(DeviceStatus::BadReply));
    DeviceResult echo = parse_status_reply("ERR SOME_NEW_CODE");
    CHECK_EQ(static_cast<int>(echo.status), static_cast<int>(DeviceStatus::DeviceRejected));
}

TEST(device_set_reply_must_echo_requested_face) {
    DeviceResult ok = parse_set_face_reply("OK FACE 7", 7);
    CHECK(ok.ok());
    CHECK_EQ(ok.face_id, 7);
    CHECK_CONTAINS(ok.message, "Ubuntu");
    DeviceResult mismatch = parse_set_face_reply("OK FACE 3", 7);
    CHECK_EQ(static_cast<int>(mismatch.status), static_cast<int>(DeviceStatus::BadReply));
    CHECK_EQ(mismatch.face_id, -1);
    CHECK_EQ(static_cast<int>(parse_set_face_reply("OK FACE 9", 9).status), static_cast<int>(DeviceStatus::BadReply));
    CHECK_EQ(static_cast<int>(parse_set_face_reply("OK TOMTOM_CONTROL 1", 7).status), static_cast<int>(DeviceStatus::BadReply));
    DeviceResult usb = parse_set_face_reply("ERR USB_ONLY", 7);
    CHECK_EQ(static_cast<int>(usb.status), static_cast<int>(DeviceStatus::DeviceRejected));
    CHECK_EQ(usb.device_code, std::string("USB_ONLY"));
    CHECK_EQ(static_cast<int>(parse_set_face_reply("ERR INVALID_FACE", 7).status), static_cast<int>(DeviceStatus::DeviceRejected));
    CHECK_EQ(static_cast<int>(parse_set_face_reply("ERR SAVE_FAILED", 7).status), static_cast<int>(DeviceStatus::DeviceRejected));
}

// ----------------------------------------------------- fake-server sessions
TEST(device_ping_status_set_over_fake_server) {
    std::atomic<int> face{3};
    FakeServer server(tomtom_like(&face));
    DeviceOptions o = options_for(server);

    DeviceResult ping = device_ping(o);
    CHECK(ping.ok());
    DeviceResult st = device_status(o);
    CHECK(st.ok());
    CHECK_EQ(st.face_id, 3);
    DeviceResult set = device_set_face(o, 7);
    CHECK(set.ok());
    CHECK_EQ(set.face_id, 7);
    CHECK_EQ(face.load(), 7);
    DeviceResult st2 = device_status(o);
    CHECK(st2.ok() && st2.face_id == 7);
    for (int id = 0; id < 9; ++id) {
        DeviceResult r = device_set_face(o, id);
        CHECK(r.ok() && r.face_id == id);
    }
    // Exactly the three fixed commands, newline terminated, nothing else was ever sent.
    std::vector<std::string> reqs = server.requests();
    CHECK_EQ(reqs.size(), size_t(4 + 9));
    CHECK_EQ(reqs[0], std::string("PING\n"));
    CHECK_EQ(reqs[1], std::string("STATUS\n"));
    CHECK_EQ(reqs[2], std::string("SET_FACE 7\n"));
    CHECK_EQ(reqs[3], std::string("STATUS\n"));
    CHECK_EQ(reqs[4], std::string("SET_FACE 0\n"));
    CHECK_EQ(reqs[12], std::string("SET_FACE 8\n"));
}

TEST(device_invalid_face_ids_never_reach_the_network) {
    std::atomic<int> face{0};
    FakeServer server(tomtom_like(&face));
    DeviceOptions o = options_for(server);
    for (int bad : {-1, 9, 10, 100, -100, 65536, 2147483647, -2147483647 - 1}) {
        DeviceResult r = device_set_face(o, bad);
        CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::InvalidArgument));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(server.accepted(), 0);
    CHECK_EQ(face.load(), 0);
}

TEST(device_non_usb_hosts_are_refused_without_connecting) {
    std::atomic<int> face{0};
    FakeServer server(tomtom_like(&face));
    DeviceOptions o = options_for(server);
    for (const char* host : {"8.8.8.8", "example.com", "", "1.2.3.4"}) {
        o.host = host;
        CHECK_EQ(static_cast<int>(device_ping(o).status), static_cast<int>(DeviceStatus::InvalidArgument));
        CHECK_EQ(static_cast<int>(device_status(o).status), static_cast<int>(DeviceStatus::InvalidArgument));
        CHECK_EQ(static_cast<int>(device_set_face(o, 1).status), static_cast<int>(DeviceStatus::InvalidArgument));
    }
    CHECK_EQ(server.accepted(), 0);
    o.host = "127.0.0.1";
    o.port = 0;
    CHECK_EQ(static_cast<int>(device_ping(o).status), static_cast<int>(DeviceStatus::InvalidArgument));
    o.port = 70000;
    CHECK_EQ(static_cast<int>(device_ping(o).status), static_cast<int>(DeviceStatus::InvalidArgument));
}

TEST(device_error_replies_from_service) {
    FakeServer usb_only(reply_with("ERR USB_ONLY\n"));
    DeviceResult r = device_set_face(options_for(usb_only), 2);
    CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::DeviceRejected));
    CHECK_EQ(r.device_code, std::string("USB_ONLY"));
    CHECK_CONTAINS(r.message, "USB");
    FakeServer no_face(reply_with("ERR NO_FACE\n"));
    DeviceResult s = device_status(options_for(no_face));
    CHECK_EQ(static_cast<int>(s.status), static_cast<int>(DeviceStatus::DeviceRejected));
    CHECK_EQ(s.face_id, -1);
}

TEST(device_mismatched_and_invalid_set_replies) {
    FakeServer wrong_face(reply_with("OK FACE 3\n"));
    CHECK_EQ(static_cast<int>(device_set_face(options_for(wrong_face), 7).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer out_of_range(reply_with("OK FACE 9\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(out_of_range)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer wrong_service(reply_with("HTTP/1.1 400 Bad Request\n"));
    CHECK_EQ(static_cast<int>(device_ping(options_for(wrong_service)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer garbage(reply_with("OK FACE \x01\x02\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(garbage)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer high_bit(reply_with("OK FACE \xC3\xA9\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(high_bit)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer crlf(reply_with("OK FACE 4\r\n"));  // the device never sends CR; reject rather than guess
    CHECK_EQ(static_cast<int>(device_status(options_for(crlf)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer trailing(reply_with("OK FACE 4\nOK FACE 5\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(trailing)).status), static_cast<int>(DeviceStatus::BadReply));
    FakeServer empty_line(reply_with("\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(empty_line)).status), static_cast<int>(DeviceStatus::BadReply));
}

TEST(device_oversized_replies_are_bounded) {
    // 200 bytes with no newline: must stop reading and report Oversized.
    FakeServer no_newline(reply_with(std::string(200, 'A')));
    DeviceResult r = device_status(options_for(no_newline));
    CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::Oversized));
    // A line one byte over the limit, terminated.
    FakeServer just_over(reply_with(std::string(kMaxReplyBytes + 1, 'B') + "\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(just_over)).status), static_cast<int>(DeviceStatus::Oversized));
    // A line of exactly the limit is read fully, then judged on content.
    FakeServer at_limit(reply_with(std::string(kMaxReplyBytes, 'C') + "\n"));
    CHECK_EQ(static_cast<int>(device_status(options_for(at_limit)).status), static_cast<int>(DeviceStatus::BadReply));
    // Megabytes of data are never buffered: the call returns promptly.
    FakeServer flood([](SocketHandle c, const std::string&) {
        std::string chunk(4096, 'Z');
        for (int i = 0; i < 2000; ++i)
            if (!send_all(c, chunk)) break;
    });
    auto t0 = Clock::now();
    CHECK_EQ(static_cast<int>(device_status(options_for(flood)).status), static_cast<int>(DeviceStatus::Oversized));
    CHECK(millis_since(t0) < 1500);
}

TEST(device_early_close_and_reset) {
    FakeServer closes([](SocketHandle, const std::string&) {});  // accepts, reads, closes silently
    DeviceResult r = device_status(options_for(closes));
    CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::ClosedEarly));
    FakeServer partial(reply_with("OK FACE"));  // closes mid-line
    CHECK_EQ(static_cast<int>(device_status(options_for(partial)).status), static_cast<int>(DeviceStatus::ClosedEarly));
}

TEST(device_read_timeout_is_bounded) {
    FakeServer silent([](SocketHandle, const std::string&) { std::this_thread::sleep_for(std::chrono::milliseconds(1500)); });
    DeviceOptions o = options_for(silent, 1000, 300);
    auto t0 = Clock::now();
    DeviceResult r = device_status(o);
    long long elapsed = millis_since(t0);
    CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::Timeout));
    CHECK(elapsed >= 250 && elapsed < 1200);
}

TEST(device_trickled_reply_hits_total_deadline) {
    // One byte every 60 ms never completes a line; the *total* budget must still expire.
    FakeServer drip([](SocketHandle c, const std::string&) {
        for (int i = 0; i < 40; ++i) {
            if (!send_all(c, "O")) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    });
    DeviceOptions o = options_for(drip, 1000, 400);
    auto t0 = Clock::now();
    DeviceResult r = device_status(o);
    long long elapsed = millis_since(t0);
    CHECK_EQ(static_cast<int>(r.status), static_cast<int>(DeviceStatus::Timeout));
    CHECK(elapsed < 1000);
}

TEST(device_slow_but_valid_reply_within_budget) {
    FakeServer slow([](SocketHandle c, const std::string&) {
        std::string reply = "OK FACE 5\n";
        for (char ch : reply) {
            send_all(c, std::string(1, ch));
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    DeviceResult r = device_status(options_for(slow, 1000, 2000));
    CHECK(r.ok());
    CHECK_EQ(r.face_id, 5);
}

TEST(device_connection_refused) {
    // Grab a free port, then close the listener so nothing is listening there.
    int port;
    {
        FakeServer temp(reply_with(""));
        port = temp.port();
    }
    DeviceOptions o;
    o.host = "127.0.0.1";
    o.port = port;
    o.connect_timeout_ms = 500;
    auto t0 = Clock::now();
    DeviceResult r = device_ping(o);
    // Linux reports a refusal at once; Windows retries the SYN for ~2 s first, so a short
    // connect budget ends as Timeout there. Either way the call is prompt and never succeeds.
    CHECK(r.status == DeviceStatus::ConnectFailed || r.status == DeviceStatus::Timeout);
    if (r.status == DeviceStatus::ConnectFailed) CHECK_CONTAINS(r.message, "Could not reach");
    else CHECK_CONTAINS(r.message, "Timed out");
    CHECK(millis_since(t0) < 1500);
}

TEST(device_connect_timeout_is_bounded) {
    // A listener with a full accept queue stops answering SYNs, so connect() stalls.
    net_init();
    SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::listen(listener, 0);
#ifdef _WIN32
    int len = sizeof addr;
#else
    socklen_t len = sizeof addr;
#endif
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    std::vector<SocketHandle> fillers;
    for (int i = 0; i < 6; ++i) {
        SocketHandle f = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        net_set_nonblocking(f);
        ::connect(f, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        fillers.push_back(f);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DeviceOptions o;
    o.host = "127.0.0.1";
    o.port = ntohs(addr.sin_port);
    o.connect_timeout_ms = 300;
    o.read_timeout_ms = 300;
    auto t0 = Clock::now();
    DeviceResult r = device_ping(o);
    long long elapsed = millis_since(t0);
    // The essential guarantee is a prompt, non-Ok result; platforms differ on Timeout vs refusal.
    CHECK(!r.ok());
    CHECK(elapsed < 2500);
    CHECK(r.status == DeviceStatus::Timeout || r.status == DeviceStatus::ConnectFailed);
    for (SocketHandle f : fillers) net_close(f);
    net_close(listener);
}
