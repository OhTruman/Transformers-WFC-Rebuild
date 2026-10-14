// Harness-only read/write access to two private product members, without touching product
// headers: World's collision (so probes can run the PRODUCTION PlayerController::applyToPawn path
// against synthetic geometry) and Character's drawn skinned pose (pose-freeze / mesh-pop analysis).
// Standard and render headers are included first with normal access; the game headers below are
// compiled with `private` widened (access only: same layout, same definitions).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "assets/SkinnedModel.h"
#include "core/Math.h"
#include "platform/Input.h"
#include "render/Mesh.h"
#include "render/Renderer.h"

#define private public
#define protected public
#include "game/Character.h"
#include "game/World.h"
#undef private
#undef protected

namespace fid {
game::CollisionWorld& worldCollision(game::World& w) { return w.collision_; }
// Builds with lazy skinning (GPU skinning, 151bc1f+) fill the CPU pose buffers only when something asks for them
// (Character::ensureSkinned, called by a CPU draw): the rig never draws, so it asks first; older builds skin in the tick.
template <class C> auto ensureSkinnedImpl(const C& c, int) -> decltype(c.ensureSkinned(), void()) { c.ensureSkinned(); }
template <class C> void ensureSkinnedImpl(const C&, long) {}
const render::MeshData& drawnPose(const game::Character& c) { ensureSkinnedImpl(c, 0); return c.poseBuf_; }
} // namespace fid

namespace fid {
// Partner mesh pose (gameplay Pass 13+ dual-mesh transform display); nullptr on builds without it.
template <class C> auto partnerPoseImpl(const C& c, int) -> decltype(&c.partnerBuf_) { return &c.partnerBuf_; }
template <class C> const render::MeshData* partnerPoseImpl(const C&, long) { return nullptr; }
const render::MeshData* partnerPose(const game::Character& c) { ensureSkinnedImpl(c, 0); return partnerPoseImpl(c, 0); }
} // namespace fid
