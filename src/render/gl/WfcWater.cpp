// Enhanced water (the Water row, user playtest 2026-10-23 #14, Integration-approved; default Original = the authored water
// materials, byte-identical). Original already carries every authored feature (RE's shipped-PS decode: the two-speed pans, the
// cube reflection, DepthBiasedAlpha, the distortion pass - restored on the vertex-lightmapped waterfalls in 567e61fe).
// Enhanced (planned): animated normals, screen-space reflections and a refraction depth tint on the water materials.
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cstdlib>

namespace render {
namespace wfc {

// the Water row; WFC_WATER=0|1 overrides it (harness runs)
int Pipeline::waterMode() const {
    static const int env = std::getenv("WFC_WATER") ? std::min(std::max(std::atoi(std::getenv("WFC_WATER")), 0), 1) : -1;
    return env >= 0 ? env : waterMode_;
}

}  // namespace wfc
}  // namespace render
