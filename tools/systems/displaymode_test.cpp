// platform::DesktopModeKeeper unit test (Systems; not part of the CMake build). No display mode is ever switched: the
// sequence runs against a fake ChangeDisplaySettingsExW that records its calls; the real display is only READ
// (ENUM_CURRENT_SETTINGS) and the restore mode is validated with CDS_TEST, which tests a mode without setting it.
// Build (repo root): clang++ -std=c++17 -Isrc tools/systems/displaymode_test.cpp -luser32 -o displaymode_test.exe
#include "platform/DisplayMode.h"

#include <cstdio>
#include <cwchar>
#include <vector>

namespace {
struct Call { bool hasMode; DWORD w, h, hz, bpp, fields, flags; };
std::vector<Call> g_calls;
LONG g_result = DISP_CHANGE_SUCCESSFUL;
LONG WINAPI fakeChange(LPCWSTR, DEVMODEW* m, HWND, DWORD flags, LPVOID) {
    g_calls.push_back({m != nullptr, m ? m->dmPelsWidth : 0, m ? m->dmPelsHeight : 0, m ? m->dmDisplayFrequency : 0, m ? m->dmBitsPerPel : 0,
                       m ? m->dmFields : 0, flags});
    return m ? g_result : DISP_CHANGE_SUCCESSFUL;
}
// fake EnumDisplaySettingsW for the start-up repair: the current and registry modes the test sets
DEVMODEW g_cur{}, g_reg{};
BOOL WINAPI fakeEnum(LPCWSTR, DWORD which, DEVMODEW* m) {
    if (which == ENUM_CURRENT_SETTINGS) { *m = g_cur; return TRUE; }
    if (which == ENUM_REGISTRY_SETTINGS) { *m = g_reg; return TRUE; }
    return FALSE;
}
bool exists(const wchar_t* p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
int fails = 0;
void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
DEVMODEW mode(DWORD w, DWORD h, DWORD hz) { DEVMODEW m{}; m.dmSize = sizeof m; m.dmPelsWidth = w; m.dmPelsHeight = h; m.dmDisplayFrequency = hz; m.dmBitsPerPel = 32;
    m.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL | DM_POSITION; return m; }
}  // namespace

int main() {
    using platform::DesktopModeKeeper;
    // 1. the 144 Hz desktop is saved, our 1080p mode does not overwrite it, the restore puts 144 Hz back dynamically
    {
        DesktopModeKeeper k; g_calls.clear(); g_result = DISP_CHANGE_SUCCESSFUL;
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        k.switched();
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(1920, 1080, 60));   // a re-apply / resolution change while ours is active
        const LONG r = k.restore(&fakeChange);
        check(r == DISP_CHANGE_SUCCESSFUL && g_calls.size() == 1, "one restore call");
        check(g_calls.size() == 1 && g_calls[0].hasMode && g_calls[0].w == 2560 && g_calls[0].h == 1440 && g_calls[0].hz == 144,
              "restores the desktop mode saved BEFORE the switch (2560x1440 @ 144), not our mode");
        check(g_calls.size() == 1 && g_calls[0].flags == 0, "dynamic change (flags 0: no registry write, not CDS_FULLSCREEN)");
        check(g_calls.size() == 1 && !(g_calls[0].fields & DM_POSITION) && (g_calls[0].fields & DM_DISPLAYFREQUENCY), "sets size / depth / refresh only");
        check(!k.active(), "inactive after the restore");
        g_calls.clear();
        k.restore(&fakeChange);
        check(g_calls.empty(), "a second restore (exit after leaving fullscreen) does nothing");
    }
    // 2. never switched: restore is a no-op (exit / crash without fullscreen)
    {
        DesktopModeKeeper k; g_calls.clear();
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));   // the switch then failed: switched() not called
        k.restore(&fakeChange);
        check(g_calls.empty(), "no restore when our mode never became active");
    }
    // 3. the saved mode is refused: the registry mode as the fallback (the previous behaviour)
    {
        DesktopModeKeeper k; g_calls.clear(); g_result = DISP_CHANGE_BADMODE;
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        k.switched();
        const LONG r = k.restore(&fakeChange);
        check(r == DISP_CHANGE_BADMODE && g_calls.size() == 2 && g_calls[0].hasMode && !g_calls[1].hasMode,
              "refused saved mode -> ChangeDisplaySettingsEx(nullptr) (registry mode) as the fallback");
        g_result = DISP_CHANGE_SUCCESSFUL;
    }
    // 4. a second fullscreen session after a restore saves the desktop afresh
    {
        DesktopModeKeeper k; g_calls.clear();
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144)); k.switched(); k.restore(&fakeChange);
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 120)); k.switched(); g_calls.clear(); k.restore(&fakeChange);
        check(g_calls.size() == 1 && g_calls[0].hz == 120, "the next session restores the desktop mode current at ITS switch");
    }
    // 4b. a refused restore (the display fell back to the registry mode): the next session keeps the FIRST saved mode
    {
        DesktopModeKeeper k; g_calls.clear(); g_result = DISP_CHANGE_BADMODE;
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144)); k.switched(); k.restore(&fakeChange);
        g_result = DISP_CHANGE_SUCCESSFUL;
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 60));   // the registry fallback now runs
        k.switched(); g_calls.clear(); k.restore(&fakeChange);
        check(g_calls.size() == 1 && g_calls[0].hz == 144, "after a refused restore the registry fallback (60 Hz) is never saved: 144 Hz restored");
    }
    // 6. the restore file: written at the switch, deleted by a successful restore, kept by a refused one
    {
        wchar_t f[MAX_PATH]; GetTempPathW(MAX_PATH, f); wcscat(f, L"wfc_dm_test_restore.txt");
        DeleteFileW(f);
        DesktopModeKeeper k; g_result = DISP_CHANGE_SUCCESSFUL;
        k.setRestoreFile(f);
        k.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        check(!exists(f), "no file before the switch");
        k.switched();
        wchar_t dev[32] = {}; DEVMODEW m{};
        const bool rd = DesktopModeKeeper::readFile(f, dev, m);
        check(rd && m.dmPelsWidth == 2560 && m.dmPelsHeight == 1440 && m.dmDisplayFrequency == 144 && m.dmBitsPerPel == 32 &&
              std::wcscmp(dev, L"\\\\.\\DISPLAY1") == 0, "the switch writes the saved mode + device (read back exactly)");
        k.restore(&fakeChange);
        check(!exists(f), "a successful restore deletes the file");
        DesktopModeKeeper k2; k2.setRestoreFile(f); g_result = DISP_CHANGE_BADMODE;
        k2.saveBeforeSwitch(L"\\\\.\\DISPLAY1", mode(2560, 1440, 144)); k2.switched(); k2.restore(&fakeChange);
        check(exists(f), "a refused restore keeps the file (the next start / --restore-display retries)");
        g_result = DISP_CHANGE_SUCCESSFUL;
        // 7. start-up repair (a run killed in fullscreen): only on the registry-revert signature
        g_cur = mode(2560, 1440, 60); g_reg = mode(2560, 1440, 60); g_calls.clear();
        DEVMODEW got{};
        int r = DesktopModeKeeper::restoreFromFileAtStartup(f, false, &fakeChange, &fakeEnum, &got);
        check(r == 2 && g_calls.size() == 1 && g_calls[0].hz == 144 && g_calls[0].flags == 0 && !exists(f),
              "current == registry (60) != saved (144): the saved mode is put back dynamically, file deleted");
        DesktopModeKeeper::writeFile(f, L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        g_cur = mode(1920, 1080, 120); g_reg = mode(2560, 1440, 60); g_calls.clear();
        r = DesktopModeKeeper::restoreFromFileAtStartup(f, false, &fakeChange, &fakeEnum);
        check(r == 1 && g_calls.empty() && !exists(f), "current differs from the registry (the user changed the desktop): left alone, file deleted");
        DesktopModeKeeper::writeFile(f, L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        g_calls.clear();
        r = DesktopModeKeeper::restoreFromFileAtStartup(f, true, &fakeChange, &fakeEnum);
        check(r == 2 && g_calls.size() == 1 && g_calls[0].hz == 144, "--restore-display (force) restores without the signature");
        DesktopModeKeeper::writeFile(f, L"\\\\.\\DISPLAY1", mode(2560, 1440, 144));
        g_cur = mode(2560, 1440, 144); g_reg = mode(2560, 1440, 144); g_calls.clear();
        r = DesktopModeKeeper::restoreFromFileAtStartup(f, true, &fakeChange, &fakeEnum);
        check(r == 1 && g_calls.empty(), "already in the saved mode: nothing switched");
        check(DesktopModeKeeper::restoreFromFileAtStartup(f, true, &fakeChange, &fakeEnum) == 0, "no file: nothing (0)");
        DeleteFileW(f);
    }
    // 5. this machine: the current desktop mode, read only, as the restore would set it - validated with CDS_TEST (no switch)
    {
        DEVMODEW cur{}; cur.dmSize = sizeof cur;
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &cur)) {
            std::printf("      current desktop %lux%lu @ %lu Hz, %lu bpp\n", cur.dmPelsWidth, cur.dmPelsHeight, cur.dmDisplayFrequency, cur.dmBitsPerPel);
            // A display that lost its EDID (generic-monitor fallback) can run a mode its own mode list does not contain; Windows
            // then refuses even the unmodified current mode (CDS_TEST -2), and the keeper falls back to the registry mode (the
            // previous behaviour). The construction is then validated on the largest mode the display does list.
            int listed = 0;
            DEVMODEW dm{}, largest{}; dm.dmSize = sizeof dm;
            for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
                listed += dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight && dm.dmDisplayFrequency == cur.dmDisplayFrequency;
                if (dm.dmBitsPerPel >= 32 && (dm.dmPelsWidth > largest.dmPelsWidth || (dm.dmPelsWidth == largest.dmPelsWidth && dm.dmDisplayFrequency > largest.dmDisplayFrequency))) largest = dm;
            }
            const DEVMODEW& base = listed ? cur : largest;
            if (!listed) std::printf("      the current mode is not in the display's mode list (EDID fallback?): validating on %lux%lu @ %lu Hz instead\n",
                                     largest.dmPelsWidth, largest.dmPelsHeight, largest.dmDisplayFrequency);
            DEVMODEW m = DesktopModeKeeper::restorableFrom(base);
            const LONG t = ChangeDisplaySettingsExW(nullptr, &m, nullptr, CDS_TEST, nullptr);
            check(t == DISP_CHANGE_SUCCESSFUL, "a restore mode built by the keeper passes CDS_TEST (valid; CDS_TEST switches nothing)");
        } else check(false, "EnumDisplaySettings(ENUM_CURRENT_SETTINGS)");
    }
    std::printf("%s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
