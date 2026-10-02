// Client side of the driver control pipe (STATUS / PAUSE / RESUME). Win32 only, header-only.
// Used by TomTomDisplayControl.exe and by the control-channel test.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "OverlappedIo.h"
#include "../src/display/idd_ipc.h"

namespace tt::idd {

// Sends one request line and reads the one-line reply, all within total_timeout_ms.
// On failure returns false with `error` = the Win32 error (ERROR_FILE_NOT_FOUND: no driver/pipe,
// ERROR_ACCESS_DENIED, WAIT_TIMEOUT: the server did not answer, ...).
inline bool control_pipe_request(const char* request, std::string& reply, DWORD& error,
                                DWORD total_timeout_ms = 1000) noexcept {
    reply.clear();
    error = ERROR_SUCCESS;
    const ULONGLONG deadline = GetTickCount64() + total_timeout_ms;
    auto remaining_ms = [deadline]() -> DWORD {
        const ULONGLONG now = GetTickCount64();
        return now >= deadline ? 0U : static_cast<DWORD>(deadline - now);
    };

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        // Explicit access mask (see kControlPipeClientAccess); anonymous impersonation so a process
        // that squatted the pipe name cannot act as us.
        pipe = CreateFileW(tt::idd_ipc::kControlPipeName,
                           static_cast<DWORD>(tt::idd_ipc::kControlPipeClientAccess), 0, nullptr,
                           OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        error = GetLastError();
        const DWORD left = remaining_ms();
        if (error != ERROR_PIPE_BUSY || left == 0) {
            return false;
        }
        WaitNamedPipeW(tt::idd_ipc::kControlPipeName, left < 100 ? left : 100);
    }

    bool ok = false;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        std::string line = request;
        line += '\n';
        DWORD transferred = 0;
        ResetEvent(event);
        BOOL started = WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &transferred, &overlapped);
        ok = finish_overlapped(pipe, overlapped, started, remaining_ms(), &transferred);
        while (ok && reply.find('\n') == std::string::npos && reply.size() < 160) {
            char buffer[64];
            ResetEvent(event);
            started = ReadFile(pipe, buffer, sizeof(buffer), &transferred, &overlapped);
            ok = finish_overlapped(pipe, overlapped, started, remaining_ms(), &transferred);
            if (ok) {
                reply.append(buffer, transferred);
            }
        }
        if (!ok) {
            error = GetLastError();
        }
        CloseHandle(event);
    } else {
        error = GetLastError();
    }
    CloseHandle(pipe);
    return ok;
}

}  // namespace tt::idd
