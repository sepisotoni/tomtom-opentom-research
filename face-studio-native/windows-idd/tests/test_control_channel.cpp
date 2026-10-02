// Windows-only: exercises the driver's ControlChannel (gate + control pipe) without the WDK, using the
// real named pipe and the real Global\ lease event. Fails loudly ("FAIL ...") for CI annotations.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>

#include "../ControlChannel.h"
#include "../ControlPipeClient.h"
#include "../../src/display/frame_transport.h"
#include "../../src/display/idd_ipc.h"
#include "../../src/display/mirror_lease.h"

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

template <typename Predicate>
bool wait_for(Predicate predicate, DWORD timeout_ms = 3000) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        if (predicate()) return true;
        Sleep(20);
    }
    return predicate();
}

std::string ask(const char* request) {
    std::string reply;
    DWORD error = 0;
    if (!tt::idd::control_pipe_request(request, reply, error, 2000)) {
        return "<error " + std::to_string(error) + ">";
    }
    while (!reply.empty() && (reply.back() == '\n' || reply.back() == '\r')) reply.pop_back();
    return reply;
}

}  // namespace

int main() {
    using tt::display::FrameTransport;
    using tt::idd::ControlChannel;

    {
        FrameTransport transport;  // never started: no network traffic in this test
        ControlChannel channel;
        check(channel.start(), "channel starts");
        check(channel.start(), "start is idempotent");
        check(channel.streaming_allowed(), "streaming allowed by default");

        check(ask("STATUS") == "OK running=0 frames=0", "no transport attached -> running=0");

        channel.attach(&transport);
        check(ask("STATUS") == "OK running=1 frames=0", "attached and allowed -> running=1");

        check(ask("PAUSE") == "OK", "PAUSE accepted");
        check(transport.paused(), "PAUSE pauses the attached transport");
        check(!channel.streaming_allowed(), "PAUSE closes the gate");
        check(ask("STATUS") == "OK running=0 frames=0", "paused -> running=0");
        check(ask("RESUME") == "OK", "RESUME accepted");
        check(!transport.paused(), "RESUME resumes the transport");
        check(channel.streaming_allowed(), "RESUME opens the gate");
        check(ask("STATUS") == "OK running=1 frames=0", "resumed -> running=1");

        check(ask("BOGUS") == "ERR UNKNOWN_COMMAND", "unknown command rejected");
        check(ask("STATUSSTATUSSTATUSSTATUSSTATUSSTATUSSTATUS") == "ERR UNKNOWN_COMMAND", "over-long request rejected");

        // Studio lease: the driver must stand down within a few poll intervals, and take over again after.
        HANDLE lease = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        check(lease != nullptr, "test can create the lease event");
        check(wait_for([&] { return !channel.streaming_allowed() && transport.paused(); }), "lease closes the gate");
        check(ask("STATUS") == "OK running=0 frames=0", "leased -> running=0");

        // Explicit pause must survive the lease going away.
        check(ask("PAUSE") == "OK", "PAUSE while leased");
        CloseHandle(lease);
        Sleep(3 * ControlChannel::kGatePollMs);
        check(!channel.streaming_allowed() && transport.paused(), "still paused after lease released");
        check(ask("RESUME") == "OK", "RESUME after lease released");
        check(wait_for([&] { return channel.streaming_allowed() && !transport.paused(); }), "gate reopens");

        // A lease alone, released again (the 'Studio crashed' path is the same: the handle just closes).
        lease = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        check(wait_for([&] { return !channel.streaming_allowed(); }), "second lease closes the gate");
        CloseHandle(lease);
        check(wait_for([&] { return channel.streaming_allowed() && !transport.paused(); }), "driver resumes by itself");

        // A mutex under the lease name still counts as a lease (OpenEventW -> ERROR_INVALID_HANDLE).
        HANDLE wrong_type = CreateMutexW(nullptr, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        check(wrong_type != nullptr, "test can create a same-named mutex");
        check(wait_for([&] { return !channel.streaming_allowed(); }), "wrong-typed object still closes the gate");
        CloseHandle(wrong_type);
        check(wait_for([&] { return channel.streaming_allowed(); }), "gate reopens after wrong-typed object is gone");

        // A client that connects and says nothing must not stall the gate.
        HANDLE silent = CreateFileW(tt::idd_ipc::kControlPipeName, static_cast<DWORD>(tt::idd_ipc::kControlPipeClientAccess),
                                    0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
        check(silent != INVALID_HANDLE_VALUE, "silent client connects");
        lease = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        check(wait_for([&] { return !channel.streaming_allowed(); }, 2500), "gate still evaluated while a client is silent");
        CloseHandle(lease);
        if (silent != INVALID_HANDLE_VALUE) CloseHandle(silent);
        check(wait_for([&] { return ask("STATUS") == "OK running=1 frames=0"; }), "channel serves again after the silent client");

        // detach keeps the running total and stops driving the transport.
        channel.detach(&transport);
        check(ask("STATUS") == "OK running=0 frames=0", "detached -> running=0");
        transport.set_paused(false);
        channel.stop();
    }

    {
        // Name squatting: somebody else already owns the pipe name. The driver must still start and
        // must still honour the lease; only status/pause become unavailable.
        HANDLE squatter = CreateNamedPipeW(tt::idd_ipc::kControlPipeName, PIPE_ACCESS_DUPLEX,
                                           PIPE_TYPE_BYTE | PIPE_WAIT, 1, 64, 64, 0, nullptr);
        check(squatter != INVALID_HANDLE_VALUE, "test can squat the pipe name");
        tt::display::FrameTransport transport;
        ControlChannel channel;
        check(channel.start(), "channel starts even if the pipe name is squatted");
        channel.attach(&transport);
        HANDLE lease = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        check(wait_for([&] { return transport.paused(); }), "lease works without a control pipe");
        CloseHandle(lease);
        channel.detach(&transport);
        channel.stop();
        if (squatter != INVALID_HANDLE_VALUE) CloseHandle(squatter);
    }

    {
        // A Studio that is already mirroring when the driver starts must be honoured from the start.
        HANDLE lease = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        ControlChannel channel;
        check(channel.start(), "channel starts while a lease exists");
        check(!channel.streaming_allowed(), "lease present at start -> gate closed immediately");
        channel.stop();
        CloseHandle(lease);
    }

    {
        // The Studio-side helper, against the real channel.
        tt::display::FrameTransport transport;
        ControlChannel channel;
        check(channel.start(), "channel starts (lease helper test)");
        channel.attach(&transport);
        {
            tt::display::MirrorLease first;
            tt::display::MirrorLease second;
            check(!first.held(), "lease not held before acquire");
            check(first.acquire() && first.held(), "first Studio instance acquires the lease");
            check(first.acquire(), "acquire is idempotent");
            check(wait_for([&] { return !channel.streaming_allowed() && transport.paused(); }), "MirrorLease closes the driver gate");
            check(!second.acquire() && !second.held() && second.last_error() == ERROR_ALREADY_EXISTS,
                  "a second holder is refused with ERROR_ALREADY_EXISTS");
            check(channel.streaming_allowed() == false, "refused second holder did not release the first lease");
            first.release();
            check(!first.held(), "release");
            check(wait_for([&] { return channel.streaming_allowed() && !transport.paused(); }), "driver resumes after release");
            check(second.acquire(), "lease can be re-acquired after release");
        }  // destructor releases
        check(wait_for([&] { return channel.streaming_allowed(); }), "destructor releases the lease");
        channel.detach(&transport);
        channel.stop();
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " control channel checks FAILED\n";
        return 1;
    }
    std::cout << "control channel: " << g_checks << " checks passed\n";
    return 0;
}
