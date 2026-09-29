#include "device_client.h"

#include <chrono>
#include <cstring>

#include "net_compat.h"

namespace tt {

const DeviceFace kDeviceFaces[9] = {
    {0, "Blue Outline"}, {1, "Aqua Wave"}, {2, "Lavender"},     {3, "Sunset"}, {4, "Weather Preview"},
    {5, "Numerals Duo"}, {6, "Roboto"},    {7, "Ubuntu"},       {8, "Nunito"},
};
const char* const kDefaultDeviceHost = "192.168.101.115";

bool is_valid_face_id(int id) { return id >= 0 && id < kDeviceFaceCount; }

const char* face_name(int id) { return is_valid_face_id(id) ? kDeviceFaces[id].name : nullptr; }

// ------------------------------------------------------------------- host
bool parse_device_host(const std::string& raw, std::string& normalized, std::string& error) {
    size_t a = 0, b = raw.size();
    while (a < b && (raw[a] == ' ' || raw[a] == '\t')) ++a;
    while (b > a && (raw[b - 1] == ' ' || raw[b - 1] == '\t')) --b;
    const std::string text = raw.substr(a, b - a);
    if (text.empty()) {
        error = "Enter the TomTom's USB IP address (default 192.168.101.115).";
        return false;
    }
    int octet[4] = {0, 0, 0, 0};
    int parts = 0;
    size_t pos = 0;
    while (pos <= text.size() && parts < 4) {
        size_t dot = text.find('.', pos);
        std::string piece = text.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (piece.empty() || piece.size() > 3 || (piece.size() > 1 && piece[0] == '0')) break;
        int value = 0;
        bool digits = true;
        for (char c : piece) {
            if (c < '0' || c > '9') { digits = false; break; }
            value = value * 10 + (c - '0');
        }
        if (!digits || value > 255) break;
        octet[parts++] = value;
        if (dot == std::string::npos) { pos = text.size() + 1; break; }
        pos = dot + 1;
    }
    if (parts != 4 || pos != text.size() + 1) {
        error = "The TomTom address must be an IPv4 address such as 192.168.101.115 (host names are not used).";
        return false;
    }
    bool allowed = octet[0] == 10 || octet[0] == 127 || (octet[0] == 172 && octet[1] >= 16 && octet[1] <= 31) ||
                   (octet[0] == 192 && octet[1] == 168) || (octet[0] == 169 && octet[1] == 254);
    if (!allowed) {
        error = "Refusing to connect: the control service is unencrypted and must only be reached over a direct "
                "USB link, so only private (10.x, 172.16-31.x, 192.168.x), link-local and loopback addresses are allowed.";
        return false;
    }
    normalized = std::to_string(octet[0]) + "." + std::to_string(octet[1]) + "." + std::to_string(octet[2]) + "." +
                 std::to_string(octet[3]);
    return true;
}

bool is_usb_link_address(const std::string& host) { return host.rfind("192.168.101.", 0) == 0; }

// ----------------------------------------------------------------- parsers
namespace {

DeviceResult make(DeviceStatus s, std::string message) {
    DeviceResult r;
    r.status = s;
    r.message = std::move(message);
    return r;
}

std::string describe_code(const std::string& code) {
    if (code == "USB_ONLY")
        return "The TomTom only accepts control connections from its USB network (192.168.101.0/24) (USB_ONLY).";
    if (code == "INVALID_FACE") return "The TomTom rejected the face number (INVALID_FACE).";
    if (code == "SAVE_FAILED") return "The TomTom could not save the face selection - check its SD card (SAVE_FAILED).";
    if (code == "NO_FACE") return "No face has been saved on the TomTom yet (NO_FACE).";
    if (code == "UNKNOWN_COMMAND") return "The TomTom did not recognise the command (UNKNOWN_COMMAND).";
    if (code == "REQUEST_TOO_LONG") return "The TomTom reported the request was too long (REQUEST_TOO_LONG).";
    return "The TomTom reported an error (" + code + ").";
}

// "ERR <CODE>" with 1-32 chars of [A-Z0-9_]; returns true and fills `out` for a well-formed error line.
bool parse_error_line(const std::string& line, DeviceResult& out) {
    if (line.rfind("ERR ", 0) != 0) return false;
    std::string code = line.substr(4);
    if (code.empty() || code.size() > 32) return false;
    for (char c : code)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    out = make(DeviceStatus::DeviceRejected, describe_code(code));
    out.device_code = code;
    return true;
}

// "OK FACE <d>" with exactly one digit 0-8; returns -1 if the line has another shape.
int parse_face_line(const std::string& line, bool& well_formed_but_invalid) {
    well_formed_but_invalid = false;
    static const char kPrefix[] = "OK FACE ";
    const size_t n = sizeof(kPrefix) - 1;
    if (line.size() < n + 1 || line.compare(0, n, kPrefix) != 0) return -1;
    std::string value = line.substr(n);
    if (value.size() == 1 && value[0] >= '0' && value[0] <= '9') {
        int id = value[0] - '0';
        if (is_valid_face_id(id)) return id;
        well_formed_but_invalid = true;
    }
    return -1;
}

}  // namespace

DeviceResult parse_ping_reply(const std::string& line) {
    if (line == "OK TOMTOM_CONTROL 1") return make(DeviceStatus::Ok, "TomTom control service is responding (protocol 1).");
    DeviceResult r;
    if (parse_error_line(line, r)) return r;
    return make(DeviceStatus::BadReply, "The device answered PING with an unexpected reply; this is not the TomTom control service.");
}

DeviceResult parse_status_reply(const std::string& line) {
    DeviceResult r;
    if (parse_error_line(line, r)) return r;
    bool invalid = false;
    int id = parse_face_line(line, invalid);
    if (id >= 0) {
        r = make(DeviceStatus::Ok, std::string(face_name(id)) + " is the active face on the TomTom.");
        r.face_id = id;
        return r;
    }
    return make(DeviceStatus::BadReply, invalid ? "The TomTom reported a face number outside 0-8." :
                                                  "The TomTom returned an invalid status reply.");
}

DeviceResult parse_set_face_reply(const std::string& line, int requested_id) {
    DeviceResult r;
    if (parse_error_line(line, r)) return r;
    bool invalid = false;
    int id = parse_face_line(line, invalid);
    if (id < 0)
        return make(DeviceStatus::BadReply, invalid ? "The TomTom acknowledged a face number outside 0-8." :
                                                      "The TomTom returned an invalid reply to the face change.");
    if (id != requested_id)
        return make(DeviceStatus::BadReply, "The TomTom acknowledged face " + std::to_string(id) + " but face " +
                                                std::to_string(requested_id) + " was requested.");
    r = make(DeviceStatus::Ok, std::string(face_name(id)) + " is now the active face. The TomTom applies it within about "
                                                            "a second and keeps it after a restart.");
    r.face_id = id;
    return r;
}

// ------------------------------------------------------------------ socket
namespace {

using Clock = std::chrono::steady_clock;

int remaining_ms(Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left < 0 ? 0 : static_cast<int>(left);
}

struct Socket {
    SocketHandle h = kInvalidSocket;
    ~Socket() {
        if (h != kInvalidSocket) net_close(h);
    }
};

// Sends `command` + '\n' and reads one reply line (without '\n').
DeviceResult exchange(const DeviceOptions& options, const std::string& command, std::string& line) {
    std::string host, error;
    if (!parse_device_host(options.host, host, error)) return make(DeviceStatus::InvalidArgument, error);
    if (options.port < 1 || options.port > 65535) return make(DeviceStatus::InvalidArgument, "Invalid TCP port.");
    const int connect_ms = options.connect_timeout_ms > 0 ? options.connect_timeout_ms : 2000;
    const int read_ms = options.read_timeout_ms > 0 ? options.read_timeout_ms : 3000;

    net_init();
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(options.port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return make(DeviceStatus::InvalidArgument, "Invalid IPv4 address.");

    Socket sock;
    sock.h = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock.h == kInvalidSocket || !net_set_nonblocking(sock.h))
        return make(DeviceStatus::IoError, "Could not create a network socket.");

    const std::string target = host + ":" + std::to_string(options.port);
    int rc = ::connect(sock.h, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (rc != 0) {
        if (!net_would_block(net_last_error()))
            return make(DeviceStatus::ConnectFailed, "Could not reach the TomTom control service at " + target +
                                                         ". Check the USB cable and network link.");
        // select() (not WSAPoll) so that a refused connection is reported promptly on every Windows version.
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(sock.h, &writable);
        FD_SET(sock.h, &failed);
        timeval tv;
        tv.tv_sec = connect_ms / 1000;
        tv.tv_usec = (connect_ms % 1000) * 1000;
        int ready = ::select(static_cast<int>(sock.h) + 1, nullptr, &writable, &failed, &tv);
        if (ready == 0) return make(DeviceStatus::Timeout, "Timed out connecting to the TomTom at " + target + ".");
        if (ready < 0) return make(DeviceStatus::IoError, "Network error while connecting to the TomTom.");
        int so_error = 0;
#ifdef _WIN32
        int len = sizeof so_error;
        getsockopt(sock.h, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
#else
        socklen_t len = sizeof so_error;
        getsockopt(sock.h, SOL_SOCKET, SO_ERROR, &so_error, &len);
#endif
        if (so_error != 0 || FD_ISSET(sock.h, &failed) || !FD_ISSET(sock.h, &writable))
            return make(DeviceStatus::ConnectFailed, "Could not reach the TomTom control service at " + target +
                                                         ". Check the USB cable and network link.");
    }

    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(read_ms);
    const std::string request = command + "\n";
    size_t sent = 0;
    while (sent < request.size()) {
        int ev = net_poll(sock.h, POLLOUT, remaining_ms(deadline));
        if (ev == 0) return make(DeviceStatus::Timeout, "Timed out sending the command to the TomTom.");
        if (ev < 0) {
            if (net_interrupted(net_last_error())) continue;
            return make(DeviceStatus::IoError, "Network error while sending the command.");
        }
        int n = static_cast<int>(::send(sock.h, request.data() + sent, static_cast<int>(request.size() - sent), kSendFlags));
        if (n < 0) {
            int e = net_last_error();
            if (net_would_block(e) || net_interrupted(e)) continue;
            return make(DeviceStatus::IoError, "The TomTom closed the connection while the command was being sent.");
        }
        sent += static_cast<size_t>(n);
    }

    // Read until '\n'. Capacity: a full-length line, its terminator, and one extra
    // byte so trailing garbage or an oversized line is detected without unbounded reads.
    char buf[kMaxReplyBytes + 2];
    size_t used = 0;
    while (true) {
        const char* nl = static_cast<const char*>(std::memchr(buf, '\n', used));
        if (nl) {
            size_t len = static_cast<size_t>(nl - buf);
            if (len > kMaxReplyBytes) return make(DeviceStatus::Oversized, "The TomTom returned an oversized reply.");
            if (used > len + 1) return make(DeviceStatus::BadReply, "The TomTom sent unexpected data after its reply.");
            line.assign(buf, len);
            for (char c : line)
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
                    return make(DeviceStatus::BadReply, "The TomTom returned a reply with invalid characters.");
            DeviceResult ok;
            ok.status = DeviceStatus::Ok;
            return ok;
        }
        if (used > kMaxReplyBytes) return make(DeviceStatus::Oversized, "The TomTom returned an oversized reply.");
        int ev = net_poll(sock.h, POLLIN, remaining_ms(deadline));
        if (ev == 0) return make(DeviceStatus::Timeout, "The TomTom did not answer in time.");
        if (ev < 0) {
            if (net_interrupted(net_last_error())) continue;
            return make(DeviceStatus::IoError, "Network error while reading the reply.");
        }
        int n = static_cast<int>(::recv(sock.h, buf + used, static_cast<int>(sizeof buf - used), 0));
        if (n == 0) return make(DeviceStatus::ClosedEarly, "The TomTom closed the connection without a complete reply.");
        if (n < 0) {
            int e = net_last_error();
            if (net_would_block(e) || net_interrupted(e)) continue;
            return make(DeviceStatus::IoError, "The TomTom reset the connection.");
        }
        used += static_cast<size_t>(n);
    }
}

}  // namespace

DeviceResult device_ping(const DeviceOptions& options) {
    std::string line;
    DeviceResult r = exchange(options, "PING", line);
    return r.ok() ? parse_ping_reply(line) : r;
}

DeviceResult device_status(const DeviceOptions& options) {
    std::string line;
    DeviceResult r = exchange(options, "STATUS", line);
    return r.ok() ? parse_status_reply(line) : r;
}

DeviceResult device_set_face(const DeviceOptions& options, int face_id) {
    if (!is_valid_face_id(face_id)) return make(DeviceStatus::InvalidArgument, "Face number must be between 0 and 8.");
    std::string line;
    DeviceResult r = exchange(options, "SET_FACE " + std::to_string(face_id), line);
    return r.ok() ? parse_set_face_reply(line, face_id) : r;
}

}  // namespace tt
