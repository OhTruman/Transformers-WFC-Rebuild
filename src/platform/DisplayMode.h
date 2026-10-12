// Clean-room reconstruction — the desktop display mode around the game's fullscreen mode switch (PC ADAPTATION, Systems).
// Fullscreen switches the monitor to the chosen resolution (Win32Window, CDS_FULLSCREEN). Leaving it must put back the mode
// the desktop ran BEFORE the switch, not the registry mode (ChangeDisplaySettingsEx(nullptr) restores the registry mode,
// which can differ - e.g. a 144 Hz desktop whose registry entry says 60 Hz). The keeper saves the current mode before the
// first switch (never overwritten while our mode is active) and restores exactly that, dynamically (no registry write),
// falling back to the registry mode only if the saved one is refused. Header-only, with the display call injected, so the
// sequence is unit-testable without switching modes (tools/systems/displaymode_test.cpp).
#pragma once
#ifdef _WIN32
#include <windows.h>

#include <atomic>
#include <cwchar>

namespace platform {

class DesktopModeKeeper {
public:
    using ChangeFn = LONG(WINAPI*)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

    // The fields a restore sets: size, depth, refresh (and the display flags, interlacing etc., when the mode carries them).
    static DEVMODEW restorableFrom(const DEVMODEW& current) {
        DEVMODEW m = current;
        m.dmSize = sizeof(DEVMODEW);
        m.dmDriverExtra = 0;
        m.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | (current.dmFields & DM_DISPLAYFLAGS);
        return m;
    }
    // Before a fullscreen switch: remember `current` (ENUM_CURRENT_SETTINGS) unless our mode is already active (a re-apply
    // or a resolution change in fullscreen: `current` is then our own mode, not the desktop's).
    void saveBeforeSwitch(const wchar_t* device, const DEVMODEW& current) {
        if (active_.load()) return;
        saved_ = restorableFrom(current);
        std::wcsncpy(device_, device ? device : L"", 31);
        device_[31] = 0;
    }
    void switched() { active_.store(true); }   // the CDS_FULLSCREEN change succeeded
    bool active() const { return active_.load(); }
    const DEVMODEW& saved() const { return saved_; }
    // Leave fullscreen / minimise / exit / crash: the saved mode, dynamically (flags 0: no registry write); if refused, the
    // registry mode (the previous behaviour). Returns the ChangeDisplaySettingsEx result of the saved mode (or
    // DISP_CHANGE_SUCCESSFUL when nothing was active). No allocation, no locks: callable from a crash handler.
    LONG restore(ChangeFn change) {
        if (!active_.exchange(false)) return DISP_CHANGE_SUCCESSFUL;
        const wchar_t* dev = device_[0] ? device_ : nullptr;
        DEVMODEW m = saved_;
        const LONG r = change(dev, &m, nullptr, 0, nullptr);
        if (r != DISP_CHANGE_SUCCESSFUL) change(dev, nullptr, nullptr, 0, nullptr);
        return r;
    }

private:
    DEVMODEW saved_{};
    wchar_t device_[32] = {};
    std::atomic<bool> active_{false};
};

// The process-wide keeper (Win32Window.cpp) and its restore with the real ChangeDisplaySettingsExW; safe from a crash
// handler, idempotent (a no-op when no fullscreen mode is active).
DesktopModeKeeper& desktopModeKeeper();
void restoreDesktopDisplayMode();

}  // namespace platform
#endif
