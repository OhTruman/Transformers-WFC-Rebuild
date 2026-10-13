// Clean-room reconstruction — entry point.
#include "core/Application.h"
#include "render/gl/RenderWatchdog.h"
#include "platform/DisplayMode.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
#ifdef _WIN32
    // [Systems] wfc_rebuild.exe --restore-display: put back the desktop mode a run killed in fullscreen left behind
    // (wfc_display_restore.txt in the working directory, written at the fullscreen switch), then exit
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--restore-display") == 0) {
            DEVMODEW m{};
            const int r = platform::DesktopModeKeeper::restoreFromFileAtStartup(platform::displayRestoreFilePath(), true,
                                                                                &ChangeDisplaySettingsExW, &EnumDisplaySettingsW, &m);
            if (r == 2) std::printf("desktop mode restored: %lux%lu @ %lu Hz\n", m.dmPelsWidth, m.dmPelsHeight, m.dmDisplayFrequency);
            else std::printf("%s\n", r == 0 ? "no saved desktop mode (wfc_display_restore.txt) in this directory"
                                     : r == 1 ? "the display already runs the saved mode" : "the saved desktop mode was refused");
            return r == 3 ? 1 : 0;
        }
#else
    (void)argc; (void)argv;
#endif
    render::watchdog::installCrashHandler();      // crash report + minidump for any unhandled exception
    core::Application app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
