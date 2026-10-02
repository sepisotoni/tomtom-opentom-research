// Contract shared by the TomTom IDD driver (windows-idd/), TomTomDisplayControl.exe and the Studio.
//
// Platform-neutral on purpose (no Windows headers) so the formatting/parsing below is unit-tested on
// every platform. The Studio may include this header to get the exact object names and exit codes;
// the rules built on them are written down in face-studio-native/windows-idd/README.md.
//
// Everything here is USB-only plumbing for the unauthenticated display receiver on TCP 18745.
// None of these objects carries a network address and none of them may be bridged to a LAN.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tt::idd_ipc {

// ---- Win32 object names -----------------------------------------------------------------------

// Driver-owned control pipe (STATUS / PAUSE / RESUME, one text line each way). Local clients only
// (the driver creates it with PIPE_REJECT_REMOTE_CLIENTS).
inline constexpr wchar_t kControlPipeName[] = L"\\\\.\\pipe\\TomTomFaceStudio.IddControl";

// Studio-owned lease. While ANY handle to a *named event* with this name exists, the driver must not
// hold the TCP 18745 connection. The Studio creates it (CreateEventW) before it connects its own
// FrameTransport and closes it after that transport has stopped. Kernel handles die with the process,
// so a crashed Studio releases the lease automatically.
inline constexpr wchar_t kMirrorLeaseEventName[] = L"Global\\TomTomFaceStudio.MirrorLease";

// Held by the elevated `TomTomDisplayControl.exe enable` process for as long as the virtual display
// device exists (single-instance guard, and `disable` waits on it).
inline constexpr wchar_t kDeviceHostMutexName[] = L"Global\\TomTomFaceStudio.IddDeviceHost";

// Signalled by `TomTomDisplayControl.exe disable` to ask the `enable` host to remove the device.
// Administrators/SYSTEM only.
inline constexpr wchar_t kDeviceHostStopEventName[] = L"Global\\TomTomFaceStudio.IddDeviceHostStop";

// Hardware ID of the software device / INF match.
inline constexpr wchar_t kHardwareId[] = L"Root\\TomTomIndirectDisplay";

// Explicit pipe access mask a client must request: FILE_READ_DATA | FILE_WRITE_DATA |
// FILE_READ_ATTRIBUTES | SYNCHRONIZE. Do NOT use GENERIC_READ/GENERIC_WRITE: they include
// FILE_APPEND_DATA (= FILE_CREATE_PIPE_INSTANCE on pipes) and more than the pipe's DACL grants.
inline constexpr unsigned long kControlPipeClientAccess = 0x00100083UL;

// ---- TomTomDisplayControl.exe exit codes -------------------------------------------------------

namespace exit_code {
inline constexpr int ok = 0;                  // success
inline constexpr int internal_error = 1;      // unexpected failure; details on stderr
inline constexpr int not_installed = 2;       // driver package / virtual display device not found
inline constexpr int access_denied = 3;       // needs an elevated (Administrator) process
inline constexpr int device_faulted = 4;      // device node exists but is not started (problem code on stderr)
inline constexpr int usage = 5;               // unknown command or bad arguments
inline constexpr int driver_unresponsive = 6; // driver control pipe not reachable / not answering
inline constexpr int reboot_required = 7;     // operation completed, Windows needs a restart
inline constexpr int already_running = 8;     // `enable` host already running
}  // namespace exit_code

// ---- `status` line -----------------------------------------------------------------------------

struct Status {
    bool present = false;        // device node exists, is started and has no problem code
    bool running = false;        // driver is armed to stream: swap chain assigned and not paused/yielded
    std::uint64_t frames = 0;    // frames ACKed by the receiver since the driver (adapter) loaded
};

namespace detail {

inline bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty() || text.size() > 20) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;  // overflow
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

inline bool parse_flag(std::string_view text, bool& out) noexcept {
    if (text == "0") { out = false; return true; }
    if (text == "1") { out = true; return true; }
    return false;
}

inline std::string_view trim_line(std::string_view line) noexcept {
    const std::size_t newline = line.find('\n');
    if (newline != std::string_view::npos) line = line.substr(0, newline);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t' || line.back() == '\0')) {
        line.remove_suffix(1);
    }
    return line;
}

// Splits "key=value" starting at `pos`, advancing past one separating space. Empty tokens fail.
inline bool next_pair(std::string_view line, std::size_t& pos, std::string_view& key, std::string_view& value) noexcept {
    if (pos >= line.size()) return false;
    std::size_t end = line.find(' ', pos);
    if (end == std::string_view::npos) end = line.size();
    const std::string_view token = line.substr(pos, end - pos);
    const std::size_t eq = token.find('=');
    if (eq == std::string_view::npos || eq == 0 || eq + 1 >= token.size()) return false;
    key = token.substr(0, eq);
    value = token.substr(eq + 1);
    pos = end < line.size() ? end + 1 : end;
    return true;
}

}  // namespace detail

// "present=1 running=0 frames=0" (no newline; the tool prints one "\n" after it).
inline std::string format_status_line(const Status& status) {
    std::string line = "present=";
    line += status.present ? '1' : '0';
    line += " running=";
    line += status.running ? '1' : '0';
    line += " frames=";
    line += std::to_string(status.frames);
    return line;
}

// Strict about the three leading keys and their order; tolerant of a trailing CR/LF and of further
// `key=value` tokens a later tool version might append (they are ignored).
inline bool parse_status_line(std::string_view line, Status& out) noexcept {
    line = detail::trim_line(line);
    std::size_t pos = 0;
    std::string_view key, value;
    Status parsed;
    if (!detail::next_pair(line, pos, key, value) || key != "present" || !detail::parse_flag(value, parsed.present)) return false;
    if (!detail::next_pair(line, pos, key, value) || key != "running" || !detail::parse_flag(value, parsed.running)) return false;
    if (!detail::next_pair(line, pos, key, value) || key != "frames" || !detail::parse_u64(value, parsed.frames)) return false;
    while (pos < line.size()) {
        if (!detail::next_pair(line, pos, key, value)) return false;  // malformed extra token
    }
    out = parsed;
    return true;
}

// ---- driver control pipe protocol ---------------------------------------------------------------
// One request line per connection, one reply line, then the server disconnects.
//   "STATUS"  -> "OK running=<0|1> frames=<n>"
//   "PAUSE"   -> "OK"        (streaming disabled until RESUME, or until the driver host restarts)
//   "RESUME"  -> "OK"
//   anything else -> "ERR UNKNOWN_COMMAND"

enum class Request { unknown, status, pause, resume };

inline constexpr std::size_t kMaxRequestBytes = 32;

inline Request parse_request(std::string_view text) noexcept {
    const std::string_view line = detail::trim_line(text);
    if (line.size() > kMaxRequestBytes) return Request::unknown;
    if (line == "STATUS") return Request::status;
    if (line == "PAUSE") return Request::pause;
    if (line == "RESUME") return Request::resume;
    return Request::unknown;
}

inline std::string format_status_reply(bool running, std::uint64_t frames) {
    std::string reply = "OK running=";
    reply += running ? '1' : '0';
    reply += " frames=";
    reply += std::to_string(frames);
    return reply;
}

inline bool parse_status_reply(std::string_view text, bool& running, std::uint64_t& frames) noexcept {
    const std::string_view line = detail::trim_line(text);
    if (line.substr(0, 3) != "OK ") return false;
    std::size_t pos = 3;
    std::string_view key, value;
    bool parsed_running = false;
    std::uint64_t parsed_frames = 0;
    if (!detail::next_pair(line, pos, key, value) || key != "running" || !detail::parse_flag(value, parsed_running)) return false;
    if (!detail::next_pair(line, pos, key, value) || key != "frames" || !detail::parse_u64(value, parsed_frames)) return false;
    running = parsed_running;
    frames = parsed_frames;
    return true;
}

inline bool is_ok_reply(std::string_view text) noexcept {
    const std::string_view line = detail::trim_line(text);
    return line == "OK" || line.substr(0, 3) == "OK ";
}

// ---- the single-owner rule ----------------------------------------------------------------------
// The driver may hold the TCP 18745 connection only if nobody asked it to stand down.
//   paused       : an explicit PAUSE is in effect (TomTomDisplayControl.exe pause)
//   mirror_lease : a Studio mirror lease event currently exists
constexpr bool streaming_allowed(bool paused, bool mirror_lease) noexcept {
    return !paused && !mirror_lease;
}

// ---- keepalive ----------------------------------------------------------------------------------
// While streaming is allowed the driver re-sends its last converted frame at least this often, even if
// the desktop is static: the receiver drops a client that is silent for 2 s, a restarted receiver must be
// repainted, and a device-side "no frames -> restore the clock" watchdog must not fire on an idle desktop.
// Keep it below half of that watchdog's timeout.
inline constexpr std::chrono::milliseconds kKeepAliveInterval{1000};

// The swap-chain loop asks this on every iteration and on a 250 ms timer.
//   allowed    : the gate is open (not paused, no Studio lease)
//   have_frame : at least one frame has been converted
//   was_allowed: the gate state seen on the previous call (true -> reopening just happened if !was_allowed)
//   since_last_submit: time since the last submit attempt
constexpr bool should_resend_last_frame(bool allowed, bool have_frame, bool was_allowed,
                                        std::chrono::steady_clock::duration since_last_submit) noexcept {
    return allowed && have_frame && (!was_allowed || since_last_submit >= kKeepAliveInterval);
}

}  // namespace tt::idd_ipc
