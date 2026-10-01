#define UNICODE
#define _UNICODE
#include <windows.h>
#include <swdevice.h>

#include <conio.h>
#include <cstdio>

namespace {

struct CreationResult {
    HANDLE event;
    HRESULT status;
};

void WINAPI on_created(HSWDEVICE, HRESULT status, void* context,
                       PCWSTR) noexcept {
    auto* result = static_cast<CreationResult*>(context);
    result->status = status;
    SetEvent(result->event);
}

}  // namespace

int wmain() {
    CreationResult result{CreateEventW(nullptr, TRUE, FALSE, nullptr),
                          E_PENDING};
    if (!result.event) {
        ::fwprintf(stderr, L"CreateEvent failed: %lu\n", GetLastError());
        return 1;
    }

    SW_DEVICE_CREATE_INFO create_info{};
    create_info.cbSize = sizeof(create_info);
    create_info.pszInstanceId = L"TomTomUsbDisplay";
    create_info.pszzHardwareIds = L"Root\\TomTomIndirectDisplay\0";
    create_info.pszDeviceDescription = L"TomTom 320x240 USB Display";
    create_info.CapabilityFlags = SWDeviceCapabilitiesRemovable |
                                 SWDeviceCapabilitiesSilentInstall |
                                 SWDeviceCapabilitiesDriverRequired;

    HSWDEVICE device = nullptr;
    const HRESULT create_status = SwDeviceCreate(
        L"TomTomIndirectDisplay", L"HTREE\\ROOT\\0", &create_info, 0,
        nullptr, on_created, &result, &device);
    if (FAILED(create_status)) {
        ::fwprintf(stderr, L"SwDeviceCreate failed: 0x%08lx\n",
                   static_cast<unsigned long>(create_status));
        CloseHandle(result.event);
        return 1;
    }

    const DWORD wait = WaitForSingleObject(result.event, 15000);
    CloseHandle(result.event);
    if (wait != WAIT_OBJECT_0 || FAILED(result.status)) {
        ::fwprintf(stderr, L"Software device creation failed: 0x%08lx\n",
                   static_cast<unsigned long>(result.status));
        SwDeviceClose(device);
        return 1;
    }

    ::wprintf(L"TomTom virtual display device is active.\n"
              L"Press X to remove it and stop the virtual monitor.\n");
    while (true) {
        const int key = _getch();
        if (key == 'x' || key == 'X') {
            break;
        }
    }

    SwDeviceClose(device);
    ::wprintf(L"TomTom virtual display device removed.\n");
    return 0;
}
