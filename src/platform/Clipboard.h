// Clipboard text (PC EXTENSION): the online lobby's join panel copies the join code (ONLINE_MP_UI_STRINGS.md section 4).
#pragma once
#include <string>

namespace platform {

// UTF-8 text to the system clipboard; false when it could not be set (no clipboard on this platform).
bool setClipboardText(const std::string& utf8);

} // namespace platform
