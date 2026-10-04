// TomTom Face Studio - native Win32 application.
//
// Pure Win32 + GDI + WIC (Windows Imaging Component, used only to decode
// PNG/JPEG/BMP artwork). All format, validation, rendering and device logic
// lives in the platform-neutral core library (src/core).
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
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "canvas.h"
#include "device_client.h"
#include "fileio.h"
#include "gallery.h"
#include "model.h"
#include "package.h"
#include "render.h"
#include "lrc.h"
#include "notify.h"
#include "webhook.h"
#include "lrclib.h"
#include "mirror.h"
#include "extdisplay.h"
#include "spotify.h"
#include <memory>
#include <mutex>

using namespace tt;

namespace {

// ------------------------------------------------------------------ helpers
std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
std::string U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
std::string get_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(static_cast<size_t>(n));
    return U(w);
}
void set_text(HWND h, const std::string& s) { SetWindowTextW(h, W(s).c_str()); }
std::string lines_to_crlf(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\r\n"; else out.push_back(c);
    }
    return out;
}
COLORREF to_colorref(Rgb c) { return RGB(c.r, c.g, c.b); }

// ------------------------------------------------------------------ control IDs
enum Ids {
    ID_TAB = 100,
    ID_TOOL_PENCIL = 110, ID_TOOL_ERASER, ID_TOOL_BUCKET, ID_TOOL_PICKER,
    ID_BRUSH = 120, ID_BRUSH_SPIN, ID_COLOR_BTN, ID_UNDO = 130, ID_REDO, ID_CLEAR, ID_IMPORT, ID_FIT, ID_ZOOM, ID_GRID,
    ID_SWATCH0 = 140,  // 8 swatches: 140..147
    ID_EL_LIST = 200, ID_EL_ADD_TIME, ID_EL_ADD_DATE, ID_EL_ADD_TEXT, ID_EL_ADD_ICON, ID_EL_DELETE,
    ID_EL_ID, ID_EL_X, ID_EL_Y, ID_EL_W, ID_EL_H, ID_EL_FONT, ID_EL_COLOR, ID_EL_RULE, ID_EL_TEXT, ID_EL_ICON,
    ID_EL_X_SPIN, ID_EL_Y_SPIN, ID_EL_W_SPIN, ID_EL_H_SPIN, ID_EL_FONT_SPIN,
    ID_SIM_BAT = 300, ID_SIM_BAT_LBL, ID_SIM_GPS, ID_SIM_WEATHER, ID_SIM_CHARGING, ID_SIM_LIVE, ID_SIM_HOUR, ID_SIM_MIN,
    ID_SIM_HOUR_SPIN, ID_SIM_MIN_SPIN,
    ID_DATA0 = 350,  // 8 checkboxes
    ID_META_NAME = 400, ID_META_AUTHOR, ID_META_VER, ID_GAL_NAME, ID_FMT0, ID_FMT1, ID_FMT_DEFAULT, ID_VALIDATE,
    ID_EXPORT, ID_GALLERY, ID_INSPECT_FACE, ID_INSPECT_GAL, ID_LOG,
    ID_DEV_HOST = 500, ID_DEV_PORT, ID_DEV_PORT_SPIN, ID_DEV_FACE, ID_DEV_APPLY, ID_DEV_READ, ID_DEV_PING, ID_DEV_STATUS, ID_DEV_WARN,
    IDM_OPEN = 600, IDM_SAVE, IDM_SAVEAS, IDM_EXIT, IDM_UNDO, IDM_REDO, IDM_ABOUT, IDM_DESIGNER,
    ID_STATUSBAR = 700,
    ID_MIR_SRC = 800, ID_MIR_REFRESH, ID_MIR_FIT, ID_MIR_START, ID_MIR_STOP, ID_MIR_STATUS, ID_MIR_WARN,
    ID_NF_TEXT = 820, ID_NF_TTL, ID_NF_TTL_SPIN, ID_NF_SEND, ID_NF_STATUS,
    ID_WH_ENABLE = 830, ID_WH_PORT, ID_WH_PORT_SPIN, ID_WH_LAN, ID_WH_TOKEN, ID_WH_REGEN, ID_WH_EXAMPLE, ID_WH_STATUS,
    ID_EXT_STATUS = 870, ID_EXT_ADD, ID_EXT_REMOVE, ID_EXT_PAUSE, ID_EXT_RESUME, ID_EXT_NOTE,
    ID_SP_NOW = 850, ID_SP_ANNOUNCE, ID_LY_ENABLE, ID_LY_SLOWER, ID_LY_FASTER, ID_LY_RESTART, ID_LY_OFFSET, ID_LY_STATUS,
};
constexpr UINT WM_DEVICE_DONE = WM_APP + 1;
constexpr UINT WM_LYRICS_DONE = WM_APP + 2;
const wchar_t* const kSwatches[8] = {L"#84EBFF", L"#00F5FF", L"#E8DAF2", L"#FF7341", L"#60A5FA", L"#000000", L"#FFFFFF", L"#FF4444"};

enum class Tool { Pencil, Eraser, Bucket, Picker };
enum Page { PageElements, PageSim, PageData, PageExport, PageDevice, PageDisplay, PageLive, PageCount };

struct DeviceReply {
    int kind;  // 0 ping, 1 status, 2 set
    DeviceResult result;
};

struct AppState {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr, canvas_wnd = nullptr, tab = nullptr, status = nullptr;
    HWND pages[PageCount] = {};
    HFONT font = nullptr;
    int dpi = 96;

    Json project;
    CanvasModel cvs{"#000000"};
    std::wstring project_path;
    bool dirty = false;

    Tool tool = Tool::Pencil;
    int brush = 1;
    std::string color = "#84EBFF";
    int zoom = 2;
    bool grid = true;
    bool drawing = false;
    int last_x = 0, last_y = 0;
    int scroll_x = 0, scroll_y = 0;

    int sel = -1;
    bool updating = false;  // suppress change notifications while populating controls

    int sim_battery = 15;
    bool sim_gps = true;
    bool sim_weather_off = true;
    bool sim_charging = false;
    bool live_clock = true;
    SimTime sim_time;
    int tick = 0;

    bool designer = false;   // false: control-center layout (default); true: face designer with canvas
    std::vector<int> tab_pages;  // page shown by each visible tab
    bool dev_busy = false;
    int dev_state = 0;  // 0 neutral, 1 ok, 2 error
    COLORREF custom_colors[16] = {};
} S;

int D(int v) { return MulDiv(v, S.dpi, 96); }

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, D(x), D(y), D(w), D(h), parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), S.inst, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(S.font), TRUE);
    return c;
}
HWND mk_label(HWND p, const wchar_t* t, int x, int y, int w, int h = 18, int id = -1, DWORD extra = 0) {
    return mk(p, L"STATIC", t, SS_LEFT | extra, x, y, w, h, id);
}
HWND mk_button(HWND p, const wchar_t* t, int x, int y, int w, int h, int id, DWORD extra = 0) {
    return mk(p, L"BUTTON", t, BS_PUSHBUTTON | WS_TABSTOP | extra, x, y, w, h, id);
}
HWND mk_check(HWND p, const wchar_t* t, int x, int y, int w, int id, int h = 20) {
    return mk(p, L"BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, h, id);
}
HWND mk_edit(HWND p, int x, int y, int w, int id, const wchar_t* t = L"", DWORD extra = 0) {
    return mk(p, L"EDIT", t, ES_AUTOHSCROLL | WS_TABSTOP | extra, x, y, w, 22, id, WS_EX_CLIENTEDGE);
}
HWND mk_combo(HWND p, int x, int y, int w, int id) {
    return mk(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 200, id);
}
HWND mk_spin(HWND p, int x, int y, int w, int edit_id, int spin_id, int lo, int hi, int value) {
    HWND e = mk_edit(p, x, y, w, edit_id, L"", ES_NUMBER);
    HWND ud = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                              0, 0, 0, 0, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(spin_id)), S.inst, nullptr);
    SendMessageW(ud, UDM_SETBUDDY, reinterpret_cast<WPARAM>(e), 0);
    SendMessageW(ud, UDM_SETRANGE32, lo, hi);
    SendMessageW(ud, UDM_SETPOS32, 0, value);
    return e;
}
HWND child(HWND page, int id) { return GetDlgItem(page, id); }
void add_item(HWND combo, const std::string& s) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(W(s).c_str())); }
int get_int(HWND page, int id, int fallback) {
    BOOL ok = FALSE;
    int v = static_cast<int>(GetDlgItemInt(page, id, &ok, TRUE));
    return ok ? v : fallback;
}
void set_int(HWND page, int id, int v) { SetDlgItemInt(page, id, static_cast<UINT>(v), TRUE); }

void message(const std::string& title, const std::string& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(S.hwnd, W(text).c_str(), W(title).c_str(), MB_OK | icon);
}

// ------------------------------------------------------------------ project
Json default_project() {
    static const char* const kJson = R"({
      "metadata": {"name": "Cyberpunk Neon", "version": "1.0.0", "author": "TomTom Owner"},
      "background_color": "#000000",
      "canvas": {"width": 320, "height": 240},
      "display": {"formats": ["horizontal"], "default_format": "horizontal"},
      "data_requirements": ["battery.percent", "gps.fix", "weather.current.temperature", "weather.current.condition"],
      "elements": [
        {"id": "time_main", "type": "digital_time", "format": "HH:MM", "x": 60, "y": 80, "width": 200, "height": 60,
         "color": "#84EBFF", "font_size": 48, "is_12h": false, "rule": null},
        {"id": "date_sec", "type": "date", "x": 80, "y": 145, "width": 160, "height": 24, "color": "#14B4FF",
         "font_size": 20, "rule": null},
        {"id": "battery_warn", "type": "status_icon", "icon_type": "battery_low", "x": 285, "y": 10, "width": 24,
         "height": 24, "color": "#FF4444", "rule": "battery_low"},
        {"id": "gps_status", "type": "status_icon", "icon_type": "gps_fix", "x": 10, "y": 10, "width": 24,
         "height": 24, "color": "#00FFCC", "rule": "gps_fix"},
        {"id": "weather_warn", "type": "status_icon", "icon_type": "weather_unavailable", "x": 40, "y": 10,
         "width": 24, "height": 24, "color": "#FFAA00", "rule": "weather_unavailable"}],
      "rules": [
        {"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20},
        {"name": "gps_fix", "signal": "gps_status", "op": "==", "value": 1},
        {"name": "weather_unavailable", "signal": "weather_status", "op": "==", "value": 0}]
    })";
    Json j;
    std::string err;
    Json::parse(kJson, j, err);
    return j;
}

Json sim_state() {
    Json s = Json::object();
    s.set("battery_percent", Json::integer(S.sim_battery));
    s.set("gps_status", Json::integer(S.sim_gps ? 1 : 0));
    s.set("weather_status", Json::integer(S.sim_weather_off ? 0 : 1));
    s.set("charging", Json::boolean(S.sim_charging));
    return s;
}

Json* elements() {
    Json* e = S.project.find("elements");
    return (e && e->is_array()) ? e : nullptr;
}

void set_title() {
    std::wstring t = L"TomTom Control Center (Face Studio) - TomTom ONE v6";
    if (!S.project_path.empty()) {
        size_t slash = S.project_path.find_last_of(L"\\/");
        t += L" - " + S.project_path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    }
    if (S.dirty) t += L" *";
    SetWindowTextW(S.hwnd, t.c_str());
}
void mark_dirty() {
    if (!S.dirty) { S.dirty = true; set_title(); }
}
void redraw_canvas() { InvalidateRect(S.canvas_wnd, nullptr, FALSE); }

// ------------------------------------------------------------------ dialogs
bool open_dialog(const wchar_t* title, const wchar_t* filter, std::wstring& out) {
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = S.hwnd;
    o.lpstrFilter = filter;
    o.lpstrFile = buf;
    o.nMaxFile = static_cast<DWORD>(sizeof buf / sizeof buf[0]);
    o.lpstrTitle = title;
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&o)) return false;
    out = buf;
    return true;
}
bool open_multi_dialog(const wchar_t* title, const wchar_t* filter, std::vector<std::wstring>& out) {
    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = S.hwnd;
    o.lpstrFilter = filter;
    o.lpstrFile = buf.data();
    o.nMaxFile = static_cast<DWORD>(buf.size());
    o.lpstrTitle = title;
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&o)) return false;
    out.clear();
    std::wstring first = buf.data();
    const wchar_t* next = buf.data() + first.size() + 1;
    if (*next == L'\0') {
        out.push_back(first);
    } else {
        while (*next) {
            std::wstring name = next;
            out.push_back(first + L"\\" + name);
            next += name.size() + 1;
        }
    }
    return true;
}
bool save_dialog(const wchar_t* title, const wchar_t* filter, const wchar_t* def_ext, const std::string& def_name, std::wstring& out) {
    wchar_t buf[MAX_PATH * 4] = L"";
    std::wstring name = W(def_name);
    wcsncpy(buf, name.c_str(), MAX_PATH - 1);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = S.hwnd;
    o.lpstrFilter = filter;
    o.lpstrFile = buf;
    o.nMaxFile = static_cast<DWORD>(sizeof buf / sizeof buf[0]);
    o.lpstrTitle = title;
    o.lpstrDefExt = def_ext;
    o.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&o)) return false;
    out = buf;
    return true;
}
std::string slug(const std::string& name, const char* ext) {
    std::string s;
    for (char c : name) s.push_back(c == ' ' ? '_' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c));
    return s + ext;
}

// ------------------------------------------------------------------ image import (WIC)
bool load_image_wic(const std::wstring& path, RgbaImage& out, std::string& error) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                  reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    if (SUCCEEDED(hr)) {
        if (w == 0 || h == 0 || w > 8192 || h > 8192) {
            error = "Image dimensions are unsupported (maximum 8192x8192).";
        } else {
            out.w = static_cast<int>(w);
            out.h = static_cast<int>(h);
            out.px.assign(static_cast<size_t>(w) * h * 4, 0);
            hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.px.size()), out.px.data());
            ok = SUCCEEDED(hr);
        }
    }
    if (!ok && error.empty()) error = "The file is not a supported image (PNG, JPEG or BMP) or could not be read.";
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return ok;
}

// ------------------------------------------------------------------ canvas rendering
std::vector<uint32_t> g_dib;

void update_scrollbars() {
    RECT rc;
    GetClientRect(S.canvas_wnd, &rc);
    int iw = kCanvasWidth * S.zoom, ih = kCanvasHeight * S.zoom;
    SCROLLINFO si{};
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = iw - 1;
    si.nPage = static_cast<UINT>(rc.right);
    S.scroll_x = std::max(0, std::min(S.scroll_x, std::max<int>(0, iw - static_cast<int>(rc.right))));
    si.nPos = S.scroll_x;
    SetScrollInfo(S.canvas_wnd, SB_HORZ, &si, TRUE);
    si.nMax = ih - 1;
    si.nPage = static_cast<UINT>(rc.bottom);
    S.scroll_y = std::max(0, std::min(S.scroll_y, std::max<int>(0, ih - static_cast<int>(rc.bottom))));
    si.nPos = S.scroll_y;
    SetScrollInfo(S.canvas_wnd, SB_VERT, &si, TRUE);
}

void paint_canvas(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const int z = S.zoom, W_ = kCanvasWidth * z, H_ = kCanvasHeight * z;

    Image frame = compose_preview(S.cvs.image(), S.project, sim_state(), S.sim_time, S.tick);
    g_dib.resize(static_cast<size_t>(W_) * H_);
    const bool fine_grid = z >= 3;
    for (int y = 0; y < H_; ++y) {
        const int sy = y / z;
        uint32_t* row = &g_dib[static_cast<size_t>(y) * W_];
        for (int x = 0; x < W_; ++x) {
            Rgb c = frame.get(x / z, sy);
            bool on_grid = S.grid && (fine_grid ? ((x % z == 0) || (y % z == 0)) : (((x / z) % 10 == 0 && x % z == 0) || ((sy % 10 == 0) && y % z == 0)));
            if (on_grid) { c.r = static_cast<uint8_t>(c.r / 2 + 64); c.g = static_cast<uint8_t>(c.g / 2 + 64); c.b = static_cast<uint8_t>(c.b / 2 + 64); }
            row[x] = 0xFF000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
        }
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = W_;
    bi.bmiHeader.biHeight = -H_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, -S.scroll_x, -S.scroll_y, static_cast<DWORD>(W_), static_cast<DWORD>(H_), 0, 0, 0, static_cast<UINT>(H_),
                      g_dib.data(), &bi, DIB_RGB_COLORS);

    // Gray surround where the bitmap does not cover the client area.
    HBRUSH surround = GetSysColorBrush(COLOR_APPWORKSPACE);
    RECT right{W_ - S.scroll_x, 0, client.right, client.bottom};
    RECT below{0, H_ - S.scroll_y, W_ - S.scroll_x, client.bottom};
    if (right.left < client.right) FillRect(dc, &right, surround);
    if (below.top < client.bottom) FillRect(dc, &below, surround);

    // Selection outline for the element being edited.
    Json* els = elements();
    if (els && S.sel >= 0 && S.sel < static_cast<int>(els->items().size())) {
        const Json& el = els->items()[static_cast<size_t>(S.sel)];
        const Json &jx = el.get("x"), &jy = el.get("y"), &jw = el.get("width"), &jh = el.get("height");
        if (jx.is_int() && jy.is_int() && jw.is_int() && jh.is_int()) {
            HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 0, 255));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            int x0 = static_cast<int>(jx.as_int()) * z - S.scroll_x, y0 = static_cast<int>(jy.as_int()) * z - S.scroll_y;
            Rectangle(dc, x0, y0, x0 + static_cast<int>(jw.as_int()) * z, y0 + static_cast<int>(jh.as_int()) * z);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
    }
    EndPaint(hwnd, &ps);
}

void set_status_text(const std::string& s) { SendMessageW(S.status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(W(s).c_str())); }

bool canvas_point(LPARAM lp, int& cx, int& cy) {
    int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
    if (mx < 0 || my < 0) return false;
    cx = (mx + S.scroll_x) / S.zoom;
    cy = (my + S.scroll_y) / S.zoom;
    return cx >= 0 && cx < kCanvasWidth && cy >= 0 && cy < kCanvasHeight;
}

void set_active_color(const std::string& hex);

void apply_tool(int x, int y) {
    switch (S.tool) {
        case Tool::Pencil: S.cvs.draw_pixel(x, y, S.color, S.brush); break;
        case Tool::Eraser: S.cvs.draw_pixel(x, y, "#000000", S.brush); break;
        case Tool::Bucket: S.cvs.flood_fill(x, y, S.color); break;
        case Tool::Picker: set_active_color(S.cvs.get_pixel_color(x, y)); break;
    }
}

void stroke(int x0, int y0, int x1, int y1) {  // Bresenham so fast drags leave no gaps
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        apply_tool(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paint_canvas(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: update_scrollbars(); return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int x, y;
            if (canvas_point(lp, x, y)) {
                SetCapture(hwnd);
                S.drawing = true;
                S.last_x = x;
                S.last_y = y;
                apply_tool(x, y);
                mark_dirty();
                redraw_canvas();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x, y;
            if (canvas_point(lp, x, y)) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "x=%d y=%d  %s", x, y, S.cvs.get_pixel_color(x, y).c_str());
                set_status_text(buf);
                if (S.drawing) {
                    if (S.tool == Tool::Pencil || S.tool == Tool::Eraser) stroke(S.last_x, S.last_y, x, y);
                    S.last_x = x;
                    S.last_y = y;
                    redraw_canvas();
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (S.drawing) {
                S.drawing = false;
                ReleaseCapture();
                S.cvs.push_history();
            }
            return 0;
        case WM_HSCROLL:
        case WM_VSCROLL: {
            const bool horz = msg == WM_HSCROLL;
            SCROLLINFO si{};
            si.cbSize = sizeof si;
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, horz ? SB_HORZ : SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= 20; break;
                case SB_LINEDOWN: pos += 20; break;
                case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
            }
            (horz ? S.scroll_x : S.scroll_y) = pos;
            update_scrollbars();
            redraw_canvas();
            return 0;
        }
        case WM_MOUSEWHEEL:
            S.scroll_y -= GET_WHEEL_DELTA_WPARAM(wp) / 2;
            update_scrollbars();
            redraw_canvas();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Tab pages forward control notifications to the main window.
LRESULT CALLBACK PageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_HSCROLL:
        case WM_DRAWITEM:
        case WM_CTLCOLORSTATIC:
            return SendMessageW(GetParent(hwnd), msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ colors / tools
HWND page(int p) { return S.pages[p]; }
HWND left_ctl(int id) { return GetDlgItem(S.hwnd, id); }

void set_active_color(const std::string& hex) {
    S.color = hex;
    InvalidateRect(left_ctl(ID_COLOR_BTN), nullptr, TRUE);
}

bool choose_color(std::string& hex) {
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = S.hwnd;
    cc.rgbResult = to_colorref(hex_to_rgb(hex, Rgb{255, 255, 255}));
    cc.lpCustColors = S.custom_colors;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    hex = rgb_to_hex(Rgb{GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult)});
    return true;
}

// ------------------------------------------------------------------ elements tab
int int_or(const Json& el, const char* key, int fallback) {
    const Json& v = el.get(key);
    if (!v.is_int()) return fallback;
    return static_cast<int>(std::max<int64_t>(-1000000, std::min<int64_t>(1000000, v.as_int())));
}

std::string element_label(const Json& el) {
    return "[" + el.get("type").py_str() + "] " + el.get("id").py_str() + " (x=" + el.get("x").py_str() +
           ", y=" + el.get("y").py_str() + ")";
}

void populate_elements_list() {
    HWND lb = child(page(PageElements), ID_EL_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    Json* els = elements();
    if (!els) return;
    for (const Json& el : els->items()) SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(W(element_label(el)).c_str()));
    if (S.sel >= 0 && S.sel < static_cast<int>(els->items().size())) SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(S.sel), 0);
}

const char* const kRuleNames[4] = {"None", "battery_low", "gps_fix", "weather_unavailable"};
const char* const kIconNames[3] = {"battery_low", "gps_fix", "weather_unavailable"};

Json* selected_element() {
    Json* els = elements();
    if (!els || S.sel < 0 || S.sel >= static_cast<int>(els->items().size())) return nullptr;
    return &els->items()[static_cast<size_t>(S.sel)];
}

void show_element_props() {
    HWND pg = page(PageElements);
    Json* el = selected_element();
    for (int id : {ID_EL_ID, ID_EL_X, ID_EL_Y, ID_EL_W, ID_EL_H, ID_EL_FONT, ID_EL_COLOR, ID_EL_RULE, ID_EL_TEXT, ID_EL_ICON, ID_EL_DELETE})
        EnableWindow(child(pg, id), el != nullptr);
    if (!el) return;
    S.updating = true;
    const Json& id = el->get("id");
    set_text(child(pg, ID_EL_ID), id.is_string() ? id.as_string() : "");
    set_int(pg, ID_EL_X, int_or(*el, "x", 0));
    set_int(pg, ID_EL_Y, int_or(*el, "y", 0));
    set_int(pg, ID_EL_W, int_or(*el, "width", 100));
    set_int(pg, ID_EL_H, int_or(*el, "height", 40));
    set_int(pg, ID_EL_FONT, int_or(*el, "font_size", 24));
    int rule_idx = 0;
    if (el->get("rule").is_string())
        for (int k = 1; k < 4; ++k)
            if (el->get("rule").as_string() == kRuleNames[k]) rule_idx = k;
    SendMessageW(child(pg, ID_EL_RULE), CB_SETCURSEL, static_cast<WPARAM>(rule_idx), 0);
    int icon_idx = 0;
    if (el->get("icon_type").is_string())
        for (int k = 0; k < 3; ++k)
            if (el->get("icon_type").as_string() == kIconNames[k]) icon_idx = k;
    SendMessageW(child(pg, ID_EL_ICON), CB_SETCURSEL, static_cast<WPARAM>(icon_idx), 0);
    const Json& t = el->get("text");
    set_text(child(pg, ID_EL_TEXT), t.is_string() ? t.as_string() : "");
    const bool is_text = el->get("type") == Json::string("text");
    const bool is_icon = el->get("type") == Json::string("status_icon");
    EnableWindow(child(pg, ID_EL_TEXT), is_text);
    EnableWindow(child(pg, ID_EL_ICON), is_icon);
    S.updating = false;
    InvalidateRect(child(pg, ID_EL_COLOR), nullptr, TRUE);
}

void apply_element_props() {
    if (S.updating) return;
    Json* el = selected_element();
    if (!el) return;
    HWND pg = page(PageElements);
    auto clamp = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
    el->set("id", Json::string(get_text(child(pg, ID_EL_ID))));
    el->set("x", Json::integer(clamp(get_int(pg, ID_EL_X, int_or(*el, "x", 0)), 0, 320)));
    el->set("y", Json::integer(clamp(get_int(pg, ID_EL_Y, int_or(*el, "y", 0)), 0, 240)));
    el->set("width", Json::integer(clamp(get_int(pg, ID_EL_W, int_or(*el, "width", 100)), 1, 320)));
    el->set("height", Json::integer(clamp(get_int(pg, ID_EL_H, int_or(*el, "height", 40)), 1, 240)));
    el->set("font_size", Json::integer(clamp(get_int(pg, ID_EL_FONT, int_or(*el, "font_size", 24)), 8, 96)));
    int rule_idx = static_cast<int>(SendMessageW(child(pg, ID_EL_RULE), CB_GETCURSEL, 0, 0));
    el->set("rule", rule_idx <= 0 || rule_idx > 3 ? Json() : Json::string(kRuleNames[rule_idx]));
    if (el->get("type") == Json::string("text")) el->set("text", Json::string(get_text(child(pg, ID_EL_TEXT))));
    if (el->get("type") == Json::string("status_icon")) {
        int icon_idx = static_cast<int>(SendMessageW(child(pg, ID_EL_ICON), CB_GETCURSEL, 0, 0));
        if (icon_idx >= 0 && icon_idx < 3) el->set("icon_type", Json::string(kIconNames[icon_idx]));
    }
    populate_elements_list();
    mark_dirty();
    redraw_canvas();
}

void add_element(const std::string& type) {
    Json* els = elements();
    if (!els) {
        S.project.set("elements", Json::array());
        els = elements();
    }
    Json el = Json::object();
    el.set("id", Json::string(type + "_" + std::to_string(els->items().size() + 1)));
    el.set("type", Json::string(type));
    el.set("x", Json::integer(50));
    el.set("y", Json::integer(50));
    el.set("width", Json::integer(100));
    el.set("height", Json::integer(40));
    el.set("color", Json::string("#84EBFF"));
    el.set("font_size", Json::integer(24));
    el.set("rule", Json());
    if (type == "status_icon") {
        el.set("icon_type", Json::string("battery_low"));
        el.set("width", Json::integer(24));
        el.set("height", Json::integer(24));
        el.set("rule", Json::string("battery_low"));
    }
    els->push(std::move(el));
    S.sel = static_cast<int>(els->items().size()) - 1;
    populate_elements_list();
    show_element_props();
    mark_dirty();
    redraw_canvas();
}

void delete_element() {
    Json* els = elements();
    if (!els || !selected_element()) return;
    els->items().erase(els->items().begin() + S.sel);
    S.sel = -1;
    populate_elements_list();
    show_element_props();
    mark_dirty();
    redraw_canvas();
}

// ------------------------------------------------------------------ export tab state
std::vector<std::string> g_default_format_ids;

std::vector<std::string> checked_formats() {
    std::vector<std::string> out;
    for (int i = 0; i < 2; ++i)
        if (Button_GetCheck(child(page(PageExport), ID_FMT0 + i)) == BST_CHECKED) out.push_back(kFormats[i].id);
    return out;
}

Json& display_object() {
    Json* d = S.project.find("display");
    if (!d || !d->is_object()) {
        S.project.set("display", Json::object());
        d = S.project.find("display");
    }
    return *d;
}

void refresh_default_format_choices(const std::string& preferred, bool write_project) {
    HWND combo = child(page(PageExport), ID_FMT_DEFAULT);
    std::vector<std::string> formats = checked_formats();
    std::string previous = preferred;
    if (previous.empty()) {
        int cur = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (cur >= 0 && cur < static_cast<int>(g_default_format_ids.size())) previous = g_default_format_ids[static_cast<size_t>(cur)];
    }
    S.updating = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    g_default_format_ids.clear();
    int selected = -1;
    for (const std::string& id : formats) {
        const FormatDef* def = find_format(id);
        add_item(combo, std::string(def->name) + " (" + id + ")");
        if (id == previous) selected = static_cast<int>(g_default_format_ids.size());
        g_default_format_ids.push_back(id);
    }
    if (selected < 0 && !formats.empty()) selected = 0;
    if (selected >= 0) SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    EnableWindow(combo, !formats.empty());
    S.updating = false;
    if (write_project) {
        Json& display = display_object();
        Json arr = Json::array();
        for (const std::string& f : formats) arr.push(Json::string(f));
        display.set("formats", arr);
        display.set("default_format", selected >= 0 ? Json::string(g_default_format_ids[static_cast<size_t>(selected)]) : Json());
    }
}

void sync_data_requirements() {
    Json reqs = Json::array();
    for (int i = 0; i < 8; ++i)
        if (Button_GetCheck(child(page(PageData), ID_DATA0 + i)) == BST_CHECKED) reqs.push(Json::string(kDataFields[i].id));
    S.project.set("data_requirements", reqs);
}

void sync_metadata() {
    HWND pg = page(PageExport);
    Json* meta = S.project.find("metadata");
    if (!meta || !meta->is_object()) {
        S.project.set("metadata", Json::object());
        meta = S.project.find("metadata");
    }
    meta->set("name", Json::string(get_text(child(pg, ID_META_NAME))));
    meta->set("author", Json::string(get_text(child(pg, ID_META_AUTHOR))));
    meta->set("version", Json::string(get_text(child(pg, ID_META_VER))));
    refresh_default_format_choices("", true);
    sync_data_requirements();
}

void log_text(const std::string& text) { set_text(child(page(PageExport), ID_LOG), lines_to_crlf(text)); }

std::string join(const std::vector<std::string>& lines, const char* sep = "\n") {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? sep : "") + lines[i];
    return out;
}

std::string formats_text(const Json& formats, const std::string& def) {
    std::string s;
    for (const Json& f : formats.items()) s += (s.empty() ? "" : ", ") + f.py_str();
    if (s.empty()) return "(none)";
    return s + " (default: " + (def.empty() ? "-" : def) + ")";
}

std::string describe_face(const InspectedFace& f) {
    std::string out;
    if (f.loaded) {
        const Json& m = f.manifest;
        ProjectFormats pf = get_project_formats(m);
        out += "Face: " + m.get("face_name").py_str() + "\nVersion: " + m.get("version").py_str() + "   Author: " + m.get("author").py_str() +
               "\nTarget: " + m.get("target_device").py_str() + "\nRenderer: " + m.get("renderer_id").py_str() +
               "\nFormats: " + formats_text(pf.formats, pf.default_format) + "\nData requirements: " +
               (m.get("data_requirements").is_array() && !m.get("data_requirements").items().empty() ? [&] {
                   std::string s;
                   for (const Json& r : m.get("data_requirements").items()) s += (s.empty() ? "" : ", ") + r.py_str();
                   return s;
               }() : std::string("(none)")) +
               "\nElements: " + std::to_string(m.get("elements").is_array() ? m.get("elements").items().size() : 0) +
               "   Rules: " + std::to_string(m.get("rules").is_array() ? m.get("rules").items().size() : 0) +
               "\nBackground: " + (f.has_background ? "320x240 RGB565 present" : "not readable") + "\n";
    }
    out += f.errors.empty() ? "\nNo problems found." : "\nProblems:\n" + join(f.errors);
    return out;
}

std::string describe_gallery(const InspectedGallery& g) {
    if (!g.ok) return "Gallery is NOT valid:\n" + join(g.errors);
    const Json& m = g.manifest;
    std::string out = "Gallery: " + m.get("name").py_str() + "   Version: " + m.get("version").py_str() + "\nTarget: " +
                      m.get("target_device").py_str() + "\nFaces: " + m.get("face_count").py_str() + "\n";
    for (const Json& face : m.get("faces").items())
        out += "\n  " + face.get("id").py_str() + " - " + face.get("name").py_str() + " v" + face.get("version").py_str() +
               "\n      renderer " + face.get("renderer_id").py_str() + ", formats " +
               formats_text(face.get("formats"), face.get("default_format").is_string() ? face.get("default_format").as_string() : "") +
               "\n      sha256 " + face.get("sha256").py_str().substr(0, 16) + "... (verified)";
    return out + "\n\nAll checksums, nested packages and previews verified.";
}

// ------------------------------------------------------------------ project file / export commands
void refresh_all_ui() {
    S.updating = true;
    HWND ex = page(PageExport);
    const Json& meta = S.project.get("metadata");
    auto meta_str = [&](const char* key) {
        return meta.is_object() && meta.get(key).is_string() ? meta.get(key).as_string() : std::string();
    };
    set_text(child(ex, ID_META_NAME), meta_str("name"));
    set_text(child(ex, ID_META_AUTHOR), meta_str("author"));
    set_text(child(ex, ID_META_VER), meta_str("version"));
    ProjectFormats pf = get_project_formats(S.project);
    for (int i = 0; i < 2; ++i) {
        bool on = false;
        for (const Json& f : pf.formats.items())
            if (f == Json::string(kFormats[i].id)) on = true;
        Button_SetCheck(child(ex, ID_FMT0 + i), on ? BST_CHECKED : BST_UNCHECKED);
    }
    S.updating = false;
    refresh_default_format_choices(pf.default_format, false);
    const Json& reqs = S.project.get("data_requirements");
    for (int i = 0; i < 8; ++i) {
        bool on = false;
        if (reqs.is_array())
            for (const Json& r : reqs.items())
                if (r == Json::string(kDataFields[i].id)) on = true;
        Button_SetCheck(child(page(PageData), ID_DATA0 + i), on ? BST_CHECKED : BST_UNCHECKED);
    }
    S.sel = -1;
    populate_elements_list();
    show_element_props();
    redraw_canvas();
}

bool do_save(bool save_as);

bool confirm_discard() {
    if (!S.dirty) return true;
    int r = MessageBoxW(S.hwnd, L"Save changes to the current project?", L"TomTom Face Studio", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return do_save(false);
    return true;
}

bool do_save(bool save_as) {
    sync_metadata();
    std::wstring path = S.project_path;
    if (save_as || path.empty()) {
        std::string def = slug(get_text(child(page(PageExport), ID_META_NAME)), ".ttproj");
        if (!save_dialog(L"Save Project", L"TomTom Face Project (*.ttproj)\0*.ttproj\0\0", L"ttproj", def, path)) return false;
    }
    std::string err;
    if (!save_project_file(U(path), S.project, S.cvs.image(), err)) {
        message("Save Failed", err, MB_ICONERROR);
        return false;
    }
    S.project_path = path;
    S.dirty = false;
    set_title();
    return true;
}

void do_open() {
    if (!confirm_discard()) return;
    std::wstring path;
    if (!open_dialog(L"Open Project", L"TomTom Face Project (*.ttproj)\0*.ttproj\0All files\0*.*\0\0", path)) return;
    Json project;
    Image image;
    std::string err;
    if (!load_project_file(U(path), project, image, err)) {
        message("Open Failed", err, MB_ICONERROR);
        return;
    }
    S.project = std::move(project);
    S.cvs = CanvasModel("#000000");
    S.cvs.set_image(image);
    S.cvs.push_history();
    S.project_path = path;
    S.dirty = false;
    refresh_all_ui();
    set_title();
}

void do_validate() {
    sync_metadata();
    std::vector<std::string> errors = validate_face_project(S.project);
    if (errors.empty()) {
        log_text("Project passed all 320x240 TomTom ONE schema checks.");
        message("Validation Passed", "Project passed all 320x240 TomTom ONE schema checks cleanly!");
    } else {
        log_text("Project validation results:\n\n" + join(errors));
        message("Validation Issues Found", "Project Validation Results:\n\n" + join(errors), MB_ICONWARNING);
    }
}

void do_export_face() {
    sync_metadata();
    std::wstring path;
    if (!save_dialog(L"Export .ttface Package", L"TomTom Face Package (*.ttface)\0*.ttface\0\0", L"ttface",
                     slug(get_text(child(page(PageExport), ID_META_NAME)), ".ttface"), path))
        return;
    ExportResult r = export_ttface_package(S.project, &S.cvs.image(), U(path));
    if (r.ok) {
        log_text("Exported package to:\n" + U(path));
        message("Export Successful", "Exported package cleanly to:\n" + U(path));
    } else {
        log_text("Export errors:\n\n" + join(r.messages));
        message("Export Failed", "Export errors:\n\n" + join(r.messages), MB_ICONERROR);
    }
}

void do_export_gallery() {
    sync_metadata();
    std::vector<std::wstring> others;
    open_multi_dialog(L"Add Existing Face Packages (optional)", L"TomTom Face Packages (*.ttface)\0*.ttface\0\0", others);
    std::string gallery_name = get_text(child(page(PageExport), ID_GAL_NAME));
    std::wstring path;
    if (!save_dialog(L"Build .ttgallery Collection", L"TomTom Face Gallery (*.ttgallery)\0*.ttgallery\0\0", L"ttgallery",
                     slug(gallery_name, ".ttgallery"), path))
        return;
    std::vector<Bytes> packages(1);
    ExportResult built = build_ttface_package(S.project, &S.cvs.image(), packages[0]);
    if (!built.ok) {
        message("Gallery Build Failed", join(built.messages), MB_ICONERROR);
        return;
    }
    for (const std::wstring& other : others) {
        Bytes data;
        std::string err;
        if (!read_file(U(other), data, kMaxFacePackageBytes + 1, err)) {
            message("Gallery Build Failed", err, MB_ICONERROR);
            return;
        }
        packages.push_back(std::move(data));
    }
    Bytes zip;
    GalleryResult g = build_ttgallery(packages, gallery_name, "1.0.0", zip);
    std::string err;
    if (g.ok && !write_file(U(path), zip, err)) {
        g.ok = false;
        g.errors.push_back("Gallery export failed: " + err);
    }
    if (!g.ok) {
        log_text("Gallery build failed:\n" + join(g.errors));
        message("Gallery Build Failed", join(g.errors), MB_ICONERROR);
        return;
    }
    log_text("Created gallery with " + std::to_string(packages.size()) + " face(s):\n" + U(path));
    message("Gallery Built", "Created gallery with " + std::to_string(packages.size()) + " face(s):\n" + U(path) +
                                 "\n\nRebuild the gallery from the desired face list to reflect additions and removals.");
}

void do_inspect_face() {
    std::wstring path;
    if (!open_dialog(L"Inspect .ttface Package", L"TomTom Face Package (*.ttface)\0*.ttface\0All files\0*.*\0\0", path)) return;
    log_text(U(path) + "\n\n" + describe_face(inspect_ttface_file(U(path))));
}

void do_inspect_gallery() {
    std::wstring path;
    if (!open_dialog(L"Inspect .ttgallery", L"TomTom Face Gallery (*.ttgallery)\0*.ttgallery\0All files\0*.*\0\0", path)) return;
    log_text(U(path) + "\n\n" + describe_gallery(inspect_ttgallery_file(U(path))));
}

void do_import_artwork() {
    std::wstring path;
    if (!open_dialog(L"Import Background Artwork", L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0\0", path)) return;
    RgbaImage img;
    std::string err;
    if (!load_image_wic(path, img, err)) {
        message("Import Failed", "Could not import image: " + err, MB_ICONERROR);
        return;
    }
    static const char* const kModes[4] = {"contain", "cover", "stretch", "custom"};
    int mode = static_cast<int>(SendMessageW(left_ctl(ID_FIT), CB_GETCURSEL, 0, 0));
    if (!S.cvs.import_background(img, kModes[mode < 0 || mode > 3 ? 0 : mode], 0, 0, err)) {
        message("Import Failed", "Could not import image: " + err, MB_ICONERROR);
        return;
    }
    mark_dirty();
    redraw_canvas();
}

// ------------------------------------------------------------------ device tab
std::string g_device_note;

void set_device_status(int state, const std::string& text) {
    S.dev_state = state;
    HWND st = child(page(PageDevice), ID_DEV_STATUS);
    set_text(st, text);
    InvalidateRect(st, nullptr, TRUE);
}

void set_device_busy(bool busy) {
    S.dev_busy = busy;
    for (int id : {ID_DEV_APPLY, ID_DEV_READ, ID_DEV_PING}) EnableWindow(child(page(PageDevice), id), !busy);
}

void start_device(int kind) {  // 0 ping, 1 status, 2 set face
    if (S.dev_busy) return;
    HWND pg = page(PageDevice);
    std::string host, err;
    if (!parse_device_host(get_text(child(pg, ID_DEV_HOST)), host, err)) {
        set_device_status(2, err);
        return;
    }
    int face = static_cast<int>(SendMessageW(child(pg, ID_DEV_FACE), CB_GETCURSEL, 0, 0));
    if (kind == 2 && !is_valid_face_id(face)) {
        set_device_status(2, "Choose a face first.");
        return;
    }
    DeviceOptions options;
    options.host = host;
    int port = get_int(pg, ID_DEV_PORT, kDefaultDevicePort);
    if (port < 1024 || port > 65535) {
        set_device_status(2, "Choose a port between 1024 and 65535 (the TomTom uses 18743).");
        return;
    }
    options.port = port;
    if (is_usb_link_address(host)) g_device_note = "";
    else if (host.rfind("127.", 0) == 0)
        g_device_note = "\nNote: connecting through a local tunnel (port " + std::to_string(port) + "). Only face control uses it; notifications and screen mirroring still need a direct route.";
    else g_device_note = "\nNote: " + host + " is not on the TomTom's USB subnet (192.168.101.0/24).";
    set_device_busy(true);
    set_device_status(0, std::string(kind == 2 ? "Sending the face change" : kind == 1 ? "Reading the current face" : "Contacting the TomTom") + "...");
    HWND hwnd = S.hwnd;
    std::thread([hwnd, kind, options, face] {
        DeviceResult r = kind == 0 ? device_ping(options) : kind == 1 ? device_status(options) : device_set_face(options, face);
        DeviceReply* reply = new DeviceReply{kind, r};
        if (!PostMessageW(hwnd, WM_DEVICE_DONE, 0, reinterpret_cast<LPARAM>(reply))) delete reply;
    }).detach();
}

void on_device_done(DeviceReply* reply) {
    set_device_busy(false);
    const DeviceResult& r = reply->result;
    if (r.ok() && r.face_id >= 0) SendMessageW(child(page(PageDevice), ID_DEV_FACE), CB_SETCURSEL, static_cast<WPARAM>(r.face_id), 0);
    set_device_status(r.ok() ? 1 : 2, r.message + g_device_note);
    delete reply;
}

// ------------------------------------------------------------------ UI construction
void show_page(int index) {
    for (int i = 0; i < PageCount; ++i) ShowWindow(S.pages[i], i == index ? SW_SHOW : SW_HIDE);
}

// Control-center mode hides the drawing tools and the canvas and gives the tabs the whole window.
void apply_mode_visibility() {
    struct Ctx { bool show; } ctx{S.designer};
    EnumChildWindows(S.hwnd, [](HWND w, LPARAM lp) -> BOOL {
        if (GetParent(w) != S.hwnd) return TRUE;
        if (w == S.tab || w == S.status || w == S.canvas_wnd) return TRUE;
        for (int i = 0; i < PageCount; ++i)
            if (w == S.pages[i]) return TRUE;
        ShowWindow(w, reinterpret_cast<Ctx*>(lp)->show ? SW_SHOW : SW_HIDE);  // left-panel controls
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    ShowWindow(S.canvas_wnd, S.designer ? SW_SHOW : SW_HIDE);
}

void layout() {
    if (!S.tab) return;
    RECT rc;
    GetClientRect(S.hwnd, &rc);
    SendMessageW(S.status, WM_SIZE, 0, 0);
    RECT sb;
    GetWindowRect(S.status, &sb);
    int bottom = rc.bottom - (sb.bottom - sb.top);
    int pad = D(8);
    int tab_x, tab_w;
    if (S.designer) {
        int right_w = D(430), left_w = D(240);
        MoveWindow(S.canvas_wnd, left_w, pad, std::max(50, static_cast<int>(rc.right) - left_w - right_w - pad), bottom - 2 * pad, TRUE);
        tab_x = rc.right - right_w;
        tab_w = right_w - pad;
    } else {
        tab_x = pad;
        tab_w = std::max<int>(D(300), static_cast<int>(rc.right) - 2 * pad);
    }
    int tab_h = bottom - 2 * pad;
    MoveWindow(S.tab, tab_x, pad, tab_w, tab_h, TRUE);
    RECT disp{0, 0, tab_w, tab_h};
    TabCtrl_AdjustRect(S.tab, FALSE, &disp);
    for (int i = 0; i < PageCount; ++i) MoveWindow(S.pages[i], tab_x + disp.left, pad + disp.top, disp.right - disp.left, disp.bottom - disp.top, TRUE);
    HWND log = child(S.pages[PageExport], ID_LOG);
    RECT lr;
    GetWindowRect(log, &lr);
    POINT tl{lr.left, lr.top};
    ScreenToClient(S.pages[PageExport], &tl);
    MoveWindow(log, tl.x, tl.y, disp.right - disp.left - D(16), std::max<int>(D(60), static_cast<int>(disp.bottom - disp.top) - static_cast<int>(tl.y) - D(8)), TRUE);
    update_scrollbars();
}

void rebuild_tabs() {
    static const wchar_t* const names[PageCount] = {L"Elements", L"Sim", L"Data", L"Export", L"Device", L"Display", L"Live"};
    TabCtrl_DeleteAllItems(S.tab);
    S.tab_pages.clear();
    if (S.designer) S.tab_pages = {PageElements, PageSim, PageData, PageExport, PageDevice, PageDisplay, PageLive};
    else S.tab_pages = {PageDevice, PageDisplay, PageLive};
    for (size_t i = 0; i < S.tab_pages.size(); ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(names[S.tab_pages[i]]);
        TabCtrl_InsertItem(S.tab, static_cast<int>(i), &ti);
    }
    TabCtrl_SetCurSel(S.tab, 0);
    show_page(S.tab_pages.empty() ? 0 : S.tab_pages[0]);
}

void set_designer_mode(bool on, bool resize_window) {
    S.designer = on;
    HMENU view = GetSubMenu(GetMenu(S.hwnd), 2);
    if (view) CheckMenuItem(view, IDM_DESIGNER, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    apply_mode_visibility();
    rebuild_tabs();
    if (resize_window && !IsZoomed(S.hwnd)) {
        RECT r;
        GetWindowRect(S.hwnd, &r);
        SetWindowPos(S.hwnd, nullptr, r.left, r.top, on ? D(1180) : D(560), on ? D(770) : D(800), SWP_NOZORDER | SWP_NOMOVE);
    }
    layout();
    redraw_canvas();
}

void create_left_panel(HWND h) {
    mk(h, L"BUTTON", L"Hand Drawing Tools", BS_GROUPBOX, 8, 4, 224, 224, -1);
    mk(h, L"BUTTON", L"Pencil", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP | WS_TABSTOP, 16, 24, 100, 26, ID_TOOL_PENCIL);
    mk(h, L"BUTTON", L"Eraser", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 120, 24, 104, 26, ID_TOOL_ERASER);
    mk(h, L"BUTTON", L"Fill Bucket", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 16, 54, 100, 26, ID_TOOL_BUCKET);
    mk(h, L"BUTTON", L"Eyedropper", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 120, 54, 104, 26, ID_TOOL_PICKER);
    CheckRadioButton(h, ID_TOOL_PENCIL, ID_TOOL_PICKER, ID_TOOL_PENCIL);
    mk_label(h, L"Brush size:", 16, 92, 64);
    mk_spin(h, 84, 88, 56, ID_BRUSH, ID_BRUSH_SPIN, 1, 8, 1);
    mk_label(h, L"Color:", 16, 122, 64);
    mk(h, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 84, 118, 56, 24, ID_COLOR_BTN);
    for (int i = 0; i < 8; ++i) mk(h, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 16 + i * 26, 150, 24, 24, ID_SWATCH0 + i);
    mk_button(h, L"Undo", 16, 184, 64, 26, ID_UNDO);
    mk_button(h, L"Redo", 84, 184, 64, 26, ID_REDO);
    mk_button(h, L"Clear", 152, 184, 72, 26, ID_CLEAR);

    mk(h, L"BUTTON", L"Import Background Artwork", BS_GROUPBOX, 8, 236, 224, 96, -1);
    mk_button(h, L"Import PNG / JPG Artwork...", 16, 256, 208, 26, ID_IMPORT);
    mk_label(h, L"Fit:", 16, 294, 30);
    HWND fit = mk_combo(h, 50, 290, 174, ID_FIT);
    for (const char* m : {"contain", "cover", "stretch", "custom"}) add_item(fit, m);
    SendMessageW(fit, CB_SETCURSEL, 0, 0);

    mk(h, L"BUTTON", L"View", BS_GROUPBOX, 8, 340, 224, 86, -1);
    mk_label(h, L"Zoom:", 16, 364, 40);
    HWND zoom = mk_combo(h, 60, 360, 164, ID_ZOOM);
    for (const char* z : {"1x (320x240)", "2x (640x480)", "3x (960x720)", "4x (1280x960)"}) add_item(zoom, z);
    SendMessageW(zoom, CB_SETCURSEL, static_cast<WPARAM>(S.zoom - 1), 0);
    HWND grid = mk_check(h, L"Show grid overlay", 16, 392, 200, ID_GRID);
    Button_SetCheck(grid, BST_CHECKED);
}

void create_elements_page(HWND p) {
    mk(p, L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, 8, 8, 364, 112, ID_EL_LIST, WS_EX_CLIENTEDGE);
    mk_button(p, L"+ Time", 8, 128, 84, 24, ID_EL_ADD_TIME);
    mk_button(p, L"+ Date", 96, 128, 84, 24, ID_EL_ADD_DATE);
    mk_button(p, L"+ Text", 184, 128, 84, 24, ID_EL_ADD_TEXT);
    mk_button(p, L"+ Icon", 272, 128, 84, 24, ID_EL_ADD_ICON);
    mk_button(p, L"Delete Element", 8, 158, 120, 24, ID_EL_DELETE);
    mk(p, L"BUTTON", L"Element Properties", BS_GROUPBOX, 8, 190, 364, 262, -1);
    mk_label(p, L"ID:", 18, 216, 70);
    mk_edit(p, 92, 212, 266, ID_EL_ID);
    mk_label(p, L"X:", 18, 246, 70);
    mk_spin(p, 92, 242, 70, ID_EL_X, ID_EL_X_SPIN, 0, 320, 0);
    mk_label(p, L"Y:", 196, 246, 30);
    mk_spin(p, 230, 242, 70, ID_EL_Y, ID_EL_Y_SPIN, 0, 240, 0);
    mk_label(p, L"Width:", 18, 276, 70);
    mk_spin(p, 92, 272, 70, ID_EL_W, ID_EL_W_SPIN, 1, 320, 100);
    mk_label(p, L"Height:", 196, 276, 40);
    mk_spin(p, 230, 272, 70, ID_EL_H, ID_EL_H_SPIN, 1, 240, 40);
    mk_label(p, L"Font size:", 18, 306, 70);
    mk_spin(p, 92, 302, 70, ID_EL_FONT, ID_EL_FONT_SPIN, 8, 96, 24);
    mk_label(p, L"Color:", 196, 306, 40);
    mk(p, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 240, 302, 60, 24, ID_EL_COLOR);
    mk_label(p, L"Rule:", 18, 338, 70);
    HWND rule = mk_combo(p, 92, 334, 200, ID_EL_RULE);
    for (const char* r : kRuleNames) add_item(rule, r);
    mk_label(p, L"Text:", 18, 370, 70);
    mk_edit(p, 92, 366, 266, ID_EL_TEXT);
    mk_label(p, L"Icon:", 18, 402, 70);
    HWND icon = mk_combo(p, 92, 398, 200, ID_EL_ICON);
    for (const char* i : kIconNames) add_item(icon, i);
    mk_label(p, L"Elements show only while their rule is true in the Simulator.", 18, 428, 340, 18);
}

void create_sim_page(HWND p) {
    mk_label(p, L"[SIMULATED DATA ONLY - never presented as live device telemetry]", 8, 8, 364, 34);
    mk_label(p, L"Battery level [SIMULATED]:", 8, 52, 200);
    HWND bat = mk(p, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP, 8, 72, 300, 32, ID_SIM_BAT);
    SendMessageW(bat, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(bat, TBM_SETTICFREQ, 10, 0);
    SendMessageW(bat, TBM_SETPOS, TRUE, S.sim_battery);
    mk_label(p, L"", 314, 80, 56, 18, ID_SIM_BAT_LBL);
    Button_SetCheck(mk_check(p, L"GPS satellite fix acquired [SIMULATED] (gps_fix)", 8, 116, 364, ID_SIM_GPS), S.sim_gps ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(mk_check(p, L"Weather telemetry offline [SIMULATED] (weather_unavailable)", 8, 142, 364, ID_SIM_WEATHER), S.sim_weather_off ? BST_CHECKED : BST_UNCHECKED);
    mk_check(p, L"Charging [SIMULATED] (charging)", 8, 168, 364, ID_SIM_CHARGING);
    mk(p, L"BUTTON", L"Clock", BS_GROUPBOX, 8, 202, 364, 96, -1);
    Button_SetCheck(mk_check(p, L"Follow the PC clock (live, 1 Hz)", 18, 224, 340, ID_SIM_LIVE), BST_CHECKED);
    mk_label(p, L"Hour:", 18, 260, 40);
    mk_spin(p, 62, 256, 60, ID_SIM_HOUR, ID_SIM_HOUR_SPIN, 0, 23, 10);
    mk_label(p, L"Minute:", 150, 260, 50);
    mk_spin(p, 204, 256, 60, ID_SIM_MIN, ID_SIM_MIN_SPIN, 0, 59, 8);
    EnableWindow(child(p, ID_SIM_HOUR), FALSE);
    EnableWindow(child(p, ID_SIM_MIN), FALSE);
    set_text(child(p, ID_SIM_BAT_LBL), std::to_string(S.sim_battery) + "%");
}

void create_data_page(HWND p) {
    mk_label(p, L"Data this face declares it needs (written to the package manifest):", 8, 8, 364, 34);
    for (int i = 0; i < 8; ++i) {
        std::wstring label = W(std::string(kDataFields[i].id) + " - " + kDataFields[i].description);
        mk(p, L"BUTTON", label.c_str(), BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 8, 46 + i * 40, 364, 36, ID_DATA0 + i);
    }
    mk_label(p, L"Weather cache and location sharing are device/service features and are not part of this editor; only the declared "
                L"requirements are exported.", 8, 372, 364, 54);
}

void create_export_page(HWND p) {
    mk(p, L"BUTTON", L"Package Metadata", BS_GROUPBOX, 8, 4, 364, 122, -1);
    mk_label(p, L"Face name:", 18, 28, 80);
    mk_edit(p, 104, 24, 254, ID_META_NAME);
    mk_label(p, L"Author:", 18, 58, 80);
    mk_edit(p, 104, 54, 254, ID_META_AUTHOR);
    mk_label(p, L"Version:", 18, 88, 80);
    mk_edit(p, 104, 84, 254, ID_META_VER);
    mk(p, L"BUTTON", L"Formats Supported by This Face", BS_GROUPBOX, 8, 132, 364, 86, -1);
    mk_check(p, L"Side by side (horizontal)", 18, 152, 160, ID_FMT0);
    mk_check(p, L"Stacked (stacked)", 190, 152, 170, ID_FMT1);
    mk_label(p, L"Default format:", 18, 188, 90);
    mk_combo(p, 112, 184, 246, ID_FMT_DEFAULT);
    mk_label(p, L"Gallery name:", 8, 228, 90);
    mk_edit(p, 104, 224, 268, ID_GAL_NAME, L"My TomTom Faces");
    mk_button(p, L"Validate Project Schema", 8, 256, 178, 26, ID_VALIDATE);
    mk_button(p, L"Export .ttface Package...", 194, 256, 178, 26, ID_EXPORT);
    mk_button(p, L"Build .ttgallery Collection...", 8, 288, 178, 26, ID_GALLERY);
    mk_button(p, L"Inspect .ttface...", 194, 288, 84, 26, ID_INSPECT_FACE);
    mk_button(p, L"Inspect .ttgallery...", 282, 288, 90, 26, ID_INSPECT_GAL);
    mk(p, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 8, 324, 364, 140, ID_LOG, WS_EX_CLIENTEDGE);
}

void create_device_page(HWND p) {
    mk(p, L"BUTTON", L"Connected TomTom Face", BS_GROUPBOX, 8, 4, 364, 282, -1);
    mk_label(p, L"TomTom USB IP:", 18, 30, 100);
    mk_edit(p, 122, 26, 138, ID_DEV_HOST, W(kDefaultDeviceHost).c_str());
    mk_label(p, L"Port:", 266, 30, 32);
    mk_spin(p, 300, 26, 58, ID_DEV_PORT, ID_DEV_PORT_SPIN, 1024, 65535, kDefaultDevicePort);
    mk_label(p, L"Face:", 18, 62, 100);
    HWND face = mk_combo(p, 122, 58, 236, ID_DEV_FACE);
    for (int i = 0; i < kDeviceFaceCount; ++i) add_item(face, std::to_string(i) + " - " + kDeviceFaces[i].name);
    SendMessageW(face, CB_SETCURSEL, 7, 0);  // Ubuntu, the face last restored on the device
    mk_button(p, L"Make This the Active Face", 18, 94, 340, 28, ID_DEV_APPLY);
    mk_button(p, L"Read Current Device Face", 18, 128, 340, 28, ID_DEV_READ);
    mk_button(p, L"Test Connection (PING)", 18, 162, 340, 28, ID_DEV_PING);
    mk_label(p, L"Connect the TomTom directly by USB Ethernet.", 18, 198, 340, 82, ID_DEV_STATUS);
    mk_label(p, L"SECURITY: Face Studio talks to a small fixed-command service (PING, STATUS, SET_FACE) on TCP port 18743 that "
                L"listens only on the TomTom's USB Ethernet address. The link is plain text and unauthenticated. Keep the USB "
                L"connection direct; never bridge, route or expose it to Wi-Fi, other networks or the internet. Only private, "
                L"link-local and loopback addresses are accepted.",
             8, 296, 364, 120, ID_DEV_WARN);
}

// ------------------------------------------------------------------ live features
// Display mirror, notification sender, webhook and Spotify now-playing / lyrics.
// Everything here talks to the TomTom only over its USB address and only through
// the existing receivers: UDP 45872 (notifications) and TCP 18745 (display frames).
struct LyricsReply {
    std::string key;
    bool ok = false;
    std::string lrc;
    std::string error;
};

struct Features {
    app::ExtDisplay ext;
    ULONGLONG last_ext_poll = 0;
    unsigned ext_action_seen = 0;
    std::string ext_message;  // one-off result of the last button press
    std::unique_ptr<app::Mirror> mirror;
    std::vector<app::MirrorSource> sources;
    bool mirror_confirmed = false;

    tt::WebhookServer webhook;
    std::string token;
    std::mutex host_mutex;
    std::string notify_host = kDefaultDeviceHost;

    bool announce = false, lyrics_on = false;
    std::string track_key, artist, title;
    bool track_playing = false;
    long long pos_base_ms = 0;
    ULONGLONG resume_tick = 0;
    long long offset_ms = 0;
    std::vector<tt::LyricLine> lines;
    std::string lyrics_state;
    int shown_line = -2, shown_chunk = -1;
    ULONGLONG last_send_tick = 0, last_poll_tick = 0;
};
Features F;

std::wstring settings_file() {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir = std::wstring(base) + L"\\TomTomFaceStudio";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void save_settings() {
    std::wstring f = settings_file();
    if (f.empty()) return;
    HWND pg = page(PageLive);
    WritePrivateProfileStringW(L"webhook", L"token", W(F.token).c_str(), f.c_str());
    WritePrivateProfileStringW(L"webhook", L"port", std::to_wstring(get_int(pg, ID_WH_PORT, tt::kDefaultWebhookPort)).c_str(), f.c_str());
    WritePrivateProfileStringW(L"webhook", L"lan", Button_GetCheck(child(pg, ID_WH_LAN)) == BST_CHECKED ? L"1" : L"0", f.c_str());
    WritePrivateProfileStringW(L"ui", L"designer", S.designer ? L"1" : L"0", f.c_str());
}

void load_settings() {
    std::wstring f = settings_file();
    if (f.empty()) return;
    HWND pg = page(PageLive);
    wchar_t buf[128] = L"";
    GetPrivateProfileStringW(L"webhook", L"token", L"", buf, 128, f.c_str());
    std::string tok = U(buf);
    if (tok.size() >= tt::kMinWebhookTokenLength) F.token = tok;
    set_int(pg, ID_WH_PORT, static_cast<int>(GetPrivateProfileIntW(L"webhook", L"port", tt::kDefaultWebhookPort, f.c_str())));
    Button_SetCheck(child(pg, ID_WH_LAN), GetPrivateProfileIntW(L"webhook", L"lan", 0, f.c_str()) ? BST_CHECKED : BST_UNCHECKED);
    set_text(child(pg, ID_WH_TOKEN), F.token);
    S.designer = GetPrivateProfileIntW(L"ui", L"designer", 0, f.c_str()) != 0;
}

void remember_notify_host() {
    std::string host, err;
    if (!parse_device_host(get_text(child(page(PageDevice), ID_DEV_HOST)), host, err)) return;
    std::lock_guard<std::mutex> lock(F.host_mutex);
    F.notify_host = host;
}

bool send_to_tomtom(int ttl, const std::string& text, std::string& err) {
    std::string host;
    {
        std::lock_guard<std::mutex> lock(F.host_mutex);
        host = F.notify_host;
    }
    return tt::send_notification(host, ttl, text, err);
}

void label(HWND pg, int id, const std::string& text) { set_text(child(pg, id), text); }

// ---- display mirror
void mirror_update_ui() {
    HWND pg = page(PageDisplay);
    bool running = F.mirror && F.mirror->running();
    EnableWindow(child(pg, ID_MIR_START), !running);
    EnableWindow(child(pg, ID_MIR_STOP), running);
    EnableWindow(child(pg, ID_MIR_SRC), !running);
    EnableWindow(child(pg, ID_MIR_FIT), !running);
    EnableWindow(child(pg, ID_MIR_REFRESH), !running);
    if (F.mirror) label(pg, ID_MIR_STATUS, F.mirror->status_text());
}

void refresh_mirror_sources() {
    HWND combo = child(page(PageDisplay), ID_MIR_SRC);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    F.sources = app::list_mirror_sources();
    for (const app::MirrorSource& s : F.sources) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.label.c_str()));
    if (!F.sources.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void mirror_start() {
    HWND pg = page(PageDisplay);
    if (!F.mirror) F.mirror = std::make_unique<app::Mirror>();
    if (F.mirror->running()) return;
    int i = static_cast<int>(SendMessageW(child(pg, ID_MIR_SRC), CB_GETCURSEL, 0, 0));
    if (i < 0 || i >= static_cast<int>(F.sources.size())) {
        label(pg, ID_MIR_STATUS, "Choose something to mirror first (press Refresh).");
        return;
    }
    if (!F.mirror_confirmed) {
        int r = MessageBoxW(S.hwnd,
                            L"Start mirroring to the TomTom?\n\n"
                            L"Read this first:\n"
                            L" - The TomTom has no start/stop command for this yet. The display receiver must already be running "
                            L"and the clock renderer stopped by hand, because both write the same screen memory.\n"
                            L" - The device's startup script restarts the renderer on its own, which can fight the receiver.\n"
                            L" - When you stop mirroring the TomTom does NOT bring its clock back by itself; restore it the way you "
                            L"normally restart the watchface.\n"
                            L" - Connect directly by USB. Pixels are sent unencrypted.\n\n"
                            L"Experimental: the receiver has not been verified on the physical TomTom.",
                            L"Mirror to TomTom", MB_OKCANCEL | MB_ICONWARNING);
        if (r != IDOK) return;
        F.mirror_confirmed = true;
    }
    int fit = static_cast<int>(SendMessageW(child(pg, ID_MIR_FIT), CB_GETCURSEL, 0, 0));
    std::string err;
    if (!F.mirror->start(F.sources[static_cast<size_t>(i)],
                         fit == 1 ? app::MirrorFit::Fill : fit == 2 ? app::MirrorFit::Stretch : app::MirrorFit::Letterbox, err))
        label(pg, ID_MIR_STATUS, err);
    mirror_update_ui();
}

void mirror_stop() {
    if (F.mirror) F.mirror->stop();
    mirror_update_ui();
}

// ---- notifications / webhook
void send_test_notification() {
    HWND pg = page(PageLive);
    std::string err;
    std::string text = get_text(child(pg, ID_NF_TEXT));
    remember_notify_host();
    if (send_to_tomtom(get_int(pg, ID_NF_TTL, 15), text, err))
        label(pg, ID_NF_STATUS, "Sent: \"" + tt::sanitize_notification_text(text) + "\"");
    else
        label(pg, ID_NF_STATUS, err);
}

void update_webhook_example() {
    HWND pg = page(PageLive);
    int port = get_int(pg, ID_WH_PORT, tt::kDefaultWebhookPort);
    set_text(child(pg, ID_WH_EXAMPLE),
             "curl -X POST http://127.0.0.1:" + std::to_string(port) + "/notify -H \"Authorization: Bearer " + F.token +
                 "\" -H \"Content-Type: application/json\" -d \"{\\\"text\\\":\\\"Hello\\\",\\\"ttl\\\":10}\"");
}

void webhook_apply(bool on) {
    HWND pg = page(PageLive);
    if (F.webhook.running()) F.webhook.stop();
    if (!on) {
        label(pg, ID_WH_STATUS, "Webhook is off.");
        return;
    }
    if (F.token.size() < tt::kMinWebhookTokenLength) {
        F.token = tt::generate_webhook_token();
        if (F.token.empty()) {
            Button_SetCheck(child(pg, ID_WH_ENABLE), BST_UNCHECKED);
            label(pg, ID_WH_STATUS, "Could not generate a secure random token.");
            return;
        }
    }
    set_text(child(pg, ID_WH_TOKEN), F.token);
    tt::WebhookServer::Config cfg;
    cfg.port = get_int(pg, ID_WH_PORT, tt::kDefaultWebhookPort);
    cfg.allow_lan = Button_GetCheck(child(pg, ID_WH_LAN)) == BST_CHECKED;
    cfg.token = F.token;
    remember_notify_host();
    std::string err;
    if (!F.webhook.start(cfg, [](int ttl, const std::string& text, std::string& e) { return send_to_tomtom(ttl, text, e); }, err)) {
        Button_SetCheck(child(pg, ID_WH_ENABLE), BST_UNCHECKED);
        label(pg, ID_WH_STATUS, err);
        return;
    }
    save_settings();
    update_webhook_example();
    label(pg, ID_WH_STATUS, std::string("Listening on ") + (cfg.allow_lan ? "all private network addresses" : "127.0.0.1 only") + ", port " +
                                std::to_string(cfg.port) + ".");
}

// ---- Spotify now playing / lyrics
void start_lyrics_fetch() {
    F.lyrics_state = "loading";
    F.lines.clear();
    F.shown_line = -2;
    F.shown_chunk = -1;
    label(page(PageLive), ID_LY_STATUS, "Looking up synced lyrics...");
    std::string artist = F.artist, title = F.title, key = F.track_key;
    HWND hwnd = S.hwnd;
    std::thread([hwnd, artist, title, key] {
        LyricsReply* r = new LyricsReply();
        r->key = key;
        r->ok = app::fetch_synced_lyrics(artist, title, r->lrc, r->error);
        if (!PostMessageW(hwnd, WM_LYRICS_DONE, 0, reinterpret_cast<LPARAM>(r))) delete r;
    }).detach();
}

void on_lyrics_done(LyricsReply* r) {
    std::unique_ptr<LyricsReply> reply(r);
    if (reply->key != F.track_key || !F.lyrics_on) return;  // stale answer
    HWND pg = page(PageLive);
    if (!reply->ok) {
        F.lyrics_state = reply->error;
        label(pg, ID_LY_STATUS, reply->error);
        return;
    }
    F.lines = tt::parse_lrc(reply->lrc);
    if (F.lines.empty()) {
        F.lyrics_state = "The lyrics could not be read.";
        label(pg, ID_LY_STATUS, F.lyrics_state);
        return;
    }
    F.lyrics_state = "ready";
    label(pg, ID_LY_STATUS, "Lyrics ready (" + std::to_string(F.lines.size()) + " lines). Sync is approximate - nudge it with the buttons.");
}

void update_offset_label() {
    char b[48];
    std::snprintf(b, sizeof b, "Offset %+.2f s", static_cast<double>(F.offset_ms) / 1000.0);
    label(page(PageLive), ID_LY_OFFSET, b);
}

void poll_spotify(ULONGLONG now) {
    HWND pg = page(PageLive);
    app::NowPlaying np = app::read_spotify_now_playing();
    if (np.playing) {
        std::string key = np.artist + "\n" + np.title;
        if (key != F.track_key) {
            F.track_key = key;
            F.artist = np.artist;
            F.title = np.title;
            F.pos_base_ms = 0;
            F.resume_tick = now;
            F.track_playing = true;
            F.lines.clear();
            F.shown_line = -2;
            F.shown_chunk = -1;
            F.lyrics_state.clear();
            if (F.announce) {
                std::string err;
                send_to_tomtom(10, np.artist + " - " + np.title, err);
            }
            if (F.lyrics_on) start_lyrics_fetch();
        } else if (!F.track_playing) {
            F.track_playing = true;
            F.resume_tick = now;
        }
        label(pg, ID_SP_NOW, "Now playing: " + np.artist + " - " + np.title);
    } else {
        if (F.track_playing) {
            F.pos_base_ms += static_cast<long long>(now - F.resume_tick);
            F.track_playing = false;
        }
        label(pg, ID_SP_NOW, np.running ? "Spotify is open - nothing playing." : "Spotify is not running.");
    }
}

void lyrics_tick(ULONGLONG now) {
    if (F.lyrics_state != "ready" || !F.track_playing || F.lines.empty()) return;
    long long pos = F.pos_base_ms + static_cast<long long>(now - F.resume_tick) + F.offset_ms;
    int idx = tt::find_lyric_line(F.lines, pos);
    if (idx < 0) return;
    const tt::LyricLine& line = F.lines[static_cast<size_t>(idx)];
    long long next_start = static_cast<size_t>(idx) + 1 < F.lines.size() ? F.lines[static_cast<size_t>(idx) + 1].start_ms : line.start_ms + 6000;
    long long dur = std::max<long long>(1500, std::min<long long>(12000, next_start - line.start_ms));
    std::vector<std::string> chunks = tt::split_for_display(line.text);
    if (chunks.empty()) {  // instrumental gap
        F.shown_line = idx;
        F.shown_chunk = -1;
        return;
    }
    long long n = static_cast<long long>(chunks.size());
    int chunk = static_cast<int>(std::max<long long>(0, std::min<long long>(n - 1, (pos - line.start_ms) * n / dur)));
    if (idx == F.shown_line && chunk == F.shown_chunk) return;
    if (now - F.last_send_tick < 700) return;  // stay gentle on the device
    int ttl = static_cast<int>(std::max<long long>(2, std::min<long long>(6, dur / n / 1000 + 1)));
    std::string err;
    if (send_to_tomtom(ttl, chunks[static_cast<size_t>(chunk)], err)) {
        F.shown_line = idx;
        F.shown_chunk = chunk;
        F.last_send_tick = now;
    }
}

// ---- extended display (virtual Windows monitor via the IDD driver's control tool)
void ext_update_ui() {
    HWND pg = page(PageDisplay);
    app::ExtSnapshot e = F.ext.snapshot();
    std::string text;
    if (!e.tool_found) {
        text = "The display driver tool (TomTomDisplayControl.exe) was not found next to this program, so the extended display is unavailable.";
    } else if (e.disable_pending) {
        text = "Removing the extended display...";
    } else if (e.enable_host_alive && !(e.status_valid && e.present)) {
        text = "Adding the extended display... waiting for Windows and the driver (up to about 20 s).";
    } else if (e.status_valid && e.present) {
        text = e.running ? "Extended display is ON and streaming to the TomTom. Frames acknowledged: " + std::to_string(e.frames)
                         : "Extended display is ON but not streaming: paused, mirroring is active, or the new monitor is not set to "
                           "Extend in Windows display settings.";
    } else if (e.status_valid) {
        text = "No extended display yet. Press Add extended display.";
    } else if (!e.last_error.empty()) {
        text = e.last_error;
    } else {
        text = "Checking...";
    }
    if (!F.ext_message.empty()) text += "\n" + F.ext_message;
    label(pg, ID_EXT_STATUS, text);
    bool present = e.status_valid && e.present;
    EnableWindow(child(pg, ID_EXT_ADD), e.tool_found && !present && !e.enable_host_alive && !e.disable_pending);
    EnableWindow(child(pg, ID_EXT_REMOVE), e.tool_found && (present || e.enable_host_alive) && !e.disable_pending);
    EnableWindow(child(pg, ID_EXT_PAUSE), e.tool_found && present);
    EnableWindow(child(pg, ID_EXT_RESUME), e.tool_found && present);
}

void ext_add() {
    std::string err;
    F.ext_message.clear();
    if (!F.ext.start_enable(err)) F.ext_message = err;
    F.last_ext_poll = 0;
    ext_update_ui();
}

void ext_remove() {
    std::string err;
    F.ext_message.clear();
    if (!F.ext.start_disable(err)) F.ext_message = err;
    F.last_ext_poll = 0;
    ext_update_ui();
}

void ext_pause(bool pause) {
    F.ext_message = pause ? "Pausing..." : "Resuming...";
    F.ext.request_pause(pause);
    F.last_ext_poll = 0;
}

void live_tick() {
    HWND live = page(PageLive);
    if (IsWindowVisible(page(PageDisplay))) {
        ULONGLONG t = GetTickCount64();
        if (t - F.last_ext_poll >= 1500) {
            F.last_ext_poll = t;
            F.ext.request_status();
        }
        app::ExtSnapshot e = F.ext.snapshot();
        if (e.action_seq != F.ext_action_seen) {  // a pause/resume finished
            F.ext_action_seen = e.action_seq;
            F.ext_message = e.action_exit == 0 ? "" : e.action_error;
        } else if (e.last_command == "enable" && !e.enable_host_alive && e.last_exit > 0) {
            F.ext_message = e.last_error;  // the elevated enable host stopped early
        } else if (e.last_command == "disable" && !e.disable_pending && e.last_exit > 0) {
            F.ext_message = e.last_error;
        }
        ext_update_ui();
    }
    if (F.mirror && F.mirror->running()) mirror_update_ui();
    else if (F.mirror && IsWindowVisible(page(PageDisplay))) mirror_update_ui();
    if (F.webhook.running()) {
        tt::WebhookServer::Status st = F.webhook.status();
        label(live, ID_WH_STATUS, "Running. Delivered " + std::to_string(st.delivered) + ", rejected " + std::to_string(st.rejected) +
                                      (st.last.empty() ? "" : " - last: " + st.last));
    }
    if (!F.announce && !F.lyrics_on) return;
    ULONGLONG now = GetTickCount64();
    if (now - F.last_poll_tick >= 1000) {
        F.last_poll_tick = now;
        poll_spotify(now);
    }
    if (F.lyrics_on) lyrics_tick(now);
}

void shutdown_features() {
    if (F.mirror) F.mirror->stop();
    F.ext.release_handles();  // the elevated enable host (if any) keeps the monitor alive; closing the window must not remove it silently
    F.webhook.stop();
}

void create_display_page(HWND p) {
    mk_label(p, L"Mirror a screen or a single window onto the TomTom's 320x240 display. This is a mirror, not a Windows "
                L"extended monitor (that needs the separate driver in windows-idd/).", 8, 8, 364, 52);
    mk_label(p, L"Source:", 8, 70, 60);
    mk_combo(p, 70, 66, 224, ID_MIR_SRC);
    mk_button(p, L"Refresh", 300, 65, 72, 24, ID_MIR_REFRESH);
    mk_label(p, L"Scaling:", 8, 102, 60);
    HWND fit = mk_combo(p, 70, 98, 224, ID_MIR_FIT);
    for (const char* f : {"Fit (keep proportions, black bars)", "Fill (crop to the screen shape)", "Stretch"}) add_item(fit, f);
    SendMessageW(fit, CB_SETCURSEL, 0, 0);
    mk_button(p, L"Start Mirroring", 8, 134, 176, 28, ID_MIR_START);
    mk_button(p, L"Stop", 196, 134, 176, 28, ID_MIR_STOP);
    EnableWindow(child(p, ID_MIR_STOP), FALSE);
    mk_label(p, L"Not running.", 8, 172, 364, 54, ID_MIR_STATUS);
    mk_label(p, L"BLOCKED ON THE DEVICE SIDE: the TomTom has no display start/stop command yet. The display receiver "
                L"(tomtom-display-receiver) must be started by hand with the clock renderer stopped (both write the same screen "
                L"memory), the device's startup script may restart the renderer underneath it, and the clock does not come back "
                L"when you stop. Experimental, not yet verified on the physical TomTom. Frames go only to 192.168.101.115:18745 "
                L"over USB, about 9 per second, unencrypted. Nothing is recorded. Windows that block capture appear black. "
                L"If the extended-display driver is installed it steps aside while you mirror.",
             8, 232, 364, 134, ID_MIR_WARN);
    mk(p, L"BUTTON", L"Extended display (a real Windows monitor)", BS_GROUPBOX, 8, 372, 364, 206, -1);
    mk_label(p, L"", 18, 392, 346, 52, ID_EXT_STATUS);
    mk_button(p, L"Add extended display", 18, 448, 170, 26, ID_EXT_ADD);
    mk_button(p, L"Remove extended display", 194, 448, 170, 26, ID_EXT_REMOVE);
    mk_button(p, L"Pause streaming", 18, 478, 170, 26, ID_EXT_PAUSE);
    mk_button(p, L"Resume streaming", 194, 478, 170, 26, ID_EXT_RESUME);
    mk_label(p, L"Needs the TomTom display driver (Windows test-signing mode, see windows-idd/README.md) and "
                L"TomTomDisplayControl.exe next to this program. Adding or removing asks for administrator permission. "
                L"While you mirror above, the driver steps aside.", 18, 510, 346, 64, ID_EXT_NOTE);
    refresh_mirror_sources();
}

void create_live_page(HWND p) {
    mk(p, L"BUTTON", L"Notification", BS_GROUPBOX, 8, 2, 364, 90, -1);
    mk_label(p, L"Message:", 18, 24, 60);
    mk_edit(p, 82, 20, 176, ID_NF_TEXT, L"Hello from Face Studio");
    mk_label(p, L"Secs:", 266, 24, 34);
    mk_spin(p, 302, 20, 62, ID_NF_TTL, ID_NF_TTL_SPIN, 1, 60, 15);
    mk_button(p, L"Send to TomTom", 18, 48, 130, 24, ID_NF_SEND);
    mk_label(p, L"Max 32 plain characters; accents are simplified.", 156, 52, 210, 32, ID_NF_STATUS);

    mk(p, L"BUTTON", L"Webhook (other programs can send notifications)", BS_GROUPBOX, 8, 98, 364, 156, -1);
    mk_check(p, L"Enable", 18, 118, 66, ID_WH_ENABLE);
    mk_label(p, L"Port:", 92, 120, 34);
    mk_spin(p, 126, 116, 64, ID_WH_PORT, ID_WH_PORT_SPIN, 1024, 65535, tt::kDefaultWebhookPort);
    mk_check(p, L"Allow private LAN", 214, 118, 150, ID_WH_LAN);
    mk_label(p, L"Token:", 18, 148, 44);
    mk(p, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP, 64, 144, 214, 22, ID_WH_TOKEN, WS_EX_CLIENTEDGE);
    mk_button(p, L"New token", 284, 143, 80, 24, ID_WH_REGEN);
    mk(p, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 18, 172, 346, 44, ID_WH_EXAMPLE, WS_EX_CLIENTEDGE);
    mk_label(p, L"Webhook is off.", 18, 220, 346, 32, ID_WH_STATUS);

    mk(p, L"BUTTON", L"Spotify (desktop app)", BS_GROUPBOX, 8, 260, 364, 214, -1);
    mk_label(p, L"Spotify is not running.", 18, 280, 346, 18, ID_SP_NOW);
    mk_check(p, L"Show track changes on the TomTom", 18, 302, 340, ID_SP_ANNOUNCE);
    mk_check(p, L"Show synced lyrics (looks up artist + title at lrclib.net)", 18, 326, 346, ID_LY_ENABLE);
    mk_button(p, L"-0.25 s", 18, 352, 70, 24, ID_LY_SLOWER);
    mk_button(p, L"+0.25 s", 92, 352, 70, 24, ID_LY_FASTER);
    mk_button(p, L"Restart lyrics", 168, 352, 100, 24, ID_LY_RESTART);
    mk_label(p, L"Offset +0.00 s", 276, 356, 92, 18, ID_LY_OFFSET);
    mk_label(p, L"Lyrics off.", 18, 384, 346, 36, ID_LY_STATUS);
    mk_label(p, L"Track and lyric lines reach the TomTom as short notifications over USB. Position is estimated from when the track "
                L"started, so seeking drifts - press Restart lyrics. Lyrics are shown line by line and never saved.", 18, 422, 346, 48);
    set_text(child(p, ID_WH_EXAMPLE), "");
}

void create_ui(HWND h) {
    S.hwnd = h;
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open Project...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save Project\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVEAS, L"Save Project &As...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
    AppendMenuW(view, MF_STRING, IDM_DESIGNER, L"&Face designer (canvas editor)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    SetMenu(h, bar);

    S.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, reinterpret_cast<HMENU>(ID_STATUSBAR), S.inst, nullptr);
    SendMessageW(S.status, WM_SETFONT, reinterpret_cast<WPARAM>(S.font), TRUE);
    set_status_text("Ready.");

    create_left_panel(h);
    S.canvas_wnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"TTCanvas", L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 10, 10, h,
                                   nullptr, S.inst, nullptr);
    S.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_MULTILINE, 0, 0, 10, 10, h,
                            reinterpret_cast<HMENU>(ID_TAB), S.inst, nullptr);
    SendMessageW(S.tab, WM_SETFONT, reinterpret_cast<WPARAM>(S.font), TRUE);
    for (int i = 0; i < PageCount; ++i)
        S.pages[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"TTPage", L"", WS_CHILD, 0, 0, 10, 10, h, nullptr, S.inst, nullptr);
    create_elements_page(S.pages[PageElements]);
    create_sim_page(S.pages[PageSim]);
    create_data_page(S.pages[PageData]);
    create_export_page(S.pages[PageExport]);
    create_device_page(S.pages[PageDevice]);
    create_display_page(S.pages[PageDisplay]);
    create_live_page(S.pages[PageLive]);
    load_settings();
    S.project = default_project();
    refresh_all_ui();
    set_designer_mode(S.designer, true);
    update_scrollbars();
    SetTimer(h, 1, 1000, nullptr);
    SetTimer(h, 2, 300, nullptr);
}

void draw_color_button(const DRAWITEMSTRUCT* dis, Rgb color, bool selected) {
    HBRUSH b = CreateSolidBrush(to_colorref(color));
    RECT r = dis->rcItem;
    FillRect(dis->hDC, &r, b);
    DeleteObject(b);
    HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, (dis->itemState & ODS_FOCUS) ? RGB(0, 0, 0) : RGB(96, 96, 96));
    HGDIOBJ op = SelectObject(dis->hDC, pen), ob = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(dis->hDC, ob);
    SelectObject(dis->hDC, op);
    DeleteObject(pen);
}

void handle_command(int id, int code, HWND src) {
    switch (id) {
        case ID_TOOL_PENCIL: S.tool = Tool::Pencil; break;
        case ID_TOOL_ERASER: S.tool = Tool::Eraser; break;
        case ID_TOOL_BUCKET: S.tool = Tool::Bucket; break;
        case ID_TOOL_PICKER: S.tool = Tool::Picker; break;
        case ID_BRUSH:
            if (code == EN_CHANGE) S.brush = std::max(1, std::min(8, get_int(S.hwnd, ID_BRUSH, S.brush)));
            break;
        case ID_COLOR_BTN: { std::string c = S.color; if (choose_color(c)) set_active_color(c); break; }
        case ID_UNDO:
        case IDM_UNDO:
            if (GetFocus() && GetParent(GetFocus()) != S.hwnd && GetParent(GetFocus()) != nullptr) {
                wchar_t cls[16];
                GetClassNameW(GetFocus(), cls, 16);
                if (wcscmp(cls, L"Edit") == 0) { SendMessageW(GetFocus(), WM_UNDO, 0, 0); break; }
            }
            if (S.cvs.undo()) { mark_dirty(); redraw_canvas(); }
            break;
        case ID_REDO:
        case IDM_REDO:
            if (S.cvs.redo()) { mark_dirty(); redraw_canvas(); }
            break;
        case ID_CLEAR:
            S.cvs.clear("#000000");
            mark_dirty();
            redraw_canvas();
            break;
        case ID_IMPORT: do_import_artwork(); break;
        case ID_ZOOM:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(src, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < 4) S.zoom = i + 1;
                update_scrollbars();
                redraw_canvas();
            }
            break;
        case ID_GRID:
            S.grid = Button_GetCheck(src) == BST_CHECKED;
            redraw_canvas();
            break;
        case ID_EL_LIST:
            if (code == LBN_SELCHANGE) {
                S.sel = static_cast<int>(SendMessageW(src, LB_GETCURSEL, 0, 0));
                show_element_props();
                redraw_canvas();
            }
            break;
        case ID_EL_ADD_TIME: add_element("digital_time"); break;
        case ID_EL_ADD_DATE: add_element("date"); break;
        case ID_EL_ADD_TEXT: add_element("text"); break;
        case ID_EL_ADD_ICON: add_element("status_icon"); break;
        case ID_EL_DELETE: delete_element(); break;
        case ID_EL_ID: case ID_EL_X: case ID_EL_Y: case ID_EL_W: case ID_EL_H: case ID_EL_FONT: case ID_EL_TEXT:
            if (code == EN_CHANGE) apply_element_props();
            break;
        case ID_EL_RULE: case ID_EL_ICON:
            if (code == CBN_SELCHANGE) apply_element_props();
            break;
        case ID_EL_COLOR:
            if (Json* el = selected_element()) {
                std::string c = el->get("color").is_string() ? el->get("color").as_string() : "#FFFFFF";
                if (choose_color(c)) {
                    el->set("color", Json::string(c));
                    InvalidateRect(src, nullptr, TRUE);
                    mark_dirty();
                    redraw_canvas();
                }
            }
            break;
        case ID_SIM_GPS: S.sim_gps = Button_GetCheck(src) == BST_CHECKED; redraw_canvas(); break;
        case ID_SIM_WEATHER: S.sim_weather_off = Button_GetCheck(src) == BST_CHECKED; redraw_canvas(); break;
        case ID_SIM_CHARGING: S.sim_charging = Button_GetCheck(src) == BST_CHECKED; redraw_canvas(); break;
        case ID_SIM_LIVE: {
            S.live_clock = Button_GetCheck(src) == BST_CHECKED;
            EnableWindow(child(page(PageSim), ID_SIM_HOUR), !S.live_clock);
            EnableWindow(child(page(PageSim), ID_SIM_MIN), !S.live_clock);
            redraw_canvas();
            break;
        }
        case ID_SIM_HOUR: case ID_SIM_MIN:
            if (code == EN_CHANGE && !S.live_clock) {
                S.sim_time.hour = std::max(0, std::min(23, get_int(page(PageSim), ID_SIM_HOUR, S.sim_time.hour)));
                S.sim_time.minute = std::max(0, std::min(59, get_int(page(PageSim), ID_SIM_MIN, S.sim_time.minute)));
                redraw_canvas();
            }
            break;
        case ID_META_NAME: case ID_META_AUTHOR: case ID_META_VER:
            if (code == EN_CHANGE && !S.updating) mark_dirty();
            break;
        case ID_FMT0: case ID_FMT1:
            if (!S.updating) {
                const Json& d = S.project.get("display");
                refresh_default_format_choices(d.is_object() && d.get("default_format").is_string() ? d.get("default_format").as_string() : "", true);
                mark_dirty();
                redraw_canvas();
            }
            break;
        case ID_FMT_DEFAULT:
            if (code == CBN_SELCHANGE && !S.updating) {
                int i = static_cast<int>(SendMessageW(src, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < static_cast<int>(g_default_format_ids.size())) {
                    refresh_default_format_choices(g_default_format_ids[static_cast<size_t>(i)], true);
                    mark_dirty();
                    redraw_canvas();
                }
            }
            break;
        case ID_VALIDATE: do_validate(); break;
        case ID_EXPORT: do_export_face(); break;
        case ID_GALLERY: do_export_gallery(); break;
        case ID_INSPECT_FACE: do_inspect_face(); break;
        case ID_INSPECT_GAL: do_inspect_gallery(); break;
        case ID_DEV_PING: start_device(0); break;
        case ID_DEV_READ: start_device(1); break;
        case ID_DEV_APPLY: start_device(2); break;
        case ID_DEV_HOST:
            if (code == EN_CHANGE) remember_notify_host();
            break;
        case ID_MIR_REFRESH: refresh_mirror_sources(); break;
        case ID_MIR_START: mirror_start(); break;
        case ID_MIR_STOP: mirror_stop(); break;
        case ID_EXT_ADD: ext_add(); break;
        case ID_EXT_REMOVE: ext_remove(); break;
        case ID_EXT_PAUSE: ext_pause(true); break;
        case ID_EXT_RESUME: ext_pause(false); break;
        case ID_NF_SEND: send_test_notification(); break;
        case ID_WH_ENABLE: webhook_apply(Button_GetCheck(src) == BST_CHECKED); break;
        case ID_WH_REGEN:
            F.token = tt::generate_webhook_token();
            set_text(child(page(PageLive), ID_WH_TOKEN), F.token);
            if (F.webhook.running()) webhook_apply(true);
            else { save_settings(); update_webhook_example(); }
            break;
        case ID_SP_ANNOUNCE:
            F.announce = Button_GetCheck(src) == BST_CHECKED;
            if (!F.announce && !F.lyrics_on) label(page(PageLive), ID_SP_NOW, "Spotify is not being watched.");
            break;
        case ID_LY_ENABLE:
            F.lyrics_on = Button_GetCheck(src) == BST_CHECKED;
            if (F.lyrics_on) {
                label(page(PageLive), ID_LY_STATUS, "Waiting for a track...");
                if (!F.track_key.empty()) start_lyrics_fetch();
            } else {
                F.lines.clear();
                F.lyrics_state.clear();
                label(page(PageLive), ID_LY_STATUS, "Lyrics off.");
            }
            break;
        case ID_LY_SLOWER: F.offset_ms -= 250; update_offset_label(); break;
        case ID_LY_FASTER: F.offset_ms += 250; update_offset_label(); break;
        case ID_LY_RESTART:
            F.pos_base_ms = 0;
            F.resume_tick = GetTickCount64();
            F.shown_line = -2;
            F.shown_chunk = -1;
            break;
        case IDM_OPEN: do_open(); break;
        case IDM_SAVE: do_save(false); break;
        case IDM_SAVEAS: do_save(true); break;
        case IDM_EXIT: SendMessageW(S.hwnd, WM_CLOSE, 0, 0); break;
        case IDM_DESIGNER:
            set_designer_mode(!S.designer, true);
            save_settings();
            break;
        case IDM_ABOUT:
            message("About TomTom Face Studio",
                    "TomTom Face Studio (native Win32 build)\nTarget: TomTom ONE v6 (Model 19), 320x240, Nano-X\n\n"
                    "Packages: .ttface, .ttgallery, .ttproj (identical to the Python Face Studio).\n"
                    "Device control: fixed-command TCP service on port 18743 over a direct USB link only - "
                    "plain text, unauthenticated.");
            break;
        default:
            if (id >= ID_SWATCH0 && id < ID_SWATCH0 + 8) set_active_color(U(kSwatches[id - ID_SWATCH0]));
            else if (id >= ID_DATA0 && id < ID_DATA0 + 8) { sync_data_requirements(); mark_dirty(); }
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: create_ui(hwnd); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S.designer ? D(1040) : D(500);
            mm->ptMinTrackSize.y = D(690);
            return 0;
        }
        case WM_TIMER: {
            if (wp == 2) { live_tick(); return 0; }
            ++S.tick;
            if (S.live_clock) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                S.sim_time.year = st.wYear; S.sim_time.month = st.wMonth; S.sim_time.day = st.wDay;
                S.sim_time.hour = st.wHour; S.sim_time.minute = st.wMinute; S.sim_time.second = st.wSecond;
            }
            if (!IsIconic(hwnd)) redraw_canvas();
            return 0;
        }
        case WM_COMMAND: handle_command(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp)); return 0;
        case WM_NOTIFY: {
            NMHDR* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == ID_TAB && nm->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(S.tab);
                if (sel >= 0 && sel < static_cast<int>(S.tab_pages.size())) show_page(S.tab_pages[static_cast<size_t>(sel)]);
            }
            return 0;
        }
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) == child(page(PageSim), ID_SIM_BAT)) {
                S.sim_battery = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp), TBM_GETPOS, 0, 0));
                set_text(child(page(PageSim), ID_SIM_BAT_LBL), std::to_string(S.sim_battery) + "%");
                redraw_canvas();
            }
            return 0;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis->CtlID == ID_COLOR_BTN) draw_color_button(dis, hex_to_rgb(S.color, Rgb{0, 0, 0}), true);
            else if (dis->CtlID >= ID_SWATCH0 && dis->CtlID < ID_SWATCH0 + 8)
                draw_color_button(dis, hex_to_rgb(U(kSwatches[dis->CtlID - ID_SWATCH0]), Rgb{0, 0, 0}), false);
            else if (dis->CtlID == ID_EL_COLOR) {
                Json* el = selected_element();
                draw_color_button(dis, hex_to_rgb(el && el->get("color").is_string() ? el->get("color").as_string() : "#FFFFFF", Rgb{255, 255, 255}), false);
            }
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HWND ctl = reinterpret_cast<HWND>(lp);
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            if (GetParent(ctl) == hwnd) {
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            int id = GetDlgCtrlID(ctl);
            if (id == ID_DEV_STATUS) SetTextColor(dc, S.dev_state == 1 ? RGB(0x1B, 0x7A, 0x2B) : S.dev_state == 2 ? RGB(0xC6, 0x28, 0x28) : GetSysColor(COLOR_WINDOWTEXT));
            else if (id == ID_DEV_WARN) SetTextColor(dc, RGB(0xB4, 0x53, 0x09));
            else if (id == ID_SIM_BAT_LBL || id == -1) SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_DEVICE_DONE: on_device_done(reinterpret_cast<DeviceReply*>(lp)); return 0;
        case WM_LYRICS_DONE: on_lyrics_done(reinterpret_cast<LyricsReply*>(lp)); return 0;
        case WM_CLOSE:
            if (confirm_discard()) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            shutdown_features();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    S.inst = inst;
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HDC screen = GetDC(nullptr);
    S.dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    S.font = CreateFontIndirectW(&ncm.lfMessageFont);

    SYSTEMTIME st;
    GetLocalTime(&st);
    S.sim_time.year = st.wYear; S.sim_time.month = st.wMonth; S.sim_time.day = st.wDay;
    S.sim_time.hour = st.wHour; S.sim_time.minute = st.wMinute; S.sim_time.second = st.wSecond;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"TTFaceStudioMain";
    wc.lpfnWndProc = MainProc;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) return 1;
    wc.lpszClassName = L"TTCanvas";
    wc.lpfnWndProc = CanvasProc;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&wc)) return 1;
    wc.lpszClassName = L"TTPage";
    wc.lpfnWndProc = PageProc;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"TTFaceStudioMain", L"TomTom Face Studio", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, D(1180), D(770), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    set_title();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    ACCEL accel[] = {{FCONTROL | FVIRTKEY, 'Z', IDM_UNDO}, {FCONTROL | FVIRTKEY, 'Y', IDM_REDO},
                     {FCONTROL | FVIRTKEY, 'O', IDM_OPEN}, {FCONTROL | FVIRTKEY, 'S', IDM_SAVE}};
    HACCEL table = CreateAcceleratorTableW(accel, 4);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, table, &m)) continue;
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyAcceleratorTable(table);
    CoUninitialize();
    return static_cast<int>(m.wParam);
}
