// Clean-room reconstruction — see PresentHook.h.
#include "platform/PresentHook.h"

namespace platform {
namespace {
PresentOverride g_override = nullptr;
}
void setPresentOverride(PresentOverride f) { g_override = f; }
PresentOverride presentOverride() { return g_override; }
} // namespace platform
