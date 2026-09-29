// Client for the TomTom's fixed-command USB control service (TCP 18743).
//
// The service is deliberately narrow: exactly PING, STATUS and SET_FACE <id>.
// This client never sends anything else, has no shell/Telnet fallback, only
// connects to private / link-local / loopback IPv4 literals, and never listens.
// The protocol is plaintext and unauthenticated: use it over a direct USB link
// only, and never bridge or expose that link to Wi-Fi or the internet.
#pragma once
#include <string>

namespace tt {

struct DeviceFace {
    int id;
    const char* name;
};
extern const DeviceFace kDeviceFaces[9];
constexpr int kDeviceFaceCount = 9;
constexpr int kDefaultDevicePort = 18743;
extern const char* const kDefaultDeviceHost;  // "192.168.101.115"
constexpr size_t kMaxReplyBytes = 64;         // longest accepted reply line (excluding '\n')

bool is_valid_face_id(int id);
const char* face_name(int id);  // nullptr for unknown IDs

enum class DeviceStatus {
    Ok,
    InvalidArgument,  // bad host / face ID; nothing was sent
    ConnectFailed,    // refused / unreachable
    Timeout,          // connect or read deadline expired
    ClosedEarly,      // peer closed before a full line arrived
    Oversized,        // reply line longer than kMaxReplyBytes
    BadReply,         // malformed or unexpected reply
    DeviceRejected,   // well-formed "ERR <CODE>" reply
    IoError,          // socket API failure
};

struct DeviceResult {
    DeviceStatus status = DeviceStatus::IoError;
    int face_id = -1;         // set by STATUS / SET_FACE on success
    std::string message;      // user-facing text (always set)
    std::string device_code;  // "USB_ONLY", "INVALID_FACE", ... for DeviceRejected
    bool ok() const { return status == DeviceStatus::Ok; }
};

struct DeviceOptions {
    std::string host = kDefaultDeviceHost;
    int port = kDefaultDevicePort;  // overridable for tests only
    int connect_timeout_ms = 2000;
    int read_timeout_ms = 3000;  // total budget for send + reply
};

// Accepts a trimmed dotted-quad IPv4 literal in 10/8, 172.16/12, 192.168/16,
// 169.254/16 or 127/8. Hostnames (and therefore DNS) are never used.
bool parse_device_host(const std::string& text, std::string& normalized, std::string& error);
// True for the TomTom's USB subnet 192.168.101.0/24 (UI shows a caution otherwise).
bool is_usb_link_address(const std::string& normalized_host);

// Pure reply parsers (input excludes the trailing '\n').
DeviceResult parse_ping_reply(const std::string& line);
DeviceResult parse_status_reply(const std::string& line);
DeviceResult parse_set_face_reply(const std::string& line, int requested_id);

// Network operations: blocking but bounded by the option timeouts. Call them
// from a worker thread in GUI code.
DeviceResult device_ping(const DeviceOptions& options);
DeviceResult device_status(const DeviceOptions& options);
DeviceResult device_set_face(const DeviceOptions& options, int face_id);

}  // namespace tt
