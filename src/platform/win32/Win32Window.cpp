// Clean-room reconstruction — Win32 window + OpenGL context + input polling.
// This is the ONLY gameplay-facing platform code that touches Win32.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <GL/gl.h>
#include <xinput.h>

#include "platform/Window.h"
#include "platform/PresentHook.h"
#include "platform/DisplayMode.h"
#include "core/Log.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <algorithm>
#include <string>
#include <vector>

namespace platform {

// [Systems] The desktop mode saved before the fullscreen mode switch (platform/DisplayMode.h): one per process, so the crash
// handler and atexit can put it back too.
DesktopModeKeeper& desktopModeKeeper() { static DesktopModeKeeper k; return k; }
void restoreDesktopDisplayMode() { desktopModeKeeper().restore(&ChangeDisplaySettingsExW); }
const wchar_t* displayRestoreFilePath() {
    static wchar_t path[MAX_PATH] = {};
    if (!path[0]) {
        const DWORD n = GetCurrentDirectoryW(MAX_PATH - 32, path);
        if (n == 0 || n >= MAX_PATH - 32) path[0] = 0;
        else std::wcscat(path, L"\\wfc_display_restore.txt");
    }
    return path;
}

namespace {

// WFC_NOINTERACT=1 (automated runs; env only, never a package default) [PC ADAPTATION, tooling]: the run never takes the user's
// focus, cursor or input. The window is shown without activation (WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE, no SetForegroundWindow /
// SetFocus, bottom of the z-order, never topmost) in a corner out of the way, the cursor is never hidden or moved (mouse-look
// recentring off), no capture / clip, fullscreen requests stay windowed, and keys, buttons, wheel, text and the real pad are
// ignored at the source (every pump: the frontend, loading and match loops); scripted inputs (injectPad, the frontend script,
// the WFC_AUTO* / player-bot hooks) still apply. WFC_NOINTERACTTEST=1 logs a self-check 5 s and 30 s after the window opens.
bool noInteract() { static const bool v = std::getenv("WFC_NOINTERACT") != nullptr; return v; }

// NOINTERACT placement of an outer window of ww x wh: WFC_WINPOS=x,y, else the bottom-right corner of a secondary monitor's
// work area when one is attached, else of the primary's (kept fully on screen: off-screen pixels of a GL window are undefined).
POINT noInteractPos(int ww, int wh) {
    if (const char* s = std::getenv("WFC_WINPOS")) {
        int x = 0, y = 0;
        if (std::sscanf(s, "%d,%d", &x, &y) == 2) return POINT{x, y};
        LOG_WARN("window: WFC_WINPOS '%s' is not x,y; using the corner placement", s);
    }
    struct Pick { HMONITOR primary = nullptr, other = nullptr; } pick;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* pk = reinterpret_cast<Pick*>(lp);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi)) { if (mi.dwFlags & MONITORINFOF_PRIMARY) pk->primary = m; else if (!pk->other) pk->other = m; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&pick));
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    const HMONITOR m = pick.other ? pick.other : pick.primary;
    if (!m || !GetMonitorInfoW(m, &mi)) return POINT{0, 0};
    return POINT{std::max(mi.rcWork.left, mi.rcWork.right - ww), std::max(mi.rcWork.top, mi.rcWork.bottom - wh)};
}

// Map a neutral Button to a Win32 virtual key.
int vkFor(Button b) {
    switch (b) {
        case Button::Forward:      return 'W';
        case Button::Back:         return 'S';
        case Button::Left:         return 'A';
        case Button::Right:        return 'D';
        case Button::Jump:         return VK_SPACE;
        case Button::Transform:    return 'F';
        case Button::Fire:         return VK_LBUTTON;
        case Button::Sprint:       return 0;        // no sprint in WFC (no binding)
        case Button::Reload:       return 'R';
        case Button::CameraToggle: return VK_F8;    // rebuild debug (C / B belong to WFC: Hover Up, Look At)
        case Button::Debug:        return VK_F11;   // rebuild debug wire boxes (was F9: it collided with the RT mode cycle + the bot overlay)
        case Button::Ascend:       return 'C';
        case Button::Descend:      return 'V';
        case Button::NextWeapon:   return VK_PRIOR;
        case Button::Ability1:     return VK_CONTROL;
        case Button::Killstreak:   return 'B';
        case Button::Melee:        return 'Q';
        case Button::Grenade:      return 'G';
        case Button::Interact:     return 'E';
        case Button::PrevWeapon:   return VK_NEXT;
        case Button::Quit:         return VK_ESCAPE;
        case Button::FineAim:      return VK_RBUTTON;
        case Button::DebugNextStart: return VK_F6;  // test spawn cycling (not a WFC binding)
        case Button::DebugPrevStart: return VK_F7;
        case Button::Dash:         return VK_SHIFT; // [CONF] Shift = "Ability0 | VehicleSpecialMove";
        case Button::RenderMode:   return VK_F9;    // PC ADAPTATION: the Ray Tracing mode cycle
                                                    // PlayerInCarForm.StartVehicleSpecialMove -> set_DashingInput
        default:                   return 0;
    }
}

class Win32Window final : public IWindow {
public:
    bool create(int w, int h, const char* title) {
        width_ = w; height_ = h;
        hinst_ = GetModuleHandleW(nullptr);
        enableDpiAwareness();
        {   // [Systems] a previous run killed in fullscreen (no restore ran): Windows reverted the display to the registry mode
            DEVMODEW m{};
            const int r = DesktopModeKeeper::restoreFromFileAtStartup(displayRestoreFilePath(), false, &ChangeDisplaySettingsExW,
                                                                      &EnumDisplaySettingsW, &m);
            if (r == 2) LOG_INFO("window: the last run ended in fullscreen without a restore: desktop mode %lux%lu @ %lu Hz put back",
                                 m.dmPelsWidth, m.dmPelsHeight, m.dmDisplayFrequency);
            else if (r == 3) LOG_WARN("window: the last run's desktop mode could not be put back (wfc_display_restore.txt kept)");
        }

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Win32Window::wndProcThunk;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WFCRebuildWindow";
        if (!RegisterClassExW(&wc)) { LOG_ERROR("RegisterClassExW failed"); return false; }

        RECT r = {0, 0, w, h};
        adjustFrame(r, systemDpi());

        wchar_t wtitle[256];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);

        const bool quiet = noInteract();
        POINT at{CW_USEDEFAULT, CW_USEDEFAULT};
        if (quiet) at = noInteractPos(r.right - r.left, r.bottom - r.top);
        // NOINTERACT: never activated by a click or at the first show; APPWINDOW keeps its taskbar button
        hwnd_ = CreateWindowExW(quiet ? WS_EX_NOACTIVATE | WS_EX_APPWINDOW : 0, wc.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW,
                                at.x, at.y, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, hinst_, this);
        if (!hwnd_) { LOG_ERROR("CreateWindowExW failed"); return false; }

        hdc_ = GetDC(hwnd_);
        if (!initGL()) return false;

        setUiBindings(UiBindings::defaults(false));
        createdAt_ = std::chrono::steady_clock::now();
        if (quiet) {
            focused_ = false;   // no window input is read (pump)
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            RECT wr; GetWindowRect(hwnd_, &wr);
            LOG_INFO("NOINTERACT: no activation / capture / clip (window at %ld,%ld, %ldx%ld, bottom of the z-order; cursor never "
                     "hidden or moved; keys, mouse, wheel, text and pad ignored; fullscreen stays windowed)",
                     wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
        } else {
            ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            SetFocus(hwnd_);
        }
        return true;
    }

    ~Win32Window() override {
        restoreDesktopMode();
        if (hglrc_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(hglrc_); }
        if (hdc_ && hwnd_) ReleaseDC(hwnd_, hdc_);
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool pump(InputFrame& input) override {
        if (fullscreen_ && modeChanged_ && !desktopModeKeeper().active()) {
            // [Systems] the desktop mode was put back from another thread (the watchdog, during a long stall): frames run
            // again, so take our mode back (debounced as a focus return)
            modeChanged_ = false;
            lastRestore_ = std::chrono::steady_clock::now();
            LOG_INFO("window: the desktop mode was restored during a stall; re-applying the fullscreen mode");
            if (GetForegroundWindow() == hwnd_) scheduleReapply();
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) wantClose_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (wantClose_) return false;
        noInteractSelfTest();

        // Poll keyboard/mouse buttons via async state (kept in platform layer).
        for (int i = 0; i < (int)Button::Count; ++i) {
            int vk = vkFor((Button)i);
            bool nowDown = vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
            // Melee is also the middle mouse button (shipped PC bindings: Q / MiddleMouseButton) [CONF Controls card].
            if ((Button)i == Button::Melee && (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) nowDown = true;
            if (!focused_) nowDown = false;
            input.pressed[i] = nowDown && !input.down[i];
            input.down[i] = nowDown;
        }

        // Mouse-look via cursor recentering while captured + focused.
        input.mouseDX = input.mouseDY = 0.0f;
        if (mouseCaptured_ && focused_) {
            POINT p;
            GetCursorPos(&p);
            POINT center = centerScreen();
            input.mouseDX = float(p.x - center.x);
            input.mouseDY = float(p.y - center.y);
            SetCursorPos(center.x, center.y);
            ++cursorMoves_;
        }

        pollGamepad(input);
        // Logical UI commands from the bindings (keyboard keys and pad buttons).
        uint32_t ui = 0;
        bool keyUi = false;
        if (focused_)
            for (int i = 0; i < (int)UiKey::Count; ++i) {
                for (int vk : uiVk_[i]) if (GetAsyncKeyState(vk) & 0x8000) { ui |= 1u << i; keyUi = true; }
                for (uint32_t m : uiPad_[i]) if (padBits_ & m) ui |= 1u << i;
            }
        input.uiDown = ui;
        input.padActive = padBits_ != 0 || std::fabs(input.padRX) > 0.5f || std::fabs(input.padRY) > 0.5f;
        input.keyActive = keyUi;
        for (int i = 0; i < (int)Button::Count && !input.keyActive; ++i) input.keyActive = input.down[i];
        // Absolute pointer for the UI.
        input.mouseX = input.mouseY = -1;
        input.mouseLeft = focused_ && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        input.mouseRight = focused_ && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
        if (!mouseCaptured_ && focused_) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            if (p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_) { input.mouseX = p.x; input.mouseY = p.y; }
        }
        if (noInteract()) {   // NOINTERACT: window messages that arrive unfocused (the wheel over an inactive window) are dropped
            wheel_ = 0; typed_.clear(); keyPresses_.clear(); focused_ = false;
        }
        input.mouseWheel = wheel_ / (float)WHEEL_DELTA;
        wheel_ = 0;
        input.text.swap(typed_);
        typed_.clear();
        input.keyPresses.swap(keyPresses_);
        keyPresses_.clear();
        return true;
    }

    // WFC_NOINTERACTTEST=1: 5 s and 30 s after the window opened, check that the run holds no capture, has no clip, is not the
    // foreground window, has not hidden or moved the cursor and is neither topmost nor activatable. Without WFC_NOINTERACT it
    // is a negative control (the foreground check fails when the run was started from the foreground).
    void noInteractSelfTest() {
        static const bool test = std::getenv("WFC_NOINTERACTTEST") != nullptr;
        if (!test || selfTests_ >= 2) return;
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - createdAt_).count();
        if (t < (selfTests_ == 0 ? 5.0 : 30.0)) return;
        ++selfTests_;
        RECT clip{}, screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        GetClipCursor(&clip);
        const LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        const HWND fg = GetForegroundWindow();
        struct Check { const char* name; bool ok; } checks[] = {
            {"GetCapture() == NULL", GetCapture() == nullptr},
            {"GetForegroundWindow() != ours", fg != hwnd_},
            {"GetFocus() != ours", GetFocus() != hwnd_},
            {"cursor clip rect == virtual screen", EqualRect(&clip, &screen) != FALSE},
            {"cursor never hidden (ShowCursor) or moved (SetCursorPos)", cursorHides_ == 0 && cursorMoves_ == 0},
            {"WS_EX_NOACTIVATE set, WS_EX_TOPMOST clear", (ex & WS_EX_NOACTIVATE) != 0 && (ex & WS_EX_TOPMOST) == 0},
        };
        int pass = 0;
        for (const Check& c : checks) {
            pass += c.ok ? 1 : 0;
            LOG_INFO("NOINTERACTTEST %s: %s", c.ok ? "PASS" : "FAIL", c.name);
        }
        LOG_INFO("NOINTERACTTEST SUMMARY: %d/%d checks passed at %.0f s (NOINTERACT %s; clip %ld,%ld-%ld,%ld, screen %ld,%ld-%ld,%ld)",
                 pass, (int)(sizeof(checks) / sizeof(checks[0])), t, noInteract() ? "on" : "OFF - negative control",
                 clip.left, clip.top, clip.right, clip.bottom, screen.left, screen.top, screen.right, screen.bottom);
    }

    void injectPad(const std::string& button, bool down) override {
        const uint32_t m = padByName(button);
        if (!m) { LOG_WARN("input: injectPad unknown button '%s'", button.c_str()); return; }
        fakePad_ = down ? (fakePad_ | m) : (fakePad_ & ~m);
    }
    void setUiBindings(const UiBindings& b) override {
        for (int i = 0; i < (int)UiKey::Count; ++i) {
            uiVk_[i].clear();
            uiPad_[i].clear();
            for (const std::string& n : b.keys[i]) {
                if (int vk = vkByName(n)) uiVk_[i].push_back(vk);
                else LOG_WARN("input: unknown key '%s' (UI.%s)", n.c_str(), UiBindings::actionName((UiKey)i));
            }
            for (const std::string& n : b.pad[i]) {
                if (uint32_t m = padByName(n)) uiPad_[i].push_back(m);
                else LOG_WARN("input: unknown pad button '%s' (UI.%s)", n.c_str(), UiBindings::actionName((UiKey)i));
            }
        }
    }

    void setOsCursorHidden(bool hidden) override { cursorHidden_ = hidden; }

    bool desktopMode(int& w, int& h, int& hz) const override {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) return false;
        // [Systems] the desktop as it runs: our saved desktop mode while our fullscreen mode is active, else the current
        // mode (the registry mode can differ - a 144 Hz desktop registered at 60 Hz - and auto-detect derives the frame
        // limit from this refresh); the registry only when the current mode cannot be read
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (desktopModeKeeper().active()) dm = desktopModeKeeper().saved();
        else if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && !EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm)) return false;
        w = (int)dm.dmPelsWidth; h = (int)dm.dmPelsHeight; hz = (int)dm.dmDisplayFrequency;
        return w > 0 && h > 0;
    }

    std::vector<Mode> displayModes() const override {
        std::vector<Mode> out;
        // [Systems] the modes of the monitor the window is on (multi-monitor: not the primary's)
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        const wchar_t* dev = GetMonitorInfoW(mon, &mi) ? mi.szDevice : nullptr;
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(dev, i, &dm); ++i) {
            if (dm.dmBitsPerPel < 32 || dm.dmPelsWidth < 800 || dm.dmPelsHeight < 600) continue;
            bool have = false;
            for (const Mode& m : out) have |= m.width == (int)dm.dmPelsWidth && m.height == (int)dm.dmPelsHeight;
            if (!have) out.push_back({(int)dm.dmPelsWidth, (int)dm.dmPelsHeight});
        }
        std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });
        return out;
    }

    // Fullscreen = the chosen resolution: the monitor is switched to w x h (CDS_FULLSCREEN: a temporary mode that Windows
    // also undoes when the process ends) and a borderless window covers it. Losing focus restores the desktop mode and
    // minimises; regaining focus switches back. If the mode switch fails, the window covers the monitor at its current
    // mode [PC: the shipped SKU ran fullscreen at the chosen resolution; its native mode handling is not in the dump].
    bool applyFullscreenMode() {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        monitorDevice_ = mi.szDevice;
        DEVMODEW cur{};
        cur.dmSize = sizeof(cur);
        EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &cur);
        bool ok = false;
        if (fsW_ > 0 && fsH_ > 0 && ((int)cur.dmPelsWidth != fsW_ || (int)cur.dmPelsHeight != fsH_ || modeChanged_)) {
            // The requested size at the desktop's refresh rate when the monitor offers it, else its highest rate. [Systems] the
            // desktop's rate is the saved one once a mode is saved: in a re-apply or a resolution change `cur` is our own mode,
            // possibly a 60 Hz fallback, which must not become the preference.
            const DWORD wantHz = desktopModeKeeper().hasSaved() ? desktopModeKeeper().saved().dmDisplayFrequency : cur.dmDisplayFrequency;
            DEVMODEW best{}, dm{};
            dm.dmSize = sizeof(dm);
            for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); ++i) {
                if ((int)dm.dmPelsWidth != fsW_ || (int)dm.dmPelsHeight != fsH_ || dm.dmBitsPerPel < 32) continue;
                bool better = best.dmSize == 0 || dm.dmDisplayFrequency == wantHz ||
                              (best.dmDisplayFrequency != wantHz && dm.dmDisplayFrequency > best.dmDisplayFrequency);
                if (better) best = dm;
            }
            if (best.dmSize) {
                best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
                // [Systems] the desktop's mode as it runs now (not the registry's), kept for the way back; not overwritten
                // while our mode is active (a re-apply or a resolution change in fullscreen sees our own mode in `cur`)
                desktopModeKeeper().setRestoreFile(displayRestoreFilePath());   // (written at the switch: kill-path repair)
                desktopModeKeeper().saveBeforeSwitch(mi.szDevice, cur);
                ok = ChangeDisplaySettingsExW(mi.szDevice, &best, nullptr, CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL;
                if (ok) {
                    desktopModeKeeper().switched();
                    static const bool atExit = std::atexit([] { restoreDesktopDisplayMode(); }) == 0;   // every normal exit path
                    (void)atExit;
                    modeChanged_ = true;
                    LOG_INFO("window: fullscreen %dx%d @ %lu Hz (desktop %lux%lu @ %lu Hz kept for the return)", fsW_, fsH_,
                             best.dmDisplayFrequency, cur.dmPelsWidth, cur.dmPelsHeight, cur.dmDisplayFrequency);
                }
            }
            if (!ok) LOG_WARN("window: display mode %dx%d not available; fullscreen at the desktop size", fsW_, fsH_);
        }
        GetMonitorInfoW(mon, &mi);   // the monitor rectangle in the new mode
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return ok;
    }
    void restoreDesktopMode() {
        // [Systems] exactly the mode the desktop ran before the switch (ChangeDisplaySettingsEx(nullptr) would restore the
        // registry mode, which can differ: a 144 Hz desktop could come back at 60 Hz); the registry mode only if refused
        if (!modeChanged_) return;
        const DEVMODEW want = desktopModeKeeper().saved();
        const LONG r = desktopModeKeeper().restore(&ChangeDisplaySettingsExW);
        modeChanged_ = false;
        lastRestore_ = std::chrono::steady_clock::now();
        DEVMODEW now{};
        now.dmSize = sizeof(now);
        EnumDisplaySettingsW(monitorDevice_.empty() ? nullptr : monitorDevice_.c_str(), ENUM_CURRENT_SETTINGS, &now);
        LOG_INFO("window: desktop mode back to %lux%lu @ %lu Hz (saved %lux%lu @ %lu Hz%s)", now.dmPelsWidth, now.dmPelsHeight,
                 now.dmDisplayFrequency, want.dmPelsWidth, want.dmPelsHeight, want.dmDisplayFrequency,
                 r == DISP_CHANGE_SUCCESSFUL ? "" : "; refused, registry mode restored instead");
    }

    // [Systems] coming back to fullscreen after a restore: never sooner than kReapplyDelayMs after it (a restore -> reapply
    // -> focus-loss loop, e.g. an overlay stealing focus at each mode change, would otherwise switch modes continuously),
    // and at most kReapplyMax re-applies per kReapplyWindowMs: beyond that the window stays borderless at the desktop mode
    // until the next fullscreen request.
    void scheduleReapply() {
        const auto now = std::chrono::steady_clock::now();
        const long long since = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRestore_).count();
        if (lastRestore_.time_since_epoch().count() != 0 && since < kReapplyDelayMs) {
            if (!reapplyPending_) SetTimer(hwnd_, kReapplyTimer, (UINT)(kReapplyDelayMs - since), nullptr);
            reapplyPending_ = true;
            return;
        }
        reapplyNow();
    }
    void reapplyNow() {
        const auto now = std::chrono::steady_clock::now();
        int recent = 0;
        for (const auto& t : reapplies_)
            if (t.time_since_epoch().count() != 0 && now - t < std::chrono::milliseconds(kReapplyWindowMs)) ++recent;
        if (recent >= kReapplyMax) {
            if (!reapplyGaveUp_) LOG_WARN("window: fullscreen mode re-applied %d times in %d s; staying at the desktop mode", recent, kReapplyWindowMs / 1000);
            reapplyGaveUp_ = true;
            applyBorderlessAtDesktop();
            return;
        }
        reapplies_[reapplyNext_] = now; reapplyNext_ = (reapplyNext_ + 1) % kReapplyMax;
        applyFullscreenMode();
    }
    void applyBorderlessAtDesktop() {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    void setDisplayMode(int w, int h, bool full) override {
        // Windowed: a client area of w x h. Fullscreen: the monitor at w x h (applyFullscreenMode).
        if (full && noInteract()) {
            LOG_INFO("NOINTERACT: fullscreen %dx%d requested; staying windowed at that client size", w, h);
            full = false;
        }
        if (full) {
            reapplyGaveUp_ = false;                          // (an explicit request: the loop guard starts afresh)
            if (!fullscreen_) { GetWindowRect(hwnd_, &windowedRect_); }
            fsW_ = w; fsH_ = h;
            fullscreen_ = true;
            applyFullscreenMode();
            return;
        }
        restoreDesktopMode();
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        RECT r = {0, 0, w, h};
        adjustFrame(r, windowDpi());
        int x = fullscreen_ ? windowedRect_.left : CW_USEDEFAULT, y = fullscreen_ ? windowedRect_.top : CW_USEDEFAULT;
        if (x == CW_USEDEFAULT) { RECT cur; GetWindowRect(hwnd_, &cur); x = cur.left; y = cur.top; }
        if (noInteract()) {   // the corner for the new size, bottom of the z-order, not activated
            const POINT at = noInteractPos(r.right - r.left, r.bottom - r.top);
            SetWindowPos(hwnd_, HWND_BOTTOM, at.x, at.y, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hwnd_, HWND_NOTOPMOST, x, y, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        fullscreen_ = false;
    }
    bool fullscreen() const override { return fullscreen_; }

    void setVSync(bool on) override {
        typedef BOOL(WINAPI * PFN_SwapInterval)(int);
        static PFN_SwapInterval swap = (PFN_SwapInterval)wglGetProcAddress("wglSwapIntervalEXT");
        if (swap) swap(on ? 1 : 0);
        vsync_ = on;
    }
    bool vsync() const override { return vsync_; }

    void present() override {
        // [integration 09c] another API may present (D3D12 path, Rendering); the PC frame limiter runs for both paths.
        bool presented = false;
        if (platform::PresentOverride o = platform::presentOverride()) presented = o();
        if (!presented) SwapBuffers(hdc_);
        if (frameLimit_ > 0) {   // PC EXTENSION frame cap: sleep (1 ms timer resolution) then spin to the deadline
            using clock = std::chrono::steady_clock;
            const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / frameLimit_));
            auto now = clock::now();
            if (nextFrame_.time_since_epoch().count() == 0 || now - nextFrame_ > period) nextFrame_ = now;   // late: resync
            nextFrame_ += period;
            while ((now = clock::now()) < nextFrame_) {
                auto left = nextFrame_ - now;
                if (left > std::chrono::milliseconds(2)) Sleep((DWORD)(std::chrono::duration_cast<std::chrono::milliseconds>(left).count() - 1));
                else std::this_thread::yield();
            }
        }
    }
    void setFrameLimit(int fps) override {
        fps = fps < 0 ? 0 : fps;
        if (fps > 0 && frameLimit_ == 0) timeBeginPeriod(1);
        if (fps == 0 && frameLimit_ > 0) timeEndPeriod(1);
        frameLimit_ = fps;
        nextFrame_ = {};
        LOG_INFO("window: frame limit %s (PC EXTENSION)", fps > 0 ? (std::to_string(fps) + " fps").c_str() : "off");
    }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool focused() const override { return focused_; }

    void setTitle(const char* title) override {
        wchar_t w[512];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, w, 512);
        SetWindowTextW(hwnd_, w);
    }

    void setMouseCaptured(bool captured) override {
        if (captured == mouseCaptured_) return;
        mouseCaptured_ = captured;
        if (noInteract()) return;   // NOINTERACT: the cursor is never hidden or moved
        ShowCursor(!captured);
        if (captured) { POINT c = centerScreen(); SetCursorPos(c.x, c.y); ++cursorHides_; ++cursorMoves_; }
    }

private:
    bool initGL() {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int pf = ChoosePixelFormat(hdc_, &pfd);
        if (!pf || !SetPixelFormat(hdc_, pf, &pfd)) { LOG_ERROR("SetPixelFormat failed"); return false; }
        hglrc_ = wglCreateContext(hdc_);
        if (!hglrc_ || !wglMakeCurrent(hdc_, hglrc_)) { LOG_ERROR("wglCreateContext failed"); return false; }
        upgradeToRobustContext();
        LOG_INFO("OpenGL: %s", (const char*)glGetString(GL_VERSION));
        return true;
    }

    static constexpr uint32_t kPadLT = 1u << 16, kPadRT = 1u << 17, kPadLStickUp = 1u << 18, kPadLStickDown = 1u << 19,
                              kPadLStickLeft = 1u << 20, kPadLStickRight = 1u << 21;
    uint32_t padBits_ = 0;
    uint32_t fakePad_ = 0;   // injectPad (scripted runs)
    std::vector<int> uiVk_[(int)UiKey::Count];
    std::vector<uint32_t> uiPad_[(int)UiKey::Count];
    int wheel_ = 0;
    std::u32string typed_;
    std::vector<uint16_t> keyPresses_;
    wchar_t highSurrogate_ = 0;
    bool cursorHidden_ = false;
    bool fullscreen_ = false, vsync_ = false;
    bool modeChanged_ = false;     // the monitor runs our fullscreen mode (restore on windowed / focus loss / exit)
    int frameLimit_ = 0;
    std::chrono::steady_clock::time_point nextFrame_{};
    int fsW_ = 0, fsH_ = 0;
    std::wstring monitorDevice_;
    RECT windowedRect_{};
    static constexpr UINT_PTR kReapplyTimer = 0x5753;
    static constexpr int kReapplyDelayMs = 1500, kReapplyMax = 4, kReapplyWindowMs = 10000;
    std::chrono::steady_clock::time_point lastRestore_{};   // (the re-apply debounce)
    std::chrono::steady_clock::time_point reapplies_[kReapplyMax] = {};
    int reapplyNext_ = 0;
    bool reapplyPending_ = false, reapplyGaveUp_ = false;

    static int vkByName(const std::string& n) {
        static const struct { const char* name; int vk; } t[] = {
            {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE}, {"Space", VK_SPACE}, {"Tab", VK_TAB}, {"Backspace", VK_BACK},
            {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"PageUp", VK_PRIOR},
            {"PageDown", VK_NEXT}, {"Home", VK_HOME}, {"End", VK_END}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE},
            {"Shift", VK_SHIFT}, {"Control", VK_CONTROL}, {"Alt", VK_MENU}, {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3},
            {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10},
            {"F11", VK_F11}, {"F12", VK_F12}};
        for (const auto& e : t) if (n == e.name) return e.vk;
        if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
        return 0;
    }
    static uint32_t padByName(const std::string& n) {
        static const struct { const char* name; uint32_t m; } t[] = {
            {"DPadUp", XINPUT_GAMEPAD_DPAD_UP}, {"DPadDown", XINPUT_GAMEPAD_DPAD_DOWN},
            {"DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT}, {"DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT},
            {"Start", XINPUT_GAMEPAD_START}, {"Back", XINPUT_GAMEPAD_BACK}, {"LThumb", XINPUT_GAMEPAD_LEFT_THUMB},
            {"RThumb", XINPUT_GAMEPAD_RIGHT_THUMB}, {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER},
            {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B},
            {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y}, {"LT", kPadLT}, {"RT", kPadRT},
            {"LStickUp", kPadLStickUp}, {"LStickDown", kPadLStickDown}, {"LStickLeft", kPadLStickLeft},
            {"LStickRight", kPadLStickRight}};
        for (const auto& e : t) if (n == e.name) return e.m;
        return 0;
    }

    // PC ADAPTATION (high-DPI desktops): the process is per-monitor DPI aware (V2), so Windows does not render the game at
    // the scaled logical size and stretch it (soft image on a 125-150 % 4K desktop). Every window / cursor coordinate is
    // then in physical pixels: the client size (the render size), GetCursorPos / ScreenToClient / SetCursorPos (menu
    // pointer, mouse-look recentring), the monitor rectangle and the display modes all agree. The entry points are looked
    // up at run time (Windows 10 1703+ for V2; older systems fall back to system-DPI awareness).
    static void enableDpiAwareness() {
        static bool done = false;
        if (done) return;
        done = true;
        HMODULE u = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI * PFN_SetCtx)(HANDLE);
        typedef BOOL(WINAPI * PFN_SetAware)();
        auto setCtx = u ? (PFN_SetCtx)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        if (setCtx && setCtx((HANDLE)(intptr_t)-4)) { LOG_INFO("window: per-monitor DPI aware (V2)"); return; }   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (setCtx && GetLastError() == ERROR_ACCESS_DENIED) { LOG_INFO("window: DPI awareness already set (manifest)"); return; }
        auto setAware = u ? (PFN_SetAware)(void*)GetProcAddress(u, "SetProcessDPIAware") : nullptr;
        if (setAware && setAware()) LOG_INFO("window: system DPI aware (no per-monitor V2 on this Windows)");
    }
    static UINT systemDpi() {
        typedef UINT(WINAPI * PFN)();
        static PFN f = (PFN)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
        return f ? f() : 96;
    }
    UINT windowDpi() const {
        typedef UINT(WINAPI * PFN)(HWND);
        static PFN f = (PFN)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        return f && hwnd_ ? f(hwnd_) : systemDpi();
    }
    // The window rectangle around a client rectangle for a DPI (frame and caption scale with the monitor).
    static void adjustFrame(RECT& r, UINT dpi) {
        typedef BOOL(WINAPI * PFN)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static PFN f = (PFN)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
        if (!f || !f(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi)) AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    }

    // PC ADAPTATION (RX 7900 XTX driver-reset hardening, Rendering request): recreate the context through
    // WGL_ARB_create_context with WGL_ARB_create_context_robustness - the same GL version and the compatibility profile
    // as the legacy context (no robust buffer access: it can cost GPU time), plus
    // LOSE_CONTEXT_ON_RESET, so glGetGraphicsResetStatus (the renderer polls it per frame) reports a reset. Any failure
    // keeps the legacy context. WFC_GL_LEGACY_CONTEXT=1 skips it (A/B).
    void upgradeToRobustContext() {
        char legacy[8] = {0};                          // Win32 env read (no new includes in this file)
        if (GetEnvironmentVariableA("WFC_GL_LEGACY_CONTEXT", legacy, sizeof legacy) > 0 && legacy[0] && legacy[0] != '0') {
            LOG_INFO("OpenGL context: legacy (WFC_GL_LEGACY_CONTEXT)");
            return;
        }
        using GetExtStr = const char*(WINAPI*)(HDC);
        using CreateAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
        auto getExt = (GetExtStr)(void*)wglGetProcAddress("wglGetExtensionsStringARB");
        auto create = (CreateAttribs)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
        const char* ext = getExt ? getExt(hdc_) : nullptr;
        auto hasExt = [](const char* list, const char* name) {   // whole-token match in the space-separated list
            for (const char* p = list; p && *p;) {
                const char* q = p; const char* n = name;
                while (*n && *q == *n) { ++q; ++n; }
                if (!*n && (*q == ' ' || *q == 0)) return true;
                while (*p && *p != ' ') ++p;
                while (*p == ' ') ++p;
            }
            return false;
        };
        // (AMD's WGL extension string omits WGL_ARB_create_context_robustness although the driver creates robust contexts -
        // the GL side has GL_KHR_robustness: 2026-10-13, RX 7900 XTX. So without the token the creation is still tried; a
        // driver that rejects the attributes returns no context and the legacy one stays.)
        const bool advertised = ext && hasExt(ext, "WGL_ARB_create_context_robustness");
        if (!create) {
            LOG_INFO("OpenGL context: legacy (wglCreateContextAttribsARB unavailable: getExt %d ext %d)", getExt != nullptr, advertised);
            if (ext && GetEnvironmentVariableA("WFC_GL_EXTLOG", legacy, sizeof legacy) > 0) LOG_INFO("WGL extensions: %s", ext);
            return;
        }
        GLint major = 0, minor = 0;
        glGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &major);
        glGetIntegerv(0x821C /* GL_MINOR_VERSION */, &minor);
        if (major <= 0) { LOG_INFO("OpenGL context: legacy (version query failed)"); return; }
        const int attribs[] = {
            0x2091 /* WGL_CONTEXT_MAJOR_VERSION_ARB */, major,
            0x2092 /* WGL_CONTEXT_MINOR_VERSION_ARB */, minor,
            0x9126 /* WGL_CONTEXT_PROFILE_MASK_ARB */, 0x00000002 /* WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB */,
            // (no WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB: bounds-checked buffer access can cost GPU time; only the reset notification)
            0x8256 /* WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB */, 0x8252 /* WGL_LOSE_CONTEXT_ON_RESET_ARB */,
            0};
        HGLRC robust = create(hdc_, nullptr, attribs);
        if (!robust || !wglMakeCurrent(hdc_, robust)) {
            if (robust) wglDeleteContext(robust);
            wglMakeCurrent(hdc_, hglrc_);
            LOG_WARN("OpenGL context: robust creation failed (GL %d.%d compatibility) - keeping the legacy context", major, minor);
            return;
        }
        GLint strategy = 0;                              // what the driver actually gave (GL_RESET_NOTIFICATION_STRATEGY)
        glGetIntegerv(0x8256, &strategy);
        if (strategy != 0x8252 /* GL_LOSE_CONTEXT_ON_RESET */) {
            wglMakeCurrent(hdc_, hglrc_);
            wglDeleteContext(robust);
            LOG_WARN("OpenGL context: the robust context reports reset strategy 0x%x, not LOSE_CONTEXT_ON_RESET - keeping the legacy context "
                     "(WGL robustness %s)", strategy, advertised ? "advertised" : "not advertised");
            return;
        }
        wglDeleteContext(hglrc_);
        hglrc_ = robust;
        LOG_INFO("OpenGL context: robust (GL %d.%d compatibility, LOSE_CONTEXT_ON_RESET%s) [PC ADAPTATION]", major, minor,
                 advertised ? "" : "; WGL_ARB_create_context_robustness not advertised, created anyway");
    }

    POINT centerScreen() const {
        RECT rc; GetClientRect(hwnd_, &rc);
        POINT c{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
        ClientToScreen(hwnd_, &c);
        return c;
    }

    void pollGamepad(InputFrame& input) {
        static const bool noPad = std::getenv("WFC_NOPAD") != nullptr;   // scripted runs: ignore a real controller
        if (noPad || noInteract()) { padBits_ = fakePad_; return; }   // (NOINTERACT: the user's controller is theirs)
        XINPUT_STATE st{};
        if (XInputGetState(0, &st) == ERROR_SUCCESS) {
            static bool logged = false;
            if (!logged) { logged = true; LOG_INFO("input: XInput pad 0 connected"); }
            input.padConnected = true;
            auto axis = [](SHORT v, SHORT dz) -> float {
                if (v > dz) return float(v - dz) / float(32767 - dz);
                if (v < -dz) return float(v + dz) / float(32768 - dz);
                return 0.0f;
            };
            input.padLX = axis(st.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.padLY = axis(st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.padRX = axis(st.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.padRY = axis(st.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.padLT = st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? st.Gamepad.bLeftTrigger / 255.0f : 0.0f;
            // Raw buttons plus synthetic bits for the triggers and the left stick's directions (bound by name).
            padBits_ = st.Gamepad.wButtons;
            if (st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) padBits_ |= kPadLT;
            if (st.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) padBits_ |= kPadRT;
            if (input.padLY > 0.5f) padBits_ |= kPadLStickUp;
            if (input.padLY < -0.5f) padBits_ |= kPadLStickDown;
            if (input.padLX < -0.5f) padBits_ |= kPadLStickLeft;
            if (input.padLX > 0.5f) padBits_ |= kPadLStickRight;
            padBits_ |= fakePad_;
        } else {
            padBits_ = fakePad_;
            input.padConnected = false;
            input.padLX = input.padLY = input.padRX = input.padRY = 0.0f;
            input.padLT = 0.0f;
        }
    }

    static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Win32Window* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self) return self->wndProc(hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CLOSE: wantClose_ = true; return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
            case WM_SIZE:
                width_ = LOWORD(lp); height_ = HIWORD(lp);
                if (width_ < 1) width_ = 1;
                if (height_ < 1) height_ = 1;
                // [Systems] minimised in fullscreen by any route (Win+D, the taskbar): the desktop gets its own mode back;
                // reactivation (WM_ACTIVATEAPP) switches to ours again
                if (wp == SIZE_MINIMIZED && fullscreen_ && modeChanged_) restoreDesktopMode();
                return 0;
            case 0x02E4: {   // WM_GETDPISCALEDSIZE: the window size for the new DPI keeps the client size (no rescale)
                if (fullscreen_) return FALSE;
                RECT c; GetClientRect(hwnd, &c);
                RECT r = {0, 0, c.right - c.left, c.bottom - c.top};
                adjustFrame(r, (UINT)wp);
                SIZE* sz = reinterpret_cast<SIZE*>(lp);
                sz->cx = r.right - r.left; sz->cy = r.bottom - r.top;
                return TRUE;
            }
            case 0x02E0: {   // WM_DPICHANGED: moved to a monitor with another scale
                if (fullscreen_) return 0;   // the popup already covers its monitor in physical pixels
                const RECT* sug = reinterpret_cast<const RECT*>(lp);
                RECT c; GetClientRect(hwnd, &c);
                RECT r = {0, 0, c.right - c.left, c.bottom - c.top};
                adjustFrame(r, HIWORD(wp));
                SetWindowPos(hwnd, nullptr, sug->left, sug->top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
                LOG_INFO("window: monitor DPI %u, client stays %dx%d", (unsigned)HIWORD(wp), (int)(c.right - c.left), (int)(c.bottom - c.top));
                return 0;
            }
            case WM_SETFOCUS: focused_ = !noInteract(); return 0;
            case WM_MOUSEWHEEL: wheel_ += GET_WHEEL_DELTA_WPARAM(wp); return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT && (cursorHidden_ || mouseCaptured_) && !noInteract()) { SetCursor(nullptr); return TRUE; }
                break;
            case WM_KILLFOCUS: focused_ = false; return 0;
            case WM_ACTIVATEAPP:
                // Fullscreen at a changed mode: alt-tab returns the desktop to its own mode; coming back reapplies ours.
                if (fullscreen_) {
                    if (!wp && modeChanged_) { restoreDesktopMode(); ShowWindow(hwnd_, SW_MINIMIZE); }
                    else if (wp && !modeChanged_ && fsW_ > 0) { ShowWindow(hwnd_, SW_RESTORE); scheduleReapply(); }
                }
                break;
            case WM_TIMER:
                if (wp == kReapplyTimer) {
                    KillTimer(hwnd_, kReapplyTimer);
                    reapplyPending_ = false;
                    if (fullscreen_ && !modeChanged_ && fsW_ > 0 && GetForegroundWindow() == hwnd_) reapplyNow();
                    return 0;
                }
                break;
            case WM_CHAR: {
                // UTF-16 code units -> code points (surrogate pairs joined); control characters are keys, not text.
                wchar_t c = (wchar_t)wp;
                if (c >= 0xD800 && c <= 0xDBFF) { highSurrogate_ = c; return 0; }
                char32_t cp = c;
                if (c >= 0xDC00 && c <= 0xDFFF) { cp = highSurrogate_ ? 0x10000 + ((highSurrogate_ - 0xD800) << 10) + (c - 0xDC00) : 0; highSurrogate_ = 0; }
                if (cp >= 32 && cp != 127 && typed_.size() < 64) typed_.push_back(cp);
                return 0;
            }
            case WM_KEYDOWN:
                if (keyPresses_.size() < 64) keyPresses_.push_back((uint16_t)wp);
                break;
            // F10 is a system key: Windows sends WM_SYSKEYDOWN and enters window-menu mode on its release, so it never
            // reached keyPresses (the developer QA panel's toggle). Taken here (no auto-repeat), menu mode suppressed.
            case WM_SYSKEYDOWN:
                if (wp == VK_F10) {
                    if (!(lp & (1 << 30)) && keyPresses_.size() < 64) keyPresses_.push_back((uint16_t)wp);
                    return 0;
                }
                break;
            case WM_SYSKEYUP:
                if (wp == VK_F10) return 0;
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HINSTANCE hinst_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    int width_ = 0, height_ = 0;
    bool focused_ = true;
    bool wantClose_ = false;
    bool mouseCaptured_ = false;
    std::chrono::steady_clock::time_point createdAt_{};
    int selfTests_ = 0, cursorHides_ = 0, cursorMoves_ = 0;   // WFC_NOINTERACTTEST
};

} // namespace

IWindow* createWindow(int width, int height, const char* title) {
    auto* w = new Win32Window();
    if (!w->create(width, height, title)) { delete w; return nullptr; }
    return w;
}

} // namespace platform
#endif // _WIN32
