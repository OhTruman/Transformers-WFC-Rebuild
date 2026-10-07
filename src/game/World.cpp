#include "core/SimRandom.h"
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
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace game {

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

// Rendering's IRenderer::prewarmDynamicMesh (agents/rendering M53), detected at compile time like the particle API.
template <class R> auto fxPrewarm(R& r, const render::MeshData& md, int) -> decltype(r.prewarmDynamicMesh(md), void()) { r.prewarmDynamicMesh(md); }
template <class R> void fxPrewarm(R&, const render::MeshData&, long) {}
// Rendering's per-draw material parameters (agents/rendering M70), detected at compile time.
template <class R> auto fxSetDrawParam(R& r, const char* n, const float* v, int) -> decltype(r.setDrawMaterialParam(std::string(n), v), void()) { r.setDrawMaterialParam(std::string(n), v); }
template <class R> void fxSetDrawParam(R&, const char*, const float*, long) {}
template <class R> auto fxClearDrawParam(R& r, const char* n, int) -> decltype(r.clearDrawMaterialParam(std::string(n)), void()) { r.clearDrawMaterialParam(std::string(n)); }
template <class R> void fxClearDrawParam(R&, const char*, long) {}
// Rendering's beam segment API (agents/rendering: spawnParticleEffectSegment / setParticleEffectSegment), compile-time detected.
template <class R> auto fxSpawnSegment(R& r, const std::string& t, const core::Vec3& a, const core::Vec3& b, int) -> decltype(r.spawnParticleEffectSegment(t, a, b), int()) { return r.spawnParticleEffectSegment(t, a, b); }
template <class R> int fxSpawnSegment(R&, const std::string&, const core::Vec3&, const core::Vec3&, long) { return -1; }
template <class R> auto fxSetSegment(R& r, int h, const core::Vec3& a, const core::Vec3& b, int) -> decltype(r.setParticleEffectSegment(h, a, b), void()) { r.setParticleEffectSegment(h, a, b); }
template <class R> void fxSetSegment(R&, int, const core::Vec3&, const core::Vec3&, long) {}
template <class R> auto fxStopEffect(R& r, int h, int) -> decltype(r.stopParticleEffect(h), void()) { r.stopParticleEffect(h); }
template <class R> void fxStopEffect(R&, int, long) {}
template <class R> auto fxSpawnPoint(R& r, const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u, int) -> decltype(r.spawnParticleEffect(t, p, f, u), int()) { return r.spawnParticleEffect(t, p, f, u); }
template <class R> int fxSpawnPoint(R&, const std::string&, const core::Vec3&, const core::Vec3&, const core::Vec3&, long) { return -1; }

// WFC_SPAWNPROF: millisecond timings of the spawn path / slow World steps (diagnostics, no behaviour change).
static bool spawnProf() { static const bool on = std::getenv("WFC_SPAWNPROF") != nullptr; return on; }
static double profNowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void World::load(render::IRenderer& renderer) {
    Character::clearRigCache();   // rigs point at models of the previous load
    repairBeamHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) { fireRepairBeamImpl(w, o, d); };
    heldWeaponMuzzleHook = [this](core::Vec3& out) { return heldWeaponMuzzleImpl(out); };
    weaponFireHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) {
        if (w.projectile()) {
            spawnProjectile(o, d * w.projSpeed, w, localPlayer_);   // Spawn at RealStartLoc (callers pass the muzzle)
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
    // The match's authored rule classes gate rule-dependent presentation (objective bases, Conquest totems,
    // objective-factory effects) exactly as GameInfo.HasRule gates the world state.
    renderer.setActiveGameRules(gameRulesForMode(matchMode_));

    renderer_ = &renderer;
    bool okMap = assets::loadGlb(mapDir() + "world.glb", mapMesh);
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
    fx_.load(renderer, root + "/../content/");
    fx_.loadMeshes(renderer, root + "/../content/");
    vehicleFx_.load(renderer, root + "/../content/");

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
    auto splitMovers = [](const render::MeshData& in, render::MeshData& out,
                          std::vector<std::pair<std::string, std::vector<core::Vec3>>>& moverTris) {
        std::vector<std::string> names = MapState::moverActorNames();
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
    if (!colMesh.empty()) {
        splitMovers(colMesh, pawnStatic, pawnMovers);
        collision_.build(pawnStatic);
        if (assets::loadGlb(mapDir + "collision_weapon.glb", weaponColMesh)) {
            splitMovers(weaponColMesh, weaponStatic, weaponMovers);
            weaponCollision_.build(weaponStatic);
        }
        mapState_.registerCollision(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, pawnMovers, weaponMovers);
        LOG_INFO("collision: movement %s, weapon %s", authored ? "collision_pawn.glb" : "collision.glb (fallback)",
                 weaponCollision_.valid() ? "collision_weapon.glb" : "movement world");
        renderer.setVisibilityQuery([this](const core::Vec3& a, const core::Vec3& b) {
            // March in short pieces: segmentHit scans every grid cell in the segment's AABB,
            // which is prohibitive for long light-visibility rays.
            core::Vec3 d = b - a;
            float len = core::length(d);
            int n = std::max(1, (int)std::ceil(len / 2.0f));
            float t;
            const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;   // zero-extent line checks
            for (int i = 0; i < n; ++i)
                if (lineWorld.segmentHit(a + d * ((float)i / n), a + d * ((float)(i + 1) / n), t)) return true;
            return false;
        });
        // KillZ of the persistent level's WorldInfo (physics.json world[<Map>_BASE_m].KillZ) [CONF authored]: Streets -75000,
        // Gorge -7500, Debris +10000 UU ... Streets' value stays the fallback.
        killZ_ = -750.0f;
        {
            std::string txt;
            assets::Json pj;
            if (readTextFile(mapDir + "physics.json", txt) && assets::Json::parse(txt, pj)) {
                for (const auto& kv : pj["world"].obj) {
                    std::string k = kv.first; for (char& ch : k) ch = (char)std::tolower((unsigned char)ch);
                    if (k.size() > 7 && k.compare(k.size() - 7, 7, "_base_m") == 0 && kv.second.has("KillZ")) killZ_ = kv.second["KillZ"].asFloat() * 0.01f;
                }
            }
            LOG_INFO("map %s: KillZ %.1f m", mapName_.c_str(), killZ_);
        }
        loadHazards();
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

    // Authored Streets pickup factories and destructibles (AssetTools 7a69756), plus the weapon-test dummy.
    actors_.clear();
    loadPickupFactories(mapDir + "gameplay.json");
    loadDestructibles(mapDir + "physics.json", root + "/../content/");
    // Weapon-test dummy (DamageTarget): test instrumentation, not WFC content — only with WFC_TESTDUMMY=1.
    if (std::getenv("WFC_TESTDUMMY")) {
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
    struct ProfScope { double t0 = profNowMs(); ~ProfScope() { const double ms = profNowMs() - t0; if (spawnProf() && ms > 8.0) LOG_INFO("SPAWNPROF World::handleInput %.1f ms", ms); } } profScope;
    player_.controller().handleInput(in, dt);   // Dash (Shift) is latched by PlayerController
    if (in.wasPressed(platform::Button::DebugNextStart)) teleportToStart(startCursor_ + 1);   // test spawn cycling
    if (in.wasPressed(platform::Button::DebugPrevStart)) teleportToStart(startCursor_ - 1);
}

bool rayAabb(const core::Vec3& o, const core::Vec3& d, float len,
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
    const float range = w.rangeM;   // [CONF] 300 m

    // Apply per-shot spread as a small random cone around the aim direction.
    core::Vec3 dir = core::normalize(dirIn);
    {
        core::Vec3 up{0, 1, 0};
        core::Vec3 rt = core::normalize(core::cross(dir, up));
        core::Vec3 u2 = core::normalize(core::cross(rt, dir));
        auto rf = [] { return core::simRandSigned(); };   // simulation stream (not shared with FX)
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
    // impact squib where the trace hit something (world or target).
    // WeaponFx reproduces the Ion Blaster's cooked particle systems only; other weapons' muzzle / tracer / squib templates
    // are exposed (HudGameState / WeaponDef) for Rendering instead of drawing the Ion Blaster's [PARTIAL].
    const bool ionFx = !w.def || std::string(w.def->id) == "IonBlaster";
    if (ionFx && weaponSocketWorld("MuzzleFlash", ms)) fx_.spawnMuzzleFlash(ms);
    if (ionFx) fx_.spawnTracer(muzzle, hitPoint);
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
    cues_.play(w.lowAmmo() ? "SHOOT_LOW_AMMO" : "SHOOT", muzzle, ownDist);
    burstActive_ = true; sinceShot_ = 0.0f;
    // DefaultImpactSound (world) / damage impact cue at the hit point.
    if (hitTarget) cues_.play("IMPT_DMG", hitPoint, core::length(hitPoint - listenerPos_));
    else if (dist < range - 0.01f) cues_.play("IMPT_WORLD", hitPoint, core::length(hitPoint - listenerPos_));
}

void World::setAudio(audio::IAudio* a) {
    audio_ = a;
    if (!a) return;
    const std::string base = assetRoot() + "/../content/";
    // Weapon audio = the original SoundCues (fire/tail/low-ammo, reload + idle notifies, impacts).
    cues_.load(a, base);
    // [PROV] non-weapon placeholders (transform/land cues not yet recovered).
    sndTransform_ = a->load(base + "WL_EVENT_IACON/EVENT_IACON_BRIDGE_TRANSFORM_GEARS.wav");
    sndLand_      = a->load(base + "WL_GUN_FOLEY/RELOAD_AIR_RELEASE_THUMP.wav");
    LOG_INFO("audio: transform=%d land=%d", sndTransform_, sndLand_);
}

void World::playSfx(Sfx s, const core::Vec3& pos) {
    if (!audio_) return;
    // refDist/maxDist in metres [PROV] — exact SoundCue attenuation radii not yet extracted.
    switch (s) {
        case Sfx::Fire:      cues_.play("SHOOT", pos, 0.0f); break;
        case Sfx::Reload:    break;   // driven by the reload animation's AnimNotifies
        case Sfx::Transform: audio_->playAt(sndTransform_, pos, 0.9f, 10.0f, 90.0f); break;
        case Sfx::Land:      audio_->playAt(sndLand_, pos, 0.8f, 5.0f, 50.0f); break;
    }
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
    bool hover = vehicle && !boost;

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
    tickEngineAudio(dt, vehicle, boost, grounded, tookOff, landed);
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
        core::Vec3 ap0 = pc.position() + core::Vec3{0, 1.4725f, 0};
        float mph0 = core::length(pc.velocity()) * 2.23694f;
        cues_.play("VEH_OPTIMUS_RAM_NITRO_START", ap0, 0.0f, mph0);   // NitroSound Auto_Ram_Nitro
        cues_.play("VEH_TRUCK_RAM_ALERT", ap0, 0.0f, mph0);           // CustomLoopingSound Auto_Ram_Alert [MED: once]
    } else if (ne == VehicleNitro::Event::Stopped) {
        vehicleFx_.deactivate(ramInst_);
        ramInst_ = -1;
    }
    vehicleFx_.tick(dt);

    // Boost audio at the AUDIO_ROOT socket (C_Reference_XR + 147.25 UU up); speed parameter in mph.
    const core::Vec3& v = pc.velocity();
    float mph = core::length(v) * 2.23694f;
    core::Vec3 ap = pc.position() + core::Vec3{0, 1.4725f, 0};
    if (boost && !boostActive_) {
        cues_.play("VEH_OPTIMUS_BOOST_START", ap, 0.0f, mph);
        boostLoopCue_ = cues_.play("VEH_OPTIMUS_BOOST_LOOP", ap, 0.0f, mph);
        boostAge_ = 0.0f; boostWheelsChecked_ = false;
    } else if (!boost && boostActive_) {
        cues_.stop(boostLoopCue_, 0.15f);            // BoostFadeOutTime
        boostLoopCue_ = -1;
        cues_.play("VEH_OPTIMUS_BOOST_END", ap, 0.0f, mph);
    }
    if (boost) {
        boostAge_ += dt;
        cues_.update(boostLoopCue_, ap, mph);
        if (!boostWheelsChecked_ && boostAge_ >= 0.27f) {   // BoostWheelsGroundCheckDelay
            boostWheelsChecked_ = true;
            if (pc.onGround()) cues_.play("VEH_OPTIMUS_BOOST_WHEELS", ap, 0.0f, mph);
        }
    }
    boostActive_ = boost;

    if (std::getenv("WFC_BOOSTLOG")) {
        static int n = 0;
        if (n % 6 == 5 && pc.form() == Form::Vehicle)
            LOG_INFO("NITRO active=%d remaining=%.2f cooldown=%.2f speedScale=%.1f steeringScale=%.1f",
                     (int)nitro_.nitroActive(), pc.vehicleState().nitroRemain, pc.vehicleState().nitroCooldown,
                     nitro_.speedScale(), nitro_.steeringScale());
        if (++n % 6 == 0 && pc.form() == Form::Vehicle) {
            LOG_INFO("VFX boost=%d hover=%d jumps=%d parts=%zu mph=%.1f ground=%d vy=%.2f sockets=%d/%d",
                     (int)boost, (int)hover, jumpCount_, vehicleFx_.liveParticles(), mph, (int)grounded,
                     v.y, haveSockets, (int)VehicleFx::kSocketCount);
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

// Engine audio. [CONF] OptimusTruckForm.HmPlayerVehicleAudioComponent_6670 + Veh_Optimus_Prime_SoundSet:
//   DriveSounds: gear MaxSpeed 20 and 110, both OnLoadLoops Auto_Engine_Gear_1_OnLoad -> VEH_OPTIMUS_DRIVE_ONLOAD,
//   OffLoadLoops Auto_Engine_Gear_1_OffLoad -> VEH_OPTIMUS_DRIVE_OFFLOAD (one-shots map to None); ReverseSound
//   maps to the same two cues; JumpRevSounds UseJumpRev, Auto_Jump_Loop -> VEH_OPTIMUS_DRIVE_JUMP_LOOP;
//   AscendSound Auto_Jump_Start -> VEH_OPTIMUS_DRIVE_JUMP_START; EngineFadeOutTime 0.2 s;
//   HoverLandSound {0.15 s air: HOVER_LAND_LIGHT, 2.0 s: HOVER_LAND_HEAVY}; BoostLandSound {0.15 s:
//   WHEELS_LAND_LIGHT, 2.0 s: WHEELS_LAND_HEAVY}; speed parameter Optimus_Prime_Speed (mph).
// [MED] on-load = throttle input held; the engine loop yields to the boost loop while boosting (the boost
// cue carries its own engine layers); airborne = jump-rev loop.
void World::tickEngineAudio(float dt, bool vehicle, bool boost, bool grounded, bool tookOff, bool landed) {
    Character& pc = player_.pawn();
    float mph = core::length(pc.velocity()) * 2.23694f;
    core::Vec3 ap = pc.position() + core::Vec3{0, 1.4725f, 0};   // AUDIO_ROOT socket
    EngineState want = EngineState::Off;
    if (vehicle) {
        if (!grounded) want = EngineState::JumpRev;
        else if (boost) want = EngineState::Boost;
        else want = player_.controller().throttleHeld() ? EngineState::OnLoad : EngineState::OffLoad;
    }
    if (want != engineState_) {
        if (engineCue_ >= 0) cues_.stop(engineCue_, 0.2f);   // EngineFadeOutTime
        engineCue_ = -1;
        const char* cue = want == EngineState::OnLoad ? "VEH_OPTIMUS_DRIVE_ONLOAD"
                        : want == EngineState::OffLoad ? "VEH_OPTIMUS_DRIVE_OFFLOAD"
                        : want == EngineState::JumpRev ? "VEH_OPTIMUS_DRIVE_JUMP_LOOP" : nullptr;
        if (cue) engineCue_ = cues_.play(cue, ap, 0.0f, mph);
        engineState_ = want;
    }
    if (engineCue_ >= 0) cues_.update(engineCue_, ap, mph);

    if (tookOff) cues_.play("VEH_OPTIMUS_DRIVE_JUMP_START", ap, 0.0f, mph);   // AscendSound
    if (vehicle && !grounded) airTime_ += dt;
    if (landed) {
        const char* cue = nullptr;
        if (airTime_ >= 2.0f) cue = boost ? "VEH_OPTIMUS_WHEELS_LAND_HEAVY" : "VEH_OPTIMUS_HOVER_LAND_HEAVY";
        else if (airTime_ >= 0.15f) cue = boost ? "VEH_OPTIMUS_WHEELS_LAND_LIGHT" : "VEH_OPTIMUS_HOVER_LAND_LIGHT";
        if (cue) cues_.play(cue, ap, 0.0f, mph);
    }
    if (grounded || !vehicle) airTime_ = 0.0f;
}

// Pawn blocking [CONF RE addendum 11]: robot form = the actor cylinder with collide + block actors (stock bBlockActors; allies block
// like enemies; per-chassis CollisionRadius / Height); vehicle form = the vehicle mesh rigid body (cylinder disabled), contact by
// physics (+ Rammed). The native sweep / rigid-body contact is not reproduced: overlaps are pushed apart after movement, a push that
// would enter world geometry being given to the other pawn [PROV method]. Robot-robot: cylinders, half each. Robot-vehicle: the robot
// cylinder out of the vehicle's mesh box (meshMatrix orientation, horizontal), the robot moving (the vehicle when the robot is
// against a wall). Vehicle-vehicle: inscribed circles of the boxes [PROV]. Ram damage / reactions come first (gameplayRamContacts).
// Bots yield to the local player (the human moves only when the bot cannot), so a crowd never shoves or pins the human.
void World::separatePawns() {
    if (!matchActive_) return;
    struct P { Character* c; };
    std::vector<P> ps;
    ps.reserve(opponents_.size() + 1);
    if (!localDead_) ps.push_back({&player_.pawn()});
    for (MatchOpponent* o : opponents_) if (o->spawned()) ps.push_back({&o->pawn()});
    const CollisionWorld* col = collision_.valid() ? &collision_ : nullptr;
    auto tryMove = [&](Character& c, const core::Vec3& d) {
        const core::Vec3 a = c.position() + core::Vec3{0, 1.0f, 0};
        float t;
        if (col && col->segmentHit(a, a + d * 1.5f, t)) return false;
        c.setPosition(c.position() + d);
        return true;
    };
    for (int pass = 0; pass < 2; ++pass)   // a second pass settles pushes into third pawns
    for (size_t i = 0; i < ps.size(); ++i)
        for (size_t j = i + 1; j < ps.size(); ++j) {
            Character& A = *ps[i].c; Character& B = *ps[j].c;
            const core::Vec3 pa = A.position(), pb = B.position();
            if (std::fabs(pa.y - pb.y) >= A.cylinderHalfHeight(A.moveForm()) + B.cylinderHalfHeight(B.moveForm())) continue;
            const bool va = A.moveForm() == Form::Vehicle, vb = B.moveForm() == Form::Vehicle;
            const bool humanA = &A == &player_.pawn();   // the local pawn is only ever ps[0]
            if (va != vb) {   // robot cylinder vs vehicle mesh box
                Character& V = va ? A : B; Character& Rb = va ? B : A;
                core::Vec3 mn, mx;
                if (!V.vehicleBoundsXZ(mn, mx)) continue;
                const core::Mat4 m = V.meshMatrix(Form::Vehicle);
                const core::Vec3 o{m.m[12], m.m[13], m.m[14]};
                core::Vec3 fx{m.m[0], 0.0f, m.m[2]}, fz{m.m[8], 0.0f, m.m[10]};
                fx = core::normalize(fx); fz = core::normalize(fz);
                const core::Vec3 rp = Rb.position() - o;
                const float lx = core::dot(rp, fx), lz = core::dot(rp, fz);
                const float rr = Rb.cylinderRadius(Form::Robot);
                const float cx = std::clamp(lx, mn.x, mx.x), cz = std::clamp(lz, mn.z, mx.z);
                float nx = lx - cx, nz = lz - cz, dd = std::sqrt(nx * nx + nz * nz), pen;
                if (dd > 1e-4f) { if (dd >= rr) continue; nx /= dd; nz /= dd; pen = rr - dd; }
                else {   // centre inside the box: out through the nearest face
                    const float ex[4] = {mx.x - lx, lx - mn.x, mx.z - lz, lz - mn.z};
                    int k = 0; for (int q = 1; q < 4; ++q) if (ex[q] < ex[k]) k = q;
                    nx = k == 0 ? 1.0f : k == 1 ? -1.0f : 0.0f; nz = k == 2 ? 1.0f : k == 3 ? -1.0f : 0.0f;
                    pen = ex[k] + rr;
                }
                const core::Vec3 n = fx * nx + fz * nz;   // world direction from the box toward the robot
                const bool humanIsRobot = &Rb == &player_.pawn(), humanIsVehicle = &V == &player_.pawn();
                if (humanIsRobot) { if (!tryMove(V, n * -pen)) tryMove(Rb, n * pen); continue; }   // the bot vehicle yields to the human
                if (humanIsVehicle) { if (!tryMove(Rb, n * pen)) tryMove(V, n * -pen); continue; }
                if (!tryMove(Rb, n * pen)) tryMove(V, n * -pen);
                continue;
            }
            auto inscribed = [](const Character& c) {
                core::Vec3 mn, mx;
                if (!c.vehicleBoundsXZ(mn, mx)) return c.cylinderRadius(Form::Vehicle);
                return 0.5f * std::min(mx.x - mn.x, mx.z - mn.z);
            };
            const float ra = va ? inscribed(A) : A.cylinderRadius(Form::Robot), rb = vb ? inscribed(B) : B.cylinderRadius(Form::Robot);
            float dx = pb.x - pa.x, dz = pb.z - pa.z;
            float d = std::sqrt(dx * dx + dz * dz);
            const float need = ra + rb;
            if (d >= need) continue;
            core::Vec3 n;
            if (d < 1e-3f) { const float a = 2.399963f * (float)(i * 31 + j); n = {std::cos(a), 0.0f, std::sin(a)}; }   // coincident: a fixed spread
            else n = {dx / d, 0.0f, dz / d};
            const float push = need - d;
            const core::Vec3 half = n * (push * 0.5f);
            if (humanA) { if (!tryMove(B, half * 2.0f)) tryMove(A, half * -2.0f); continue; }
            const bool okA = tryMove(A, half * -1.0f), okB = tryMove(B, half);
            if (!okA && okB) tryMove(B, half);
            else if (okA && !okB) tryMove(A, half * -1.0f);
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
    cues_.play("VEH_TRUCK_RAM_IMPACT", pos, core::length(pos - listenerPos_));   // RamSound Auto_Ram_Impact
    return true;
}

bool World::heldWeaponMuzzleImpl(core::Vec3& out) const {
    const Weapon& w = player_.pawn().weapon();
    if (shownWeapon_ != (w.def ? w.def->id : "IonBlaster")) return false;
    core::Mat4 ms;
    if (!weaponSocketWorld("MuzzleFlash", ms)) return false;
    out = {ms.m[12], ms.m[13], ms.m[14]};
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
        if (renderer_) { render::MeshData md; md.subs = m->subs; md.mats = m->mats; fxPrewarm(*renderer_, md, 0); }
    } else LOG_ERROR("weapon %s: mesh %s unavailable", d.id, d.meshGltf ? d.meshGltf : "-");
    const assets::SkinnedModel* raw = ok ? m.get() : nullptr;
    weaponModels_[d.id] = std::move(m);
    return raw;
}

// The weapon mesh drawn at the socket is the ACTIVE inventory weapon's (no other weapon may be shown in its place).
double World::profileWeaponModelLoad(const WeaponDef& d) { const double t0 = profNowMs(); weaponModelFor(d); return profNowMs() - t0; }

void World::preloadSelections(const std::vector<CharacterSelection>& selections) {
    const int fa = localPlayer_ >= 0 ? match_.faction(localPlayer_) : 0;
    for (const CharacterSelection& sel : selections) {
        const ChassisAssets* ca = chassisAssets(resolveChassis(sel, fa));
        if (sel.type == 0 && !sel.weapons.empty()) preloadHeldWeaponModels(sel.weapons);
        else if (ca) preloadHeldWeaponModels(ca->def.iconicWeapons);
    }
}

void World::preloadHeldWeaponModels(const std::vector<std::string>& weapons) {
    for (const std::string& n : weapons) {
        const WeaponDef* d = findWeaponDef(n);
        // Only weapons that can be the held weapon (primary / heavy) and draw a mesh; the Ion Blaster uses the boot model.
        if (!d || (d->typeCode != 0 && d->typeCode != 1) || !d->meshGltf || !*d->meshGltf || std::string(d->id) == "IonBlaster") continue;
        weaponModelFor(*d);
    }
}

void World::tickParticipantWeapons(float dt) {
    // Held weapon views follow each participant's active robot weapon (model swap on a switch, fire / reload event anims).
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned()) continue;
        ParticipantWeaponView& v = partWeapons_[o->matchPlayer()];
        const Weapon& w = o->pawn().weapon();
        const std::string id = w.def ? w.def->id : "IonBlaster";
        if (id != v.id) {
            v.id = id;
            if (id == "IonBlaster") v.anim.setModel(weaponModel_.valid() ? &weaponModel_ : nullptr);
            else if (const assets::SkinnedModel* m = w.def ? weaponModelFor(*w.def) : nullptr) v.anim.setModelGeneric(m, *w.def);
            else v.anim.setModel(nullptr);
            v.seenShot = w.shotSerial; v.seenReload = w.reloadSerial;
        }
        if (w.reloadSerial != v.seenReload) { v.seenReload = w.reloadSerial; v.anim.play(WeaponMesh::Event::Reload); }
        if (w.shotSerial != v.seenShot) { v.seenShot = w.shotSerial; v.anim.play(WeaponMesh::Event::Fire); }
        std::vector<WeaponNotify> notifies;   // participant weapon notifies (shells / magazines) are not presented [PARTIAL]
        v.anim.tick(dt, notifies);
    }
    // This step's participant shots: the muzzle socket of the shooter's shown weapon (else its eye frame along the shot).
    for (const ParticipantShot& s : participantShots_) {
        if (s.weapon == "RepairRay") continue;   // the beam has its own looping presentation [PARTIAL for bots]
        core::Mat4 muzzle = core::Mat4::identity();
        bool have = false;
        for (const MatchOpponent* o : opponents_) {
            if (o->matchPlayer() != s.player || !o->spawned()) continue;
            auto it = partWeapons_.find(s.player);
            core::Mat4 local;
            if (o->pawn().hasWeapon() && it != partWeapons_.end() && it->second.anim.valid() && it->second.anim.socketLocal("MuzzleFlash", local)) {
                muzzle = o->pawn().weaponWorld() * local; have = true;
            }
        }
        if (!have) {
            const core::Vec3 f = core::normalize(s.to - s.from);
            const core::Vec3 rt = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            const core::Vec3 up = core::cross(rt, f);
            muzzle.m[0] = f.x; muzzle.m[1] = f.y; muzzle.m[2] = f.z; muzzle.m[4] = up.x; muzzle.m[5] = up.y; muzzle.m[6] = up.z;
            muzzle.m[8] = rt.x; muzzle.m[9] = rt.y; muzzle.m[10] = rt.z; muzzle.m[12] = s.from.x; muzzle.m[13] = s.from.y; muzzle.m[14] = s.from.z;
        }
        if (participantShotFxHook) participantShotFxHook(s, muzzle);
        else if (partShotFx_.size() < 256) {
            const WeaponDef* d = findWeaponDef(s.weapon);
            partShotFx_.push_back({s.weapon, muzzle, s.to, !(d && d->projSpeed > 0.0f)});   // projectiles draw their own flight effect
        }
    }
}

void World::syncShownWeapon() {
    const Weapon& w = player_.pawn().weapon();
    std::string id = w.def ? w.def->id : "IonBlaster";
    if (id == shownWeapon_) return;
    struct ProfScope { const std::string& id; double t0 = profNowMs(); ~ProfScope() { if (spawnProf()) LOG_INFO("SPAWNPROF syncShownWeapon %s: %.1f ms", id.c_str(), profNowMs() - t0); } } profScope{id};
    shownWeapon_ = id;
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
    const double t0 = profNowMs();
    pc.setLoadout(robot, veh);
    const double t1 = profNowMs();
    // TnCharacterApplier.ApplyAbilities: CharacterData.Abilities (custom selection, else the iconic preset).
    pc.setAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities);
    const double t2 = profNowMs();
    syncShownWeapon();
    if (spawnProf()) LOG_INFO("SPAWNPROF applyLoadout: setLoadout %.1f ms, setAbilities %.1f ms, syncShownWeapon %.1f ms", t1 - t0, t2 - t1, profNowMs() - t2);
    return refused;
}

void World::tickWeaponPresentation(float dt) {
    if (player_.pawn().weaponChangeSerial() != seenWeaponChange_) { seenWeaponChange_ = player_.pawn().weaponChangeSerial(); syncShownWeapon(); }
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
        // HmAnimNotify_Sound plays its cue at the weapon mesh (owned by the local player).
        const core::Mat4& wm = player_.pawn().weaponWorld();
        core::Vec3 p{wm.m[12], wm.m[13], wm.m[14]};
        const char* name = n.what.c_str();
        const char* dot = std::strrchr(name, '.');
        if (n.what.rfind("BL_WPN_GUN_ION_BLASTER.", 0) == 0 && dot) name = dot + 1;
        cues_.play(name, p, core::length(p - player_.pawn().position()));
    }
}

static const char* gameplayEventName(GameplayEventType t) {
    switch (t) {
    case GameplayEventType::MatchStart: return "MatchStart"; case GameplayEventType::Spawn: return "Spawn";
    case GameplayEventType::CharacterSelected: return "CharacterSelected"; case GameplayEventType::Kill: return "Kill";
    case GameplayEventType::Suicide: return "Suicide"; case GameplayEventType::EnvironmentDeath: return "EnvironmentDeath";
    case GameplayEventType::Assist: return "Assist"; case GameplayEventType::KillstreakEarned: return "KillstreakEarned";
    case GameplayEventType::Objective: return "Objective"; case GameplayEventType::MatchEnd: return "MatchEnd";
    }
    return "?";
}

void World::setRenderAlpha(float a) {
    player_.pawn().setRenderAlpha(a);
    for (MatchOpponent* o : opponents_) o->pawn().setRenderAlpha(a);
}

namespace {
// WFC_TICKPROF (diagnostics): accumulated ms per World::tick phase, logged every 300 steps.
struct TickProf {
    static bool on() { static const bool o = std::getenv("WFC_TICKPROF") != nullptr; return o; }
    double acc[12] = {}; long n = 0;
    const char* names[12] = {"pre", "abilities", "match", "bots", "oppMove", "oppAnim", "partWeapons", "pawn+camera", "weaponFx", "projectiles", "actors", "rest"};
};
TickProf& tickProf() { static TickProf p; return p; }
struct TickTimer { int slot; double t0; TickTimer(int s) : slot(s), t0(TickProf::on() ? profNowMs() : 0.0) {} ~TickTimer() { if (TickProf::on()) tickProf().acc[slot] += profNowMs() - t0; } };
}

void World::tick(float dt) {
    if (TickProf::on() && ++tickProf().n % 300 == 0) {
        std::string line;
        for (int i = 0; i < 12; ++i) { char b[48]; std::snprintf(b, sizeof b, " %s %.2f", tickProf().names[i], tickProf().acc[i] / 300.0); line += b; tickProf().acc[i] = 0.0; }
        LOG_INFO("TICKPROF ms/step (%zu participants):%s", match_.players().size(), line.c_str());
    }
    // Presentation interpolation: remember each pawn's state at the start of the step.
    player_.pawn().beginStep();
    for (MatchOpponent* o : opponents_) o->pawn().beginStep();
    // WFC_EVENTLOG (diagnostics): each authoritative gameplay event once, with its main context.
    {
        static const bool evlog = std::getenv("WFC_EVENTLOG") != nullptr;
        if (evlog)
            for (const GameplayEvent& e : match_.gameplayEvents()) {
                if (e.serial <= eventLogSerial_) continue;
                eventLogSerial_ = e.serial;
                LOG_INFO("EVENT #%u t=%.2f %s inst %d (%s %s%s streak %d) victim %d (%s%s streak %d) dmg %s weapon %s melee %d ability %d dist %.0f UU obj %s score +%d/+%d value %d %s",
                         e.serial, e.time, gameplayEventName(e.type), e.instigator, e.instigatorState.specialty.c_str(), e.instigatorState.chassis.c_str(),
                         e.instigatorState.vehicleForm ? " VEH" : "", e.instigatorState.killStreak, e.victim, e.victimState.chassis.c_str(),
                         e.victimState.vehicleForm ? " VEH" : "", e.victimState.killStreak, e.damageType.c_str(), e.weapon.c_str(), (int)e.melee,
                         (int)e.ability, e.distanceUU, e.objective.c_str(), e.personalScore, e.teamScore, e.value, e.text.c_str());
            }
    }
    struct ProfScope { double t0 = profNowMs(); ~ProfScope() { const double ms = profNowMs() - t0; if (spawnProf() && ms > 8.0) LOG_INFO("SPAWNPROF World::tick %.1f ms", ms); } } profScope;
    // Cache (and prewarm) each participant's body as soon as its selection exists - during the countdown for everyone present,
    // and for bots / joiners / class or team changes before their next spawn wave - instead of at the spawn itself (a first
    // cache costs the glb load + renderer prewarm, ~130-165 ms). Only bodies that can appear in this match are loaded (Pass 24h
    // cached all eight MP defaults: ~1 GB per match, Integration 08i soak). Load scheduling only, not original behaviour.
    // At most one first-time body load per tick: two late participants never stack into one frame.
    if (matchActive_)
        for (size_t p = 0; p < match_.players().size(); ++p) {
            const MatchPlayer& mp = match_.players()[p];
            if (!mp.hasSelectedCharacter) continue;
            const std::string id = resolveChassis(mp.selection, match_.faction((int)p));
            if (chassisCache_.count(id)) continue;
            chassisAssets(id);
            break;
        }
    // The local player's selected weapons (only the local pawn draws a held weapon mesh): custom list, else the iconic preset.
    if (matchActive_ && localPlayer_ >= 0 && (size_t)localPlayer_ < match_.players().size()) {
        const MatchPlayer& lp = match_.players()[(size_t)localPlayer_];
        if (lp.hasSelectedCharacter) {
            std::vector<std::string> want = lp.selection.weapons;
            if (lp.selection.type != 0 || want.empty())
                if (const ChassisAssets* ca = chassisAssets(resolveChassis(lp.selection, match_.faction(localPlayer_)))) want = ca->def.iconicWeapons;
            if (want != preloadedSelection_) { preloadedSelection_ = want; preloadHeldWeaponModels(want); }
        }
    }
    pickupEvents_.clear();
    matchEvents_.clear();
    destructibleEvents_.clear();
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
    { TickTimer tt(1); tickAbilityEffects(dt); }
    if (!localPlayerDead()) player_.pawn().health().tickRegen(dt, player_.pawn().regenBuffRemain_ > 0.0f ? 2.0f : 1.0f);
    for (MatchOpponent* o : opponents_) if (o->spawned()) o->health().tickRegen(dt, o->pawn().regenBuffRemain_ > 0.0f ? 2.0f : 1.0f);
    if (matchActive_) { TickTimer tt(2); tickMatch(dt); }
    if (matchActive_) {
        awards_.consume(match_);
        static const bool xplog = std::getenv("WFC_XPLOG") != nullptr;   // diagnostics: each award once (left undrained for the caller)
        if (xplog) {
            if (xpLogged_ > awards_.pendingXp().size()) xpLogged_ = 0;   // drained / trimmed since
            for (size_t i = xpLogged_; i < awards_.pendingXp().size(); ++i) {
                const XpAward& a = awards_.pendingXp()[i];
                LOG_INFO("XP p%d txn %d %s +%ld \"%s\" %s", a.player, a.transactionId, a.eventId.c_str(), a.xp, a.announcement.c_str(), a.extra.c_str());
            }
            xpLogged_ = awards_.pendingXp().size();
        }
    }
    if (!localPlayerDead()) {                       // dead / not yet spawned (match): no pawn simulation
        player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn
    }
    participantShots_.clear();
    { TickTimer tt(3); tickBots(dt); }                                  // bot participants: decisions -> intents, weapons
    for (MatchOpponent* o : opponents_) {                               // participants: shared movement + animation
        { TickTimer tt(4); o->simulateMovement(dt, collision()); }
        {   // Animation LOD above 16 participants (PC ADAPTATION; off at the original counts): beyond 40 m from the camera every 2nd
            // step, beyond 100 m every 4th, with the accumulated time (clip timing exact); transforming pawns always every step.
            TickTimer tt(5);
            int every = 1;
            if (match_.players().size() > 16 && o->spawned() && !o->pawn().isTransforming()) {
                const float d = core::length(o->pawn().position() - player_.pawn().position());   // simulation state, not the camera
                every = d < 40.0f ? 1 : (d < 100.0f ? 2 : 4);
            }
            o->simulateAnimationLod(dt, every);
        }
    }
    { TickTimer tt(6); tickParticipantWeapons(dt); }
    for (auto& kv : partBeams_) kv.second.time = std::max(0.0f, kv.second.time - dt);
    player_.controller().tickCameraCollision(dt);   // obstruction behaviour after the pawn moved
    gameplayRamContacts();
    separatePawns();
    if (const char* ap = std::getenv("WFC_AIMPITCH"))     // diagnostic: force the aim pitch (rad)
        player_.pawn().setAimPitch((float)std::atof(ap));
    player_.pawn().updateAnimation(dt);
    tickWeaponPresentation(dt);
    tickVehicleBoost(dt);
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
        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
    }
    // Event-driven audio via edge detection on pawn state.
    {
        core::Vec3 pp = player_.pawn().position();
        bool grounded = player_.pawn().onGround();
        // Robot-form landing placeholder; vehicle landings use the authored land cues.
        if (grounded && !prevGrounded_ && player_.pawn().form() == Form::Robot) playSfx(Sfx::Land, pp);
        prevGrounded_ = grounded;
        bool tf = player_.pawn().isTransforming();
        if (tf && !prevTransforming_) playSfx(Sfx::Transform, pp);
        prevTransforming_ = tf;
        // WP_LoopingTail: the SHOOT_TAIL cue when a burst ends (trigger released / mag empty).
        sinceShot_ += dt;
        const Weapon& w = player_.pawn().weapon();
        if (burstActive_ && sinceShot_ > w.fireInterval * 2.0f) {
            burstActive_ = false;
            core::Mat4 ms;
            core::Vec3 tp = weaponSocketWorld("MuzzleFlash", ms) ? core::Vec3{ms.m[12], ms.m[13], ms.m[14]} : pp;
            cues_.play("SHOOT_TAIL", tp, core::length(tp - pp));
        }
        cues_.tick(dt);
    }

    tickHazards(dt);
    { TickTimer tt(9); tickProjectiles(dt); }
    if (player_.pawn().position().y < killZ_ && !localPlayerDead()) {
        // Below KillZ: FellOutOfWorld -> Died with no killer (an environmental death in a match).
        LOG_INFO("World: player fell out of world; %s", matchActive_ ? "killed (KillZ)" : "respawning");
        if (matchActive_) killLocalPlayer(-1, false, "Engine.DmgType_Fell");   // WorldInfo.KillZDamageType [HIGH: stock default]
        else respawnPlayer();
    }
    { TickTimer tt(10); for (auto& a : actors_) if (a->alive()) a->tick(*this, dt); }
    for (size_t i = 0; i < actors_.size();) {
        if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); }
        else ++i;
    }
}

// Local match host glue (TnMultiplayerGame / TnTeamGame on the authority): the Match decides spawns, deaths and the
// end; World applies them to the local pawn and the map actors.
void World::startLocalMatch(const MatchSettings& s) {
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
    match_.setSnapshotProvider([this](int p) { return participantSnapshot(p); });
    if (localPlayer_ < 0) localPlayer_ = match_.addPlayer("Player");
    match_.playerMutable(localPlayer_).kind = ParticipantKind::Local;
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
    // Under the match load: cache (and prewarm) the four default MP bodies of the local player's faction, so a class pick in
    // the lobby (after this, on a visible frame) is instant. Other participants' bodies (bots, the other faction, custom
    // chassis) cache as soon as their selection exists (World::tick). Keep this after the local team is final.
    // Load scheduling only, not original behaviour.
    if (localPlayer_ >= 0) {
        const int fa = match_.faction(localPlayer_);
        for (int sp = 0; sp < 4; ++sp) {
            chassisAssets(defaultChassis((Specialty)sp, fa));
            preloadHeldWeaponModels(classPresetWeapons(specialtyName((Specialty)sp)));   // its preset weapons too
        }
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
    // Private Match Bot Settings (Frontend contract; PC ADAPTATION). Clamped again against the slots when the bots are added.
    if (opt.count("BotsFriendly")) out.bots.friendly = std::max(0, std::atoi(opt["BotsFriendly"].c_str()));
    if (opt.count("BotsEnemy")) out.bots.enemy = std::max(0, std::atoi(opt["BotsEnemy"].c_str()));
    if (opt.count("BotDifficulty")) out.bots.difficulty = std::clamp(std::atoi(opt["BotDifficulty"].c_str()), 0, 2);
    // CUSTOM-GAME EXTENSION: 16 bots per team (+ the human); off = the original 10-player slots.
    if (opt.count("BotsAutobot")) out.bots.autobot = std::max(0, std::atoi(opt["BotsAutobot"].c_str()));
    if (opt.count("BotsDecepticon")) out.bots.decepticon = std::max(0, std::atoi(opt["BotsDecepticon"].c_str()));
    if (opt.count("BotDifficultyAutobot")) out.bots.difficultyAutobot = std::clamp(std::atoi(opt["BotDifficultyAutobot"].c_str()), 0, 2);
    if (opt.count("BotDifficultyDecepticon")) out.bots.difficultyDecepticon = std::clamp(std::atoi(opt["BotDifficultyDecepticon"].c_str()), 0, 2);
    for (const char* k : {"ExtendedPlayers", "BotsExtended"})
        if (opt.count(k) && std::atoi(opt[k].c_str()) != 0) { out.bots.extended = true; out.settings.applyExtendedSlots(); }
    return !out.map.empty();
}

void World::resetForNewLevel() {
    // A new match is a fresh load of the map in the original (MatchOver -> ReturnToGameLobby -> ServerTravel).
    mapState_.resetForNewMatch();
    if (collision_.valid()) mapState_.tick(0.0f, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    for (Destructible* d : destructibles_) d->resetForNewMatch();
    for (PickupFactory* f : pickupFactories_) f->resetToPickup(*this);
}

// CUSTOM-GAME EXTENSION (32 v 32 / FFA 64; PC ADAPTATION): maps author 20-30 team starts per side and 10-50 FFA starts, so an
// extended match gets extra spawn points, generated once at launch (deterministic, no runtime search). Candidates ring each
// authored start of the side (FFA: every start) at 4.5 m steps out to 40 m; a point is kept when it stands on walkable ground
// (normal y >= 0.75, on the bot nav mesh when there is one), has the full pawn capsule clear (radius 2 m, height 4 m), is in line
// of sight of its seed start (same area, not through a wall), lies above KillZ, is at least 4.5 m from every authored and
// generated point, and (team games) stays out of the enemy start area: >= 25 m from every enemy start and nearer the own side's.
// It faces as its seed start. Target: 32 per side (64 FFA) plus a margin of 12 beyond what the authored pool offers.
void World::generateExtraStarts() {
    const auto& starts = match_.starts();
    const CollisionWorld* col = collision_.valid() ? &collision_ : nullptr;
    if (!col) { match_.setGeneratedStarts({}); return; }
    const bool navOk = ensureBotNav();
    const bool teamGame = match_.settings().teamGame;
    const float R = core::config::kPawnRadius, H = 2.0f * core::config::kPawnHalfHeight, kSep = 4.5f;
    std::vector<core::Vec3> taken;
    for (const Match::Start& s : starts) if (!s.generated) taken.push_back(s.pos);
    auto clear = [&](const core::Vec3& g) {
        float t;
        if (col->segmentHit(g + core::Vec3{0, 0.3f, 0}, g + core::Vec3{0, H + 0.2f, 0}, t)) return false;   // headroom
        for (float hy : {0.6f, 2.0f, 3.6f})
            for (int k = 0; k < 8; ++k) {
                const float a = 0.785398f * (float)k;
                const core::Vec3 c = g + core::Vec3{0, hy, 0};
                if (col->segmentHit(c, c + core::Vec3{std::cos(a) * (R + 0.2f), 0, std::sin(a) * (R + 0.2f)}, t)) return false;
            }
        return true;
    };
    std::vector<Match::Start> extra;
    auto fill = [&](int team, int want) {
        std::vector<const Match::Start*> seeds, enemies;
        for (const Match::Start& s : starts) {
            if (s.generated) continue;
            if (!teamGame) seeds.push_back(&s);
            else if (!s.ffa && s.team == team) seeds.push_back(&s);
            else if (!s.ffa && s.team != team) enemies.push_back(&s);
        }
        int made = 0;
        for (float r = kSep; r <= 40.0f && made < want; r += kSep)
            for (const Match::Start* sd : seeds) {
                if (made >= want) break;
                const int n = std::max(6, (int)std::round(6.2831853f * r / kSep));
                for (int k = 0; k < n && made < want; ++k) {
                    const float a = 6.2831853f * (float)k / (float)n + 0.37f * (float)(&*sd - &starts[0]);
                    core::Vec3 c = sd->pos + core::Vec3{std::cos(a) * r, 0, std::sin(a) * r};
                    float gy; core::Vec3 gn;
                    if (!col->groundHeight(c.x, c.z, sd->pos.y + 2.0f, 2.5f, gy, gn) || gn.y < 0.75f) continue;
                    c.y = gy;
                    if (c.y < killZ_ + 5.0f) continue;
                    bool hazard = hazardAt(c + core::Vec3{0, 1.0f, 0}) >= 0 || hazardAt(c + core::Vec3{0, 3.0f, 0}) >= 0;   // not in a kill / pain volume
                    for (int q = 0; q < 4 && !hazard; ++q)
                        hazard = hazardAt(c + core::Vec3{q == 0 ? R : q == 1 ? -R : 0.0f, 1.0f, q == 2 ? R : q == 3 ? -R : 0.0f}) >= 0;
                    if (hazard) continue;
                    bool near = false;
                    for (const core::Vec3& q : taken) if (core::length(q - c) < kSep) { near = true; break; }
                    if (near) continue;
                    if (navOk && botNav_.findCell(c, 1.0f, 2.0f) < 0) continue;
                    if (teamGame) {
                        float dOwn = 1e9f, dEnemy = 1e9f;
                        for (const Match::Start* s : seeds) dOwn = std::min(dOwn, core::length(s->pos - c));
                        for (const Match::Start* s : enemies) dEnemy = std::min(dEnemy, core::length(s->pos - c));
                        if (dEnemy < 25.0f || dEnemy < dOwn) continue;
                    }
                    float t;
                    if (col->segmentHit(sd->pos + core::Vec3{0, 1.0f, 0}, c + core::Vec3{0, 1.0f, 0}, t)) continue;   // same area as its seed
                    if (!clear(c)) continue;
                    Match::Start g;
                    g.actor = "Generated_" + std::to_string(extra.size()); g.cluster = sd->cluster;
                    g.team = teamGame ? team : 255; g.ffa = !teamGame; g.pos = c; g.yaw = sd->yaw;
                    extra.push_back(g); taken.push_back(c); ++made;
                }
            }
        return made;
    };
    const int margin = 12;   // spare points: the extended safe check keeps 1 m beyond both cylinders
    if (teamGame) {
        for (int t = 0; t < 2; ++t) {
            int authored = 0;
            for (const Match::Start& s : starts) authored += !s.generated && (s.ffa || s.team == t);
            const int want = std::max(0, match_.settings().maxPerTeam + margin - authored);
            const int made = fill(t, want);
            LOG_INFO("spawns: team %d: %d authored (team + FFA), %d generated of %d wanted", t, authored, made, want);
        }
    } else {
        const int authored = match_.authoredStartCount();
        const int want = std::max(0, match_.settings().maxPlayers + margin - authored);
        const int made = fill(255, want);
        LOG_INFO("spawns: FFA: %d authored, %d generated of %d wanted", authored, made, want);
    }
    match_.setGeneratedStarts(extra);
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
    removeBots();   // the previous match's bots leave with it (a new match is a fresh level in the original)
    awards_.setXpScale(1.0f);
    {   // simulation RNG per match: WFC_SEED (DEV / TEST) or a fixed value, so the same inputs replay the same match
        const char* sd = std::getenv("WFC_SEED");
        core::simRandSeed(sd ? (uint32_t)std::strtoul(sd, nullptr, 10) * 2654435761u + 1u : 0x5eed2026u);
    }
    startLocalMatch(l.settings);
    if (l.settings.extendedSlots) generateExtraStarts(); else match_.setGeneratedStarts({});
    const int nb = addBots(l.bots);
    if (nb > 0) awards_.setXpScale(BotXpPolicy::scale(botDifficulty_));   // XP in bot matches by bot difficulty (user decision)
    LOG_INFO("match: launched %s %s (goal %d, time %d s, bots %d: friendly %d enemy %d %s)", l.map.c_str(), l.modeTag.c_str(), l.settings.goalScore,
             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));
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
        const Match::KillContext kc = killContext(instigator, victim, damageType);
        if (victim == localPlayer_) killLocalPlayer(instigator, false, damageType, &kc);
        else { match_.killed(instigator, victim, false, damageType, &kc); if (opp) opp->despawn(); }
    }
    return true;
}

void World::removeBots() {
    size_t first = match_.players().size();
    for (size_t i = match_.players().size(); i-- > 0;) { if (match_.players()[i].kind != ParticipantKind::Bot) break; first = i; }
    if (first == match_.players().size()) { bots_.clear(); return; }
    for (size_t i = 0; i < opponents_.size();) {
        if ((size_t)opponents_[i]->matchPlayer() >= first) { opponents_[i]->destroy(); opponents_.erase(opponents_.begin() + (long)i); } else ++i;
    }
    for (size_t i = 0; i < actors_.size();) { if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); } else ++i; }
    match_.truncatePlayers(first);
    bots_.clear();
    botSearchOwner_ = -1;
    for (auto& kv : partBeams_) kv.second.time = 0.0f;   // stopped at the next draw
}

int World::addBots(const BotLaunch& launch) {
    // Per-faction counts (team modes) become friendly / enemy relative to the human's team (team 0 Autobots, 1 Decepticons).
    BotLaunch b = launch;
    if (match_.settings().teamGame && (b.autobot >= 0 || b.decepticon >= 0) && localPlayer_ >= 0) {
        const bool humanAutobot = match_.players()[(size_t)localPlayer_].team != 1;
        const int a = std::max(0, b.autobot), d = std::max(0, b.decepticon);
        b.friendly = humanAutobot ? a : d; b.enemy = humanAutobot ? d : a;
    }
    if (!matchActive_ || (b.friendly <= 0 && b.enemy <= 0)) return 0;
    ensureBotNav();   // under the match load, not on a simulation step
    botDifficulty_ = std::clamp(b.difficulty, 0, 2);
    const MatchSettings& s = match_.settings();
    std::vector<std::string> taken;
    int humans = 0;
    for (const MatchPlayer& p : match_.players()) { taken.push_back(p.name); humans += p.kind != ParticipantKind::Bot; }
    const int humanTeam = localPlayer_ >= 0 ? match_.players()[(size_t)localPlayer_].team : 0;
    static unsigned matchSeed = 0x5eed;
    // WFC_SEED (DEV / TEST only): offsets every bot / spawn random source so two runs can be compared or varied; unset = unchanged.
    // with WFC_SEED every launch uses the same identity seed (no chaining from earlier matches), so a launch replays exactly
    if (const char* sd = std::getenv("WFC_SEED")) { const unsigned v = (unsigned)std::strtoul(sd, nullptr, 10); matchSeed = v * 2654435761u ^ 0x5eedu; std::srand(v); }
    matchSeed = matchSeed * 1664525U + 1013904223U;
    const std::vector<BotIdentity> ids = makeBotIdentities(b, s.teamGame, humanTeam, s.maxPerTeam, s.maxBotsPerTeam, s.maxPlayers, humans, taken, matchSeed);
    for (const BotIdentity& id : ids) {
        const int p = match_.addPlayer(id.name, id.team);
        MatchPlayer& mp = match_.playerMutable(p);
        mp.kind = ParticipantKind::Bot;
        mp.level = id.level;
        match_.selectCharacter(p, id.selection);
        preloadHeldWeaponModels(id.selection.weapons);   // under the match load, not at the bot's first shot
        chassisAssets(resolveChassis(id.selection, match_.faction(p)));   // the body too (glb load + renderer prewarm), not at its first spawn
        auto o = std::make_unique<MatchOpponent>(p, mp.team, true);
        o->pressesPickup = true;
        const int perTeam = mp.team == 0 ? b.difficultyAutobot : (mp.team == 1 ? b.difficultyDecepticon : -1);
        addBotBrain(p, perTeam >= 0 ? perTeam : botDifficulty_);
        opponents_.push_back(o.get());
        actors_.push_back(std::move(o));
        LOG_INFO("bots: %s team %d %s (%s) level %d abilities %s/%s", id.name.c_str(), mp.team, specialtyName(id.selection.specialty),
                 resolveChassis(id.selection, match_.faction(p)).c_str(), id.level,
                 id.selection.abilities.size() > 0 ? id.selection.abilities[0].c_str() : "-", id.selection.abilities.size() > 1 ? id.selection.abilities[1].c_str() : "-");
    }
    return (int)ids.size();
}

MatchOpponent* World::addMatchOpponent(const std::string& name, bool drawn) {
    int p = match_.addPlayer(name);
    auto o = std::make_unique<MatchOpponent>(p, match_.players()[(size_t)p].team, drawn);
    MatchOpponent* raw = o.get();
    opponents_.push_back(raw);
    actors_.push_back(std::move(o));
    // Cache the participant's body now (callers add opponents at match load, under the loading screen) instead of in a
    // visible World tick later. Load scheduling only, not original behaviour.
    const MatchPlayer& mp = match_.players()[(size_t)p];
    if (mp.hasSelectedCharacter) chassisAssets(resolveChassis(mp.selection, match_.faction(p)));
    return raw;
}

HudGameState World::hudState() const {
    HudGameState h;
    const Character& pc = player_.pawn();
    h.alive = !localPlayerDead();
    h.health = pc.health().current; h.healthMax = pc.health().max;
    h.overshield = pc.health().overshield(); h.normalizedOverShield = pc.health().normalizedOverShield();
    h.activeSegment = pc.health().activeSegment(); h.segmentCount = pc.health().segmentCount;
    // The HUD weapon observers read PC.Pawn.Weapon: in vehicle form that is the vehicle weapon (clip / reserve; all vehicle
    // WEPDATA are ammo-based, HeatMax 0). No observer exposes refire / cooldown / reload progress [CONF RE answer 2026-10-06].
    const Weapon* hw = &pc.weapon();
    if (pc.moveForm() == Form::Vehicle && pc.vehicleWeapon()) hw = pc.vehicleWeapon();
    h.vehicleWeaponHeld = hw != &pc.weapon();
    h.clipAmmo = hw->ammo; h.reserveAmmo = hw->reserve; h.clipMax = hw->magSize; h.reserveMax = hw->reserveMax;
    h.weaponName = hw->name;
    h.weaponId = hw->def ? hw->def->provider : "IonBlaster";
    h.weaponSimulated = hw->simulated();
    h.weaponIcon = hw->def ? hw->def->killFeedIcon : "death_IonBlaster";
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
    { const BarrierState& lb = barrier(); h.barrier = lb.alive; h.barrierHealth = lb.health; }
    h.repairBeam = repairBeam_.active && repairBeam_.time > 0.0f; h.repairBeamHealing = repairBeam_.healing;
    h.repairBeamStart = repairBeam_.start; h.repairBeamEnd = repairBeam_.end; h.repairBeamTarget = repairBeam_.target;
    h.vehicleShotSerial = vehicleShotSerial_; h.vehicleShotSocket = vehicleShotSocket_; h.vehicleShotMuzzle = vehicleShotMuzzle_;
    if (!localPlayerDead() && player_.pawn().weapon().charge()) {
        h.weaponChargeState = player_.pawn().weapon().chargeState;
        h.weaponChargeMessage = player_.pawn().weapon().chargeHudMessage();
        h.weaponChargeGlow = player_.pawn().weapon().chargeGlow();
        h.weaponChargeSerial = player_.pawn().weapon().chargeSerial;
        h.weaponChargeFizzle = player_.pawn().weapon().chargeFizzle;
        h.weaponChargeShotLevel = player_.pawn().weapon().chargeShotLevel;
    }
    if (matchActive_ && !localDead_) {
        MapState::ObjPawn op{localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, pc.form() == Form::Robot && !pc.isTransforming() && !pc.isMeleeing()};
        int ci = mapState_.pickupCandidate(op);
        h.pickupPrompt = ci < 0 ? "" : mapState_.carried()[(size_t)ci].kind == 0 ? "Code Of Power" : "Bomb";
    }
    { const AmmoBeacon& lb = localBeacon(); h.ammoBeacon = lb.alive; h.ammoBeaconPos = lb.pos; h.ammoBeaconLife = lb.life; h.ammoBeaconHealth = lb.health; }
    h.ammoBeaconBuff = pc.beaconDamageBuff_ > 0.0f;
    h.drain = pc.drainRemain_;
    h.kamikazeMines = (int)mines_.size(); h.tempWeaponLeft = pc.tempWeapon_ == 1 ? pc.tempWeaponRemain_ : 0.0f;
    { const RollerMine& lr = rollerMine();
      h.roller = lr.alive; h.rollerArmed = lr.alive && lr.t >= 3.0f; h.rollerPos = lr.pos;
      h.rollerFuse = lr.alive ? std::max(0.0f, 10.0f - lr.t) : 0.0f; h.rollerHealth = lr.health; h.rollerSlow = pc.rollerSlowRemain_; }
    h.guidedMissile = missile_.alive; h.guidedMissilePos = missile_.pos; h.guidedMissileFuse = missile_.life;
    { const Sentry& ls = sentry(); h.sentry = ls.alive; h.sentryHealth = ls.health; h.sentryPos = ls.pos; h.sentryTarget = ls.target; }
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

void World::killLocalPlayer(int killer, bool suicide, const std::string& damageType, const Match::KillContext* ctx) {
    if (!matchActive_ || localDead_) return;
    const Match::KillContext kc = ctx ? *ctx : killContext(killer, localPlayer_, damageType);
    match_.killed(killer, localPlayer_, suicide, damageType, &kc);
    localDead_ = true;
}

void World::tickMatch(float dt) {
    Character& pc = player_.pawn();
    if (!localDead_) {
        match_.setPlayerLocation(localPlayer_, pc.position(), pc.cylinderRadius(pc.moveForm()));
        if (pc.health().isDead()) killLocalPlayer(-1, false);   // damage without an instigator
    }
    for (MatchOpponent* o : opponents_) if (o->spawned()) match_.setPlayerLocation(o->matchPlayer(), o->position(), o->pawn().cylinderRadius(o->pawn().moveForm()));
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
        for (const auto& a : sc.actions) match_.recordObjective(a.kind, a.player, a.team, a.value);
        for (auto& msg : sc.messages) LOG_INFO("match: %s switch %d", msg.first.c_str(), msg.second);
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
                break;
            case MatchEvent::Type::RoundStarted:
                mapState_.roundStart(e.value);         // SingleFlagCTF.SetupRoundStart (attacking team)
                break;
            case MatchEvent::Type::MatchStarted:
                mapState_.matchStarting();             // KOTH initial zone (MatchStarting); CTF / EXT carried objectives
                // TnTeamGame.StartMatch: Reset() every pickup factory (sleeping factories return to 'Pickup').
                for (PickupFactory* f : pickupFactories_) f->resetToPickup(*this);
                break;
            case MatchEvent::Type::PlayerSpawned:
                if (e.player == localPlayer_ && e.value >= 0) {
                    // RestartPlayer: a fresh pawn (robot form, full health, default inventory) at the chosen start,
                    // with its authored rotation.
                    const Match::Start& st = match_.starts()[(size_t)e.value];
                    // TnPawn.PostBeginPlay -> ApplyTransformer(chassis), then SetPlayerDefaults -> ApplyCharacter ->
                    // ApplySpecialty (StartingForm forced to robot) [CONF script, RE TARGETED_PASS3 §A].
                    MatchPlayer& mp = match_.playerMutable(localPlayer_);
                    const double tp0 = profNowMs();
                    applyChassisToLocalPawn(mp.chassis);
                    const double tp1 = profNowMs();
                    pc.respawnReset();   // a fresh pawn: robot form, no fold, HealthMax, default inventory
                    const double tp2 = profNowMs();
                    double tp3 = tp2;
                    {
                        const ChassisAssets* ca = chassisAssets(mp.chassis);
                        mp.specialty = mp.selection.type == 0 ? specialtyName(mp.selection.specialty)
                                                              : (ca ? ca->def.iconicSpecialty : std::string());   // CharacterData.Specialty of the preset [CONF RE]
                        if (const SpecialtyDef* sd = specialtyDef(mp.specialty))
                            pc.setSpecialty(sd->id, sd->speedMultiplier, sd->segments, sd->overshield);
                        else pc.clearSpecialty();
                        loadoutRefused_ = applyLoadout(&mp.selection);
                        tp3 = profNowMs();
                        mp.healthMax = pc.health().max;
                    }
                    core::Vec3 p = st.pos;
                    float gy; core::Vec3 gn;
                    if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                    pc.setPosition(p); pc.setYaw(st.yaw); pc.velocity() = {0, 0, 0}; pc.groundY = p.y;
                    player_.controller().setCameraYaw(st.yaw);
                    localDead_ = false;
                    player_.controller().clearSpectatorView();
                    if (spawnProf()) LOG_INFO("SPAWNPROF local spawn: chassis apply %.1f ms, respawnReset %.1f ms, specialty+loadout %.1f ms, rest %.1f ms (%s)",
                                              tp1 - tp0, tp2 - tp1, tp3 - tp2, profNowMs() - tp3, mp.chassis.c_str());
                    recordSpawnEvent(localPlayer_);
                    LOG_INFO("match: local player spawned at %s (%s team %d)", st.actor.c_str(), st.cluster.c_str(),
                             match_.players()[(size_t)localPlayer_].team);
                }
                for (MatchOpponent* o : opponents_)
                    if (o->matchPlayer() == e.player && e.value >= 0) {
                        MatchPlayer& op = match_.playerMutable(o->matchPlayer());
                        const double to0 = profNowMs();
                        const bool chassisOk = applyChassisToPawn(o->pawn(), op.chassis);
                        const double to1 = profNowMs();
                        if (chassisOk) applyCharacterTo(o->pawn(), &op.selection, &op);
                        const double to2 = profNowMs();
                        core::Vec3 p = match_.starts()[(size_t)e.value].pos;
                        float gy; core::Vec3 gn;
                        if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                        o->spawnAt(p);
                        recordSpawnEvent(o->matchPlayer());
                        if (spawnProf()) LOG_INFO("SPAWNPROF opponent %d spawn: chassis apply %.1f ms, character %.1f ms, place+spawnAt %.1f ms (%s)",
                                                  o->matchPlayer(), to1 - to0, to2 - to1, profNowMs() - to2, op.chassis.c_str());
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
}

void World::draw(render::IRenderer& r) const {
    syncMapPresentation(r);
    if (mapMesh_ != render::kInvalidMesh) {
        r.drawMesh(mapMesh_, core::Mat4::identity(), mapColor_);
    } else {
        r.drawGroundGrid(60.0f, 2.0f, core::Vec3{0.30f, 0.33f, 0.38f});
        for (const auto& b : blocks_) r.drawBox(b.center, b.size, b.color);
    }
    // Participant culling for extended matches (> 16 participants, PC ADAPTATION; off at the original counts): a pawn outside the
    // view cone (horizontal half angle 75 deg + its size) is not drawn or skinned this frame. Off-screen shadows are the cost.
    {
        const bool cull = match_.players().size() > 16;
        const core::Vec3 cp = player_.controller().cameraPos();
        const core::Vec3 vd = core::forwardFromYawPitch(player_.controller().viewYaw(), player_.controller().camPitch());
        for (const MatchOpponent* o : opponents_) {
            bool off = false;
            if (cull && o->spawned()) {
                const core::Vec3 to = o->pawn().actorLocation() - cp;
                const float d = core::length(to);
                if (d > 6.0f) off = core::dot(to * (1.0f / d), vd) < std::cos(std::min(3.1f, 1.309f + std::atan(5.0f / d)));
            }
            o->setCulled(off);
        }
    }
    for (const auto& a : actors_) if (a->alive()) a->draw(r);
    if (!localPlayerDead()) player_.draw(r);
    // Projectiles: the authored FlightEffect is the body (a renderer particle system, projectileFxStart); the thrown grenades
    // also draw their class-default static mesh. The box marker remains only when nothing authored can be shown (renderer
    // without the particle API, or a template missing from this map's FX data) [fallback, not original].
    for (const Projectile& p : projectiles_) {
        const ProjectileVisual* v = p.visual >= 0 ? &projVisuals_[(size_t)p.visual] : nullptr;
        if (v && v->body != render::kInvalidMesh) {
            // Grenades: spawn rotation (bRotationFollowsVelocity false) x the spinning mesh pitch; others follow the velocity.
            const float yaw = p.grenade ? p.yaw0 : std::atan2(-p.vel.x, -p.vel.z);
            const float pitch = p.grenade ? p.pitch0 + p.spin : 0.0f;
            r.drawMesh(v->body, core::Mat4::translate(p.pos) * core::Mat4::rotateY(yaw + core::config::kMeshYawOffset) * core::Mat4::rotateZ(pitch),
                       core::Vec3{1, 1, 1});
        } else if (p.fxHandle < 0) {
            r.drawBox(p.pos, core::Vec3{0.25f, 0.25f, 0.25f}, core::Vec3{1.0f, 0.6f, 0.2f});
        }
    }

    // Ion Blaster mesh held at the weapon socket (robot form only).
    if (localPlayerDead()) { /* no pawn: no weapon / pawn effects (PendingMatch, dead) */ }
    else if (weaponAnim_.valid() && player_.pawn().hasWeapon()) {
        // TnChargeWeapon.UpdateChargeEffects -> TnWeaponMesh.SetMaterialParameter(1, MaterialGlowAmount): index 1 of the Plasma
        // Cannon WEPMESH MaterialParameterModifiers is MPT_WeaponSpecific "Overheat" [CONF cooked data, Rendering M70]. Set for
        // this draw only and cleared after it, whatever the current draw owner (other weapons author 0 = the default).
        const Weapon& hw = player_.pawn().weapon();
        const bool glow = hw.charge() && hw.chargeGlow() > 0.0f;
        if (glow) { const float g = hw.chargeGlow(); const float rgba[4] = {g, g, g, 1.0f}; fxSetDrawParam(r, "Overheat", rgba, 0); }
        r.drawDynamicMesh(weaponAnim_.pose(), core::Mat4::translate(player_.pawn().renderOffset()) * player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
        if (glow) fxClearDrawParam(r, "Overheat", 0);
    }
    else if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
        r.drawMesh(weaponMesh_, core::Mat4::translate(player_.pawn().renderOffset()) * player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});

    // Participants' held weapons (robot form, weapon shown), at their pawn's interpolated weapon socket.
    for (const MatchOpponent* o : opponents_) {
        if (!o->spawned() || o->culled() || !o->pawn().hasWeapon()) continue;
        auto it = partWeapons_.find(o->matchPlayer());
        if (it == partWeapons_.end() || !it->second.anim.valid()) continue;
        r.drawDynamicMesh(it->second.anim.pose(), core::Mat4::translate(o->pawn().renderOffset()) * o->pawn().weaponWorld(), core::Vec3{1, 1, 1});
    }
    // Participant shots without a presentation hook: the weapon's authored templates by name [CONF WEPMESH data].
    for (const PendingShotFx& s : partShotFx_) {
        const WeaponDef* d = findWeaponDef(s.weapon);
        const core::Vec3 at{s.muzzle.m[12], s.muzzle.m[13], s.muzzle.m[14]};
        const core::Vec3 fwd = core::normalize(core::Vec3{s.muzzle.m[0], s.muzzle.m[1], s.muzzle.m[2]});
        const core::Vec3 up = core::normalize(core::Vec3{s.muzzle.m[4], s.muzzle.m[5], s.muzzle.m[6]});
        const bool muzzle = d && d->muzzleFx && *d->muzzleFx, tracer = s.tracer && d && d->tracerFx && *d->tracerFx;
        if (muzzle) fxSpawnPoint(r, d->muzzleFx, at, fwd, up, 0);
        if (tracer) fxSpawnSegment(r, d->tracerFx, at, s.to, 0);
        if (!muzzle && !tracer) {
            static std::set<std::string> warned;
            if (warned.insert(s.weapon).second) LOG_WARN("participant shot FX: no authored muzzle / tracer template for %s (nothing drawn)", s.weapon.c_str());
        }
    }
    partShotFx_.clear();
    // Participants' Repair Ray beams: the player's looping tracer (FX_RepairBeam_p.FX.Tracer_RepairBeam_FX) per healing bot.
    for (const auto& kv : partBeams_) {
        const ParticipantBeam& pb = kv.second;
        const bool on = pb.time > 0.0f;
        if (on && pb.fx < 0) pb.fx = fxSpawnSegment(r, "FX_RepairBeam_p.FX.Tracer_RepairBeam_FX", pb.start, pb.end, 0);
        else if (on) fxSetSegment(r, pb.fx, pb.start, pb.end, 0);
        else if (pb.fx >= 0) { fxStopEffect(r, pb.fx, 0); pb.fx = -1; }
    }

    // Repair Ray beam: spawn the looping tracer when the beam starts, move its source / target every frame, stop on release.
    {
        const bool on = repairBeam_.active && repairBeam_.time > 0.0f;
        if (on && repairBeamFx_ < 0) repairBeamFx_ = fxSpawnSegment(r, "FX_RepairBeam_p.FX.Tracer_RepairBeam_FX", repairBeam_.start, repairBeam_.end, 0);
        else if (on) fxSetSegment(r, repairBeamFx_, repairBeam_.start, repairBeam_.end, 0);
        else if (repairBeamFx_ >= 0) { fxStopEffect(r, repairBeamFx_, 0); repairBeamFx_ = -1; }
        if (on && repairSquibDraw_) {
            const core::Vec3 d = core::normalize(repairBeam_.start - repairBeam_.end);
            const core::Vec3 up = std::fabs(d.y) > 0.99f ? core::Vec3{0, 0, -1} : core::Vec3{0, 1, 0};
            fxSpawnPoint(r, repairSquibHealing_ ? "FX_RepairBeam_p.FX.Squib_RepairTeam_FX" : "FX_RepairBeam_p.FX.Squib_RepairEnemy_FX", repairBeam_.end, d, up, 0);
        }
        repairSquibDraw_ = false;
    }

    // Weapon + vehicle boost effects last (translucent/additive over the opaque scene).
    fx_.draw(r);
    if (!localPlayerDead()) vehicleFx_.draw(r);
    for (const BarrierState& bs : barriers_) if (bs.alive && !bs.mesh.positions.empty()) r.drawDynamicMesh(bs.mesh, bs.world, core::Vec3{1, 1, 1});   // TnBarrierSpawnable mesh
    for (const Sentry& se : sentries_)
        if (se.alive && !se.mesh.positions.empty())
            r.drawDynamicMesh(se.mesh, core::Mat4::translate(se.pos) * core::Mat4::rotateY(se.yaw + core::config::kMeshYawOffset), core::Vec3{1, 1, 1});

    drawQaBotOverlay(r);   // DEV / QA TOOLING (off unless the panel turns it on)
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
    struct ProfScope { const std::string& id; double t0 = profNowMs(); ~ProfScope() { if (spawnProf()) LOG_INFO("SPAWNPROF chassis load %s: %.1f ms", id.c_str(), profNowMs() - t0); } } profScope{id};
    auto a = std::make_unique<ChassisAssets>();
    double chProf[6] = {0, 0, 0, 0, 0, 0};   // SPAWNPROF split: robot glb, vehicle glb, textures, arm, prewarm
    const std::string root = assetRoot();
    const std::string ext = root + "/../";
    if (!loadChassisDef(root, id, a->def)) {
        a->error = a->def.loadError;
    } else if ((chProf[0] = profNowMs(), !(loadRobotShared(a->def, a->robot) || assets::loadSkinnedGlb(ext + a->def.robotGlb, a->robot))) || (chProf[1] = profNowMs(), !a->robot.valid())) {
        a->error = "robot.glb failed to load for " + id;
    } else if (!assets::loadSkinnedGlb(ext + a->def.vehicleGlb, a->vehicle) || (chProf[2] = profNowMs(), !a->vehicle.valid())) {
        a->error = "vehicle.glb failed to load for " + id;
    } else {
        resolveModelTextures(a->robot);
        resolveModelTextures(a->vehicle);
        chProf[3] = profNowMs();
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
    chProf[4] = profNowMs();
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
    if (spawnProf() && chProf[0] > 0.0) LOG_INFO("SPAWNPROF chassis %s split: robot glb %.0f ms, vehicle glb %.0f, textures %.0f, arm %.0f, prewarm %.0f",
                                         id.c_str(), chProf[1] - chProf[0], chProf[2] - chProf[1], chProf[3] - chProf[2], chProf[4] - chProf[3], profNowMs() - chProf[4]);
    chassisCache_[id] = std::move(a);
    return raw;
}

bool World::applyChassisToPawn(Character& pc, const std::string& id) {
    const ChassisAssets* a = chassisAssets(id);
    if (!a || !a->ok) return false;
    pc.setChassis(&a->def);
    // A non-transforming flyer (Laserbeak) draws and animates its single body (robot.glb: hover / boost / land clips) in both forms;
    // its hit cylinder follows that body's bounds (CalculateCylinderBounds), not the full-size roster default.
    if (a->def.flyerNoTransform) pc.setFormModels(&a->robot, &a->robot);
    else pc.setFormModels(&a->robot, &a->vehicle);
    pc.setArmModel(a->hasArm ? &a->arm : nullptr);
    const SocketDef& wp = a->def.weaponPrimary;
    pc.setWeaponSocket(a->robot.nodeByName(wp.bone), wp.local);
    const SocketDef& ws = a->def.weaponSecondary;
    if (ws.valid) pc.setArmSocket(a->robot.nodeByName(ws.bone), ws.local);
    else pc.setArmSocket(-1, core::Mat4::identity());
    return true;
}

bool World::applyChassisToLocalPawn(const std::string& id) {
    const double t0 = profNowMs();
    if (!applyChassisToPawn(player_.pawn(), id)) return false;
    const double t1 = profNowMs();
    localChassis_ = id;
    applyLoadout(nullptr);   // iconic preset WeaponTypes / VehicleWeapons
    if (spawnProf()) LOG_INFO("SPAWNPROF applyChassisToLocalPawn %s: body %.1f ms, iconic loadout %.1f ms", id.c_str(), t1 - t0, profNowMs() - t1);
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
    pc.setLoadout(robot, (sel && sel->type == 0 && !sel->vehicleWeapons.empty()) ? sel->vehicleWeapons : d.iconicVehicleWeapons);
    pc.setAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities);
    pc.respawnReset();
    if (mp) { mp->specialty = spec; mp->healthMax = pc.health().max; }
    return refused;
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
    if (w.projHoming) {
        Projectile& p = projectiles_.back();
        p.homingForce = w.homingForce; p.closingDist = w.closingDistM; p.closingForce = w.closingForce;
        p.closingTime = w.closingTime; p.maxSpeed = w.projMaxSpeed; p.lockRobots = w.lockRobots;
    }
    {
        int vis = -1;
        if (w.def && w.projClass > 0) vis = projectileVisualFor((std::string(w.def->id) + "#" + std::to_string(w.projClass)).c_str());
        projectiles_.back().visual = vis >= 0 ? vis : projectileVisualFor(w.def ? w.def->id : nullptr);
    }
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
    for (size_t ri = 0; ri < rollers_.size(); ++ri) {
        if (!rollers_[ri].alive) continue;
        float dd = std::max(0.0f, core::length(rollers_[ri].pos - at) - 1.21f);
        if (dd < radius) damageRollerAt(ri, damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator);
    }
    for (size_t si = 0; si < sentries_.size(); ++si) {
        if (!sentries_[si].alive) continue;
        float dd = std::max(0.0f, core::length(sentries_[si].pos + core::Vec3{0, 2.0f, 0} - at) - 2.0f);
        if (dd < radius) damageSentryAt(si, damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator, type);
    }
    for (size_t bi = 0; bi < beacons_.size(); ++bi)
        if (beacons_[bi].alive && core::length(beacons_[bi].pos - at) < radius)
            damageAmmoBeaconAt(bi, damage * (1.0f - core::length(beacons_[bi].pos - at) / std::max(radius, 1e-3f)), instigator);
    for (size_t bi = 0; bi < barriers_.size(); ++bi) {
        const BarrierState& barrier_ = barriers_[bi];
        if (!barrier_.alive) continue;
        // Distance to the wall box (clamped point), not its centre.
        core::Vec3 l = core::transformPoint(barrier_.boxInv, at);
        core::Vec3 q{std::max(-barrier_.half.x, std::min(l.x, barrier_.half.x)), std::max(-barrier_.half.y, std::min(l.y, barrier_.half.y)),
                     std::max(-barrier_.half.z, std::min(l.z, barrier_.half.z))};
        float dd = core::length(l - q);
        if (dd < radius) damageBarrierAt(bi, damage * (1.0f - dd / std::max(radius, 1e-3f)), type);
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
            p.spin += p.spinRate * dt;   // MeshComp.SetRotation(Rotation + RotationRate * DeltaTime)
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
                    if (p.life > 1e8f) p.life = p.fuseMin + (p.fuseMax - p.fuseMin) * (float)(core::simRandU32() % 1000) / 999.0f;
                    p.vel = (p.vel - n * (2.0f * core::dot(p.vel, n))) * p.bounce;
                    if (core::dot(p.vel, p.vel) < 500.0f * 1e-4f) { p.resting = true; p.vel = {0, 0, 0}; p.spinRate = 0.0f; }
                };
                if (hitPawn && p.explodeOnPawn) {
                    radiusDamage(p.pos, p.damage, p.radius, p.instigator, p.damageType);
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
                projectileFxEnd(p, p.pos, core::Vec3{0, 1, 0}, true);   // fuse: resting on the floor [PROV normal]
                projectiles_.erase(projectiles_.begin() + (long)i); continue;
            }
            projectileFxMove(p);
            ++i;
            continue;
        }
        core::Vec3 next = p.pos + p.vel * dt;
        float best = 1.0f; bool hit = false;
        float t;
        core::Vec3 hitN{0, 0, 0};
        if (lineWorld && lineWorld->segmentHit(p.pos, next, t, hitN)) { best = t; hit = true; }
        core::Vec3 d = next - p.pos; float len = core::length(d);
        const float worldBest = best;
        bool barrierHit = false;
        if (len > 1e-5f) { float tb; if (barrierRayHit(p.pos, d * (1.0f / len), len, tb) && tb / len < best) { best = tb / len; hit = true; barrierHit = true; } }
        if (len > 1e-5f) {
            core::Vec3 dir = d * (1.0f / len);
            for (MatchOpponent* o : opponents_) {
                if (o->matchPlayer() == p.instigator) continue;
                float th;
                if (o->rayHit(p.pos, dir, len, th) && th / len < best) { best = th / len; hit = true; }
            }
            if (matchActive_ && !localDead_ && p.instigator != localPlayer_) {
                const Character& pc = player_.pawn();
                core::Vec3 c = pc.actorLocation(); float r = pc.cylinderRadius(pc.moveForm()), hh = pc.cylinderHalfHeight(pc.moveForm());
                for (int k = 1; k <= 8; ++k) {   // sampled segment vs the local cylinder
                    core::Vec3 q = p.pos + d * (k / 8.0f);
                    if (std::hypot(q.x - c.x, q.z - c.z) <= r && std::fabs(q.y - c.y) <= hh && k / 8.0f < best) { best = k / 8.0f; hit = true; break; }
                }
            }
        }
        p.life -= dt;
        if (hit || p.life <= 0.0f || closingExpired) {
            core::Vec3 at = p.pos + d * best;
            if (hit || closingExpired) radiusDamage(at, p.damage, p.radius, p.instigator, p.damageType);
            // HitNormal: the world surface's normal; a pawn / barrier hit (or no normal) faces back along the flight.
            const bool worldHit = hit && best == worldBest && core::dot(hitN, hitN) > 0.5f;
            const core::Vec3 n = worldHit ? hitN : (len > 1e-5f ? d * (-1.0f / len) : core::Vec3{0, 1, 0});
            projectileFxEnd(p, at, n, hit || closingExpired);   // LifeSpan expiry: Destroyed, no explosion
            projectiles_.erase(projectiles_.begin() + (long)i);
            continue;
        }
        p.pos = next;
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
    if (id == "GuidedMissileStreak") {                     // the local guided missile (camera-steered)
        mp.acquiredKillstreaks.pop_back();
        startGuidedMissile();
        LOG_INFO("killstreak %s triggered", id.c_str());
        return id;
    }
    const std::string fired = triggerKillstreakFor(localPlayer_);
    if (fired == "RefillAmmoStreak") lockedClip_ = pc.weapon().ammo;   // the owner's TnBuffLockAmmoClip holds this clip
    return fired;
}

// The newest acquired killstreak of any participant (TnPlayerController.TriggerKillstreak -> the provider's ability) [CONF effects
// per Pass 22]. Team / other-team effects reach every participant, the local pawn included. Omega Missile (GuidedMissileStreak) and
// Thermo Mine Re-Spawner (MinePooperStreak) are local-player code paths: other participants do not use them yet [PARTIAL].
std::string World::triggerKillstreakFor(int player) {
    if (!matchActive_ || player < 0 || (size_t)player >= match_.players().size()) return "";
    MatchPlayer& mp = match_.playerMutable(player);
    Character* pcp = participantPawnMutable(player);
    if (!pcp || mp.acquiredKillstreaks.empty()) return "";
    Character& pc = *pcp;
    const std::string id = mp.acquiredKillstreaks.back();
    if (player != localPlayer_ && (id == "GuidedMissileStreak" || id == "MinePooperStreak")) return "";
    mp.acquiredKillstreaks.pop_back();
    const int team = mp.team;
    const bool teamGame = match_.settings().teamGame;
    auto teamPawns = [&](auto fn) {   // TnTeamHandler.GetTeamMembers: living pawns of the owner's team (FFA: the owner)
        fn(pc);
        if (!teamGame) return;
        for (size_t i = 0; i < match_.players().size(); ++i)
            if ((int)i != player && match_.players()[i].team == team) if (Character* c = participantPawnMutable((int)i)) fn(*c);
    };
    auto otherTeam = [&](auto fn) {   // ABT_OtherTeam: every living participant not on the owner's team (FFA: everyone else)
        for (size_t i = 0; i < match_.players().size(); ++i)
            if ((int)i != player && !(teamGame && match_.players()[i].team == team)) if (Character* c = participantPawnMutable((int)i)) fn((int)i, *c);
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
        pc.ammoLockRemain_ = 10.0f;
    } else if (id == "HealthRegenStreak") {
        pc.regenBuffRemain_ = 30.0f;                       // TnBuffHealthRegenKillStreak FloatModifier 2, BuffTime 30
    } else if (id == "FastAbilityCooldownStreak") {
        pc.fastCooldownRemain_ = 30.0f;                    // TnBuffFastAbilityCooldown CooldownMultiplier 5, BuffTime 30
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
        otherTeam([&](int p, Character& c) {
            c.hardLockedRemain_ = 10.0f; c.hardLockedByTeam_ = team;
            applyMatchDamage(p, player, 1.0f, false, "TransGame.TnDamageTypeFlashBang");
        });
    } else if (id == "FriendlyKillHealthBonusStreak") {
        teamPawns([](Character& p) { p.refillOnKillRemain_ = 60.0f; });   // ABT_Team TnBuffRefillHealthOnKill
    } else if (id == "TeamAbilityJammerStreak") {
        otherTeam([](int, Character& c) { c.applyJammed(30.0f); });   // ABT_OtherTeam TnBuffAbilityJammedKillstreak 30 s
    } else {
        LOG_WARN("killstreak %s triggered: effect not implemented in the rebuild [PARTIAL]", id.c_str());
    }
    LOG_INFO("killstreak %s triggered (p%d)", id.c_str(), player);
    if (player != localPlayer_) for (BotBrain& b : bots_) if (b.player == player) ++b.streaks;
    return id;
}

// Ability effects for the local pawn (TnAbility*.ServerTriggerAbility) [CONF script + authored CDOs].
// TnAbilityWarcry for any participant: GetFriendliesInRange(AoeRange 3000 UU) = same-team live pawns (FFA: the owner only);
// BuffLevel = Clamp(count - 1, 0, 1) (+1 with TnSkillImprovedWarcry - skills not applied); BuffsToApply x level; BuffTime[0] = 15 s;
// removes HardLocked [CONF script + authored CDOs].
void World::applyWarcry(Character& pc, int self) {
    std::vector<Character*> friends{&pc};
    if (matchActive_ && match_.settings().teamGame && self >= 0) {
        for (MatchOpponent* o : opponents_)
            if (o->spawned() && &o->pawn() != &pc && match_.sameTeam(o->matchPlayer(), self) && core::length(o->pawn().actorLocation() - pc.actorLocation()) <= 30.0f)
                friends.push_back(&o->pawn());
        if (self != localPlayer_ && !localDead_ && match_.sameTeam(localPlayer_, self) && core::length(player_.pawn().actorLocation() - pc.actorLocation()) <= 30.0f)
            friends.push_back(&player_.pawn());
    }
    int level = std::max(0, std::min((int)friends.size() - 1, 1));
    const float dmg[3] = {1.1f, 1.2f, 1.3f}, taken[3] = {0.5f, 0.4f, 0.3f};
    for (Character* p : friends) { p->warcryRemain_ = 15.0f; p->warcryDamageMul_ = dmg[level]; p->warcryTakenMul_ = taken[level]; p->hardLockedRemain_ = 0.0f; }
    LOG_INFO("ability Warcry (p%d): %zu friendlies, buff level %d (damage x%.1f, taken x%.1f, 15 s)", self, friends.size(), level, dmg[level], taken[level]);
}

// TnAbilityShockwave.Shockwave for any participant: GetBP(index 0): Damage 65, Radius 2500 UU; HurtRadius(..., bDoFullDamage true)
// from PositionSocket; the owner is not hurt [HIGH]; teammates are filtered by the damage rules; momentum 700000 along origin ->
// victim [CONF].
void World::applyShockwave(Character& pc, int self) {
    const core::Vec3 at = pc.actorLocation();
    LOG_INFO("ability Shockwave (p%d): 65 within 25 m", self);
    auto hit = [&](int victim, const Character& v) {
        if (victim == self || core::length(v.actorLocation() - at) > 25.0f) return;
        const core::Vec3 dir = v.actorLocation() - at;
        applyMatchDamage(victim, self, 65.0f, false, "TransGame.TnDamageTypeShockwave");
        if (core::length(dir) > 1e-4f && !(match_.settings().teamGame && match_.sameTeam(victim, self)))
            applyKnockback(victim, core::normalize(dir) * 700000.0f, "TransGame.TnDamageTypeShockwave");
    };
    for (MatchOpponent* o : opponents_) if (o->spawned()) hit(o->matchPlayer(), o->pawn());
    if (self != localPlayer_ && !localDead_) hit(localPlayer_, player_.pawn());
    for (Destructible* d : destructibles_)
        if (d->state() == 0 && core::length((d->boxMin() + d->boxMax()) * 0.5f - at) <= 25.0f) d->applyDamage(*this, 65.0f);
}

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
        (void)team;
        applyWarcry(pc, localPlayer_);
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
        requestRoller(localPlayer_);                       // SpawnDelay 0.5
    } else if (fx == "GuidedMissile") {
        startGuidedMissile();
    } else if (fx == "SpawnSentry") {
        requestSentry(localPlayer_);                      // ActiveSentry.Kill(); SpawnDelay 0.2 (OnTriggerAnim additive: not played)
    } else if (fx == "SpawnAmmoCrate") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        requestAmmoBeacon(localPlayer_);                  // SpawnDelay 0.5 -> SpawnInventory
    } else if (fx == "Barrier") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        requestBarrier(localPlayer_);                     // SpawnDelay 0.5 -> SpawnBarrier
    }
    tickBarrier(dt);
    tickAmmoBeacon(dt);
    tickSentry(dt);
    tickGuidedMissile(dt);
    tickRollerMine(dt);
    repairBeam_.time = std::max(0.0f, repairBeam_.time - dt);
    if (repairSquibPending_) { repairSquibDraw_ = true; repairSquibPending_ = false; }
    if (repairBeam_.time <= 0.0f) repairBeam_.active = false;
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
    grenadeCooldown_ = std::max(0.0f, grenadeCooldown_ - dt);
    if (grenadeTossDelay_ >= 0.0f) {
        grenadeTossDelay_ -= dt;
        Character& gp = player_.pawn();
        const Weapon* gb = grenadeBag(gp);
        if (grenadeTossDelay_ < 0.0f && gb && !localDead_ && gp.moveForm() == Form::Robot) {
            releaseGrenade(gp, localPlayer_, *gb, grenadeTarget_);
        }
    }
    if (pc.shockwaveDelay_ >= 0.0f) {
        pc.shockwaveDelay_ -= dt;
        if (pc.shockwaveDelay_ < 0.0f && !localDead_) applyShockwave(pc, localPlayer_);
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
    if (localPlayerDead()) return;
    startMeleeFor(player_.pawn(), localPlayer_, whirlwind, player_.controller().viewYaw());
}

// TnMeleeAttack start for any participant pawn: the attack chooser, its action, and the melee-assist lunge toward an enemy in the
// picker cone about viewYaw.
void World::startMeleeFor(Character& pc, int self, bool whirlwind, float viewYaw) {
    if (pc.moveForm() != Form::Robot || pc.isTransforming() || pc.isMeleeing()) return;
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
    const core::Vec3 fwd = core::forwardFromYawPitch(viewYaw, 0.0f);
    float best = 1e9f;
    for (size_t vp = 0; vp < match_.players().size(); ++vp) {
        if ((int)vp == self || match_.sameTeam((int)vp, self)) continue;
        const Character* vc = participantPawn((int)vp);
        if (!vc) continue;
        core::Vec3 d = vc->actorLocation() - eye; d.y = 0.0f;
        float dist = core::length(d);
        if (dist > 20.0f || dist < 1e-3f) continue;
        float half = std::max(4.0f * 0.0174533f, std::min(std::atan(4.5f / dist), std::max(std::atan(3.5f / dist), 4.0f * 0.0174533f)));
        if (std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f)) > half) continue;
        if (dist < best) { best = dist; pc.lungeDir_ = d * (1.0f / dist); }
    }
    if (best < 1e9f) { pc.lungeRemain_ = 0.25f; pc.setYaw(std::atan2(-pc.lungeDir_.x, -pc.lungeDir_.z)); }
}

void World::tickLocalMelee(float dt) { tickMeleeFor(player_.pawn(), localPlayer_, dt); }

// The melee sweeps of any participant pawn: box checks against every other live participant (teammates excluded), damage with the
// attacker as instigator, momentum.
void World::tickMeleeFor(Character& pc, int self, float dt) {
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
        // Owner / teammate melee kicks a roller (TnRollerMineAbility.MeleeImpulse 5000), once per sweep.
        if (!pc.meleeHitRoller_) {
            for (RollerMine& rm : rollers_) {
                if (!rm.alive || std::fabs(rm.pos.x - at.x) > ex.x + 1.21f || std::fabs(rm.pos.z - at.z) > ex.z + 1.21f || std::fabs(rm.pos.y - at.y) > ex.y + 1.21f) continue;
                if (matchActive_ && self != rm.owner && !(match_.settings().teamGame && match_.sameTeam(self, rm.owner))) continue;
                core::Vec3 d = rm.pos - pc.actorLocation(); d.y = 0.0f;
                if (core::length(d) > 1e-4f) rm.vel = rm.vel + core::normalize(d) * 50.0f;
                pc.meleeHitRoller_ = true;
            }
        }
        const char* type = whirl ? "TransGame.TnDamageTypeWhirlwind" : pc.meleePoke_ ? "TransGame.TnDamageTypePoke" : "TransGame.TnDamageTypeMelee";
        for (size_t vpi = 0; vpi < match_.players().size(); ++vpi) {
            const int vp = (int)vpi;
            if (vp == self || (match_.settings().teamGame && match_.sameTeam(vp, self))) continue;
            const Character* vptr = participantPawn(vp);
            if (!vptr || std::find(pc.meleeHit_.begin(), pc.meleeHit_.end(), vp) != pc.meleeHit_.end()) continue;
            const Character& v = *vptr;
            const core::Vec3 c = v.actorLocation();
            const float r = v.cylinderRadius(v.moveForm()), hh = v.cylinderHalfHeight(v.moveForm());
            // MultiPointCheck of the box against the victim cylinder (as a box), then a clear trace attacker -> victim.
            if (std::fabs(c.x - at.x) > ex.x + r || std::fabs(c.z - at.z) > ex.z + r || std::fabs(c.y - at.y) > ex.y + hh) continue;
            float t;
            if (line && line->segmentHit(pc.actorLocation(), c, t)) continue;
            pc.meleeHit_.push_back(vp); ++pc.meleeHitCount_;
            applyMatchDamage(vp, self, damage, false, type);
            // Momentum = normal(victim - attacker) x Impulse (WeaponAttack 30000, flag / bomb 80000, Whirlwind 2000) [CONF].
            const float impulse = whirl ? 2000.0f : pc.meleeCarrier_ ? 80000.0f : pc.meleePoke_ ? 200000.0f : 30000.0f;
            core::Vec3 dir = c - pc.actorLocation();
            if (core::length(dir) > 1e-4f) applyKnockback(vp, core::normalize(dir) * impulse, type);
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
// TnGrenadeThrower.SpawnGrenade for any participant pawn: the toss toward target (the release after TossDelay).
void World::releaseGrenade(Character& gp, int player, const Weapon& gb, const core::Vec3& target) {
        // TnGrenadeThrower.SpawnGrenade at MeleeSocket_RightHand.
        core::Vec3 src = gp.actorLocation() + core::Vec3{0, gp.robotParams().eyeHeight, 0};
        const SocketDef& sd = gp.chassis().rightHand;
        core::Mat4 bm;
        if (sd.valid && gp.boneWorld(sd.bone, bm)) { core::Mat4 w = bm * sd.local; src = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
        const WeaponDef& d = *gb.def;
        // SuggestTossVelocity(target, src, TossStrength): the lower ballistic arc at that speed under world gravity
        // (native; exact solve here, 45 deg when out of reach) [PROV].
        core::Vec3 to = target - src;
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
        Projectile pr{src, vel, d.projDamage, d.projRadiusM, 1e9f, d.projDamageType ? d.projDamageType : "", player};
        pr.grenade = true; pr.explodeOnPawn = d.explodeOnPawn; pr.gravityScale = d.gravityScale; pr.bounce = d.bounce;
        pr.fuseMin = d.fuseMin; pr.fuseMax = d.fuseMax;
        pr.visual = projectileVisualFor(d.id);
        pr.yaw0 = std::atan2(-vel.x, -vel.z);
        pr.pitch0 = std::atan2(vel.y, std::hypot(vel.x, vel.z));
        // RotationRate (Pitch -100000 rotator units/s, the TnProjectileDataGrenadeLauncher default these grenades' data
        // inherit) [CONF authored]; the tumble sign in mesh space follows the vehicle pitch convention [HIGH].
        const std::string gid = d.id;
        if (gid == "FlakGrenades" || gid == "FlashBangs" || gid == "HealGrenades") pr.spinRate = -100000.0f * 6.2831853f / 65536.0f;
        projectiles_.push_back(pr);
        projectileFxStart(projectiles_.back());
        LOG_INFO("grenade %s: |v| %.1f m/s pitch %.1f deg (aim %.1f)", d.id, core::length(vel), launchPitch, aimPitch);
}

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

void World::requestBarrier(int owner) {
    // TnAbilityBarrier for any participant: SpawnDelay 0.5 -> SpawnBarrier; one barrier per owner (the cooldown waits for it).
    BarrierState b; b.owner = owner; b.delay = 0.5f;
    barriers_.push_back(b);
    if (Character* pc = participantPawnMutable(owner)) pc->barrierAlive_ = true;
}

void World::spawnBarrier(BarrierState& slot) {
    Character* pcp = participantPawnMutable(slot.owner);
    if (!pcp) return;                                       // IsOwnerDead
    Character& pc = *pcp;
    if (!barrierModelTried_) {
        barrierModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_Shield_p/Barrier/";
        if (assets::loadSkinnedGlb(ext + "WEP_Barrier_SKEL.gltf", barrierModel_) && barrierModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_Barrier_ANIM.anim.gltf", barrierModel_);
            resolveModelTextures(barrierModel_);
        } else LOG_ERROR("barrier: WEP_Barrier_SKEL unavailable (collision only)");
    }
    BarrierState& b = slot;
    const int owner = b.owner, dyn = b.dyn, dynW = b.dynW;
    b = BarrierState{};
    b.owner = owner; b.dyn = dyn; b.dynW = dynW;
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
                assets::skinPose(barrierModel_, lp, g, b.mesh);
            }
            if ((size_t)node < g.size()) bone = core::Vec3{g[(size_t)node].m[12], g[(size_t)node].m[13], g[(size_t)node].m[14]};
        }
    }
    const core::Vec3 centre = bone + core::Vec3{-0.59f, 0.91f, 0.0f};
    b.half = core::Vec3{0.835f, 4.1175f, 8.68f};          // (X 167, Z 823.5, Y 1736) / 2 in mesh axes (fwd, up, right)
    b.boxInv = core::Mat4::translate(centre * -1.0f) * core::Mat4::rotateY(-(b.yaw + core::config::kMeshYawOffset)) * core::Mat4::translate(b.pos * -1.0f);
    const std::vector<core::Vec3> tris = boxTris(centre, b.half);
    if (b.dyn < 0 && !freeBarrierDyn_.empty()) { b.dyn = freeBarrierDyn_.back(); freeBarrierDyn_.pop_back(); collision_.setDynamicPose(b.dyn, b.world); collision_.setDynamicEnabled(b.dyn, true); }
    if (b.dynW < 0 && !freeBarrierDynW_.empty() && weaponCollision_.valid()) { b.dynW = freeBarrierDynW_.back(); freeBarrierDynW_.pop_back(); weaponCollision_.setDynamicPose(b.dynW, b.world); weaponCollision_.setDynamicEnabled(b.dynW, true); }
    if (b.dyn < 0) b.dyn = collision_.addDynamicSet(tris, b.world); else { collision_.setDynamicPose(b.dyn, b.world); collision_.setDynamicEnabled(b.dyn, true); }
    if (weaponCollision_.valid()) {
        if (b.dynW < 0) b.dynW = weaponCollision_.addDynamicSet(tris, b.world);
        else { weaponCollision_.setDynamicPose(b.dynW, b.world); weaponCollision_.setDynamicEnabled(b.dynW, true); }
    }
    LOG_INFO("ability Barrier: wall at (%.1f %.1f %.1f), 1000 HP", b.pos.x, b.pos.y, b.pos.z);
}

void World::tickBarrier(float dt) {
    for (BarrierState& b : barriers_) {
        if (b.delay >= 0.0f) { b.delay -= dt; if (b.delay < 0.0f) spawnBarrier(b); }
        if (b.alive && !participantPawn(b.owner)) b.alive = false;   // destroyed on the owner's death
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
        if (!b.alive && b.delay < 0.0f) {                      // gone: its collision sets return to the pool
            if (b.dyn >= 0) { collision_.setDynamicEnabled(b.dyn, false); freeBarrierDyn_.push_back(b.dyn); b.dyn = -1; }
            if (b.dynW >= 0) { weaponCollision_.setDynamicEnabled(b.dynW, false); freeBarrierDynW_.push_back(b.dynW); b.dynW = -1; }
        }
        if (b.alive && barrierModel_.valid()) {
            int clip = barrierModel_.clipByName("Barrier_Equip");
            assets::LocalPose lp; std::vector<core::Mat4> g;
            if (clip >= 0) { assets::samplePose(barrierModel_, clip, b.t, false, lp); assets::skinPose(barrierModel_, lp, g, b.mesh); }
        }
    }
    // A barrier that went away stays one step with alive=false for presentation consumers, then goes.
    for (BarrierState& b : barriers_) if (!b.alive && b.delay < 0.0f) ++b.deadTicks;
    barriers_.erase(std::remove_if(barriers_.begin(), barriers_.end(), [](const BarrierState& b) { return b.deadTicks >= 2; }), barriers_.end());
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (Character* pc = participantPawnMutable((int)i)) {
            bool any = false;
            for (const BarrierState& b : barriers_) any |= b.owner == (int)i;
            pc->barrierAlive_ = any;
        }
}

const World::BarrierState& World::barrier() const {
    for (const BarrierState& b : barriers_) if (b.owner == localPlayer_ && b.alive) return b;
    static const BarrierState none;
    return none;
}

bool World::barrierRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const {
    bool any = false; float best = range;
    lastBarrierHit_ = -1;
    for (size_t i = 0; i < barriers_.size(); ++i) {
        const BarrierState& b = barriers_[i];
        if (!b.alive) continue;
        const core::Vec3 lo = core::transformPoint(b.boxInv, o);
        const core::Vec3 ld = core::transformPoint(b.boxInv, o + d) - lo;
        float tt;
        if (rayAabb(lo, ld, best, b.half * -1.0f, b.half, tt) && tt <= best) { best = tt; any = true; lastBarrierHit_ = (int)i; }
    }
    if (any) t = best;
    return any;
}

void World::damageBarrier(float amount, const std::string& type) {
    int idx = lastBarrierHit_;
    if (idx < 0 || (size_t)idx >= barriers_.size())
        for (size_t i = 0; i < barriers_.size(); ++i) if (barriers_[i].owner == localPlayer_ && barriers_[i].alive) idx = (int)i;
    lastBarrierHit_ = -1;
    if (idx >= 0) damageBarrierAt((size_t)idx, amount, type);
}

void World::damageBarrierAt(size_t idx, float amount, const std::string& type) {
    if (idx >= barriers_.size()) return;
    BarrierState& b = barriers_[idx];
    if (!b.alive || b.fade >= 0.0f) return;
    if (type.find("Melee") != std::string::npos || type.find("Whirlwind") != std::string::npos) return;   // ignores melee
    b.health -= amount;
    if (b.health <= 0.0f) { b.health = 0.0f; b.fade = 3.0f; }
}

// ---- Ammo beacon [CONF TnAbilitySpawnInventory / TnAbilitySpawnAmmoCrate / TnDroppedPickupAmmoBeacon / TnDroppedPickupDefrag
// script + authored CDOs] ----
// SpawnDelay 0.5 -> DropFrom(owner Location, TossVelocity (2000, 1200, 0) rotated by the owner) -> falls and lands.
// BeaconLifespan 60 s; the owner dead -> FadeOut. Pickup.Tick: every pawn within Radius 1500 UU that VisibleCollidingActors
// finds (owner or same team): current weapon FillReserveAmmo when not full, TnBuffAmmoBeaconIncreaseDamage (x1.15, 1 s,
// reset while in range). Health 100: damage from the owner or the owner's team is ignored. TnAmmoBeacon.PickupAllowed false.
// Cooldown[0] 60 s once the beacon is gone (ServerCanStartCooldown). Skill gifts / grenades not applied (no skills in MP).
// FadeOut duration not applied: removal is immediate [PARTIAL].
void World::requestAmmoBeacon(int owner) {
    // TnAbilitySpawnAmmoCrate for any participant: SpawnDelay 0.5 -> SpawnInventory; one beacon per owner (cooldown waits for it).
    AmmoBeacon b; b.owner = owner; b.delay = 0.5f;
    beacons_.push_back(b);
    if (Character* pc = participantPawnMutable(owner)) pc->beaconAlive_ = true;
}

void World::tickAmmoBeacon(float dt) {
    auto tickBuff = [dt](Character& p) {
        p.beaconDamageBuff_ = std::max(0.0f, p.beaconDamageBuff_ - dt);
        p.seeEnemiesRemain_ = std::max(0.0f, p.seeEnemiesRemain_ - dt);
        p.hardLockedRemain_ = std::max(0.0f, p.hardLockedRemain_ - dt);
        p.refillOnKillRemain_ = std::max(0.0f, p.refillOnKillRemain_ - dt);
        p.jammedRemain_ = std::max(0.0f, p.jammedRemain_ - dt);
    };
    tickBuff(player_.pawn());
    for (MatchOpponent* o : opponents_) tickBuff(o->pawn());
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (AmmoBeacon& b : beacons_) {
        const Character* owner = participantPawn(b.owner);
        if (b.delay >= 0.0f) {
            b.delay -= dt;
            if (b.delay < 0.0f && owner) {
                const core::Vec3 f = core::forwardFromYawPitch(owner->yaw(), 0.0f), r{-f.z, 0.0f, f.x};
                b.alive = true; b.landed = false; b.pos = owner->actorLocation(); b.vel = f * 20.0f + r * 12.0f;
                b.life = 60.0f; b.health = 100.0f;
                LOG_INFO("ability SpawnAmmoCrate (p%d): beacon dropped", b.owner);
            }
        }
        if (b.alive) {
            b.life -= dt;
            if (b.life <= 0.0f || !owner || b.health <= 0.0f) b.alive = false;   // lifespan / the owner dead / destroyed
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
        if (!b.alive) continue;
        // Pickup.Tick: the owner and the owner's team within 15 m in sight.
        const core::Vec3 eye = b.pos + core::Vec3{0, 0.5f, 0};
        const bool teamGame = matchActive_ && match_.settings().teamGame;
        for (size_t i = 0; i < match_.players().size(); ++i) {
            const int p = (int)i;
            if (p != b.owner && !(teamGame && match_.sameTeam(p, b.owner))) continue;
            Character* c = participantPawnMutable(p);
            if (!c || core::length(c->actorLocation() - b.pos) > 15.0f) continue;
            float t;
            if (line && line->segmentHit(eye, c->actorLocation(), t)) continue;   // VisibleCollidingActors
            Weapon& w = c->weapon();
            if (w.reserve < w.reserveMax) w.reserve = w.reserveMax;              // FillReserveAmmo
            c->beaconDamageBuff_ = 1.0f;                                          // AddBuff / ResetBuffTime
        }
    }
    // Ended beacons stay one step with alive=false (presentation consumers), then go.
    for (AmmoBeacon& b : beacons_) if (!b.alive && b.delay < 0.0f) ++b.deadTicks;
    beacons_.erase(std::remove_if(beacons_.begin(), beacons_.end(), [](const AmmoBeacon& b) { return b.deadTicks >= 2; }), beacons_.end());
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (Character* pc = participantPawnMutable((int)i)) {
            bool any = false;
            for (const AmmoBeacon& b : beacons_) any |= b.owner == (int)i && (b.alive || b.delay >= 0.0f);
            pc->beaconAlive_ = any;
        }
}

const World::AmmoBeacon& World::localBeacon() const {
    for (const AmmoBeacon& b : beacons_) if (b.owner == localPlayer_ && b.alive) return b;
    static const AmmoBeacon none;
    return none;
}

void World::damageAmmoBeacon(float amount, int instigator) {
    // Direct damage targets the local player's beacon (tests); radius damage uses damageAmmoBeaconAt per beacon.
    for (size_t i = 0; i < beacons_.size(); ++i) if (beacons_[i].owner == localPlayer_ && beacons_[i].alive) { damageAmmoBeaconAt(i, amount, instigator); return; }
}

void World::damageAmmoBeaconAt(size_t idx, float amount, int instigator) {
    if (idx >= beacons_.size()) return;
    AmmoBeacon& b = beacons_[idx];
    if (!b.alive || instigator < 0 || instigator == b.owner) return;                          // the owner's damage is ignored
    if (matchActive_ && match_.settings().teamGame && match_.sameTeam(instigator, b.owner)) return;   // and its team's
    b.health -= amount;
}

// ---- Sentry [CONF TnAbilitySpawnSentry / TnSentryPawnAbility / TnAiSentryController script + authored Default_TURRETDEF /
// Default_WEPDATA / Sentry_DSYS; RE TARGETED_PASS3 §J] ----
// Spawn: SpawnDelay 0.2; desired = owner + (0, 0, SpawnHeight 375) rotated, clamped by a trace from the owner; the sentry pawn
// then settles on the floor below [HIGH pawn falling]. Health 135 (Sentry_DSYS), drained to 0 over Lifetime 30 s; owner
// damage ignored; melee kills it; dies with the owner; one per owner. Targeting: the closest visible enemy (SightRadius
// 30000, 360 deg) within pitch -45..45; YawPitchControl LagDegreesPerSecond 270; fires while aimed within
// AimedAtTargetThreshold 3 deg and closer than MaxAttackRange 6000: instant hit 8 x RangeDamageModifiers (steps: 1.0 to 8000,
// else 0.5) every 0.12 s with PerShotSpread 0.1; heat +2 per shot to HeatMax 100, OverheatDelay 2 s (heat then
// reset [PROV: lose-heat rate native]). Kill credit to the owner (_KillOwner). Cooldown 60 s once the sentry is gone.
// PARTIAL: flashbang dormancy, Rocket / Repair blueprints (skills), turret pitch on the mesh, corpse (LifeSpanAfterDeath 5).
void World::requestSentry(int owner) {
    // TnAbilitySpawnSentry.ServerTriggerAbility: one sentry per owner (ActiveSentry.Kill()), SpawnDelay 0.2.
    for (Sentry& s : sentries_) if (s.owner == owner) { s.alive = false; s.delay = -1.0f; }
    Sentry s; s.owner = owner; s.delay = 0.2f;
    sentries_.push_back(s);
    if (Character* pc = participantPawnMutable(owner)) pc->sentryAlive_ = true;
}

void World::spawnSentry(Sentry& s) {
    Character* pcp = participantPawnMutable(s.owner);
    if (!pcp) return;
    const Character& pc = *pcp;
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
    s.alive = true; s.pos = p; s.yaw = pc.yaw(); s.health = 135.0f; s.t = 0.0f; s.target = -1;
    LOG_INFO("ability SpawnSentry (p%d): sentry at (%.1f %.1f %.1f)", s.owner, p.x, p.y, p.z);
}

void World::tickSentry(float dt) {
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (Sentry& s : sentries_) {
        if (s.delay >= 0.0f) { s.delay -= dt; if (s.delay < 0.0f) spawnSentry(s); }
        const bool ownerAlive = s.owner >= 0 && participantPawn(s.owner) != nullptr;
        if (s.alive) {
            s.t += dt;
            s.health -= 135.0f / 30.0f * dt;              // Lifetime 30
            if (s.health <= 0.0f || !ownerAlive) { s.alive = false; s.target = -1; }   // dies with the owner
        }
        if (!s.alive) continue;
        const core::Vec3 muzzle = s.pos + core::Vec3{0, 2.0f, 0};
        // Target: the closest visible enemy of the owner's team within the pitch constraints (every participant, the local pawn too).
        s.target = -1;
        float best = 300.0f;
        const Character* tgt = nullptr;
        if (matchActive_)
            for (size_t i = 0; i < match_.players().size(); ++i) {
                const int p = (int)i;
                if (p == s.owner || (match_.settings().teamGame && match_.sameTeam(p, s.owner))) continue;
                const Character* c = participantPawn(p);
                if (!c) continue;
                core::Vec3 d = c->actorLocation() - muzzle;
                float dist = core::length(d);
                if (dist > best || dist < 1e-3f) continue;
                if (std::fabs(std::atan2(d.y, std::hypot(d.x, d.z))) > 45.0f * 0.0174533f) continue;
                float tt;
                if (line && line->segmentHit(muzzle, c->actorLocation(), tt)) continue;
                best = dist; s.target = p; tgt = c;
            }
        float wantYaw = s.yaw, wantPitch = 0.0f;   // idle: pitch returns to 0
        if (tgt) {
            core::Vec3 d = tgt->actorLocation() - muzzle;
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
            float dist = core::length(tgt->actorLocation() - muzzle);
            if (err <= 3.0f * 0.0174533f && dist < 60.0f) {
                s.fireTimer = 0.12f;
                ++s.shots;
                s.heat += 2.0f;
                if (s.heat >= 100.0f) s.overheat = 2.0f;
                core::Vec3 dir = core::forwardFromYawPitch(s.yaw, s.pitch);
                core::Vec3 up{0, 1, 0}, rt = core::normalize(core::cross(dir, up)), u2 = core::cross(rt, dir);
                auto rf = []() { return core::simRandSigned(); };
                dir = core::normalize(dir + rt * (rf() * 0.1f) + u2 * (rf() * 0.1f));
                float wall = 300.0f, tw;
                if (line && line->segmentHit(muzzle, muzzle + dir * 300.0f, tw)) wall = 300.0f * tw;
                int hit = -1; float hd = wall;
                for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(muzzle, dir, wall, th) && th < hd) { hd = th; hit = o->matchPlayer(); } }
                if (!localDead_ && localPlayer_ != s.owner) { float th; if (MatchOpponent::pawnRayHit(player_.pawn(), muzzle, dir, wall, th) && th < hd) { hd = th; hit = localPlayer_; } }
                if (hit >= 0 && !(match_.settings().teamGame && match_.sameTeam(hit, s.owner)) && hit != s.owner) {
                    const float mod = hd <= 80.0f ? 1.0f : 0.5f;   // GetRangeDamageModifier steps (1.0 to 8000 UU, else 0.5) [CONF script]
                    applyMatchDamage(hit, s.owner, 8.0f * mod, false, "TransGame.TnDamageTypeSentry");   // kill credit to the owner
                }
            }
        }
        if (sentryModel_.valid()) {
            int clip = sentryModel_.clipByName("WEP_DeployedTurret_Activate");
            if (clip >= 0) { assets::LocalPose lp; std::vector<core::Mat4> g; assets::samplePose(sentryModel_, clip, s.t, false, lp); assets::skinPose(sentryModel_, lp, g, s.mesh); }
        }
    }
    // Owners' SpawnSentry cooldown waits for their sentry (pending or alive).
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (Character* pc = participantPawnMutable((int)i)) {
            bool any = false;
            for (const Sentry& s : sentries_) any |= s.owner == (int)i && (s.alive || s.delay >= 0.0f);
            pc->sentryAlive_ = any;
        }
    // A sentry that died stays one step with alive=false (Systems: SENTRY_EXPL on the reported death), then goes.
    for (Sentry& s : sentries_) if (!s.alive && s.delay < 0.0f) ++s.deadTicks;
    sentries_.erase(std::remove_if(sentries_.begin(), sentries_.end(), [](const Sentry& s) { return s.deadTicks >= 2; }), sentries_.end());
}

const World::Sentry& World::sentry() const {
    for (const Sentry& s : sentries_) if (s.owner == localPlayer_) return s;
    static const Sentry none;
    return none;
}

bool World::sentryRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const {
    // CollisionCylinder radius 200 / height 200 UU about the base + 2 m [HIGH: the DSYS mesh physics is the hit volume].
    bool any = false; float best = range;
    lastSentryHit_ = -1;
    for (size_t i = 0; i < sentries_.size(); ++i) {
        const Sentry& s = sentries_[i];
        if (!s.alive) continue;
        const core::Vec3 c = s.pos + core::Vec3{0, 2.0f, 0};
        float ox = o.x - c.x, oz = o.z - c.z, a = d.x * d.x + d.z * d.z, b = 2.0f * (ox * d.x + oz * d.z), cc = ox * ox + oz * oz - 4.0f;
        if (a < 1e-8f) continue;
        float disc = b * b - 4.0f * a * cc;
        if (disc < 0.0f) continue;
        float tt = std::max(0.0f, (-b - std::sqrt(disc)) / (2.0f * a));
        if (tt > best) continue;
        float y = o.y + d.y * tt;
        if (y < c.y - 2.0f || y > c.y + 2.0f) continue;
        best = tt; any = true; lastSentryHit_ = (int)i;
    }
    if (any) t = best;
    return any;
}

void World::damageSentry(float amount, int instigator, const std::string& type) {
    // The sentry the last sentryRayHit found; without one, the local player's (tests / radius callers use damageSentryAt).
    int idx = lastSentryHit_;
    if (idx < 0 || (size_t)idx >= sentries_.size())
        for (size_t i = 0; i < sentries_.size(); ++i) if (sentries_[i].owner == localPlayer_ && sentries_[i].alive) idx = (int)i;
    lastSentryHit_ = -1;
    if (idx >= 0) damageSentryAt((size_t)idx, amount, instigator, type);
}

void World::damageSentryAt(size_t idx, float amount, int instigator, const std::string& type) {
    if (idx >= sentries_.size()) return;
    Sentry& s = sentries_[idx];
    if (!s.alive || instigator == s.owner) return;                                                           // the owner cannot damage it
    if (instigator >= 0 && matchActive_ && match_.settings().teamGame && match_.sameTeam(instigator, s.owner)) return;   // nor its team
    if (type.find("Melee") != std::string::npos || type.find("Whirlwind") != std::string::npos) { s.health = 0.0f; return; }   // melee kills
    s.health -= amount;
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
void World::requestRoller(int owner) {
    // TnAbilityRollerSphere for any participant: SpawnDelay 0.5; one per owner (the cooldown waits for it).
    RollerMine m; m.owner = owner; m.delay = 0.5f;
    rollers_.push_back(m);
    if (owner == localPlayer_) player_.pawn().rollerAlive_ = true;
    else if (Character* pc = participantPawnMutable(owner)) pc->rollerAlive_ = true;
}

const World::RollerMine& World::rollerMine() const {
    for (const RollerMine& m : rollers_) if (m.owner == localPlayer_ && (m.alive || m.delay >= 0.0f)) return m;
    static const RollerMine none;
    return none;
}

void World::explodeRollerAt(size_t idx) {
    if (idx >= rollers_.size() || !rollers_[idx].alive) return;
    RollerMine& m = rollers_[idx];
    m.alive = false;
    radiusDamage(m.pos, 135.0f, 15.0f, m.owner, "TransGame.TnDamageTypeRollerMine");
    LOG_INFO("roller sphere (p%d) exploded at (%.1f %.1f %.1f) t %.2f", m.owner, m.pos.x, m.pos.y, m.pos.z, m.t);
}

void World::explodeRollerMine() {   // the local player's (tests / input)
    for (size_t i = 0; i < rollers_.size(); ++i) if (rollers_[i].owner == localPlayer_ && rollers_[i].alive) { explodeRollerAt(i); return; }
}

void World::damageRollerAt(size_t idx, float amount, int instigator) {
    if (idx >= rollers_.size()) return;
    RollerMine& m = rollers_[idx];
    if (!m.alive || instigator < 0 || instigator == m.owner) return;
    if (matchActive_ && match_.settings().teamGame && match_.sameTeam(instigator, m.owner)) return;
    m.health -= amount;
    if (m.health <= 0.0f) explodeRollerAt(idx);
}

void World::damageRollerMine(float amount, int instigator) {   // the local player's (tests)
    for (size_t i = 0; i < rollers_.size(); ++i) if (rollers_[i].owner == localPlayer_ && rollers_[i].alive) { damageRollerAt(i, amount, instigator); return; }
}

void World::tickRollerMine(float dt) {
    auto tickBuff = [dt](Character& p) { p.rollerSlowRemain_ = std::max(0.0f, p.rollerSlowRemain_ - dt); };
    tickBuff(player_.pawn());
    for (MatchOpponent* o : opponents_) tickBuff(o->pawn());
    const float R = 1.21f;
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (size_t idx = 0; idx < rollers_.size(); ++idx) {
        RollerMine& m = rollers_[idx];
        const Character* owner = m.owner == localPlayer_ ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(m.owner);
        if (m.delay >= 0.0f) {
            m.delay -= dt;
            if (m.delay < 0.0f && owner) {
                const core::Vec3 f = core::forwardFromYawPitch(owner->yaw(), 0.0f);
                const core::Vec3 spot = owner->actorLocation() + f * 5.0f + core::Vec3{0, 1.0f, 0};
                float t; core::Vec3 n;
                bool safe = collision_.valid() && !collision_.segmentHit(owner->actorLocation(), spot + f * (R * 1.1f), t, n);
                if (safe) {
                    m.alive = true; m.pos = spot; m.vel = f * 27.5f; m.health = 200.0f; m.t = 0.0f; m.onGround = false;
                    LOG_INFO("ability RollerSphere (p%d): spawned", m.owner);
                } else m.delay = 1.0f;   // SpawnLocationValidator failed: retry every 1 s
            }
        }
        if (m.alive && !owner) m.alive = false;   // destroyed with the owner
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
        if (m.alive && matchActive_) {
            const bool armed = m.t >= 3.0f;
            bool boom = m.t >= 10.0f;                                  // _Fuse
            const bool teamGame = match_.settings().teamGame;
            for (size_t pi = 0; pi < match_.players().size(); ++pi) {   // the owner's enemies (the local pawn included)
                const int p = (int)pi;
                if (p == m.owner || (teamGame && match_.sameTeam(p, m.owner))) continue;
                Character* e = participantPawnMutable(p);
                if (!e) continue;
                const core::Vec3 d = e->actorLocation() - m.pos;
                if (core::length(d) <= 15.0f) {
                    float tt;
                    if (!line || !line->segmentHit(m.pos, e->actorLocation(), tt))
                        e->rollerSlowRemain_ = std::max(e->rollerSlowRemain_, e->moveForm() == Form::Vehicle ? 2.0f : 1.0f);
                }
                const float r = e->cylinderRadius(e->moveForm()), hh = e->cylinderHalfHeight(e->moveForm());
                if (armed && std::hypot(d.x, d.z) <= r + R && std::fabs(d.y) <= hh + R) boom = true;   // RB contact
            }
            if (boom) explodeRollerAt(idx);
        }
    }
    // Ended rollers stay one step with alive=false (Systems' audio reports the end), then go.
    for (RollerMine& m : rollers_) if (!m.alive && m.delay < 0.0f) ++m.deadTicks;
    rollers_.erase(std::remove_if(rollers_.begin(), rollers_.end(), [](const RollerMine& m) { return m.deadTicks >= 2; }), rollers_.end());
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (Character* pc = participantPawnMutable((int)i)) {
            bool any = false;
            for (const RollerMine& m : rollers_) any |= m.owner == (int)i && (m.alive || m.delay >= 0.0f);
            pc->rollerAlive_ = any;
        }
    if (!matchActive_) {   // outside a match (sandbox): the local pawn's flag only
        bool any = false;
        for (const RollerMine& m : rollers_) any |= m.alive || m.delay >= 0.0f;
        player_.pawn().rollerAlive_ = any;
    }
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

// Energon Repair Ray [CONF TnWeaponRepair / TnWeaponBeam script + RepairBeam_WEPDATA]: every fire interval (0.1 s) the beam
// traces WeaponRange 3500 UU from the eye along the aim. A teammate hit is healed HealthPerSecond 60 x RepairRateModifier (no buffs:
// x1) x interval with TnHealTypeRepairTeam (no SegmentedHealType: across segments [HIGH]); any other pawn takes DamagePerSecond 60 x
// interval of TnDamageTypeRepairEnemy. HeatMax 0: no overheat [HIGH]. PlayerTargeting.GetRepairTarget lock-on assist (the trace is
// redirected to a picked teammate) is not recovered: the beam follows the crosshair [PARTIAL].
void World::fireRepairBeamImpl(const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn) {
    const core::Vec3 dir = core::normalize(dirIn);
    const float range = w.rangeM > 0.0f ? w.rangeM : 35.0f;
    const float tickSecs = w.fireInterval > 0.0f ? w.fireInterval : 0.1f;
    core::Vec3 dirLock = dir; bool locked = false;
    float best = range;
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    float t;
    if (line && line->segmentHit(origin, origin + dir * range, t)) best = range * t;
    MatchOpponent* hit = nullptr;
    // PlayerTargeting.GetRepairTarget [CONF RE answer 2026-10-06]: PickedTarget slot 6 while holding TnWeaponRepair - within the
    // weapon trace range, inside the target's TnTargetableComponent picker 6 "Repair" (Angle 4 deg, MinRadius 200, MaxRadius 400
    // UU, the angle widened by GetAdjustedTargetAngle with distance), NO team filter (CanTargetTeammates; enemies too). While a
    // target exists, GetEndTrace returns its TargetableLocation: the beam locks on even with the crosshair slightly off.
    // GetAdjustedTargetAngle is taken as max(Angle, atan(radius / distance)) with the radius clamped to [Min, Max] [HIGH];
    // TargetableLocation as the pawn centre [HIGH].
    {
        float bestAng = 1e9f; MatchOpponent* pick = nullptr; core::Vec3 pickAt{0, 0, 0};
        for (MatchOpponent* o : opponents_) {
            if (!o->spawned() || o->health().isDead()) continue;
            const core::Vec3 at = o->pawn().actorLocation();
            const core::Vec3 to = at - origin; const float d = core::length(to);
            if (d < 0.1f || d > range) continue;
            const float radius = core::clampf(d * 0.1f, 2.0f, 4.0f);
            const float allowed = std::max(4.0f * 0.0174533f, std::atan(radius / d));
            const float ang = std::acos(core::clampf(core::dot(to * (1.0f / d), dir), -1.0f, 1.0f));
            if (ang > allowed || ang >= bestAng) continue;
            float tl;
            if (line && line->segmentHit(origin, at, tl)) continue;   // no line of sight
            bestAng = ang; pick = o; pickAt = at;
        }
        if (pick) { hit = pick; best = core::length(pickAt - origin); dirLock = core::normalize(pickAt - origin); locked = true; }
    }
    if (!locked)
        for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(origin, dir, best, th) && th < best) { best = th; hit = o; } }
    repairBeam_.active = true; repairBeam_.time = tickSecs * 1.5f;
    repairBeam_.locked = locked;
    // Ribbon start = the muzzle (as the hitscan tracer); the damage trace itself starts on the crosshair ray (origin).
    core::Vec3 muzzle = origin;
    { core::Mat4 ms;
      if (weaponSocketWorld("MuzzleFlash", ms)) muzzle = {ms.m[12], ms.m[13], ms.m[14]};
      else if (player_.pawn().hasWeapon())
          muzzle = core::transformPoint(player_.pawn().weaponWorld(), core::Vec3{core::config::kMuzzleLocalX, core::config::kMuzzleLocalY, core::config::kMuzzleLocalZ}); }
    repairBeam_.start = muzzle; repairBeam_.end = origin + (locked ? dirLock : dir) * best;
    // One impact squib per beam tick where the beam meets something (OnPlayFireEffects at the fire interval).
    repairSquibPending_ = hit != nullptr || best < range - 0.01f;
    repairSquibHealing_ = hit && matchActive_ && match_.sameTeam(hit->matchPlayer(), localPlayer_); repairBeam_.target = hit ? hit->matchPlayer() : -1;
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

// ---- DEV / QA TOOLING (F10 panel; Frontend owns the buttons): bots ----
void World::qaKillAllBots() {
    if (!qaEnabled() || !matchActive_) return;
    for (MatchOpponent* o : opponents_) {
        const int p = o->matchPlayer();
        if (!o->spawned() || match_.players()[(size_t)p].kind != ParticipantKind::Bot) continue;
        const Match::KillContext kc = killContext(p, p, "Engine.DmgType_Suicided");
        match_.killed(p, p, true, "Engine.DmgType_Suicided", &kc);   // DmgType_Suicided: no score change; the normal respawn wave
        o->despawn();
    }
}

void World::qaFreezeBots(bool on) { if (qaEnabled()) qaBotsFrozen_ = on; }
void World::qaSetBotOverlay(bool on) { if (qaEnabled()) qaBotOverlay_ = on; }

void World::qaTeleportToAim() {
    if (!qaEnabled() || localPlayerDead()) return;
    Character& pc = player_.pawn();
    const core::Vec3 eye = pc.position() + core::Vec3{0, core::config::kCamHeight, 0};
    const core::Vec3 dir = core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch());
    const CollisionWorld* col = collision_.valid() ? &collision_ : nullptr;
    float t = 1.0f;
    const float range = 1000.0f;
    if (!col || !col->segmentHit(eye, eye + dir * range, t)) return;   // nothing under the crosshair
    core::Vec3 p = eye + dir * (range * t) - dir * 2.5f;                 // a pawn radius back along the ray
    float gy; core::Vec3 gn;
    if (col->groundHeight(p.x, p.z, p.y + 1.0f, 6.0f, gy, gn)) p.y = gy;
    pc.setPosition(p); pc.velocity() = {0, 0, 0}; pc.groundY = p.y;
    LOG_INFO("qa: teleported to aim (%.1f %.1f %.1f)", p.x, p.y, p.z);
}

std::vector<World::QaBotLabel> World::qaBotLabels() const {
    std::vector<QaBotLabel> out;
    if (!qaEnabled() || !qaBotOverlay_) return out;
    for (const BotBrain& b : bots_) {
        const Character* c = participantPawn(b.player);
        if (!c) continue;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s [%s] %s tgt %d wp %zu/%zu%s", match_.players()[(size_t)b.player].name.c_str(), botDifficultyName(b.difficulty),
                      botGoalName(b.goal.kind), b.target, b.wp, b.path.size(), b.stuckLevel > 0 ? " STUCK" : "");
        out.push_back({c->position() + core::Vec3{0, 5.0f, 0}, buf, b.player});
    }
    return out;
}

void World::drawQaBotOverlay(render::IRenderer& r) const {
    if (!qaEnabled() || !qaBotOverlay_) return;
    for (const BotBrain& b : bots_) {
        const Character* c = participantPawn(b.player);
        if (!c) continue;
        const core::Vec3 head = c->position() + core::Vec3{0, 4.5f, 0};
        if (const Character* tp = b.target >= 0 ? participantPawn(b.target) : nullptr)
            r.drawLine(head, tp->position() + core::Vec3{0, 2.0f, 0}, {1.0f, 0.25f, 0.2f});            // line to its target
        core::Vec3 prev = c->position() + core::Vec3{0, 0.3f, 0};
        for (size_t k = b.wp; k < b.path.size(); ++k) {                                                   // nav path from the current waypoint
            const core::Vec3 q = b.path[k].pos + core::Vec3{0, 0.3f, 0};
            r.drawLine(prev, q, k == b.wp ? core::Vec3{1.0f, 1.0f, 0.2f} : core::Vec3{0.2f, 0.9f, 1.0f});
            prev = q;
        }
        if (b.wp < b.path.size()) {                                                                         // current waypoint marker
            const core::Vec3 w = b.path[b.wp].pos;
            r.drawLine(w, w + core::Vec3{0, 3.0f, 0}, {1.0f, 1.0f, 0.2f});
        }
    }
}

void World::qaRespawn() {
    if (!qaEnabled() || !matchActive_) return;
    killLocalPlayer(localPlayer_, true);   // DmgType_Suicided: no score change; the match's own respawn wave brings the pawn back
}

void World::qaSetCharacter(const CharacterSelection& sel) {
    if (!qaEnabled() || !matchActive_ || localPlayer_ < 0) return;
    preloadSelections({sel});                          // no first-use load on the respawn frame
    match_.selectCharacter(localPlayer_, sel);
    if (!localDead_) killLocalPlayer(localPlayer_, true);
}

std::vector<CharacterSelection> World::qaCharacterChoices() const {
    if (!qaEnabled()) return {};
    return qaCharacterChoicesAlways();
}

std::vector<CharacterSelection> World::qaCharacterChoicesAlways() const {
    std::vector<CharacterSelection> out;
    for (int sp = 0; sp < 4; ++sp) {
        CharacterSelection c; c.type = 0; c.specialty = (Specialty)sp;
        c.chassisByFaction[0] = defaultChassis(c.specialty, 0); c.chassisByFaction[1] = defaultChassis(c.specialty, 1);
        c.weapons = classPresetList(specialtyName(c.specialty), "weapons");
        c.vehicleWeapons = classPresetList(specialtyName(c.specialty), "vehicle_weapons");
        c.melee = classPresetList(specialtyName(c.specialty), "melee");
        c.abilities = classPresetList(specialtyName(c.specialty), "abilities");
        c.customSlot = specialtyName(c.specialty);
        out.push_back(c);
    }
    return out;
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
        // One entry per projectile class (WeaponProjectiles by fire mode; PlasmaCannon Charge1 / 2 / 3): class k > 0 is keyed
        // "<id>#<k>" (Weapon::projClass).
        for (size_t k = 0; k < ps.size(); ++k) {
            const assets::Json& v = ps[k]["projectile_visual"];
            ProjectileVisual pv;
            pv.weapon = k == 0 ? std::string(d.id) : std::string(d.id) + "#" + std::to_string(k);
            pv.flight = v["flight_effect"]["template"].asString();
            pv.explosion = v["explosion_effect"]["template"].asString();
            const std::string gltf = ps[k]["body_mesh"]["gltf"].asString();
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

// The materials of a robot.glb without parsing its ~19 MB of clip JSON: read only the JSON chunk, cut out the top-level
// "materials" / "textures" / "images" arrays (string-aware bracket scan) and parse those with the same parseGltfMaterial.
static bool glbTopLevelArray(const std::string& js, const char* key, std::string& out) {
    const std::string k = std::string("\"") + key + "\"";
    int depth = 0; bool inStr = false;
    for (size_t i = 0; i < js.size(); ++i) {
        const char c = js[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') {
            if (depth == 1 && js.compare(i, k.size(), k) == 0) {
                size_t j = js.find_first_not_of(" \t\r\n:", i + k.size());
                if (j == std::string::npos || js[j] != '[') return false;
                int d = 0; bool s2 = false;
                for (size_t e = j; e < js.size(); ++e) {
                    const char ch = js[e];
                    if (s2) { if (ch == '\\') ++e; else if (ch == '"') s2 = false; continue; }
                    if (ch == '"') s2 = true;
                    else if (ch == '[' || ch == '{') ++d;
                    else if (ch == ']' || ch == '}') { if (--d == 0) { out = js.substr(j, e - j + 1); return true; } }
                }
                return false;
            }
            inStr = true;
            continue;
        }
        if (c == '{' || c == '[') ++depth; else if (c == '}' || c == ']') --depth;
    }
    return false;
}

static bool glbMaterialsOnly(const std::string& path, std::vector<render::Material>& mats) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t hdr[3] = {0, 0, 0}, ch[2] = {0, 0};
    f.read((char*)hdr, 12); f.read((char*)ch, 8);
    if (!f || hdr[0] != 0x46546C67u || ch[1] != 0x4E4F534Au) return false;   // 'glTF', chunk 'JSON'
    std::string js(ch[0], '\0');
    f.read(&js[0], (std::streamsize)ch[0]);
    if (!f) return false;
    std::string m, t, im;
    if (!glbTopLevelArray(js, "materials", m)) return false;
    glbTopLevelArray(js, "textures", t); glbTopLevelArray(js, "images", im);
    const std::string mini = "{\"materials\":" + m + ",\"textures\":" + (t.empty() ? "[]" : t) + ",\"images\":" + (im.empty() ? "[]" : im) + "}";
    assets::Json root;
    if (!assets::Json::parse(mini.data(), mini.size(), root)) return false;
    std::string dir; const size_t sl = path.find_last_of("/\\"); dir = sl == std::string::npos ? "." : path.substr(0, sl);
    const assets::Json& jm = root["materials"];
    mats.assign(jm.size(), render::Material{});
    for (size_t i = 0; i < jm.size(); ++i) assets::parseGltfMaterial(root, i, dir, mats[i]);
    return true;
}

const World::SharedAnimFile* World::sharedAnimFile(const std::string& path) {
    auto it = animFiles_.find(path);
    if (it != animFiles_.end()) return it->second.get();
    auto f = std::make_unique<SharedAnimFile>();
    f->ok = assets::loadAnimationFile(path, f->file);
    for (size_t i = 0; i < f->file.clips.size(); ++i) f->byName.emplace(f->file.clips[i].name, i);   // first clip of a name
    const SharedAnimFile* raw = f.get();
    animFiles_[path] = std::move(f);
    return raw;
}

bool World::loadRobotShared(const ChassisDef& def, assets::SkinnedModel& m) {
    if (def.robotSkelGltf.empty() || def.robotAnims.empty()) return false;
    const std::string ext = assetRoot() + "/../";
    if (!assets::loadSkinnedGlb(ext + def.robotSkelGltf, m) || !m.valid()) return false;
    m.clips.clear();
    // The exported robot.glb's materials (baked base colour / emissive / specular, wfc_material): same slots, same order.
    std::vector<render::Material> mats;
    if (!glbMaterialsOnly(ext + def.robotGlb, mats) || mats.size() != m.mats.size()) {
        LOG_WARN("shared anims: robot.glb materials unavailable for %s", def.id.c_str());
        return false;
    }
    m.mats = std::move(mats);
    // Per source file: file node -> model node by bone name (the matching loadAnimationsByName / appendAnimations use).
    std::map<const SharedAnimFile*, std::vector<int>> remaps;
    for (const ChassisDef::AnimRef& r : def.robotAnims) {
        const SharedAnimFile* f = sharedAnimFile(ext + r.source);
        if (!f || !f->ok) { LOG_WARN("shared anims: %s unavailable for %s", r.source.c_str(), def.id.c_str()); return false; }
        auto ci = f->byName.find(r.name);
        if (ci == f->byName.end()) { LOG_WARN("shared anims: clip %s not in %s (%s)", r.name.c_str(), r.source.c_str(), def.id.c_str()); return false; }
        std::vector<int>& remap = remaps[f];
        if (remap.empty()) {
            remap.assign(f->file.nodeNames.size(), -1);
            for (size_t i = 0; i < f->file.nodeNames.size(); ++i)
                for (size_t k = 0; k < m.nodeNames.size(); ++k)
                    if (m.nodeNames[k] == f->file.nodeNames[i]) { remap[i] = (int)k; break; }
        }
        const assets::AnimClip& src = f->file.clips[ci->second];
        assets::AnimClip clip;
        clip.name = r.name;
        clip.category = r.category;
        clip.additive = r.additive;
        clip.duration = src.duration;
        clip.samplers = src.samplers;
        for (const assets::AnimChannel& c : src.channels)
            if (c.node >= 0 && (size_t)c.node < remap.size() && remap[(size_t)c.node] >= 0) {
                assets::AnimChannel ch = c; ch.node = remap[(size_t)c.node]; clip.channels.push_back(ch);
            }
        m.clips.push_back(std::move(clip));
    }
    return true;
}

std::string World::compareRobotShared(const std::string& id, bool& ok) {
    ok = false;
    ChassisDef def;
    if (!loadChassisDef(assetRoot(), id, def)) return id + ": no chassis def";
    const std::string ext = assetRoot() + "/../";
    assets::SkinnedModel A, B;
    const double t0 = profNowMs();
    if (!assets::loadSkinnedGlb(ext + def.robotGlb, A)) return id + ": robot.glb failed";
    const double t1 = profNowMs();
    if (!loadRobotShared(def, B)) return id + ": shared load failed";
    const double t2 = profNowMs();
    char b[512];
    // Skeleton / mesh.
    bool same = A.nodeNames == B.nodeNames && A.skinJoints == B.skinJoints && A.positions.size() == B.positions.size() &&
                A.indices == B.indices && A.subs.size() == B.subs.size() && A.mats.size() == B.mats.size() && A.joints == B.joints;
    float geo = 0.0f;
    if (same) {
        for (size_t i = 0; i < A.positions.size(); ++i) geo = std::max(geo, std::fabs(A.positions[i] - B.positions[i]));
        for (size_t i = 0; i < A.weights.size() && i < B.weights.size(); ++i) geo = std::max(geo, std::fabs(A.weights[i] - B.weights[i]));
        for (size_t i = 0; i < A.invBind.size() && i < B.invBind.size(); ++i)
            for (int k = 0; k < 16; ++k) geo = std::max(geo, std::fabs(A.invBind[i].m[k] - B.invBind[i].m[k]));
        for (size_t i = 0; i < A.nodes.size(); ++i) {
            geo = std::max(geo, core::length(A.nodes[i].t - B.nodes[i].t));
            geo = std::max(geo, std::min(std::fabs(A.nodes[i].r.x - B.nodes[i].r.x) + std::fabs(A.nodes[i].r.y - B.nodes[i].r.y) + std::fabs(A.nodes[i].r.z - B.nodes[i].r.z) + std::fabs(A.nodes[i].r.w - B.nodes[i].r.w),
                                         std::fabs(A.nodes[i].r.x + B.nodes[i].r.x) + std::fabs(A.nodes[i].r.y + B.nodes[i].r.y) + std::fabs(A.nodes[i].r.z + B.nodes[i].r.z) + std::fabs(A.nodes[i].r.w + B.nodes[i].r.w)));
        }
        for (size_t i = 0; i < A.mats.size(); ++i) if (A.mats[i].baseColorUri != B.mats[i].baseColorUri) same = false;
    }
    if (!same) LOG_INFO("ANIMSHARE %s detail: names %d joints %d pos %zu/%zu idx %d subs %zu/%zu mats %zu/%zu vjoints %d", id.c_str(),
        (int)(A.nodeNames == B.nodeNames), (int)(A.skinJoints == B.skinJoints), A.positions.size(), B.positions.size(), (int)(A.indices == B.indices),
        A.subs.size(), B.subs.size(), A.mats.size(), B.mats.size(), (int)(A.joints == B.joints));
    if (!same) for (size_t i = 0; i < A.mats.size() && i < B.mats.size(); ++i) if (A.mats[i].baseColorUri != B.mats[i].baseColorUri) { LOG_INFO("ANIMSHARE %s mat %zu: %s | %s", id.c_str(), i, A.mats[i].baseColorUri.c_str(), B.mats[i].baseColorUri.c_str()); break; }
    // Clips, by name.
    int missing = 0, metaDiff = 0, chanDiff = 0; float pose = 0.0f; std::string worst;
    assets::LocalPose pa, pb;
    for (size_t i = 0; i < A.clips.size(); ++i) {
        const assets::AnimClip& ca = A.clips[i];
        int j = -1;
        for (size_t k = 0; k < B.clips.size(); ++k) if (B.clips[k].name == ca.name) { j = (int)k; break; }
        if (j < 0) { ++missing; continue; }
        const assets::AnimClip& cb = B.clips[(size_t)j];
        if (std::fabs(ca.duration - cb.duration) > 1e-4f || ca.additive != cb.additive || ca.category != cb.category) ++metaDiff;
        if (ca.channels.size() != cb.channels.size()) ++chanDiff;
        for (float u : {0.0f, 0.37f, 0.71f, 1.0f}) {
            assets::samplePose(A, (int)i, ca.duration * u, false, pa, ca.additive);
            assets::samplePose(B, j, cb.duration * u, false, pb, cb.additive);
            for (size_t n = 0; n < pa.size() && n < pb.size(); ++n) {
                float d = std::max(core::length(pa.t[n] - pb.t[n]), core::length(pa.s[n] - pb.s[n]));
                const core::Quat& qa = pa.r[n]; const core::Quat& qb = pb.r[n];
                d = std::max(d, std::min(std::fabs(qa.x - qb.x) + std::fabs(qa.y - qb.y) + std::fabs(qa.z - qb.z) + std::fabs(qa.w - qb.w),
                                         std::fabs(qa.x + qb.x) + std::fabs(qa.y + qb.y) + std::fabs(qa.z + qb.z) + std::fabs(qa.w + qb.w)));
                if (d > pose) { pose = d; worst = ca.name; }
            }
        }
    }
    ok = same && geo < 1e-4f && missing == 0 && metaDiff == 0 && chanDiff == 0 && pose < 1e-4f && A.clips.size() == B.clips.size();
    std::snprintf(b, sizeof b, "%s: skeleton/mesh %s (max diff %.2g), clips %zu vs %zu, missing %d, meta diff %d, channel-count diff %d, "
                  "max pose diff %.2g (%s); robot.glb %.0f ms, shared %.0f ms",
                  id.c_str(), same ? "same" : "DIFFERENT", geo, A.clips.size(), B.clips.size(), missing, metaDiff, chanDiff, pose,
                  worst.c_str(), t1 - t0, t2 - t1);
    return b;
}

// ---- Authoritative gameplay events: participant snapshots / kill context (GameplayEvents.h) ----
const Character* World::participantPawn(int player) const {
    if (player < 0) return nullptr;
    if (player == localPlayer_) return localDead_ ? nullptr : &player_.pawn();
    for (const MatchOpponent* o : opponents_) if (o->matchPlayer() == player && o->spawned()) return &o->pawn();
    return nullptr;
}

ParticipantSnapshot World::participantSnapshot(int player) const {
    ParticipantSnapshot s;
    s.player = player;
    const Character* c = participantPawn(player);
    if (!c) return s;
    s.alive = true;
    s.specialty = c->specialty();
    s.chassis = c->chassis().id;
    s.vehicleForm = c->moveForm() == Form::Vehicle;
    s.transforming = c->isTransforming();
    if (s.vehicleForm) s.vehicleType = (int)c->vehicleParams().form;
    s.flying = s.vehicleForm && c->vehicleState().flying;
    s.health = c->health().current; s.healthMax = c->health().max;
    s.meleeing = c->isMeleeing();
    s.hovering = c->hoverState_ == 2;
    s.fineAim = player == localPlayer_ && player_.controller().fineAiming();
    s.pos = c->position();
    // Active buffs (original class names) the kill-award rules test; instigators where the rebuild tracks them.
    auto buff = [&](bool on, const char* cls) { if (on) s.buffs.push_back({cls, -1}); };
    buff(c->warcryRemain_ > 0.0f, "TransGame.TnBuffWarcryIncreaseDamage");
    buff(c->hoverState_ == 2, "TransGame.TnBuffIncreaseDamageDuringHover");
    buff(c->cloakRemain_ > 0.0f, "TransGame.TnBuffCloak");
    buff(c->hardLockedRemain_ > 0.0f, "TransGame.TnBuffHardLocked");
    buff(c->drainRemain_ > 0.0f, "TransGame.TnBuffDrainTarget");
    buff(c->jammedRemain_ > 0.0f, "TransGame.TnBuffAbilityJammed");
    buff(c->transformDisruptRemain_ > 0.0f, "TransGame.TnBuffTransformDisruptor");
    buff(c->rollerSlowRemain_ > 0.0f, "TransGame.TnBuffRollerSphereDecreaseSpeed");
    buff(c->beaconDamageBuff_ > 0.0f, "TransGame.TnBuffAmmoBeaconIncreaseDamage");
    if (matchActive_) {
        const int ci = mapState_.carriedBy(player);
        if (ci >= 0) s.carrying = mapState_.carried()[(size_t)ci].kind;
        const int z = mapState_.activeKothZone();
        if (z >= 0 && (size_t)z < mapState_.objectives().size()) s.inActiveZone = mapState_.objectives()[(size_t)z].contains(s.pos);
        const int team = match_.players()[(size_t)player].team;
        for (const ObjectiveObject& o : mapState_.objectives())
            if (o.activeInMode && o.cls == "TnDominationPoint" && o.defenderTeam != 255 && o.defenderTeam != team && o.contains(s.pos)) s.inEnemyNode = true;
    }
    return s;
}

Match::KillContext World::killContext(int instigator, int victim, const std::string& damageType) const {
    Match::KillContext k;
    const Character* ip = participantPawn(instigator);
    const Character* vp = participantPawn(victim);
    if (ip) {
        const Weapon* w = (ip->moveForm() == Form::Vehicle && ip->vehicleWeapon()) ? ip->vehicleWeapon() : &ip->weapon();
        if (w && w->def) k.weapon = w->def->provider;
    }
    k.killAfterDeath = instigator >= 0 && instigator != victim && !ip;   // the killer was already dead (projectile / mine)
    if (ip && vp) k.distanceUU = core::length(ip->position() - vp->position()) * 100.0f;
    // Melee death type (RE §12 addendum 20): Melee + MeleeBerzerk, Rammed, ShoulderSlam, WeakMelee, Whirlwind.
    k.melee = damageType.find("Melee") != std::string::npos || damageType.find("Rammed") != std::string::npos ||
              damageType.find("ShoulderSlam") != std::string::npos || damageType.find("Whirlwind") != std::string::npos;
    static const char* abilityTypes[] = {"Whirlwind", "Shockwave", "SentryGun", "KamikazeMine", "RollerSphere", "GuidedMissile",
                                         "Drain", "AOE", "Rammed"};
    for (const char* a : abilityTypes) if (damageType.find(a) != std::string::npos) k.ability = true;
    return k;
}

void World::recordSpawnEvent(int player) {
    GameplayEvent& e = match_.recordEvent(GameplayEventType::Spawn, player);
    e.text = e.instigatorState.chassis;
}

} // namespace game
