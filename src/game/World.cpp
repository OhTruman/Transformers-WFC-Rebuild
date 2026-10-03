#include "game/World.h"
#include "game/DamageTarget.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "platform/Image.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace game {

// Systems section profiler (WFC_SYSPROF=1): CPU ms per section, averaged over 120 drawn frames.
namespace sysprof {
enum Sec { Ctrl, Hitscan, Anim, WpnPres, Vehicle, FxTick, Cues, DrawPlayer, DrawWeapon, DrawFx, DrawVfx, Count };
const char* kNames[Count] = {"ctrl", "hitscan", "anim", "wpnPres", "vehicle", "fxTick", "cues",
                             "drawPlayer", "drawWeapon", "drawFx", "drawVfx"};
double acc[Count] = {};
int shots = 0;
const bool on = std::getenv("WFC_SYSPROF") != nullptr;
struct Scope {
    Sec s; std::chrono::steady_clock::time_point t0;
    explicit Scope(Sec s_) : s(s_), t0(on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~Scope() { if (on) acc[s] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
};
size_t cueInst = 0, cuePending = 0;
void frame(size_t particles, size_t meshes) {
    static int frames = 0;
    if (!on || ++frames < 120) return;
    char buf[512]; int n = 0;
    for (int i = 0; i < Count; ++i) n += std::snprintf(buf + n, sizeof(buf) - n, " %s=%.2f", kNames[i], acc[i] / frames);
    LOG_INFO("SYSPROF ms/frame:%s | shots=%d particles=%zu meshes=%zu cues=%zu pending=%zu", buf, shots, particles,
             meshes, cueInst, cuePending);
    for (double& a : acc) a = 0; frames = 0; shots = 0;
}
} // namespace sysprof

static std::string assetRoot() {
    if (const char* e = std::getenv("WFC_ASSETS")) return std::string(e);
    return core::config::kAssetRootDefault;
}

static bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void World::load(render::IRenderer& renderer) {
    if (loadVerticalSlice(renderer)) {
        usingSlice_ = true;
        LOG_INFO("World: loaded vertical slice from %s", assetRoot().c_str());
    } else {
        usingSlice_ = false;
        LOG_WARN("World: vertical slice unavailable; using graybox arena");
        buildGraybox();
    }
}

bool World::loadVerticalSlice(render::IRenderer& renderer) {
    const std::string root = assetRoot();
    render::MeshData mapMesh;
    renderer.loadMapRenderData("MP_IAC_Streets");   // original-data shader path (if generated)

    bool okMap = assets::loadGlb(root + "/Maps/MP_IAC_Streets/world.glb", mapMesh);
    bool okRobot = assets::loadSkinnedGlb(root + "/Characters/Optimus/robot.glb", robotModel_);
    bool okVeh = assets::loadSkinnedGlb(root + "/Characters/Optimus/vehicle.glb", vehicleModel_);
    if (!okMap || !okRobot || !okVeh) return false;

    // Resolve base-colour textures (decode PNG -> upload GL texture), cached by URI.
    std::map<std::string, render::TextureHandle> texCache;
    int loaded = 0, failed = 0;
    auto resolveOne = [&](const std::string& uri) -> render::TextureHandle {
        if (uri.empty()) return render::kInvalidTexture;
        auto it = texCache.find(uri);
        if (it != texCache.end()) return it->second;
        render::ImageData img;
        render::TextureHandle th = render::kInvalidTexture;
        if (platform::decodeImage(uri, img)) { th = renderer.uploadTexture(img); ++loaded; }
        else ++failed;
        texCache[uri] = th;
        return th;
    };
    auto resolveTextures = [&](std::vector<render::Material>& mats) {
        for (render::Material& M : mats) {
            M.tex = resolveOne(M.baseColorUri);
            M.emissiveTexHandle = resolveOne(M.emissiveUri);   // WFC glow (may be absent)
        }
    };
    resolveTextures(mapMesh.mats);
    resolveTextures(robotModel_.mats);
    resolveTextures(vehicleModel_.mats);
    LOG_INFO("textures: %d loaded, %d failed, %zu unique", loaded, failed, texCache.size());

    // Baked lightmap atlases: resolve each submesh's _LM atlas name to a GL texture.
    const std::string lmDir = root + "/Maps/MP_IAC_Streets/lightmaps/";
    int lmBound = 0;
    for (render::SubMesh& sm : mapMesh.subs) {
        if (sm.lightmapName.empty()) continue;
        sm.lightmapTex = resolveOne(lmDir + sm.lightmapName + ".png");
        if (sm.lightmapTex >= 0) ++lmBound;
    }
    LOG_INFO("lightmaps: %d/%zu submeshes bound to atlases", lmBound, mapMesh.subs.size());

    mapMesh_ = renderer.uploadMesh(mapMesh);
    fx_.load(renderer, root + "/../content/");
    fx_.loadMeshes(renderer, root + "/../content/");
    levelFx_.load(renderer, root + "/Maps/MP_IAC_Streets/map_fx.json", root + "/../content/");
    vehicleFx_.load(renderer, root + "/../content/");
    player_.pawn().setFormModels(&robotModel_, &vehicleModel_);

    // Ion Blaster: animated skeletal mesh held at the robot's primary weapon socket (falls back
    // to the static bind-pose mesh if the skinned load fails).
    render::MeshData weaponMesh;
    bool okWeaponSkin = assets::loadSkinnedGlb(root + "/Weapons/IonBlaster/weapon.glb", weaponModel_);
    if (okWeaponSkin) {
        resolveTextures(weaponModel_.mats);
        weaponAnim_.setModel(&weaponModel_);
    }
    if (okWeaponSkin || assets::loadGlb(root + "/Weapons/IonBlaster/weapon.glb", weaponMesh)) {
        if (!okWeaponSkin) {
            resolveTextures(weaponMesh.mats);
            weaponMesh_ = renderer.uploadMesh(weaponMesh);
        }
        int bone = robotModel_.nodeByName("R_Arm03_Elbow_XB");
        // [CONF] WeaponSocket_Primary relative transform (character.json): loc_ue [-40,0,0],
        // rot_ue [pitch 0, yaw 31311, roll 5461] -> gltf-space socket matrix (column-major).
        float sm[16] = {-0.990259f, 0.0f,       0.139234f, 0.0f,
                        -0.069613f, 0.866041f, -0.495102f, 0.0f,
                        -0.120583f, -0.499972f, -0.857606f, 0.0f,
                        -0.4f,      0.0f,       0.0f,       1.0f};
        core::Mat4 off = core::mat4FromArray(sm);
        player_.pawn().setWeaponSocket(bone, off);
        LOG_INFO("weapon: Ion Blaster loaded, socket bone R_Arm03_Elbow_XB node=%d", bone);
    }

    // Collision: load the dedicated collision mesh (solid BSP + BlockingVolume hulls).
    render::MeshData colMesh;
    if (assets::loadGlb(root + "/Maps/MP_IAC_Streets/collision.glb", colMesh)) {
        collision_.build(colMesh);
        renderer.setVisibilityQuery([this](const core::Vec3& a, const core::Vec3& b) {
            // segmentHit walks only the grid cells the ray crosses and stops at the first hit, so
            // long light-visibility rays no longer need to be marched in 2 m pieces.
            float t;
            return collision_.segmentHit(a, b, t);
        });
        killZ_ = collision_.boundsMin().y - 25.0f;   // fell out of the world
    }

    // Place the player at an authored free-for-all start, facing the play area.
    spawnPos_ = {0, 0, 0};
    spawnYaw_ = 0.0f;
    if (loadSpawn(root + "/Maps/MP_IAC_Streets/spawnpoints.json", spawnPos_, spawnYaw_)) {
        LOG_INFO("World: spawn at %.1f, %.1f, %.1f facing yaw %.2f",
                 spawnPos_.x, spawnPos_.y, spawnPos_.z, spawnYaw_);
    }
    respawnPlayer();
    if (std::getenv("WFC_STARTVEHICLE")) player_.pawn().setForm(Form::Vehicle);  // for vehicle tests

    // A couple of pickups near spawn for visual life, plus a weapon-test dummy.
    actors_.clear();
    actors_.push_back(std::make_unique<Pickup>(spawnPos_ + core::Vec3{3, 0, 0}, Pickup::Kind::Health));
    actors_.push_back(std::make_unique<Pickup>(spawnPos_ + core::Vec3{-3, 0, 2}, Pickup::Kind::Ammo));
    {
        core::Vec3 d = spawnPos_ + core::forwardFromYawPitch(spawnYaw_, 0.0f) * 10.0f;
        if (collision_.valid()) { float gy; core::Vec3 n; if (collision_.groundHeight(d.x, d.z, d.y + 0.5f, 1.5f, gy, n)) d.y = gy; }
        actors_.push_back(std::make_unique<DamageTarget>(d));
    }
    return true;
}

void World::respawnPlayer() {
    core::Vec3 p = spawnPos_;
    // Snap to the floor at the authored spawn. Keep the window tight (<=1.5 m above the
    // authored Y) so dense prop collision overhead (walkways, ceilings) is NOT grabbed.
    if (collision_.valid()) {
        float gy; core::Vec3 n;
        if (collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, n)) p.y = gy;
    }
    player_.pawn().setPosition(p);
    player_.pawn().setYaw(spawnYaw_);
    player_.pawn().velocity() = {0, 0, 0};
    player_.pawn().setHoverApplied(0.0f);   // placed on the floor
    player_.pawn().groundY = spawnPos_.y;
    player_.controller().setCameraYaw(spawnYaw_);
}

bool World::loadSpawn(const std::string& path, core::Vec3& outPos, float& outYaw) {
    std::string text;
    if (!readTextFile(path, text)) return false;
    assets::Json root;
    if (!assets::Json::parse(text, root)) return false;
    const assets::Json& points = root["points"];
    if (!points.isArray()) return false;

    // Centroid of all authored player starts == the middle of the playable area.
    core::Vec3 centroid{0, 0, 0};
    int nStarts = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        const std::string& cls = points[i]["class"].asString();
        if ((cls == "TnFreeForAllPlayerStart" || cls == "TnTeamPlayerStart") &&
            points[i]["location_gltf"].size() >= 3) {
            const assets::Json& L = points[i]["location_gltf"];
            centroid += core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
            ++nStarts;
        }
    }
    if (nStarts > 0) centroid = centroid * (1.0f / nStarts);

    // WFC_SPAWN_INDEX picks the Nth matching start (for traversal/region screenshots).
    int wantIdx = 0;
    if (const char* e = std::getenv("WFC_SPAWN_INDEX")) wantIdx = std::atoi(e);
    auto pick = [&](const char* cls) -> bool {
        int seen = 0, total = 0;
        for (size_t i = 0; i < points.size(); ++i)
            if (points[i]["class"].asString() == cls && points[i]["location_gltf"].size() >= 3) ++total;
        if (total == 0) return false;
        int target = ((wantIdx % total) + total) % total;
        for (size_t i = 0; i < points.size(); ++i) {
            const assets::Json& pt = points[i];
            if (pt["class"].asString() == cls && pt["location_gltf"].size() >= 3) {
                if (seen++ != target) continue;
                const assets::Json& L = pt["location_gltf"];
                outPos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
                // Face from the spawn toward the play-area centroid (robust to yaw convention).
                core::Vec3 d = centroid - outPos;
                d.y = 0;
                if (core::length(d) > 1.0f) outYaw = std::atan2(-d.x, -d.z);
                return true;
            }
        }
        return false;
    };
    if (pick("TnFreeForAllPlayerStart")) return true;
    if (pick("TnTeamPlayerStart")) return true;
    if (pick("TnSpawnCluster")) return true;
    outYaw = 0.0f;
    return false;
}

void World::buildGraybox() {
    blocks_.clear();
    auto add = [&](float x, float z, float w, float h, float d, core::Vec3 col) {
        blocks_.push_back(Block{core::Vec3{x, h * 0.5f, z}, core::Vec3{w, h, d}, col});
    };
    core::Vec3 grey{0.45f, 0.47f, 0.5f};
    core::Vec3 grey2{0.38f, 0.4f, 0.44f};
    add(10, 0, 4, 2, 4, grey);
    add(-8, 6, 6, 4, 6, grey2);
    add(0, 18, 10, 1, 10, grey);
    add(16, -12, 3, 6, 3, grey2);
    add(-14, -10, 5, 3, 5, grey);
    add(6, -20, 8, 1.5f, 2, grey2);
    add(-4, -4, 3, 0.5f, 6, core::Vec3{0.5f, 0.42f, 0.3f});

    actors_.clear();
    actors_.push_back(std::make_unique<Pickup>(core::Vec3{4, 0, 4}, Pickup::Kind::Health));
    actors_.push_back(std::make_unique<Pickup>(core::Vec3{-6, 0, 10}, Pickup::Kind::Ammo));
    actors_.push_back(std::make_unique<Pickup>(core::Vec3{12, 0, -8}, Pickup::Kind::Ability));

    player_.pawn().setPosition(core::Vec3{0, 0, 0});
    player_.pawn().setYaw(0.0f);
    player_.pawn().groundY = 0.0f;
    LOG_INFO("Graybox arena built: %zu blocks, %zu actors", blocks_.size(), actors_.size());
}

void World::handleInput(const platform::InputFrame& in, float dt) {
    player_.controller().handleInput(in, dt);   // Dash (Shift) is latched by PlayerController
}

static bool rayAabb(const core::Vec3& o, const core::Vec3& d, float len,
                    const core::Vec3& bmin, const core::Vec3& bmax, float& tHit) {
    float tmin = 0.0f, tmax = len;
    const float* od = &o.x; const float* dd = &d.x;
    const float* lo = &bmin.x; const float* hi = &bmax.x;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-6f) { if (od[i] < lo[i] || od[i] > hi[i]) return false; }
        else {
            float inv = 1.0f / dd[i];
            float t1 = (lo[i] - od[i]) * inv, t2 = (hi[i] - od[i]) * inv;
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            tmin = t1 > tmin ? t1 : tmin;
            tmax = t2 < tmax ? t2 : tmax;
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

void World::fireHitscan(const core::Vec3& origin, const core::Vec3& dirIn) {
    sysprof::Scope spHit(sysprof::Hitscan);
    ++sysprof::shots;
    const Weapon& w = player_.pawn().weapon();
    const float range = w.rangeM;   // [CONF] 300 m

    // Apply per-shot spread as a small random cone around the aim direction.
    core::Vec3 dir = core::normalize(dirIn);
    {
        core::Vec3 up{0, 1, 0};
        core::Vec3 rt = core::normalize(core::cross(dir, up));
        core::Vec3 u2 = core::normalize(core::cross(rt, dir));
        auto rf = [] { return (float)std::rand() / (float)RAND_MAX * 2.0f - 1.0f; };
        // Fine aim scales spread by the weapon's FineAimSpreadModifier (Ion Blaster 0.5) [CONF data].
        float spread = w.spread * (player_.pawn().fineAiming() ? core::config::kFineAimSpreadMult : 1.0f);
        dir = core::normalize(dir + rt * (rf() * spread) + u2 * (rf() * spread));
    }
    core::Vec3 end = origin + dir * range;

    float bestDist = range;
    // World geometry.
    if (collision_.valid()) {
        float t;
        if (collision_.segmentHit(origin, end, t)) bestDist = range * t;
    }
    // Damageable targets (closest wins).
    DamageTarget* hitTarget = nullptr;
    float targetDist = bestDist;
    for (auto& a : actors_) {
        auto* tgt = dynamic_cast<DamageTarget*>(a.get());
        if (!tgt || !tgt->alive()) continue;
        core::Vec3 c = tgt->position() + core::Vec3{0, tgt->halfExtent().y, 0};
        core::Vec3 bmin = c - tgt->halfExtent(), bmax = c + tgt->halfExtent();
        float th;
        if (rayAabb(origin, dir, range, bmin, bmax, th) && th < targetDist) { targetDist = th; hitTarget = tgt; }
    }
    float dist = hitTarget ? targetDist : bestDist;
    core::Vec3 hitPoint = origin + dir * dist;
    if (hitTarget) hitTarget->applyDamage(w.damageAt(dist));   // [CONF] range-based falloff

    // Tracer from the barrel muzzle to the impact point. The muzzle is the weapon-local barrel
    // tip transformed by the weapon's world matrix (not the hand attach point), so the tracer and
    // flash leave the end of the gun rather than the fist.
    core::Vec3 muzzle = origin;
    core::Mat4 ms;
    if (weaponSocketWorld("MuzzleFlash", ms)) {
        muzzle = {ms.m[12], ms.m[13], ms.m[14]};          // [CONF] MuzzleFlash socket
    } else if (player_.pawn().hasWeapon()) {
        const core::Mat4& wm = player_.pawn().weaponWorld();
        muzzle = core::transformPoint(wm, core::Vec3{core::config::kMuzzleLocalX,
                     core::config::kMuzzleLocalY, core::config::kMuzzleLocalZ});
    }
    // WP_Fire presentation: muzzle flash at the MuzzleFlash socket, tracer muzzle -> impact,
    // impact squib where the trace hit something (world or target).
    if (weaponSocketWorld("MuzzleFlash", ms)) fx_.spawnMuzzleFlash(ms);
    fx_.spawnTracer(muzzle, hitPoint);
    if (dist < range - 0.01f)
        fx_.spawnImpact(hitPoint, dir * -1.0f, origin);
    if (std::getenv("WFC_MUZZLELOG") && player_.pawn().hasWeapon()) {
        const core::Mat4& wm = player_.pawn().weaponWorld();
        LOG_INFO("MUZZLE hand=%.2f,%.2f,%.2f tip=%.2f,%.2f,%.2f (|offset|=%.2fm) aimYaw=%.3f legYaw=%.3f",
                 wm.m[12], wm.m[13], wm.m[14], muzzle.x, muzzle.y, muzzle.z,
                 core::length(muzzle - core::Vec3{wm.m[12], wm.m[13], wm.m[14]}), player_.pawn().yaw(),
                 player_.pawn().legYaw());
    }
    // WP_Fire / WP_LowAmmoFire SoundCue (LowAmmoThreshold 5). kSmartPan_PreferPlayer: the
    // distance-layer parameter is measured from the owning player, not the camera.
    float ownDist = core::length(muzzle - origin);
    SoundCues::Emitter me = atWeapon("MuzzleFlash");
    me.pos = muzzle;
    cues_.play(w.lowAmmo() ? "SHOOT_LOW_AMMO" : "SHOOT", me, ownDist);
    burstActive_ = true; sinceShot_ = 0.0f;
    // DefaultImpactSound (world) / damage impact cue at the hit point.
    if (hitTarget) cues_.play("IMPT_DMG", hitPoint, core::length(hitPoint - listenerPos_));
    else if (dist < range - 0.01f) cues_.play("IMPT_WORLD", hitPoint, core::length(hitPoint - listenerPos_));
}

void World::setAudio(audio::IAudio* a) {
    audio_ = a;
    if (!a) return;
    const std::string base = assetRoot() + "/../content/";
    // All audio = the original SoundCues (weapon, vehicle, robot movement, transformation, fine aim).
    cues_.load(a, base);
    ambient_.load(assetRoot() + "/Maps/MP_IAC_Streets/audio.json", base, cues_, a);
    // Occlusion line check listener -> source against the world collision. Attached (player-owned)
    // sounds are tested against the pawn's body (mesh origin + 1.5 m), not the socket tip, which can
    // poke into walls (the arm / gun have no collision). The last 0.5 m at the source and 0.25 m at
    // the listener are ignored so the floor under the feet or an emitter's mounting surface does
    // not count as an occluder [MED].
    cues_.setOcclusion([this](const core::Vec3& from, const core::Vec3& to, int owner) {
        if (!collision_.valid()) return false;
        ++occlusionRays_;
        const Character& pc = player_.pawn();
        core::Vec3 src = owner >= 0 ? pc.position() + pc.meshOffset() + core::Vec3{0, 1.5f, 0}
                                    : to + core::Vec3{0, 0.5f, 0};
        core::Vec3 d = src - from;
        float len = core::length(d);
        if (len < 1.0f) return false;
        core::Vec3 n = d * (1.0f / len);
        float t;
        return collision_.segmentHit(from + n * 0.25f, src - n * 0.5f, t);
    });
    // Attached cues follow their owner every tick (see resolveCueOwner); world cues stay put.
    cues_.setResolver([this](int owner, const std::string& socket, const core::Vec3& off, core::Vec3& out) {
        return resolveCueOwner(owner, socket, off, out);
    });
}

// Current world position of an attached AudioComponent. Pawn: the skeletal mesh origin (the
// transform shift included) or, with a socket, that bone of the displayed skeleton; `offset` is
// added in world space (the truck's AUDIO_ROOT is C_Reference_XR + 147.25 UU up). Weapon: the
// Ion Blaster mesh or one of its sockets; while it is holstered (transform), the pawn carrying it
// [MED: chest height]. A pawn bone missing on the displayed skeleton falls back to the mesh origin.
bool World::resolveCueOwner(int owner, const std::string& socket, const core::Vec3& offset, core::Vec3& out) const {
    const Character& pc = player_.pawn();
    if (owner == kOwnPawn) {
        core::Mat4 bm;
        if (!socket.empty() && pc.boneWorld(socket, bm)) { out = core::Vec3{bm.m[12], bm.m[13], bm.m[14]} + offset; return true; }
        out = pc.position() + pc.meshOffset() + offset;
        return true;
    }
    if (owner == kOwnWeapon) {
        // Holstered (mid-transform): the gun is carried by the pawn, so its sounds go with the pawn.
        if (!pc.hasWeapon()) { out = pc.position() + pc.meshOffset() + core::Vec3{0, 1.5f, 0}; return true; }
        core::Mat4 ms;
        if (!socket.empty() && weaponSocketWorld(socket.c_str(), ms)) { out = {ms.m[12], ms.m[13], ms.m[14]}; return true; }
        const core::Mat4& wm = pc.weaponWorld();
        out = {wm.m[12], wm.m[13], wm.m[14]};
        return true;
    }
    return false;
}

SoundCues::Emitter World::atPawn(const core::Vec3& up, const char* socket) const {
    SoundCues::Emitter e{player_.pawn().position(), kOwnPawn, up, socket};
    resolveCueOwner(e.owner, e.socket, e.offset, e.pos);
    return e;
}

SoundCues::Emitter World::atWeapon(const char* socket) const {
    SoundCues::Emitter e{player_.pawn().position(), kOwnWeapon, {0, 0, 0}, socket};
    resolveCueOwner(e.owner, e.socket, e.offset, e.pos);
    return e;
}

// Robot / transformation / fine-aim audio, attached to the pawn or its weapon.
//   Transformation [CONF Optimus_ROBO_ANIM]: Transform_ToVehicle_ROBO HmAnimNotify_Sound
//   BL_TRANSFORM.OPTIMUS_BOT2VEH @0.125 s of 2.0 s; Transform_ToRobot_ROBO OPTIMUS_VEH2BOT @0.0 (MinWeight 0).
//   No SocketName: the component sits on the pawn's skeletal mesh and moves with it for the whole
//   layered cue (servos 0.0, main 0.15, land thump 0.64, flare 1.44, air release 1.59, finish 1.67 s).
//   Fine aim [CONF TnWeaponIonBlaster]: WP_StartFineAim / WP_EndFineAim -> BL_WPN_GUN_PULSE_RIFLE.
//   FINE_AIM_START / FINE_AIM_END, played on the weapon.
void World::tickCharacterAudio(float dt) {
    const Character& pc = player_.pawn();
    bool tf = pc.isTransforming();
    if (tf && !prevTransforming_) { transformCuePlayed_ = false; transformTarget_ = pc.moveForm(); }
    if (tf && !transformCuePlayed_) {
        const bool toVehicle = transformTarget_ == Form::Vehicle;
        const float at = toVehicle ? 0.125f / 2.0f : 0.0f;          // notify time / authored length
        if (pc.transformProgress() >= at) {
            transformCue_ = cues_.play(toVehicle ? "BL_TRANSFORM.OPTIMUS_BOT2VEH" : "BL_TRANSFORM.OPTIMUS_VEH2BOT", atPawn(), 0.0f);
            transformCuePlayed_ = true;
        }
    }
    prevTransforming_ = tf;
    static const bool track = std::getenv("WFC_CUETRACK") != nullptr;   // attachment check: cue vs pawn
    core::Vec3 tp;
    if (track && transformCue_ >= 0 && cues_.instancePos(transformCue_, tp) && (trackT_ += dt) >= 0.25f) {
        trackT_ = 0.0f;
        core::Vec3 pp = pc.position() + pc.meshOffset();
        LOG_INFO("CUETRACK transform cue pos=%.2f,%.2f,%.2f pawn=%.2f,%.2f,%.2f |d|=%.3f m", tp.x, tp.y, tp.z, pp.x, pp.y,
                 pp.z, core::length(tp - pp));
    }

    static const bool spatialLog = std::getenv("WFC_SPATIALLOG") != nullptr;
    static float spatialT = 0.0f;
    if (spatialLog && (spatialT += dt) >= 0.25f) {
        spatialT = 0.0f;
        cues_.logSpatial(pc.isTransforming() ? "transform" : (pc.form() == Form::Vehicle ? "vehicle" : "robot"),
                         pc.position() + pc.meshOffset(), false);
    }

    foleyCues_.clear();
    robotFoley_.tick(pc, dt, foleyCues_);
    for (const char* c : foleyCues_) cues_.play(c, atPawn(), 0.0f);
    static const bool foleyLog = std::getenv("WFC_FOLEYLOG") != nullptr;
    if (foleyLog) {
        const core::Vec3& v = pc.velocity();
        LOG_INFO("FOLEY clip=%s t=%.6f phase=%.3f w=%.2f spd=%.2f grounded=%d land=%s fall=%.0fUU%s%s", pc.animName(),
                 pc.animTime(), pc.locoPhase(), pc.locoMasterWeight(), std::sqrt(v.x * v.x + v.z * v.z),
                 (int)pc.onGround(), robotFoley_.lastLandClip(), robotFoley_.lastFallHeightUU(),
                 foleyCues_.empty() ? "" : " -> ", foleyCues_.empty() ? "" : foleyCues_[0]);
    }

    bool fa = pc.fineAiming() && pc.hasWeapon();
    if (fa != prevFineAim_)
        cues_.play(fa ? "BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_START" : "BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_END",
                   atWeapon(), 0.0f);
    prevFineAim_ = fa;
}

// Vehicle-form presentation. [CONF] TR_Optimus_VEHDEF_p.OptimusTruckForm:
//   BoostFx: BoostSocket_L / BoostSocket_R -> FX_Navigation_p.bumble_boost_small1_FX
//   HoverFX: 6 x HoverBooster_* (on the wheel bones) -> FX_Navigation_p.CarHover_A_01_FX
//   JumpFX:  JumpBoostSocket_C/R/L -> FX_Navigation_p.Jump_FX (one-shot, 0.5 s)
//   AudioComp (HmPlayerVehicleAudioComponent_6670): BoostSound Auto_Boost_Start, BoostLoops
//   Auto_Boost_Loop, BoostStopSound Auto_Boost_End, BoostWheelsSound Auto_Boost_Wheels,
//   BoostFadeOutTime 0.15 s, BoostWheelsGroundCheckDelay 0.27 s; Veh_Optimus_Prime_SoundSet maps
//   those events to BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_START / _LOOP / _END / _WHEELS.
// Boost state = the movement code's boost condition (Gameplay's TnCarForm Driving state), outside
// transforms.
// Hover state [MED]: the truck rides its HoverBlueprint (wheel-mounted hover thrusters) except while
// boosting, when it drops to its wheels (the audio component's Hover vs Boost/Wheels land sounds
// and the boost wheels peel-out). Jump [MED]: vehicle take-off with upward velocity.
void World::tickVehicleBoost(float dt) {
    Character& pc = player_.pawn();
    bool vehicle = pc.form() == Form::Vehicle && !pc.isTransforming();
    bool boost = vehicle && pc.vehicleState().driving;
    // [CONF] Transform_ToVehicle_VEH TnAnimNotify_ToggleVehicleFx (enable) @1.8 s of the 2.0 s fold;
    // Transform_ToRobot_VEH disables at 0.0 (= leaving `vehicle` when the fold starts).
    bool fxFold = pc.form() == Form::Vehicle && pc.isTransforming() && pc.moveForm() == Form::Vehicle &&
                  pc.transformProgress() >= 1.8f / 2.0f;
    bool hover = (vehicle && !boost) || fxFold;

    // Socket world matrices: bone (current pose) x socket relative transform (incl. socket scale).
    core::Mat4 sw[VehicleFx::kSocketCount];
    int haveSockets = 0;
    for (int i = 0; i < VehicleFx::kSocketCount; ++i) {
        const VehicleFx::SocketDef& sd = VehicleFx::socketDef(i);
        core::Mat4 bone;
        bool ok = pc.form() == Form::Vehicle && pc.boneWorld(sd.bone, bone);
        if (ok) { sw[i] = bone * core::mat4FromArray(sd.rel); ++haveSockets; }
        vehicleFx_.setSocket(i, ok ? &sw[i] : nullptr);
    }

    // Boost afterburners (looping while held; bKillOnDeactivate).
    if (boost && !boostActive_) {
        boostInst_[0] = vehicleFx_.start(VehicleFx::Boost, VehicleFx::BoostL);
        boostInst_[1] = vehicleFx_.start(VehicleFx::Boost, VehicleFx::BoostR);
    } else if (!boost && boostActive_) {
        for (int& id : boostInst_) { vehicleFx_.deactivate(id); id = -1; }
    }
    boostActive_ = boost;
    // Hover thrusters on all six wheel sockets.
    if (hover && !hoverActive_) {
        for (int i = 0; i < 6; ++i) hoverInst_[i] = vehicleFx_.start(VehicleFx::Hover, VehicleFx::HoverLBack + i);
    } else if (!hover && hoverActive_) {
        for (int& id : hoverInst_) { vehicleFx_.deactivate(id); id = -1; }
    }
    hoverActive_ = hover;
    // Jump boosters: one-shot burst on vehicle take-off; killed if the vehicle form ends.
    bool grounded = pc.onGround();
    bool tookOff = vehicle && vehiclePrevGrounded_ && !grounded && pc.velocity().y > 2.0f;
    bool landed = vehicle && !vehiclePrevGrounded_ && grounded;
    (void)landed;
    if (tookOff) {
        jumpInst_[0] = vehicleFx_.start(VehicleFx::Jump, VehicleFx::JumpC);
        jumpInst_[1] = vehicleFx_.start(VehicleFx::Jump, VehicleFx::JumpR);
        jumpInst_[2] = vehicleFx_.start(VehicleFx::Jump, VehicleFx::JumpL);
        jumpCount_++;
    }
    if (!vehicle) for (int& id : jumpInst_) if (id >= 0) { vehicleFx_.deactivate(id); id = -1; }
    vehiclePrevGrounded_ = grounded;

    // Nitro / ram: DASH while driving on wheels (= boosting). Gameplay's movement code owns the
    // nitro timer/cooldown and the speed/steering scales; Systems follows that state for RamFX,
    // audio and the ram-hit registry.
    VehicleNitro::Event ne = nitro_.follow(pc.vehicleState().nitroRemain > 0.0f);
    if (ne == VehicleNitro::Event::Started) {
        ramInst_ = vehicleFx_.start(VehicleFx::Ram, VehicleFx::RamSocket);
        // StartNitro: RamFX, NitroForceFeedback, then PlayNitroSound (VehicleAudio below). No script calls
        // PlayCustomLoopingSound (Auto_Ram_Alert) [CONF: decompiled TransGame / HM_Engine], so it is not played.
    } else if (ne == VehicleNitro::Event::Stopped) {
        vehicleFx_.deactivate(ramInst_);
        ramInst_ = -1;
    }
    vehicleFx_.tick(dt);

    // Vehicle audio component (HmVehicleAudioComponent + HmPlayerVehicleAudioComponentImpl port), attached
    // at AUDIO_ROOT (C_Reference_XR + 147.25 UU up).
    const core::Vec3& v = pc.velocity();
    float mph = core::length(v) * 2.23694f;
    {
        VehicleAudio::Input in;
        in.entered = vehicle;
        in.boosting = boost;
        in.onGround = grounded;
        const float fwdIn = player_.controller().moveForwardInput();
        if (!boost) vehLoadState_ = fwdIn > 0.01f ? 1 : (fwdIn < -0.01f ? 2 : 0);   // Hovering.UpdateSounds
        in.loadState = vehLoadState_;
        // WheelSlipRatio: 0 while hovering [CONF]; CarSimulation.SlipAngle while driving, which only Gameplay
        // can provide (World::setTireSlipAngle); none -> 0.
        in.wheelSlip = boost && tireSlipOverride_ >= 0.0f ? tireSlipOverride_ : 0.0f;
        tireSlip_ = in.wheelSlip;
        in.velocity = v;
        in.forward = core::forwardFromYawPitch(pc.yaw(), 0.0f);
        in.ascend = tookOff;
        const bool dashing = pc.vehicleState().dashRemain > 0.0f;
        in.booster = vehicle && !boost && dashing && !prevDashing_;              // TnTruckForm.Hovering.DoDash
        prevDashing_ = dashing;
        in.nitro = ne == VehicleNitro::Event::Started;
        vehicleAudio_.tick(dt, in, cues_, [this] { return atPawn({0, 1.4725f, 0}); });
    }

    if (std::getenv("WFC_BOOSTLOG")) {
        static int n = 0;
        if (n % 6 == 5 && pc.form() == Form::Vehicle)
            LOG_INFO("NITRO active=%d remaining=%.2f cooldown=%.2f speedScale=%.1f steeringScale=%.1f",
                     (int)nitro_.nitroActive(), pc.vehicleState().nitroRemain, pc.vehicleState().nitroCooldown,
                     nitro_.speedScale(), nitro_.steeringScale());
        if (++n % 6 == 0 && pc.form() == Form::Vehicle) {
            LOG_INFO("VFX boost=%d hover=%d jumps=%d parts=%zu mph=%.1f ground=%d vy=%.2f sockets=%d/%d slip=%.3f engine=%s avgMph=%.1f",
                     (int)boost, (int)hover, jumpCount_, vehicleFx_.liveParticles(), mph, (int)grounded,
                     v.y, haveSockets, (int)VehicleFx::kSocketCount, tireSlip_, vehicleAudio_.engineState(),
                     vehicleAudio_.speedMph());
            for (int i : {(int)VehicleFx::BoostL, (int)VehicleFx::HoverLFront, (int)VehicleFx::JumpC}) {
                if (pc.form() != Form::Vehicle) break;
                core::Vec3 rel = core::Vec3{sw[i].m[12], sw[i].m[13], sw[i].m[14]} - pc.position();
                core::Vec3 xa = core::normalize(core::Vec3{sw[i].m[0], sw[i].m[1], sw[i].m[2]});
                LOG_INFO("VFX socket %s rel=%.2f,%.2f,%.2f x-axis=%.2f,%.2f,%.2f scale=%.2f", VehicleFx::socketDef(i).name,
                         rel.x, rel.y, rel.z, xa.x, xa.y, xa.z,
                         core::length(core::Vec3{sw[i].m[0], sw[i].m[1], sw[i].m[2]}));
            }
        }
    }
}


bool World::notifyRamHit(const void* target, const core::Vec3& pos) {
    if (!nitro_.registerRamHit(target)) return false;
    // AttemptToRam -> ServerPlayRammingSound -> ClientPlayRammingSound -> PlayRamSound: the truck's
    // audio component plays RamSound (owner-attached), not a world sound at the hit point [CONF script].
    (void)pos;
    vehicleAudio_.ram(cues_, [this] { return atPawn({0, 1.4725f, 0}); });
    return true;
}

bool World::weaponSocketWorld(const char* socket, core::Mat4& out) const {
    if (!weaponAnim_.valid() || !player_.pawn().hasWeapon()) return false;
    core::Mat4 local;
    if (!weaponAnim_.socketLocal(socket, local)) return false;
    out = player_.pawn().weaponWorld() * local;
    return true;
}

// Weapon-mesh event animations (TnWeaponMesh.WeaponEventAnims) + their AnimNotifies.
void World::tickWeaponPresentation(float dt) {
    const Weapon& w = player_.pawn().weapon();
    if (w.reloadSerial != weaponSeenReload_) { weaponSeenReload_ = w.reloadSerial; weaponAnim_.play(WeaponMesh::Event::Reload); }
    if (w.shotSerial != weaponSeenShot_)     { weaponSeenShot_ = w.shotSerial;     weaponAnim_.play(WeaponMesh::Event::Fire); }
    notifies_.clear();
    weaponAnim_.tick(dt, notifies_);
    if (player_.pawn().hasWeapon())
        for (const WeaponNotify& n : notifies_) handleWeaponNotify(n);
}

void World::handleWeaponNotify(const WeaponNotify& n) {
    if (std::getenv("WFC_NOTIFYLOG"))
        LOG_INFO("NOTIFY %s %s @%.3f %s", n.kind == WeaponNotify::Kind::Sound ? "sound" : "fx",
                 n.what.c_str(), n.time, n.socket.c_str());
    if (n.kind == WeaponNotify::Kind::Effect) {
        // HmAnimNotify_PlayEffect: spawn the authored ParticleSystem at the notify's socket.
        core::Mat4 sw;
        if (weaponSocketWorld(n.socket.c_str(), sw) && !fx_.spawnNotifyEffect(n.what, sw))
            LOG_WARN("notify effect %s not reconstructed", n.what.c_str());
    }
    if (n.kind == WeaponNotify::Kind::Sound) {
        // HmAnimNotify_Sound: an AudioComponent on the weapon mesh, following it.
        SoundCues::Emitter e = atWeapon();
        const char* name = n.what.c_str();
        const char* dot = std::strrchr(name, '.');
        if (n.what.rfind("BL_WPN_GUN_ION_BLASTER.", 0) == 0 && dot) name = dot + 1;
        cues_.play(name, e, core::length(e.pos - player_.pawn().position()));
    }
}

void World::tick(float dt) {
    static const bool hitchLog = std::getenv("WFC_HITCHLOG") != nullptr;   // game-thread gaps between ticks
    if (hitchLog) {
        static auto last = std::chrono::steady_clock::now();
        static int n = 0;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (++n > 1 && ms > 30.0) LOG_INFO("HITCH tick %d: %.1f ms since the previous tick", n, ms);
    }
    {   // Audio listener = camera (same pose the app hands to IAudio::setListener).
        render::Camera cam;
        player_.controller().updateCamera(cam);
        listenerPos_ = cam.pos;
    }
    { sysprof::Scope sp(sysprof::Ctrl); player_.controller().applyToPawn(*this, dt); }   // also feeds the aim pitch to the pawn
    if (const char* ap = std::getenv("WFC_AIMPITCH"))     // diagnostic: force the aim pitch (rad)
        player_.pawn().setAimPitch((float)std::atof(ap));
    { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }
    { sysprof::Scope sp(sysprof::WpnPres); tickWeaponPresentation(dt); }
    { sysprof::Scope sp(sysprof::Vehicle); tickVehicleBoost(dt); }
    if (std::getenv("WFC_ANIMLOG")) {                       // layering diagnostics
        static int n = 0;
        if (++n % 6 == 0) {
            const Character& pc = player_.pawn();
            const core::Vec3& v = pc.velocity();
            LOG_INFO("ANIM base=%s t=%.2f | aim=%.2f,%.2f w=%.2f | reload w=%.2f | recoil=%d | weapon=%s | spd=%.2f reload=%d ammo=%d",
                     pc.animName(), pc.animTime(), pc.aimYawNorm(), pc.aimPitchNorm(), pc.aimWeight(), pc.reloadWeight(),
                     (int)pc.recoiling(), weaponAnim_.clipName(), std::sqrt(v.x * v.x + v.z * v.z),
                     (int)pc.weapon().reloading(), pc.weapon().ammo);
            LOG_INFO("FX particles=%zu meshes=%zu impacts=%d", fx_.liveParticles(), fx_.liveMeshes(), fx_.liveImpacts());
            core::Mat4 ms;
            if (weaponSocketWorld("MuzzleFlash", ms)) {
                core::Vec3 bx = core::normalize(core::Vec3{ms.m[0], ms.m[1], ms.m[2]});
                const float aimPitch = player_.controller().camPitch();
                core::Vec3 aimDir = core::forwardFromYawPitch(player_.controller().camYaw(), aimPitch);
                float aimYaw = player_.controller().camYaw();
                float barrelYaw = std::atan2(-bx.x, -bx.z);       // same convention as forwardFromYawPitch
                float rel = core::degrees(std::remainder(barrelYaw - aimYaw, 2.0f * core::PI));
                LOG_INFO("AIM barrelPitch=%.1fdeg aimPitch=%.1fdeg barrel.aim=%.2f barrelYaw-aimYaw=%.1fdeg pawnYaw-aimYaw=%.1fdeg",
                         core::degrees(std::asin(bx.y)), core::degrees(aimPitch), core::dot(bx, aimDir), rel,
                         core::degrees(std::remainder(pc.yaw() - aimYaw, 2.0f * core::PI)));
            }
        }
    }
    {
        core::Mat4 ms;
        bool have = weaponSocketWorld("MuzzleFlash", ms);
        sysprof::Scope sp(sysprof::FxTick);
        levelFx_.tick(dt);
        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
    }
    // Event-driven audio via edge detection on pawn state.
    {
        core::Vec3 pp = player_.pawn().position();
        tickCharacterAudio(dt);
        // WP_LoopingTail: the SHOOT_TAIL cue when a burst ends (trigger released / mag empty).
        sinceShot_ += dt;
        const Weapon& w = player_.pawn().weapon();
        if (burstActive_ && sinceShot_ > w.fireInterval * 2.0f) {
            burstActive_ = false;
            SoundCues::Emitter te = atWeapon("MuzzleFlash");   // WP_LoopingTail on the weapon
            cues_.play("SHOOT_TAIL", te, core::length(te.pos - pp));
        }
        static const bool audioTime = std::getenv("WFC_AUDIOTIME") != nullptr;
        if (audioTime && audio_) {
            audio::MixStats ms;
            static int af = 0; ++af;
            if (audio_->mixStats(ms) && ms.lastUpdateMs > 0.5f && (ms.lastUpdateMs != lastAudioMs_))
            {
                lastAudioMs_ = ms.lastUpdateMs;
                LOG_INFO("AUDIOTIME tick %d: last update %d blocks %.3f ms (mixing %.3f ms, rest = waveOut) mix avg %.3f ms/block",
                         af, ms.lastUpdateBlocks, ms.lastUpdateMs, ms.lastMixMs, ms.mixMsPerBlock);
            }
        }
        sysprof::Scope sp(sysprof::Cues);
        cues_.setListener(listenerPos_);
        if (audio_)   // PreferPlayer pan reference: the local pawn's origin (same point pawn-attached sources use)
            audio_->setSmartPanPlayer(player_.pawn().position() + player_.pawn().meshOffset(), true);
        ambient_.tick(dt, listenerPos_, player_.pawn().position(), cues_);
        static const bool ambLog = std::getenv("WFC_AMBLOG") != nullptr;
        static float ambT = 0.0f;
        if (ambLog && (ambT += dt) >= 0.5f) {
            ambT = 0.0f;
            audio::MixStats ms;
            bool have = audio_ && audio_->mixStats(ms);
            LOG_INFO("AMB zone=%s emitters=%d/%d oneShots=%d cues=%zu occluded=%d rays/s=%.0f pending=%zu voices=%d (max %d, dropped %d, stolen %d) wet=%d peak=%.1fdB gr=%.1fdB mix=%.3fms/block levelfx=%zu",
                     ambient_.zoneName(), ambient_.activeEmitters(), ambient_.emitterCount(), ambient_.oneShotsPlayed(),
                     cues_.liveInstances(), cues_.occludedInstances(), occlusionRays_ / 0.5f, cues_.pendingEvents(), have ? ms.voices : -1,
                     have ? ms.peakVoices : -1, have ? ms.droppedVoices : -1, have ? ms.stolenVoices : -1, have ? ms.wetVoices : -1,
                     have ? ms.peakDb : -96.0f, have ? ms.gainReductionDb : 0.0f, have ? ms.mixMsPerBlock : 0.0f,
                     levelFx_.liveParticles());
            occlusionRays_ = 0;
        }
        cues_.tick(dt);
    }

    if (player_.pawn().position().y < killZ_) {
        LOG_INFO("World: player fell out of world; respawning");
        respawnPlayer();
    }
    for (auto& a : actors_) if (a->alive()) a->tick(*this, dt);
    for (size_t i = 0; i < actors_.size();) {
        if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); }
        else ++i;
    }
}

void World::draw(render::IRenderer& r) const {
    if (mapMesh_ != render::kInvalidMesh) {
        r.drawMesh(mapMesh_, core::Mat4::identity(), mapColor_);
    } else {
        r.drawGroundGrid(60.0f, 2.0f, core::Vec3{0.30f, 0.33f, 0.38f});
        for (const auto& b : blocks_) r.drawBox(b.center, b.size, b.color);
    }
    for (const auto& a : actors_) if (a->alive()) a->draw(r);
    { sysprof::Scope sp(sysprof::DrawPlayer); player_.draw(r); }

    // Ion Blaster mesh held at the weapon socket (robot form only).
    {
        sysprof::Scope sp(sysprof::DrawWeapon);
        if (weaponAnim_.valid() && player_.pawn().hasWeapon())
            r.drawDynamicMesh(weaponAnim_.pose(), player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
        else if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
            r.drawMesh(weaponMesh_, player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
    }
    // Weapon + vehicle boost effects last (translucent/additive over the opaque scene).
    { sysprof::Scope sp(sysprof::DrawFx); levelFx_.draw(r); fx_.draw(r); }
    { sysprof::Scope sp(sysprof::DrawVfx); vehicleFx_.draw(r); }
    sysprof::cueInst = cues_.liveInstances(); sysprof::cuePending = cues_.pendingEvents();
    sysprof::frame(fx_.liveParticles(), fx_.liveMeshes());

    // Debug overlay (toggle with B): world bounds, player capsule, aim ray, weapon socket.
    if (core::DebugFlags::get().enabled) {
        auto wireBox = [&](core::Vec3 c, core::Vec3 half, core::Vec3 col) {
            core::Vec3 k[8];
            for (int i = 0; i < 8; ++i)
                k[i] = {c.x + ((i & 1) ? half.x : -half.x), c.y + ((i & 2) ? half.y : -half.y),
                        c.z + ((i & 4) ? half.z : -half.z)};
            const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
            for (auto& ed : e) r.drawLine(k[ed[0]], k[ed[1]], col);
        };
        if (collision_.valid()) {
            core::Vec3 mn = collision_.boundsMin(), mx = collision_.boundsMax();
            wireBox((mn + mx) * 0.5f, (mx - mn) * 0.5f, {0.2f, 0.8f, 1.0f});
        }
        // Live sound sources (human validation of attachment / occlusion): green = pawn-attached,
        // yellow = weapon-attached, blue = world; red when occluded.
        cues_.forEachInstance([&](const core::Vec3& p, int owner, float occl) {
            core::Vec3 col = owner == kOwnPawn ? core::Vec3{0.2f, 1.0f, 0.3f}
                           : owner == kOwnWeapon ? core::Vec3{1.0f, 0.9f, 0.2f} : core::Vec3{0.3f, 0.5f, 1.0f};
            if (occl > 0.5f) col = {1.0f, 0.15f, 0.1f};
            wireBox(p, {0.35f, 0.35f, 0.35f}, col);
        });
        core::Vec3 pp = player_.pawn().position();
        core::Vec3 bs = player_.pawn().boxSize();
        wireBox(pp + core::Vec3{0, bs.y * 0.5f, 0}, bs * 0.5f, {0.3f, 1.0f, 0.4f});
        core::Vec3 eye = pp + core::Vec3{0, core::config::kCamHeight, 0};
        core::Vec3 dir = core::forwardFromYawPitch(player_.pawn().yaw(), 0.0f);
        r.drawLine(eye, eye + dir * 50.0f, {1.0f, 1.0f, 0.2f});
        if (player_.pawn().hasWeapon()) {
            const core::Mat4& wm = player_.pawn().weaponWorld();
            wireBox({wm.m[12], wm.m[13], wm.m[14]}, {0.2f, 0.2f, 0.2f}, {1.0f, 0.3f, 1.0f});
        }
    }

    // Debug beacon: unmissable 20 m magenta pillar at the player + a green foot marker.
    if (std::getenv("WFC_DEBUGCAM")) {
        core::Vec3 pp = player_.pawn().position();
        r.drawBox(pp + core::Vec3{0, 10, 0}, core::Vec3{0.6f, 20.0f, 0.6f}, core::Vec3{1.0f, 0.1f, 0.9f});
        r.drawBox(pp + core::Vec3{0, 0.1f, 0}, core::Vec3{2.0f, 0.2f, 2.0f}, core::Vec3{0.1f, 1.0f, 0.2f});
    }
}

} // namespace game
