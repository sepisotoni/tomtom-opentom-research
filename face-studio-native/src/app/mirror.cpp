#include "mirror.h"

#include <algorithm>
#include <array>
#include <chrono>

#include "../display/frame_converter.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace app {

namespace {

constexpr int kPanelW = static_cast<int>(tt::display::kFrameWidth);
constexpr int kPanelH = static_cast<int>(tt::display::kFrameHeight);
constexpr int kMaxWindowDim = 3840;
constexpr int kCaptureIntervalMs = 120;  // the transport itself caps delivery at ~9 fps

struct Enum {
    std::vector<MirrorSource>* out;
    int monitor_index = 0;
};

BOOL CALLBACK monitor_proc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    Enum* e = reinterpret_cast<Enum*>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    MirrorSource s;
    s.monitor = mon;
    ++e->monitor_index;
    s.label = L"Screen " + std::to_wstring(e->monitor_index) + L" (" + std::to_wstring(mi.rcMonitor.right - mi.rcMonitor.left) + L"x" +
              std::to_wstring(mi.rcMonitor.bottom - mi.rcMonitor.top) + L")" + ((mi.dwFlags & MONITORINFOF_PRIMARY) ? L" - primary" : L"");
    e->out->push_back(s);
    return TRUE;
}

BOOL CALLBACK window_proc(HWND hwnd, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<MirrorSource>*>(lp);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    int n = GetWindowTextLengthW(hwnd);
    if (n <= 0 || n > 200) return TRUE;
    RECT r;
    if (!GetWindowRect(hwnd, &r) || r.right - r.left < 64 || r.bottom - r.top < 64) return TRUE;
    std::wstring t(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, &t[0], n + 1);
    t.resize(static_cast<size_t>(n));
    MirrorSource s;
    s.is_window = true;
    s.window = hwnd;
    s.label = L"Window: " + t;
    out->push_back(s);
    return TRUE;
}

// Computes source/destination rectangles for scaling a (w x h) surface onto the panel.
void fit_rects(MirrorFit fit, int w, int h, RECT& src, RECT& dst) {
    src = RECT{0, 0, w, h};
    dst = RECT{0, 0, kPanelW, kPanelH};
    if (fit == MirrorFit::Stretch || w <= 0 || h <= 0) return;
    if (fit == MirrorFit::Letterbox) {
        double scale = std::min(static_cast<double>(kPanelW) / w, static_cast<double>(kPanelH) / h);
        int dw = std::max(1, static_cast<int>(w * scale)), dh = std::max(1, static_cast<int>(h * scale));
        dst = RECT{(kPanelW - dw) / 2, (kPanelH - dh) / 2, (kPanelW - dw) / 2 + dw, (kPanelH - dh) / 2 + dh};
    } else {  // Fill: crop the source to the panel's aspect ratio, centred
        double panel_aspect = static_cast<double>(kPanelW) / kPanelH;
        int cw = w, ch = h;
        if (static_cast<double>(w) / h > panel_aspect) cw = std::max(1, static_cast<int>(h * panel_aspect));
        else ch = std::max(1, static_cast<int>(w / panel_aspect));
        src = RECT{(w - cw) / 2, (h - ch) / 2, (w - cw) / 2 + cw, (h - ch) / 2 + ch};
    }
}

}  // namespace

std::vector<MirrorSource> list_mirror_sources() {
    std::vector<MirrorSource> out;
    Enum e{&out, 0};
    EnumDisplayMonitors(nullptr, nullptr, monitor_proc, reinterpret_cast<LPARAM>(&e));
    EnumWindows(window_proc, reinterpret_cast<LPARAM>(&out));
    return out;
}

Mirror::~Mirror() { stop(); }

void Mirror::set_note(const std::string& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    note_ = s;
}

bool Mirror::start(const MirrorSource& source, MirrorFit fit, std::string& error) {
    if (running_) { error = "Mirroring is already running."; return false; }
    if (source.is_window ? !IsWindow(source.window) : source.monitor == nullptr) {
        error = "That source is no longer available - refresh the list.";
        return false;
    }
    source_ = source;
    fit_ = fit;
    frames_ = 0;
    set_note("");
    // Single-owner rule (windows-idd/README.md): take the lease first so the extended-display driver
    // releases the receiver's one TCP client slot, give it a moment, then connect.
    if (!lease_.acquire()) {
        error = lease_.last_error() == ERROR_ALREADY_EXISTS
                    ? "Another Face Studio window is already mirroring to the TomTom."
                    : "Could not take the display-sharing lease.";
        return false;
    }
    Sleep(300);
    if (!transport_.start()) {
        lease_.release();
        error = "Could not start the frame sender.";
        return false;
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Mirror::stop() {
    if (!running_) return;
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    transport_.stop();
    lease_.release();  // after the transport is closed, so the driver never meets our socket
    running_ = false;
}

std::string Mirror::status_text() const {
    std::string note;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        note = note_;
    }
    if (!running_) return note.empty() ? "Stopped." : note;
    if (!note.empty()) return note;
    using tt::display::TransportState;
    switch (transport_.state()) {
        case TransportState::stopped: return "Starting...";
        case TransportState::waiting_for_frame: return "Capturing - waiting to send the first frame.";
        case TransportState::connecting: return "Connecting to the TomTom display receiver (192.168.101.115:18745)...";
        case TransportState::streaming: return "Streaming to the TomTom. Frames captured: " + std::to_string(frames_.load());
        case TransportState::unavailable: return "TomTom display receiver unavailable - is it running (and the clock renderer stopped)? Retrying.";
    }
    return "";
}

void Mirror::run() {
    HDC screen = GetDC(nullptr);
    HDC panel_dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = kPanelW;
    bi.bmiHeader.biHeight = -kPanelH;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* panel_bits = nullptr;
    HBITMAP panel_bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &panel_bits, nullptr, 0);
    if (!panel_bmp || !panel_bits) {
        set_note("Could not allocate the capture buffer.");
        if (panel_bmp) DeleteObject(panel_bmp);
        DeleteDC(panel_dc);
        ReleaseDC(nullptr, screen);
        running_ = false;
        return;
    }
    HGDIOBJ panel_old = SelectObject(panel_dc, panel_bmp);
    SetStretchBltMode(panel_dc, HALFTONE);
    SetBrushOrgEx(panel_dc, 0, 0, nullptr);

    // Cached full-size surface for window capture.
    HDC win_dc = CreateCompatibleDC(screen);
    HBITMAP win_bmp = nullptr;
    HGDIOBJ win_old = nullptr;
    int win_w = 0, win_h = 0;

    std::array<std::uint8_t, tt::display::kFrameBytes> frame{};
    auto next = std::chrono::steady_clock::now();
    while (!stop_) {
        next += std::chrono::milliseconds(kCaptureIntervalMs);
        bool drew = false;
        if (source_.is_window) {
            if (!IsWindow(source_.window)) {
                set_note("The mirrored window was closed. Choose another source.");
                break;
            }
            RECT wr;
            if (!IsIconic(source_.window) && GetWindowRect(source_.window, &wr)) {
                int w = wr.right - wr.left, h = wr.bottom - wr.top;
                if (w > 0 && h > 0 && w <= kMaxWindowDim && h <= kMaxWindowDim) {
                    if (w != win_w || h != win_h || !win_bmp) {
                        if (win_bmp) { SelectObject(win_dc, win_old); DeleteObject(win_bmp); }
                        BITMAPINFO wb{};
                        wb.bmiHeader.biSize = sizeof wb.bmiHeader;
                        wb.bmiHeader.biWidth = w;
                        wb.bmiHeader.biHeight = -h;
                        wb.bmiHeader.biPlanes = 1;
                        wb.bmiHeader.biBitCount = 32;
                        wb.bmiHeader.biCompression = BI_RGB;
                        void* bits = nullptr;
                        win_bmp = CreateDIBSection(screen, &wb, DIB_RGB_COLORS, &bits, nullptr, 0);
                        win_old = win_bmp ? SelectObject(win_dc, win_bmp) : nullptr;
                        win_w = w;
                        win_h = h;
                    }
                    if (win_bmp && PrintWindow(source_.window, win_dc, PW_RENDERFULLCONTENT)) {
                        RECT src, dst;
                        fit_rects(fit_, w, h, src, dst);
                        PatBlt(panel_dc, 0, 0, kPanelW, kPanelH, BLACKNESS);
                        StretchBlt(panel_dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, win_dc, src.left, src.top,
                                   src.right - src.left, src.bottom - src.top, SRCCOPY);
                        drew = true;
                    }
                }
            }
        } else {
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            if (!GetMonitorInfoW(source_.monitor, &mi)) {
                set_note("The mirrored screen is no longer available (display change). Choose a source again.");
                break;
            }
            int w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
            RECT src, dst;
            fit_rects(fit_, w, h, src, dst);
            PatBlt(panel_dc, 0, 0, kPanelW, kPanelH, BLACKNESS);
            drew = StretchBlt(panel_dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, screen, mi.rcMonitor.left + src.left,
                              mi.rcMonitor.top + src.top, src.right - src.left, src.bottom - src.top, SRCCOPY | CAPTUREBLT) != 0;
        }
        if (drew) {
            GdiFlush();
            if (tt::display::convert_bgra8_to_rgb565(static_cast<const std::uint8_t*>(panel_bits), static_cast<size_t>(kPanelW) * kPanelH * 4,
                                                     tt::display::kFrameWidth, tt::display::kFrameHeight, static_cast<size_t>(kPanelW) * 4,
                                                     frame.data(), frame.size())) {
                transport_.submit_frame(frame.data(), frame.size());
                ++frames_;
            }
        }
        std::this_thread::sleep_until(next);
        if (std::chrono::steady_clock::now() > next + std::chrono::milliseconds(500)) next = std::chrono::steady_clock::now();
    }

    if (win_bmp) { SelectObject(win_dc, win_old); DeleteObject(win_bmp); }
    DeleteDC(win_dc);
    SelectObject(panel_dc, panel_old);
    DeleteObject(panel_bmp);
    DeleteDC(panel_dc);
    ReleaseDC(nullptr, screen);
    transport_.stop();
    lease_.release();
    running_ = false;
}

}  // namespace app
