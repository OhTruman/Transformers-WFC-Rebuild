// Clean-room reconstruction — global debug-draw flags (toggled at runtime).
#pragma once

namespace core {

struct DebugFlags {
    bool enabled = false;      // master debug-draw toggle (bounds, aim ray, capsule, socket)
    static DebugFlags& get() { static DebugFlags f; return f; }
};

} // namespace core
