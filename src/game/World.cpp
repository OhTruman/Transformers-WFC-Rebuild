#include "game/World.h"
#include <set>
#include "core/LoadYield.h"
#include "game/CharacterAudio.h"
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


// WFC_VFX_FAKE: a recording stand-in for Rendering's particle runtime (logs spawn / stop / params) so the per-chassis vehicle
// FX driving can be checked in a build whose renderer has no spawn API yet.
static void bindFakeVehicleFxRuntime(VehicleFxDriver::Runtime& r) {
    static int next = 1;
    r.spawnAt = [](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) {
        LOG_INFO("VFX spawn h=%d %s pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f up=%.2f,%.2f,%.2f", next, t.c_str(), p.x, p.y, p.z, f.x, f.y, f.z, u.x, u.y, u.z);
        return next++;
    };
    r.setTransform = [](int, const core::Vec3&, const core::Vec3&, const core::Vec3&) { return true; };
    r.setParam = [](int h, const std::string& n, const float* v) {
        static int k = 0;
        if (n == "Color" || ++k % 60 == 0) LOG_INFO("VFX param h=%d %s=%.2f,%.2f,%.2f,%.2f", h, n.c_str(), v[0], v[1], v[2], v[3]);
        return true;
    };
    r.stop = [](int h) { LOG_INFO("VFX stop h=%d", h); };
}

// Rendering's IRenderer::prewarmDynamicMesh (agents/rendering M53), detected at compile time like the particle API.
template <class R> auto fxPrewarm(R& r, const render::MeshData& md, int) -> decltype(r.prewarmDynamicMesh(md), void()) { r.prewarmDynamicMesh(md); }
template <class R> void fxPrewarm(R&, const render::MeshData&, long) {}

void World::load(render::IRenderer& renderer) {
    if (std::getenv("WFC_VFX_FAKE") && *std::getenv("WFC_VFX_FAKE")) { VehicleFxDriver::Runtime fr; bindFakeVehicleFxRuntime(fr); setVehicleFxRuntime(fr); }
    else {
        // [integration M08e] Per-chassis vehicle FX (Systems VehicleFxDriver) through Rendering's particle runtime; bound, the
        // hand-made Optimus vehicle FX turn off (nothing drawn twice). The renderer outlives this World.
        VehicleFxDriver::Runtime rt;
        rt.spawnAt = [&renderer](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.spawnParticleEffect(t, p, f, u); };
        rt.setTransform = [&renderer](int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.setParticleEffectTransform(h, p, f, u); };
        rt.setParam = [&renderer](int h, const std::string& n, const float v[4]) { return renderer.setParticleEffectParam(h, n, v); };
        rt.stop = [&renderer](int h) { renderer.stopParticleEffect(h); };
        setVehicleFxRuntime(rt);
    }
    // [Systems M08e] With Rendering's runtime (agents/rendering 38c9ecf / e15862f):
    //   setVehicleFxRuntime({[&renderer](auto& t, auto& p, auto& f, auto& u) { return renderer.spawnParticleEffect(t, p, f, u); },
    //                        [&renderer](int h, auto& p, auto& f, auto& u) { return renderer.setParticleEffectTransform(h, p, f, u); },
    //                        [&renderer](int h, auto& n, const float* v) { return renderer.setParticleEffectParam(h, n, v); },
    //                        [&renderer](int h) { renderer.stopParticleEffect(h); }});
    repairBeamHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) { fireRepairBeamImpl(w, o, d); };
    weaponFireHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) {
        if (w.projectile()) {
            spawnProjectile(o + d * 1.5f, d * w.projSpeed, w, localPlayer_);
            if (player_.pawn().moveForm() == Form::Vehicle) {   // [Systems M08h] the vehicle shot's muzzle flash
                core::Vec3 vm;
                spawnVehicleMuzzleFlash(projectiles_.back().weaponClass, vm);
            }
            {   // [Systems M08d] PlayFiringSound for projectile weapons too (it was only on the instant-hit path)
                const bool vehForm = player_.pawn().moveForm() == Form::Vehicle;
                onWeaponFired(projectiles_.back().weaponClass, w.lowAmmo(), vehForm, o);
            }
            // Fire sends (locked, target): SetTarget(locked ? target : none) [CONF TnWeaponHoming].
            if (w.projHoming && locked_ && lockTarget_ >= 0) projectiles_.back().target = lockTarget_;
        }
        else fireHitscanWith(w, o, d);
    };
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
    renderer.loadMapRenderData(mapName_);   // original-data shader path (if generated)
    core::loadYield("World: map render data");   // [frontend] the loading screen presents between load steps
    // The match's authored rule classes gate rule-dependent presentation (objective bases, Conquest totems,
    // objective-factory effects) exactly as GameInfo.HasRule gates the world state. [integration] Gameplay's
    // match mode now supplies the rules Rendering's interim WFC_GAMERULES stood in for; select the mode with
    // WFC_GAMEMODE.
    renderer.setActiveGameRules(gameRulesForMode(matchMode_));

    renderer_ = &renderer;
    bool okMap = assets::loadGlb(mapDir() + "world.glb", mapMesh);
    core::loadYield("World: world.glb");
    if (!okMap) return false;
    auto resolveOne = [&](const std::string& uri) { return resolveTexture(uri); };
    auto resolveTextures = [&](std::vector<render::Material>& mats) {
        for (render::Material& M : mats) {
            M.tex = resolveOne(M.baseColorUri);
            M.emissiveTexHandle = resolveOne(M.emissiveUri);   // WFC glow (may be absent)
        }
    };
    resolveTextures(mapMesh.mats);
    // The pawn body comes from the selected chassis (applyChassisToLocalPawn). Before any selection the direct boot
    // and the harnesses use the iconic "Truck" (Optimus Prime) export, exactly like a selection of it.
    if (!applyChassisToLocalPawn("Truck")) return false;
    LOG_INFO("textures: %d loaded, %d failed, %zu unique", texLoaded_, texFailed_, texCache_.size());

    // Baked lightmap atlases: resolve each submesh's _LM atlas name to a GL texture.
    const std::string lmDir = mapDir() + "lightmaps/";
    int lmBound = 0;
    for (render::SubMesh& sm : mapMesh.subs) {
        if (sm.lightmapName.empty()) continue;
        sm.lightmapTex = resolveOne(lmDir + sm.lightmapName + ".png");
        if (sm.lightmapTex >= 0) ++lmBound;
    }
    LOG_INFO("lightmaps: %d/%zu submeshes bound to atlases", lmBound, mapMesh.subs.size());

    mapMesh_ = renderer.uploadMesh(mapMesh);
    core::loadYield("World: map mesh upload");
    fx_.load(renderer, root + "/../content/");
    // [integration M08b] Weapon templates WeaponFx does not reconstruct go to Rendering's particle runtime (M32: cooked
    // ParticleSystems of the map packages; released at unloadMapRenderData). The renderer outlives this World.
    fx_.setGenericRuntime({
        [&renderer](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.spawnParticleEffect(t, p, f, u); },
        [&renderer](const std::string& t, const core::Vec3& a, const core::Vec3& b) { return renderer.spawnParticleEffectSegment(t, a, b); },
        [&renderer](int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.setParticleEffectTransform(h, p, f, u); }});
    core::loadYield("World: effects");
    fx_.loadMeshes(renderer, root + "/../content/");
    vehicleFx_.load(renderer, root + "/../content/");
    core::loadYield("World: effect meshes");

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
        // The socket itself is the chassis' WeaponSocket_Primary (applyChassisToLocalPawn).
        LOG_INFO("weapon: Ion Blaster loaded");
    }
    loadProjectileVisuals(root, resolveTextures);

    // Collision: the authored per-trace worlds (AssetTools PHYSICS_STREETS): collision_pawn.glb blocks pawn and
    // vehicle movement (BSP + 71 BlockingVolumes + 4 TnForcedDirVolumes + authored simple hulls), and
    // collision_weapon.glb blocks hitscan / line checks (BSP + the 34 weapon-blocking volumes + hulls).
    // collision.glb (render geometry of every blocking prop) is only a fallback. Movers' triangles are split
    // out into moving collision sets (MapState).
    const std::string mapDir = this->mapDir();
    mapState_.load(mapDir + "gameplay.json", matchMode_);
    mapState_.loadObjectiveVolumes(mapDir + "physics.json");
    auto splitMovers = [this](const render::MeshData& in, render::MeshData& out,
                          std::vector<std::pair<std::string, std::vector<core::Vec3>>>& moverTris) {
        std::vector<std::string> names = mapState_.moverActorNames();
        out.positions = in.positions;
        for (const render::SubMesh& sm : in.subs) {
            bool mover = std::find(names.begin(), names.end(), sm.nodeName) != names.end();
            std::vector<core::Vec3>* dst = nullptr;
            if (mover) { moverTris.push_back({sm.nodeName, {}}); dst = &moverTris.back().second; }
            for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
                if (!mover) { out.indices.push_back(in.indices[i]); continue; }
                uint32_t v = in.indices[i];
                dst->push_back({in.positions[v * 3], in.positions[v * 3 + 1], in.positions[v * 3 + 2]});
            }
        }
    };
    render::MeshData colMesh, pawnStatic, weaponColMesh, weaponStatic;
    std::vector<std::pair<std::string, std::vector<core::Vec3>>> pawnMovers, weaponMovers;
    bool authored = assets::loadGlb(mapDir + "collision_pawn.glb", colMesh);
    if (!authored) assets::loadGlb(mapDir + "collision.glb", colMesh);
    // Per-node bounds of the authored movement collision (for tracing contacts back to authored actors).
    colActors_.clear();
    for (const render::SubMesh& sm : colMesh.subs) {
        if (sm.indexCount == 0) continue;
        ColActor a;
        a.name = sm.nodeName; a.mesh = sm.sourceMesh;
        a.kind = a.name.rfind("BSPCollision", 0) == 0 ? "bsp" : a.name.rfind("BlockingVolume", 0) == 0 ? "BlockingVolume"
               : a.name.rfind("TnForcedDirVolume", 0) == 0 ? "TnForcedDirVolume" : "prop";
        uint32_t v0 = colMesh.indices[sm.indexOffset];
        a.lo = a.hi = core::Vec3{colMesh.positions[v0 * 3], colMesh.positions[v0 * 3 + 1], colMesh.positions[v0 * 3 + 2]};
        for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
            uint32_t v = colMesh.indices[i];
            core::Vec3 q{colMesh.positions[v * 3], colMesh.positions[v * 3 + 1], colMesh.positions[v * 3 + 2]};
            a.lo = {std::min(a.lo.x, q.x), std::min(a.lo.y, q.y), std::min(a.lo.z, q.z)};
            a.hi = {std::max(a.hi.x, q.x), std::max(a.hi.y, q.y), std::max(a.hi.z, q.z)};
        }
        colActors_.push_back(a);
    }
    core::loadYield("World: collision meshes");
    if (!colMesh.empty()) {
        splitMovers(colMesh, pawnStatic, pawnMovers);
        collision_.build(pawnStatic);
        core::loadYield("World: movement collision");
        if (assets::loadGlb(mapDir + "collision_weapon.glb", weaponColMesh)) {
            splitMovers(weaponColMesh, weaponStatic, weaponMovers);
            weaponCollision_.build(weaponStatic);
        }
        mapState_.registerCollision(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, pawnMovers, weaponMovers);
        LOG_INFO("collision: movement %s, weapon %s", authored ? "collision_pawn.glb" : "collision.glb (fallback)",
                 weaponCollision_.valid() ? "collision_weapon.glb" : "movement world");
        renderer.setVisibilityQuery([this](const core::Vec3& a, const core::Vec3& b) {
            // segmentHit walks only the grid cells the ray crosses and stops at the first hit, so
            // long light-visibility rays no longer need to be marched in 2 m pieces.
            float t;
            // Zero-extent line checks use the weapon collision world (Gameplay Pass 17).
            const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;
            return lineWorld.segmentHit(a, b, t);
        });
        // KillZ: the persistent level's TnWorldInfo (<map>_BASE_m in physics.json "world") [CONF AssetTools physics].
        // [integration M06] Read per map (was Streets' -75000 UU for every map; Gorge authors -7500, Seed -1500).
        killZ_ = -2621.43f;   // UE3 WorldInfo default -262143 UU if the map authors none
        {
            std::ifstream pf(mapDir + "physics.json", std::ios::binary);
            std::stringstream ps; ps << pf.rdbuf();
            assets::Json pj;
            if (pf && assets::Json::parse(ps.str(), pj)) {
                for (const auto& kv : pj["world"].obj) {
                    std::string lv = kv.first;
                    std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
                    if (lv.size() >= 7 && lv.compare(lv.size() - 7, 7, "_base_m") == 0 && kv.second.has("KillZ"))
                        killZ_ = kv.second["KillZ"].asFloat() * 0.01f;
                }
            }
            LOG_INFO("World: KillZ %.1f m (%s)", killZ_, mapName_.c_str());
        }
        loadHazards();   // per-map pain volumes (Gameplay; AssetTools hazard_volumes.json)
    }

    // Place the player at an authored start of the match's class (FFA in DM, team starts otherwise), with the
    // start's authored rotation.
    spawnPos_ = {0, 0, 0};
    spawnYaw_ = 0.0f;
    if (loadSpawn(mapDir + "spawnpoints.json", spawnPos_, spawnYaw_)) {
        LOG_INFO("World: %s spawn at %.1f, %.1f, %.1f facing yaw %.2f", gameModeName(matchMode_),
                 spawnPos_.x, spawnPos_.y, spawnPos_.z, spawnYaw_);
    }
    respawnPlayer();
    // Test spawn selection (not a menu): WFC_START=<0..83> picks one of the 84 authored player starts,
    // WFC_START_ACTOR=<name> picks by actor name; F6 / F7 cycle starts at run time (debug).
    loadStartPoints(mapDir + "gameplay.json");
    if (const char* s = std::getenv("WFC_START")) teleportToStart(std::atoi(s));
    if (const char* s = std::getenv("WFC_START_ACTOR"))
        for (size_t i = 0; i < starts_.size(); ++i) if (starts_[i].actor == s) teleportToStart((int)i);
    LOG_INFO("vehicle mesh: actor %.2f m above origin, top %.2f m", player_.pawn().meshToActor(Form::Vehicle), player_.pawn().meshTopAboveOrigin());
    if (std::getenv("WFC_STARTVEHICLE")) {   // for vehicle tests: vehicle mesh hung off the same actor location
        Character& pc = player_.pawn();
        float above = pc.meshToActor(Form::Robot) - pc.meshToActor(Form::Vehicle);
        pc.setForm(Form::Vehicle);
        pc.setPosition(pc.position() + core::Vec3{0, above, 0});
    }

    // Authored Streets pickup factories and destructibles (AssetTools 7a69756). Their meshes, effects and beams are
    // presented by the renderer from the map data; graybox scaffold pickups only with WFC_GRAYBOXPICKUPS.
    core::loadYield("World: spawns");
    actors_.clear();
    loadPickupFactories(mapDir + "gameplay.json");
    loadDestructibles(mapDir + "physics.json", root + "/../content/");
    if (std::getenv("WFC_GRAYBOXPICKUPS")) {
        actors_.push_back(std::make_unique<Pickup>(spawnPos_ + core::Vec3{3, 0, 0}, Pickup::Kind::Health));
        actors_.push_back(std::make_unique<Pickup>(spawnPos_ + core::Vec3{-3, 0, 2}, Pickup::Kind::Ammo));
    }
    // Weapon-test dummy (DamageTarget): test instrumentation, not WFC content. Only with WFC_TESTDUMMY=1
    // (Gameplay) or WFC_DAMAGETARGET=1 (Rendering's name for the same hook).
    if (std::getenv("WFC_TESTDUMMY") || std::getenv("WFC_DAMAGETARGET")) {
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

// gameplay.json "pickups": the placed TnAmmoCrate/TnHealth/TnOverShield pickup factories with their
// authored effective RespawnTime (30 / 60 / 120 s). Objective factories (flag/bomb) require
// TnGameRules_SingleFlagCTF / ScoreBombingRun, which the slice does not run, and are not instanced.
void World::loadPickupFactories(const std::string& path) {
    pickupFactories_.clear();
    std::string text;
    assets::Json root;
    if (!readTextFile(path, text) || !assets::Json::parse(text, root)) { LOG_WARN("pickups: cannot read %s", path.c_str()); return; }
    const assets::Json& list = root["pickups"];
    int counts[3] = {0, 0, 0};
    for (size_t i = 0; i < list.size(); ++i) {
        const assets::Json& p = list[i];
        const std::string& cls = p["class"].asString();
        PickupFactory::Kind kind;
        if (cls == "TnAmmoCratePickupFactory") kind = PickupFactory::Kind::AmmoCrate;
        else if (cls == "TnHealthPickupFactory") kind = PickupFactory::Kind::Health;
        else if (cls == "TnOverShieldPickupFactory") kind = PickupFactory::Kind::OverShield;
        else continue;
        const assets::Json& L = p["location_gltf"];
        if (L.size() < 3) continue;
        float respawn = p["effective"]["RespawnTime"].asFloat(-1.0f);
        if (respawn < 0.0f) continue;
        auto f = std::make_unique<PickupFactory>(p["actor"].asString(), kind,
                                                 core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()},
                                                 respawn, (int)pickupFactories_.size());
        pickupFactories_.push_back(f.get());
        actors_.push_back(std::move(f));
        ++counts[(int)kind];
    }
    LOG_INFO("pickups: %zu factories (ammo crate %d, health %d, overshield %d) from %s", pickupFactories_.size(),
             counts[0], counts[1], counts[2], path.c_str());
}

// physics.json "destructibles": the placed TnStaticDestructibleActor(s) at their authored location
// (UE -> glTF metres: 0.01 * (X, Z, Y)). The damage/touch box is the Base piece mesh bounds at the piece
// transform (Z -160 UU) [CONF mesh + transform].
void World::loadDestructibles(const std::string& path, const std::string& contentRoot) {
    destructibles_.clear();
    std::string text;
    assets::Json root;
    if (!readTextFile(path, text) || !assets::Json::parse(text, root)) return;
    const assets::Json& list = root["destructibles"];
    for (size_t i = 0; i < list.size(); ++i) {
        const assets::Json& d = list[i];
        const assets::Json& loc = d["props"]["Location"];
        core::Vec3 p{loc["X"].asFloat() * 0.01f, loc["Z"].asFloat() * 0.01f, loc["Y"].asFloat() * 0.01f};
        core::Vec3 bmin{-1, -1, -1}, bmax{1, 1, 1};
        render::MeshData mesh;
        if (assets::loadGlb(contentRoot + "DES_IAC_WallPanelSign_p/Meshes/WallPanelSign_Base_STAT.gltf", mesh) && mesh.positions.size() >= 3) {
            bmin = bmax = core::Vec3{mesh.positions[0], mesh.positions[1], mesh.positions[2]};
            for (size_t k = 0; k + 2 < mesh.positions.size(); k += 3) {
                core::Vec3 v{mesh.positions[k], mesh.positions[k + 1], mesh.positions[k + 2]};
                bmin = {std::min(bmin.x, v.x), std::min(bmin.y, v.y), std::min(bmin.z, v.z)};
                bmax = {std::max(bmax.x, v.x), std::max(bmax.y, v.y), std::max(bmax.z, v.z)};
            }
            bmin.y -= 1.6f; bmax.y -= 1.6f;                     // piece Transform Position Z -160 UU
        }
        auto a = std::make_unique<Destructible>(d["actor"].asString(), p, bmin, bmax, (int)destructibles_.size());
        // Piece collision per state: Base (intact) and Chunk02 (destroyed / settled) at the piece transform.
        auto meshTris = [&](const char* file) {
            std::vector<core::Vec3> tris;
            render::MeshData m;
            if (!assets::loadGlb(contentRoot + file, m)) return tris;
            for (uint32_t idx : m.indices)
                tris.push_back({m.positions[idx * 3], m.positions[idx * 3 + 1] - 1.6f, m.positions[idx * 3 + 2]});
            return tris;
        };
        std::vector<core::Vec3> baseTris = meshTris("DES_IAC_WallPanelSign_p/Meshes/WallPanelSign_Base_STAT.gltf");
        std::vector<core::Vec3> stumpTris = meshTris("DES_IAC_WallPanelSign_p/Meshes/WallPanelSign_Chunk02_STAT.gltf");
        for (CollisionWorld* w : {&collision_, &weaponCollision_}) {
            if (!w->valid()) continue;
            Destructible::CollisionSet cs;
            cs.world = w;
            cs.intact = baseTris.empty() ? -1 : w->addDynamicSet(baseTris, core::Mat4::translate(p));
            cs.broken = stumpTris.empty() ? -1 : w->addDynamicSet(stumpTris, core::Mat4::translate(p));
            a->addCollision(cs);
        }
        LOG_INFO("destructible: %s at authored %.2f %.2f %.2f (state 0, health %.0f)", a->name().c_str(), p.x, p.y, p.z, a->health());
        destructibles_.push_back(a.get());
        actors_.push_back(std::move(a));
    }
}

void World::loadStartPoints(const std::string& path) {
    starts_.clear();
    std::string text;
    assets::Json root;
    if (!readTextFile(path, text) || !assets::Json::parse(text, root)) return;
    const assets::Json& ps = root["player_starts"];
    for (size_t i = 0; i < ps.size(); ++i) {
        const assets::Json& L = ps[i]["location_gltf"];
        if (L.size() < 3) continue;
        StartPoint s;
        s.actor = ps[i]["actor"].asString(); s.cls = ps[i]["class"].asString();
        s.cluster = ps[i]["clusters"][0].asString();
        s.pos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        // Same yaw convention as loadSpawn: UE yaw -> rebuild yaw (forward -Z at 0). UE +X = glTF +X.
        float ueYaw = ps[i]["yaw_deg"].asFloat() * 0.01745329252f;
        core::Vec3 f{std::cos(ueYaw), 0.0f, std::sin(ueYaw)};          // UE forward in glTF (x, z <- y)
        s.yaw = std::atan2(-f.x, -f.z);
        starts_.push_back(s);
    }
    LOG_INFO("starts: %zu player starts", starts_.size());
}

void World::teleportToStart(int index) {
    if (starts_.empty()) return;
    index = ((index % (int)starts_.size()) + (int)starts_.size()) % (int)starts_.size();
    startCursor_ = index;
    const StartPoint& s = starts_[(size_t)index];
    core::Vec3 p = s.pos;
    if (collision_.valid()) { float gy; core::Vec3 n; if (collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, n)) p.y = gy; }
    Character& pc = player_.pawn();
    if (pc.moveForm() == Form::Vehicle) p.y += pc.meshToActor(Form::Robot) - pc.meshToActor(Form::Vehicle);
    pc.setPosition(p);
    pc.setYaw(s.yaw);
    pc.velocity() = {0, 0, 0};
    pc.groundY = p.y;
    player_.controller().setCameraYaw(s.yaw);
    LOG_INFO("start %d/%zu: %s (%s, cluster %s) at %.1f %.1f %.1f", index, starts_.size(), s.actor.c_str(), s.cls.c_str(),
             s.cluster.c_str(), p.x, p.y, p.z);
}

bool World::loadSpawn(const std::string& path, core::Vec3& outPos, float& outYaw) {
    std::string text;
    if (!readTextFile(path, text)) return false;
    assets::Json root;
    if (!assets::Json::parse(text, root)) return false;
    const assets::Json& points = root["points"];
    if (!points.isArray()) return false;

    // WFC_SPAWN_INDEX picks the Nth matching start (for traversal/region screenshots). Which start
    // TnSpawnPointManager picks (cluster scoring, InitialSpawns) is not recovered: index 0 [PROV].
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
                // The pawn spawns with the start's authored Rotation yaw (UE yaw -> rebuild yaw, as loadStartPoints).
                float ueYaw = pt["yaw_deg"].asFloat() * 0.01745329252f;
                outYaw = std::atan2(-std::cos(ueYaw), -std::sin(ueYaw));
                return true;
            }
        }
        return false;
    };
    // TnFreeForAllGame (DM) spawns at TnFreeForAllPlayerStart; TnVersusGame modes at TnTeamPlayerStart [HIGH].
    if (matchMode_ == MatchMode::DM ? pick("TnFreeForAllPlayerStart") : pick("TnTeamPlayerStart")) return true;
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
    if (in.wasPressed(platform::Button::DebugNextStart)) teleportToStart(startCursor_ + 1);   // test spawn cycling
    if (in.wasPressed(platform::Button::DebugPrevStart)) teleportToStart(startCursor_ - 1);
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

void World::fireHitscan(const core::Vec3& origin, const core::Vec3& dirIn) { fireHitscanWith(player_.pawn().weapon(), origin, dirIn); }

void World::fireHitscanWith(const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn) {
    sysprof::Scope spHit(sysprof::Hitscan);
    ++sysprof::shots;
    const float range = w.rangeM;   // [CONF] 300 m

    // Apply per-shot spread as a small random cone around the aim direction.
    core::Vec3 dir = core::normalize(dirIn);
    {
        core::Vec3 up{0, 1, 0};
        core::Vec3 rt = core::normalize(core::cross(dir, up));
        core::Vec3 u2 = core::normalize(core::cross(rt, dir));
        auto rf = [] { return (float)std::rand() / (float)RAND_MAX * 2.0f - 1.0f; };
        // Fine aim scales spread by the weapon's FineAimSpreadModifier (Ion Blaster 0.5) [CONF data].
        float spread = player_.pawn().effectiveSpread();   // bloom x airborne x fine aim (HmWeapon.GetSpread)
        dir = core::normalize(dir + rt * (rf() * spread) + u2 * (rf() * spread));
    }
    core::Vec3 end = origin + dir * range;

    float bestDist = range;
    // World geometry.
    if (collision_.valid()) {
        float t; core::Vec3 n;
        const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;
        if (lineWorld.segmentHit(origin, end, t, n)) bestDist = range * t;
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
    // Match opponents (test / diagnostic participants): robot cylinder.
    MatchOpponent* hitOpp = nullptr;
    for (MatchOpponent* o : opponents_) {
        float th;
        if (o->rayHit(origin, dir, range, th) && th < targetDist) { targetDist = th; hitOpp = o; hitTarget = nullptr; }
    }
    // Authored destructibles (HmGenericDamageDestructionTrigger) in front of any closer hit.
    Destructible* hitDes = nullptr;
    for (Destructible* d : destructibles_) {
        float th;
        if (d->state() == 0 && rayAabb(origin, dir, range, d->boxMin(), d->boxMax(), th) && th < targetDist) {
            targetDist = th; hitDes = d; hitTarget = nullptr; hitOpp = nullptr;
        }
    }
    { float ts; if (sentryRayHit(origin, dir, range, ts) && ts < targetDist) { targetDist = ts; hitTarget = nullptr; hitDes = nullptr; hitOpp = nullptr; damageSentry(w.damageAt(ts), localPlayer_, w.damageType ? w.damageType : ""); } }
    bool hitBarrier = false;
    { float th; if (barrierRayHit(origin, dir, range, th) && th <= targetDist + 0.05f) { targetDist = th; hitBarrier = true; hitTarget = nullptr; hitDes = nullptr; hitOpp = nullptr; } }
    float dist = (hitTarget || hitDes || hitOpp || hitBarrier) ? targetDist : bestDist;
    if (hitBarrier) damageBarrier(w.damageAt(dist), w.damageType ? w.damageType : "");
    if (hitOpp) applyMatchDamage(hitOpp->matchPlayer(), localPlayer_, w.damageAt(dist), false, w.damageType);   // InstantHitDamage, falloff, the weapon's InstantHitDamageTypes[0]
    core::Vec3 hitPoint = origin + dir * dist;
    if (hitTarget) hitTarget->applyDamage(w.damageAt(dist));   // [CONF] range-based falloff
    if (hitDes) hitDes->applyDamage(*this, w.damageAt(dist));

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
    // impact squib where the trace hit something (world or target) - the HELD weapon class's templates
    // (setPlayerWeaponAudio); WeaponFx draws the ones it reconstructs, Rendering's particle runtime the others
    // (setGenericRuntime), and nothing for a template neither has (logged once) [integration M08b: no other weapon's
    // FX is substituted - a class without an FX entry draws none].
    // [Systems M08h] Vehicle form: the vehicle weapon's own templates, the flash at the shot's alternating socket and the
    // tracer from that socket (the damage trace still starts at the start-trace location) [CONF RE pass 5 9g].
    const bool vehShot = player_.pawn().moveForm() == Form::Vehicle && w.def;
    const std::string fxClass = vehShot ? "TransContent.TnWeapon" + std::string(w.def->id) : weaponClass_;
    const WeaponFxTemplates* wfx = CharacterAudio::weaponFx(fxClass);
    if (!wfx) {
        static std::set<std::string> warned;
        if (warned.insert(fxClass).second) LOG_WARN("weapon fx: no templates for %s (nothing drawn)", fxClass.c_str());
    }
    if (vehShot) { core::Vec3 vm; if (spawnVehicleMuzzleFlash(fxClass, vm)) muzzle = vm; }
    else if (wfx && weaponSocketWorld("MuzzleFlash", ms)) fx_.spawnMuzzleFlash(wfx->muzzle, ms);
    if (wfx) fx_.spawnTracer(wfx->tracer, muzzle, hitPoint);
    if (wfx && dist < range - 0.01f)
        fx_.spawnImpact(wfx->squib, hitPoint, dir * -1.0f, origin);
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
    // [Systems M08d] the fired weapon's own WP_Fire (robot or vehicle weapon), by identity.
    {
        const bool vehForm = player_.pawn().moveForm() == Form::Vehicle;
        const std::string cls = w.def ? "TransContent.TnWeapon" + std::string(w.def->id) : firingWeaponClass(vehForm);
        onWeaponFired(cls, w.lowAmmo(), vehForm, muzzle);
        (void)me; (void)ownDist;
    }
    burstActive_ = true; sinceShot_ = 0.0f;
    // World hit: HmWeaponMesh.CreateImpactEffects -> TnWeaponMesh.GetImpactSound: the surface's weapon-type sound (no
    // physmat authors one [CONF data]) -> PhysMaterial.ImpactSound (only special surfaces, e.g. ForceField; surfaces
    // are not resolved here [PARTIAL]) -> the weapon mesh's DefaultImpactSound, played at the hit location [CONF].
    // Pawn hit: Transformers have AllowHitEffects false (only Vehicle / MatineePawn / SentryPawn set it) [HIGH], so no
    // weapon impact sound; the victim's TnHitEffectPlayer plays HitSound (an event in the VICTIM's SoundEventSet) if
    // the damage type bCausesBlood, at most every RetriggerTime per victim per entry [CONF script]. The damage
    // targets are stand-ins without a character: they use the default profile as the victim [PROV].
    // [Systems M08d] impacts / victim hit effect by the weapon actually fired (robot or vehicle weapon)
    const std::string firedCls = w.def ? "TransContent.TnWeapon" + std::string(w.def->id) : firingWeaponClass(player_.pawn().moveForm() == Form::Vehicle);
    const float hitDist = core::length(hitPoint - listenerPos_);
    if (hitTarget) {
        const WeaponHitEffect* he = CharacterAudio::weaponHitEffect(firedCls);
        if (he && he->causesBlood) {
            auto key = std::make_pair((const void*)hitTarget, he->index);
            auto it = lastHitEffect_.find(key);
            if (it == lastHitEffect_.end() || it->second + he->retrigger < hitClock_) {
                lastHitEffect_[key] = hitClock_;
                const std::string cue = CharacterAudio::defaultProfile().voiceCue(he->hitEvent);
                if (!cue.empty()) cues_.play(cue.c_str(), hitPoint, hitDist);
            }
        }
    } else if (dist < range - 0.01f) {
        const std::string& impact = CharacterAudio::weaponCue(firedCls, "DefaultImpactSound");
        if (!impact.empty()) cues_.play(impact.c_str(), hitPoint, hitDist);
    }
}

void World::setAudio(audio::IAudio* a, bool loadSliceMap) {
    audio_ = a;
    if (!a) return;
    const std::string base = assetRoot() + "/../content/";
    // All audio = the original SoundCues (weapon, vehicle, robot movement, transformation, fine aim).
    cues_.load(a, base);
    levelAudio_.attach(a, assetRoot());
    // Master's Default DSP compressor (global SoundMixerProperties data) [CONF values; MED DSPEffectConfig bit].
    float thr, att, rel, mk;
    if (SoundMixer::masterCompressor(thr, att, rel, mk)) a->setMasterCompressor(thr, att, rel, mk);
    // Direct boot loads the World's map (WFC_MAP / default); a frontend boot calls setAudio(a, false) and then
    // loadMapAudio(<selected level>) [integration: was the hard-wired "MP_IAC_Streets"; FRONTEND.md handoff].
    if (loadSliceMap) loadMapAudio(mapName_);
    // Validation hook: the slice body with another character's audio profile (Gameplay sets the real chassis).
    if (const char* ch = std::getenv("WFC_CHARACTER_AUDIO")) if (*ch) setPlayerCharacterAudio(ch);
    if (const char* wc = std::getenv("WFC_WEAPON_AUDIO")) if (*wc) setPlayerWeaponAudio(wc);
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

bool World::loadMapAudio(const std::string& level) {
    if (!levelAudio_.level().empty()) unloadMapAudio();
    const bool ok = levelAudio_.load(level);
    if (ok) {                                                         // level-owned
        CharacterAudio::loadCues(cues_, audioProfile());
        CharacterAudio::loadWeaponCues(cues_, weaponClass_);
        CharacterAudio::loadHitCues(cues_, CharacterAudio::defaultProfile(), weaponClass_);   // the targets' hit sounds
        lastHitEffect_.clear();
        for (const std::string& c : loadoutWeaponClasses_) ensureWeaponAudio(c);   // [Systems M08d] the loadout's weapons
        const SoundCues::LocStats& ls = SoundCues::locStats();
        LOG_INFO("localized waves (language %s): %d from the _LOC twin, %d merged copy of that twin, %d not played",
                 std::getenv("WFC_LANGUAGE") ? std::getenv("WFC_LANGUAGE") : "INT", ls.twin, ls.merged, ls.skipped);
    }
    return ok;
}

// The player's weapon WeaponSounds (CharacterAudio weapon table, by weapon class; default TnWeaponIonBlaster) [CONF data].
const char* World::weaponCue(const char* event) const {
    const std::string& c = CharacterAudio::weaponCue(weaponClass_, event);
    return c.c_str();
}

void World::setPlayerWeaponAudio(const std::string& weaponClass) {
    weaponClass_ = weaponClass;
    if (audio_ && !levelAudio_.level().empty()) {
        CharacterAudio::loadWeaponCues(cues_, weaponClass);
        CharacterAudio::loadHitCues(cues_, CharacterAudio::defaultProfile(), weaponClass);
    }
}

void World::setPlayerCharacterAudio(const std::string& chassisKey) {
    const CharacterAudioProfile* p = CharacterAudio::find(chassisKey);
    if (!p) { LOG_WARN("character audio: no profile for %s (default kept)", chassisKey.c_str()); return; }
    vehicleAudio_.stopAll(cues_);           // [Systems M08d] the previous body's vehicle loops end with it (class change)
    vehicleForm_.reset();
    vehicleFxDriver_.setData(&p->vehicleFx);   // [Systems M08e] the new body's authored vehicle effects
    audioProfile_ = p;
    robotFoley_.setProfile(p);
    vehicleAudio_.setProfile(*p);
    if (audio_ && !levelAudio_.level().empty()) CharacterAudio::loadCues(cues_, *p);
    LOG_INFO("character audio: %s (%s / %s)", p->key.c_str(), p->voiceSet.c_str(), p->vehicleSet.c_str());
}

void World::tickAudioOnly(float dt) {
    if (!audio_) return;
    cues_.setListener(listenerPos_);
    levelAudio_.tick(dt, listenerPos_, listenerPos_);   // no pawn outside a match: zones are tested at the listener
    cues_.tick(dt);
}

int World::playPickupSound(const char* factoryClass, const core::Vec3& receiverPos) {
    return PickupPresentation::onTaken(factoryClass, cues_, atPawn(), core::length(receiverPos - listenerPos_));
}

void World::unloadMapAudio() {
    weaponAudioLoaded_.clear();                // the level's cues are released with it
    resetSystemsForMatch();                    // player-side sounds + Systems FX + queues
    levelAudio_.unload();                      // music player, every instance, level cues / samples / presets, Flush
}

void World::resetSystemsForMatch() {
    cues_.stopNonMapInstances();               // weapon / vehicle / foley / transform / pickup sounds (immediate)
    vehicleAudio_.stopAll(cues_);              // [Systems M08d]
    weaponAudio_.stopAll(cues_);
    vehicleFxDriver_.stopAll();
    vehicleForm_.reset();
    vehicleAudio_ = VehicleAudio{};
    vehicleAudio_.setProfile(audioProfile());
    robotFoley_ = RobotFoley{};
    nitro_ = VehicleNitro{};
    fx_.clearParticles();
    vehicleFx_.clearParticles();
    for (int& i : boostInst_) i = -1;
    for (int& i : hoverInst_) i = -1;
    for (int& i : jumpInst_) i = -1;
    ramInst_ = -1;
    hoverActive_ = false; boostActive_ = false; vehiclePrevGrounded_ = true; jumpCount_ = 0;
    vehLoadState_ = 0; prevDashing_ = false;
    foleyCues_.clear();
    transformCuePlayed_ = false; transformCue_ = -1; trackT_ = 0.0f; prevTransforming_ = false;
    prevFineAim_ = false; tireSlipOverride_ = -1.0f; tireSlip_ = 0.0f;
    burstActive_ = false; sinceShot_ = 0.0f;
    notifies_.clear();
    const Weapon& w = player_.pawn().weapon();   // resync: a reset must not replay a shot / reload animation
    weaponSeenShot_ = w.shotSerial; weaponSeenReload_ = w.reloadSerial;
    levelAudio_.resetMatch();
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
//   Transformation [CONF, the character profile's clips]: the HmAnimNotify_Sound notifies of Transform_ToVehicle_ROBO /
//   Transform_ToRobot_ROBO at their authored times (Optimus: BL_TRANSFORM.OPTIMUS_BOT2VEH @0.125 s of 2.0 s;
//   OPTIMUS_VEH2BOT @0.0, MinWeight 0; Megatron adds BOT2VEH_ENGINESTART).
//   No SocketName: the component sits on the pawn's skeletal mesh and moves with it for the whole
//   layered cue (servos 0.0, main 0.15, land thump 0.64, flare 1.44, air release 1.59, finish 1.67 s).
//   Fine aim [CONF TnWeaponIonBlaster]: WP_StartFineAim / WP_EndFineAim -> BL_WPN_GUN_PULSE_RIFLE.
//   FINE_AIM_START / FINE_AIM_END, played on the weapon.
void World::tickCharacterAudio(float dt) {
    const Character& pc = player_.pawn();
    bool tf = pc.isTransforming();
    if (tf && !prevTransforming_) { transformCuePlayed_ = false; transformNotify_ = 0; transformTarget_ = pc.moveForm(); }
    if (tf) {
        const CharacterAudioProfile& prof = audioProfile();
        const CharacterAudioProfile::Clip* clip =
            prof.clip(transformTarget_ == Form::Vehicle ? "Transform_ToVehicle_ROBO" : "Transform_ToRobot_ROBO");
        // Sound notifies in authored order; each fires once when the fold passes its time / authored length.
        for (int i = transformNotify_; clip && i < (int)clip->notifies.size(); ++i) {
            const CharacterAudioProfile::Notify& n = clip->notifies[(size_t)i];
            if (clip->length > 0.0f && pc.transformProgress() < n.t / clip->length) break;
            const std::string& cue = prof.notifyCue(n);
            if (!cue.empty()) {
                const int id = cues_.play(cue.c_str(), atPawn(), 0.0f);
                if (!transformCuePlayed_) transformCue_ = id;
                transformCuePlayed_ = true;
            }
            transformNotify_ = i + 1;
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
        if (*weaponCue(fa ? "WP_StartFineAim" : "WP_EndFineAim")) cues_.play(weaponCue(fa ? "WP_StartFineAim" : "WP_EndFineAim"), atWeapon(), 0.0f);
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
    // These effects / sounds are OptimusTruckForm's (BoostFx / HoverFX / JumpFX on VH_OptimusPrime bones). Other chassis
    // author their own sets (character.json vehicle.fx) on their own sockets: Rendering's per-chassis FX [PARTIAL here],
    // so they are not drawn at the truck's socket positions on another body.
    const bool optimusFx = pc.chassis().id == "Truck" || pc.chassis().id == "Truck7";
    bool vehicle = optimusFx && pc.form() == Form::Vehicle && !pc.isTransforming();
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
    // [Systems M08e] the hand-made Optimus effects only without Rendering's runtime (VehicleFxDriver drives the authored
    // per-chassis HoverFX / BoostFx / JumpFX / RamFX, Optimus included, when it is bound).
    const bool handFx = !vehicleFxDriver_.bound();
    if (boost && handFx && !boostActive_) {
        boostInst_[0] = vehicleFx_.start(VehicleFx::Boost, VehicleFx::BoostL);
        boostInst_[1] = vehicleFx_.start(VehicleFx::Boost, VehicleFx::BoostR);
    } else if (!(boost && handFx) && boostActive_) {
        for (int& id : boostInst_) { vehicleFx_.deactivate(id); id = -1; }
    }
    boostActive_ = boost && handFx;
    // Hover thrusters on all six wheel sockets.
    if (hover && handFx && !hoverActive_) {
        for (int i = 0; i < 6; ++i) hoverInst_[i] = vehicleFx_.start(VehicleFx::Hover, VehicleFx::HoverLBack + i);
    } else if (!(hover && handFx) && hoverActive_) {
        for (int& id : hoverInst_) { vehicleFx_.deactivate(id); id = -1; }
    }
    hoverActive_ = hover && handFx;
    // Jump boosters: one-shot burst on vehicle take-off; killed if the vehicle form ends.
    bool grounded = pc.onGround();
    bool tookOff = vehicle && vehiclePrevGrounded_ && !grounded && pc.velocity().y > 2.0f;
    bool landed = vehicle && !vehiclePrevGrounded_ && grounded;
    (void)landed;
    if (tookOff && handFx) {
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
        if (handFx) ramInst_ = vehicleFx_.start(VehicleFx::Ram, VehicleFx::RamSocket);
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
        // [Systems M08d] Vehicle audio for EVERY chassis (the FX above stay Optimus-only until Rendering's per-chassis
        // vehicle FX): Gameplay's vehicle state -> the chassis form class's component calls (VehicleFormAudio).
        const Character::VehicleState& vs = pc.vehicleState();
        const VehicleFormType ft = pc.vehicleParams().form;
        VehicleFormSignals s;
        s.kind = ft == VehicleFormType::Car ? VehicleFormSignals::Kind::Car : ft == VehicleFormType::Tank ? VehicleFormSignals::Kind::Tank
               : ft == VehicleFormType::Jet ? VehicleFormSignals::Kind::Jet : VehicleFormSignals::Kind::Truck;
        s.vehicle = pc.form() == Form::Vehicle && !pc.isTransforming() && !localDead_;
        const bool grounded = pc.onGround();
        s.onGround = grounded;
        s.boostState = s.kind == VehicleFormSignals::Kind::Tank ? vs.tankBoost : s.kind == VehicleFormSignals::Kind::Jet ? vs.flying : vs.driving;
        s.stickForward = player_.controller().moveForwardInput();
        s.velocity = v;
        s.forward = core::forwardFromYawPitch(pc.yaw(), 0.0f);
        s.tookOff = s.vehicle && audioPrevGrounded_ && !grounded && v.y > 2.0f;
        audioPrevGrounded_ = grounded;
        s.dashing = vs.dashRemain > 0.0f;
        s.rolling = vs.rollRemain > 0.0f;
        s.nitroStarted = ne == VehicleNitro::Event::Started;
        s.ascendHeld = player_.controller().moveIntent().ascend;
        s.descendHeld = player_.controller().moveIntent().descend;
        s.wheelSlip = tireSlipOverride_;
        {   // [Systems M08e] per-chassis vehicle FX (authored sets, Rendering's runtime); its hover BoosterAmount -> audio
            VehicleFxDriver::Inputs fi;
            fi.kind = s.kind;
            const bool shown = pc.form() == Form::Vehicle && !localDead_;
            fi.fxAllowed = shown && (!pc.isTransforming() ||
                                     (pc.moveForm() == Form::Vehicle && pc.transformProgress() >= audioProfile().vehicleFx.enableFraction));
            fi.hovering = !s.boostState;
            fi.boostState = s.boostState;
            const bool rollStart = s.kind == VehicleFormSignals::Kind::Car && s.boostState && s.rolling && !fxPrevRolling_;
            fxPrevRolling_ = s.rolling;
            fi.jumpStart = s.tookOff || rollStart;             // UpdateJumping / Driving.UpdateRolling: Play(JumpFX)
            fi.nitroActive = vs.nitroRemain > 0.0f;
            fi.formEnded = fxPrevShown_ && !shown;
            fxPrevShown_ = shown;
            fi.normJumpRemaining = vs.jumpBoost / core::config::kDriveJumpBoostTime;
            const int lp = localPlayer_;
            const int team = (lp >= 0 && (size_t)lp < match_.players().size()) ? match_.players()[(size_t)lp].team : -1;
            if (!teamEnergon(team == 255 ? -1 : team, fi.energon)) fi.energon[0] = fi.energon[1] = fi.energon[2] = 1.0f;
            fi.body = pc.meshMatrix(Form::Vehicle);
            fi.velocity = v;
            fi.gravity = {0.0f, -core::config::kGravity, 0.0f};   // [HIGH: the rigid body uses the pawn gravity]
            fi.boneWorld = [&pc](const std::string& b, core::Mat4& o) { return pc.boneWorld(b, o); };
            const float amount = tickVehicleEffects(dt, fi);
            if (amount >= 0.0f) s.thrusterAmount = amount;
        }
        s.special180 = vs.quickTurnSerial != quickTurnSeen_;   // [Systems M08d / Gameplay 24b] TnTankForm PlayOneEightySound
        quickTurnSeen_ = vs.quickTurnSerial;
        tickVehicleAudio(dt, s);
        tireSlip_ = s.boostState && tireSlipOverride_ >= 0.0f ? tireSlipOverride_ : 0.0f;
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


// Gameplay: TnTruckForm.AttemptToRam (Driving.OnRigidBodyCollision / RigidBodyTrigger) [CONF native M03 P8].
// Only while nitro runs (RamState 1). The victim must be a TnPawn of another team with Mass <= MaxRamMass 1000,
// once per pawn per nitro (notifyRamHit owns that registry and the impact cue). Robot victims enter
// RammedReaction (Character::rammedAsRobot, dir = Normal(victim - rammer)); vehicle victims get
// AddVelocity(momentum/Mass) x 0.5 with momentum = rammer RB velocity + (0,0,ExtraRamZVelocity 7000).
// The slice spawns no other pawns: the weapon-test dummy (DamageTarget) is not a TnPawn and is not
// rammable in the original, so nothing is hit here until pawn victims exist. [PARTIAL: victim masses]
void World::gameplayRamContacts() {
    Character& pc = player_.pawn();
    const Character::VehicleState& vs = pc.vehicleState();
    if (pc.moveForm() != Form::Vehicle || !vs.driving || vs.nitroRemain <= 0.0f) return;
    for (auto& a : actors_) {
        auto* victim = dynamic_cast<Character*>(a.get());   // other pawns only
        if (!victim || victim == &pc) continue;
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

bool World::vehicleShotSocketWorld(core::Mat4& out) const {
    const Character& pc = player_.pawn();
    if (vehicleShotSerial_ == 0 || pc.form() != Form::Vehicle) return false;
    const SocketDef& vs = vehicleShotSocket_ == 1 ? pc.chassis().vehicleWeapon2 : pc.chassis().vehicleWeapon;
    core::Mat4 bm;
    if (!vs.valid || !pc.boneWorld(vs.bone, bm)) return false;
    out = bm * vs.local;
    return true;
}

bool World::spawnVehicleMuzzleFlash(const std::string& weaponClass, core::Vec3& muzzleOut) {
    core::Mat4 sw;
    if (!vehicleShotSocketWorld(sw)) return false;
    muzzleOut = {sw.m[12], sw.m[13], sw.m[14]};
    if (vehicleFlashSerial_ == vehicleShotSerial_) return true;          // NumShotsToFire traces: one flash per shot
    vehicleFlashSerial_ = vehicleShotSerial_;
    const WeaponFxTemplates* wfx = CharacterAudio::weaponFx(weaponClass);
    if (wfx && !wfx->muzzle.empty()) fx_.spawnMuzzleFlash(wfx->muzzle, sw);
    static const bool log = std::getenv("WFC_MUZZLELOG") != nullptr;
    if (log) LOG_INFO("VEHICLE MUZZLE shot %d socket %d %s at %.2f,%.2f,%.2f", vehicleShotSerial_, vehicleShotSocket_,
                      wfx ? wfx->muzzle.c_str() : "(no fx)", sw.m[12], sw.m[13], sw.m[14]);
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
const assets::SkinnedModel* World::weaponModelFor(const WeaponDef& d) {
    auto it = weaponModels_.find(d.id);
    if (it != weaponModels_.end()) return it->second.get();
    auto m = std::make_unique<assets::SkinnedModel>();
    const std::string ext = assetRoot() + "/../";
    bool ok = d.meshGltf && *d.meshGltf && assets::loadSkinnedGlb(ext + d.meshGltf, *m) && m->valid();
    if (ok) {
        if (d.animGltf && *d.animGltf) assets::loadAnimationsByName(ext + d.animGltf, *m);
        resolveModelTextures(*m);
    } else LOG_ERROR("weapon %s: mesh %s unavailable", d.id, d.meshGltf ? d.meshGltf : "-");
    const assets::SkinnedModel* raw = ok ? m.get() : nullptr;
    weaponModels_[d.id] = std::move(m);
    return raw;
}

// The weapon mesh drawn at the socket is the ACTIVE inventory weapon's (no other weapon may be shown in its place).
void World::syncShownWeapon() {
    const Weapon& w = player_.pawn().weapon();
    std::string id = w.def ? w.def->id : "IonBlaster";
    if (id == shownWeapon_) return;
    shownWeapon_ = id;
    // [integration M08] Systems' weapon audio (WP_Fire, reload / equip / impact) follows the equipped weapon; the
    // CharacterAudio weapon table is keyed by the TnWeapon class (was the Ion Blaster's for every weapon).
    setPlayerWeaponAudio("TransContent.TnWeapon" + id);
    if (id == "IonBlaster") weaponAnim_.setModel(weaponModel_.valid() ? &weaponModel_ : nullptr);
    else if (const assets::SkinnedModel* m = weaponModelFor(*w.def)) weaponAnim_.setModelGeneric(m, *w.def);
    else weaponAnim_.setModel(nullptr);
    weaponSeenShot_ = w.shotSerial; weaponSeenReload_ = w.reloadSerial;
}

// The class's own grenade bag: TR_MPPlayerCharacterData_p.<Class>_PCD_MP WeaponTypes give Scout FlashBangs, Scientist
// HealGrenades, Soldier FlakGrenades, Leader KamikazeMines to the class's own chassis [CONF authored]. The exported per-chassis
// on-foot list (TnDataProvider_Weapon restriction) does not cover grenade bags consistently (it omits these on the class's
// own chassis), so a WT_Grenades bag is accepted when it is the selection class's preset grenade; other grenades stay refused.
static bool classPresetGrenadeAllowed(const CharacterSelection* sel, const WeaponDef* wd) {
    if (!sel || !wd || wd->fire != WeaponFire::Grenade) return false;
    static const char* kClassGrenade[4] = {"KamikazeMines", "HealGrenades", "FlashBangs", "FlakGrenades"};   // Leader, Scientist, Scout, Soldier
    const int c = (int)sel->specialty;
    return c >= 0 && c < 4 && std::string(wd->id) == kClassGrenade[c];
}

std::vector<std::string> World::applyLoadout(const CharacterSelection* sel) {
    Character& pc = player_.pawn();
    const ChassisDef& d = pc.chassis();
    std::vector<std::string> refused;
    const bool custom = sel && sel->type == 0 && !sel->weapons.empty();
    const std::vector<std::string>& names = custom ? sel->weapons : d.iconicWeapons;
    std::vector<Weapon> robot;
    for (const std::string& n : names) {
        const WeaponDef* wd = findWeaponDef(n);
        bool allowed = !custom || std::find(d.allowedOnFoot.begin(), d.allowedOnFoot.end(), n) != d.allowedOnFoot.end() ||
                       classPresetGrenadeAllowed(sel, wd);
        if (!wd || !allowed) {
            refused.push_back(n);
            LOG_ERROR("loadout: weapon %s %s for chassis %s - not equipped", n.c_str(), !wd ? "unknown" : "not allowed (TnDataProvider_Weapon restriction)", d.id.c_str());
            continue;
        }
        robot.push_back(Weapon::fromDef(*wd));
    }
    std::vector<std::string> veh = (sel && sel->type == 0 && !sel->vehicleWeapons.empty()) ? sel->vehicleWeapons : d.iconicVehicleWeapons;
    pc.setLoadout(robot, veh);
    {   // [Systems M08d] the loadout's weapon classes (robot + vehicle weapons): their cues for this level, and the vehicle
        // weapon's class for vehicle-form fire sounds.
        std::vector<std::string> cls;
        for (const Weapon& rw : robot) if (rw.def) cls.push_back("TransContent.TnWeapon" + std::string(rw.def->id));
        std::string vehCls;
        for (const std::string& vn : veh) if (const WeaponDef* vd = findWeaponDef(vn)) {
            cls.push_back("TransContent.TnWeapon" + std::string(vd->id));
            if (vehCls.empty()) vehCls = cls.back();
        }
        preloadWeaponAudio(cls);
        setPlayerVehicleWeaponAudio(vehCls);
    }
    // TnCharacterApplier.ApplyAbilities: CharacterData.Abilities (custom selection, else the iconic preset).
    pc.setAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities);
    syncShownWeapon();
    return refused;
}

void World::tickWeaponPresentation(float dt) {
    if (player_.pawn().weaponChangeSerial() != seenWeaponChange_) { seenWeaponChange_ = player_.pawn().weaponChangeSerial(); syncShownWeapon(); }
    const Weapon& w = player_.pawn().weapon();
    if (weaponSounds_.sounds() != CharacterAudio::weaponAnimSounds(weaponClass_))
        weaponSounds_.set(CharacterAudio::weaponAnimSounds(weaponClass_));
    if (w.reloadSerial != weaponSeenReload_) {
        weaponSeenReload_ = w.reloadSerial;
        weaponAnim_.play(WeaponMesh::Event::Reload);
        weaponSounds_.play(WeaponSoundTimeline::Event::Reload);
    }
    if (w.shotSerial != weaponSeenShot_) {
        weaponSeenShot_ = w.shotSerial;
        weaponAnim_.play(WeaponMesh::Event::Fire);
        weaponSounds_.play(WeaponSoundTimeline::Event::Fire);
    }
    notifies_.clear();
    weaponAnim_.tick(dt, notifies_);
    weaponSoundsFired_.clear();
    weaponSounds_.tick(dt, weaponSoundsFired_);
    if (player_.pawn().hasWeapon()) {
        // Visual-mesh effects; its sound notifies are the Ion Blaster's, so the held class's timeline plays them.
        for (const WeaponNotify& n : notifies_)
            if (n.kind == WeaponNotify::Kind::Effect) handleWeaponNotify(n);
        for (const std::string* q : weaponSoundsFired_) handleWeaponNotify({WeaponNotify::Kind::Sound, 0.0f, *q, ""});
    }
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
    pickupEvents_.clear();
    matchEvents_.clear();
    destructibleEvents_.clear();
    hitClock_ += dt;
    static const bool hitchLog = std::getenv("WFC_HITCHLOG") != nullptr;   // game-thread gaps between ticks
    if (hitchLog) {
        static auto last = std::chrono::steady_clock::now();
        static int n = 0;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (++n > 1 && ms > 30.0) LOG_INFO("HITCH tick %d: %.1f ms since the previous tick", n, ms);
    }
    if (collision_.valid()) mapState_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    {   // Audio listener = camera (same pose the app hands to IAudio::setListener).
        render::Camera cam;
        player_.controller().updateCamera(cam);
        listenerPos_ = cam.pos;
    }
    // Health regeneration (robot blueprint HealthRegenParameters, both forms) for every live pawn.
    {
        Character& lp = player_.pawn();
        lp.regenBuffRemain_ = std::max(0.0f, lp.regenBuffRemain_ - dt);
        lp.fastCooldownRemain_ = std::max(0.0f, lp.fastCooldownRemain_ - dt);
        if (lp.ammoLockRemain_ > 0.0f) {                   // TnBuffLockAmmoClip: the clip does not drain while active
            lp.ammoLockRemain_ = std::max(0.0f, lp.ammoLockRemain_ - dt);
            lp.weapon().ammo = std::max(lp.weapon().ammo, lockedClip_);
        } else lockedClip_ = 0;
        if (matchActive_ && player_.controller().consumeKillstreakRequest()) triggerLocalKillstreak();
        if (player_.controller().consumeMeleeRequest()) startLocalMelee(false);
        if (player_.controller().consumeGrenadeRequest()) startLocalGrenadeToss();
        if (deferredKillstreak_ && lp.moveForm() == Form::Robot && !lp.isTransforming()) { deferredKillstreak_ = false; triggerLocalKillstreak(); }
    }
    tickAbilityEffects(dt);
    if (!localPlayerDead()) player_.pawn().health().tickRegen(dt, player_.pawn().regenBuffRemain_ > 0.0f ? 2.0f : 1.0f);
    for (MatchOpponent* o : opponents_) if (o->spawned()) o->health().tickRegen(dt);
    if (matchActive_) tickMatch(dt);
    if (!localPlayerDead()) {                       // dead / not yet spawned (match): no pawn simulation
        sysprof::Scope sp(sysprof::Ctrl);
        player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn
    }
    for (MatchOpponent* o : opponents_) o->simulate(dt, collision());   // participants: shared movement + animation
    player_.controller().tickCameraCollision(dt);   // obstruction behaviour after the pawn moved
    gameplayRamContacts();
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
            if (*weaponCue("WP_LoopingTail")) cues_.play(weaponCue("WP_LoopingTail"), te, core::length(te.pos - pp));
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
        // Soak-test hooks: periodic map-audio unload/reload and match resets (lifecycle validation only).
        static const float mapCycle = std::getenv("WFC_MAPAUDIO_CYCLE") ? (float)std::atof(std::getenv("WFC_MAPAUDIO_CYCLE")) : 0.0f;
        static const float resetCycle = std::getenv("WFC_MATCHRESET_CYCLE") ? (float)std::atof(std::getenv("WFC_MATCHRESET_CYCLE")) : 0.0f;
        // WFC_LEVELAUDIO_CYCLE="<seconds>:<level>[@<trigger>],<level>,..." walks the level list (the frontend lifecycle:
        // each step = a travel: unload, load, the frontend-owned trigger), e.g.
        // "20:UI_FrontEnd_m@FsCommand:enterFrontEnd,UI_PartyLobby_m,UI_Lobby_m,MP_IAC_Streets,UI_Lobby_m".
        static std::vector<std::string> levelCycle;
        static float levelCycleSecs = 0.0f;
        static size_t levelCycleIdx = 0;
        static bool levelCycleInit = false;
        if (!levelCycleInit) {
            levelCycleInit = true;
            if (const char* e = std::getenv("WFC_LEVELAUDIO_CYCLE")) {
                std::string v = e;
                const size_t colon = v.find(':');
                levelCycleSecs = (float)std::atof(v.substr(0, colon).c_str());
                std::stringstream ss(v.substr(colon + 1));
                for (std::string item; std::getline(ss, item, ',');) if (!item.empty()) levelCycle.push_back(item);
            }
        }
        if (!levelCycle.empty() && levelCycleSecs > 0.0f && (mapAudioCycleT_ += dt) >= levelCycleSecs) {
            mapAudioCycleT_ = 0.0f;
            const std::string& item = levelCycle[levelCycleIdx++ % levelCycle.size()];
            const size_t at = item.find('@');
            const auto t0 = std::chrono::steady_clock::now();
            loadMapAudio(item.substr(0, at));
            if (at != std::string::npos) levelAudioEvent(item.substr(at + 1));
            const auto st = levelAudio_.state();
            LOG_INFO("LEVELCYCLE %zu -> %s (%.1f ms): levelCues %d presets %d ops %d pcm %.1fMB voices %d live %d",
                     levelCycleIdx, item.c_str(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                     st.levelCues, st.levelPresets, levelAudio_.ambient().script().opCount(), st.pcmMB, st.voices, st.instances);
        } else if (mapCycle > 0.0f && (mapAudioCycleT_ += dt) >= mapCycle) {
            mapAudioCycleT_ = 0.0f;
            const std::string m = levelAudio_.level().empty() ? std::string("MP_IAC_Streets") : levelAudio_.level();
            unloadMapAudio();
            loadMapAudio(m);
        }
        if (resetCycle > 0.0f && (matchResetCycleT_ += dt) >= resetCycle) { matchResetCycleT_ = 0.0f; resetSystemsForMatch(); }
        levelAudio_.tick(dt, listenerPos_, player_.pawn().position());
        static const bool ambLog = std::getenv("WFC_AMBLOG") != nullptr;
        static float ambT = 0.0f;
        if (ambLog && (ambT += dt) >= 0.5f) {
            ambT = 0.0f;
            audio::MixStats ms;
            bool have = audio_ && audio_->mixStats(ms);
            LOG_INFO("AMB zone=%s emitters=%d/%d oneShots=%d cues=%zu occluded=%d rays/s=%.0f pending=%zu voices=%d (max %d, dropped %d, stolen %d) wet=%d peak=%.1fdB gr=%.1fdB mix=%.3fms/block live=%zu backendVoices=%d pcm=%.1fMB map=%s script=%d pools=%d music=%d/%d tl=%.1f",
                     levelAudio_.ambient().zoneName(), levelAudio_.ambient().activeEmitters(), levelAudio_.ambient().emitterCount(), levelAudio_.ambient().oneShotsPlayed(),
                     cues_.liveInstances(), cues_.occludedInstances(), occlusionRays_ / 0.5f, cues_.pendingEvents(), have ? ms.voices : -1,
                     have ? ms.peakVoices : -1, have ? ms.droppedVoices : -1, have ? ms.stolenVoices : -1, have ? ms.wetVoices : -1,
                     have ? ms.peakDb : -96.0f, have ? ms.gainReductionDb : 0.0f, have ? ms.mixMsPerBlock : 0.0f,
                     cues_.liveInstances(), audio_ ? audio_->activeVoices() : -1, audio_ ? audio_->residentBytes() / 1048576.0 : 0.0,
                     levelAudio_.level().c_str(), levelAudio_.ambient().script().liveSounds(), levelAudio_.state().poolsPlaying,
                     levelAudio_.state().musicState, levelAudio_.state().musicInstance, levelAudio_.state().timelinePos);
            if (cues_.pendingEvents() > 30) LOG_INFO("AMB pending: %s", cues_.pendingSummary().c_str());
            occlusionRays_ = 0;
        }
        cues_.tick(dt);
    }

    tickHazards(dt);
    tickProjectiles(dt);
    if (player_.pawn().position().y < killZ_ && !localPlayerDead()) {
        // Below KillZ: FellOutOfWorld -> Died with no killer (an environmental death in a match).
        LOG_INFO("World: player fell out of world; %s", matchActive_ ? "killed (KillZ)" : "respawning");
        if (matchActive_) killLocalPlayer(-1, false, "Engine.DmgType_Fell");   // WorldInfo.KillZDamageType [HIGH: stock default]
        else respawnPlayer();
    }
    for (auto& a : actors_) if (a->alive()) a->tick(*this, dt);
    // Pickup glue (Systems M04 integration preview): Gameplay's PickupFactory raises one PickupEvent per
    // transition. Taken -> Inventory.AnnouncePickup: Systems plays the PickupSound once, attached to the
    // receiving pawn. Respawned plays nothing (RespawnEffect empty). Effect/mesh visibility is
    // Rendering's (setMapEffectState, driven by Gameplay). Joined by authored actor name.
    for (const PickupEvent& e : pickupEvents_)
        if (e.type == PickupEvent::Type::Taken && e.factory >= 0 && (size_t)e.factory < pickupFactories_.size())
            pickupFx_.onTaken(pickupFactories_[(size_t)e.factory]->name().c_str(), cues_, atPawn(),
                              core::length(e.receiverPos - listenerPos_));
    for (size_t i = 0; i < actors_.size();) {
        if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); }
        else ++i;
    }
}

// Local match host glue (TnMultiplayerGame / TnTeamGame on the authority): the Match decides spawns, deaths and the
// end; World applies them to the local pawn and the map actors.
void World::startLocalMatch(const MatchSettings& s, int localTeam) {
    if (match_.starts().empty()) {
        std::string root = assetRoot();
        match_.loadSpawnData(mapDir() + "gameplay.json");
    }
    match_.setChassisCheck([this](const std::string& id, std::string& err) {
        const ChassisAssets* a = chassisAssets(id);
        if (a && a->ok) return true;
        err = a ? a->error : std::string("unknown chassis");
        return false;
    });
    match_.begin(s);
    if (localPlayer_ < 0) localPlayer_ = match_.addPlayer("Player");
    if (s.teamGame && (localTeam == 0 || localTeam == 1)) {
        match_.playerMutable(localPlayer_).team = localTeam;   // [integration M08b] the lobby's team (before the login start)
        LOG_INFO("match: local player team %d from the lobby", localTeam);
    }
    matchActive_ = true;
    localDead_ = true;            // PendingMatch: TrySpawnPlayer false -> nobody spawns before the start
    // GameInfo.Login: the controller is created at FindPlayerStart (team start of the initial cluster) and spectates
    // from there until the match spawns its pawn [HIGH]. The (not yet existing) pawn is parked at rest at that start.
    int ls = match_.loginStart(localPlayer_);
    if (ls >= 0) {
        const Match::Start& st = match_.starts()[(size_t)ls];
        Character& pc = player_.pawn();
        pc.respawnReset();
        core::Vec3 p = st.pos; float gy; core::Vec3 gn;
        if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
        pc.setPosition(p); pc.setYaw(st.yaw); pc.groundY = p.y;
        player_.controller().setCameraYaw(st.yaw);
        player_.controller().setSpectatorView(st.pos + core::Vec3{0, core::config::kPawnHalfHeight, 0}, st.yaw);
    }
}

bool MatchLaunch::fromURL(const std::string& url, MatchLaunch& out) {
    // "<Map>?Key=Value?Key=Value..." (ServerTravel / StartLevel URL).
    size_t q = url.find('?');
    std::string map = url.substr(0, q);
    std::string lower = map;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    out.map = World::canonicalMapName(map);
    std::map<std::string, std::string> opt;
    while (q != std::string::npos) {
        size_t next = url.find('?', q + 1);
        std::string kv = url.substr(q + 1, next == std::string::npos ? std::string::npos : next - q - 1);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) opt[kv.substr(0, eq)] = kv.substr(eq + 1);
        q = next;
    }
    out.modeTag = opt.count("GameModeTag") ? opt["GameModeTag"] : "TDM";
    out.settings = MatchSettings::forMode(out.modeTag);
    out.settings.modeTag = out.modeTag;
    // TnMultiplayerGame.InitGame: GoalScore = max(0, PointsToWin); GameInfo.InitGame: TimeLimit = max(0, TimeLimit) s.
    if (opt.count("PointsToWin")) out.settings.goalScore = std::max(0, std::atoi(opt["PointsToWin"].c_str()));
    // RoundsBase: GRI.Rounds = GoalScore for the round-based rule (CTF; must be even) [CONF RE §4].
    if (out.settings.rounds > 0 && opt.count("PointsToWin")) { out.settings.rounds = std::max(2, std::atoi(opt["PointsToWin"].c_str())); out.settings.goalScore = 1000000; }
    if (opt.count("TimeLimit")) out.settings.timeLimit = std::max(0, (int)std::atof(opt["TimeLimit"].c_str()));
    return !out.map.empty();
}

void World::resetForNewLevel() {
    // A new match is a fresh load of the map in the original (MatchOver -> ReturnToGameLobby -> ServerTravel).
    mapState_.resetForNewMatch();
    if (collision_.valid()) mapState_.tick(0.0f, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    for (Destructible* d : destructibles_) d->resetForNewMatch();
    for (PickupFactory* f : pickupFactories_) f->resetToPickup(*this);
}

bool World::launchMatch(const MatchLaunch& l) {
    if (canonicalMapName(l.map) != mapName_ || !usingSlice_) {
        LOG_WARN("match: map %s is not the loaded map (%s); the world loads one map per session", l.map.c_str(), mapName_.c_str());
        return false;
    }
    MatchMode mode = MatchMode::DM;
    bool known = false;
    for (MatchMode m : {MatchMode::DM, MatchMode::TDM, MatchMode::CTF, MatchMode::KOTH, MatchMode::EXT, MatchMode::DOM})
        if (l.modeTag == gameModeName(m)) { mode = m; known = true; }
    if (!known) { LOG_WARN("match: unknown mode %s", l.modeTag.c_str()); return false; }
    // All six versus modes run on the shared framework (CTF rounds / EXT bomb: Pass 22).
    matchMode_ = mode;
    mapState_.setMode(mode);
    resetForNewLevel();
    startLocalMatch(l.settings, l.localTeam);
    LOG_INFO("match: launched %s %s (goal %d, time %d s)", l.map.c_str(), l.modeTag.c_str(), l.settings.goalScore, l.settings.timeLimit);
    return true;
}

bool World::applyMatchDamage(int victim, int instigator, float amount, bool aoe, const std::string& damageType) {
    if (!matchActive_ || match_.state() != Match::State::InProgress || victim < 0 || (size_t)victim >= match_.players().size()) return false;
    if (qaGod_ && victim == localPlayer_) return false;   // DEV / QA TOOLING god mode
    if (!match_.players()[(size_t)victim].alive) return false;
    // TnPlayerPawn.TakeDamage: teammates' damage is discarded except TnDamageTypeAOE (NotifyHitByFriendlyFire).
    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;
    Health* h = nullptr;
    MatchOpponent* opp = nullptr;
    if (victim == localPlayer_) h = &player_.pawn().health();
    else for (MatchOpponent* o : opponents_) if (o->matchPlayer() == victim) { opp = o; h = &o->health(); }
    if (!h) return false;
    // Form damage multiplier (ROBODEF / VEHDEF DamageMultiplier, e.g. vehicles 0.75-0.9) and self damage
    // (SelfDamageMultiplier 0.45) of the victim pawn [CONF data; HIGH: applied in TnPawn.TakeDamage / AdjustDamage].
    {
        const Character* vp = victim == localPlayer_ ? &player_.pawn() : (opp ? &opp->pawn() : nullptr);
        if (vp) {
            const bool veh = vp->moveForm() == Form::Vehicle;
            amount *= veh ? vp->vehicleParams().damageMultiplier : vp->robotParams().damageMultiplier;
            if (instigator == victim) amount *= veh ? vp->vehicleParams().selfDamageMultiplier : vp->robotParams().selfDamageMultiplier;
            if (vp->warcryRemain_ > 0.0f) amount *= vp->warcryTakenMul_;          // TnBuffWarcryDecreaseDamageTaken
            if (vp->hardLockedRemain_ > 0.0f) amount *= 1.4f;   // TnBuffHardLocked FloatModifier[0] in _AllDamageModifierSelf [CONF RE §K]
        }
        const Character* ip = instigator == localPlayer_ ? &player_.pawn() : nullptr;
        if (!ip) for (MatchOpponent* o : opponents_) if (o->matchPlayer() == instigator) ip = &o->pawn();
        if (ip && instigator != victim && ip->warcryRemain_ > 0.0f) amount *= ip->warcryDamageMul_;   // TnBuffWarcryIncreaseDamage
        if (ip && instigator != victim && ip->hoverState_ == 2) amount *= 1.4f;   // TnBuffIncreaseDamageDuringHover [0]
        if (ip && instigator != victim && ip->beaconDamageBuff_ > 0.0f) amount *= 1.15f;   // TnBuffAmmoBeaconIncreaseDamage [0]
    }
    float applied = h->applyDamage(amount);
    if (applied > 0.0f) {                                   // TnPlayerPawn.TakeDamage -> ExposeSelf
        if (victim == localPlayer_) player_.pawn().exposeSelf(); else if (opp) opp->pawn().exposeSelf();
    }
    match_.recordDamage(victim, instigator, applied);
    if (victim == localPlayer_ && applied > 0.0f && instigator != victim) { ++damageTakenCount_; lastDamageFrom_ = match_.playerLocation(instigator); }
    if (h->isDead() && instigator >= 0 && instigator != victim) {
        Character* kp = instigator == localPlayer_ ? (localDead_ ? nullptr : &player_.pawn()) : nullptr;
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == instigator && o->spawned()) kp = &o->pawn();
        if (kp && kp->refillOnKillRemain_ > 0.0f) kp->health().heal(Health::HealType::AddAllSegments, 1.0f);   // TnBuffRefillHealthOnKill
    }
    if (h->isDead()) {
        if (victim == localPlayer_) killLocalPlayer(instigator, false, damageType);
        else { match_.killed(instigator, victim, false, damageType); if (opp) opp->despawn(); }
    }
    return true;
}

MatchOpponent* World::addMatchOpponent(const std::string& name, bool drawn) {
    int p = match_.addPlayer(name);
    auto o = std::make_unique<MatchOpponent>(p, match_.players()[(size_t)p].team, drawn);
    MatchOpponent* raw = o.get();
    opponents_.push_back(raw);
    actors_.push_back(std::move(o));
    return raw;
}

HudGameState World::hudState() const {
    HudGameState h;
    const Character& pc = player_.pawn();
    h.alive = !localPlayerDead();
    h.health = pc.health().current; h.healthMax = pc.health().max;
    h.overshield = pc.health().overshield(); h.normalizedOverShield = pc.health().normalizedOverShield();
    h.activeSegment = pc.health().activeSegment(); h.segmentCount = pc.health().segmentCount;
    h.clipAmmo = pc.weapon().ammo; h.reserveAmmo = pc.weapon().reserve;
    h.weaponName = pc.weapon().name;
    h.weaponId = pc.weapon().def ? pc.weapon().def->provider : "IonBlaster";
    h.weaponSimulated = pc.weapon().simulated();
    h.weaponIcon = pc.weapon().def ? pc.weapon().def->killFeedIcon : "death_IonBlaster";
    h.weaponSwitching = pc.switchingWeapon();
    for (const Weapon& iw : pc.inventory()) h.inventory.push_back(iw.def ? iw.def->provider : "IonBlaster");
    h.activeWeapon = pc.activeWeaponIndex();
    h.vehicleWeapons = pc.vehicleWeapons();
    h.loadoutRefused = loadoutRefused_;
    h.attackingTeam = match_.attackingTeam(); h.currentRound = match_.currentRound(); h.rounds = match_.settings().rounds;
    h.attackingTeamIndex = (matchMode_ == MatchMode::CTF || matchMode_ == MatchMode::EXT) && (h.attackingTeam == 0 || h.attackingTeam == 1) ? h.attackingTeam : -1;
    h.currentObjectiveCountdown = mapState_.planted().active ? (int)std::ceil(mapState_.planted().fuse) : -1;
    h.betweenRounds = match_.betweenRounds();
    for (const MapState::Carried& c : mapState_.carried())
        h.carried.push_back({c.kind, c.holder, c.holderTeam, c.dropped, c.active, c.pos, c.autoReturn, c.returnLeft, c.sleep});
    h.bombPlanted = mapState_.planted().active; h.bombFuse = mapState_.planted().fuse; h.bombDefuse = mapState_.planted().defuse;
    h.bombPlantTeam = mapState_.planted().team;
    h.localCarrying = matchActive_ && mapState_.carriedBy(localPlayer_) >= 0;
    if (matchActive_ && localPlayer_ >= 0 && (size_t)localPlayer_ < match_.players().size()) {
        const MatchPlayer& mp = match_.players()[(size_t)localPlayer_];
        h.killStreak = mp.currentKillStreak; h.killstreaks = mp.acquiredKillstreaks;
        if (!mp.acquiredKillstreaks.empty()) if (const KillstreakDef* k = killstreakById(mp.acquiredKillstreaks.back())) h.killstreakImplemented = k->implemented;
    }
    h.regenBuff = pc.regenBuffRemain_; h.fastCooldownBuff = pc.fastCooldownRemain_; h.ammoLockBuff = pc.ammoLockRemain_;
    for (const Character::AbilitySlot& a : pc.abilities_) h.abilities.push_back({a.id, a.implemented, a.cooldown, a.pendingCooldown});
    h.dodging = pc.isDodging();
    h.cloaked = pc.cloakRemain_ > 0.0f;
    h.hoverState = pc.hoverState_;
    h.lockTarget = lockTarget_; h.locked = locked_;
    h.barrier = barrier_.alive; h.barrierHealth = barrier_.health;
    h.repairBeam = repairBeam_.active && repairBeam_.time > 0.0f; h.repairBeamHealing = repairBeam_.healing;
    h.repairBeamStart = repairBeam_.start; h.repairBeamEnd = repairBeam_.end; h.repairBeamTarget = repairBeam_.target;
    h.vehicleShotSerial = vehicleShotSerial_; h.vehicleShotSocket = vehicleShotSocket_; h.vehicleShotMuzzle = vehicleShotMuzzle_;
    if (matchActive_ && !localDead_) {
        MapState::ObjPawn op{localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, pc.form() == Form::Robot && !pc.isTransforming() && !pc.isMeleeing()};
        int ci = mapState_.pickupCandidate(op);
        h.pickupPrompt = ci < 0 ? "" : mapState_.carried()[(size_t)ci].kind == 0 ? "Code Of Power" : "Bomb";
    }
    h.ammoBeacon = beacon_.alive; h.ammoBeaconPos = beacon_.pos; h.ammoBeaconLife = beacon_.life; h.ammoBeaconHealth = beacon_.health;
    h.ammoBeaconBuff = pc.beaconDamageBuff_ > 0.0f;
    h.drain = pc.drainRemain_;
    h.kamikazeMines = (int)mines_.size(); h.tempWeaponLeft = pc.tempWeapon_ == 1 ? pc.tempWeaponRemain_ : 0.0f;
    h.roller = roller_.alive; h.rollerArmed = roller_.alive && roller_.t >= 3.0f; h.rollerPos = roller_.pos;
    h.rollerFuse = roller_.alive ? std::max(0.0f, 10.0f - roller_.t) : 0.0f; h.rollerHealth = roller_.health; h.rollerSlow = pc.rollerSlowRemain_;
    h.guidedMissile = missile_.alive; h.guidedMissilePos = missile_.pos; h.guidedMissileFuse = missile_.life;
    h.sentry = sentry_.alive; h.sentryHealth = sentry_.health; h.sentryPos = sentry_.pos; h.sentryTarget = sentry_.target;
    h.seeEnemies = pc.seeEnemiesRemain_; h.refillOnKill = pc.refillOnKillRemain_; h.abilitiesJammed = pc.jammedRemain_; h.hardLocked = pc.hardLockedRemain_;
    h.heavyWeapon = pc.carryingHeavy_ == 1 ? "Code Of Power" : pc.carryingHeavy_ == 2 ? "Bomb" : "";   // ItemName
    { const Weapon* gb = grenadeBag(pc); h.grenades = gb ? gb->reserve : -1; }
    {
        const Weapon* aw = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();
        h.lockProgress = aw && aw->lockOnTime > 0.0f ? std::min(1.0f, lockTimer_ / aw->lockOnTime) : 0.0f;
        if (locked_) h.lockProgress = 1.0f;
    }
    h.damageTakenCount = damageTakenCount_;
    h.lastDamageFrom = lastDamageFrom_;
    if (damageTakenCount_ > 0) {
        core::Vec3 d = lastDamageFrom_ - pc.position();
        core::Vec3 f = core::forwardFromYawPitch(player_.controller().viewYaw(), 0.0f);
        core::Vec3 r{-f.z, 0.0f, f.x};
        h.lastDamageBearing = std::atan2(core::dot(d, r), core::dot(d, f));
    }
    h.vehicleForm = pc.moveForm() == Form::Vehicle; h.transforming = pc.isTransforming();
    h.cantTransformCount = player_.controller().cantTransformCount();
    if (matchActive_ && localPlayer_ >= 0 && (size_t)localPlayer_ < match_.players().size()) {
        const MatchPlayer& mp = match_.players()[(size_t)localPlayer_];
        h.selectedChassis = mp.chassis; h.drawnChassis = mp.alive ? localChassis_ : std::string();
        h.specialty = mp.specialty; h.spawnError = mp.spawnError;
    }
    h.matchActive = matchActive_;
    if (!matchActive_ || localPlayer_ < 0) return h;
    const MatchPlayer& me = match_.players()[(size_t)localPlayer_];
    h.timeToRespawn = me.timeToRespawn;
    h.spectating = match_.spectating(localPlayer_);
    h.timeLimit = match_.settings().timeLimit;
    h.faction = match_.faction(localPlayer_);
    h.endReason = match_.endReason();
    h.winnerPlayer = match_.winnerPlayer();
    h.matchOverTimeLeft = match_.matchOverTimeLeft();
    h.killFeed = match_.killFeed();
    for (size_t i = 0; i < match_.players().size(); ++i) {
        const MatchPlayer& p = match_.players()[i];
        h.scoreboard.push_back({(int)i, p.name, p.team, p.score, p.kills, p.deaths, p.assists, p.alive, (int)i == localPlayer_});
    }
    h.modeTag = match_.settings().modeTag;
    h.matchState = (int)match_.state(); h.gameStatus = match_.gameStatus();
    h.countdown = match_.countdown(); h.remainingTime = match_.remainingTime(); h.elapsedTime = match_.elapsedTime();
    h.goalScore = match_.settings().goalScore;
    h.teamScore[0] = match_.teamScore(0); h.teamScore[1] = match_.teamScore(1);
    h.myTeam = me.team; h.score = me.score; h.kills = me.kills; h.deaths = me.deaths; h.assists = me.assists;
    h.winnerTeam = match_.winnerTeam();
    if (match_.state() == Match::State::MatchOver || match_.state() == Match::State::Returned)
        h.result = !match_.settings().teamGame
                       ? std::string()   // TnFreeForAllGameOverMessage sets an empty GameOverMessage [CONF RE OVERNIGHT A5]
                       : (match_.winnerTeam() < 0 ? "Tie game" : (match_.winnerTeam() == me.team ? "Your team won" : "Your team lost"));
    for (const ObjectiveObject& o : mapState_.objectives()) {
        if (!o.activeInMode) continue;
        HudGameState::Objective ob;
        ob.actor = o.actor; ob.markerType = o.markerTypeString; ob.pointNumber = o.pointNumber; ob.ownerTeam = o.defenderTeam;
        ob.active = o.cls == "TnDominationPoint" || o.cls == "TnBombPlantPoint" || o.state == ObjectiveObject::State::Active;
        // Flag / bomb factories: active while their objective is in state Pickup at home (not carried / dropped / asleep).
        for (const MapState::Carried& c : mapState_.carried())
            if (&mapState_.objectives()[(size_t)c.home] == &o) ob.active = c.active && c.holder < 0 && !c.dropped && c.sleep <= 0.0f;
        ob.ownerTeam = (o.cls == "TnDominationPoint" || o.cls == "TnKingOfTheHillZone") ? o.defenderTeam : o.authoredTeam;
        ob.captureProgress = o.captureTime / 20.0f; ob.beingCaptured = o.captureTime > 0.0f;
        ob.timeLeft = o.activeTimeLeft; ob.pos = o.pos;
        h.objectives.push_back(ob);
    }
    for (size_t i = 0; i < match_.players().size(); ++i) {
        if ((int)i == localPlayer_ || !match_.players()[i].alive) continue;   // hidden for yourself and dead pawns
        HudGameState::Tag t;
        t.player = (int)i; t.name = match_.players()[i].name; t.team = match_.players()[i].team;
        t.ally = match_.settings().teamGame && t.team == me.team;
        t.drawn = t.ally;                                                       // enemy marker disabled by default
        t.label = t.drawn;
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == (int)i) {
            t.pos = o->position();
            // SetupEnemyMarker: drawn with TnBuffSeeEnemyObjectiveMarkers unless the enemy has a Warcry buff, or when the enemy is
            // HardLocked by the displayer's team [CONF TnObjectiveMarkerTypeTransformerVersus].
            if (!t.ally && ((pc.seeEnemiesRemain_ > 0.0f && o->pawn().warcryRemain_ <= 0.0f) ||
                            (o->pawn().hardLockedRemain_ > 0.0f && o->pawn().hardLockedByTeam_ == me.team))) { t.drawn = true; t.label = true; }
            if (o->pawn().cloakRemain_ > 0.0f) t.label = false;   // TnBuffCloak: DisableLabel
        }
        h.tags.push_back(t);
    }
    return h;
}

void World::killLocalPlayer(int killer, bool suicide, const std::string& damageType) {
    if (!matchActive_ || localDead_) return;
    match_.killed(killer, localPlayer_, suicide, damageType);
    localDead_ = true;
}

void World::tickMatch(float dt) {
    Character& pc = player_.pawn();
    if (!localDead_) {
        match_.setPlayerLocation(localPlayer_, pc.position());
        if (pc.health().isDead()) killLocalPlayer(-1, false);   // damage without an instigator
    }
    for (MatchOpponent* o : opponents_) if (o->spawned()) match_.setPlayerLocation(o->matchPlayer(), o->position());
    match_.tick(dt);
    // Live objective rules (DOM nodes, KOTH zone) while InProgress: pawns -> captures / scores -> the match rules.
    if (match_.state() == Match::State::InProgress && !match_.betweenRounds()) {
        mapState_.setKillZ(killZ_);
        std::vector<MapState::ObjPawn> pawns;
        // Carriers: Transform to vehicle drops the heavy weapon (TnPawn.Transform -> DropHeavyWeapons) [CONF].
        if (!localDead_ && (pc.isTransforming() || pc.form() == Form::Vehicle)) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());
        if (!localDead_ && pc.heavyDropRequested_) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());
        pc.heavyDropRequested_ = false;
        for (MatchOpponent* o : opponents_)
            if (o->spawned() && (o->pawn().isTransforming() || o->pawn().form() == Form::Vehicle)) mapState_.dropCarriedBy(o->matchPlayer(), o->pawn().actorLocation());
        auto robotForm = [](const Character& c) { return c.form() == Form::Robot && !c.isTransforming() && !c.isMeleeing(); };
        const bool localPickup = player_.controller().consumePickupRequest();
        if (!localDead_) pawns.push_back({localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, robotForm(pc), localPickup});
        for (MatchOpponent* o : opponents_)
            if (o->spawned()) pawns.push_back({o->matchPlayer(), o->team(), o->pawn().actorLocation(), true, robotForm(o->pawn()), o->pressesPickup});
        MapState::ObjectiveScoring sc;
        mapState_.tickObjectives(dt, pawns, sc);
        for (auto& p : sc.personalScores) match_.addPersonalScore(p.first, p.second);
        for (auto& p : sc.objectiveScores) match_.scoreObjective(p.first, p.second);
        for (auto& t : sc.teamScores) match_.scoreTeamObjective(t.first, t.second);
        for (auto& msg : sc.messages) {
            LOG_INFO("match: %s switch %d", msg.first.c_str(), msg.second);
            matchAudio().objectiveBroadcast(msg.first, msg.second);   // [Systems M08f] the message class's audio
        }
        {   // [Systems M08f] KOTH announcer: Active.BeginState -> ZoneChangeSound; DefendingTeamChanged -> captured / contested /
            // neutral, not for the activation's own UpdateClaim (IgnoringTeamChangeAnnouncement) nor once the match is over.
            const int kz = mapState_.activeKothZone();
            const int def = kz >= 0 ? mapState_.objectives()[(size_t)kz].defenderTeam : 255;
            if (kz >= 0 && kz != kothAudioZone_) matchAudio().kothZoneActivated(!matchActive_);
            else if (kz >= 0 && def != kothAudioDefender_) matchAudio().kothDefenderChanged(def, false, !matchActive_);
            kothAudioZone_ = kz; kothAudioDefender_ = def;
            // TnGameReplicationInfoMultiplayer.OnObjectiveCountdownChange (the planted bomb's fuse)
            const int oc = mapState_.planted().active ? (int)std::ceil(mapState_.planted().fuse) : -1;
            if (oc != objCountdownAudio_) { objCountdownAudio_ = oc; matchAudio().objectiveCountdownChanged(oc); }
        }
        if (sc.attackingTeam >= 0) match_.setAttackingTeam(sc.attackingTeam);
        // The carrier holds the heavy weapon (TnWeaponFlag1Hand MWT_Flag / TnWeaponBomb MWT_Bomb, WT_Heavy) [CONF].
        { int ci = mapState_.carriedBy(localPlayer_); pc.carryingHeavy_ = ci >= 0 ? mapState_.carried()[(size_t)ci].kind + 1 : 0; }
        // HurtRadius (bomb detonation, AOE): every match pawn within the radius.
        for (const auto& rd : sc.radiusDamage) {
            LOG_INFO("match: HurtRadius %.0f within %.0f m (%s)", rd.damage, rd.radius, rd.damageType.c_str());
            if (!localDead_ && core::length(pc.actorLocation() - rd.pos) <= rd.radius)
                applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType);
            for (MatchOpponent* o : opponents_)
                if (o->spawned() && core::length(o->position() - rd.pos) <= rd.radius)
                    applyMatchDamage(o->matchPlayer(), rd.instigator, rd.damage, true, rd.damageType);
        }
        std::vector<Match::SpawnModifier> mods;
        for (const ObjectiveObject& o : mapState_.objectives()) {
            if (!o.activeInMode) continue;
            if (o.cls == "TnKingOfTheHillZone" && o.state == ObjectiveObject::State::Active) mods.push_back({o.pos, -50.0f, 5000.0f, 0, 255});
            if (o.cls == "TnDominationPoint" && (o.defenderTeam == 0 || o.defenderTeam == 1)) mods.push_back({o.pos, 1.0f, -1.0f, 1, o.defenderTeam});
        }
        match_.setObjectiveSpawnModifiers(mods);
    }
    bool returned = false;
    for (const MatchEvent& e : match_.events()) {
        switch (e.type) {
            case MatchEvent::Type::MatchEnded:
                for (size_t i = 0; i < match_.players().size(); ++i) match_.playerMutable((int)i).acquiredKillstreaks.clear();   // ClientGameEnded
                mapState_.matchEnded();                // ScoreKingOfTheHill.CheckEndGame: every zone deactivates
                // [integration M06, Systems M07 patch] HandleEndGame(Winner) -> game-type message switch 2 (end music)
                // + the versus game-over line; winner team -1 = tie.
                matchAudio().onMatchEnded(e.value, e.player == localPlayer_);
                break;
            case MatchEvent::Type::RoundStarted:
                mapState_.roundStart(e.value);         // SingleFlagCTF.SetupRoundStart (attacking team)
                break;
            case MatchEvent::Type::CountdownTick:
                matchAudio().countdownChanged(e.value, true);   // [Systems M08f] GRI.OnCountdownChange: 10..0 ticks
                break;
            case MatchEvent::Type::MatchStarted: {
                mapState_.matchStarting();             // KOTH initial zone (MatchStarting); CTF / EXT carried objectives
                matchAudio().kothMatchStarting();      // [Systems M08f] StartIgnoringAnnouncer(AnnouncerMatchStartHysteresisTime)
                // TnTeamGame.StartMatch: Reset() every pickup factory (sleeping factories return to 'Pickup').
                for (PickupFactory* f : pickupFactories_) f->resetToPickup(*this);
                // [integration M06, Systems M07 patch] TnGameRules.HandleStartGame -> the mode's game-type message
                // (announcer GameTypeDialog + GameDescriptionDialog, GameTypeMusic). Gameplay decides when; Systems plays.
                const int team = localPlayer_ >= 0 ? match_.players()[(size_t)localPlayer_].team : 0;
                matchAudio().onMatchStarted(match_.settings().modeTag, team == 1 ? 1 : 0);
                break;
            }
            case MatchEvent::Type::GameNearlyComplete:
                matchAudio().onGameNearlyComplete();           // Systems: GameNearlyCompleteMusic (final stretch)
                break;
            case MatchEvent::Type::TimeAnnouncement:           // TnGameProgressAnnouncementMessage switch 0..2
            case MatchEvent::Type::KillsLeftAnnouncement:      // switch 5..7
            case MatchEvent::Type::PointsLeftAnnouncement:     // switch 3 / 4 (DOM / KOTH)
                matchAudio().onProgressAnnouncement(e.value);
                break;
            case MatchEvent::Type::PlayerSpawned:
                if (e.player == localPlayer_ && e.value >= 0) {
                    // RestartPlayer: a fresh pawn (robot form, full health, default inventory) at the chosen start,
                    // with its authored rotation.
                    const Match::Start& st = match_.starts()[(size_t)e.value];
                    // TnPawn.PostBeginPlay -> ApplyTransformer(chassis), then SetPlayerDefaults -> ApplyCharacter ->
                    // ApplySpecialty (StartingForm forced to robot) [CONF script, RE TARGETED_PASS3 §A].
                    MatchPlayer& mp = match_.playerMutable(localPlayer_);
                    applyChassisToLocalPawn(mp.chassis);
                    pc.respawnReset();   // a fresh pawn: robot form, no fold, HealthMax, default inventory
                    {
                        const ChassisAssets* ca = chassisAssets(mp.chassis);
                        mp.specialty = mp.selection.type == 0 ? specialtyName(mp.selection.specialty)
                                                              : (ca ? ca->def.iconicSpecialty : std::string());   // CharacterData.Specialty of the preset [CONF RE]
                        if (const SpecialtyDef* sd = specialtyDef(mp.specialty))
                            pc.setSpecialty(sd->id, sd->speedMultiplier, sd->segments, sd->overshield);
                        else pc.clearSpecialty();
                        loadoutRefused_ = applyLoadout(&mp.selection);
                        mp.healthMax = pc.health().max;
                    }
                    core::Vec3 p = st.pos;
                    float gy; core::Vec3 gn;
                    if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                    pc.setPosition(p); pc.setYaw(st.yaw); pc.velocity() = {0, 0, 0}; pc.groundY = p.y;
                    player_.controller().setCameraYaw(st.yaw);
                    localDead_ = false;
                    player_.controller().clearSpectatorView();
                    LOG_INFO("match: local player spawned at %s (%s team %d)", st.actor.c_str(), st.cluster.c_str(),
                             match_.players()[(size_t)localPlayer_].team);
                }
                for (MatchOpponent* o : opponents_)
                    if (o->matchPlayer() == e.player && e.value >= 0) {
                        MatchPlayer& op = match_.playerMutable(o->matchPlayer());
                        if (applyChassisToPawn(o->pawn(), op.chassis)) applyCharacterTo(o->pawn(), &op.selection, &op);
                        core::Vec3 p = match_.starts()[(size_t)e.value].pos;
                        float gy; core::Vec3 gn;
                        if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                        o->spawnAt(p);
                    }
                break;
            case MatchEvent::Type::PlayerKilled:
                if (e.player == localPlayer_) localDead_ = true;
                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == e.player) o->despawn();
                break;
            case MatchEvent::Type::ReturnToLobby:
                // TnGame.ReturnToGameLobby: the host (front end / Integration) decides what follows; the local
                // runtime stops the match here and returns to free play.
                LOG_INFO("match: return to lobby");
                player_.controller().clearSpectatorView();
                matchActive_ = false;
                localDead_ = false;
                returned = true;
                break;
            default: break;
        }
    }
    (void)returned;
    matchEvents_ = match_.events();
    match_.clearEvents();                    // consumed by the host
}

std::string World::collisionActorsAt(const core::Vec3& p, float pad, int maxNames) const {
    std::string out;
    int n = 0;
    bool inBsp = false;
    for (const ColActor& a : colActors_) {
        if (p.x < a.lo.x - pad || p.x > a.hi.x + pad || p.y < a.lo.y - pad || p.y > a.hi.y + pad ||
            p.z < a.lo.z - pad || p.z > a.hi.z + pad) continue;
        if (a.kind == "bsp") { inBsp = true; continue; }
        if (n++ >= maxNames) continue;
        if (!out.empty()) out += ",";
        out += a.name;
    }
    if (n > maxNames) out += ",+" + std::to_string(n - maxNames);
    if (out.empty()) out = inBsp ? "BSP" : "-";
    return out;
}

// Gameplay owns the runtime state of the authored map; the renderer only draws it. Pushed every frame:
// the map clock (movers / totem idle animation), actor bHidden (Kismet UnHide of the objective bases, DOM-only
// totems, the Active KOTH zone, Disabled objective factories) and the pickup factory presentation
// (TnPickupFactory.SetPickupVisible / SetPickupHidden).
void World::syncMapPresentation(render::IRenderer& r) const {
    if (pushedRulesMode_ != (int)mapState_.mode()) { r.setActiveGameRules(mapState_.gameRules()); pushedRulesMode_ = (int)mapState_.mode(); }
    r.setMapClock(mapState_.clock());
    for (const MapState::ActorVisibility& v : mapState_.actorVisibility()) r.setActorHidden(v.actor, v.hidden);
    for (const PickupFactory* f : pickupFactories_) {
        std::string a = f->name();
        size_t dot = a.rfind('.');
        if (dot != std::string::npos) a = a.substr(dot + 1);
        r.setMapEffectState(a + "|custom", f->customEffectActive(), !f->meshVisible());
        r.setMapEffectState(a + "|highlight", f->beamActive(), false);
    }
    // Flag / bomb factories: Pickup state (beam on, ShouldDisplayHighlightFx inherited from TnWeaponPickupFactory)
    // in their mode; Disabled (hidden, no collision) otherwise [CONF RE MILESTONE04 pickup/objective presentation].
    for (const ObjectiveObject& o : mapState_.objectives())
        if (o.cls == "TnGameObjectivePickupFactoryFlag" || o.cls == "TnGameObjectivePickupFactoryBomb")
            r.setMapEffectState(o.actor + "|highlight", o.visible, !o.visible);
    // [integration] Destructible presentation: Rendering draws the intact / Chunk02 stump mesh from the
    // HmDestructionState Gameplay simulates (0 intact, 1 destroyed, 2 settled). WFC_DESTRUCTSTATE (Rendering's
    // diagnostic) forces the state instead.
    static const bool forcedDestruct = std::getenv("WFC_DESTRUCTSTATE") != nullptr;
    if (!forcedDestruct)
        for (const Destructible* d : destructibles_) r.setDestructibleState(d->name(), d->state());
}

void World::draw(render::IRenderer& r) const {
    syncMapPresentation(r);
    if (mapMesh_ != render::kInvalidMesh) {
        r.drawMesh(mapMesh_, core::Mat4::identity(), mapColor_);
    } else {
        r.drawGroundGrid(60.0f, 2.0f, core::Vec3{0.30f, 0.33f, 0.38f});
        for (const auto& b : blocks_) r.drawBox(b.center, b.size, b.color);
    }
    for (const auto& a : actors_) {
        if (!a->alive()) continue;
        // [integration M08] Each participant pawn is its own character instance for TnCharacterApplier (draw owner 100 +
        // match player): its team's EnergonColor; no customization paint (participants carry none: material defaults).
        // The local pawn stays owner 0 (colours from the selection at spawn).
        int owner = 0;
        for (const MatchOpponent* o : opponents_)
            if (o == a.get()) {
                owner = 100 + o->matchPlayer();
                render::CharacterColors cc;
                const int p = o->matchPlayer();
                const int team = (p >= 0 && (size_t)p < match_.players().size()) ? match_.players()[(size_t)p].team : -1;
                if (teamEnergon(team == 255 ? -1 : team, cc.energon)) cc.energon[3] = 1.0f;
                r.setDrawOwner(owner);
                r.setCharacterColors(cc);
            }
        a->draw(r);
        if (owner) r.setDrawOwner(0);
    }
    if (!localPlayerDead()) { sysprof::Scope sp(sysprof::DrawPlayer); player_.draw(r); }
    // Projectiles: the authored FlightEffect is the body (a renderer particle system, projectileFxStart); the thrown grenades
    // also draw their class-default static mesh. The box marker remains only when nothing authored can be shown (renderer
    // without the particle API, or a template missing from this map's FX data) [fallback, not original].
    for (const Projectile& p : projectiles_) {
        const ProjectileVisual* v = p.visual >= 0 ? &projVisuals_[(size_t)p.visual] : nullptr;
        if (v && v->body != render::kInvalidMesh) {
            const float yaw = std::atan2(-p.vel.x, -p.vel.z);
            r.drawMesh(v->body, core::Mat4::translate(p.pos) * core::Mat4::rotateY(yaw + core::config::kMeshYawOffset), core::Vec3{1, 1, 1});
        } else if (p.fxHandle < 0) {
            r.drawBox(p.pos, core::Vec3{0.25f, 0.25f, 0.25f}, core::Vec3{1.0f, 0.6f, 0.2f});
        }
    }

    // Ion Blaster mesh held at the weapon socket (robot form only).
    {
        sysprof::Scope sp(sysprof::DrawWeapon);
        if (localPlayerDead()) { /* no pawn: no weapon / pawn effects (PendingMatch, dead) [Gameplay 21a] */ }
        else if (weaponAnim_.valid() && player_.pawn().hasWeapon())
            r.drawDynamicMesh(weaponAnim_.pose(), player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
        else if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
            r.drawMesh(weaponMesh_, player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
    }
    // Weapon + vehicle boost effects last (translucent/additive over the opaque scene).
    { sysprof::Scope sp(sysprof::DrawFx); fx_.draw(r); }
    if (!localPlayerDead()) { sysprof::Scope sp(sysprof::DrawVfx); vehicleFx_.draw(r); }
    if (barrier_.alive && !barrierMesh_.positions.empty()) r.drawDynamicMesh(barrierMesh_, barrier_.world, core::Vec3{1, 1, 1});   // TnBarrierSpawnable mesh
    if (sentry_.alive && !sentryMesh_.positions.empty())
        r.drawDynamicMesh(sentryMesh_, core::Mat4::translate(sentry_.pos) * core::Mat4::rotateY(sentry_.yaw + core::config::kMeshYawOffset), core::Vec3{1, 1, 1});
    sysprof::cueInst = cues_.liveInstances(); sysprof::cuePending = cues_.pendingEvents();
    sysprof::frame(fx_.liveParticles(), fx_.liveMeshes());

    // HUD crosshair state (presentation only; drawn by the renderer from the original HUD movie).
    // [integration/milestone-03] The values come from Gameplay's TnHUD observers (PlayerController
    // hudAimState / NotifyWeaponSpreadChanged): raw effective spread = bloom x airborne multiplier x
    // fine aim 0.5, re-sent only when it moves by > 0.002 [CONF RE d50e2a9]. No scope / replacement
    // reticle for the Ion Blaster (AssetTools 7a69756 fineaim_hud.json).
    render::IRenderer::ReticleState reticle;
    const HudAimState hud = player_.controller().hudAimState();
    reticle.visible = hud.crosshairVisible && !localPlayerDead();   // [integration M06] none while dead / pre-spawn
    reticle.weaponSpread = player_.controller().hudSpread();
    reticle.targetType = hud.targetType;
    r.setReticle(reticle);

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

render::TextureHandle World::resolveTexture(const std::string& uri) {
    if (uri.empty() || !renderer_) return render::kInvalidTexture;
    auto it = texCache_.find(uri);
    if (it != texCache_.end()) return it->second;
    render::ImageData img;
    render::TextureHandle th = render::kInvalidTexture;
    if (platform::decodeImage(uri, img)) { th = renderer_->uploadTexture(img); ++texLoaded_; }
    else ++texFailed_;
    texCache_[uri] = th;
    core::loadYield("World: texture");   // [integration M07] the loading screen presents between texture uploads
    return th;
}

void World::resolveModelTextures(assets::SkinnedModel& m) {
    for (render::Material& M : m.mats) {
        M.tex = resolveTexture(M.baseColorUri);
        M.emissiveTexHandle = resolveTexture(M.emissiveUri);
    }
}

const World::ChassisAssets* World::chassisAssets(const std::string& id) {
    auto it = chassisCache_.find(id);
    if (it != chassisCache_.end()) return it->second.get();
    auto a = std::make_unique<ChassisAssets>();
    const std::string root = assetRoot();
    const std::string ext = root + "/../";
    if (!loadChassisDef(root, id, a->def)) {
        a->error = a->def.loadError;
    } else if (!assets::loadSkinnedGlb(ext + a->def.robotGlb, a->robot) || !a->robot.valid()) {
        a->error = "robot.glb failed to load for " + id;
    } else if (!assets::loadSkinnedGlb(ext + a->def.vehicleGlb, a->vehicle) || !a->vehicle.valid()) {
        a->error = "vehicle.glb failed to load for " + id;
    } else {
        resolveModelTextures(a->robot);
        resolveModelTextures(a->vehicle);
        if (!a->def.vehicle.hullFromPhysics) {
            // Hull from the vehicle mesh bind-pose bounds (glTF x = UE forward, z = UE right) [PROV until the per-chassis
            // physics assets are exported]; the Truck keeps its VH_Optimus_PHYSSYS box.
            VehicleParams& V = a->def.vehicle;
            const auto& lo = a->vehicle.boundsMin; const auto& hi = a->vehicle.boundsMax;
            V.hullFront = hi.x; V.hullBack = -lo.x; V.hullHalfWidth = std::max(std::fabs(lo.z), std::fabs(hi.z));
            V.hullBottom = lo.y; V.hullTop = hi.y; V.hullFromMesh = true;
            LOG_INFO("chassis %s hull from mesh bounds: front %.2f back %.2f half-width %.2f bottom %.2f top %.2f", id.c_str(),
                     V.hullFront, V.hullBack, V.hullHalfWidth, V.hullBottom, V.hullTop);
        }
        if (!a->def.armGltf.empty() && assets::loadSkinnedGlb(ext + a->def.armGltf, a->arm)) {
            if (!a->def.armAnimGltf.empty()) assets::loadAnimationsByName(ext + a->def.armAnimGltf, a->arm);
            resolveModelTextures(a->arm);
            a->hasArm = true;
        }
        a->ok = true;
    }
    if (a->ok) LOG_INFO("chassis %s (%s): robot %zu clips, vehicle %zu clips, arm %s", id.c_str(), a->def.iconic.c_str(),
                        a->robot.clips.size(), a->vehicle.clips.size(), a->hasArm ? "yes" : "no");
    else LOG_ERROR("chassis %s UNAVAILABLE: %s", id.c_str(), a->error.c_str());
    ChassisAssets* raw = a.get();
    // Rendering M53: compile the programs / upload the textures of every form's materials now, so the first robot -> vehicle
    // transform does not pay for them in one frame (measured 65-166 ms hitch). Once per chassis (cached; bots share it).
    if (renderer_ && a->ok) {
        for (const assets::SkinnedModel* m : {&a->robot, &a->vehicle, a->hasArm ? &a->arm : nullptr}) {
            if (!m || !m->valid()) continue;
            render::MeshData md; md.subs = m->subs; md.mats = m->mats;
            fxPrewarm(*renderer_, md, 0);
        }
    }
    chassisCache_[id] = std::move(a);
    return raw;
}

bool World::applyChassisToPawn(Character& pc, const std::string& id) {
    const ChassisAssets* a = chassisAssets(id);
    if (!a || !a->ok) return false;
    pc.setChassis(&a->def);
    pc.setFormModels(&a->robot, &a->vehicle);
    pc.setArmModel(a->hasArm ? &a->arm : nullptr);
    const SocketDef& wp = a->def.weaponPrimary;
    pc.setWeaponSocket(a->robot.nodeByName(wp.bone), wp.local);
    const SocketDef& ws = a->def.weaponSecondary;
    if (ws.valid) pc.setArmSocket(a->robot.nodeByName(ws.bone), ws.local);
    else pc.setArmSocket(-1, core::Mat4::identity());
    return true;
}

bool World::applyChassisToLocalPawn(const std::string& id) {
    if (!applyChassisToPawn(player_.pawn(), id)) return false;
    // [integration M07] Systems' per-character audio (footsteps / body / vehicle sets) follows the spawned body; the
    // profiles are keyed by the roster chassis id (Systems e37c489 handoff: "Gameplay sets the real chassis").
    if (id != localChassis_) setPlayerCharacterAudio(id);
    localChassis_ = id;
    applyLoadout(nullptr);   // iconic preset WeaponTypes / VehicleWeapons
    return true;
}

std::vector<std::string> World::applyCharacterTo(Character& pc, const CharacterSelection* sel, MatchPlayer* mp) {
    const ChassisDef& d = pc.chassis();
    // ApplySpecialty: custom -> the slot's specialty; iconic -> the preset's CharacterData.Specialty.
    std::string spec = (sel && sel->type == 0) ? specialtyName(sel->specialty) : d.iconicSpecialty;
    if (const SpecialtyDef* sd = specialtyDef(spec)) pc.setSpecialty(sd->id, sd->speedMultiplier, sd->segments, sd->overshield);
    else pc.clearSpecialty();
    std::vector<std::string> refused;
    const bool custom = sel && sel->type == 0 && !sel->weapons.empty();
    std::vector<Weapon> robot;
    for (const std::string& n : custom ? sel->weapons : d.iconicWeapons) {
        const WeaponDef* wd = findWeaponDef(n);
        bool allowed = !custom || std::find(d.allowedOnFoot.begin(), d.allowedOnFoot.end(), n) != d.allowedOnFoot.end() ||
                       classPresetGrenadeAllowed(sel, wd);
        if (!wd || !allowed) { refused.push_back(n); continue; }
        robot.push_back(Weapon::fromDef(*wd));
    }
    const std::vector<std::string>& vehSel = (sel && sel->type == 0 && !sel->vehicleWeapons.empty()) ? sel->vehicleWeapons : d.iconicVehicleWeapons;
    pc.setLoadout(robot, vehSel);
    {   // [Systems M08d] the loadout's weapon classes (robot + vehicle weapons): their cues for this level, and the vehicle
        // weapon's class for vehicle-form fire sounds.
        std::vector<std::string> cls;
        for (const Weapon& rw : robot) if (rw.def) cls.push_back("TransContent.TnWeapon" + std::string(rw.def->id));
        std::string vehCls;
        for (const std::string& vn : vehSel) if (const WeaponDef* vd = findWeaponDef(vn)) {
            cls.push_back("TransContent.TnWeapon" + std::string(vd->id));
            if (vehCls.empty()) vehCls = cls.back();
        }
        preloadWeaponAudio(cls);
        setPlayerVehicleWeaponAudio(vehCls);
    }
    pc.setAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities);
    pc.respawnReset();
    if (mp) { mp->specialty = spec; mp->healthMax = pc.health().max; }
    return refused;
}

bool World::teamEnergon(int team, float out[3]) const {
    // The values are class defaults (identical in every chassis export); read once from the first chassis this world
    // loaded. Keys as AssetTools vs_roster_export writes them.
    static bool loaded = false, ok[3] = {false, false, false};
    static float tbl[3][3] = {};
    if (!loaded && !localChassis_.empty()) {
        loaded = true;
        std::string txt;
        assets::Json j;
        if (readTextFile(assetRoot() + "/Characters/" + localChassis_ + "/character.json", txt) && assets::Json::parse(txt, j)) {
            const assets::Json& tc = j["team_colour"]["team_energon_colors (class defaults, CONFIRMED)"];
            const char* keys[3] = {"Autobots (TnFactionTeamAutobots)", "Decepticons (TnFactionTeamDecepticons)", "neutral (TnTeamInfo)"};
            for (int i = 0; i < 3; ++i)
                if (tc.has(keys[i])) { ok[i] = true; tbl[i][0] = tc[keys[i]]["R"].asFloat(); tbl[i][1] = tc[keys[i]]["G"].asFloat(); tbl[i][2] = tc[keys[i]]["B"].asFloat(); }
        }
        if (!ok[0] || !ok[1] || !ok[2]) LOG_ERROR("team energon colours missing in the %s export: material defaults kept", localChassis_.c_str());
    }
    const int i = team == 0 ? 0 : team == 1 ? 1 : 2;
    if (!ok[i]) return false;
    for (int k = 0; k < 3; ++k) out[k] = tbl[i][k];
    return true;
}

std::string World::mapDir() const { return assetRoot() + "/Maps/" + mapName_ + "/"; }

std::string World::canonicalMapName(const std::string& m) {
    std::string base = m;
    for (const char* suf : {"_BASE_m", "_Base_m", "_base_m", "_m"}) {
        size_t n = std::char_traits<char>::length(suf);
        if (base.size() > n && base.compare(base.size() - n, n, suf) == 0) { base.resize(base.size() - n); break; }
    }
    static const char* kMaps[] = {"MP_IAC_Streets", "MP_UND_Gorge", "MP_ESC_BrokenHope", "MP_ESC_Remnant", "MP_IAC_Berth",
                                  "MP_IAC_Rust", "MP_IAC_Seed", "MP_KON_Molten", "MP_ORB_Debris", "MP_UND_Complex"};
    auto lower = [](std::string x) { for (char& c : x) c = (char)std::tolower((unsigned char)c); return x; };
    for (const char* k : kMaps) if (lower(base) == lower(k)) return k;
    return base;
}

void World::loadHazards() {
    hazards_.clear();
    const char* mr = std::getenv("WFC_MANIFESTS");
    std::string path = std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/maps/" + mapName_ + "/hazard_volumes.json";
    std::string txt;
    assets::Json j;
    if (!readTextFile(path, txt) || !assets::Json::parse(txt, j)) { LOG_INFO("map %s: no hazard volumes (%s)", mapName_.c_str(), path.c_str()); return; }
    const assets::Json& list = j["hazard_volumes"];
    for (size_t i = 0; i < list.size(); ++i) {
        const assets::Json& v = list[i];
        HazardVolume h;
        h.actor = v["actor"].asString();
        h.damageType = v["DamageType"].asString();
        h.damagePerSec = v["DamagePerSec"].asFloat(0.0f);
        h.painInterval = v["PainInterval"].isNumber() ? v["PainInterval"].asFloat() : 1.0f;
        h.entryPain = v["bEntryPain"].type == assets::Json::Type::Bool ? v["bEntryPain"].asBool() : true;
        const assets::Json& polys = v["polygons_gltf"];
        core::Vec3 c{0, 0, 0}; int nv = 0;
        for (size_t p = 0; p < polys.size(); ++p)
            for (size_t k = 0; k < polys[p].size(); ++k) { c = c + core::Vec3{polys[p][k][0].asFloat(), polys[p][k][1].asFloat(), polys[p][k][2].asFloat()}; ++nv; }
        if (nv == 0 || h.damagePerSec <= 0.0f) continue;
        c = c * (1.0f / nv);
        h.centroid = c;
        for (size_t p = 0; p < polys.size(); ++p) {
            if (polys[p].size() < 3) continue;
            auto P = [&](size_t k) { return core::Vec3{polys[p][k][0].asFloat(), polys[p][k][1].asFloat(), polys[p][k][2].asFloat()}; };
            core::Vec3 n = core::normalize(core::cross(P(1) - P(0), P(2) - P(0)));
            float w = core::dot(n, P(0));
            if (core::dot(n, c) > w) { n = n * -1.0f; w = -w; }   // outward: the centroid is inside
            h.planes.push_back(HazardVolume::Plane{n, w});
        }
        hazards_.push_back(std::move(h));
    }
    LOG_INFO("map %s: %zu hazard volumes", mapName_.c_str(), hazards_.size());
}

int World::hazardAt(const core::Vec3& p) const {
    for (size_t i = 0; i < hazards_.size(); ++i) {
        bool in = !hazards_[i].planes.empty();
        for (const HazardVolume::Plane& pl : hazards_[i].planes) if (core::dot(pl.n, p) > pl.w) { in = false; break; }
        if (in) return (int)i;
    }
    return -1;
}

void World::tickHazards(float dt) {
    if (hazards_.empty()) return;
    Character& pc = player_.pawn();
    if (localPlayerDead()) { localHazard_ = -1; return; }
    int h = hazardAt(pc.actorLocation());
    if (h < 0) { localHazard_ = -1; return; }
    const HazardVolume& v = hazards_[(size_t)h];
    bool entered = h != localHazard_;
    localHazard_ = h;
    if (entered) localPainTimer_ = v.entryPain ? 0.0f : v.painInterval;
    else localPainTimer_ -= dt;
    if (localPainTimer_ > 0.0f) return;
    localPainTimer_ += v.painInterval;
    float dmg = v.damagePerSec * v.painInterval;
    LOG_INFO("hazard %s: %.0f damage (%s)", v.actor.c_str(), dmg, v.damageType.c_str());
    if (matchActive_) applyMatchDamage(localPlayer_, -1, dmg, true, v.damageType);   // environment: no instigator
    else if (pc.health().applyDamage(dmg) > 0.0f && pc.health().isDead()) respawnPlayer();
}

void World::spawnProjectile(const core::Vec3& pos, const core::Vec3& vel, const Weapon& w, int instigator) {
    float range = w.rangeM > 0.0f ? w.rangeM : 300.0f;
    projectiles_.push_back({pos, vel, w.projDamage, w.projRadiusM, std::max(1.0f, range / std::max(1.0f, core::length(vel))) + 1.0f,
                            w.damageType ? w.damageType : "", instigator});
    {   // [Systems M08d] FlightSound from spawn (HmProjectile.ClientSpawnFlightEffect)
        Projectile& np = projectiles_.back();
        np.weaponClass = w.def ? "TransContent.TnWeapon" + std::string(w.def->id) : std::string();
        np.audioKey = ++projAudioKey_;
        onProjectileSpawned(np.audioKey, np.weaponClass, pos);
    }
    if (w.projHoming) {
        Projectile& p = projectiles_.back();
        p.homingForce = w.homingForce; p.closingDist = w.closingDistM; p.closingForce = w.closingForce;
        p.closingTime = w.closingTime; p.maxSpeed = w.projMaxSpeed; p.lockRobots = w.lockRobots;
    }
    projectiles_.back().visual = projectileVisualFor(w.def ? w.def->id : nullptr);
    projectileFxStart(projectiles_.back());
}

void World::radiusDamage(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type) {
    // Actor.HurtRadius -> TakeRadiusDamage: Dist = max(|Location - origin| - collision radius, 0), scale = 1 - Dist / DamageRadius
    // (bFullDamage false) [HIGH stock UE3 Actor.TakeRadiusDamage]; teammates are filtered by applyMatchDamage (projectile
    // damage types are not TnDamageTypeAOE).
    auto falloff = [&](const core::Vec3& p, float colR = 0.0f) {
        float d = std::max(core::length(p - at) - colR, 0.0f);
        return d >= radius ? 0.0f : 1.0f - d / std::max(radius, 1e-3f);
    };
    auto colR = [](const Character& c) { return c.cylinderRadius(c.moveForm()); };
    if (matchActive_ && !localDead_) {
        float k = falloff(player_.pawn().actorLocation(), colR(player_.pawn()));
        if (k > 0.0f) applyMatchDamage(localPlayer_, instigator, damage * k, false, type);
    }
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned()) continue;
        float k = falloff(o->pawn().actorLocation(), colR(o->pawn()));
        if (k > 0.0f) applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type);
    }
    for (Destructible* d : destructibles_)
        if (d->state() == 0) { core::Vec3 c = (d->boxMin() + d->boxMax()) * 0.5f; float k = falloff(c); if (k > 0.0f) d->applyDamage(*this, damage * k); }
    if (roller_.alive) {
        float dd = std::max(0.0f, core::length(roller_.pos - at) - 1.21f);
        if (dd < radius) damageRollerMine(damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator);
    }
    if (sentry_.alive) {
        float dd = std::max(0.0f, core::length(sentry_.pos + core::Vec3{0, 2.0f, 0} - at) - 2.0f);
        if (dd < radius) damageSentry(damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator, type);
    }
    if (beacon_.alive && core::length(beacon_.pos - at) < radius)
        damageAmmoBeacon(damage * (1.0f - core::length(beacon_.pos - at) / std::max(radius, 1e-3f)), instigator);
    if (barrier_.alive) {
        // Distance to the wall box (clamped point), not its centre.
        core::Vec3 l = core::transformPoint(barrier_.boxInv, at);
        core::Vec3 q{std::max(-barrier_.half.x, std::min(l.x, barrier_.half.x)), std::max(-barrier_.half.y, std::min(l.y, barrier_.half.y)),
                     std::max(-barrier_.half.z, std::min(l.z, barrier_.half.z))};
        float dd = core::length(l - q);
        if (dd < radius) damageBarrier(damage * (1.0f - dd / std::max(radius, 1e-3f)), type);
    }
}

void World::tickProjectiles(float dt) {
    const CollisionWorld* lineWorld = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (size_t i = 0; i < projectiles_.size();) {
        Projectile& p = projectiles_[i];
        // TnProjectileHoming: Homing (accel HomingForce toward the target, no lead in MP data) -> within ClosingDistance
        // Closing (ClosingForce; explodes after ClosingTime regardless). Homing stops when the target dies or is in robot
        // form while CanLockOnToRobots is false. Speed capped at MaxSpeed [CONF script + authored].
        bool closingExpired = false;
        if (p.target >= 0) {
            const Character* tp = matchPawn(p.target);
            if (!tp || (!p.lockRobots && tp->moveForm() == Form::Robot)) p.target = -1;
            else {
                core::Vec3 to = tp->actorLocation() - p.pos;
                float dist = core::length(to);
                if (p.closingRemain < 0.0f && dist <= p.closingDist) p.closingRemain = p.closingTime;
                float force = p.closingRemain >= 0.0f ? p.closingForce : p.homingForce;
                if (dist > 1e-4f) p.vel = p.vel + to * (force * dt / dist);
                float sp = core::length(p.vel);
                if (p.maxSpeed > 0.0f && sp > p.maxSpeed) p.vel = p.vel * (p.maxSpeed / sp);
            }
        }
        if (p.closingRemain >= 0.0f) { p.closingRemain -= dt; if (p.closingRemain < 0.0f) closingExpired = true; }
        if (p.grenade) {
            // PHYS_Falling at GravityScale; HitWall / Bump -> HitThing: explode on a pawn only with ExplodeWhenHittingPawn; else
            // the first impact starts the fuse (LifeSpan = RandomInRange(FuseTime)), v = BounceDampening x reflect(v), at rest
            // (PHYS_None) when |v|^2 < LowSpeedThreshold 500 UU^2/s^2 [CONF script + authored].
            if (!p.resting) {
                p.vel.y -= core::config::kGravity * p.gravityScale * dt;
                core::Vec3 nx = p.pos + p.vel * dt;
                float tw;
                bool hitPawn = false;
                core::Vec3 seg = nx - p.pos; float sl = core::length(seg);
                if (sl > 1e-5f)
                    for (MatchOpponent* o : opponents_) {
                        float th;
                        if (o->matchPlayer() != p.instigator && o->rayHit(p.pos, seg * (1.0f / sl), sl, th)) { hitPawn = true; break; }
                    }
                auto impact = [&](const core::Vec3& n) {
                    onProjectileHitWall(p.weaponClass, p.pos, p.life > 1e8f);   // [Systems M08f] FuseSound (first) + BounceSound
                    if (p.life > 1e8f) p.life = p.fuseMin + (p.fuseMax - p.fuseMin) * (float)(std::rand() % 1000) / 999.0f;
                    p.vel = (p.vel - n * (2.0f * core::dot(p.vel, n))) * p.bounce;
                    if (core::dot(p.vel, p.vel) < 500.0f * 1e-4f) { p.resting = true; p.vel = {0, 0, 0}; }
                };
                if (hitPawn && p.explodeOnPawn) {
                    radiusDamage(p.pos, p.damage, p.radius, p.instigator, p.damageType);
                    onProjectileExploded(p.audioKey, p.weaponClass, p.pos);   // [Systems M08d] Explode
                    projectileFxEnd(p, p.pos, sl > 1e-5f ? seg * (-1.0f / sl) : core::Vec3{0, 1, 0}, true);
                    projectiles_.erase(projectiles_.begin() + (long)i); continue;
                }
                if (hitPawn) {
                    core::Vec3 n = core::normalize(core::Vec3{-seg.x, 0.0f, -seg.z});
                    impact(n);
                } else if (lineWorld && lineWorld->segmentHit(p.pos, nx, tw)) {
                    // Surface normal: floor when moving down onto it, else the reversed horizontal travel [PROV normal estimate].
                    core::Vec3 n = p.vel.y < 0.0f && std::fabs(p.vel.y) > std::hypot(p.vel.x, p.vel.z) * 0.5f ? core::Vec3{0, 1, 0}
                                                                                                         : core::normalize(core::Vec3{-p.vel.x, 0.0f, -p.vel.z});
                    p.pos = p.pos + seg * std::max(0.0f, tw - 1e-3f);
                    impact(n);
                } else p.pos = nx;
            }
            p.life -= dt;
            if (p.life <= 0.0f) {
                radiusDamage(p.pos + core::Vec3{0, 0.1f, 0}, p.damage, p.radius, p.instigator, p.damageType);
                onProjectileExploded(p.audioKey, p.weaponClass, p.pos);   // [Systems M08d] Explode
                projectileFxEnd(p, p.pos, core::Vec3{0, 1, 0}, true);   // fuse: resting on the floor [PROV normal]
                projectiles_.erase(projectiles_.begin() + (long)i); continue;
            }
            onProjectileMoved(p.audioKey, p.pos);
            projectileFxMove(p);
            ++i;
            continue;
        }
        core::Vec3 next = p.pos + p.vel * dt;
        float best = 1.0f; bool hit = false;
        float t;
        bool worldHit = false;
        core::Vec3 hitN{0, 0, 0};
        if (lineWorld && lineWorld->segmentHit(p.pos, next, t, hitN)) { best = t; hit = true; worldHit = true; }
        core::Vec3 d = next - p.pos; float len = core::length(d);
        const float worldBest = best;
        bool barrierHit = false;
        if (len > 1e-5f) { float tb; if (barrierRayHit(p.pos, d * (1.0f / len), len, tb) && tb / len < best) { best = tb / len; hit = true; barrierHit = true; worldHit = false; } }
        if (len > 1e-5f) {
            core::Vec3 dir = d * (1.0f / len);
            for (MatchOpponent* o : opponents_) {
                if (o->matchPlayer() == p.instigator) continue;
                float th;
                if (o->rayHit(p.pos, dir, len, th) && th / len < best) { best = th / len; hit = true; worldHit = false; }
            }
            if (matchActive_ && !localDead_ && p.instigator != localPlayer_) {
                const Character& pc = player_.pawn();
                core::Vec3 c = pc.actorLocation(); float r = pc.cylinderRadius(pc.moveForm()), hh = pc.cylinderHalfHeight(pc.moveForm());
                for (int k = 1; k <= 8; ++k) {   // sampled segment vs the local cylinder
                    core::Vec3 q = p.pos + d * (k / 8.0f);
                    if (std::hypot(q.x - c.x, q.z - c.z) <= r && std::fabs(q.y - c.y) <= hh && k / 8.0f < best) { best = k / 8.0f; hit = true; worldHit = false; break; }
                }
            }
        }
        p.life -= dt;
        if (hit || p.life <= 0.0f || closingExpired) {
            core::Vec3 at = p.pos + d * best;
            if (hit || closingExpired) radiusDamage(at, p.damage, p.radius, p.instigator, p.damageType);
            if (barrierHit && barrier_.alive && barrier_.health > 0.0f) {}   // radiusDamage reached the barrier
            if (worldHit) onProjectileHitWall(p.weaponClass, at, false);                         // [Systems M08f] HitWall: BounceSound
            if (hit || closingExpired) onProjectileExploded(p.audioKey, p.weaponClass, at);   // [Systems M08d] Explode
            else onProjectileRemoved(p.audioKey);                                               // LifeSpan end: destroyed
            // HitNormal: the world surface's normal; a pawn / barrier hit (or no normal) faces back along the flight.
            const bool surfaceHit = hit && best == worldBest && core::dot(hitN, hitN) > 0.5f;
            const core::Vec3 n = surfaceHit ? hitN : (len > 1e-5f ? d * (-1.0f / len) : core::Vec3{0, 1, 0});
            projectileFxEnd(p, at, n, hit || closingExpired);   // LifeSpan expiry: Destroyed, no explosion
            projectiles_.erase(projectiles_.begin() + (long)i);
            continue;
        }
        p.pos = next;
        onProjectileMoved(p.audioKey, p.pos);
        projectileFxMove(p);
        ++i;
    }
}

std::string World::triggerLocalKillstreak() {
    if (!matchActive_ || localDead_ || localPlayer_ < 0) return "";
    MatchPlayer& mp = match_.playerMutable(localPlayer_);
    if (mp.acquiredKillstreaks.empty()) return "";
    const std::string id = mp.acquiredKillstreaks.back();
    Character& pc = player_.pawn();
    // RequiresRobotForm streaks (abilities spawned / used on foot) in vehicle form: StartTransform(robot) + defer.
    const bool needsRobot = id == "PokeStreak" || id == "MinePooperStreak" || id == "SpawnRocketTurretStreak" || id == "GuidedMissileStreak";   // RequiresRobotForm
    if (needsRobot && pc.moveForm() != Form::Robot) {
        if (!pc.isTransforming()) player_.controller().tryBeginTransform();
        deferredKillstreak_ = true;
        return "";
    }
    mp.acquiredKillstreaks.pop_back();
    const int team = mp.team;
    auto teamPawns = [&](auto fn) {   // TnTeamHandler.GetTeamMembers: living pawns of the owner's team (FFA: the owner)
        if (!localDead_) fn(pc);
        if (match_.settings().teamGame)
            for (MatchOpponent* o : opponents_) if (o->spawned() && o->team() == team) fn(o->pawn());
    };
    if (id == "OverShieldStreak") {
        teamPawns([](Character& p) { p.health().heal(Health::HealType::AddOverShield, 1.0f); });   // HealDamage(TnHealTypeOverShieldPickup)
    } else if (id == "RefillAmmoStreak") {
        // RefillAmmo: FillReserveAmmo for WT_Primary / Secondary / Grenades / Vehicle of every team member; the owner gets
        // TnBuffLockAmmoClip 10 s.
        auto fill = [](Character& p) {
            for (Weapon& w : p.inventoryMutable()) w.reserve = w.reserveMax;
            if (Weapon* vw = p.vehicleWeapon()) vw->reserve = vw->reserveMax;
        };
        teamPawns(fill);
        pc.ammoLockRemain_ = 10.0f; lockedClip_ = pc.weapon().ammo;
    } else if (id == "HealthRegenStreak") {
        pc.regenBuffRemain_ = 30.0f;                       // TnBuffHealthRegenKillStreak FloatModifier 2, BuffTime 30
    } else if (id == "FastAbilityCooldownStreak") {
        pc.fastCooldownRemain_ = 30.0f;                    // TnBuffFastAbilityCooldown CooldownMultiplier 5, BuffTime 30
    } else if (id == "GuidedMissileStreak") {
        startGuidedMissile();                              // Omega Missile: the same TnGuidedMissile (RequiresRobotForm)
    } else if (id == "PokeStreak") {
        if (const WeaponDef* d = findWeaponDef("Poke")) pc.grantTempWeapon(*d, 1, 20.0f);   // TnAbilityPoke -> TnWeaponPoke
    } else if (id == "SpawnRocketTurretStreak") {
        if (const WeaponDef* d = findWeaponDef("HeavyRocketTurret")) pc.grantTempWeapon(*d, 2, 1e9f);   // CreateInventory
    } else if (id == "MinePooperStreak") {
        pc.minePooperRemain_ = 15.0f; pc.minePooperTimer_ = 0.0f;   // AddSelfBuff(TnBuffMinePooper)
    } else if (id == "OrbitalReconStreak") {
        teamPawns([](Character& p) { p.seeEnemiesRemain_ = 30.0f; });   // ABT_Team TnBuffSeeEnemyObjectiveMarkers
    } else if (id == "ImprovedOrbitalReconStreak") {
        // ABT_OtherTeam TnBuffHardLocked (BuffTime 10) + each opposing member TakeDamage(1, TnDamageTypeFlashBang).
        for (MatchOpponent* o : opponents_)
            if (o->spawned() && !match_.sameTeam(o->matchPlayer(), localPlayer_)) {
                o->pawn().hardLockedRemain_ = 10.0f; o->pawn().hardLockedByTeam_ = team;
                applyMatchDamage(o->matchPlayer(), localPlayer_, 1.0f, false, "TransGame.TnDamageTypeFlashBang");
            }
    } else if (id == "FriendlyKillHealthBonusStreak") {
        teamPawns([](Character& p) { p.refillOnKillRemain_ = 60.0f; });   // ABT_Team TnBuffRefillHealthOnKill
    } else if (id == "TeamAbilityJammerStreak") {
        for (MatchOpponent* o : opponents_)                                // ABT_OtherTeam TnBuffAbilityJammedKillstreak 30 s
            if (o->spawned() && !match_.sameTeam(o->matchPlayer(), localPlayer_)) o->pawn().applyJammed(30.0f);
    } else {
        LOG_WARN("killstreak %s triggered: effect not implemented in the rebuild [PARTIAL]", id.c_str());
    }
    LOG_INFO("killstreak %s triggered", id.c_str());
    return id;
}

// Ability effects for the local pawn (TnAbility*.ServerTriggerAbility) [CONF script + authored CDOs].
void World::tickAbilityEffects(float dt) {
    Character& pc = player_.pawn();
    auto tickBuff = [dt](Character& p) {
        p.cloakRemain_ = std::max(0.0f, p.cloakRemain_ - dt);
        if (p.warcryRemain_ > 0.0f) { p.warcryRemain_ = std::max(0.0f, p.warcryRemain_ - dt); if (p.warcryRemain_ == 0.0f) { p.warcryDamageMul_ = 1.0f; p.warcryTakenMul_ = 1.0f; } }
    };
    tickBuff(pc);
    for (MatchOpponent* o : opponents_) tickBuff(o->pawn());
    const std::string fx = pc.pendingAbilityEffect_;
    pc.pendingAbilityEffect_.clear();
    const int team = matchActive_ && localPlayer_ >= 0 ? match_.players()[(size_t)localPlayer_].team : 255;
    if (fx == "Warcry") {
        // GetFriendliesInRange(AoeRange 3000 UU): same-team pawns (FFA: the owner only); BuffLevel = Clamp(count - 1, 0, 1)
        // (+1 with TnSkillImprovedWarcry - skills not applied); BuffsToApply x level; BuffTime[0] = 15 s; removes HardLocked.
        std::vector<Character*> friends{&pc};
        if (matchActive_ && match_.settings().teamGame)
            for (MatchOpponent* o : opponents_)
                if (o->spawned() && o->team() == team && core::length(o->pawn().actorLocation() - pc.actorLocation()) <= 30.0f) friends.push_back(&o->pawn());
        int level = std::max(0, std::min((int)friends.size() - 1, 1));
        const float dmg[3] = {1.1f, 1.2f, 1.3f}, taken[3] = {0.5f, 0.4f, 0.3f};
        for (Character* p : friends) { p->warcryRemain_ = 15.0f; p->warcryDamageMul_ = dmg[level]; p->warcryTakenMul_ = taken[level]; p->hardLockedRemain_ = 0.0f; }
        LOG_INFO("ability Warcry: %zu friendlies, buff level %d (damage x%.1f, taken x%.1f, 15 s)", friends.size(), level, dmg[level], taken[level]);
    } else if (fx == "Shockwave") {
        pc.shockwaveDelay_ = 0.25f;                       // Delay 0.25 -> Shockwave()
    } else if (fx == "Whirlwind") {
        startLocalMelee(true);                            // MeleeService.StartMeleeAttack(MELEE_Whirlwind)
    } else if (fx == "HardLock" || fx == "MarkTarget") {
        // TnAbilityHardLock: target = the homing-lock pick (picker 4); TnBuffHardLocked level 0, 10 s; fails without a target.
        pc.playAction("Skill_MarkTarget", false);
        const int tgt = pickHomingTarget(true, 300.0f);
        for (MatchOpponent* o : opponents_)
            if (o->matchPlayer() == tgt && o->spawned()) {
                o->pawn().hardLockedRemain_ = 10.0f;
                o->pawn().hardLockedByTeam_ = matchActive_ ? match_.players()[(size_t)localPlayer_].team : 255;
                LOG_INFO("ability HardLock: player %d marked", tgt);
            }
        if (tgt < 0) LOG_INFO("ability HardLock: no target");
    } else if (fx == "AbilityJammer" || fx == "TransformDisruptor") {
        // Projectile from the owner (jammer offset (0, 175, 25)) along the aim: jammer 10000 UU/s radius 500, disruptor 6000 / 250.
        const bool jam = fx == "AbilityJammer";
        pc.playAction("Skill_AbilityJammer", false);
        const PlayerController& ctl = player_.controller();
        const core::Vec3 rt = core::normalize(core::cross(core::forwardFromYawPitch(pc.yaw(), 0.0f), core::Vec3{0, 1, 0}));
        const core::Vec3 from = pc.actorLocation() + (jam ? rt * 1.75f + core::Vec3{0, 0.25f, 0} : core::Vec3{0, 0.25f, 0});
        // Aimed through the crosshair like weapon fire: camera ray trace -> aim point -> from the spawn point toward it.
        const core::Vec3 camDir = core::forwardFromYawPitch(ctl.camYaw(), ctl.camPitch()), camPos = ctl.cameraPos();
        core::Vec3 aimPoint = camPos + camDir * 300.0f; float tt;
        if (collision_.valid() && collision_.segmentHit(camPos, aimPoint, tt)) aimPoint = camPos + camDir * (300.0f * tt);
        const core::Vec3 dir = core::normalize(aimPoint - from);
        buffShots_.push_back({from, dir * (jam ? 100.0f : 60.0f), jam ? 5.0f : 2.5f, 3.0f, jam ? 0 : 1});
    } else if (fx == "RollerSphere") {
        pc.playAction("Skill_AbilityJammer", false);       // OnTriggerAnimParams
        rollerDelay_ = 0.5f;                               // SpawnDelay 0.5
        pc.rollerAlive_ = true;
    } else if (fx == "GuidedMissile") {
        startGuidedMissile();
    } else if (fx == "SpawnSentry") {
        if (sentry_.alive) sentry_.alive = false;         // ActiveSentry.Kill()
        sentryDelay_ = 0.2f;                              // SpawnDelay 0.2 (OnTriggerAnim ADD_Grenade_Throw additive: not played)
        pc.sentryAlive_ = true;
    } else if (fx == "SpawnAmmoCrate") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        beaconDelay_ = 0.5f;                              // SpawnDelay 0.5 -> SpawnInventory
        pc.beaconAlive_ = true;
    } else if (fx == "Barrier") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        barrierDelay_ = 0.5f;                             // SpawnDelay 0.5 -> SpawnBarrier
        pc.barrierAlive_ = true;
    }
    tickBarrier(dt);
    tickAmmoBeacon(dt);
    tickSentry(dt);
    tickGuidedMissile(dt);
    tickRollerMine(dt);
    repairBeam_.time = std::max(0.0f, repairBeam_.time - dt);
    if (repairBeam_.time <= 0.0f) repairBeam_.active = false;
    // [Systems M08d / Gameplay 24c] TnWeaponBeam / TnWeaponRepair sound state from Gameplay's beam: teammate -> heal loop
    // (WP_Fire), enemy -> damage loop (WP_FireSecondary), no pawn -> neither; release -> WP_LoopingTail.
    {
        const bool firing = repairBeam_.active && repairBeam_.time > 0.0f;
        const int target = !firing ? 0 : repairBeam_.healing ? 1 : (repairBeam_.target >= 0 ? 2 : 0);
        if (firing || weaponAudio().beamActive())
            onBeamWeapon("TransContent.TnWeaponRepairRay", firing, target);
    }
    tickBuffShots(dt);
    tickKillstreakItems(dt);
    auto tickTD = [dt](Character& p) { p.transformDisruptRemain_ = std::max(0.0f, p.transformDisruptRemain_ - dt); };
    tickTD(pc);
    for (MatchOpponent* o : opponents_) tickTD(o->pawn());
    // TnBuffDrainSource (Blueprints[0]): each tick every enemy TnPawn within Range 2000 UU with line of sight takes
    // DamagePerSecond 25 x dt; the caster heals HealthPerSecond 35 x dt per target [CONF authored + RE §J]. Heal type
    // AddHealthToAll [PROV].
    if (pc.drainRemain_ > 0.0f) {
        pc.drainRemain_ = localDead_ ? 0.0f : std::max(0.0f, pc.drainRemain_ - dt);
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        int targets = 0;
        if (matchActive_ && !localDead_)
            for (MatchOpponent* o : opponents_) {
                if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                if (core::length(o->pawn().actorLocation() - pc.actorLocation()) > 20.0f) continue;
                float t;
                if (line && line->segmentHit(pc.actorLocation(), o->pawn().actorLocation(), t)) continue;
                ++targets;
                applyMatchDamage(o->matchPlayer(), localPlayer_, 25.0f * dt, false, "TransGame.TnDamageTypeDrain");
            }
        if (targets > 0 && !localDead_) pc.health().heal(Health::HealType::AddHealthToAll, 35.0f * dt * targets);
    }
    tickLocalMelee(dt);
    tickHomingLock(dt);
    if (localDead_ && barrier_.alive) { barrier_.alive = false; barrierDelay_ = -1.0f; }
    grenadeCooldown_ = std::max(0.0f, grenadeCooldown_ - dt);
    if (grenadeTossDelay_ >= 0.0f) {
        grenadeTossDelay_ -= dt;
        Character& gp = player_.pawn();
        const Weapon* gb = grenadeBag(gp);
        if (grenadeTossDelay_ < 0.0f && gb && !localDead_ && gp.moveForm() == Form::Robot) {
            // TnGrenadeThrower.SpawnGrenade at MeleeSocket_RightHand.
            core::Vec3 src = gp.actorLocation() + core::Vec3{0, gp.robotParams().eyeHeight, 0};
            const SocketDef& sd = gp.chassis().rightHand;
            core::Mat4 bm;
            if (sd.valid && gp.boneWorld(sd.bone, bm)) { core::Mat4 w = bm * sd.local; src = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
            const WeaponDef& d = *gb->def;
            // SuggestTossVelocity(target, src, TossStrength): the lower ballistic arc at that speed under world gravity
            // (native; exact solve here, 45 deg when out of reach) [PROV].
            core::Vec3 to = grenadeTarget_ - src;
            float hd = std::hypot(to.x, to.z), dy = to.y, S = d.tossStrength, g = core::config::kGravity;
            float disc = S * S * S * S - g * (g * hd * hd + 2.0f * dy * S * S);
            float ang = disc >= 0.0f && hd > 1e-3f ? std::atan((S * S - std::sqrt(disc)) / (g * hd)) : 0.7853982f;
            core::Vec3 hdir = hd > 1e-3f ? core::Vec3{to.x / hd, 0, to.z / hd} : core::forwardFromYawPitch(gp.yaw(), 0.0f);
            core::Vec3 vel = hdir * (S * std::cos(ang)) + core::Vec3{0, S * std::sin(ang), 0};
            // AdjustTossVelocity: aim pitch (src -> target) <= LowPitchDegrees.Max -> speed lerps toward LowPitchSpeed at .Min.
            const float aimPitch = std::atan2(dy, std::max(hd, 1e-3f)) * 57.29578f;
            if (d.lowPitchMin != d.lowPitchMax && aimPitch <= d.lowPitchMax) {
                float np = core::clampf((aimPitch - d.lowPitchMin) / (d.lowPitchMax - d.lowPitchMin), 0.0f, 1.0f);
                float sp = core::length(vel);
                vel = core::normalize(vel) * (d.lowPitchSpeed + (sp - d.lowPitchSpeed) * np);
            }
            // Grenade.Init: x Lerp(SpeedScaleAtMinPitch, SpeedScaleAtMaxPitch, pct(launch pitch, MinPitch, MaxPitch)).
            const float launchPitch = std::atan2(vel.y, std::hypot(vel.x, vel.z)) * 57.29578f;
            float lp = core::clampf((launchPitch - d.minPitch) / (d.maxPitch - d.minPitch), 0.0f, 1.0f);
            vel = vel * (d.speedScaleMinPitch + (d.speedScaleMaxPitch - d.speedScaleMinPitch) * lp);
            Projectile pr{src, vel, d.projDamage, d.projRadiusM, 1e9f, d.projDamageType ? d.projDamageType : "", localPlayer_};
            pr.grenade = true; pr.explodeOnPawn = d.explodeOnPawn; pr.gravityScale = d.gravityScale; pr.bounce = d.bounce;
            pr.fuseMin = d.fuseMin; pr.fuseMax = d.fuseMax;
            pr.visual = projectileVisualFor(d.id);
            projectiles_.push_back(pr);
            projectileFxStart(projectiles_.back());
            LOG_INFO("grenade %s: |v| %.1f m/s pitch %.1f deg (aim %.1f)", d.id, core::length(vel), launchPitch, aimPitch);
        }
    }
    if (pc.shockwaveDelay_ >= 0.0f) {
        pc.shockwaveDelay_ -= dt;
        if (pc.shockwaveDelay_ < 0.0f && !localDead_) {
            // GetBP(index 0): Damage 65, Radius 2500 UU; HurtRadius(..., bDoFullDamage true) from PositionSocket; the owner
            // is not hurt [HIGH]; Momentum 700000 knock-back not applied [PARTIAL].
            const core::Vec3 at = pc.actorLocation();
            LOG_INFO("ability Shockwave: 65 within 25 m");
            for (MatchOpponent* o : opponents_)
                if (o->spawned() && core::length(o->pawn().actorLocation() - at) <= 25.0f) {
                    core::Vec3 dir = o->pawn().actorLocation() - at;
                    applyMatchDamage(o->matchPlayer(), localPlayer_, 65.0f, false, "TransGame.TnDamageTypeShockwave");
                    // HurtRadius momentum (bDoFullDamage: scale 1) = Momentum 700000 along origin -> victim [CONF].
                    if (core::length(dir) > 1e-4f) applyKnockback(o->matchPlayer(), core::normalize(dir) * 700000.0f, "TransGame.TnDamageTypeShockwave");
                }
            for (Destructible* d : destructibles_)
                if (d->state() == 0 && core::length((d->boxMin() + d->boxMax()) * 0.5f - at) <= 25.0f) d->applyDamage(*this, 65.0f);
        }
    }
}

// ---- Melee [CONF RE TARGETED_PASS3 §H: TnMeleeManager / TnMeleeSet / TnAnimNotify_DamageSweep / TnTraceSweeper] ----
namespace {
struct Sweep { float t, dur; bool positionSocket; core::Vec3 extentUU; };
// Melee_EnergonSword_01 / _03: t 0.19, MeleeSocket_SmallRobot, Extent (250, 250, 350), 0.35 s.
const Sweep kWeaponSweeps[] = {{0.19f, 0.35f, false, {250, 250, 350}}};
// Transform_Whirlwind_ROBO (LightMedium shared set, 5.9 s): PositionSocket, Extent (450, 450, 200), 0.4 s each [HIGH: the
// per-chassis whirlwind anim sets shift these times slightly].
const Sweep kWhirlSweeps[] = {{0.9f, 0.4f, true, {450, 450, 200}}, {1.6f, 0.4f, true, {450, 450, 200}}, {2.2f, 0.4f, true, {450, 450, 200}},
                              {2.8f, 0.4f, true, {450, 450, 200}}, {3.4f, 0.4f, true, {450, 450, 200}}, {4.0f, 0.4f, true, {450, 450, 200}},
                              {4.54f, 0.4f, true, {450, 450, 200}}, {5.19f, 0.4f, true, {450, 450, 200}}};
}

void World::startLocalMelee(bool whirlwind) {
    Character& pc = player_.pawn();
    if (localPlayerDead() || pc.moveForm() != Form::Robot || pc.isTransforming() || pc.isMeleeing()) return;
    if (!whirlwind && pc.weapon().reloading()) return;
    pc.meleeState_ = whirlwind ? 2 : 1;
    pc.meleeT_ = 0.0f; pc.meleeSweep_ = -1; pc.meleeHit_.clear();
    pc.meleeCarrier_ = !whirlwind && pc.carryingHeavy_ != 0;
    pc.meleePoke_ = !whirlwind && !pc.meleeCarrier_ && pc.tempWeapon_ == 1;
    if (pc.meleePoke_) {
        // MWT_Poke: Melee_Axe (TR shared set: _01 1.5 s, sweep t 0.335 MeleeSocket_LargeRobot (250, 250, 350) 0.30 s), 9999,
        // TnDamageTypePoke, Impulse 200000, AttackDash 2500 / 0.25 (the assist lunge below), GroundSpeedMultiplier 1.5 [CONF].
        pc.playAction("Melee_Axe_01", false);
        pc.meleeLen_ = 1.5f;
        if (const assets::SkinnedModel* mm = pc.currentModel()) { int ci = mm->clipByName("Melee_Axe_01"); if (ci >= 0) pc.meleeLen_ = mm->clips[(size_t)ci].duration; }
    }
    if (pc.meleeCarrier_) {
        // FindAttack: current weapon MeleeWeaponType MWT_Flag / MWT_Bomb -> Melee_Mace (chooser _01/_02/_03), 9999, AttackDash
        // Speed 0 (no lunge), GroundSpeedMultiplier 0.75 [CONF TnMeleeSet]. Clip lengths / sweep times from the anim set when
        // the robot export lacks the clip.
        static const char* kMace[3] = {"Melee_Mace_01", "Melee_Mace_02", "Melee_Mace_03"};
        static const float kMaceLen[3] = {1.033f, 1.233f, 1.167f};
        const int k = pc.meleeAlternate_++ % 3;
        pc.meleeVariant_ = k;
        pc.playAction(kMace[k], false);
        pc.meleeLen_ = kMaceLen[k];
        if (const assets::SkinnedModel* m = pc.currentModel()) { int ci = m->clipByName(kMace[k]); if (ci >= 0) pc.meleeLen_ = m->clips[(size_t)ci].duration; }
        return;
    }
    if (pc.meleePoke_) {
        // fall through to the assist lunge
    } else if (whirlwind) {
        pc.meleeLen_ = 5.9f;
        pc.playAction("Transform_Whirlwind_ROBO", true);
        return;
    }
    // AnimSet chooser: Melee_EnergonSword -> _01 / _03 alternately; clip length (1.0 s authored) from the model when present.
    const char* clip = (pc.meleeAlternate_++ % 2) ? "Melee_EnergonSword_03" : "Melee_EnergonSword_01";
    if (!pc.meleePoke_) {
    pc.playAction(clip, false);
    pc.meleeLen_ = 1.0f;
    if (const assets::SkinnedModel* m = pc.currentModel()) { int ci = m->clipByName(clip); if (ci >= 0) pc.meleeLen_ = m->clips[(size_t)ci].duration; }
    }
    // PlayerTargeting.GetMeleeAssistTarget: an enemy within 2000 UU inside the picker cone (half angle
    // clamp(4 deg, atan(3.5 m / d), atan(4.5 m / d)) about the view direction) -> AttackDash lunge toward it (yaw only).
    if (!matchActive_) return;
    const core::Vec3 eye = pc.actorLocation();
    const core::Vec3 fwd = core::forwardFromYawPitch(player_.controller().viewYaw(), 0.0f);
    float best = 1e9f;
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
        core::Vec3 d = o->pawn().actorLocation() - eye; d.y = 0.0f;
        float dist = core::length(d);
        if (dist > 20.0f || dist < 1e-3f) continue;
        float half = std::max(4.0f * 0.0174533f, std::min(std::atan(4.5f / dist), std::max(std::atan(3.5f / dist), 4.0f * 0.0174533f)));
        if (std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f)) > half) continue;
        if (dist < best) { best = dist; pc.lungeDir_ = d * (1.0f / dist); }
    }
    if (best < 1e9f) { pc.lungeRemain_ = 0.25f; pc.setYaw(std::atan2(-pc.lungeDir_.x, -pc.lungeDir_.z)); }
}

void World::tickLocalMelee(float dt) {
    Character& pc = player_.pawn();
    if (!pc.isMeleeing()) return;
    pc.meleeT_ += dt;
    const bool whirl = pc.meleeState_ == 2;
    // Melee_Mace_01/_02/_03 sweeps (LightMedium shared set): t 0.328 / 0.287 / 0.383, SmallRobot, (250, 250, 350), 0.30 s.
    static const Sweep kPokeSweep = {0.335f, 0.30f, false, {250, 250, 350}};
    static const Sweep kMaceSweeps[3] = {{0.328f, 0.30f, false, {250, 250, 350}}, {0.287f, 0.30f, false, {250, 250, 350}},
                                         {0.383f, 0.30f, false, {250, 250, 350}}};
    const Sweep* sw = whirl ? kWhirlSweeps : pc.meleeCarrier_ ? &kMaceSweeps[pc.meleeVariant_ % 3] : pc.meleePoke_ ? &kPokeSweep : kWeaponSweeps;
    const int n = whirl ? 8 : 1;
    int active = -1;
    for (int i = 0; i < n; ++i) if (pc.meleeT_ >= sw[i].t && pc.meleeT_ < sw[i].t + sw[i].dur) active = i;
    if (active != pc.meleeSweep_) { pc.meleeSweep_ = active; pc.meleeHit_.clear(); pc.meleeHitRoller_ = false; }   // a new sweep: hit list reset
    if (active >= 0 && matchActive_) {
        const SocketDef& sd = sw[active].positionSocket ? pc.chassis().positionSocket : pc.chassis().meleeSmall;
        core::Vec3 at = pc.actorLocation();
        core::Mat4 bm;
        if (sd.valid && pc.boneWorld(sd.bone, bm)) { core::Mat4 w = bm * sd.local; at = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
        const core::Vec3 ex{sw[active].extentUU.x * 0.01f, sw[active].extentUU.z * 0.01f, sw[active].extentUU.y * 0.01f};   // UE Z = up
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        const float damage = whirl ? 85.0f : (pc.meleeCarrier_ || pc.meleePoke_) ? 9999.0f : 150.0f;
        if (roller_.alive && !pc.meleeHitRoller_ && std::fabs(roller_.pos.x - at.x) <= ex.x + 1.21f && std::fabs(roller_.pos.z - at.z) <= ex.z + 1.21f &&
            std::fabs(roller_.pos.y - at.y) <= ex.y + 1.21f) {
            core::Vec3 d = roller_.pos - pc.actorLocation(); d.y = 0.0f;
            if (core::length(d) > 1e-4f) roller_.vel = roller_.vel + core::normalize(d) * 50.0f;   // TnRollerMineAbility.MeleeImpulse 5000
            pc.meleeHitRoller_ = true;
        }
        const char* type = whirl ? "TransGame.TnDamageTypeWhirlwind" : pc.meleePoke_ ? "TransGame.TnDamageTypePoke" : "TransGame.TnDamageTypeMelee";
        for (MatchOpponent* o : opponents_) {
            if (!o->spawned() || std::find(pc.meleeHit_.begin(), pc.meleeHit_.end(), o->matchPlayer()) != pc.meleeHit_.end()) continue;
            const Character& v = o->pawn();
            const core::Vec3 c = v.actorLocation();
            const float r = v.cylinderRadius(v.moveForm()), hh = v.cylinderHalfHeight(v.moveForm());
            // MultiPointCheck of the box against the victim cylinder (as a box), then a clear trace attacker -> victim.
            if (std::fabs(c.x - at.x) > ex.x + r || std::fabs(c.z - at.z) > ex.z + r || std::fabs(c.y - at.y) > ex.y + hh) continue;
            float t;
            if (line && line->segmentHit(pc.actorLocation(), c, t)) continue;
            pc.meleeHit_.push_back(o->matchPlayer()); ++pc.meleeHitCount_;
            applyMatchDamage(o->matchPlayer(), localPlayer_, damage, false, type);
            // Momentum = normal(victim - attacker) x Impulse (WeaponAttack 30000, flag / bomb 80000, Whirlwind 2000) [CONF].
            const float impulse = whirl ? 2000.0f : pc.meleeCarrier_ ? 80000.0f : pc.meleePoke_ ? 200000.0f : 30000.0f;
            core::Vec3 dir = c - pc.actorLocation();
            if (core::length(dir) > 1e-4f) applyKnockback(o->matchPlayer(), core::normalize(dir) * impulse, type);
        }
    }
    if (pc.meleeT_ >= pc.meleeLen_) { pc.meleeState_ = 0; pc.meleeSweep_ = -1; }
}

const Character* World::matchPawn(int mp) const {
    if (mp < 0) return nullptr;
    if (mp == localPlayer_) return localDead_ ? nullptr : &player_.pawn();
    for (const MatchOpponent* o : opponents_) if (o->matchPlayer() == mp) return o->spawned() ? &o->pawn() : nullptr;
    return nullptr;
}

// TnWeaponHoming.Active.Tick for the local pawn [CONF RE TARGETED_PASS3 §H2]: PlayerTargeting.GetHomingLockTarget = the picked
// enemy within weapon range inside TnTargetableComponent picker index 4 ("Homing Lock": robot 4 deg / 600-700 UU; car
// 4 deg / 500-700 UU; other vehicle forms use the car picker [PROV]), dropped when in robot form and !CanLockOnToRobots.
// A target accumulates LockOnTimer -> locked at LockOnTime; a target change resets it; with no target HoldLockOnTimer
// drops the lock after HoldLockOnTime (at once when the target is dead).
void World::tickHomingLock(float dt) {
    Character& pc = player_.pawn();
    const Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();
    if (!matchActive_ || localDead_ || pc.isTransforming() || !w || !w->projHoming || w->lockOnTime <= 0.0f) {
        lockCandidate_ = lockTarget_ = -1; lockTimer_ = holdLockTimer_ = 0.0f; locked_ = false;
        return;
    }
    // The picker ray is the crosshair (camera) ray, as firing aims through it.
    const core::Vec3 eye = player_.controller().cameraPos();
    const core::Vec3 fwd = core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch());
    const float deg4 = 4.0f * 0.0174533f;
    int best = -1; float bestAng = 1e9f;
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
        const Character& t = o->pawn();
        const bool robot = t.moveForm() == Form::Robot;
        if (robot && !w->lockRobots) continue;
        core::Vec3 d = t.actorLocation() - eye;
        float dist = core::length(d);
        if (dist < 1e-3f || dist > w->rangeM) continue;
        const float minR = robot ? 6.0f : 5.0f, maxR = 7.0f;
        float half = std::max(std::atan(minR / dist), std::min(deg4, std::atan(maxR / dist)));
        half = std::min(half, std::atan(maxR / dist));
        float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));
        if (ang <= half && ang < bestAng) { bestAng = ang; best = o->matchPlayer(); }
    }
    if (best >= 0) {
        holdLockTimer_ = 0.0f;
        if (best != lockCandidate_) { lockCandidate_ = best; lockTimer_ = 0.0f; if (best != lockTarget_) locked_ = false; }
        lockTimer_ += dt;
        if (lockTimer_ >= w->lockOnTime && (w->ammo > 0 || w->magSize <= 0)) { locked_ = true; lockTarget_ = best; }
    } else {
        lockCandidate_ = -1; lockTimer_ = 0.0f;
        if (locked_) {
            holdLockTimer_ += dt;
            if (holdLockTimer_ >= w->holdLockOnTime || !matchPawn(lockTarget_)) { locked_ = false; lockTarget_ = -1; holdLockTimer_ = 0.0f; }
        }
    }
}

const Weapon* World::grenadeBag(const Character& c) const {
    for (const Weapon& w : c.inventory()) if (w.grenade()) return &w;
    return nullptr;
}

// TnPlayerController.PlayerWalking.TossGrenade -> TnGrenadeBag.TossGrenade (CanToss: ammo, FireInterval 1.5 s) ->
// TnGrenadeThrower: target = the view trace from TargetTraceRange.Min 1000 to .Max 10000 UU (hit or end); GrenadeThrow
// upper-body anim; spawn after TossDelay 0.4 s [CONF script + authored].
void World::startLocalGrenadeToss() {
    Character& pc = player_.pawn();
    if (localDead_ || pc.moveForm() != Form::Robot || pc.isTransforming() || pc.isMeleeing() || grenadeTossDelay_ >= 0.0f) return;
    if (pc.carryingHeavy_ != 0) return;   // carrying a WT_Heavy weapon: PlayDryFireSound
    Weapon* gb = nullptr;
    for (Weapon& w : pc.inventoryMutable()) if (w.grenade()) { gb = &w; break; }
    if (!gb) return;
    if (gb->reserve <= 0 || grenadeCooldown_ > 0.0f) return;   // PlayDryFireSound
    gb->reserve -= 1;
    grenadeCooldown_ = gb->fireInterval;
    const core::Vec3 cam = player_.controller().cameraPos();
    const core::Vec3 dir = core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch());
    const core::Vec3 a = cam + dir * 10.0f, b = cam + dir * 100.0f;
    grenadeTarget_ = b;
    float t;
    const CollisionWorld* line = collision_.valid() ? &collision_ : nullptr;
    if (line && line->segmentHit(a, b, t)) grenadeTarget_ = a + (b - a) * t;
    pc.playAction("GrenadeThrow", true);
    pc.exposeSelf();   // ServerTossGrenade -> ExposeSelf
    grenadeTossDelay_ = 0.4f;
}

// TnPawn bIgnoreForces = True; ShouldIgnoreForces is false only for damage types with RequestRespectForcesApplied: the melee
// family (Melee / WeakMelee / Whirlwind), Shockwave, AOE*, HeavyTankShell*, ShieldPush*, OmegaAOE*, ExplodeWithForces,
// BruteBackpack, OmegaTractorBeamGrab (* = bExtraMomentumZ). Weapon / projectile / grenade types give no knockback [CONF §I].
void World::applyKnockback(int victim, const core::Vec3& m, const std::string& type) {
    static const char* kRespect[] = {"TnDamageTypeMelee", "TnDamageTypeWeakMelee", "TnDamageTypeWhirlwind", "TnDamageTypeShockwave",
                                     "TnDamageTypeAOE", "TnDamageTypeHeavyTankShell", "TnDamageTypeShieldPush", "TnDamageTypeOmegaAOE",
                                     "TnDamageTypeExplodeWithForces", "TnDamageTypeBruteBackpack", "TnDamageTypeOmegaTractorBeamGrab"};
    static const char* kExtraZ[] = {"TnDamageTypeAOE", "TnDamageTypeHeavyTankShell", "TnDamageTypeShieldPush", "TnDamageTypeOmegaAOE"};
    const std::string t = type.substr(type.find('.') == std::string::npos ? 0 : type.find('.') + 1);
    bool respect = false, extraZ = false;
    for (const char* k : kRespect) if (t.rfind(k, 0) == 0) respect = true;   // prefix: subclasses (AOE*, ...) inherit
    for (const char* k : kExtraZ) if (t.rfind(k, 0) == 0) extraZ = true;
    if (!respect) return;
    Character* p = nullptr;
    if (victim == localPlayer_) p = localDead_ ? nullptr : &player_.pawn();
    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == victim && o->spawned()) p = &o->pawn();
    if (p) p->addMomentum(m, extraZ);
}

// ---- Barrier [CONF TnAbilityBarrier / TnBarrierSpawnable script + authored CDOs; RE TARGETED_PASS3 §I3] ----
// Spawn at Location + BarrierOffset (1000, 0, -200) rotated by the pawn, facing the pawn's rotation. Collision = the
// WEP_Barrier_PHYSSYS box on C_Robo01_XT: X 167 / Y 1736 / Z 823.5 UU at (-59, 0, 91) - placed about the bone's
// end-of-Barrier_Equip position with the bone frame taken as the actor frame [PROV]. Blocks pawns, hitscan and projectiles
// (zero-extent blocking HIGH). BarrierHealth 1000, DegenRate 15/s, ignores melee; at 0: FadeOutTime 3 s, then destroyed;
// destroyed on the owner's death. The ability Cooldown 20 s starts once the barrier is gone (ServerCanStartCooldown).
namespace {
std::vector<core::Vec3> boxTris(const core::Vec3& c, const core::Vec3& h) {
    const core::Vec3 v[8] = {{c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                             {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
                             {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z}};
    const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2},
                          {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    std::vector<core::Vec3> out;
    for (const auto& t : f) for (int k = 0; k < 3; ++k) out.push_back(v[t[k]]);
    return out;
}
}

void World::spawnBarrier() {
    Character& pc = player_.pawn();
    if (localDead_) { pc.barrierAlive_ = false; return; }   // IsOwnerDead
    if (!barrierModelTried_) {
        barrierModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_Shield_p/Barrier/";
        if (assets::loadSkinnedGlb(ext + "WEP_Barrier_SKEL.gltf", barrierModel_) && barrierModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_Barrier_ANIM.anim.gltf", barrierModel_);
            resolveModelTextures(barrierModel_);
        } else LOG_ERROR("barrier: WEP_Barrier_SKEL unavailable (collision only)");
    }
    BarrierState& b = barrier_;
    b = BarrierState{};
    b.alive = true; b.health = 1000.0f; b.yaw = pc.yaw();
    const core::Vec3 fwd = core::forwardFromYawPitch(b.yaw, 0.0f);
    b.pos = pc.actorLocation() + fwd * 10.0f + core::Vec3{0, -2.0f, 0};
    b.world = core::Mat4::translate(b.pos) * core::Mat4::rotateY(b.yaw + core::config::kMeshYawOffset);
    // C_Robo01_XT position at the end of Barrier_Equip (mesh space), else the actor origin.
    core::Vec3 bone{0, 0, 0};
    if (barrierModel_.valid()) {
        int clip = barrierModel_.clipByName("Barrier_Equip"), node = barrierModel_.nodeByName("C_Robo01_XT");
        if (node >= 0) {
            assets::LocalPose lp; std::vector<core::Mat4> g;
            if (clip >= 0) {
                assets::samplePose(barrierModel_, clip, barrierModel_.clips[(size_t)clip].duration, false, lp);
                assets::skinPose(barrierModel_, lp, g, barrierMesh_);
            }
            if ((size_t)node < g.size()) bone = core::Vec3{g[(size_t)node].m[12], g[(size_t)node].m[13], g[(size_t)node].m[14]};
        }
    }
    const core::Vec3 centre = bone + core::Vec3{-0.59f, 0.91f, 0.0f};
    b.half = core::Vec3{0.835f, 4.1175f, 8.68f};          // (X 167, Z 823.5, Y 1736) / 2 in mesh axes (fwd, up, right)
    b.boxInv = core::Mat4::translate(centre * -1.0f) * core::Mat4::rotateY(-(b.yaw + core::config::kMeshYawOffset)) * core::Mat4::translate(b.pos * -1.0f);
    const std::vector<core::Vec3> tris = boxTris(centre, b.half);
    if (barrierDyn_ < 0) barrierDyn_ = collision_.addDynamicSet(tris, b.world); else { collision_.setDynamicPose(barrierDyn_, b.world); collision_.setDynamicEnabled(barrierDyn_, true); }
    if (weaponCollision_.valid()) {
        if (barrierDynW_ < 0) barrierDynW_ = weaponCollision_.addDynamicSet(tris, b.world);
        else { weaponCollision_.setDynamicPose(barrierDynW_, b.world); weaponCollision_.setDynamicEnabled(barrierDynW_, true); }
    }
    LOG_INFO("ability Barrier: wall at (%.1f %.1f %.1f), 1000 HP", b.pos.x, b.pos.y, b.pos.z);
}

void World::tickBarrier(float dt) {
    Character& pc = player_.pawn();
    if (barrierDelay_ >= 0.0f) { barrierDelay_ -= dt; if (barrierDelay_ < 0.0f) spawnBarrier(); }
    BarrierState& b = barrier_;
    if (b.alive) {
        b.t += dt;
        if (b.fade < 0.0f) {
            b.health -= 15.0f * dt;                        // DegenRate
            if (b.health <= 0.0f) { b.health = 0.0f; b.fade = 3.0f; }   // DestroySound, FadeOutTime
        } else {
            b.fade -= dt;
            if (b.fade <= 0.0f) b.alive = false;
        }
    }
    if (!b.alive) {
        if (barrierDyn_ >= 0) collision_.setDynamicEnabled(barrierDyn_, false);
        if (barrierDynW_ >= 0) weaponCollision_.setDynamicEnabled(barrierDynW_, false);
    }
    pc.barrierAlive_ = b.alive || barrierDelay_ >= 0.0f;
    if (b.alive && barrierModel_.valid()) {
        int clip = barrierModel_.clipByName("Barrier_Equip");
        assets::LocalPose lp; std::vector<core::Mat4> g;
        if (clip >= 0) { assets::samplePose(barrierModel_, clip, b.t, false, lp); assets::skinPose(barrierModel_, lp, g, barrierMesh_); }
    }
}

bool World::barrierRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const {
    if (!barrier_.alive) return false;
    const core::Vec3 lo = core::transformPoint(barrier_.boxInv, o);
    const core::Vec3 ld = core::transformPoint(barrier_.boxInv, o + d) - lo;
    return rayAabb(lo, ld, range, barrier_.half * -1.0f, barrier_.half, t);
}

void World::damageBarrier(float amount, const std::string& type) {
    if (!barrier_.alive || barrier_.fade >= 0.0f) return;
    if (type.find("Melee") != std::string::npos || type.find("Whirlwind") != std::string::npos) return;   // ignores melee
    barrier_.health -= amount;
    if (barrier_.health <= 0.0f) { barrier_.health = 0.0f; barrier_.fade = 3.0f; }
}

// ---- Ammo beacon [CONF TnAbilitySpawnInventory / TnAbilitySpawnAmmoCrate / TnDroppedPickupAmmoBeacon / TnDroppedPickupDefrag
// script + authored CDOs] ----
// SpawnDelay 0.5 -> DropFrom(owner Location, TossVelocity (2000, 1200, 0) rotated by the owner) -> falls and lands.
// BeaconLifespan 60 s; the owner dead -> FadeOut. Pickup.Tick: every pawn within Radius 1500 UU that VisibleCollidingActors
// finds (owner or same team): current weapon FillReserveAmmo when not full, TnBuffAmmoBeaconIncreaseDamage (x1.15, 1 s,
// reset while in range). Health 100: damage from the owner or the owner's team is ignored. TnAmmoBeacon.PickupAllowed false.
// Cooldown[0] 60 s once the beacon is gone (ServerCanStartCooldown). Skill gifts / grenades not applied (no skills in MP).
// FadeOut duration not applied: removal is immediate [PARTIAL].
void World::tickAmmoBeacon(float dt) {
    Character& pc = player_.pawn();
    auto tickBuff = [dt](Character& p) {
        p.beaconDamageBuff_ = std::max(0.0f, p.beaconDamageBuff_ - dt);
        p.seeEnemiesRemain_ = std::max(0.0f, p.seeEnemiesRemain_ - dt);
        p.hardLockedRemain_ = std::max(0.0f, p.hardLockedRemain_ - dt);
        p.refillOnKillRemain_ = std::max(0.0f, p.refillOnKillRemain_ - dt);
        p.jammedRemain_ = std::max(0.0f, p.jammedRemain_ - dt);
    };
    tickBuff(pc);
    for (MatchOpponent* o : opponents_) tickBuff(o->pawn());
    if (beaconDelay_ >= 0.0f) {
        beaconDelay_ -= dt;
        if (beaconDelay_ < 0.0f && !localDead_) {
            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r{-f.z, 0.0f, f.x};
            beacon_ = AmmoBeacon{};
            beacon_.alive = true; beacon_.pos = pc.actorLocation(); beacon_.vel = f * 20.0f + r * 12.0f;
            beacon_.life = 60.0f; beacon_.health = 100.0f;
            LOG_INFO("ability SpawnAmmoCrate: beacon dropped");
        }
    }
    AmmoBeacon& b = beacon_;
    if (b.alive) {
        b.life -= dt;
        if (b.life <= 0.0f || localDead_ || b.health <= 0.0f) b.alive = false;
    }
    if (b.alive && !b.landed) {
        // PHYS_Falling until it lands (stock DroppedPickup physics; walls stop the horizontal travel).
        b.vel.y -= core::config::kGravity * dt;
        core::Vec3 next = b.pos + b.vel * dt;
        float t; core::Vec3 n;
        if (collision_.valid() && collision_.segmentHit(b.pos, next, t, n)) {
            next = b.pos + (next - b.pos) * std::max(0.0f, t - 1e-3f);
            if (n.y > 0.7f) { b.landed = true; b.vel = {0, 0, 0}; } else { b.vel.x = 0.0f; b.vel.z = 0.0f; }
        }
        b.pos = next;
        if (b.pos.y < killZ_) b.alive = false;
    }
    if (b.alive) {
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        const core::Vec3 eye = b.pos + core::Vec3{0, 0.5f, 0};
        auto serve = [&](Character& p) {
            if (core::length(p.actorLocation() - b.pos) > 15.0f) return;
            float t;
            if (line && line->segmentHit(eye, p.actorLocation(), t)) return;   // VisibleCollidingActors
            Weapon& w = p.weapon();
            if (w.reserve < w.reserveMax) w.reserve = w.reserveMax;          // FillReserveAmmo
            p.beaconDamageBuff_ = 1.0f;                                       // AddBuff / ResetBuffTime
        };
        if (!localDead_) serve(pc);
        const int team = matchActive_ && localPlayer_ >= 0 ? match_.players()[(size_t)localPlayer_].team : 255;
        if (matchActive_ && match_.settings().teamGame)
            for (MatchOpponent* o : opponents_) if (o->spawned() && o->team() == team) serve(o->pawn());
    }
    pc.beaconAlive_ = b.alive || beaconDelay_ >= 0.0f;
}

void World::damageAmmoBeacon(float amount, int instigator) {
    if (!beacon_.alive || instigator < 0 || instigator == localPlayer_) return;
    if (matchActive_ && match_.sameTeam(instigator, localPlayer_)) return;
    beacon_.health -= amount;
}

// ---- Sentry [CONF TnAbilitySpawnSentry / TnSentryPawnAbility / TnAiSentryController script + authored Default_TURRETDEF /
// Default_WEPDATA / Sentry_DSYS; RE TARGETED_PASS3 §J] ----
// Spawn: SpawnDelay 0.2; desired = owner + (0, 0, SpawnHeight 375) rotated, clamped by a trace from the owner; the sentry pawn
// then settles on the floor below [HIGH pawn falling]. Health 135 (Sentry_DSYS), drained to 0 over Lifetime 30 s; owner
// damage ignored; melee kills it; dies with the owner; one per owner. Targeting: the closest visible enemy (SightRadius
// 30000, 360 deg) within pitch -45..45; YawPitchControl LagDegreesPerSecond 270; fires while aimed within
// AimedAtTargetThreshold 3 deg and closer than MaxAttackRange 6000: instant hit 8 x RangeDamageModifiers (1.0 to 8000,
// 0.5 at 30000) every 0.12 s with PerShotSpread 0.1; heat +2 per shot to HeatMax 100, OverheatDelay 2 s (heat then
// reset [PROV: lose-heat rate native]). Kill credit to the owner (_KillOwner). Cooldown 60 s once the sentry is gone.
// PARTIAL: flashbang dormancy, Rocket / Repair blueprints (skills), turret pitch on the mesh, corpse (LifeSpanAfterDeath 5).
void World::spawnSentry() {
    Character& pc = player_.pawn();
    if (localDead_) return;
    if (!sentryModelTried_) {
        sentryModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_SentryAbility_p/";
        if (assets::loadSkinnedGlb(ext + "WEP_SentryDeploy_SKEL.gltf", sentryModel_) && sentryModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_DeployedTurret_ANIM.anim.gltf", sentryModel_);
            resolveModelTextures(sentryModel_);
        } else LOG_ERROR("sentry: WEP_SentryDeploy_SKEL unavailable");
    }
    core::Vec3 from = pc.actorLocation(), desired = from + core::Vec3{0, 3.75f, 0};
    float t;
    if (collision_.valid() && collision_.segmentHit(from, desired, t)) desired = from + (desired - from) * std::max(0.0f, t - 0.02f);
    core::Vec3 gn; float gy;
    core::Vec3 p = desired;
    if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y, 0.2f, gy, gn)) p.y = gy; else p = pc.position();
    sentry_ = Sentry{};
    sentry_.alive = true; sentry_.pos = p; sentry_.yaw = pc.yaw(); sentry_.health = 135.0f;
    LOG_INFO("ability SpawnSentry: sentry at (%.1f %.1f %.1f)", p.x, p.y, p.z);
}

void World::tickSentry(float dt) {
    Character& pc = player_.pawn();
    if (sentryDelay_ >= 0.0f) { sentryDelay_ -= dt; if (sentryDelay_ < 0.0f) spawnSentry(); }
    Sentry& s = sentry_;
    if (s.alive) {
        s.t += dt;
        s.health -= 135.0f / 30.0f * dt;                  // Lifetime 30
        if (s.health <= 0.0f || localDead_) { s.alive = false; s.target = -1; }
    }
    if (s.alive) {
        const core::Vec3 muzzle = s.pos + core::Vec3{0, 2.0f, 0};
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        // Target: closest visible valid enemy within the pitch constraints.
        s.target = -1;
        float best = 300.0f;
        const MatchOpponent* tgt = nullptr;
        if (matchActive_)
            for (MatchOpponent* o : opponents_) {
                if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                core::Vec3 d = o->pawn().actorLocation() - muzzle;
                float dist = core::length(d);
                if (dist > best || dist < 1e-3f) continue;
                if (std::fabs(std::atan2(d.y, std::hypot(d.x, d.z))) > 45.0f * 0.0174533f) continue;
                float tt;
                if (line && line->segmentHit(muzzle, o->pawn().actorLocation(), tt)) continue;
                best = dist; s.target = o->matchPlayer(); tgt = o;
            }
        float wantYaw = s.yaw, wantPitch = 0.0f;   // idle: pitch returns to 0
        if (tgt) {
            core::Vec3 d = tgt->pawn().actorLocation() - muzzle;
            wantYaw = std::atan2(-d.x, -d.z);
            wantPitch = std::atan2(d.y, std::hypot(d.x, d.z));
        }
        const float maxStep = 270.0f * 0.0174533f * dt;
        float dy = std::remainder(wantYaw - s.yaw, 6.2831853f);
        s.yaw += core::clampf(dy, -maxStep, maxStep);
        s.pitch += core::clampf(wantPitch - s.pitch, -maxStep, maxStep);
        s.fireTimer = std::max(0.0f, s.fireTimer - dt);
        if (s.overheat > 0.0f) { s.overheat -= dt; if (s.overheat <= 0.0f) s.heat = 0.0f; }
        if (tgt && s.overheat <= 0.0f && s.fireTimer <= 0.0f) {
            float err = std::fabs(std::remainder(wantYaw - s.yaw, 6.2831853f)) + std::fabs(wantPitch - s.pitch);
            float dist = core::length(tgt->pawn().actorLocation() - muzzle);
            if (err <= 3.0f * 0.0174533f && dist < 60.0f) {
                s.fireTimer = 0.12f;
                ++s.shots;
                s.heat += 2.0f;
                if (s.heat >= 100.0f) s.overheat = 2.0f;
                core::Vec3 dir = core::forwardFromYawPitch(s.yaw, s.pitch);
                core::Vec3 up{0, 1, 0}, rt = core::normalize(core::cross(dir, up)), u2 = core::cross(rt, dir);
                auto rf = []() { return (float)(std::rand() % 2001 - 1000) / 1000.0f; };
                dir = core::normalize(dir + rt * (rf() * 0.1f) + u2 * (rf() * 0.1f));
                float wall = 300.0f, tw;
                if (line && line->segmentHit(muzzle, muzzle + dir * 300.0f, tw)) wall = 300.0f * tw;
                MatchOpponent* hit = nullptr; float hd = wall;
                for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(muzzle, dir, wall, th) && th < hd) { hd = th; hit = o; } }
                if (hit && !match_.sameTeam(hit->matchPlayer(), localPlayer_)) {
                    float mod = hd <= 80.0f ? 1.0f : 1.0f - 0.5f * std::min(1.0f, (hd - 80.0f) / 220.0f);
                    applyMatchDamage(hit->matchPlayer(), localPlayer_, 8.0f * mod, false, "TransGame.TnDamageTypeSentry");
                }
            }
        }
        if (sentryModel_.valid()) {
            int clip = sentryModel_.clipByName("WEP_DeployedTurret_Activate");
            if (clip >= 0) { assets::LocalPose lp; std::vector<core::Mat4> g; assets::samplePose(sentryModel_, clip, s.t, false, lp); assets::skinPose(sentryModel_, lp, g, sentryMesh_); }
        }
    }
    pc.sentryAlive_ = s.alive || sentryDelay_ >= 0.0f;
}

bool World::sentryRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const {
    if (!sentry_.alive) return false;
    // CollisionCylinder radius 200 / height 200 UU about the base + 2 m [HIGH: the DSYS mesh physics is the hit volume].
    const core::Vec3 c = sentry_.pos + core::Vec3{0, 2.0f, 0};
    float ox = o.x - c.x, oz = o.z - c.z, a = d.x * d.x + d.z * d.z, b = 2.0f * (ox * d.x + oz * d.z), cc = ox * ox + oz * oz - 4.0f;
    if (a < 1e-8f) return false;
    float disc = b * b - 4.0f * a * cc;
    if (disc < 0.0f) return false;
    float tt = std::max(0.0f, (-b - std::sqrt(disc)) / (2.0f * a));
    if (tt > range) return false;
    float y = o.y + d.y * tt;
    if (y < c.y - 2.0f || y > c.y + 2.0f) return false;
    t = tt;
    return true;
}

void World::damageSentry(float amount, int instigator, const std::string& type) {
    if (!sentry_.alive || instigator == localPlayer_) return;   // the owner cannot damage it
    if (instigator >= 0 && matchActive_ && match_.sameTeam(instigator, localPlayer_)) return;
    if (type.find("Melee") != std::string::npos || type.find("Whirlwind") != std::string::npos) { sentry_.health = 0.0f; return; }   // melee kills
    sentry_.health -= amount;
}

// ---- Guided missile [CONF RE TARGETED_PASS3 §J3; authored GuidedMissile_PROJDATA, CAM_Strategies_p.GuidedMissile_STRATEGY] ----
// Skill_GuidedMissile loop for 1.0 s, then spawn at ReactionSocket_Chest + (200, 0, 0) along the controller rotation with the
// pitch clamped to [700, 16384] rotator units (3.8 .. 90 deg). The PC enters GuidingMissile: inputs cleared (pawn stops),
// camera = HmAttachToActorCameraBehavior LocalOffset (135, 0, 125) in the missile frame, FOV 120. Steering: Acceleration =
// (LeftRight x right + UpDown x up) x ControlStrength 2500, speed held at MaxSpeed 2000. Ability button again = detonate.
// Fuse 30 s; Damage 10000 / DamageRadius 4500 (TnDamageTypeGuidedMissile). Cooldown 45 s once the missile is gone.
// Chest socket approximated by eye height [PROV]; fuse expiry detonates [PROV].
void World::startGuidedMissile() {
    Character& pc = player_.pawn();
    if (localDead_ || missile_.alive || missileDelay_ >= 0.0f) return;
    pc.playAction("Skill_GuidedMissile", false);
    missileDelay_ = 1.0f;
    pc.missileAlive_ = true;
}

void World::detonateGuidedMissile(const core::Vec3& at) {
    if (!missile_.alive) return;
    missile_.alive = false;
    radiusDamage(at, 10000.0f, 45.0f, localPlayer_, "TransGame.TnDamageTypeGuidedMissile");
    LOG_INFO("guided missile detonated at (%.1f %.1f %.1f)", at.x, at.y, at.z);
}

void World::tickGuidedMissile(float dt) {
    Character& pc = player_.pawn();
    PlayerController& ctl = player_.controller();
    if (missileDelay_ >= 0.0f) {
        missileDelay_ -= dt;
        if (missileDelay_ < 0.0f && !localDead_) {
            const float pitch = core::clampf(ctl.camPitch(), 700.0f * 6.2831853f / 65536.0f, 1.5707963f);
            const core::Vec3 dir = core::forwardFromYawPitch(ctl.camYaw(), pitch);
            missile_ = GuidedMissile{};
            missile_.alive = true; missile_.life = 30.0f; missile_.vel = dir * 20.0f;
            missile_.pos = pc.actorLocation() + core::Vec3{0, pc.robotParams().eyeHeight, 0} + core::forwardFromYawPitch(pc.yaw(), 0.0f) * 2.0f;
            ctl.setGuiding(true);
            LOG_INFO("guided missile launched (pitch %.1f deg)", pitch * 57.2958f);
        }
    }
    GuidedMissile& m = missile_;
    if (m.alive) {
        if (localDead_) detonateGuidedMissile(m.pos);
        else if (ctl.consumeDetonateRequest()) detonateGuidedMissile(m.pos);
    }
    if (m.alive) {
        const core::Vec3 f = core::normalize(m.vel);
        const core::Vec3 r = core::normalize(core::cross(f, core::Vec3{0, 1, 0})), u = core::cross(r, f);
        m.vel = m.vel + (r * ctl.guideLR() + u * ctl.guideUD()) * (25.0f * dt);
        m.vel = core::normalize(m.vel) * 20.0f;                 // MaxSpeed 2000
        const core::Vec3 next = m.pos + m.vel * dt;
        float t; core::Vec3 n;
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        bool hit = line && line->segmentHit(m.pos, next, t, n);
        core::Vec3 at = hit ? m.pos + (next - m.pos) * t : next;
        const core::Vec3 d = next - m.pos; const float len = core::length(d);
        if (len > 1e-5f)
            for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(m.pos, d * (1.0f / len), len, th)) { hit = true; at = m.pos + d * (th / len); } }
        m.life -= dt;
        if (hit || m.life <= 0.0f || m.pos.y < killZ_) detonateGuidedMissile(at);
        else m.pos = next;
    }
    if (m.alive) {
        const core::Vec3 f = core::normalize(m.vel);
        const float yaw = std::atan2(-f.x, -f.z), pitch = std::asin(core::clampf(f.y, -1.0f, 1.0f));
        const core::Vec3 r = core::normalize(core::cross(f, core::Vec3{0, 1, 0})), u = core::cross(r, f);
        // LocalOffset (135, 0, 125) UU: X forward, Z up in the missile frame.
        ctl.setSpectatorView(m.pos + f * 1.35f + u * 1.25f, yaw, pitch, 120.0f);
    } else if (ctl.guiding()) {
        ctl.setGuiding(false);
        ctl.clearSpectatorView();
    }
    pc.missileAlive_ = m.alive || missileDelay_ >= 0.0f;
}

// ---- Roller sphere [CONF TnAbilityRollerSphere / TnRollerMineAbility CDOs + RE §J4; authored RB_BodySetup / PHYSMAT] ----
// SpawnDelay 0.5 -> at owner + SpawnOffset (500, 0, 100) rotated if the spot is safe (radius x 1.1 clear), else retry every
// 1 s; InitialVelocity (2750, 0, 0) local. Rigid-body sphere: radius 241.5 UU x mesh scale 0.5 = 1.21 m, PhysMaterial
// LinearDamping 0.6, stock Friction 0.7 / Restitution 0.3 [HIGH: UE3 PhysicalMaterial defaults]; rolling under gravity on
// the floor (slope acceleration not modelled [PROV]). ArmTime 3 s; _Fuse 10 s -> explodes; _Health 200; armed + touching an
// enemy pawn -> explodes: 135 / 1500 UU, no momentum (TnDamageTypeRollerMine). Owner / teammate melee kicks it: + normal
// (horizontal) x MeleeImpulse 5000. Aura BuffRadius 1500 (visible enemies): TnBuffRollerSphere speed x0.75 (1 s robot /
// 2 s vehicle), refreshed. Destroyed with the owner. Cooldown 60 s once gone.
void World::explodeRollerMine() {
    if (!roller_.alive) return;
    roller_.alive = false;
    radiusDamage(roller_.pos, 135.0f, 15.0f, localPlayer_, "TransGame.TnDamageTypeRollerMine");
    LOG_INFO("roller sphere exploded at (%.1f %.1f %.1f) t %.2f", roller_.pos.x, roller_.pos.y, roller_.pos.z, roller_.t);
}

void World::damageRollerMine(float amount, int instigator) {
    if (!roller_.alive || instigator < 0 || instigator == localPlayer_) return;
    if (matchActive_ && match_.sameTeam(instigator, localPlayer_)) return;
    roller_.health -= amount;
    if (roller_.health <= 0.0f) explodeRollerMine();
}

void World::tickRollerMine(float dt) {
    Character& pc = player_.pawn();
    auto tickBuff = [dt](Character& p) { p.rollerSlowRemain_ = std::max(0.0f, p.rollerSlowRemain_ - dt); };
    tickBuff(pc);
    for (MatchOpponent* o : opponents_) tickBuff(o->pawn());
    const float R = 1.21f;
    if (rollerDelay_ >= 0.0f) {
        rollerDelay_ -= dt;
        if (rollerDelay_ < 0.0f && !localDead_) {
            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f);
            const core::Vec3 spot = pc.actorLocation() + f * 5.0f + core::Vec3{0, 1.0f, 0};
            float t; core::Vec3 n;
            bool safe = collision_.valid() && !collision_.segmentHit(pc.actorLocation(), spot + f * (R * 1.1f), t, n);
            if (safe) {
                roller_ = RollerMine{};
                roller_.alive = true; roller_.pos = spot; roller_.vel = f * 27.5f; roller_.health = 200.0f;
                LOG_INFO("ability RollerSphere: spawned");
            } else rollerDelay_ = 1.0f;   // SpawnLocationValidator failed: retry every 1 s
        }
    }
    RollerMine& m = roller_;
    if (m.alive && localDead_) m.alive = false;   // destroyed with the owner
    if (m.alive) {
        m.t += dt;
        // Rigid body: gravity, PhysX linear damping, floor contact, wall bounce with restitution 0.3.
        m.vel.y -= core::config::kGravity * dt;
        m.vel = m.vel * std::max(0.0f, 1.0f - 0.6f * dt);
        core::Vec3 next = m.pos + m.vel * dt;
        float gy; core::Vec3 gn;
        if (collision_.valid() && collision_.groundHeight(next.x, next.z, next.y - R + 0.3f, 0.5f, gy, gn) && next.y - R <= gy) {
            next.y = gy + R;
            if (m.vel.y < 0.0f) m.vel.y = -m.vel.y * 0.3f < 0.5f ? 0.0f : -m.vel.y * 0.3f;
            m.onGround = true;
        } else m.onGround = false;
        float t; core::Vec3 n;
        const core::Vec3 c0 = m.pos + core::Vec3{0, 0.2f, 0}, c1 = next + core::Vec3{0, 0.2f, 0};
        core::Vec3 hv{m.vel.x, 0, m.vel.z};
        if (core::length(hv) > 1e-4f && collision_.valid() && collision_.segmentHit(c0, c1 + core::normalize(hv) * R, t, n)) {
            core::Vec3 hn = core::normalize(core::Vec3{n.x, 0.0f, n.z});
            if (core::length(core::Vec3{n.x, 0, n.z}) < 1e-3f) hn = core::normalize(hv) * -1.0f;
            const float vn = core::dot(m.vel, hn);
            if (vn < 0.0f) m.vel = m.vel - hn * (vn * 1.3f);   // reflect the normal part x restitution 0.3
            next = m.pos;
        }
        m.pos = next;
        if (m.pos.y < killZ_) m.alive = false;
    }
    if (m.alive) {
        const bool armed = m.t >= 3.0f;
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        bool boom = m.t >= 10.0f;                                  // _Fuse
        if (matchActive_)
            for (MatchOpponent* o : opponents_) {
                if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                const Character& e = o->pawn();
                const core::Vec3 d = e.actorLocation() - m.pos;
                const float dist = core::length(d);
                if (dist <= 15.0f) {
                    float tt;
                    if (!line || !line->segmentHit(m.pos, e.actorLocation(), tt))
                        o->pawn().rollerSlowRemain_ = std::max(o->pawn().rollerSlowRemain_, e.moveForm() == Form::Vehicle ? 2.0f : 1.0f);
                }
                const float r = e.cylinderRadius(e.moveForm()), hh = e.cylinderHalfHeight(e.moveForm());
                if (armed && std::hypot(d.x, d.z) <= r + R && std::fabs(d.y) <= hh + R) boom = true;   // RB contact
            }
        if (boom) explodeRollerMine();
    }
    pc.rollerAlive_ = m.alive || rollerDelay_ >= 0.0f;
}

int World::pickHomingTarget(bool allowRobots, float range) const {
    const core::Vec3 eye = player_.controller().cameraPos();
    const core::Vec3 fwd = core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch());
    const float deg4 = 4.0f * 0.0174533f;
    int best = -1; float bestAng = 1e9f;
    if (!matchActive_) return -1;
    for (const MatchOpponent* o : opponents_) {
        if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
        const Character& t = o->pawn();
        const bool robot = t.moveForm() == Form::Robot;
        if (robot && !allowRobots) continue;
        core::Vec3 d = t.actorLocation() - eye;
        float dist = core::length(d);
        if (dist < 1e-3f || dist > range) continue;
        const float minR = robot ? 6.0f : 5.0f, maxR = 7.0f;
        float half = std::min(std::max(deg4, std::atan(minR / dist)), std::atan(maxR / dist));
        float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));
        if (ang <= half && ang < bestAng) { bestAng = ang; best = o->matchPlayer(); }
    }
    return best;
}

// AbilityJammer: TnBuffAbilityJammed 15 s (removes Cloak / Disguise / Warcry / DrainSource; abilities blocked; cooldowns frozen).
// TransformDisruptor: TnBuffTransformDisruptor 3 s (ForceIntoForm(opposite) now; transforming disabled while active)
// [CONF RE §K]. Enemies only; world hit or 3 s life ends the shot (life PROV).
void World::tickBuffShots(float dt) {
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (size_t i = 0; i < buffShots_.size();) {
        BuffShot& s = buffShots_[i];
        const core::Vec3 next = s.pos + s.vel * dt;
        float t; bool done = false;
        if (line && line->segmentHit(s.pos, next, t)) done = true;
        if (matchActive_)
            for (MatchOpponent* o : opponents_) {
                if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                Character& e = o->pawn();
                const core::Vec3 c = e.actorLocation();
                // segment-vs-cylinder approximated by the closest point on the step segment
                core::Vec3 seg = next - s.pos; float sl2 = core::dot(seg, seg);
                float u = sl2 > 1e-8f ? core::clampf(core::dot(c - s.pos, seg) / sl2, 0.0f, 1.0f) : 0.0f;
                core::Vec3 q = s.pos + seg * u;
                if (std::hypot(q.x - c.x, q.z - c.z) > e.cylinderRadius(e.moveForm()) + s.radius * 0.1f ||
                    std::fabs(q.y - c.y) > e.cylinderHalfHeight(e.moveForm()) + s.radius * 0.1f) continue;
                if (s.kind == 0) { e.applyJammed(15.0f); LOG_INFO("ability AbilityJammer: player %d jammed 15 s", o->matchPlayer()); }
                else {
                    e.transformDisruptRemain_ = 3.0f;
                    if (!e.isTransforming()) e.beginTransform();   // ForceIntoForm(opposite) [PROV: via the transform path]
                    LOG_INFO("ability TransformDisruptor: player %d forced to transform", o->matchPlayer());
                }
                done = true;
                break;
            }
        s.life -= dt;
        if (done || s.life <= 0.0f) { buffShots_.erase(buffShots_.begin() + (long)i); continue; }
        s.pos = next;
        ++i;
    }
}

// Killstreak items: P.O.K.E. timer / fire, rocket turret drop, MinePooper mines [CONF RE §K + authored].
void World::tickKillstreakItems(float dt) {
    Character& pc = player_.pawn();
    // TnWeaponPoke: SecondsUntilDeactivated 20 [H]; primary fire starts the MWT_Poke melee attack [H: melee weapons attack on fire].
    if (pc.tempWeapon_ == 1) {
        pc.tempWeaponRemain_ -= dt;
        if (pc.tempWeaponRemain_ <= 0.0f || localDead_) pc.removeTempWeapon();
        else if (player_.controller().fireHeld() && !pc.isMeleeing()) startLocalMelee(false);
    }
    // Rocket turret (WT_Heavy): tossed on Transform to vehicle (DropHeavyWeapons) or a swap (ChangedWeapon) [CONF]; the
    // dropped turret pickup is not re-takeable here [PARTIAL].
    if (pc.tempWeapon_ == 2 && (pc.isTransforming() || pc.form() == Form::Vehicle || pc.tempDropRequested_ || localDead_)) pc.removeTempWeapon();
    pc.tempDropRequested_ = false;
    // MinePooper: every 2.0 s while the 15 s buff lasts, spawn at owner + (400, 100, 0) rotated.
    if (pc.minePooperRemain_ > 0.0f && !localDead_) {
        pc.minePooperRemain_ -= dt; pc.minePooperTimer_ -= dt;
        if (pc.minePooperTimer_ <= 0.0f) {
            pc.minePooperTimer_ += 2.0f;
            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            KamikazeMine m; m.pos = pc.actorLocation() + f * 4.0f + r * 1.0f; m.vel = {0, 0, 0};
            mines_.push_back(m);
        }
    }
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (size_t i = 0; i < mines_.size();) {
        KamikazeMine& m = mines_[i];
        m.t += dt;
        bool boom = false, gone = m.t >= 60.0f || m.health <= 0.0f || localDead_;   // LifeSpan 60; destroyed with the owner [PROV]
        if (m.t < 2.0f) {
            // Hover phase (2 s) between 50 and 150 UU above the floor: rise to 1.0 m [PROV height within the range].
            float gy; core::Vec3 gn;
            if (collision_.valid() && collision_.groundHeight(m.pos.x, m.pos.z, m.pos.y + 0.5f, 0.5f, gy, gn)) m.pos.y += (gy + 1.0f - m.pos.y) * std::min(1.0f, 4.0f * dt);
        } else {
            // Seek: closest visible enemy within SearchRadius 2000 UU (disguised pawns ignored), HomingSpeed 2300 UU/s.
            const MatchOpponent* tgt = nullptr; float best = 20.0f;
            if (matchActive_)
                for (const MatchOpponent* o : opponents_) {
                    if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                    float d = core::length(o->pawn().actorLocation() - m.pos), tt;
                    if (d < best && !(line && line->segmentHit(m.pos, o->pawn().actorLocation(), tt))) { best = d; tgt = o; }
                }
            if (tgt) {
                m.vel = core::normalize(tgt->pawn().actorLocation() - m.pos) * 23.0f;
                const Character& e = tgt->pawn();
                const core::Vec3 d = e.actorLocation() - m.pos;
                if (std::hypot(d.x, d.z) <= e.cylinderRadius(e.moveForm()) + 0.3f && std::fabs(d.y) <= e.cylinderHalfHeight(e.moveForm()) + 0.3f) boom = true;
            } else m.vel = {0, 0, 0};
            const core::Vec3 next = m.pos + m.vel * dt;
            float tt;
            if (line && core::length(m.vel) > 0.0f && line->segmentHit(m.pos, next, tt)) boom = true;
            else m.pos = next;
        }
        if (boom) radiusDamage(m.pos, 125.0f, 5.0f, localPlayer_, "TransGame.TnDamageTypeKamikazeMine");
        if (boom || gone) { mines_.erase(mines_.begin() + (long)i); continue; }
        ++i;
    }
}

// ---- Systems M08d: per-form vehicle audio, weapon identity, projectiles, beams (Gameplay reports; Systems plays) ----

void World::tickVehicleAudio(float dt, const VehicleFormSignals& s) {
    VehicleAudio::Input in = vehicleForm_.translate(s, &vehicleEvents_);
    vehicleAudio_.tick(dt, in, cues_, [this] { return atPawn({0, 1.4725f, 0}); });
    // WFC_AUDIOCHECK: ownership audit every 30 steps - vehicle loops only while the vehicle form owns them.
    static const bool check = std::getenv("WFC_AUDIOCHECK") != nullptr;
    static int n = 0;
    if (check && ++n % 30 == 0) {
        const int loops = vehicleAudio_.liveLoops();
        LOG_INFO("AUDIOCHECK form=%s vehicle=%d entered=%d vehLoops=%d flight=%d beam=%d instances=%zu voices=%d%s",
                 audioProfile().vehicleForm.c_str(), (int)s.vehicle, (int)vehicleAudio_.entered(), loops, weaponAudio_.flightLoops(),
                 (int)weaponAudio_.beamActive(), cues_.liveInstances(), audio_ ? audio_->activeVoices() : -1,
                 (!s.vehicle && loops > 0) ? " LEAK" : "");
    }
}

// The cues of a weapon class (WeaponSounds, impact, projectile, mesh anims, the targets' hit sounds), level-owned, loaded
// once per level - at loadout time (preloadWeaponAudio) and, as a safety net, at its first fire / projectile.
void World::ensureWeaponAudio(const std::string& cls) {
    if (cls.empty() || !audio_ || levelAudio_.level().empty() || !weaponAudioLoaded_.insert(cls).second) return;
    CharacterAudio::loadWeaponCues(cues_, cls);
    CharacterAudio::loadHitCues(cues_, CharacterAudio::defaultProfile(), cls);
}

void World::preloadWeaponAudio(const std::vector<std::string>& classes) {
    loadoutWeaponClasses_ = classes;            // re-applied when a level's audio loads
    for (const std::string& c : classes) ensureWeaponAudio(c);
}

const std::string& World::firingWeaponClass(bool vehicleForm) const {
    return vehicleForm && !vehicleWeaponClass_.empty() ? vehicleWeaponClass_ : weaponClass_;
}

void World::setPlayerVehicleWeaponAudio(const std::string& weaponClass) {
    vehicleWeaponClass_ = weaponClass;
    ensureWeaponAudio(weaponClass);
}

int World::onWeaponFired(const std::string& weaponClass, bool lowAmmo, bool vehicleForm, const core::Vec3& muzzle) {
    // TnWeapon.PlayFiringSound for any fire type: the robot weapon from its MuzzleFlash socket; a vehicle weapon is the
    // vehicle's (owner-attached at the pawn's audio root) [HIGH: PlaySound on the weapon's owner].
    ensureWeaponAudio(weaponClass);
    SoundCues::Emitter e = vehicleForm ? atPawn({0, 1.4725f, 0}) : atWeapon("MuzzleFlash");
    if (!vehicleForm) e.pos = muzzle;
    return weaponAudio_.fire(cues_, weaponClass, lowAmmo, e, core::length(muzzle - player_.pawn().position()));
}

void World::onProjectileSpawned(int key, const std::string& weaponClass, const core::Vec3& pos) {
    ensureWeaponAudio(weaponClass);
    weaponAudio_.projectileSpawned(cues_, key, weaponClass, pos, core::length(pos - listenerPos_));
}

void World::onProjectileMoved(int key, const core::Vec3& pos) { weaponAudio_.projectileMoved(cues_, key, pos); }

void World::onProjectileExploded(int key, const std::string& weaponClass, const core::Vec3& pos) {
    weaponAudio_.projectileExploded(cues_, key, weaponClass, pos, core::length(pos - listenerPos_));
}

void World::onProjectileRemoved(int key) { weaponAudio_.projectileRemoved(cues_, key); }

void World::onProjectileHitWall(const std::string& weaponClass, const core::Vec3& pos, bool fuseStarted) {
    weaponAudio_.projectileHitWall(cues_, weaponClass, pos, core::length(pos - listenerPos_), fuseStarted);
}

void World::onBeamWeapon(const std::string& weaponClass, bool firing, int target) {
    weaponAudio_.beam(cues_, weaponClass, firing,
                      target == 1 ? WeaponAudio::BeamTarget::Friendly : target == 2 ? WeaponAudio::BeamTarget::Enemy
                                                                          : WeaponAudio::BeamTarget::None,
                      atWeapon("MuzzleFlash"));
}

// ---- Systems M08e: per-chassis vehicle FX through Rendering's runtime ----
float World::tickVehicleEffects(float dt, const VehicleFxDriver::Inputs& in) {
    if (!vehicleFxDriver_.bound()) return -1.0f;
    if (!vehicleFxData_) vehicleFxDriver_.setData(&audioProfile().vehicleFx);
    vehicleFxData_ = true;
    return vehicleFxDriver_.tick(dt, in);
}

// Energon Repair Ray [CONF TnWeaponRepair / TnWeaponBeam script + RepairBeam_WEPDATA]: every fire interval (0.1 s) the beam
// traces WeaponRange 3500 UU from the eye along the aim. A teammate hit is healed HealthPerSecond 60 x RepairRateModifier (no buffs:
// x1) x interval with TnHealTypeRepairTeam (no SegmentedHealType: across segments [HIGH]); any other pawn takes DamagePerSecond 60 x
// interval of TnDamageTypeRepairEnemy. HeatMax 0: no overheat [HIGH]. PlayerTargeting.GetRepairTarget lock-on assist (the trace is
// redirected to a picked teammate) is not recovered: the beam follows the crosshair [PARTIAL].
void World::fireRepairBeamImpl(const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn) {
    const core::Vec3 dir = core::normalize(dirIn);
    const float range = w.rangeM > 0.0f ? w.rangeM : 35.0f;
    const float tickSecs = w.fireInterval > 0.0f ? w.fireInterval : 0.1f;
    float best = range;
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    float t;
    if (line && line->segmentHit(origin, origin + dir * range, t)) best = range * t;
    MatchOpponent* hit = nullptr;
    for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(origin, dir, best, th) && th < best) { best = th; hit = o; } }
    repairBeam_.active = true; repairBeam_.time = tickSecs * 1.5f;
    repairBeam_.start = origin; repairBeam_.end = origin + dir * best; repairBeam_.target = hit ? hit->matchPlayer() : -1;
    repairBeam_.healing = false;
    if (!hit || !matchActive_) return;
    if (match_.sameTeam(hit->matchPlayer(), localPlayer_)) {
        repairBeam_.healing = true;
        hit->pawn().health().heal(Health::HealType::AddHealthToAll, 60.0f * tickSecs);   // HealDamage(RepairAmount, TnHealTypeRepairTeam)
    } else {
        applyMatchDamage(hit->matchPlayer(), localPlayer_, 60.0f * tickSecs, false, "TransGame.TnDamageTypeRepairEnemy");
    }
}

// ---- DEV / QA TOOLING (not original WFC; gated by WFC_QA=1) ----
bool World::qaEnabled() { static const bool on = std::getenv("WFC_QA") != nullptr; return on; }

std::vector<std::string> World::qaWeaponIds(bool vehicle) const {
    std::vector<std::string> out;
    if (!qaEnabled()) return out;
    for (int i = 0; i < weaponDefCount(); ++i) {
        const WeaponDef& d = weaponDefAt(i);
        if (!d.provider || !*d.provider) continue;
        if (vehicle ? d.typeCode == 3 : (d.typeCode != 3 && d.typeCode >= 0)) out.push_back(d.provider);
    }
    return out;
}

std::vector<std::string> World::qaSetLoadout(const std::vector<std::string>& ids) {
    if (!qaEnabled() || localPlayer_ < 0 || !matchActive_) return {};
    // The local selection becomes a custom loadout (as Create a Character would make it); restrictions apply through applyLoadout.
    CharacterSelection& sel = match_.playerMutable(localPlayer_).selection;
    sel.type = 0;
    sel.weapons = ids;
    std::vector<std::string> refused = applyLoadout(&sel);
    LOG_INFO("QA loadout: %zu weapon(s), %zu refused", ids.size(), refused.size());
    return refused;
}

void World::qaRespawn() {
    if (!qaEnabled() || !matchActive_) return;
    killLocalPlayer(localPlayer_, true);   // DmgType_Suicided: no score change; the match's own respawn wave brings the pawn back
}

void World::qaTeleportToStart(int index) { if (qaEnabled()) teleportToStart(index); }
void World::qaSetNoclip(bool on) { if (!qaEnabled()) return; qaNoclip_ = on; player_.controller().setQaNoclip(on); }
void World::qaSetGodMode(bool on) { if (qaEnabled()) qaGod_ = on; }

std::string World::qaStatus() const {
    if (!qaEnabled()) return "";
    const Character& pc = player_.pawn();
    char b[384];
    std::snprintf(b, sizeof b, "map %s | mode %s | body %s (%s) | form %s | weapon %s | pos %.1f %.1f %.1f | noclip %d god %d",
                  mapName_.c_str(), gameModeName(matchMode_), pc.chassis().id.c_str(), pc.specialty().c_str(),
                  pc.form() == Form::Vehicle ? "vehicle" : "robot", pc.weapon().def ? pc.weapon().def->provider : "-",
                  pc.position().x, pc.position().y, pc.position().z, (int)qaNoclip_, (int)qaGod_);
    return b;
}

// ---- Projectile FX (Rendering's particle API, agents/rendering 38c9ecf+: spawnParticleEffect / setParticleEffectTransform /
// stopParticleEffect). Detected at compile time so this file builds against a renderer interface without it. ----
namespace {
template <class R>
auto fxSpawn(R& r, const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u, int)
    -> decltype(r.spawnParticleEffect(t, p, f, u), int()) { return r.spawnParticleEffect(t, p, f, u); }
template <class R> int fxSpawn(R&, const std::string&, const core::Vec3&, const core::Vec3&, const core::Vec3&, long) { return -1; }
template <class R>
auto fxMove(R& r, int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u, int)
    -> decltype(r.setParticleEffectTransform(h, p, f, u), void()) { r.setParticleEffectTransform(h, p, f, u); }
template <class R> void fxMove(R&, int, const core::Vec3&, const core::Vec3&, const core::Vec3&, long) {}
template <class R> auto fxStop(R& r, int h, int) -> decltype(r.stopParticleEffect(h), void()) { r.stopParticleEffect(h); }
template <class R> void fxStop(R&, int, long) {}
template <class R> constexpr auto fxApi(int) -> decltype(std::declval<R&>().spawnParticleEffect(std::string(), core::Vec3{}, core::Vec3{}, core::Vec3{}), bool()) { return true; }
template <class R> constexpr bool fxApi(long) { return false; }
// UE3 rotator(dir) as a (forward, up) frame: up = world up unless the direction is near vertical.
void fxFrame(const core::Vec3& dirIn, core::Vec3& f, core::Vec3& u) {
    const float l = core::length(dirIn);
    f = l > 1e-5f ? dirIn * (1.0f / l) : core::Vec3{0, 0, -1};
    const core::Vec3 ref = std::fabs(f.y) > 0.99f ? core::Vec3{0, 0, -1} : core::Vec3{0, 1, 0};
    const core::Vec3 rgt = core::normalize(core::cross(f, ref));
    u = core::normalize(core::cross(rgt, f));
}
}  // namespace

bool World::projectileFxApi() { return fxApi<render::IRenderer>(0); }

void World::loadProjectileVisuals(const std::string& root, const std::function<void(std::vector<render::Material>&)>& resolveTextures) {
    projVisuals_.clear();
    int meshes = 0;
    for (int i = 0; i < weaponDefCount(); ++i) {
        const WeaponDef& d = weaponDefAt(i);
        if (!d.id || projectileVisualFor(d.id) >= 0) continue;
        // The folder is the provider id; try the class id first (most match), then the provider; the file must be this class.
        std::string txt; assets::Json j; bool found = false;
        for (const char* dir : {d.id, d.provider}) {
            if (!dir || !*dir || !readTextFile(root + "/Weapons/" + dir + "/weapon.json", txt) || !assets::Json::parse(txt, j)) continue;
            const std::string cls = j["class"].asString();
            if (cls.size() >= std::strlen(d.id) && cls.compare(cls.size() - std::strlen(d.id), std::string::npos, d.id) == 0) { found = true; break; }
        }
        if (!found) continue;
        const assets::Json& ps = j["projectiles"];
        if (ps.size() == 0) continue;
        // The first projectile class (PlasmaCannon: Charge1; the charge levels are not simulated) [PARTIAL for PlasmaCannon].
        const assets::Json& v = ps[(size_t)0]["projectile_visual"];
        ProjectileVisual pv;
        pv.weapon = d.id;
        pv.flight = v["flight_effect"]["template"].asString();
        pv.explosion = v["explosion_effect"]["template"].asString();
        const std::string gltf = ps[(size_t)0]["body_mesh"]["gltf"].asString();
        if (!gltf.empty() && renderer_) {
            render::MeshData md;
            if (assets::loadGlb(root + "/../" + gltf, md)) {
                resolveTextures(md.mats);
                pv.body = renderer_->uploadMesh(md);
                if (pv.body != render::kInvalidMesh) ++meshes;
            }
        }
        if (pv.flight.empty() && pv.explosion.empty() && pv.body == render::kInvalidMesh) continue;
        projVisuals_.push_back(pv);
    }
    LOG_INFO("projectile visuals: %zu weapons (%d body meshes), renderer particle API %s", projVisuals_.size(), meshes,
             projectileFxApi() ? "present" : "absent (box marker fallback)");
}

int World::projectileVisualFor(const char* weaponId) const {
    if (!weaponId) return -1;
    for (size_t i = 0; i < projVisuals_.size(); ++i) if (projVisuals_[i].weapon == weaponId) return (int)i;
    return -1;
}

void World::projectileFxStart(Projectile& p) {
    p.fxHandle = -1;
    if (!renderer_ || p.visual < 0 || projVisuals_[(size_t)p.visual].flight.empty()) return;
    core::Vec3 f, u; fxFrame(p.vel, f, u);
    p.fxHandle = fxSpawn(*renderer_, projVisuals_[(size_t)p.visual].flight, p.pos, f, u, 0);
    if (p.fxHandle >= 0) ++projectileFxSpawned_;
}

void World::projectileFxMove(const Projectile& p) {
    if (!renderer_ || p.fxHandle < 0) return;
    core::Vec3 f, u;
    // A resting grenade keeps its last orientation (zero velocity).
    if (core::dot(p.vel, p.vel) < 1e-8f) return;
    fxFrame(p.vel, f, u);
    fxMove(*renderer_, p.fxHandle, p.pos, f, u, 0);
}

void World::projectileFxEnd(Projectile& p, const core::Vec3& at, const core::Vec3& normal, bool explode) {
    if (!renderer_) return;
    if (p.fxHandle >= 0) { fxStop(*renderer_, p.fxHandle, 0); p.fxHandle = -1; }   // trails finish their lifetime
    if (!explode || p.visual < 0 || projVisuals_[(size_t)p.visual].explosion.empty()) return;
    core::Vec3 f, u; fxFrame(normal, f, u);
    if (fxSpawn(*renderer_, projVisuals_[(size_t)p.visual].explosion, at, f, u, 0) >= 0) ++projectileFxExplosions_;
}

} // namespace game
