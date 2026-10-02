// TomTomDisplayControl.exe - control surface for the experimental TomTom 320x240 virtual display.
//
//   status                 print "present=<0|1> running=<0|1> frames=<n>"            (no elevation)
//   pause | resume         stop / restart the driver streaming to the TomTom          (no elevation)
//   enable [--interactive] create the virtual display device and HOLD it (elevated)
//   disable                remove the virtual display device                          (elevated)
//   install <path\to.inf>  add the driver package to the driver store                 (elevated)
//   uninstall              remove the device and delete our driver package(s)         (elevated)
//
// No arguments behaves like `enable --interactive` (the original development behaviour).
// Exit codes are tt::idd_ipc::exit_code and are documented in windows-idd/README.md.
//
// The link to the TomTom is plain text, unauthenticated and USB-only. Nothing here adds a network target.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <swdevice.h>

#include <conio.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include "ControlPipeClient.h"
#include "InfScan.h"
#include "../src/display/idd_ipc.h"

namespace {

namespace ec = tt::idd_ipc::exit_code;

constexpr DWORD kCreateTimeoutMs = 15000;
constexpr DWORD kRemoveTimeoutMs = 15000;
constexpr DWORD kHostExitTimeoutMs = 20000;
constexpr DWORD kPnputilTimeoutMs = 180000;

void say_error(const char* format, ...) {
    std::fputs("TomTomDisplayControl: ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

int require_elevation() {
    if (is_elevated()) {
        return ec::ok;
    }
    say_error("this command needs an elevated (Administrator) process.");
    return ec::access_denied;
}

// ---- device node (cfgmgr32; read-only queries work without elevation) -------------------------

struct DeviceState {
    bool found = false;        // a present device node with our hardware ID exists
    bool started = false;      // DN_STARTED
    bool has_problem = false;  // DN_HAS_PROBLEM
    ULONG problem = 0;         // CM_PROB_* when has_problem
    DEVINST instance = 0;
};

bool multi_sz_has(const std::vector<wchar_t>& buffer, const wchar_t* wanted) {
    const wchar_t* p = buffer.data();
    const wchar_t* end = p + buffer.size();
    while (p < end && *p != L'\0') {
        if (_wcsicmp(p, wanted) == 0) {
            return true;
        }
        p += wcslen(p) + 1;
    }
    return false;
}

// Returns false (with *error set) only when the question itself could not be answered.
bool find_device(DeviceState& state, CONFIGRET* error) {
    state = DeviceState{};
    ULONG length = 0;
    const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    CONFIGRET cr = CM_Get_Device_ID_List_SizeW(&length, L"SWD", flags);
    if (cr != CR_SUCCESS) {
        *error = cr;
        return false;
    }
    std::vector<wchar_t> ids(length + 2, L'\0');
    cr = CM_Get_Device_ID_ListW(L"SWD", ids.data(), static_cast<ULONG>(ids.size()), flags);
    if (cr != CR_SUCCESS) {
        *error = cr;
        return false;
    }
    for (const wchar_t* id = ids.data(); *id != L'\0'; id += wcslen(id) + 1) {
        DEVINST instance = 0;
        if (CM_Locate_DevNodeW(&instance, const_cast<DEVINSTID_W>(id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
            continue;
        }
        ULONG type = 0;
        ULONG size = 0;
        if (CM_Get_DevNode_Registry_PropertyW(instance, CM_DRP_HARDWAREID, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
            size == 0) {
            continue;
        }
        std::vector<wchar_t> hardware_ids(size / sizeof(wchar_t) + 2, L'\0');
        ULONG bytes = static_cast<ULONG>((hardware_ids.size() - 1) * sizeof(wchar_t));
        if (CM_Get_DevNode_Registry_PropertyW(instance, CM_DRP_HARDWAREID, &type, hardware_ids.data(), &bytes, 0) !=
            CR_SUCCESS) {
            continue;
        }
        if (!multi_sz_has(hardware_ids, tt::idd_ipc::kHardwareId)) {
            continue;
        }
        state.found = true;
        state.instance = instance;
        ULONG status = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, instance, 0) == CR_SUCCESS) {
            state.started = (status & DN_STARTED) != 0;
            state.has_problem = (status & DN_HAS_PROBLEM) != 0;
            state.problem = state.has_problem ? problem : 0;
        }
        return true;
    }
    return true;
}

int device_query_failed(CONFIGRET cr) {
    if (cr == CR_ACCESS_DENIED) {
        say_error("access denied while querying the device tree (CONFIGRET 0x%lx).", static_cast<unsigned long>(cr));
        return ec::access_denied;
    }
    say_error("device query failed (CONFIGRET 0x%lx).", static_cast<unsigned long>(cr));
    return ec::internal_error;
}

bool wait_until_device_gone(DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        DeviceState state;
        CONFIGRET cr = CR_SUCCESS;
        if (find_device(state, &cr) && !state.found) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(250);
    }
}

// ---- status / pause / resume -----------------------------------------------------------------

int cmd_status() {
    DeviceState device;
    CONFIGRET cr = CR_SUCCESS;
    if (!find_device(device, &cr)) {
        return device_query_failed(cr);
    }
    tt::idd_ipc::Status status;
    status.present = device.found && device.started && !device.has_problem;
    if (status.present) {
        // An unreachable driver (starting up, or host restarting) is not an error for `status`.
        std::string reply;
        DWORD error = 0;
        bool running = false;
        std::uint64_t frames = 0;
        if (tt::idd::control_pipe_request("STATUS", reply, error, 700) &&
            tt::idd_ipc::parse_status_reply(reply, running, frames)) {
            status.running = running;
            status.frames = frames;
        }
    }
    std::puts(tt::idd_ipc::format_status_line(status).c_str());
    std::fflush(stdout);
    return ec::ok;
}

int cmd_pause_resume(bool pause) {
    DeviceState device;
    CONFIGRET cr = CR_SUCCESS;
    if (!find_device(device, &cr)) {
        return device_query_failed(cr);
    }
    if (!device.found) {
        say_error("the TomTom virtual display device is not present.");
        return ec::not_installed;
    }
    if (!device.started || device.has_problem) {
        say_error("the TomTom virtual display device exists but is not started (problem code %lu).", device.problem);
        return ec::device_faulted;
    }
    std::string reply;
    DWORD error = 0;
    if (!tt::idd::control_pipe_request(pause ? "PAUSE" : "RESUME", reply, error, 1500)) {
        if (error == ERROR_ACCESS_DENIED) {
            say_error("access denied opening the driver control pipe.");
            return ec::access_denied;
        }
        say_error("the driver did not answer on its control pipe (Win32 error %lu).", error);
        return ec::driver_unresponsive;
    }
    if (!tt::idd_ipc::is_ok_reply(reply)) {
        say_error("the driver rejected the request: %s", reply.c_str());
        return ec::internal_error;
    }
    return ec::ok;
}

// ---- enable (hold the software device) / disable ---------------------------------------------

struct CreationResult {
    HANDLE event;
    HRESULT status;
};

void WINAPI on_created(HSWDEVICE, HRESULT status, void* context, PCWSTR) noexcept {
    auto* result = static_cast<CreationResult*>(context);
    result->status = status;
    SetEvent(result->event);
}

HANDLE g_quit_event = nullptr;  // set by Ctrl+C / console close / logoff / shutdown / the X key
HANDLE g_done_event = nullptr;  // set by main() once the device is removed

BOOL WINAPI on_console_event(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            if (g_quit_event) SetEvent(g_quit_event);
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // The process dies when this handler returns: give main() time to remove the device.
            if (g_quit_event) SetEvent(g_quit_event);
            if (g_done_event) WaitForSingleObject(g_done_event, 8000);
            return TRUE;
        default:
            return FALSE;
    }
}

DWORD WINAPI key_thread(void*) {
    for (;;) {
        const int key = _getch();
        if (key == 'x' || key == 'X') {
            if (g_quit_event) SetEvent(g_quit_event);
            return 0;
        }
    }
}

int classify_creation_failure(HRESULT hr) {
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
        say_error("access denied creating the software device (needs elevation).");
        return ec::access_denied;
    }
    DeviceState device;
    CONFIGRET cr = CR_SUCCESS;
    if (find_device(device, &cr) && device.found && (device.has_problem || !device.started)) {
        if (device.problem == CM_PROB_FAILED_INSTALL || device.problem == CM_PROB_NOT_CONFIGURED) {
            say_error("no driver is installed for %ls (problem code %lu). Run `install <path\\TomTomIdd.inf>` first.",
                      tt::idd_ipc::kHardwareId, device.problem);
            return ec::not_installed;
        }
        say_error("the device node exists but did not start (problem code %lu, HRESULT 0x%08lx).", device.problem,
                  static_cast<unsigned long>(hr));
        return ec::device_faulted;
    }
    say_error("software device creation failed or timed out (HRESULT 0x%08lx).", static_cast<unsigned long>(hr));
    return ec::internal_error;
}

int cmd_enable(bool interactive) {
    if (const int rc = require_elevation(); rc != ec::ok) {
        return rc;
    }

    // One host at a time. The mutex is held for as long as the device exists; `disable` waits on it.
    HANDLE host_mutex = CreateMutexW(nullptr, TRUE, tt::idd_ipc::kDeviceHostMutexName);
    if (!host_mutex) {
        const DWORD error = GetLastError();
        say_error("could not create the host mutex (Win32 error %lu).", error);
        return error == ERROR_ACCESS_DENIED ? ec::access_denied : ec::internal_error;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(host_mutex);
        say_error("the virtual display is already enabled (another `enable` is running).");
        return ec::already_running;
    }
    // Administrators/SYSTEM only (default DACL of an elevated token). A pre-existing event of that name
    // is not ours to trust.
    HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, tt::idd_ipc::kDeviceHostStopEventName);
    if (!stop_event || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (stop_event) CloseHandle(stop_event);
        ReleaseMutex(host_mutex);
        CloseHandle(host_mutex);
        say_error("the host stop event already exists; is another `enable` running?");
        return ec::already_running;
    }
    g_quit_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_done_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto* result = new CreationResult{CreateEventW(nullptr, TRUE, FALSE, nullptr), E_PENDING};
    int rc = ec::ok;
    HSWDEVICE device = nullptr;

    auto cleanup = [&](bool leak_result) {
        if (g_done_event) SetEvent(g_done_event);
        if (!leak_result) {
            if (result->event) CloseHandle(result->event);
            delete result;
        }  // else: a late creation callback may still write to `result`; the process is about to exit.
        // g_done_event is deliberately left open: the console-handler thread may still be waiting on it
        // and the process exits right after this.
        for (HANDLE* h : {&g_quit_event, &stop_event}) {
            if (*h) { CloseHandle(*h); *h = nullptr; }
        }
        ReleaseMutex(host_mutex);
        CloseHandle(host_mutex);
    };

    if (!g_quit_event || !g_done_event || !result->event) {
        say_error("could not create synchronisation objects (Win32 error %lu).", GetLastError());
        cleanup(false);
        return ec::internal_error;
    }
    SetConsoleCtrlHandler(on_console_event, TRUE);

    SW_DEVICE_CREATE_INFO create_info{};
    create_info.cbSize = sizeof(create_info);
    create_info.pszInstanceId = L"TomTomUsbDisplay";
    create_info.pszzHardwareIds = L"Root\\TomTomIndirectDisplay\0";
    create_info.pszDeviceDescription = L"TomTom 320x240 USB Display";
    create_info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                                  SWDeviceCapabilitiesDriverRequired;

    const HRESULT create_status = SwDeviceCreate(L"TomTomIndirectDisplay", L"HTREE\\ROOT\\0", &create_info, 0, nullptr,
                                                 on_created, result, &device);
    if (FAILED(create_status)) {
        rc = classify_creation_failure(create_status);
        cleanup(false);
        return rc;
    }

    const DWORD wait = WaitForSingleObject(result->event, kCreateTimeoutMs);
    if (wait != WAIT_OBJECT_0 || FAILED(result->status)) {
        const HRESULT hr = wait == WAIT_OBJECT_0 ? result->status : HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        rc = classify_creation_failure(hr);  // inspect the node BEFORE removing it
        SwDeviceClose(device);
        wait_until_device_gone(kRemoveTimeoutMs);
        cleanup(wait != WAIT_OBJECT_0);
        return rc;
    }

    std::puts("TomTom virtual display device is enabled.");
    if (interactive) {
        std::puts("Press X (or Ctrl+C) to remove it and stop the virtual monitor.");
        HANDLE thread = CreateThread(nullptr, 0, key_thread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);  // never joined: _getch() cannot be interrupted
    }
    std::fflush(stdout);

    HANDLE waits[2] = {stop_event, g_quit_event};
    WaitForMultipleObjects(2, waits, FALSE, INFINITE);

    SwDeviceClose(device);
    if (!wait_until_device_gone(kRemoveTimeoutMs)) {
        say_error("the device node is still present %lu s after closing the handle.", kRemoveTimeoutMs / 1000);
        rc = ec::internal_error;
    } else {
        std::puts("TomTom virtual display device removed.");
    }
    std::fflush(stdout);
    SetConsoleCtrlHandler(on_console_event, FALSE);
    cleanup(false);
    return rc;
}

int cmd_disable() {
    HANDLE stop_event = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, tt::idd_ipc::kDeviceHostStopEventName);
    if (stop_event) {
        SetEvent(stop_event);
        CloseHandle(stop_event);
        // The host closes the device and exits; its mutex becomes available (abandoned or released).
        HANDLE host = OpenMutexW(SYNCHRONIZE, FALSE, tt::idd_ipc::kDeviceHostMutexName);
        if (host) {
            const DWORD wait = WaitForSingleObject(host, kHostExitTimeoutMs);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
                ReleaseMutex(host);
            } else {
                CloseHandle(host);
                say_error("the enable host did not exit within %lu s.", kHostExitTimeoutMs / 1000);
                return ec::internal_error;
            }
            CloseHandle(host);
        }
        if (!wait_until_device_gone(kRemoveTimeoutMs)) {
            say_error("the device node is still present after the host exited.");
            return ec::internal_error;
        }
        return ec::ok;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
        say_error("access denied: the enable host runs elevated; run `disable` from an elevated process.");
        return ec::access_denied;
    }
    if (error != ERROR_FILE_NOT_FOUND) {
        say_error("could not open the host stop event (Win32 error %lu).", error);
        return ec::internal_error;
    }

    // No host is running. Normally that means no device either (a crashed host takes the device with it).
    DeviceState device;
    CONFIGRET cr = CR_SUCCESS;
    if (!find_device(device, &cr)) {
        return device_query_failed(cr);
    }
    if (!device.found) {
        return ec::ok;  // already disabled
    }
    // A device node with our hardware ID that no host owns (created by another tool): remove it.
    if (const int rc = require_elevation(); rc != ec::ok) {
        return rc;
    }
    cr = CM_Query_And_Remove_SubTreeW(device.instance, nullptr, nullptr, 0, CM_REMOVE_NO_RESTART);
    if (cr == CR_ACCESS_DENIED) {
        return ec::access_denied;
    }
    if (cr != CR_SUCCESS) {
        say_error("could not remove the device node (CONFIGRET 0x%lx).", static_cast<unsigned long>(cr));
        return ec::internal_error;
    }
    return wait_until_device_gone(kRemoveTimeoutMs) ? ec::ok : ec::internal_error;
}

// ---- install / uninstall (pnputil) ------------------------------------------------------------

// Runs %SystemRoot%\System32\pnputil.exe with fixed arguments (no shell, no PATH lookup).
// Its console output is inherited so the user sees pnputil's own messages.
bool run_pnputil(const std::wstring& arguments, DWORD* exit_code) {
    wchar_t system_dir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    const std::wstring exe = std::wstring(system_dir) + L"\\pnputil.exe";
    std::wstring command_line = L"\"" + exe + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                        &process)) {
        say_error("could not start pnputil.exe (Win32 error %lu).", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, kPnputilTimeoutMs);
    bool ok = false;
    if (wait == WAIT_OBJECT_0) {
        ok = GetExitCodeProcess(process.hProcess, exit_code) != FALSE;
    } else {
        say_error("pnputil.exe did not finish within %lu s; it was left running.", kPnputilTimeoutMs / 1000);
    }
    CloseHandle(process.hProcess);
    return ok;
}

int map_pnputil_exit(DWORD code, const char* what) {
    if (code == 0) {
        return ec::ok;
    }
    if (code == ERROR_SUCCESS_REBOOT_REQUIRED) {
        say_error("%s succeeded but Windows must be restarted to finish.", what);
        return ec::reboot_required;
    }
    if (code == ERROR_ACCESS_DENIED) {
        return ec::access_denied;
    }
    say_error("pnputil %s failed (exit code %lu / 0x%lx). If its message mentions a signature, catalog or "
              "publisher, the package needs test-signing mode and the test certificate installed - see "
              "windows-idd/README.md.", what, code, code);
    return ec::internal_error;
}

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int cmd_install(const wchar_t* inf_argument) {
    if (const int rc = require_elevation(); rc != ec::ok) {
        return rc;
    }
    wchar_t full[MAX_PATH] = {};
    const DWORD n = GetFullPathNameW(inf_argument, MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) {
        say_error("bad INF path.");
        return ec::usage;
    }
    const std::wstring inf = full;
    const std::size_t slash = inf.find_last_of(L'\\');
    const std::wstring folder = slash == std::wstring::npos ? L"" : inf.substr(0, slash + 1);
    if (!file_exists(inf)) {
        say_error("INF not found: %ls", inf.c_str());
        return ec::usage;
    }
    // The package must be a built driver folder: the INF names these two files.
    for (const wchar_t* sibling : {L"TomTomIdd.dll", L"TomTomIdd.cat"}) {
        if (!file_exists(folder + sibling)) {
            say_error("%ls is missing next to the INF; point at the built package folder (see README).", sibling);
            return ec::usage;
        }
    }
    DWORD code = 0;
    if (!run_pnputil(L"/add-driver \"" + inf + L"\" /install", &code)) {
        return ec::internal_error;
    }
    const int rc = map_pnputil_exit(code, "/add-driver");
    if (rc == ec::ok) {
        std::puts("Driver package added. Next: TomTomDisplayControl.exe enable");
    }
    return rc;
}

std::vector<std::wstring> find_published_infs() {
    std::vector<std::wstring> found;
    wchar_t windows_dir[MAX_PATH] = {};
    const UINT n = GetWindowsDirectoryW(windows_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return found;
    }
    const std::wstring directory = std::wstring(windows_dir) + L"\\INF\\";
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((directory + L"oem*.inf").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        return found;
    }
    do {
        if (data.nFileSizeHigh != 0 || data.nFileSizeLow == 0 || data.nFileSizeLow > (4U << 20)) {
            continue;
        }
        HANDLE file = CreateFileW((directory + data.cFileName).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            continue;
        }
        std::string bytes(data.nFileSizeLow, '\0');
        DWORD read = 0;
        const bool ok = ReadFile(file, bytes.data(), data.nFileSizeLow, &read, nullptr) != FALSE;
        CloseHandle(file);
        if (ok) {
            bytes.resize(read);
            if (tt::idd::inf_belongs_to_tomtom_idd(bytes)) {
                found.emplace_back(data.cFileName);
            }
        }
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return found;
}

int cmd_uninstall() {
    if (const int rc = require_elevation(); rc != ec::ok) {
        return rc;
    }
    if (const int rc = cmd_disable(); rc != ec::ok) {
        return rc;  // never delete a package whose device is still live
    }
    const std::vector<std::wstring> published = find_published_infs();
    if (published.empty()) {
        say_error("no TomTom display driver package is installed.");
        return ec::not_installed;
    }
    int result = ec::ok;
    for (const std::wstring& name : published) {
        DWORD code = 0;
        if (!run_pnputil(L"/delete-driver " + name, &code)) {
            return ec::internal_error;
        }
        const int rc = map_pnputil_exit(code, "/delete-driver");
        if (rc == ec::reboot_required) {
            result = rc;
        } else if (rc != ec::ok) {
            return rc;
        }
    }
    std::puts("Driver package removed. Test-signing mode and the test certificate (if you set them up) are NOT "
              "changed by this tool; see windows-idd/README.md to undo them by hand.");
    return result;
}

// ---- command line ------------------------------------------------------------------------------

int usage(FILE* out, int code) {
    std::fputs(
        "usage: TomTomDisplayControl <command>\n"
        "  status                  print: present=<0|1> running=<0|1> frames=<n>\n"
        "  pause | resume          stop/restart streaming to the TomTom (no elevation)\n"
        "  enable [--interactive]  create the virtual display and hold it (elevated)\n"
        "  disable                 remove the virtual display (elevated)\n"
        "  install <path\\to.inf>   add the driver package to the driver store (elevated)\n"
        "  uninstall               remove the device and delete the driver package (elevated)\n"
        "exit codes: 0 ok, 1 internal error, 2 not installed, 3 access denied/needs elevation,\n"
        "  4 device faulted, 5 usage, 6 driver not responding, 7 reboot required, 8 already running\n",
        out);
    return code;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc <= 1) {
        return cmd_enable(true);  // original development behaviour: create, wait for X, remove
    }
    const wchar_t* command = argv[1];
    auto is = [command](const wchar_t* name) { return _wcsicmp(command, name) == 0; };

    if (is(L"status") && argc == 2) return cmd_status();
    if (is(L"pause") && argc == 2) return cmd_pause_resume(true);
    if (is(L"resume") && argc == 2) return cmd_pause_resume(false);
    if (is(L"disable") && argc == 2) return cmd_disable();
    if (is(L"uninstall") && argc == 2) return cmd_uninstall();
    if (is(L"install") && argc == 3) return cmd_install(argv[2]);
    if (is(L"enable")) {
        if (argc == 2) return cmd_enable(false);
        if (argc == 3 && _wcsicmp(argv[2], L"--interactive") == 0) return cmd_enable(true);
    }
    if (is(L"help") || is(L"--help") || is(L"/?")) return usage(stdout, ec::ok);
    return usage(stderr, ec::usage);
}
