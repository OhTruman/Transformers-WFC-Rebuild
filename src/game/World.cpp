#include "game/World.h"
#include "game/DamageTarget.h"
#include "render/Renderer.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "platform/Image.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
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
    player_.pawn().setFormModels(&robotModel_, &vehicleModel_);

    // Ion Blaster: static mesh held at the robot's primary weapon socket.
    render::MeshData weaponMesh;
    if (assets::loadGlb(root + "/Weapons/IonBlaster/weapon.glb", weaponMesh)) {
        resolveTextures(weaponMesh.mats);
        weaponMesh_ = renderer.uploadMesh(weaponMesh);
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
            float t; return collision_.segmentHit(a, b, t);
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
    player_.controller().handleInput(in, dt);
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
        dir = core::normalize(dir + rt * (rf() * w.spread) + u2 * (rf() * w.spread));
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
    if (player_.pawn().hasWeapon()) {
        const core::Mat4& wm = player_.pawn().weaponWorld();
        muzzle = core::transformPoint(wm, core::Vec3{core::config::kMuzzleLocalX,
                     core::config::kMuzzleLocalY, core::config::kMuzzleLocalZ});
    }
    shots_.push_back({muzzle, hitPoint, 0.06f});
    if (std::getenv("WFC_MUZZLELOG") && player_.pawn().hasWeapon()) {
        const core::Mat4& wm = player_.pawn().weaponWorld();
        LOG_INFO("MUZZLE hand=%.2f,%.2f,%.2f tip=%.2f,%.2f,%.2f (|offset|=%.2fm)",
                 wm.m[12], wm.m[13], wm.m[14], muzzle.x, muzzle.y, muzzle.z,
                 core::length(muzzle - core::Vec3{wm.m[12], wm.m[13], wm.m[14]}));
    }
    playSfx(Sfx::Fire, muzzle);
}

void World::setAudio(audio::IAudio* a) {
    audio_ = a;
    if (!a) return;
    const std::string base = assetRoot() + "/../content/";
    sndFire_      = a->load(base + "WL_GUN_ION_BLASTER/GUN_ION_BLASTER_HEAD.wav");
    sndReload_    = a->load(base + "WL_GUN_FOLEY/GUN_RIFLE_CLIP_RELOAD.wav");
    sndTransform_ = a->load(base + "WL_EVENT_IACON/EVENT_IACON_BRIDGE_TRANSFORM_GEARS.wav");
    sndLand_      = a->load(base + "WL_GUN_FOLEY/RELOAD_AIR_RELEASE_THUMP.wav");
    LOG_INFO("audio: cues fire=%d reload=%d transform=%d land=%d",
             sndFire_, sndReload_, sndTransform_, sndLand_);
}

void World::playSfx(Sfx s, const core::Vec3& pos) {
    if (!audio_) return;
    // refDist/maxDist in metres [PROV] — exact SoundCue attenuation radii not yet extracted.
    switch (s) {
        case Sfx::Fire:      audio_->playAt(sndFire_, pos, 0.8f, 8.0f, 150.0f); break;
        case Sfx::Reload:    audio_->playAt(sndReload_, pos, 0.9f, 4.0f, 40.0f); break;
        case Sfx::Transform: audio_->playAt(sndTransform_, pos, 0.9f, 10.0f, 90.0f); break;
        case Sfx::Land:      audio_->playAt(sndLand_, pos, 0.8f, 5.0f, 50.0f); break;
    }
}

void World::tick(float dt) {
    player_.controller().applyToPawn(*this, dt);
    player_.pawn().updateAnimation(dt);
    for (size_t i = 0; i < shots_.size();) {
        shots_[i].ttl -= dt;
        if (shots_[i].ttl <= 0) { shots_[i] = shots_.back(); shots_.pop_back(); }
        else ++i;
    }
    // Event-driven audio via edge detection on pawn state.
    {
        core::Vec3 pp = player_.pawn().position();
        bool grounded = player_.pawn().onGround();
        if (grounded && !prevGrounded_) playSfx(Sfx::Land, pp);
        prevGrounded_ = grounded;
        bool tf = player_.pawn().isTransforming();
        if (tf && !prevTransforming_) playSfx(Sfx::Transform, pp);
        prevTransforming_ = tf;
        bool rl = player_.pawn().weapon().reloading();
        if (rl && !prevReloading_) playSfx(Sfx::Reload, pp);
        prevReloading_ = rl;
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
    player_.draw(r);

    // Ion Blaster mesh held at the weapon socket (robot form only).
    if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
        r.drawMesh(weaponMesh_, player_.pawn().weaponWorld(), core::Vec3{1, 1, 1});

    // Tracers + muzzle flashes for recent shots.
    for (const Shot& s : shots_) {
        r.drawLine(s.a, s.b, core::Vec3{1.0f, 0.85f, 0.35f});
        r.drawBox(s.a, core::Vec3{0.35f, 0.35f, 0.35f}, core::Vec3{1.0f, 0.9f, 0.4f});
    }

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
