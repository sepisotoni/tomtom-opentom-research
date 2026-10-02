// Studio-side half of the single-owner rule for the TomTom receiver's one TCP client (TCP 18745).
//
// While a MirrorLease is held, the IDD driver (if installed) releases its connection and does not
// reconnect, so the Studio's mirror FrameTransport is the only client. Header-only, Windows only.
//
// Usage in mirror start/stop (see windows-idd/README.md, "Sharing TCP 18745"):
//     start:  lease.acquire()  ->  (optionally wait ~300 ms)  ->  transport.start()
//     stop:   transport.stop() ->  lease.release()          // in THIS order
// If the Studio crashes the kernel closes the handle and the driver takes over again by itself.
#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "idd_ipc.h"

namespace tt::display {

class MirrorLease {
public:
    MirrorLease() = default;
    ~MirrorLease() { release(); }
    MirrorLease(const MirrorLease&) = delete;
    MirrorLease& operator=(const MirrorLease&) = delete;

    // true: this object now holds the lease (also true if it already did).
    // false: somebody else holds it - typically a second Studio instance that is already mirroring
    // (last_error() == ERROR_ALREADY_EXISTS) - or it could not be created. Do not start mirroring.
    bool acquire() noexcept {
        if (handle_ != nullptr) {
            return true;
        }
        SetLastError(ERROR_SUCCESS);  // CreateEventW does not reliably clear a stale error on success
        HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kMirrorLeaseEventName);
        const DWORD error = GetLastError();
        if (handle == nullptr) {
            error_ = error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
            return false;
        }
        if (error == ERROR_ALREADY_EXISTS) {
            CloseHandle(handle);  // the object lives on through the other holder
            error_ = ERROR_ALREADY_EXISTS;
            return false;
        }
        handle_ = handle;
        error_ = ERROR_SUCCESS;
        return true;
    }

    void release() noexcept {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    bool held() const noexcept { return handle_ != nullptr; }
    DWORD last_error() const noexcept { return error_; }

private:
    HANDLE handle_ = nullptr;
    DWORD error_ = ERROR_SUCCESS;
};

}  // namespace tt::display

#endif  // _WIN32
