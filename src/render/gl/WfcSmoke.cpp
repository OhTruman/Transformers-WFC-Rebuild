// Enhanced smoke (the Smoke row, user 2026-10-17; default Original = the authored sprites, byte-identical): stage 1 lights the
// smoke emitters' sprites by their surroundings (the sun with its shadow, the ambient light environment at the particle, and the
// local / transient lights where the mode has them) and gives them a flowing churn; stage 2 (Realistic) splats the big plumes into
// the X3 froxel volume. The smoke emitters come from Systems' template list (map FX data flag).
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cstdlib>

namespace render {
namespace wfc {

// the Smoke row; WFC_SMOKE=0|1 overrides it (harness runs)
int Pipeline::smokeMode() const {
    static const int env = std::getenv("WFC_SMOKE") ? std::min(std::max(std::atoi(std::getenv("WFC_SMOKE")), 0), 1) : -1;
    return env >= 0 ? env : smokeMode_;
}

}  // namespace wfc
}  // namespace render
