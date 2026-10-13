// Clean-room reconstruction — the desktop display mode around the game's fullscreen mode switch (PC ADAPTATION, Systems).
// Fullscreen switches the monitor to the chosen resolution (Win32Window, CDS_FULLSCREEN). Leaving it must put back the mode
// the desktop ran BEFORE the switch, not the registry mode (ChangeDisplaySettingsEx(nullptr) restores the registry mode,
// which can differ - e.g. a 144 Hz desktop whose registry entry says 60 Hz). The keeper saves the current mode before the
// first switch (never overwritten while our mode is active, and never replaced by a registry fallback) and restores exactly
// that, dynamically (no registry write), falling back to the registry mode only if the saved one is refused. Header-only,
// with the display call injected, so the sequence is unit-testable without switching modes (tools/systems/displaymode_test.cpp).
// A process that is killed (TerminateProcess) runs no code: Windows then reverts to the registry mode. For that case the
// keeper writes the saved mode to a small file at switch time (deleted by a successful restore); the next start of the game
// (or wfc_rebuild.exe --restore-display) puts it back when the display shows the registry-revert signature
// (restoreFromFileAtStartup).
#pragma once
#ifdef _WIN32
#include <windows.h>

#include <atomic>
#include <cstdio>
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
    // Before a fullscreen switch: remember `current` (ENUM_CURRENT_SETTINGS) as the desktop mode, unless
    //  - our mode is already active (a re-apply or a resolution change in fullscreen: `current` is then our own mode), or
    //  - a mode is already saved and the last restore did not put it back exactly (refused: the display then runs the
    //    registry fallback, which must never become "the desktop mode"; the first saved mode stays for the process).
    // After a successful restore the next session saves afresh: the user may have changed the desktop mode in between.
    void saveBeforeSwitch(const wchar_t* device, const DEVMODEW& current) {
        if (active_.load()) return;
        if (hasSaved_ && !lastRestoreOk_) return;
        saved_ = restorableFrom(current);
        hasSaved_ = true;
        std::wcsncpy(device_, device ? device : L"", 31);
        device_[31] = 0;
    }
    void switched() {                                    // the CDS_FULLSCREEN change succeeded
        active_.store(true);
        if (file_[0]) writeFile(file_, device_, saved_);
    }
    bool active() const { return active_.load(); }
    bool hasSaved() const { return hasSaved_; }
    const DEVMODEW& saved() const { return saved_; }
    // where switched() records the saved mode for the next start (empty: no file); set before the first switch
    void setRestoreFile(const wchar_t* path) { std::wcsncpy(file_, path ? path : L"", MAX_PATH - 1); file_[MAX_PATH - 1] = 0; }
    // Leave fullscreen / minimise / exit / crash / hang: the saved mode, dynamically (flags 0: no registry write); if refused,
    // the registry mode (the previous behaviour). Returns the ChangeDisplaySettingsEx result of the saved mode (or
    // DISP_CHANGE_SUCCESSFUL when nothing was active). No allocation, no locks: callable from a crash handler or another
    // thread (the atomic exchange makes exactly one caller restore).
    LONG restore(ChangeFn change) {
        if (!active_.exchange(false)) return DISP_CHANGE_SUCCESSFUL;
        const wchar_t* dev = device_[0] ? device_ : nullptr;
        DEVMODEW m = saved_;
        const LONG r = change(dev, &m, nullptr, 0, nullptr);
        lastRestoreOk_ = r == DISP_CHANGE_SUCCESSFUL;
        if (!lastRestoreOk_) change(dev, nullptr, nullptr, 0, nullptr);
        if (file_[0] && lastRestoreOk_) DeleteFileW(file_);   // (refused: kept, so the next start / --restore-display retries)
        return r;
    }

    // ---- the restore file: "device width height bpp hz flags" on one line (UTF-16 device name as UTF-8 is not needed:
    // \\.\DISPLAYn is ASCII) ----
    static bool writeFile(const wchar_t* path, const wchar_t* device, const DEVMODEW& m) {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char dev[32] = {};
        for (int i = 0; i < 31 && device[i]; ++i) dev[i] = device[i] < 128 ? (char)device[i] : '?';
        char line[160];
        const int n = std::snprintf(line, sizeof line, "%s %lu %lu %lu %lu %lu\r\n", dev[0] ? dev : "-", m.dmPelsWidth, m.dmPelsHeight,
                                    m.dmBitsPerPel, m.dmDisplayFrequency, (m.dmFields & DM_DISPLAYFLAGS) ? m.dmDisplayFlags : 0ul);
        DWORD w = 0;
        const bool ok = n > 0 && WriteFile(h, line, (DWORD)n, &w, nullptr) && w == (DWORD)n;
        CloseHandle(h);
        return ok;
    }
    static bool readFile(const wchar_t* path, wchar_t* device, DEVMODEW& m) {
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char buf[160] = {};
        DWORD got = 0;
        ReadFile(h, buf, sizeof buf - 1, &got, nullptr);
        CloseHandle(h);
        char dev[32] = {};
        unsigned long w = 0, ht = 0, bpp = 0, hz = 0, fl = 0;
        if (std::sscanf(buf, "%31s %lu %lu %lu %lu %lu", dev, &w, &ht, &bpp, &hz, &fl) != 6 || !w || !ht || !hz) return false;
        const bool none = dev[0] == '-' && dev[1] == 0;
        for (int i = 0; i < 32; ++i) device[i] = none ? 0 : (wchar_t)(unsigned char)dev[i];
        DEVMODEW d{};
        d.dmSize = sizeof d;
        d.dmPelsWidth = w; d.dmPelsHeight = ht; d.dmBitsPerPel = bpp; d.dmDisplayFrequency = hz; d.dmDisplayFlags = fl;
        d.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | (fl ? DM_DISPLAYFLAGS : 0);
        m = d;
        return true;
    }
    // At start-up (or --restore-display, force = true): a file left by a process that died in fullscreen. The mode is put
    // back only when the display shows the registry-revert signature: the current mode IS the registry mode and differs from
    // the saved one (otherwise the user changed the desktop since, and it is left alone). Returns the action taken:
    // 0 no file, 1 left alone (deleted), 2 restored, 3 restore refused (file kept for --restore-display).
    using EnumFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DEVMODEW*);
    static int restoreFromFileAtStartup(const wchar_t* path, bool force, ChangeFn change, EnumFn enumSettings, DEVMODEW* restored = nullptr) {
        wchar_t dev[32] = {};
        DEVMODEW m{};
        if (!readFile(path, dev, m)) return 0;
        const wchar_t* d = dev[0] ? dev : nullptr;
        DEVMODEW cur{}, reg{};
        cur.dmSize = reg.dmSize = sizeof(DEVMODEW);
        const bool haveCur = enumSettings(d, ENUM_CURRENT_SETTINGS, &cur) != 0, haveReg = enumSettings(d, ENUM_REGISTRY_SETTINGS, &reg) != 0;
        auto same = [](const DEVMODEW& a, const DEVMODEW& b) {
            return a.dmPelsWidth == b.dmPelsWidth && a.dmPelsHeight == b.dmPelsHeight && a.dmDisplayFrequency == b.dmDisplayFrequency;
        };
        const bool reverted = haveCur && haveReg && same(cur, reg) && !same(cur, m);
        if (!force && !reverted) { DeleteFileW(path); return 1; }
        if (haveCur && same(cur, m)) { DeleteFileW(path); return 1; }   // (already in the saved mode)
        DEVMODEW want = m;
        if (change(d, &want, nullptr, 0, nullptr) != DISP_CHANGE_SUCCESSFUL) return 3;
        DeleteFileW(path);
        if (restored) *restored = m;
        return 2;
    }

private:
    DEVMODEW saved_{};
    wchar_t device_[32] = {};
    wchar_t file_[MAX_PATH] = {};
    bool hasSaved_ = false, lastRestoreOk_ = true;
    std::atomic<bool> active_{false};
};

// The process-wide keeper (Win32Window.cpp) and its restore with the real ChangeDisplaySettingsExW; safe from a crash
// handler and from the watchdog thread, idempotent (a no-op when no fullscreen mode is active).
DesktopModeKeeper& desktopModeKeeper();
void restoreDesktopDisplayMode();
// The restore file's path: wfc_display_restore.txt in the working directory (the profile / log directory).
const wchar_t* displayRestoreFilePath();

}  // namespace platform
#endif
