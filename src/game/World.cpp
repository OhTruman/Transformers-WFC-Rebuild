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
#include <cstring>
#include <fstream>
#include <map>
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
    // The match's authored rule classes gate rule-dependent presentation (objective bases, Conquest totems,
    // objective-factory effects) exactly as GameInfo.HasRule gates the world state.
    renderer.setActiveGameRules(gameRulesForMode(matchMode_));

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
    // Optimus_ROBODEF.ArmBlueprint: CP_OptimusArm_SKEL + OptimusArm_ROBO_ANIM (ARM_Equip / ARM_Unequip),
    // umodel glTF exports in the same Y-up metre convention as robot.glb.
    if (assets::loadSkinnedGlb(root + "/../content/TR_Optimus_ROBO_p/CP_OptimusArm_SKEL.gltf", armModel_)) {
        assets::loadAnimationsByName(root + "/../content/TR_HeavyMedium_ANM_p/OptimusArm_ROBO_ANIM.anim.gltf", armModel_);
        resolveTextures(armModel_.mats);
        player_.pawn().setArmModel(&armModel_);
        for (const auto& c : armModel_.clips) LOG_INFO("arm clip %s %.2f s", c.name.c_str(), c.duration);
    }
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

    // Collision: the authored per-trace worlds (AssetTools PHYSICS_STREETS): collision_pawn.glb blocks pawn and
    // vehicle movement (BSP + 71 BlockingVolumes + 4 TnForcedDirVolumes + authored simple hulls), and
    // collision_weapon.glb blocks hitscan / line checks (BSP + the 34 weapon-blocking volumes + hulls).
    // collision.glb (render geometry of every blocking prop) is only a fallback. Movers' triangles are split
    // out into moving collision sets (MapState).
    const std::string mapDir = root + "/Maps/MP_IAC_Streets/";
    mapState_.load(mapDir + "gameplay.json", matchMode_);
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
        killZ_ = -750.0f;   // BASE TnWorldInfo KillZ -75000 UU [CONF PHYSICS_STREETS]
    }

    // Place the player at an authored start of the match's class (FFA in DM, team starts otherwise), with the
    // start's authored rotation.
    spawnPos_ = {0, 0, 0};
    spawnYaw_ = 0.0f;
    if (loadSpawn(root + "/Maps/MP_IAC_Streets/spawnpoints.json", spawnPos_, spawnYaw_)) {
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
    loadPickupFactories(root + "/Maps/MP_IAC_Streets/gameplay.json");
    loadDestructibles(root + "/Maps/MP_IAC_Streets/physics.json", root + "/../content/");
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

void World::fireHitscan(const core::Vec3& origin, const core::Vec3& dirIn) {
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
    float dist = (hitTarget || hitDes || hitOpp) ? targetDist : bestDist;
    if (hitOpp) applyMatchDamage(hitOpp->matchPlayer(), localPlayer_, w.damageAt(dist), false);   // InstantHitDamage, falloff
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
    bool vehicle = pc.form() == Form::Vehicle && !pc.isTransforming();
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
        // HmAnimNotify_Sound plays its cue at the weapon mesh (owned by the local player).
        const core::Mat4& wm = player_.pawn().weaponWorld();
        core::Vec3 p{wm.m[12], wm.m[13], wm.m[14]};
        const char* name = n.what.c_str();
        const char* dot = std::strrchr(name, '.');
        if (n.what.rfind("BL_WPN_GUN_ION_BLASTER.", 0) == 0 && dot) name = dot + 1;
        cues_.play(name, p, core::length(p - player_.pawn().position()));
    }
}

void World::tick(float dt) {
    pickupEvents_.clear();
    matchEvents_.clear();
    destructibleEvents_.clear();
    if (collision_.valid()) mapState_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    {   // Audio listener = camera (same pose the app hands to IAudio::setListener).
        render::Camera cam;
        player_.controller().updateCamera(cam);
        listenerPos_ = cam.pos;
    }
    if (matchActive_) tickMatch(dt);
    if (!localPlayerDead()) {                       // dead / not yet spawned (match): no pawn simulation
        player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn
    }
    player_.controller().tickCameraCollision(dt);   // obstruction behaviour after the pawn moved
    gameplayRamContacts();
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

    if (player_.pawn().position().y < killZ_ && !localPlayerDead()) {
        // Below KillZ: FellOutOfWorld -> Died with no killer (an environmental death in a match).
        LOG_INFO("World: player fell out of world; %s", matchActive_ ? "killed (KillZ)" : "respawning");
        if (matchActive_) killLocalPlayer(-1, false);
        else respawnPlayer();
    }
    for (auto& a : actors_) if (a->alive()) a->tick(*this, dt);
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
        match_.loadSpawnData(root + "/Maps/MP_IAC_Streets/gameplay.json");
    }
    match_.begin(s);
    if (localPlayer_ < 0) localPlayer_ = match_.addPlayer("Player");
    matchActive_ = true;
    localDead_ = true;            // PendingMatch: TrySpawnPlayer false -> nobody spawns before the start
}

bool MatchLaunch::fromURL(const std::string& url, MatchLaunch& out) {
    // "<Map>?Key=Value?Key=Value..." (ServerTravel / StartLevel URL).
    size_t q = url.find('?');
    std::string map = url.substr(0, q);
    std::string lower = map;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.rfind("mp_iac_streets", 0) == 0) out.map = "MP_IAC_Streets"; else out.map = map;
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
    if (l.map != "MP_IAC_Streets" || !usingSlice_) { LOG_WARN("match: map %s is not loaded (only MP_IAC_Streets)", l.map.c_str()); return false; }
    MatchMode mode = MatchMode::DM;
    bool known = false;
    for (MatchMode m : {MatchMode::DM, MatchMode::TDM, MatchMode::CTF, MatchMode::KOTH, MatchMode::EXT, MatchMode::DOM})
        if (l.modeTag == gameModeName(m)) { mode = m; known = true; }
    if (!known) { LOG_WARN("match: unknown mode %s", l.modeTag.c_str()); return false; }
    if (mode != MatchMode::TDM && mode != MatchMode::DM) {
        LOG_WARN("match: %s match rules are not implemented (map state only)", l.modeTag.c_str());
        return false;
    }
    matchMode_ = mode;
    mapState_.setMode(mode);
    resetForNewLevel();
    startLocalMatch(l.settings);
    LOG_INFO("match: launched %s %s (goal %d, time %d s)", l.map.c_str(), l.modeTag.c_str(), l.settings.goalScore, l.settings.timeLimit);
    return true;
}

bool World::applyMatchDamage(int victim, int instigator, float amount, bool aoe) {
    if (!matchActive_ || match_.state() != Match::State::InProgress || victim < 0 || (size_t)victim >= match_.players().size()) return false;
    if (!match_.players()[(size_t)victim].alive) return false;
    // TnPlayerPawn.TakeDamage: teammates' damage is discarded except TnDamageTypeAOE (NotifyHitByFriendlyFire).
    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;
    Health* h = nullptr;
    MatchOpponent* opp = nullptr;
    if (victim == localPlayer_) h = &player_.pawn().health();
    else for (MatchOpponent* o : opponents_) if (o->matchPlayer() == victim) { opp = o; h = &o->health(); }
    if (!h) return false;
    float applied = h->applyDamage(amount);
    match_.recordDamage(victim, instigator, applied);
    if (h->isDead()) {
        if (victim == localPlayer_) killLocalPlayer(instigator, false);
        else { match_.killed(instigator, victim, false); if (opp) opp->despawn(); }
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
    h.activeSegment = pc.health().activeSegment(); h.segmentCount = Health::kSegmentCount;
    h.clipAmmo = pc.weapon().ammo; h.reserveAmmo = pc.weapon().reserve;
    h.vehicleForm = pc.moveForm() == Form::Vehicle; h.transforming = pc.isTransforming();
    h.matchActive = matchActive_;
    if (!matchActive_ || localPlayer_ < 0) return h;
    const MatchPlayer& me = match_.players()[(size_t)localPlayer_];
    h.timeToRespawn = me.timeToRespawn;
    h.modeTag = match_.settings().modeTag;
    h.matchState = (int)match_.state(); h.gameStatus = match_.gameStatus();
    h.countdown = match_.countdown(); h.remainingTime = match_.remainingTime(); h.elapsedTime = match_.elapsedTime();
    h.goalScore = match_.settings().goalScore;
    h.teamScore[0] = match_.teamScore(0); h.teamScore[1] = match_.teamScore(1);
    h.myTeam = me.team; h.score = me.score; h.kills = me.kills; h.deaths = me.deaths; h.assists = me.assists;
    h.winnerTeam = match_.winnerTeam();
    if (match_.state() == Match::State::MatchOver || match_.state() == Match::State::Returned)
        h.result = match_.winnerTeam() < 0 ? "Tie game" : (match_.winnerTeam() == me.team ? "Your team won" : "Your team lost");
    for (size_t i = 0; i < match_.players().size(); ++i) {
        if ((int)i == localPlayer_ || !match_.players()[i].alive) continue;   // hidden for yourself and dead pawns
        HudGameState::Tag t;
        t.player = (int)i; t.name = match_.players()[i].name; t.team = match_.players()[i].team;
        t.ally = match_.settings().teamGame && t.team == me.team;
        t.drawn = t.ally;                                                       // enemy marker disabled by default
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == (int)i) t.pos = o->position();
        h.tags.push_back(t);
    }
    return h;
}

void World::killLocalPlayer(int killer, bool suicide) {
    if (!matchActive_ || localDead_) return;
    match_.killed(killer, localPlayer_, suicide);
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
    bool returned = false;
    for (const MatchEvent& e : match_.events()) {
        switch (e.type) {
            case MatchEvent::Type::MatchStarted:
                // TnTeamGame.StartMatch: Reset() every pickup factory (sleeping factories return to 'Pickup').
                for (PickupFactory* f : pickupFactories_) f->resetToPickup(*this);
                break;
            case MatchEvent::Type::PlayerSpawned:
                if (e.player == localPlayer_ && e.value >= 0) {
                    // RestartPlayer: a fresh pawn (robot form, full health, default inventory) at the chosen start,
                    // with its authored rotation.
                    const Match::Start& st = match_.starts()[(size_t)e.value];
                    pc.respawnReset();   // a fresh pawn: robot form, no fold, HealthMax, default inventory
                    core::Vec3 p = st.pos;
                    float gy; core::Vec3 gn;
                    if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                    pc.setPosition(p); pc.setYaw(st.yaw); pc.velocity() = {0, 0, 0}; pc.groundY = p.y;
                    player_.controller().setCameraYaw(st.yaw);
                    localDead_ = false;
                    LOG_INFO("match: local player spawned at %s (%s team %d)", st.actor.c_str(), st.cluster.c_str(),
                             match_.players()[(size_t)localPlayer_].team);
                }
                for (MatchOpponent* o : opponents_)
                    if (o->matchPlayer() == e.player && e.value >= 0) {
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
    for (const auto& a : actors_) if (a->alive()) a->draw(r);
    if (!localPlayerDead()) player_.draw(r);

    // Ion Blaster mesh held at the weapon socket (robot form only).
    if (weaponAnim_.valid() && player_.pawn().hasWeapon())
        r.drawDynamicMesh(weaponAnim_.pose(), player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});
    else if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
        r.drawMesh(weaponMesh_, player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});

    // Weapon + vehicle boost effects last (translucent/additive over the opaque scene).
    fx_.draw(r);
    vehicleFx_.draw(r);

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

} // namespace game
