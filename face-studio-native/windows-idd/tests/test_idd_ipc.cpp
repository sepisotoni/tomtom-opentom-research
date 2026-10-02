// Portable (no Windows APIs): the TomTomDisplayControl status line, the driver pipe protocol, the
// single-owner rule and the FrameTransport pause/counter API that the driver relies on.
#include "../../src/display/frame_transport.h"
#include "../../src/display/idd_ipc.h"
#include "../InfScan.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL " << what << "\n";
    }
}

}  // namespace

int main() {
    using namespace tt::idd_ipc;

    // ---- status line --------------------------------------------------------------------------
    check(format_status_line({false, false, 0}) == "present=0 running=0 frames=0", "format all zero");
    check(format_status_line({true, true, 12345}) == "present=1 running=1 frames=12345", "format typical");
    check(format_status_line({true, false, 18446744073709551615ULL}) ==
              "present=1 running=0 frames=18446744073709551615", "format max frames");

    Status parsed;
    check(parse_status_line("present=1 running=0 frames=7", parsed) && parsed.present && !parsed.running && parsed.frames == 7,
          "parse typical");
    check(parse_status_line("present=0 running=0 frames=0\n", parsed) && !parsed.present, "parse LF");
    check(parse_status_line("present=1 running=1 frames=9\r\n", parsed) && parsed.running && parsed.frames == 9, "parse CRLF");
    check(parse_status_line("present=1 running=1 frames=9 paused=1 extra=x", parsed) && parsed.frames == 9,
          "unknown trailing keys ignored");
    check(parse_status_line("present=1 running=1 frames=18446744073709551615", parsed) && parsed.frames == UINT64_MAX,
          "parse max frames");
    check(!parse_status_line("present=1 running=1 frames=18446744073709551616", parsed), "reject frames overflow");
    check(!parse_status_line("running=1 present=1 frames=1", parsed), "reject wrong key order");
    check(!parse_status_line("present=2 running=0 frames=0", parsed), "reject flag value 2");
    check(!parse_status_line("present=1 running=0", parsed), "reject missing frames");
    check(!parse_status_line("present=1 running=0 frames=", parsed), "reject empty frames");
    check(!parse_status_line("present=1 running=0 frames=-1", parsed), "reject negative frames");
    check(!parse_status_line("present=1  running=0 frames=0", parsed), "reject double space");
    check(!parse_status_line("", parsed), "reject empty line");
    check(!parse_status_line("present=1 running=0 frames=0 junk", parsed), "reject malformed trailing token");
    {
        Status before{true, true, 5};
        Status unchanged = before;
        check(!parse_status_line("present=0 running=0 frames=x", unchanged) && unchanged.frames == 5 && unchanged.present,
              "failed parse leaves output untouched");
    }
    for (const Status s : {Status{false, false, 0}, Status{true, false, 1}, Status{true, true, 99999999999ULL}}) {
        Status round;
        check(parse_status_line(format_status_line(s), round) && round.present == s.present && round.running == s.running &&
                  round.frames == s.frames, "round trip");
    }

    // ---- driver pipe protocol -----------------------------------------------------------------
    check(parse_request("STATUS") == Request::status, "STATUS");
    check(parse_request("STATUS\n") == Request::status, "STATUS LF");
    check(parse_request("PAUSE\r\n") == Request::pause, "PAUSE CRLF");
    check(parse_request("RESUME\n") == Request::resume, "RESUME");
    check(parse_request("status") == Request::unknown, "commands are case-sensitive");
    check(parse_request("PAUSE now") == Request::unknown, "no arguments accepted");
    check(parse_request("") == Request::unknown, "empty request");
    check(parse_request(std::string(kMaxRequestBytes + 1, 'A')) == Request::unknown, "over-long request");
    check(parse_request("STATUS\nPAUSE\n") == Request::status, "only the first line counts");

    bool running = true;
    std::uint64_t frames = 0;
    check(format_status_reply(true, 42) == "OK running=1 frames=42", "format reply");
    check(parse_status_reply("OK running=0 frames=7\n", running, frames) && !running && frames == 7, "parse reply");
    check(!parse_status_reply("ERR UNKNOWN_COMMAND", running, frames), "ERR is not a status reply");
    check(!parse_status_reply("OK running=1", running, frames), "incomplete reply");
    check(is_ok_reply("OK\n") && is_ok_reply("OK running=1 frames=0") && !is_ok_reply("ERR X") && !is_ok_reply("OKAY"),
          "is_ok_reply");

    // ---- single-owner rule --------------------------------------------------------------------
    check(streaming_allowed(false, false), "free -> may stream");
    check(!streaming_allowed(true, false), "paused -> must not stream");
    check(!streaming_allowed(false, true), "Studio lease -> must not stream");
    check(!streaming_allowed(true, true), "both -> must not stream");

    // ---- keepalive decision (driver swap-chain loop) -------------------------------------------------
    {
        using std::chrono::milliseconds;
        check(!should_resend_last_frame(false, true, true, milliseconds(5000)), "keepalive: never while the gate is closed");
        check(!should_resend_last_frame(true, false, true, milliseconds(5000)), "keepalive: nothing to send before the first frame");
        check(!should_resend_last_frame(true, true, true, milliseconds(999)), "keepalive: not due at 999 ms");
        check(should_resend_last_frame(true, true, true, milliseconds(1000)), "keepalive: due at 1000 ms");
        check(should_resend_last_frame(true, true, true, milliseconds(60000)), "keepalive: due after a long idle");
        check(should_resend_last_frame(true, true, false, milliseconds(0)), "keepalive: gate just reopened -> send at once");
        check(!should_resend_last_frame(false, true, false, milliseconds(0)), "keepalive: still closed -> nothing");
        check(kKeepAliveInterval < std::chrono::seconds(2), "keepalive must beat the receiver's 2 s idle timeout");
    }

    // ---- exit codes are part of the documented contract ---------------------------------------
    check(exit_code::ok == 0 && exit_code::internal_error == 1 && exit_code::not_installed == 2 &&
              exit_code::access_denied == 3 && exit_code::device_faulted == 4 && exit_code::usage == 5 &&
              exit_code::driver_unresponsive == 6 && exit_code::reboot_required == 7 && exit_code::already_running == 8,
          "exit codes match windows-idd/README.md");

    // ---- staged-INF ownership (uninstall must only ever delete OUR package) ----------------------
    {
        const std::string ansi =
            "[Manufacturer]\r\n%ProviderName%=Models,NTamd64\r\n[Models.NTamd64]\r\n"
            "%DeviceName%=Device_Install,Root\\TomTomIndirectDisplay\r\n[SourceDisksFiles]\r\nTomTomIdd.dll=1\r\n";
        std::string utf16 = "\xFF\xFE";
        for (const char c : ansi) { utf16 += c; utf16 += '\0'; }
        check(tt::idd::inf_belongs_to_tomtom_idd(ansi), "ANSI INF is recognised");
        check(tt::idd::inf_belongs_to_tomtom_idd(utf16), "UTF-16LE INF is recognised");
        check(!tt::idd::inf_belongs_to_tomtom_idd("Root\\TomTomIndirectDisplay only"), "hardware ID alone is not enough");
        check(!tt::idd::inf_belongs_to_tomtom_idd("TomTomIdd.dll only"), "binary name alone is not enough");
        check(!tt::idd::inf_belongs_to_tomtom_idd("[Version]\r\nSignature=\"$Windows NT$\"\r\n"), "unrelated INF rejected");
        check(!tt::idd::inf_belongs_to_tomtom_idd(""), "empty file rejected");
        check(!tt::idd::inf_belongs_to_tomtom_idd("\xFF\xFE"), "BOM-only file rejected");
        std::string upper = ansi;
        for (char& c : upper) { if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); }
        check(tt::idd::inf_belongs_to_tomtom_idd(upper), "matching is case-insensitive");
    }

    // ---- FrameTransport pause / counter (no network: the worker is never given a reachable peer) ----
    {
        using tt::display::FrameTransport;
        std::array<std::uint8_t, tt::display::kFrameBytes> frame{};
        FrameTransport transport;
        check(!transport.paused() && transport.frames_sent() == 0, "fresh transport: not paused, no frames");

        transport.set_paused(true);  // allowed while stopped
        check(transport.paused(), "pause while stopped is remembered");
        check(transport.start(), "start while paused");
        check(!transport.submit_frame(frame.data(), frame.size()), "paused transport rejects frames");
        transport.set_paused(false);
        check(!transport.paused(), "resume");
        check(transport.submit_frame(frame.data(), frame.size()), "resumed transport accepts frames");
        transport.set_paused(true);
        transport.set_paused(true);  // idempotent
        check(!transport.submit_frame(frame.data(), frame.size()), "pause again rejects frames");
        transport.stop();
        check(transport.paused(), "pause survives stop()");
        check(transport.frames_sent() == 0, "no receiver, no ACKed frames");
        check(transport.start() && !transport.submit_frame(frame.data(), frame.size()), "pause survives start()");
        transport.set_paused(false);
        transport.stop();
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " IDD contract checks FAILED\n";
        return 1;
    }
    std::cout << "idd contract: " << g_checks << " checks passed\n";
    return 0;
}
