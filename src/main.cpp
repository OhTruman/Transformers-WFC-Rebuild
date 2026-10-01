// Clean-room reconstruction — entry point.
#include "core/Application.h"

int main() {
    core::Application app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
