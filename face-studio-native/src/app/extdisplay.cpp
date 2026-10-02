#include "extdisplay.h"

#include <shellapi.h>

#include <chrono>
#include <string_view>
#include <thread>

#include "../display/idd_ipc.h"

namespace app {

namespace {

constexpr DWORD kStatusTimeoutMs = 3000;
constexpr DWORD kCommandTimeoutMs = 5000;
constexpr size_t kMaxCapture = 4096;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\r' || s[a] == '\n')) ++a;
    return s.substr(a);
}

void read_pipe(HANDLE h, std::string& out) {
    char buf[512];
    DWORD got = 0;
    while (out.size() < kMaxCapture && ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) out.append(buf, got);
}

}  // namespace

std::string describe_ext_exit_code(int code) {
    namespace ec = tt::idd_ipc::exit_code;
    switch (code) {
        case ec::ok: return "OK";
        case ec::internal_error: return "The display tool hit an unexpected error.";
        case ec::not_installed: return "The extended-display driver is not installed (see windows-idd/README.md).";
        case ec::access_denied: return "Administrator permission is required.";
        case ec::device_faulted: return "The virtual display exists but did not start. Remove it and add it again.";
        case ec::usage: return "The display tool did not understand the request.";
        case ec::driver_unresponsive: return "The driver is starting or restarting - try again in a few seconds.";
        case ec::reboot_required: return "Done, but Windows needs a restart.";
        case ec::already_running: return "The extended display is already enabled.";
        default: return "The display tool failed (exit code " + std::to_string(code) + ").";
    }
}

ExtDisplay::ExtDisplay() {
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof path / sizeof path[0]));
    if (n > 0 && n < sizeof path / sizeof path[0]) {
        std::wstring dir(path, n);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash + 1);
        tool_path_ = dir + L"TomTomDisplayControl.exe";
        found_ = GetFileAttributesW(tool_path_.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    snap_.tool_found = found_;
}

ExtDisplay::~ExtDisplay() {
    // Worker threads are detached and only touch members guarded by the mutex / atomics; give an
    // in-flight one a moment to finish so it does not outlive us.
    for (int i = 0; i < 40 && busy_; ++i) Sleep(100);
    release_handles();
}

void ExtDisplay::release_handles() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enable_proc_) { CloseHandle(enable_proc_); enable_proc_ = nullptr; }
    if (disable_proc_) { CloseHandle(disable_proc_); disable_proc_ = nullptr; }
}

bool ExtDisplay::run_hidden(const std::wstring& args, int& exit_code, std::string& out, std::string& err) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        err = "Could not create a pipe for the display tool.";
        return false;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + tool_path_ + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        err = "Could not start TomTomDisplayControl.exe.";
        return false;
    }
    std::thread t_out([&] { read_pipe(out_r, out); });
    std::thread t_err([&] { read_pipe(err_r, err); });
    DWORD timeout = args == L"status" ? kStatusTimeoutMs : kCommandTimeoutMs;
    bool finished = WaitForSingleObject(pi.hProcess, timeout) == WAIT_OBJECT_0;
    if (!finished) TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    t_out.join();
    t_err.join();
    CloseHandle(out_r);
    CloseHandle(err_r);
    exit_code = static_cast<int>(code);
    if (!finished) {
        err = "The display tool did not answer in time.";
        return false;
    }
    return true;
}

void ExtDisplay::request_status() {
    if (!found_) return;
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return;
    std::thread([this] { worker(L"status", true); }).detach();
}

bool ExtDisplay::acquire_busy(int wait_ms) {
    for (int waited = 0;; waited += 50) {
        bool expected = false;
        if (busy_.compare_exchange_strong(expected, true)) return true;
        if (waited >= wait_ms) return false;
        Sleep(50);
    }
}

void ExtDisplay::request_pause(bool pause) {
    if (!found_) return;
    std::wstring cmd = pause ? L"pause" : L"resume";
    // An explicit user action waits (briefly) for a status poll in flight instead of being dropped.
    std::thread([this, cmd] {
        if (!acquire_busy(4000)) {
            std::lock_guard<std::mutex> lock(mutex_);
            snap_.action_exit = -1;
            snap_.action_error = "The display tool is busy - try again.";
            ++snap_.action_seq;
            return;
        }
        worker(cmd, false);
    }).detach();
}

void ExtDisplay::worker(std::wstring command, bool is_status) {
    int code = 1;
    std::string out, err;
    bool ran = run_hidden(command, code, out, err);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snap_.last_command = narrow(command);
        if (!ran) {
            snap_.last_exit = -1;
            snap_.last_error = err;
            if (is_status) snap_.status_valid = false;
            else { snap_.action_exit = -1; snap_.action_error = err; ++snap_.action_seq; }
        } else if (is_status) {
            tt::idd_ipc::Status st;
            std::string line = trim(out);
            if (code == tt::idd_ipc::exit_code::ok && tt::idd_ipc::parse_status_line(line, st)) {
                snap_.status_valid = true;
                snap_.present = st.present;
                snap_.running = st.running;
                snap_.frames = st.frames;
                snap_.last_exit = 0;
                snap_.last_error.clear();
            } else {
                snap_.status_valid = false;
                snap_.last_exit = code;
                snap_.last_error = code != 0 ? describe_ext_exit_code(code) : "The display tool returned an unexpected status line.";
            }
        } else {
            snap_.last_exit = code;
            snap_.last_error = code == 0 ? "" : describe_ext_exit_code(code);
            snap_.action_exit = code;
            snap_.action_error = snap_.last_error;
            ++snap_.action_seq;
        }
    }
    busy_ = false;
}

bool ExtDisplay::launch_elevated(const wchar_t* args, HANDLE& process, std::string& error) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = tool_path_.c_str();
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DWORD e = GetLastError();
        error = e == ERROR_CANCELLED ? "The administrator prompt was declined." : "Could not start the display tool with administrator rights.";
        return false;
    }
    process = sei.hProcess;
    return true;
}

bool ExtDisplay::start_enable(std::string& error) {
    if (!found_) { error = "TomTomDisplayControl.exe was not found next to this program."; return false; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enable_proc_ && WaitForSingleObject(enable_proc_, 0) == WAIT_TIMEOUT) {
            error = "The extended display is already being added by this window.";
            return false;
        }
        if (enable_proc_) { CloseHandle(enable_proc_); enable_proc_ = nullptr; }
    }
    HANDLE h = nullptr;
    if (!launch_elevated(L"enable", h, error)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    enable_proc_ = h;
    snap_.last_command = "enable";
    snap_.last_exit = -1;
    snap_.last_error.clear();
    return true;
}

bool ExtDisplay::start_disable(std::string& error) {
    if (!found_) { error = "TomTomDisplayControl.exe was not found next to this program."; return false; }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (disable_proc_ && WaitForSingleObject(disable_proc_, 0) == WAIT_TIMEOUT) {
            error = "Removal is already in progress.";
            return false;
        }
        if (disable_proc_) { CloseHandle(disable_proc_); disable_proc_ = nullptr; }
    }
    HANDLE h = nullptr;
    if (!launch_elevated(L"disable", h, error)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    disable_proc_ = h;
    snap_.last_command = "disable";
    snap_.last_exit = -1;
    snap_.last_error.clear();
    return true;
}

ExtSnapshot ExtDisplay::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enable_proc_) {
        if (WaitForSingleObject(enable_proc_, 0) == WAIT_TIMEOUT) {
            snap_.enable_host_alive = true;
        } else {
            DWORD code = 0;
            GetExitCodeProcess(enable_proc_, &code);
            CloseHandle(enable_proc_);
            enable_proc_ = nullptr;
            snap_.enable_host_alive = false;
            // The host exits 0 after `disable`; any other code is the reason enable stopped early.
            if (snap_.last_command == "enable" && code != 0) {
                snap_.last_exit = static_cast<int>(code);
                snap_.last_error = describe_ext_exit_code(static_cast<int>(code));
            }
        }
    }
    if (disable_proc_) {
        if (WaitForSingleObject(disable_proc_, 0) == WAIT_TIMEOUT) {
            snap_.disable_pending = true;
        } else {
            DWORD code = 0;
            GetExitCodeProcess(disable_proc_, &code);
            CloseHandle(disable_proc_);
            disable_proc_ = nullptr;
            snap_.disable_pending = false;
            snap_.last_command = "disable";
            snap_.last_exit = static_cast<int>(code);
            snap_.last_error = code == 0 ? "" : describe_ext_exit_code(static_cast<int>(code));
        }
    }
    ExtSnapshot copy = snap_;
    copy.busy = busy_;
    copy.tool_found = found_;
    return copy;
}

}  // namespace app
