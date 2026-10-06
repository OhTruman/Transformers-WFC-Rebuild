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
const render::MeshData& drawnPose(const game::Character& c) { return c.poseBuf_; }
} // namespace fid

namespace fid {
// Partner mesh pose (gameplay Pass 13+ dual-mesh transform display); nullptr on builds without it.
template <class C> auto partnerPoseImpl(const C& c, int) -> decltype(&c.partnerBuf_) { return &c.partnerBuf_; }
template <class C> const render::MeshData* partnerPoseImpl(const C&, long) { return nullptr; }
const render::MeshData* partnerPose(const game::Character& c) { return partnerPoseImpl(c, 0); }
} // namespace fid
