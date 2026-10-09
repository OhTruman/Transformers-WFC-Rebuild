// Clean-room reconstruction — an optional replacement for the window's buffer swap (PC ADAPTATION): the renderer
// registers it when another API presents the frame (D3D12 present for upscalers / frame generation). Unset = the
// window swaps its GL buffers as before.
#pragma once

namespace platform {

// Returns true when it presented the frame (the window then skips SwapBuffers); false = present normally.
using PresentOverride = bool (*)();
void setPresentOverride(PresentOverride f);
PresentOverride presentOverride();

} // namespace platform
