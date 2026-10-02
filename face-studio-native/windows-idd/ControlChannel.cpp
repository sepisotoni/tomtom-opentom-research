#include "ControlChannel.h"

#include "OverlappedIo.h"
#include "../src/display/idd_ipc.h"

#include <sddl.h>

#include <new>
#include <string>
#include <string_view>

namespace tt::idd {
namespace {

// SYSTEM and Administrators: full control. Authenticated Users: read/write data only - deliberately
// without FILE_APPEND_DATA (= FILE_CREATE_PIPE_INSTANCE on a pipe) so nobody else can create instances.
// 0x00100083 = FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE.
constexpr wchar_t kPipeSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100083;;;AU)";

// A name nobody ever creates; used once to learn what OpenEventW reports for a missing object.
constexpr wchar_t kProbeMissingName[] = L"Global\\TomTomFaceStudio.IddProbeNeverCreated";

}  // namespace

ControlChannel::~ControlChannel() {
    stop();
}

bool ControlChannel::start() noexcept {
    if (thread_.joinable()) {
        return true;
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    connect_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    io_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_ || !connect_event_ || !io_event_) {
        stop();
        return false;
    }

    // Learn how a MISSING object looks from this process. Normally ERROR_FILE_NOT_FOUND. If the host
    // sandbox reports ACCESS_DENIED for everything, ACCESS_DENIED must not be read as "a lease exists"
    // (that would silence the driver forever); then only a successful open counts as a lease.
    if (HANDLE probe = OpenEventW(SYNCHRONIZE, FALSE, kProbeMissingName)) {
        CloseHandle(probe);
    } else {
        denied_means_exists_ = GetLastError() != ERROR_ACCESS_DENIED;
        if (!denied_means_exists_) {
            OutputDebugStringW(
                L"TomTom IDD: object namespace reports ACCESS_DENIED for missing names; "
                L"Studio mirror lease detection only works for objects this process may open.\n");
        }
    }

    evaluate_gate();  // a Studio that is already mirroring must be honoured from the first frame
    pipe_ = create_pipe();  // synchronous: a client that connects right after start() must find it
    try {
        thread_ = std::thread(&ControlChannel::run, this);
    } catch (...) {
        stop();
        return false;
    }
    return true;
}

void ControlChannel::stop() noexcept {
    if (thread_.joinable()) {
        if (stop_event_) {
            SetEvent(stop_event_);
        }
        thread_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    for (HANDLE* handle : {&stop_event_, &connect_event_, &io_event_}) {
        if (*handle) {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}

void ControlChannel::attach(tt::display::FrameTransport* transport) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    transport_ = transport;
    apply_gate_locked();
}

void ControlChannel::detach(tt::display::FrameTransport* transport) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transport_ == transport && transport != nullptr) {
        frames_base_ += transport->frames_sent();
        transport_ = nullptr;
    }
}

bool ControlChannel::lease_present() const noexcept {
    HANDLE handle = OpenEventW(SYNCHRONIZE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
    if (handle) {
        CloseHandle(handle);
        return true;
    }
    const DWORD error = GetLastError();
    // The Studio creates the event with its own default DACL, which does not have to grant this
    // service account anything: ACCESS_DENIED still proves the name exists. ERROR_INVALID_HANDLE
    // means the name exists but is a different object type (e.g. a mutex) - also a lease.
    return (denied_means_exists_ && error == ERROR_ACCESS_DENIED) || error == ERROR_INVALID_HANDLE;
}

void ControlChannel::evaluate_gate() noexcept {
    const bool lease = lease_present();
    std::lock_guard<std::mutex> lock(mutex_);
    lease_ = lease;
    apply_gate_locked();
}

void ControlChannel::apply_gate_locked() noexcept {
    const bool allowed = tt::idd_ipc::streaming_allowed(paused_, lease_);
    // Opening: un-pause the transport BEFORE publishing the flag, so a producer that sees "allowed"
    // never finds a still-paused transport. Closing: publish first, so it stops submitting at once.
    if (allowed) {
        if (transport_) {
            transport_->set_paused(false);
        }
        allowed_.store(true);
    } else {
        allowed_.store(false);
        if (transport_) {
            transport_->set_paused(true);
        }
    }
}

HANDLE ControlChannel::create_pipe() noexcept {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kPipeSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        OutputDebugStringW(L"TomTom IDD: could not build the control pipe security descriptor.\n");
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;

    // FIRST_PIPE_INSTANCE: refuse to run on a name somebody else already created (squatting).
    // REJECT_REMOTE_CLIENTS: this pipe is local-only, never reachable over SMB.
    const HANDLE pipe = CreateNamedPipeW(
        tt::idd_ipc::kControlPipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 256, 256, 0, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) {
        OutputDebugStringW(
            L"TomTom IDD: could not create the control pipe (name taken?); status/pause are unavailable.\n");
    }
    return pipe;
}

void ControlChannel::serve(HANDLE pipe) noexcept {
    try {
        OVERLAPPED overlapped{};
        overlapped.hEvent = io_event_;

        char request_bytes[tt::idd_ipc::kMaxRequestBytes + 1] = {};
        DWORD transferred = 0;
        ResetEvent(io_event_);
        BOOL started = ReadFile(pipe, request_bytes, sizeof(request_bytes), &transferred, &overlapped);
        if (!finish_overlapped(pipe, overlapped, started, kPipeIoTimeoutMs, &transferred)) {
            return;
        }

        std::string reply;
        const auto request = tt::idd_ipc::parse_request(std::string_view(request_bytes, transferred));
        switch (request) {
            case tt::idd_ipc::Request::status: {
                bool running = false;
                std::uint64_t frames = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    running = transport_ != nullptr && allowed_.load();
                    frames = frames_base_ + (transport_ ? transport_->frames_sent() : 0);
                }
                reply = tt::idd_ipc::format_status_reply(running, frames);
                break;
            }
            case tt::idd_ipc::Request::pause:
            case tt::idd_ipc::Request::resume: {
                std::lock_guard<std::mutex> lock(mutex_);
                paused_ = request == tt::idd_ipc::Request::pause;
                apply_gate_locked();
                reply = "OK";
                break;
            }
            case tt::idd_ipc::Request::unknown:
            default:
                reply = "ERR UNKNOWN_COMMAND";
                break;
        }
        reply += '\n';

        ResetEvent(io_event_);
        started = WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &transferred, &overlapped);
        if (!finish_overlapped(pipe, overlapped, started, kPipeIoTimeoutMs, &transferred)) {
            return;
        }

        // DisconnectNamedPipe discards unread data, including our reply: keep reading (and ignoring) whatever
        // the client still has queued until it hangs up, bounded in time and in number of reads.
        const ULONGLONG drain_deadline = GetTickCount64() + kPipeIoTimeoutMs;
        for (int reads = 0; reads < 16; ++reads) {
            const ULONGLONG now = GetTickCount64();
            if (now >= drain_deadline) {
                break;
            }
            char drain[64];
            ResetEvent(io_event_);
            started = ReadFile(pipe, drain, sizeof(drain), &transferred, &overlapped);
            if (!finish_overlapped(pipe, overlapped, started, static_cast<DWORD>(drain_deadline - now), &transferred)) {
                break;  // the client closed its end (ERROR_BROKEN_PIPE) or the deadline passed
            }
        }
    } catch (...) {
        // Out of memory while formatting a reply: drop this request, keep the channel alive.
    }
}

void ControlChannel::run() noexcept {
    HANDLE pipe = pipe_;
    OVERLAPPED connect_overlapped{};
    connect_overlapped.hEvent = connect_event_;
    bool connect_pending = false;
    ULONGLONG next_poll = 0;

    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= next_poll) {
            evaluate_gate();
            next_poll = now + kGatePollMs;
        }

        if (pipe != INVALID_HANDLE_VALUE && !connect_pending) {
            ResetEvent(connect_event_);
            const BOOL connected = ConnectNamedPipe(pipe, &connect_overlapped);
            const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (connected || error == ERROR_PIPE_CONNECTED) {
                serve(pipe);
                DisconnectNamedPipe(pipe);
                continue;  // re-evaluates the gate first if it is due, so a chatty client cannot starve it
            }
            if (error == ERROR_IO_PENDING) {
                connect_pending = true;
            } else {
                OutputDebugStringW(L"TomTom IDD: control pipe connect failed; status/pause are unavailable.\n");
                pipe = INVALID_HANDLE_VALUE;  // stop() closes the handle
            }
        }

        const ULONGLONG after = GetTickCount64();
        DWORD timeout = kGatePollMs;
        if (next_poll > after) {
            timeout = static_cast<DWORD>(next_poll - after);
        } else {
            timeout = 1;
        }
        HANDLE waits[2] = {stop_event_, connect_event_};
        const DWORD count = connect_pending ? 2U : 1U;
        const DWORD wait = WaitForMultipleObjects(count, waits, FALSE, timeout);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1 && connect_pending) {
            connect_pending = false;
            DWORD ignored = 0;
            if (GetOverlappedResult(pipe, &connect_overlapped, &ignored, FALSE)) {
                serve(pipe);
            }
            DisconnectNamedPipe(pipe);
        } else if (wait != WAIT_TIMEOUT) {
            break;  // WAIT_FAILED: nothing sensible left to do
        }
    }

    if (pipe != INVALID_HANDLE_VALUE) {
        if (connect_pending) {
            CancelIoEx(pipe, &connect_overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(pipe, &connect_overlapped, &ignored, TRUE);
        }
    }
}

}  // namespace tt::idd
