// Clean-room reconstruction — entry point.
#include "core/Application.h"
#include "render/gl/RenderWatchdog.h"

int main() {
    render::watchdog::installCrashHandler();      // crash report + minidump for any unhandled exception
    core::Application app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
