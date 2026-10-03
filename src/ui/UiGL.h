// Clean-room reconstruction — frontend (Scaleform-equivalent) GL entry points used by the application.
#pragma once

namespace ui {

// Frontend frame with no 3D world: viewport + clear to black (the UI_FrontEnd_m scene is not exported yet).
void beginScreenFrame(int width, int height);

} // namespace ui
