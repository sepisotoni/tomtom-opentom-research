// Overlapped-I/O helper shared by the driver's control channel and TomTomDisplayControl.exe.
// Win32 only; header-only so no project file has to change.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tt::idd {

// Completes an overlapped Read/Write/Connect within timeout_ms. `started` is the BOOL returned by
// the call that was issued with `overlapped` (whose hEvent must be a manual-reset event the caller
// reset before issuing it). Returns true if the operation completed successfully; on a timeout it
// cancels the operation, waits for the cancellation to finish and returns false with
// GetLastError() == WAIT_TIMEOUT. `transferred` may be null.
inline bool finish_overlapped(HANDLE handle, OVERLAPPED& overlapped, BOOL started,
                              DWORD timeout_ms, DWORD* transferred) noexcept {
    DWORD ignored = 0;
    DWORD* count = transferred ? transferred : &ignored;
    if (!started) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (WaitForSingleObject(overlapped.hEvent, timeout_ms) != WAIT_OBJECT_0) {
            CancelIoEx(handle, &overlapped);
            DWORD cancelled = 0;
            GetOverlappedResult(handle, &overlapped, &cancelled, TRUE);
            SetLastError(WAIT_TIMEOUT);
            return false;
        }
    }
    return GetOverlappedResult(handle, &overlapped, count, FALSE) != FALSE;
}

}  // namespace tt::idd
