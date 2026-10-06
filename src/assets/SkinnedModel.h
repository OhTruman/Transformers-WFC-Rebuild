// Clean-room reconstruction — skinned glTF model: skeleton, skin, clips, and CPU skinning.
// Self-contained (reads its own GLB); complements the static baker in Gltf.h.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Mesh.h"

namespace assets {

enum class AnimPath { Translation, Rotation, Scale };
enum class Interp { Linear, Step, CubicSpline };

struct AnimSampler {
    std::vector<float> times;    // keyframe times (seconds)
    std::vector<float> values;   // flat: comps per key (3 for T/S, 4 for R)
    int comps = 0;
    Interp interp = Interp::Linear;
};

struct AnimChannel {
    int node = -1;               // target node index
    AnimPath path = AnimPath::Translation;
    int sampler = -1;
};

struct AnimClip {
    std::string name;
    std::string category;        // from extras.category when present
    float duration = 0.0f;
    bool additive = false;
    std::vector<AnimSampler> samplers;
    std::vector<AnimChannel> channels;
};

struct Node {
    core::Vec3 t{0, 0, 0};
    core::Quat r;
    core::Vec3 s{1, 1, 1};
    int parent = -1;
    std::vector<int> children;
};

struct SkinnedModel {
    // Skinned mesh, in mesh-local space (one combined primitive set).
    std::vector<float> positions;    // 3/vertex
    std::vector<float> normals;      // 3/vertex
    std::vector<float> uv;           // 2/vertex
    std::vector<uint16_t> joints;    // 4/vertex (indices into skinJoints)
    std::vector<float> weights;      // 4/vertex
    std::vector<uint32_t> indices;
    std::vector<render::SubMesh> subs;
    std::vector<render::Material> mats;

    // Skeleton + skin.
    std::vector<Node> nodes;
    std::vector<std::string> nodeNames;
    std::vector<int> roots;
    std::vector<int> skinJoints;         // node index for each skin joint
    std::vector<core::Mat4> invBind;     // per skin joint

    std::vector<AnimClip> clips;

    core::Vec3 boundsMin{0, 0, 0}, boundsMax{0, 0, 0};

    bool valid() const { return !positions.empty() && !skinJoints.empty(); }
    size_t vertexCount() const { return positions.size() / 3; }
    int nodeByName(const std::string& n) const {
        for (size_t i = 0; i < nodeNames.size(); ++i) if (nodeNames[i] == n) return (int)i;
        return -1;
    }
    int clipByName(const std::string& n) const {
        for (size_t i = 0; i < clips.size(); ++i) if (clips[i].name == n) return (int)i;
        return -1;
    }
    // First non-additive clip in category `c` whose name contains `needle`; falls back to
    // the plain first-of-category. Used for directional locomotion (e.g. the "_F" variant).
    int clipOfCategoryNamed(const std::string& c, const std::string& needle) const {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].category != c) continue;
            if (clips[i].additive || clips[i].name.rfind("ADD_", 0) == 0) continue;
            if (clips[i].name.find(needle) != std::string::npos) return (int)i;
        }
        return firstClipOfCategory(c);
    }
    int firstClipOfCategory(const std::string& c) const {
        int additiveFallback = -1;
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].category != c) continue;
            bool add = clips[i].additive || clips[i].name.rfind("ADD_", 0) == 0;
            if (!add) return (int)i;                      // prefer full-body clips
            if (additiveFallback < 0) additiveFallback = (int)i;
        }
        return additiveFallback;
    }
};

bool loadSkinnedGlb(const std::string& path, SkinnedModel& out);   // .glb, or .gltf with an external buffer
// Append the clips of a separate animation file (UE AnimSet export), channels matched by bone name.
bool loadAnimationsByName(const std::string& path, SkinnedModel& out);
// The same file parsed once, skeleton-independent: clips whose channel nodes are the FILE's node indices, plus the
// file's node names. appendAnimations then matches channels to a skeleton by bone name (as loadAnimationsByName does),
// so a parsed AnimSet can be cached and shared by every skeleton that uses it (frontend preview bodies).
struct AnimFile { std::vector<std::string> nodeNames; std::vector<AnimClip> clips; };
bool loadAnimationFile(const std::string& path, AnimFile& out);
size_t appendAnimations(const AnimFile& file, SkinnedModel& out);

// Parent-relative bone transforms for every node: the blendable pose representation
// (UE3 FBoneAtom space). Layering (crossfade, per-bone masks, additive overlays, aim offsets)
// happens here, before skinning.
struct LocalPose {
    std::vector<core::Vec3> t, s;
    std::vector<core::Quat> r;
    size_t size() const { return r.size(); }
    bool empty() const { return r.empty(); }
};

// Sample `clip` at `timeSec`. Full clips start from the bind/rest pose; additive (ADD_) clips
// start from identity, so the result is a pure delta for addPose().
void samplePose(const SkinnedModel& model, int clip, float timeSec, bool loop, LocalPose& out,
                bool additive = false);
// out = lerp/slerp(a, b, alpha * mask[bone]); mask null = all bones. `out` may alias a or b.
void blendPose(const LocalPose& a, const LocalPose& b, float alpha, LocalPose& out,
               const std::vector<float>* mask = nullptr);
// As blendPose with a per-bone mask, but rotations blend in MESH space (UE3 AnimNodeBlendPerBone
// default, bForceLocalSpaceBlend=false): a masked branch keeps b's model-space orientation even
// when a turns the branch's parent (e.g. upper-body slot over strafe hips). `out` may alias a.
void blendPoseMeshSpace(const SkinnedModel& model, const LocalPose& a, const LocalPose& b, float alpha,
                        const std::vector<float>& mask, LocalPose& out);
// base = base (+) delta * weight * mask[bone]  (translation add, rotation base*delta, scale mul).
void addPose(LocalPose& base, const LocalPose& delta, float weight,
             const std::vector<float>* mask = nullptr);
// out = the delta that turns `ref` into `p` under addPose (p.t-ref.t, inv(ref.r)*p.r, p.s/ref.s).
void deltaPose(const LocalPose& ref, const LocalPose& p, LocalPose& out);
// Quaternion helpers (Hamilton product, axis-angle with a unit axis).
core::Quat quatMul(const core::Quat& a, const core::Quat& b);
core::Quat quatAxisAngle(const core::Vec3& axis, float angle);
core::Quat quatSlerp(const core::Quat& a, const core::Quat& b, float t);
core::Vec3 quatRotate(const core::Quat& q, const core::Vec3& v);
// Model-space rotation of `node` under `pose` (composed parent chain; scale ignored).
core::Quat meshRotation(const SkinnedModel& model, const LocalPose& pose, int node);
// Skel-control style edit: rotate `node` by `meshRot` about its own pivot and move it by
// `meshOffset`, both expressed in model space; descendants follow.
void applyMeshSpace(const SkinnedModel& model, LocalPose& pose, int node, const core::Quat& meshRot,
                    const core::Vec3& meshOffset);
// Per-node weight: 1 for `rootNode` and its descendants, 0 elsewhere.
std::vector<float> subtreeMask(const SkinnedModel& model, int rootNode);
// Resolve hierarchy into `global` (per-node model-space matrices) and CPU-skin into `outMesh`.
void skinPose(const SkinnedModel& model, const LocalPose& pose,
              std::vector<core::Mat4>& global, render::MeshData& outMesh);
// Hierarchy only (no skinning): per-node model-space matrices for `pose`.
void poseGlobals(const SkinnedModel& model, const LocalPose& pose, std::vector<core::Mat4>& global);

// Evaluate `clip` at `timeSec` and CPU-skin into `outMesh` (positions+normals+indices).
// `scratch` is reused across calls (per-node global matrices); pass a persistent vector.
void evaluatePose(const SkinnedModel& model, int clip, float timeSec,
                  std::vector<core::Mat4>& scratch, render::MeshData& outMesh, bool loop = true);

} // namespace assets
