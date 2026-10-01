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

bool loadSkinnedGlb(const std::string& path, SkinnedModel& out);

// Evaluate `clip` at `timeSec` and CPU-skin into `outMesh` (positions+normals+indices).
// `scratch` is reused across calls (per-node global matrices); pass a persistent vector.
void evaluatePose(const SkinnedModel& model, int clip, float timeSec,
                  std::vector<core::Mat4>& scratch, render::MeshData& outMesh, bool loop = true);

// ---- layered evaluation (UE3 AnimTree-style: sample -> blend -> skel controls -> skin) ----

// Local (parent-relative) bone transforms for every node.
struct LocalPose {
    std::vector<core::Vec3> t, s;
    std::vector<core::Quat> r;
};

// A model-space post-process applied to one bone's global transform before its children are
// resolved (UE3 SkelControl): global = translate(loc) * pivot(rot about the bone origin) * global.
struct BoneAdjust {
    int node = -1;
    core::Mat4 rot = core::Mat4::identity();   // model-space rotation (about the bone origin)
    core::Vec3 loc{0, 0, 0};                   // model-space translation
};

void bindPose(const SkinnedModel& model, LocalPose& out);
// Overwrite the channels `clip` animates (bind pose elsewhere if `out` was reset with bindPose).
void samplePose(const SkinnedModel& model, int clip, float timeSec, bool loop, LocalPose& out);
// base = lerp(base, over, w * mask[node]) per node (slerp rotations). Empty mask == all nodes.
void blendPose(LocalPose& base, const LocalPose& over, const std::vector<float>& mask, float w);
// Per-node weight 1 for `rootNode` and its whole subtree, 0 elsewhere (AnimNodeBlendPerBone /
// AnimNodeBlendMultiBone with InitTargetStartBone + PerBoneIncrease 1.0).
std::vector<float> subtreeMask(const SkinnedModel& model, int rootNode);
void computeGlobals(const SkinnedModel& model, const LocalPose& pose, std::vector<core::Mat4>& global,
                    const BoneAdjust* adj = nullptr, int nAdj = 0);
void skinMesh(const SkinnedModel& model, const std::vector<core::Mat4>& global, render::MeshData& out);

} // namespace assets
