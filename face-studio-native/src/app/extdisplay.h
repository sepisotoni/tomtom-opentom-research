// Extended-display (Windows virtual monitor) control: a thin, non-blocking wrapper around
// TomTomDisplayControl.exe, following windows-idd/README.md ("Control tool" and "status contract").
//
//   status              no elevation   parsed with tt::idd_ipc::parse_status_line()
//   pause / resume      no elevation   stream to the TomTom off/on without removing the monitor
//   enable              elevated       long-running: the process OWNS the virtual device until it exits
//   disable             elevated       asks the enable host to remove the device
//
// Nothing here blocks the UI thread: commands run on short-lived worker threads and the UI
// reads snapshot() from its timer. The tool is expected next to the Studio's .exe.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

namespace app {

struct ExtSnapshot {
    bool tool_found = false;
    bool status_valid = false;  // a status line was parsed on the last poll
    bool present = false;
    bool running = false;
    unsigned long long frames = 0;
    int last_exit = -1;         // exit code of the last command that finished (-1 = none yet)
    std::string last_command;   // "pause", "resume", "enable", "disable", "status"
    std::string last_error;     // stderr text or a description of the failure
    unsigned action_seq = 0;    // increments whenever a pause/resume command finishes
    int action_exit = -1;       // its exit code (-1 = the tool could not be run / timed out)
    std::string action_error;   // description when action_exit != 0
    bool busy = false;          // a non-elevated command or status poll is in flight
    bool enable_host_alive = false;  // the elevated `enable` process we started is still running
    bool disable_pending = false;    // an elevated `disable` we started has not finished yet
};

// Human sentence for tt::idd_ipc::exit_code values.
std::string describe_ext_exit_code(int code);

class ExtDisplay {
public:
    ExtDisplay();
    ~ExtDisplay();
    ExtDisplay(const ExtDisplay&) = delete;
    ExtDisplay& operator=(const ExtDisplay&) = delete;

    bool tool_found() const { return found_; }
    // Starts one status poll on a worker thread unless one is already running.
    void request_status();
    // pause / resume without elevation, on a worker thread.
    void request_pause(bool pause);
    // Elevated: shows the Windows UAC prompt. Returns false (with `error`) if it could not be launched
    // or the user declined.
    bool start_enable(std::string& error);
    bool start_disable(std::string& error);
    // Cheap, call from the UI timer: checks the elevated child handles and returns the cached state.
    ExtSnapshot snapshot();
    // Does not remove the device: the `enable` host keeps running if the Studio exits.
    void release_handles();

private:
    void worker(std::wstring command, bool is_status);
    bool acquire_busy(int wait_ms);
    bool run_hidden(const std::wstring& args, int& exit_code, std::string& out, std::string& err);
    bool launch_elevated(const wchar_t* args, HANDLE& process, std::string& error);

    std::wstring tool_path_;
    bool found_ = false;
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    ExtSnapshot snap_;
    HANDLE enable_proc_ = nullptr;
    HANDLE disable_proc_ = nullptr;
};

}  // namespace app
