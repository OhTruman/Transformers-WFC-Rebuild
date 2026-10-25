#include <unordered_map>
#include "core/WorkerPool.h"
#include "core/SimRandom.h"
#include "game/World.h"
#include "game/TickProfile.h"
#include <atomic>
#include "frontend/DataPaths.h"
#include <utility>
#include <set>
#include "core/LoadYield.h"
#include "game/CharacterAudio.h"
#include "game/GpuSkin.h"
#include "game/GibRagdoll.h"
#include "game/DamageTarget.h"
#include "render/Renderer.h"
#include "render/Camera.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "platform/Image.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"

#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <fstream>
#include "core/Sha256.h"
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <functional>
#include <atomic>
#include <future>
#include <set>
#include <thread>

namespace {
struct MuzzleLightDef { const char* cls; float radius; float color[3]; float falloff; float duration; float brightness; };
#include "game/MuzzleLights.inc"
#include "game/AnimNotifies.inc"   // [Systems] authored body notifies (HmAnimNotify_PlayEffect rows of the TR_* AnimSets)
#include "game/MeshSockets.inc"    // [Systems] the body meshes' SkeletalMeshSockets
#include "game/PhysmatImpacts.inc" // [Systems] PhysicalMaterial ImpactEffect / ImpactSound (gen_physmat_impacts.py)
struct DeathLightDef { float radius; float color[3]; float falloff, rampIn, lightTime, rampOut; };
#include "game/DeathLights.inc"
// [Gameplay] The original's death entries / pickers' data (tools/gameplay/death_entries.py, authored.db).
struct DeathEntryDef { const char* types[3]; int nTypes; const char* anim; float blendIn, tMin, tMax, aiMin, aiMax; unsigned mods; };
struct DamageDeathTypeDef { const char* damage; const char* death; };
struct DeathChooserDef { const char* group; const char* anim; int weight; };
struct ChassisDeathDef { const char* chassis; const char* robotDeaths; const char* vehicleDeaths; const char* hull; float transfer; };
#include "game/DeathEntries.inc"
#include "game/DeathFx.inc"   // [Systems] the entries' gibs / PlayEffect effects (agents/systems 299d85da)
// Vehicle headlights: INVENTED PLACEMENT (the original authors none; Realistic-only addition, Integration 2026-10-16).
struct HeadlightDef { const char* chassis; const char* bone; float pos[3]; float dir[3]; float color[3]; };
#include "game/VehicleHeadlights.inc"
// RT-era effect lights (NOT original lights; SYSTEMS_RT_LIGHT_HOOKS.md): Realistic only, on by default there (Integration
// 2026-10-16, after Rendering's calibration); WFC_FXLIGHTS=0 turns them off. Off / Faithful never add or submit one. Effect templates (explosions, impact squibs, boost, pickup FX) and per-chassis boost sockets.
struct FxLightDef { const char* tmpl; float color[3]; float radius, falloff, duration; };
struct BoostLightDef { const char* chassis; const char* bone; float pos[3]; float color[3]; float radius, falloff; };
#include "game/FxLights.inc"
bool fxLightsOn() { static const bool on = [] { const char* e = std::getenv("WFC_FXLIGHTS"); return !e || std::string(e) != "0"; }(); return on; }
const FxLightDef* fxLightFor(const std::string& tmpl) {
    for (const FxLightDef& d : kFxLights) if (tmpl == d.tmpl) return &d;
    return nullptr;
}
const MuzzleLightDef* muzzleLightFor(const std::string& cls) {
    for (const MuzzleLightDef& d : kMuzzleLights)
        if (cls == d.cls) return &d;
    return nullptr;
}
}  // namespace

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
    return core::config::assetRootDefault();
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
// [integration 08n] The integrated renderer must provide these: the detection above would otherwise fall back to a silent
// no-op (missing prewarm / charge glow / effects) instead of failing the build.
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().prewarmDynamicMesh(std::declval<const render::MeshData&>()), 0)) > 0, "IRenderer::prewarmDynamicMesh");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().setDrawMaterialParam(std::string(), nullptr), 0)) > 0, "IRenderer::setDrawMaterialParam");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().clearDrawMaterialParam(std::string()), 0)) > 0, "IRenderer::clearDrawMaterialParam");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().spawnParticleEffect(std::string(), core::Vec3{}, core::Vec3{}, core::Vec3{}), 0)) > 0, "IRenderer::spawnParticleEffect");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().stopParticleEffect(0), 0)) > 0, "IRenderer::stopParticleEffect");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().spawnParticleEffectSegment(std::string(), core::Vec3{}, core::Vec3{}), 0)) > 0, "IRenderer::spawnParticleEffectSegment");
static_assert(sizeof(decltype(std::declval<render::IRenderer&>().setParticleEffectSegment(0, core::Vec3{}, core::Vec3{}), 0)) > 0, "IRenderer::setParticleEffectSegment");
// Rendering's per-effect parameter (agents/rendering: setParticleEffectParam(handle, name, rgba[4])), compile-time detected. "Team" picks
// the ParticleModuleSwitchableColorScaleOverLife channel: 0 = A, 1 = B [CONF RE, Rendering 2026-10-07].
template <class R> auto fxTeamParam(R& r, int h, int team, int) -> decltype(r.setParticleEffectParam(h, std::string(), (const float*)nullptr), void()) {
    if (h < 0 || team < 0) return;
    const float v[4] = {(float)team, 0.0f, 0.0f, 0.0f};
    r.setParticleEffectParam(h, "Team", v);
}
template <class R> void fxTeamParam(R&, int, int, long) {}
// EmitterPool membership (Rendering's opt-in MaxActiveEffects cap) [CONF script, RE 4cce28f MILESTONE_E_PAWN_RENDER_COST_RULES §7]:
// pooled = single tracers, projectile / grenade / roller explosions, vehicle one-shots, pawn hit effects (impact squibs: a per-weapon
// bucket); owned = muzzle flashes (the weapon mesh's PSComponent), looping tracers, shell casings, projectile flight FX, abilities.
template <class R> auto fxPooled(R& r, int h, int) -> decltype(r.setParticleEffectParam(h, std::string(), (const float*)nullptr), int()) {
    if (h >= 0) { const float v[4] = {1.0f, 0.0f, 0.0f, 0.0f}; r.setParticleEffectParam(h, "Pooled", v); }
    return h;
}
template <class R> int fxPooled(R&, int h, long) { return h; }

// WFC_SPAWNPROF: millisecond timings of the spawn path / slow World steps (diagnostics, no behaviour change).
static bool spawnProf() { static const bool on = std::getenv("WFC_SPAWNPROF") != nullptr; return on; }
static double profNowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void World::load(render::IRenderer& renderer) {
    joinStep();
    if (std::getenv("WFC_VFX_FAKE") && *std::getenv("WFC_VFX_FAKE")) { VehicleFxDriver::Runtime fr; bindFakeVehicleFxRuntime(fr); setVehicleFxRuntime(fr); }
    else {
        // [integration M08e] Per-chassis vehicle FX (Systems VehicleFxDriver) through Rendering's particle runtime; bound, the
        // hand-made Optimus vehicle FX turn off (nothing drawn twice). The renderer outlives this World.
        VehicleFxDriver::Runtime rt;
        if (fxDirect()) {   // WFC_FXDIRECT=1: the old direct calls (the driver ticks in the async step: races with endFrame)
            rt.spawnAt = [&renderer](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.spawnParticleEffect(t, p, f, u); };
            rt.setTransform = [&renderer](int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return renderer.setParticleEffectTransform(h, p, f, u); };
            rt.setParam = [&renderer](int h, const std::string& n, const float v[4]) { return renderer.setParticleEffectParam(h, n, v); };
            rt.stop = [&renderer](int h) { renderer.stopParticleEffect(h); };
        } else {
            // [Systems, FX race fix] The driver ticks in the step's background part (tickVehicleBoost), which overlaps the renderer's
            // endFrame: every call goes through the step's FX deferral (virtual ids; on the worker the ops run at the next join).
            rt.spawnAt = [this](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { return fxStepSpawnPlain(t, p, f, u); };
            rt.setTransform = [this](int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) { fxStepMove(h, p, f, u); return true; };
            rt.setParam = [this](int h, const std::string& n, const float v[4]) { fxStepParam(h, n, v); return true; };
            rt.stop = [this](int h) { fxStepStop(h); };
        }
        setVehicleFxRuntime(rt);
    }
    // [Systems M08e] With Rendering's runtime (agents/rendering 38c9ecf / e15862f):
    //   setVehicleFxRuntime({[&renderer](auto& t, auto& p, auto& f, auto& u) { return renderer.spawnParticleEffect(t, p, f, u); },
    //                        [&renderer](int h, auto& p, auto& f, auto& u) { return renderer.setParticleEffectTransform(h, p, f, u); },
    //                        [&renderer](int h, auto& n, const float* v) { return renderer.setParticleEffectParam(h, n, v); },
    //                        [&renderer](int h) { renderer.stopParticleEffect(h); }});
    Character::clearRigCache();   // rigs point at models of the previous load
    repairBeamHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) { fireRepairBeamImpl(w, o, d); };
    // [integration 09b] Participant (bot) shot presentation through the SAME per-weapon lookup as the local player's WP_Fire:
    // the fired weapon class's WeaponFx templates (muzzle at the shooter's MuzzleFlash socket, tracer to the hit point, impact
    // squib where it hit); WeaponFx draws the ones it reconstructs, Rendering's particle runtime the others. No other weapon's
    // FX is substituted (no Ion fallback): a class without templates draws nothing, logged once. Projectile weapons draw
    // their own flight effect (no tracer).
    participantShotFxHook = [this](const ParticipantShot& shot, const core::Mat4& muzzle) {
        const std::string cls = "TransContent.TnWeapon" + shot.weapon;
        const WeaponFxTemplates* wfx = CharacterAudio::weaponFx(cls);
        if (!wfx) {
            static std::set<std::string> warned;
            if (warned.insert(cls).second) LOG_WARN("participant weapon fx: no templates for %s (nothing drawn)", cls.c_str());
            return;
        }
        const WeaponDef* d = findWeaponDef(shot.weapon);
        const bool projectile = d && d->projSpeed > 0.0f;
        const core::Vec3 from{muzzle.m[12], muzzle.m[13], muzzle.m[14]};
        const bool firstPellet = shot.pellet == 0;   // RE 9a776fb: muzzle flash + tracer once per fire event (shot index 0)
        // Anchored to the shooter's own socket (WeaponFx owner = participant), never to the local held weapon: the shared fx_
        // drew bot flashes at the player's muzzle (playtest 2026-10-13 "my gun appears to randomly fire").
        if (firstPellet && !wfx->muzzle.empty()) fx_.spawnMuzzleFlash(wfx->muzzle, muzzle, shot.player);
        if (firstPellet) spawnMuzzleLight(cls, muzzle, shot.player);
        if (!projectile) {
            if (firstPellet) fx_.spawnTracer(wfx->tracer, from, shot.to);
            // [integration 09c] Pawn.EffectIsRelevant for a non-local shooter's impact (RE 4cce28f / Gameplay): the shooter's mesh
            // rendered within the last 1 s (renderer LastRenderTime, owner 100 + player) and the impact within MaxImpactEffectDistance
            // 2500 UU x LODDistanceFactor 1.0 of the camera, or within 1600 UU when behind it (RE 19c8fbf). User decision 2026-10-08:
            // applied only in extended lobbies (> 10 participants);
            // original-size matches keep every impact. WFC_IMPACTRELEVANCE=0 / 1 forces it off / on.
            bool relevant = true;
            static const char* irEnv = std::getenv("WFC_IMPACTRELEVANCE");
            const bool gate = irEnv ? std::atoi(irEnv) != 0 : extendedLobby();
            if (shot.impact && gate && shot.player != localPlayer_ && renderer_) {
                const float age = renderer_->drawOwnerRenderAge(100 + shot.player);
                const core::Vec3 cam = player_.controller().cameraPos();
                const core::Vec3 toImpact = shot.to - cam;
                const bool behind = core::dot(toImpact, core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch())) < 0.0f;
                relevant = age >= 0.0f && age <= 1.0f && core::length(toImpact) <= (behind ? 16.0f : 25.0f);   // 1600 / 2500 UU
            }
            if (shot.impact && relevant && gate && shot.player != localPlayer_) {
                // SkipSquibEffect: only ImpactSquibPercentage of the attempts spawn (per shooter, a deterministic accumulator instead
                // of the original's per-second random roll). The ImpactSquibMaxCount bucket (5-14 live per weapon mesh) is not
                // enforced: the renderer has no kill for a live one-shot, and squibs are short-lived (pool peak 3-4 at 64 players).
                const float pct = impactFxRule(shot.weapon).squibPercentage;
                if (pct < 1.0f) {
                    float& acc = squibAccum_[shot.player];
                    acc += pct;
                    if (acc < 1.0f) relevant = false; else acc -= 1.0f;
                }
            }
            if (shot.impact && relevant) {
                fxSpawnPooled_ = true;   // impact squibs: pooled
                const std::string squib = impactSquibFor(shot.physmat, wfx->squib);   // [Systems] physmat rule
                fx_.spawnImpact(squib, shot.to, core::normalize(shot.from - shot.to), shot.from);
                fxSpawnPooled_ = false;
                fxLight(squib, shot.to + core::normalize(shot.from - shot.to) * 0.2f);   // RT-era impact light, off the surface
            }
        }
    };
    heldWeaponMuzzleHook = [this](core::Vec3& out) { return heldWeaponMuzzleImpl(out); };
    fx_.setParticipantMuzzleSource([this](int player, core::Mat4& out) { return participantMuzzleWorld(player, out); });
    // [integration 09c] Gameplay's bot-ability hook (called once at each successful bot trigger, WorldBots botTryAbility) ->
    // Systems M09d onParticipantAbility (OnTriggerSound + the ability clip's notifies, attached to the caster). Bound here
    // instead of editing botTryAbility, so exactly one sound path exists.
    participantAbilityHook = [this](int player, const std::string& abilityId, const std::string& chassisId, const core::Vec3& at) {
        onParticipantAbility(player, abilityId, chassisId, at);
    };
    participantPositionHook = [this](int player, core::Vec3& out) {   // [Systems M09d] participant sounds follow the pawn
        const MatchOpponent* o = opponentByPlayer(player);   // [integration 09c] O(1) (was a scan per bot-attached cue per step)
        if (o && o->spawned()) { out = o->pawn().actorLocation(); return true; }
        return false;
    };
    weaponFireHook = [this](const Weapon& w, const core::Vec3& o, const core::Vec3& d) {
        if (w.projectile()) {
            spawnProjectile(o, addSpread(d, player_.pawn().shotSpreadFor(w), w) * w.projSpeed, w, localPlayer_);   // RealStartLoc; AddSpread
            if (player_.pawn().moveForm() == Form::Vehicle) {   // [Systems M08h] the vehicle shot's muzzle flash
                core::Vec3 vm;
                spawnVehicleMuzzleFlash(projectiles_.back().weaponClass, vm);
            }
            {   // [Systems M08d] PlayFiringSound for projectile weapons too (it was only on the instant-hit path)
                const bool vehForm = player_.pawn().moveForm() == Form::Vehicle;
                onWeaponFired(projectiles_.back().weaponClass, w.lowAmmo(), vehForm, o,
                              w.charge() ? std::max(0, std::min(2, w.projClass)) : 0);   // [Systems M08k] FireCharge mode (the shot copy's projClass)
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
    // Campaign chapters (D5_OME_BASE_m / D5_OME_BASE / D5_OME): AssetTools' per-unit chapter data; the renderer takes the persistent
    // level and merges the other units itself (Rendering's chapter loader); collision / spawns / nav come from the units here.
    campaignMap_ = false;
    {
        const std::string ch = CampaignChapter::chapterOf(mapName_);
        if (!ch.empty() && CampaignChapter::load(ch, cpChapter_)) {
            campaignMap_ = true; mapName_ = ch;
            LOG_INFO("campaign: chapter %s (MapId %d), %zu units at the start (state %s)", ch.c_str(), cpChapter_.mapId, cpChapter_.units.size(), cpChapter_.initialState.c_str());
        } else if (!ch.empty()) LOG_ERROR("campaign: chapter %s has no data under %s", ch.c_str(), CampaignChapter::root().c_str());
    }
    renderer.loadMapRenderData(campaignMap_ ? cpChapter_.dir + cpChapter_.persistentDir : mapName_);   // (a chapter: its persistent unit's directory, read in place)   // original-data shader path (if generated)
    int chapterUnitsDrawn = 0;
    if (campaignMap_) {
        std::vector<std::string> others;
        cpUnitLoaded_.clear();
        for (const std::string& u : cpChapter_.units) if (u != cpChapter_.persistentDir) { others.push_back(u); cpUnitLoaded_.insert(u); }   // the initial state's units (streaming loads the rest)
        chapterUnitsDrawn = renderer.loadChapterUnits(cpChapter_.dir, others);
    }
    core::loadYield("World: map render data");   // [frontend] the loading screen presents between load steps
    // The match's authored rule classes gate rule-dependent presentation (objective bases, Conquest totems,
    // objective-factory effects) exactly as GameInfo.HasRule gates the world state. [integration] Gameplay's
    // match mode now supplies the rules Rendering's interim WFC_GAMERULES stood in for; select the mode with
    // WFC_GAMEMODE.
    renderer.setActiveGameRules(gameRulesForMode(matchMode_));

    renderer_ = &renderer;
    // campaign: the persistent level's world.glb (no triangles of its own) when the renderer drew the units, else every unit merged
    bool okMap = campaignMap_ ? (chapterUnitsDrawn > 0 ? (assets::loadGlb(mapDir() + "world.glb", mapMesh), true) : (loadCampaignMesh("world.glb", mapMesh), true))
                              : assets::loadGlb(mapDir() + "world.glb", mapMesh);
    core::loadYield("World: world.glb");
    if (!okMap) return false;
    auto resolveOne = [&](const std::string& uri) { return resolveTexture(uri); };
    auto resolveTextures = [&](std::vector<render::Material>& mats) {
        prefetchMaterialTextures(mats);
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
    for (const auto& kv : texCache_) pinnedTex_.insert(kv.first);   // the map's textures (and the boot body's) stay for the session

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
    // [integration 09c] EmitterPool (RE 4cce28f §7): tracers and impact squibs are pooled (EmitterPool.SpawnEmitter), muzzle flashes
    // are owned; the renderer's MaxActiveEffects 50 cap acts only when enabled (extended lobbies, user decision 2026-10-08).
    fx_.setGenericRuntime({
        [this, &renderer](const std::string& t, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) {
            const int h = renderer.spawnParticleEffect(t, p, f, u);
            if (h >= 0 && fxSpawnPooled_) renderer.setParticleEffectPooled(h);
            return h;
        },
        [&renderer](const std::string& t, const core::Vec3& a, const core::Vec3& b) {
            const int h = renderer.spawnParticleEffectSegment(t, a, b);
            if (h >= 0) renderer.setParticleEffectPooled(h);   // tracers: pooled
            return h;
        },
        // [Systems, FX race fix] WeaponFx::tick (follow-the-muzzle transforms) runs in the step's background part: deferred to the join
        [this, &renderer](int h, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) {
            if (fxDirect()) return renderer.setParticleEffectTransform(h, p, f, u);
            presentHook([&renderer, h, p, f, u] { renderer.setParticleEffectTransform(h, p, f, u); });
            return true;
        }});
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
    mapState_.loadObjectiveVolumes(overlayed(mapDir + "physics.json"));
    loadForcedDirVolumes(overlayed(mapDir + "physics.json"));
    auto splitMovers = [this](const render::MeshData& in, render::MeshData& out,
                          std::vector<std::pair<std::string, std::vector<core::Vec3>>>& moverTris) {
        std::vector<std::string> names = mapState_.moverActorNames();
        out.positions = in.positions;
        for (const render::SubMesh& sm : in.subs) {
            bool mover = std::find(names.begin(), names.end(), sm.nodeName) != names.end();
            if (!escBlockerNames_.empty() && std::find(escBlockerNames_.begin(), escBlockerNames_.end(), sm.nodeName) != escBlockerNames_.end()) {
                escBlockerTris_.push_back({sm.nodeName, {}});   // an Escalation mechanism's blocking volume: a switchable set
                for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
                    const uint32_t v = in.indices[i];
                    escBlockerTris_.back().second.push_back({in.positions[v * 3], in.positions[v * 3 + 1], in.positions[v * 3 + 2]});
                }
                continue;
            }
            std::vector<core::Vec3>* dst = nullptr;
            if (mover) { moverTris.push_back({sm.nodeName, {}}); dst = &moverTris.back().second; }
            // a PhysicsVolume, not world collision (forcedDirPre / Post); WFC_NOFORCEDDIR=1 keeps the old solid brushes (regression A/B)
            const bool forcedDir = !std::getenv("WFC_NOFORCEDDIR") && sm.nodeName.rfind("TnForcedDirVolume", 0) == 0;
            for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
                if (forcedDir) continue;
                if (!mover) { out.indices.push_back(in.indices[i]); continue; }
                uint32_t v = in.indices[i];
                dst->push_back({in.positions[v * 3], in.positions[v * 3 + 1], in.positions[v * 3 + 2]});
            }
        }
    };
    escBlockerNames_ = svMechBlockerNames(mapName_); escBlockerTris_.clear(); escBlockerSets_.clear();
    render::MeshData colMesh, pawnStatic, weaponColMesh, weaponStatic;
    std::vector<std::pair<std::string, std::vector<core::Vec3>>> pawnMovers, weaponMovers;
    bool authored = campaignMap_ ? loadCampaignMesh("collision_pawn.glb", colMesh) : assets::loadGlb(overlayed(mapDir + "collision_pawn.glb"), colMesh);
    if (!authored) assets::loadGlb(mapDir + "collision.glb", colMesh);
    perPolyProbe_.clear(); perPolyTris_.clear();
    if (!campaignMap_) applyPerPolyCollision(colMesh, mapDir, true, !std::getenv("WFC_NOPERPOLY"));   // PC ADAPTATION: per-poly meshes (Streets' crater); WFC_NOPERPOLY=1: the authored hulls (A/B)
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
        if (campaignMap_) { cpCollectMovers(); cpSplitMovers(colMesh, pawnStatic); }   // campaign: the chapter's InterpActors
        else splitMovers(colMesh, pawnStatic, pawnMovers);
        collision_.build(pawnStatic);
        if (std::getenv("WFC_CRATERPROBE")) perPolyProbe();   // diagnostic
        svMechAddBlockers();
        if (campaignMap_) cpRegisterMovers();
        core::loadYield("World: movement collision");
        if (assets::loadGlb(mapDir + "collision_weapon.glb", weaponColMesh)) {
            if (!campaignMap_) applyPerPolyCollision(weaponColMesh, mapDir, false, !std::getenv("WFC_NOPERPOLY"));
            splitMovers(weaponColMesh, weaponStatic, weaponMovers);
            weaponCollision_.build(weaponStatic);
            loadWeaponPhysmats(mapDir, weaponColMesh);
        }
        mapState_.registerCollision(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, pawnMovers, weaponMovers);
        {   // TnTransformingWorldActors (AssetTools mp_transforming_actors.json; the frontend's manifest root)
            const char* mr = std::getenv("WFC_FRONTEND_MANIFESTS");
            const std::string mroot = mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests";
            if (campaignMap_) {   // a campaign chapter: AssetTools' <CH>/transforming_actors_gameplay.json
                transforming_.loadChapter(cpChapter_.dir + "transforming_actors_gameplay.json", mroot + "/..", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
                LOG_INFO("campaign: %zu transforming world actors", transforming_.actors().size());
                // drawn posed by TransformingActorsDraw: the bind-pose copies in the units' world.glb are hidden (Rendering 2026-10-23)
                for (const TransformingActors::Actor& ta : transforming_.actors()) renderer.setActorHidden(ta.name, true);
            } else transforming_.load(mapName_, mroot, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
            syncTransformingActorSounds();
        }
        {   // the VehicleDying body pool: unit boxes (+-1), scaled by the pose; disabled until used
            std::vector<core::Vec3> tris;
            static const int f[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1}, {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
            for (const auto& t : f) for (int k = 0; k < 3; ++k) { const int i = t[k]; tris.push_back(core::Vec3{(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f}); }
            dyingBodies_.clear(); dyingQueue_.clear();
            for (int i = 0; i < 16; ++i) {
                DyingBody b;
                b.pawnSet = collision_.addDynamicSet(tris, core::Mat4::identity()); collision_.setDynamicEnabled(b.pawnSet, false);
                if (weaponCollision_.valid()) { b.weaponSet = weaponCollision_.addDynamicSet(tris, core::Mat4::identity()); weaponCollision_.setDynamicEnabled(b.weaponSet, false); }
                dyingBodies_.push_back(b);
            }
        }
        LOG_INFO("collision: movement %s, weapon %s", authored ? "collision_pawn.glb" : "collision.glb (fallback)",
                 weaponCollision_.valid() ? "collision_weapon.glb" : "movement world");
        renderer.setVisibilityQuery([this](const core::Vec3& a, const core::Vec3& b) {
            // One exact query: segmentHit walks only the grid cells the segment crosses and stops at the first hit (the old 2 m march
            // dated from an AABB scan and cost a setup per piece - Rendering's 64p profile: segmentHit 8.7 % of the main thread).
            // Static triangles only (the renderer calls this on the main thread while the step may run).
            const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;   // zero-extent line checks
            return lineWorld.segmentAnyHit(a, b);   // a boolean: stops at the first hit
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
    if (campaignMap_) { spawnPos_ = cpChapter_.playerStart.pos; spawnYaw_ = 0.0f; }   // the chapter start (TnCampaignPlayerStart)
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
    if (renderer_) renderer_->notifyCameraCut("respawn");
}

// gameplay.json "pickups": the placed TnAmmoCrate/TnHealth/TnOverShield pickup factories with their
// authored effective RespawnTime (30 / 60 / 120 s). Objective factories (flag/bomb) require
// TnGameRules_SingleFlagCTF / ScoreBombingRun, which the slice does not run, and are not instanced.
void World::loadPickupFactories(const std::string& path) {
    pickupFactories_.clear();
    pickupFxKeys_.clear(); objectiveFxKeys_.clear();   // [integration 09c] a new map: rebuild syncMapPresentation's keys
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
    if (renderer_) renderer_->notifyCameraCut("teleport");
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

// Pad X is TryInteract, else reload: what TryInteract would find for the local player right now.
bool World::localInteractAvailable() const {
    if (!matchActive_ || localDead_ || localPlayer_ < 0 || (size_t)localPlayer_ >= match_.players().size()) return false;
    const Character& pc = player_.pawn();
    MapState::ObjPawn op{localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true,
                         pc.form() == Form::Robot && !pc.isTransforming() && !pc.isMeleeing()};
    if (mapState_.pickupCandidate(op) >= 0) return true;                  // the Code of Power / bomb
    if (cp_.active && !cp_.interactPrompt.empty()) return true;          // a campaign interact
    if (sv_.phase != SurvivalState::Phase::Off) {
        if ((size_t)localPlayer_ < sv_.nearGenerator.size() && sv_.nearGenerator[(size_t)localPlayer_] >= 0) return true;   // a generator
        const SvMech& M = svData_.mech;
        const core::Vec3 p = pc.position();
        for (size_t k = 0; k < M.lasers.size() && k < sv_.mech.lasers.size(); ++k)
            if (sv_.mech.lasers[k].active && sv_.mech.lasers[k].t >= M.lasers[k].armDelay && core::length(p - M.lasers[k].button) <= M.lasers[k].buttonRange) return true;
        for (size_t k = 0; k < M.detpacks.size() && k < sv_.mech.detpacks.size(); ++k)
            if (!sv_.mech.detpacks[k].used && core::length(p - M.detpacks[k].pos) <= M.detpacks[k].range) return true;
    }
    return false;
}

void World::handleInput(const platform::InputFrame& in, float dt) {
    player_.controller().setInteractAvailable(localInteractAvailable());
    struct ProfScope { double t0 = profNowMs(); ~ProfScope() { const double ms = profNowMs() - t0; if (spawnProf() && ms > 8.0) LOG_INFO("SPAWNPROF World::handleInput %.1f ms", ms); } } profScope;
    player_.controller().handleInput(cp_.active && cp_.cinematicMode ? platform::InputFrame{} : in, dt);
    seat2HandleInput(dt);   // split screen: the second local seat's controller   // Dash (Shift) is latched by PlayerController; campaign cinematic mode: no input
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
    sysprof::Scope spHit(sysprof::Hitscan);
    ++sysprof::shots;
    const float range = w.rangeM;   // [CONF] 300 m

    // Apply per-shot spread as a small random cone around the aim direction.
    core::Vec3 dir = core::normalize(dirIn);
    {
        core::Vec3 up{0, 1, 0};
        core::Vec3 rt = core::normalize(core::cross(dir, up));
        core::Vec3 u2 = core::normalize(core::cross(rt, dir));
        (void)rt; (void)u2;
        dir = addSpread(dir, player_.pawn().shotSpreadFor(w), w);   // Weapon.AddSpread [CONF RE playtest #11]
    }
    core::Vec3 end = origin + dir * range;

    float bestDist = range;
    uint16_t hitPhys = 0; const CollisionWorld* hitPhysWorld = nullptr;   // the world hit's physical material
    // World geometry.
    if (collision_.valid()) {
        float t; core::Vec3 n;
        const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;
        if (lineWorld.segmentHit(origin, end, t, n, &hitPhys)) { bestDist = range * t; hitPhysWorld = &lineWorld; }
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
    int cpHit = -1;   // campaign destructibles (in front of any closer hit)
    if (cp_.active) { float tc; const int ci = cpDesRayHit(origin, dir, range, tc); if (ci >= 0 && tc < targetDist) { targetDist = tc; cpHit = ci; hitTarget = nullptr; hitDes = nullptr; hitOpp = nullptr; hitBarrier = false; } }
    if (cpHit >= 0) cpDamageDes(cpHit, w.damageAt(targetDist), localPlayer_, w.damageType ? w.damageType : "");
    float dist = (hitTarget || hitDes || hitOpp || hitBarrier || cpHit >= 0) ? targetDist : bestDist;
    if (hitBarrier) damageBarrier(w.damageAt(dist), w.damageType ? w.damageType : "");
    if (hitOpp) { noteHitDir(dir); applyMatchDamage(hitOpp->matchPlayer(), localPlayer_, w.damageAt(dist), false, w.damageType); }   // InstantHitDamage, falloff, the weapon's InstantHitDamageTypes[0]
    core::Vec3 hitPoint = origin + dir * dist;
    // [Systems] the hit surface's PhysicalMaterial for the impact rule ("" = a target / pawn / barrier / nothing was hit).
    const std::string hitPhysmat = (!hitTarget && !hitDes && !hitOpp && !hitBarrier && hitPhysWorld) ? hitPhysWorld->physmatName(hitPhys) : std::string();
    (void)hitPhysmat;
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
    // RE 9a776fb: Weapon.InstantFire plays PlayFireEffects (muzzle flash + tracer) for shot index 0 only; every pellet impacts.
    if (pellet_ == 0) {
    if (vehShot) { core::Vec3 vm; if (spawnVehicleMuzzleFlash(fxClass, vm)) muzzle = vm; }
    else if (wfx && weaponSocketWorld("MuzzleFlash", ms)) fx_.spawnMuzzleFlash(wfx->muzzle, ms);
    if (!vehShot && weaponSocketWorld("MuzzleFlash", ms)) spawnMuzzleLight(fxClass, ms, localPlayer_);
    if (wfx) fx_.spawnTracer(wfx->tracer, muzzle, hitPoint);
    }
    if (wfx && dist < range - 0.01f) {
        const std::string squib = impactSquibFor(hitPhysmat, wfx->squib);   // [Systems] physmat rule
        fx_.spawnImpact(squib, hitPoint, dir * -1.0f, origin);
        fxLight(squib, hitPoint - dir * 0.2f);   // RT-era impact light, off the surface
    }
    static const bool muzzleLog = std::getenv("WFC_MUZZLELOG") != nullptr;
    if (muzzleLog && player_.pawn().hasWeapon()) {
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
        const std::string impact = impactSoundFor(hitPhysmat, CharacterAudio::weaponCue(firedCls, "DefaultImpactSound"));   // [Systems] physmat rule
        if (!impact.empty()) cues_.play(impact.c_str(), hitPoint, hitDist);
    }
}

void World::setAudio(audio::IAudio* a, bool loadSliceMap) {
    joinStep();   // [integration 09c] async step: no background part may play into the cue tables while they change (Systems review)
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
        core::Vec3 src = to + core::Vec3{0, 0.5f, 0};
        if (owner >= kOwnParticipantBase) {             // [Systems M09m] a participant's sound: from ITS body, not the local pawn's
            core::Vec3 bp;
            if (participantPositionHook && participantPositionHook(owner - kOwnParticipantBase, bp)) src = bp;   // actorLocation: body centre
        } else if (owner >= 0) {
            src = pc.position() + pc.meshOffset() + core::Vec3{0, 1.5f, 0};
        }
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
    joinStep();   // [integration 09c] async step: no background part may play into the cue tables while they change (Systems review)
    if (!levelAudio_.level().empty()) unloadMapAudio();
    // [Systems] a campaign chapter: its generated audio (tools/systems/gen_level_audio.py --campaign) from data/campaign beside the
    // frontend data (WFC_CAMPAIGN_AUDIO=<dir> overrides); the chapter's Kismet VM drives it (cpAudioBind, hosted)
    std::string chapterAudio;
    if (campaignMap_ && level == cpChapter_.chapter) {
        const char* d = std::getenv("WFC_CAMPAIGN_AUDIO");
        chapterAudio = (d ? std::string(d) : frontend::frontendDataDir() + "/../campaign") + "/" + level + "_audio.json";
    }
    const bool ok = chapterAudio.empty() ? levelAudio_.load(level) : levelAudio_.load(level, chapterAudio);
    if (ok) {                                                         // level-owned
        CharacterAudio::loadCues(cues_, audioProfile());
        // [Systems, 300 FPS pass] every cached body's voice / death / hit-reaction set too (the map's cue table starts empty): a first
        // death or hit of a body nobody selected (Escalation wave enemies, campaign factory bodies) loaded it inside the sim step
        // (Gameplay 2026-10-24: 5.7-6.9 ms at AbilityAudio::pawnDeath). loadCues skips the cues already in the table.
        static const bool preload = [] { const char* e = std::getenv("WFC_PRESENTPRELOAD"); return !(e && e[0] == '0'); }();   // (A/B)
        if (preload)
            for (const auto& kv : chassisCache_)
                if (const CharacterAudioProfile* ap = CharacterAudio::find(kv.first)) CharacterAudio::loadCues(cues_, *ap);
        CharacterAudio::loadWeaponCues(cues_, weaponClass_);
        CharacterAudio::loadHitCues(cues_, CharacterAudio::defaultProfile(), weaponClass_);   // the targets' hit sounds
        CharacterAudio::loadAbilityCues(cues_);                                                // [Systems M08i]
        lastHitEffect_.clear(); participantHitEffect_.clear(); participantProfiles_.clear(); participantNotifies_.clear();
        participantCloakAnim_.clear(); localCloakAnim_ = false;
        participantBodies_.clear();                                // the level's cues (and their instances) went with it
        for (const std::string& c : loadoutWeaponClasses_) ensureWeaponAudio(c);   // [Systems M08d] the loadout's weapons
        for (const std::string& c : participantWeaponClasses_) ensureWeaponAudio(c);   // [Systems M09b] the bots' weapons
        if (!selectionChassis_.empty() || !selectionWeapons_.empty()) preloadSelectionAudio({}, {});   // [Systems M09c]
        dialogEvents_.setLevel(canonicalMapName(level), cues_);   // [Systems] the level's character dialog sets + manager
        const SoundCues::LocStats& ls = SoundCues::locStats();
        LOG_INFO("localized waves (language %s): %d from the _LOC twin, %d merged copy of that twin, %d not played",
                 SoundCues::language().c_str(), ls.twin, ls.merged, ls.skipped);
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
    joinStep();   // [integration 09c] async step: no background part may play into the cue tables while they change (Systems review)
    weaponAudioLoaded_.clear();                // the level's cues are released with it
    resetSystemsForMatch();                    // player-side sounds + Systems FX + queues
    levelAudio_.unload();                      // music player, every instance, level cues / samples / presets, Flush
}

void World::resetSystemsForMatch() {
    joinStep();   // [integration 09c] async step: no background part may play into the cue tables while they change (Systems review)
    cues_.stopNonMapInstances();               // weapon / vehicle / foley / transform / pickup sounds (immediate)
    vehicleAudio_.stopAll(cues_);              // [Systems M08d]
    weaponAudio_.stopAll(cues_);
    abilityAudio_.stopAll(cues_);
    for (auto& kv : participantBodies_) kv.second.vehicle.stopAll(cues_);   // [Systems M09f]
    participantBodies_.clear();
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
    if (owner >= kOwnParticipantBase) {                  // [Systems M09d] a participant's pawn (the hook the glue provides)
        core::Vec3 p;
        if (!participantPositionHook || !participantPositionHook(owner - kOwnParticipantBase, p)) return false;
        out = p + offset;
        return true;
    }
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
    for (const char* c : foleyCues_) if (!routeDialogNotify(c, audioProfile(), localPlayer_, atPawn(), 0.0f)) cues_.play(c, atPawn(), 0.0f);
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
            // [integration 09a] The FX EnergonColor is the character's own (RE b0d9b22): customization energon (the selection has
            // none) else the chassis material default (character.json energon_default). Never the team colour.
            if (!chassisEnergonDefault(localChassis_, fi.energon)) fi.energon[0] = fi.energon[1] = fi.energon[2] = 1.0f;
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

    static const bool boostLog = std::getenv("WFC_BOOSTLOG") != nullptr;
    if (boostLog) {
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


// Pawn blocking [CONF RE addendum 11]: robot form = the actor cylinder with collide + block actors (stock bBlockActors; allies block
// like enemies; per-chassis CollisionRadius / Height); vehicle form = the vehicle mesh rigid body (cylinder disabled), contact by
// physics (+ Rammed). The native sweep / rigid-body contact is not reproduced: overlaps are pushed apart after movement, a push that
// would enter world geometry being given to the other pawn [PROV method]. Robot-robot: cylinders, half each. Robot-vehicle: the robot
// cylinder out of the vehicle's mesh box (meshMatrix orientation, horizontal), the robot moving (the vehicle when the robot is
// against a wall). Vehicle-vehicle: inscribed circles of the boxes [PROV]. Ram damage / reactions come first (gameplayRamContacts).
// Bots yield to the local player (the human moves only when the bot cannot), so a crowd never shoves or pins the human.
void World::separatePawns() {
    if (!matchActive_) return;
    // R: a conservative XZ reach about position() (robot cylinder; vehicle mesh box: its offset from the position + circumradius). A pair
    // farther apart than R_i + R_j cannot interact in either branch below, so it is skipped before the exact tests (same pairs, same
    // order, same pushes; positions are read as they change). 2 passes x N^2 / 2 pairs at 64 pawns.
    struct P { Character* c; float r; };
    static std::vector<P> ps;
    ps.clear();
    auto reach = [](Character& c) {
        float r = c.cylinderRadius(c.moveForm());
        if (c.moveForm() == Form::Vehicle) {
            r = std::max(r, c.cylinderRadius(Form::Vehicle));
            core::Vec3 mn, mx;
            if (c.vehicleBoundsXZ(mn, mx)) {
                const core::Mat4 m = c.simMatrix(Form::Vehicle);
                const float ox = m.m[12] - c.position().x, oz = m.m[14] - c.position().z;
                const float cx = std::max(std::fabs(mn.x), std::fabs(mx.x)), cz = std::max(std::fabs(mn.z), std::fabs(mx.z));
                const float sx = std::sqrt(m.m[0] * m.m[0] + m.m[2] * m.m[2]), sz = std::sqrt(m.m[8] * m.m[8] + m.m[10] * m.m[10]);   // box axes scale
                r = std::max(r, std::sqrt(ox * ox + oz * oz) + std::sqrt(cx * cx * sx * sx + cz * cz * sz * sz));
            }
        }
        return r + 0.01f;
    };
    static std::vector<char> human;   // per ps entry: a human seat (the bots yield to it)
    human.clear();
    if (seatOrdered_) {   // online: by match player index, every human seat alike
        for (size_t q = 0; q < match_.players().size(); ++q)
            if (Character* c = participantPawnMutable((int)q)) { ps.push_back({c, reach(*c)}); human.push_back(seatHuman((int)q) ? 1 : 0); }
    } else {
        if (!localDead_) { ps.push_back({&player_.pawn(), reach(player_.pawn())}); human.push_back(1); }
        for (MatchOpponent* o : opponents_) if (o->spawned()) { ps.push_back({&o->pawn(), reach(o->pawn())}); human.push_back(0); }
    }
    const CollisionWorld* col = collision_.valid() ? &collision_ : nullptr;
    auto tryMove = [&](Character& c, const core::Vec3& d) {
        // The pushed pawn keeps its whole radius clear of walls (knee and centre height): a push never wedges a pawn into a pocket
        // narrower than itself (an off-mesh trap the movement probes then cannot leave).
        const float dl = core::length(d);
        if (dl < 1e-5f) return true;
        const core::Vec3 dn = d * (1.0f / dl);
        const float r = c.cylinderRadius(c.moveForm());
        float t;
        if (col)
            for (float h : {0.6f, 2.0f}) {
                const core::Vec3 a = c.position() + core::Vec3{0, h, 0};
                if (col->segmentHit(a, a + dn * (dl + r), t)) return false;
            }
        c.setPosition(c.position() + d);
        return true;
    };
    for (int pass = 0; pass < 2; ++pass)   // a second pass settles pushes into third pawns
    for (size_t i = 0; i < ps.size(); ++i)
        for (size_t j = i + 1; j < ps.size(); ++j) {
            Character& A = *ps[i].c; Character& B = *ps[j].c;
            const core::Vec3 pa = A.position(), pb = B.position();
            { const float dx = pb.x - pa.x, dz = pb.z - pa.z, rr = ps[i].r + ps[j].r; if (dx * dx + dz * dz >= rr * rr) continue; }   // cannot touch
            if (std::fabs(pa.y - pb.y) >= A.cylinderHalfHeight(A.moveForm()) + B.cylinderHalfHeight(B.moveForm())) continue;
            const bool va = A.moveForm() == Form::Vehicle, vb = B.moveForm() == Form::Vehicle;
            const bool humanA = human[i] && !human[j], humanB = human[j] && !human[i];   // offline the local pawn is only ever ps[0]
            if (va != vb) {   // robot cylinder vs vehicle mesh box
                Character& V = va ? A : B; Character& Rb = va ? B : A;
                core::Vec3 mn, mx;
                if (!V.vehicleBoundsXZ(mn, mx)) continue;
                const core::Mat4 m = V.simMatrix(Form::Vehicle);
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
                const bool humanIsRobot = va ? humanB : humanA, humanIsVehicle = va ? humanA : humanB;
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
            if (humanB) { if (!tryMove(A, half * -2.0f)) tryMove(B, half * 2.0f); continue; }   // online: a human seat later in the order
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
    spawnMuzzleLight(weaponClass, sw, localPlayer_);
    static const bool log = std::getenv("WFC_MUZZLELOG") != nullptr;
    if (log) LOG_INFO("VEHICLE MUZZLE shot %d socket %d %s at %.2f,%.2f,%.2f", vehicleShotSerial_, vehicleShotSocket_,
                      wfx ? wfx->muzzle.c_str() : "(no fx)", sw.m[12], sw.m[13], sw.m[14]);
    return true;
}

void World::spawnMuzzleLight(const std::string& weaponClass, const core::Mat4& socketWorld, int owner) {
    const MuzzleLightDef* d = muzzleLightFor(weaponClass);
    if (!d) return;
    LiveTransientLight l;
    l.pos = {socketWorld.m[12], socketWorld.m[13], socketWorld.m[14]};
    l.color = {d->color[0], d->color[1], d->color[2]};
    // [Systems] TnWeaponMesh.Attach -> ApplyCharacterColorsToWeapon: MuzzleFlashLight.SetLightProperties(, Pawn.EnergonColor) - the
    // owner's own energon colour (customization, else the chassis default; never the team colour), Brightness kept (RE
    // TIME_MATERIALS_LIGHTS_RE, CONFIRMED script). The authored colour stays as the fallback.
    {
        const Character* p = owner == localPlayer_ ? &player_.pawn() : participantPawn(owner);
        float e[3];
        static const bool authored = std::getenv("WFC_MUZZLEENERGON") && std::getenv("WFC_MUZZLEENERGON")[0] == '0';   // A/B: the authored colour
        if (!authored && p && chassisEnergonDefault(owner == localPlayer_ ? localChassis_ : p->chassis().id, e))
            l.color = {e[0] * d->brightness, e[1] * d->brightness, e[2] * d->brightness};
    }
    l.radius = d->radius; l.falloff = d->falloff; l.lightTime = d->duration; l.key = owner;
    static const bool log = std::getenv("WFC_MUZZLELOG") != nullptr;
    static int logged = 0;
    if (log && logged < 40) {
        ++logged;
        LOG_INFO("MUZZLE LIGHT %s owner %d at %.2f,%.2f,%.2f r %.2f m colour %.2f,%.2f,%.2f falloff %.0f for %.2f s", weaponClass.c_str(),
                 owner, l.pos.x, l.pos.y, l.pos.z, l.radius, l.color.x, l.color.y, l.color.z, l.falloff, l.lightTime);
    }
    addTransientLight(l);   // one MuzzleFlashLight component per weapon: a new shot restarts it
}

void World::spawnDeathLight(const std::string& chassisId, bool vehicleForm, const std::string& damageType, const core::Vec3& pos, int lit) {
    // The victim's death record (newest first): its player (the light's key), headshot and pawn (the vehicle form type).
    int player = -1;
    bool headshot = false, jet = false;
    for (auto it = match_.gameplayEvents().rbegin(); it != match_.gameplayEvents().rend(); ++it) {
        if (it->type != GameplayEventType::Kill && it->type != GameplayEventType::Suicide && it->type != GameplayEventType::EnvironmentDeath) continue;
        const Character* p = it->victim == localPlayer_ ? &player_.pawn() : participantPawn(it->victim);
        if (!p || p->chassis().id != chassisId || core::length(p->actorLocation() - pos) > 0.01f) continue;
        player = it->victim; headshot = it->headshot; jet = p->chassis().vehicle.form == VehicleFormType::Jet;
        break;
    }
    const DeathLightDef* d = nullptr;
    if (lit >= 0) {   // [Gameplay] the corpse's picked death entry carries TnDeathModifierTriggerLight (body and light agree)
        if (lit) d = vehicleForm ? &kVehicleDeathLight : &kRobotDeathLight;
    } else if (vehicleForm) {
        if (!jet) d = &kVehicleDeathLight;   // SharedJetDeaths: no light
    } else if (!headshot) {
        for (const char* t : kLitRobotDeathDamageTypes)
            if (damageType == t) { d = &kRobotDeathLight; break; }
    }
    static const bool log = std::getenv("WFC_MUZZLELOG") != nullptr;
    if (log) LOG_INFO("DEATH LIGHT player %d %s %s (%s%s): %s", player, chassisId.c_str(), vehicleForm ? (jet ? "jet" : "vehicle") : "robot",
                      damageType.c_str(), headshot ? ", headshot" : "", d == &kVehicleDeathLight ? "vehicle light" : d ? "robot light" : "none");
    if (!d) return;
    LiveTransientLight l;
    l.pos = pos;
    l.color = {d->color[0], d->color[1], d->color[2]};
    l.radius = d->radius; l.falloff = d->falloff; l.rampIn = d->rampIn; l.lightTime = d->lightTime; l.rampOut = d->rampOut;
    l.key = player >= 0 ? 1000 + player : 100000 + (unkeyedDeaths_++ & 1023);   // the victim pawn's DeathLight component
    addTransientLight(l);
}

void World::fxLight(const std::string& tmpl, const core::Vec3& pos) {
    // Realistic only, decided at spawn from the last drawn frame's mode: in Off / Faithful no effect light even enters the live
    // list (its expiry would reorder the original muzzle / death lights there).
    if (!fxLightsOn() || fxRtMode_ != 2) return;
    const FxLightDef* d = fxLightFor(tmpl);
    if (!d || d->duration <= 0.0f) return;   // continuous rows (boost, pickups) are submitted per frame in draw
    LiveTransientLight l;
    l.pos = pos;
    l.color = {d->color[0], d->color[1], d->color[2]};
    l.radius = d->radius; l.falloff = d->falloff;
    l.lightTime = 0.5f * d->duration; l.rampOut = 0.5f * d->duration;   // full, then fades out over the second half
    l.key = 200000 + (fxLightSerial_++ & 0xffff); l.rtOnly = true;
    static const bool log = std::getenv("WFC_MUZZLELOG") != nullptr;
    static int logged = 0;
    if (log && logged < 40) {
        ++logged;
        LOG_INFO("FX LIGHT %s at %.2f,%.2f,%.2f r %.1f m colour %.2f,%.2f,%.2f for %.2f s (Realistic only)", tmpl.c_str(), pos.x, pos.y, pos.z,
                 l.radius, l.color.x, l.color.y, l.color.z, d->duration);
    }
    addTransientLight(l);
}

void World::addTransientLight(const LiveTransientLight& l) {
    for (LiveTransientLight& o : transientLights_)
        if (o.key == l.key) { o = l; return; }
    transientLights_.push_back(l);
}

void World::tickTransientLights(float dt) {
    for (size_t i = 0; i < transientLights_.size();) {
        LiveTransientLight& l = transientLights_[i];
        if ((l.age += dt) >= std::max(l.lightTime, l.rampIn) + l.rampOut || (l.cutAt >= 0.0f && l.age >= l.cutAt)) { l = transientLights_.back(); transientLights_.pop_back(); }
        else ++i;
    }
}

bool World::heldWeaponMuzzleImpl(core::Vec3& out) const {
    const Weapon& w = player_.pawn().weapon();
    if (shownWeapon_ != (w.def ? w.def->id : "IonBlaster")) return false;
    core::Mat4 ms;
    if (!weaponSocketWorld("MuzzleFlash", ms)) return false;
    out = {ms.m[12], ms.m[13], ms.m[14]};
    return true;
}

bool World::participantMuzzleWorld(int player, core::Mat4& out) const {
    if (player == localPlayer_) return !localDead_ && player_.pawn().hasWeapon() && weaponSocketWorld("MuzzleFlash", out);   // the online local seat
    const MatchOpponent* o = player >= 0 && (size_t)player < oppByPlayer_.size() ? oppByPlayer_[(size_t)player] : nullptr;
    if (!o || !o->spawned() || !o->pawn().hasWeapon()) return false;
    auto it = partWeapons_.find(player);
    core::Mat4 local;
    if (it == partWeapons_.end() || !it->second.anim.valid() || !it->second.anim.socketLocal("MuzzleFlash", local)) return false;
    out = o->pawn().weaponWorld() * local;
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
    weaponUse_[d.id] = cacheGen_;
    if (it != weaponModels_.end()) return it->second.get();
    if (!onMainThread()) {   // the async background part: GL uploads only on the main thread; loaded at the join, retried next step
        deferredWeaponLoads_.push_back(&d);
        return nullptr;
    }
    auto m = std::make_unique<assets::SkinnedModel>();
    const std::string ext = assetRoot() + "/../";
    bool ok = d.meshGltf && *d.meshGltf && assets::loadSkinnedGlb(ext + d.meshGltf, *m) && m->valid();
    if (ok) {
        if (d.animGltf && *d.animGltf) assets::loadAnimationsByName(ext + d.animGltf, *m);
        resolveModelTextures(*m);
        if (renderer_) { render::MeshData md; md.subs = m->subs; md.mats = m->mats; fxPrewarm(*renderer_, md, 0); prewarmSkin(*m); }
    } else LOG_ERROR("weapon %s: mesh %s unavailable", d.id, d.meshGltf ? d.meshGltf : "-");
    const assets::SkinnedModel* raw = ok ? m.get() : nullptr;
    ++weaponLoads_;
    if (matchActive_ && match_.state() == Match::State::InProgress)   // a load during play is a frame hitch: every weapon a pawn can hold should load at the match load
        LOG_WARN("weapon model %s loaded during play (local weapon %s)", d.id, player_.pawn().hasWeapon() && player_.pawn().weapon().def ? player_.pawn().weapon().def->id : "-");
    weaponModels_[d.id] = std::move(m);
    return raw;
}

// The weapon mesh drawn at the socket is the ACTIVE inventory weapon's (no other weapon may be shown in its place).
double World::profileWeaponModelLoad(const WeaponDef& d) { const double t0 = profNowMs(); weaponModelFor(d); return profNowMs() - t0; }

void World::preloadSelections(const std::vector<CharacterSelection>& selections) {
    const int fa = localPlayer_ >= 0 ? match_.faction(localPlayer_) : 0;
    for (const CharacterSelection& sel : selections) {
        const std::string chassis = resolveChassis(sel, fa);
        const ChassisAssets* ca = chassisAssets(chassis);
        if (sel.type == 0 && !sel.weapons.empty()) preloadHeldWeaponModels(sel.weapons);
        else if (ca) preloadHeldWeaponModels(ca->def.iconicWeapons);
        if (ca) queueSelectionAudio(chassis, (sel.type == 0 && !sel.weapons.empty()) ? sel.weapons : ca->def.iconicWeapons,
                                      (sel.type == 0 && !sel.vehicleWeapons.empty()) ? sel.vehicleWeapons : ca->def.iconicVehicleWeapons);
    }
}

// [integration 09a/09b] Load scheduling (PC ADAPTATION): every preloaded selection's character cue set and loadout weapon cues
// are warmed under the match load by Systems M09c preloadSelectionAudio (worker decode; re-applied at level-audio load), so
// the spawn frame's setPlayerCharacterAudio / applyLoadout find them loaded (09a flow: 160-207 + 31-87 ms -> 0.6 ms).
void World::queueSelectionAudio(const std::string& chassis, const std::vector<std::string>& weapons,
                                const std::vector<std::string>& vehicleWeapons) {
    std::vector<std::string> cls;
    for (const std::vector<std::string>* list : {&weapons, &vehicleWeapons})
        for (const std::string& n : *list)
            if (const WeaponDef* d = findWeaponDef(n)) cls.push_back("TransContent.TnWeapon" + std::string(d->id));
    preloadSelectionAudio(chassis.empty() ? std::vector<std::string>{} : std::vector<std::string>{chassis}, cls);
}

void World::preloadHeldWeaponModels(const std::vector<std::string>& weapons) {
    for (const std::string& n : weapons) {
        const WeaponDef* d = findWeaponDef(n);
        // Only weapons that can be the held weapon (primary / heavy, melee, and grenade bags: held while tossed) and draw a mesh; the Ion Blaster uses the boot model.
        if (!d || (d->typeCode != 0 && d->typeCode != 1 && d->typeCode != 2 && d->typeCode != 4) || !d->meshGltf || !*d->meshGltf || std::string(d->id) == "IonBlaster") continue;
        weaponModelFor(*d);
    }
}

bool tickProfOn();
void tickProfAdd(int slot, double ms);   // WFC_TICKPROF (defined below)

void World::tickParticipantWeapons(float dt) {
    // Held weapon views follow each participant's active robot weapon (model swap on a switch, fire / reload event anims).
    static std::vector<ParticipantWeaponView*> ticking;
    ticking.clear();
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned()) continue;
        ParticipantWeaponView& v = partWeapons_[o->matchPlayer()];
        const Weapon& w = o->pawn().weapon();
        const std::string id = w.def ? w.def->id : "IonBlaster";
        if (id != v.id) {
            if (id != "IonBlaster" && w.def && !onMainThread() && !weaponModels_.count(w.def->id)) { weaponModelFor(*w.def); continue; }   // loaded at the join
            v.id = id;
            if (id == "IonBlaster") v.anim.setModel(weaponModel_.valid() ? &weaponModel_ : nullptr);
            else if (const assets::SkinnedModel* m = w.def ? weaponModelFor(*w.def) : nullptr) v.anim.setModelGeneric(m, *w.def);
            else v.anim.setModel(nullptr);
            v.seenShot = w.shotSerial; v.seenReload = w.reloadSerial;
        }
        v.player = o->matchPlayer();
        if (w.reloadSerial != v.seenReload) { v.seenReload = w.reloadSerial; v.anim.play(WeaponMesh::Event::Reload); }
        if (w.shotSerial != v.seenShot) { v.seenShot = w.shotSerial; v.anim.play(WeaponMesh::Event::Fire); }
        ticking.push_back(&v);
    }
    // The weapon pose ticks are independent per participant: on the worker pool (map entries created above, serially).
    core::WorkerPool::get().run((int)ticking.size(), [&](int i) {
        ParticipantWeaponView& v = *ticking[(size_t)i];
        v.fired.clear();
        v.anim.tick(dt, v.fired);
    });
    // [Systems] participant weapon notify effects (shells, magazines, reload FX) within 40 m of the local pawn [PROV distance]:
    // presented on the main thread from the views' fired lists (their sounds: the class sound timelines, as before).
    {
        const core::Vec3 eye = player_.pawn().position();
        for (ParticipantWeaponView* v : ticking) {
            if (v->fired.empty() || v->player < 0) continue;
            const MatchOpponent* o = (size_t)v->player < oppByPlayer_.size() ? oppByPlayer_[(size_t)v->player] : nullptr;
            if (!o || !o->spawned() || !o->pawn().hasWeapon() || core::length(o->pawn().position() - eye) > 40.0f) continue;
            for (const WeaponNotify& n : v->fired) {
                if (n.kind != WeaponNotify::Kind::Effect) continue;
                core::Mat4 local;
                if (!v->anim.socketLocal(n.socket, local)) continue;
                spawnWeaponNotifyEffect(n, o->pawn().weaponWorld() * local, fxTeamOf(v->player), false);
            }
        }
    }
    // This step's participant shots: the muzzle socket of the shooter's shown weapon (else its eye frame along the shot).
    for (const ParticipantShot& s : participantShots_) {
        if (s.weapon == "RepairRay") continue;   // the beam has its own looping presentation [PARTIAL for bots]
        // Pellets after the first: impact only (the hook gets them with s.pellet > 0; the fallback below draws muzzle / tracer only).
        core::Mat4 muzzle = core::Mat4::identity();
        const bool have = participantMuzzleWorld(s.player, muzzle);
        if (!have) {
            const core::Vec3 f = core::normalize(s.to - s.from);
            const core::Vec3 rt = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            const core::Vec3 up = core::cross(rt, f);
            muzzle.m[0] = f.x; muzzle.m[1] = f.y; muzzle.m[2] = f.z; muzzle.m[4] = up.x; muzzle.m[5] = up.y; muzzle.m[6] = up.z;
            muzzle.m[8] = rt.x; muzzle.m[9] = rt.y; muzzle.m[10] = rt.z; muzzle.m[12] = s.from.x; muzzle.m[13] = s.from.y; muzzle.m[14] = s.from.z;
        }
        if (participantShotFxHook) {
            presentHook([this, s, muzzle] {
                const double fx0 = tickProfOn() ? profNowMs() : 0.0;
                participantShotFxHook(s, muzzle);
                if (tickProfOn()) tickProfAdd(25, profNowMs() - fx0);   // WFC_TICKPROF glue.shotFx
            });
        }
        else if (s.pellet == 0 && partShotFx_.size() < 256) {
            const WeaponDef* d = findWeaponDef(s.weapon);
            partShotFx_.push_back({s.weapon, muzzle, s.to, !(d && d->projSpeed > 0.0f), fxTeamOf(s.player)});   // projectiles draw their own flight effect
        }
    }
}

void World::syncShownWeapon() {
    const Weapon& w = player_.pawn().weapon();
    std::string id = w.def ? w.def->id : "IonBlaster";
    if (id == shownWeapon_) return;
    if (id != "IonBlaster" && w.def && !onMainThread() && !weaponModels_.count(w.def->id)) {   // loaded at the join; retried next step
        weaponModelFor(*w.def); seenWeaponChange_ = ~player_.pawn().weaponChangeSerial(); return;
    }
    struct ProfScope { const std::string& id; double t0 = profNowMs(); ~ProfScope() { if (spawnProf()) LOG_INFO("SPAWNPROF syncShownWeapon %s: %.1f ms", id.c_str(), profNowMs() - t0); } } profScope{id};
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
    const double t0 = profNowMs();
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
    const double t1 = profNowMs();
    // TnCharacterApplier.ApplyAbilities: CharacterData.Abilities (custom selection, else the iconic preset).
    pc.setAbilities(testAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities));
    const double t2 = profNowMs();
    syncShownWeapon();
    if (spawnProf()) LOG_INFO("SPAWNPROF applyLoadout: setLoadout %.1f ms, setAbilities %.1f ms, syncShownWeapon %.1f ms", t1 - t0, t2 - t1, profNowMs() - t2);
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
    static const bool notifyLog = std::getenv("WFC_NOTIFYLOG") != nullptr;
    if (notifyLog)
        LOG_INFO("NOTIFY %s %s @%.3f %s", n.kind == WeaponNotify::Kind::Sound ? "sound" : "fx",
                 n.what.c_str(), n.time, n.socket.c_str());
    if (n.kind == WeaponNotify::Kind::Effect) {
        // HmAnimNotify_PlayEffect: spawn the authored ParticleSystem at the notify's socket.
        core::Mat4 sw;
        if (weaponSocketWorld(n.socket.c_str(), sw)) spawnWeaponNotifyEffect(n, sw, fxTeamOf(localPlayer_), true);
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

// [Systems] a weapon AnimNotify effect: the hand-reconstructed ones (Ion Blaster shell / magazine / reload, WeaponFx) keep their
// path for the local weapon (WeaponFx holds the local muzzle state); every other authored template, and every participant's,
// goes through the map-FX system at the socket (UE socket X = forward, Z = up).
void World::spawnWeaponNotifyEffect(const WeaponNotify& n, const core::Mat4& sw, int team, bool local) {
    static const bool off = std::getenv("WFC_NOTIFYFX") && std::getenv("WFC_NOTIFYFX")[0] == '0';
    static const bool log = std::getenv("WFC_NOTIFYLOG") != nullptr;
    if (log) LOG_INFO("NOTIFYFX %s %s @%s", local ? "local" : "participant", n.what.c_str(), n.socket.c_str());
    if ((local && fx_.spawnNotifyEffect(n.what, sw)) || off || !renderer_) return;
    const core::Vec3 p{sw.m[12], sw.m[13], sw.m[14]}, f{sw.m[0], sw.m[1], sw.m[2]}, u{sw.m[4], sw.m[5], sw.m[6]};
    fxStepSpawn(n.what, p, core::normalize(f), core::normalize(u), team, false);
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
    if (Character::bgPalettes() && remainderPending_) {   // between a local part and its background part: the completed step's pose
        player_.pawn().setPoseAlpha(1.0f);
        for (MatchOpponent* o : opponents_) o->pawn().setPoseAlpha(1.0f);
    }
}

namespace {
// WFC_TICKPROF (diagnostics): accumulated ms per World::tick phase, logged every 300 steps.
struct TickProf {
    static bool env() { static const bool o = std::getenv("WFC_TICKPROF") != nullptr; return o; }
    static bool& forced() { static bool f = false; return f; }   // World::setStepProfiling (WFC_SCALETEST)
    static bool logOn() { static const bool o = std::getenv("WFC_TICKLOG") != nullptr; return o; }   // the per-step CSV
    static bool on() { return env() || forced() || logOn(); }
    double acc[48] = {}; double step[48] = {}; long n = 0;   // step: this sim step only (WFC_TICKLOG)
    const char* names[48] = {"pre", "abilities", "match", "bots", "oppMove", "oppAnim(cpu)", "partWeapons", "pawn+camera", "weaponFx", "projectiles", "actors", "rest",
                             "separate", "mapstate", "TOTAL", "ab.barrier", "ab.beacon", "ab.sentry", "ab.roller", "bot.think", "bot.steer", "bot.aim",
                             // integration glue blocks (wrapped with TickTimer at merge): 22 ability audio, 23 participant notifies,
                             // 24 participant audio loop (buff / hover / body / weapon), 25 participant shot FX hook, 26 character audio + cues
                             "glue.abilityAudio", "glue.partNotify", "glue.partAudio", "glue.shotFx", "glue.charAudio+cues", "ab.sentry.search", "ab.sentry.pose", "presentedFill",
                             "sv.director", "sv.kismet", "sv.mech", "cp.vm", "cp.matinee", "cp.touches", "cp.destructibles", "cp.interacts", "cp.sentries+mines", "cp.factories", "hazards", "fill.markers", "prefix", "s43", "s44", "s45", "s46", "s47"};
};
TickProf& tickProf() { static TickProf p; return p; }
}
bool tickProfOn() { return TickProf::on(); }
void World::setStepProfiling(bool on) { TickProf::forced() = on; }
void World::stepProfileReset() { for (double& a : tickProf().acc) a = 0.0; }
std::vector<std::pair<std::string, double>> World::stepProfileSums() {
    std::vector<std::pair<std::string, double>> v;
    for (int i = 0; i < 30; ++i) v.push_back({tickProf().names[i], tickProf().acc[i]});
    return v;
}
void tickProfAdd(int slot, double ms) { if (TickProf::on() && slot >= 0 && slot < 48) { tickProf().acc[slot] += ms; tickProf().step[slot] += ms; } }
namespace {
struct TickTimer { int slot; double t0; TickTimer(int s) : slot(s), t0(TickProf::on() ? profNowMs() : 0.0) {} ~TickTimer() { if (TickProf::on()) { const double ms = profNowMs() - t0; tickProf().acc[slot] += ms; tickProf().step[slot] += ms; } } };
std::atomic<long> gTickCounts[4];   // WFC_TICKLOG counters (LOS rays, path searches, thinks, spawn tries) since the last row
}
double tickProfNowMs() { return profNowMs(); }
void tickCount(int which) { if (TickProf::on() && which >= 0 && which < 4) gTickCounts[which].fetch_add(1, std::memory_order_relaxed); }

// The sim thread for the async step's background part (one job at a time).
struct World::AsyncStepThread {
    std::thread t;
    std::mutex m;
    std::condition_variable cv;
    std::function<void()> job;
    bool has = false, done = true, quit = false;
    AsyncStepThread() {
        t = std::thread([this] {
            for (;;) {
                std::function<void()> j;
                { std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&] { return has || quit; }); if (quit) return; j = std::move(job); has = false; }
                j();
                { std::lock_guard<std::mutex> lk(m); done = true; }
                cv.notify_all();
            }
        });
    }
    void launch(std::function<void()> j) { { std::lock_guard<std::mutex> lk(m); job = std::move(j); has = true; done = false; } cv.notify_all(); }
    void wait() { std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&] { return done; }); }
    ~AsyncStepThread() { { std::lock_guard<std::mutex> lk(m); quit = true; } cv.notify_all(); t.join(); }
};

// Default on (Experimental 05db936: the background part hides fully up to 32 v 32 uncapped; p90 lower; Systems audio PASS).
// WFC_ASYNCSTEP=0 is the escape hatch (the synchronous step); WFC_SIMTHREADS=0 (fully serial debugging) also forces it.
bool World::asyncStepEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("WFC_ASYNCSTEP");
        if (e && e[0] == '0') return false;
        const char* t = std::getenv("WFC_SIMTHREADS");
        if (t && std::atoi(t) == 0) return false;
        return true;
    }();
    return on;
}

void World::tick(float dt) {   // one whole step, synchronously (tests, tools, WFC_ASYNCSTEP off)
    tickPrefix(dt);
    joinStep();
}

void World::launchStep() {
    if (!remainderPending_ || remainderRunning_) return;
    if (!asyncStepEnabled() || !matchActive_) { finishRemainder(); return; }
    if (!asyncThread_) asyncThread_ = std::make_shared<AsyncStepThread>();
    remainderRunning_ = true; fillBack_ = true;
    asyncThread_->launch([this] { finishRemainder(); });
}

void World::joinStep() {
    if (remainderRunning_) {
        const double t0 = profNowMs();
        asyncThread_->wait();
        remainderRunning_ = false;
        static const bool log = std::getenv("WFC_ASYNCLOG") != nullptr;
        if (log) {
            static int n = 0; static double acc = 0.0, rem = 0.0;
            static std::vector<double> waits, rems;   // the window's samples: tails (firefight bursts), not only averages
            const double w = profNowMs() - t0;
            acc += w; rem += lastRemainderMs_; waits.push_back(w); rems.push_back(lastRemainderMs_);
            if (++n % 300 == 0) {
                auto tail = [](std::vector<double>& v, double& p99, double& mx) {
                    std::sort(v.begin(), v.end()); mx = v.empty() ? 0.0 : v.back(); p99 = v.empty() ? 0.0 : v[(size_t)((v.size() - 1) * 0.99)]; v.clear();
                };
                double wp99, wmax, rp99, rmax; tail(waits, wp99, wmax); tail(rems, rp99, rmax);
                LOG_INFO("ASYNCSTEP local part %.3f ms avg (beginStep %.3f), background part %.3f ms avg (p99 %.3f, max %.3f), main-thread join wait %.3f ms avg (p99 %.3f, max %.3f)",
                         prefixMsAcc_ / 300.0, beginStepMsAcc_ / 300.0, rem / 300.0, rp99, rmax, acc / 300.0, wp99, wmax);
                static const char* secNames[12] = {"commands", "beginStep", "bodyPreload", "mapState", "listener", "localBuffs", "abilities", "regen",
                                                   "match", "awards", "localController", "weaponPreload"};
                std::string sec;
                for (int i = 0; i < 12; ++i) { char b[48]; std::snprintf(b, sizeof b, " %s %.3f", secNames[i], prefixSecMs_[i] / 300.0); sec += b; prefixSecMs_[i] = 0.0; }
                LOG_INFO("ASYNCSTEP local part by section (ms / step):%s", sec.c_str());
                acc = rem = 0.0; prefixMsAcc_ = 0.0; beginStepMsAcc_ = 0.0;
            }
        }
    }
    if (remainderPending_) finishRemainder();          // not launched (catch-up step, sync mode): inline
    if (fillBack_) { publishPresented(); fillBack_ = false; }
    if (palettesPending_) {   // main thread, no draw in flight: next -> cur -> prev
        palettesPending_ = false;
        player_.pawn().publishPalettes();
        for (MatchOpponent* o : opponents_) o->pawn().publishPalettes();
    }
    // Main-thread work the background part asked for: model loads (GL uploads), retried by the next step.
    for (const WeaponDef* d : deferredWeaponLoads_) if (d) weaponModelFor(*d);
    deferredWeaponLoads_.clear();
    if (!deferredChassisGpu_.empty()) completeDeferredChassis();   // [Systems] bodies finished off the main thread
    if (!deferredHooks_.empty()) { std::vector<std::function<void()>> hooks; hooks.swap(deferredHooks_); for (auto& h : hooks) h(); }
    if (!fxOps_.empty()) { std::vector<FxOp> ops; ops.swap(fxOps_); for (const FxOp& op : ops) runFxOp(op); }
    tickCorpses(pendingDt_);   // [Gameplay] presentation only (main thread, after the deferred corpse requests ran)
    tickDeathCamera(pendingDt_);
}

// The background part's presented() goes to presentedBack_: the front's unconsumed queues stay first, the back's follow.
void World::publishPresented() {
    PresentedFrame& f = presented_;
    PresentedFrame& b = presentedBack_;
    auto front = [](auto& dst, auto& src) { dst.insert(dst.begin(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end())); src.clear(); };
    front(b.matchEvents, f.matchEvents); front(b.gameplayEvents, f.gameplayEvents); front(b.kills, f.kills);
    front(b.damageTaken, f.damageTaken); front(b.damageCaused, f.damageCaused); front(b.xpAwards, f.xpAwards); front(b.statAwards, f.statAwards);
    for (size_t i = 0; i < b.seats.size() && i < f.seats.size(); ++i) {   // split screen: each seat's unconsumed queues, front first
        if (b.seats[i].player != f.seats[i].player) continue;   // a seat that changed: the old player's entries go
        front(b.seats[i].damageTaken, f.seats[i].damageTaken); front(b.seats[i].damageCaused, f.seats[i].damageCaused);
    }
    b.steps += f.steps;
    std::swap(f, b);
    for (auto& s : b.seats) { s.damageTaken.clear(); s.damageCaused.clear(); }
    b.matchEvents.clear(); b.gameplayEvents.clear(); b.kills.clear(); b.damageTaken.clear(); b.damageCaused.clear();
    b.xpAwards.clear(); b.statAwards.clear(); b.steps = 0;
    constexpr long kCap = 8192;
    if ((long)f.matchEvents.size() > kCap) f.matchEvents.erase(f.matchEvents.begin(), f.matchEvents.end() - kCap);
    if ((long)f.gameplayEvents.size() > kCap) f.gameplayEvents.erase(f.gameplayEvents.begin(), f.gameplayEvents.end() - kCap);
    if ((long)f.kills.size() > kCap) f.kills.erase(f.kills.begin(), f.kills.end() - kCap);
}

void World::asyncJitter() {
    static const char* env = std::getenv("WFC_ASYNCJITTER");
    if (!env) return;
    thread_local unsigned s = 0;
    if (s == 0) s = (unsigned)std::strtoul(env, nullptr, 10) * 2654435761u + (unsigned)std::hash<std::thread::id>{}(std::this_thread::get_id()) + 1u;
    s = s * 1664525u + 1013904223u;
    const unsigned ms = (s >> 16) % 9u;
    if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void World::finishRemainder() {
    asyncJitter();   // WFC_ASYNCJITTER: the background part starts late by a seeded 0-8 ms (determinism self-test)
    const double t0 = profNowMs();
    stepRemainder(pendingDt_);
    // WFC_SIMHASH (determinism diagnostics): an FNV-1a hash of every pawn's exact state and every bot's steering state per step; with
    // WFC_SIMHASH=<step>, that step's per-pawn values too. Compare a serial (WFC_SIMTHREADS=0) and a threaded run of the same seed.
    static const long stateDumpAt = std::getenv("WFC_NET_STATEDUMP") ? std::atol(std::getenv("WFC_NET_STATEDUMP")) : -1;
    if (stateDumpAt >= 0 && (long)simTick_ == stateDumpAt) {
        LOG_INFO("STATEDUMP tick %lld local p%d online %d simRand %08x matchRand %08x", (long long)simTick_, localPlayer_, (int)onlineSeats_, core::simRandState(), match_.rngState());
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const Character* c = participantPawn((int)q);
            if (!c) { LOG_INFO("STATEDUMP p%zu -", q); continue; }
            const core::Vec3 p = c->position(), v = c->velocity(); const Weapon& w = c->weapon();
            LOG_INFO("STATEDUMP p%zu pos %.6f %.6f %.6f vel %.6f %.6f %.6f yaw %.6f hp %.4f form %d tr %d pose %016llx drawYaw %.6f w %s ammo %d since %.4f rel %d ab %.3f/%.3f hover %d",
                     q, p.x, p.y, p.z, v.x, v.y, v.z, c->yaw(), c->health().current, (int)c->moveForm(), (int)c->isTransforming(), (unsigned long long)c->poseHash(),
                     c->drawYawOffset(), w.def ? w.def->id : "-", w.ammo, w.sinceShot, (int)w.reloading(), c->abilities_[0].cooldown, c->abilities_[1].cooldown, c->hoverState_);
        }
        for (const BotBrain& b : bots_) LOG_INFO("STATEDUMP bot p%d rng %08x tgt %d yaw %.6f wp %zu/%zu", b.player, b.rng, b.target, b.yaw, b.wp, b.path.size());
        for (size_t q = 0; q < smart_.size(); ++q) if (smart_[q].active) LOG_INFO("STATEDUMP smart p%zu rng %08x", q, smart_[q].rng);
        for (const auto& pr : projectiles_) LOG_INFO("STATEDUMP proj inst %d pos %.6f %.6f %.6f vel %.6f %.6f %.6f life %.4f", pr.instigator, pr.pos.x, pr.pos.y, pr.pos.z, pr.vel.x, pr.vel.y, pr.vel.z, pr.life);
    }
    static const char* simHashEnv = std::getenv("WFC_SIMHASH");
    if (const char* sh = simHashEnv) {
        static long step = 0; ++step;
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](const void* p, size_t n) { const unsigned char* c = (const unsigned char*)p; for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; } };
        const long detail = std::atol(sh); const char* dash = std::strchr(sh, '-'); const long detailTo = dash ? std::atol(dash + 1) : detail;
        static const long period = std::getenv("WFC_SIMHASH_PERIOD") ? std::atol(std::getenv("WFC_SIMHASH_PERIOD")) : 0;   // two runs in one process
        const long inRun = period > 0 ? (step - 1) % period + 1 : step;
        const bool inDetail = inRun >= detail && inRun <= detailTo && detail > 0;
        auto pawn = [&](const Character& c, int player) {
            const core::Vec3 p = c.position(), v = c.velocity(); const float hp = c.health().current, y = c.yaw();
            mix(&p, sizeof p); mix(&v, sizeof v); mix(&hp, sizeof hp); mix(&y, sizeof y);
            if (inDetail && player == localPlayer_) LOG_INFO("SIMHASH %ld local camYaw %.6f simYaw? viewYaw %.6f", step, player_.controller().camYaw(), player_.controller().viewYaw());
            if (inDetail) LOG_INFO("SIMHASH %ld p%d pos %.6f %.6f %.6f vel %.6f %.6f %.6f hp %.4f yaw %.6f", step, player, p.x, p.y, p.z, v.x, v.y, v.z, hp, y);
        };
        if (seatOrdered_) { for (size_t q = 0; q < match_.players().size(); ++q) if (const Character* c = participantPawn((int)q)) pawn(*c, (int)q); }   // online: by index
        else {
        if (!localDead_) pawn(player_.pawn(), localPlayer_);
        for (MatchOpponent* o : opponents_) if (o->spawned()) pawn(o->pawn(), o->matchPlayer());
        }
        for (const BotBrain& b : bots_) {
            mix(&b.wp, sizeof b.wp); mix(&b.yaw, sizeof b.yaw); mix(&b.pitch, sizeof b.pitch); mix(&b.target, sizeof b.target);
            const size_t ps = b.path.size(); mix(&ps, sizeof ps);
            if (inDetail) LOG_INFO("SIMHASH %ld bot p%d wp %zu/%zu yaw %.6f tgt %d", step, b.player, b.wp, b.path.size(), b.yaw, b.target);
        }
        LOG_INFO("SIMHASH %ld %016llx", step, (unsigned long long)h);
    }
    { TickTimer tf(29); fillPresented(fillBack_ ? presentedBack_ : presented_); }
    // A frame may have drawn (and cached the palettes of) the poses before this part animated them.
    player_.pawn().invalidatePalettes();
    for (MatchOpponent* o : opponents_) o->pawn().invalidatePalettes();
    if (Character::bgPalettes()) {   // the bone palettes of this step, off the main thread (pawns culled at the last draw: built at draw)
        if (!localDead_) player_.pawn().buildNextPalettes();
        core::WorkerPool::get().run((int)opponents_.size(), [&](int i) {
            MatchOpponent* o = opponents_[(size_t)i];
            if (o->spawned() && !o->culled()) o->pawn().buildNextPalettes();
        });
        palettesPending_ = true;
    }
    remainderPending_ = false;
    lastRemainderMs_ = profNowMs() - t0;
    if (TickProf::logOn()) {   // WFC_TICKLOG: this step's row (the prefix ran before this remainder)
        static FILE* f = nullptr; static long stepNo = 0;
        if (!f && (f = std::fopen(std::getenv("WFC_TICKLOG"), "w")) != nullptr) {
            std::fprintf(f, "step,participants,horde,prefix_ms,remainder_ms,total_ms,los,searches,thinks,spawntries");
            for (int i = 0; i < 48; ++i) std::fprintf(f, ",%s", tickProf().names[i]);
            std::fprintf(f, "\n");
        }
        if (f) {
            int horde = 0; for (const MatchPlayer& mp : match_.players()) horde += mp.directorSpawned && mp.alive;
            const double pre = tickProf().step[kTsPrefix];
            std::fprintf(f, "%ld,%zu,%d,%.4f,%.4f,%.4f", ++stepNo, match_.players().size(), horde, pre, lastRemainderMs_, pre + lastRemainderMs_);
            for (int c = 0; c < 4; ++c) std::fprintf(f, ",%ld", gTickCounts[c].exchange(0));
            for (int i = 0; i < 48; ++i) std::fprintf(f, ",%.4f", tickProf().step[i]);
            std::fprintf(f, "\n");
            if (stepNo % 600 == 0) std::fflush(f);
        }
        for (double& s : tickProf().step) s = 0.0;
    }
}

// Weapon models of every held weapon, loaded on the main thread before the background part may switch to one (GL uploads).
void World::preloadHeldWeaponsOfPawns() {
    // Change-driven: a def is looked up in the (string-keyed) model cache once; afterwards a sorted pointer search (~256 per step at 64).
    auto pre = [&](const Character& c) {
        for (const Weapon& w : c.inventory()) {
            if (!w.def || !w.def->meshGltf || !*w.def->meshGltf) continue;
            auto it = std::lower_bound(preloadedDefs_.begin(), preloadedDefs_.end(), w.def);
            if (it != preloadedDefs_.end() && *it == w.def) continue;
            weaponModelFor(*w.def);
            preloadedDefs_.insert(it, w.def);
        }
    };
    pre(player_.pawn());
    for (MatchOpponent* o : opponents_) if (o->spawned()) pre(o->pawn());
}

void World::tickPrefix(float dt) {
    joinStep();
    seat2QueueCommand();   // split screen: the second local seat's command for this step (applied with the remote ones)
    if (seat2_ >= 0 && seat2Ctl_.pawn()) seat2Ctl_.tickCameraCollision(dt);
    applyQueuedRemoteCommands(dt);   // the Remote seats' commands for this step (main thread, after the background part joined)
    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)
    applyPendingLateSeats();    // join in progress: dormant seats waking this tick (before their picks)
    applyPendingSelections();   // online Choose Character picks due this tick (before spawns)
    tickSeatMissiles(dt);   // the seats' guided missiles (every peer, by match player index)
    static const bool secLog = std::getenv("WFC_ASYNCLOG") != nullptr;
    auto prefixMark = [&](int i) { if (!secLog) return; const double t = profNowMs(); prefixSecMs_[i] += t - prefixMark_; prefixMark_ = t; };
    prefixMark_ = secLog ? profNowMs() : 0.0;
    struct PrefixTime { double& acc; double t0 = profNowMs(); ~PrefixTime() { acc += profNowMs() - t0; } } prefixTime{prefixMsAcc_};
    if (TickProf::env() && ++tickProf().n % 300 == 0) {
        std::string line;
        for (int i = 0; i < 30; ++i) { char b[48]; std::snprintf(b, sizeof b, " %s %.2f", tickProf().names[i], tickProf().acc[i] / 300.0); line += b; tickProf().acc[i] = 0.0; }
        LOG_INFO("TICKPROF ms/step (%zu participants):%s", match_.players().size(), line.c_str());
    }
    TickTimer tickTotal(14);   // WFC_TICKPROF: the whole step (this local part + the background part)
    TickTimer prefixTotal(kTsPrefix);   // WFC_TICKLOG: this local part alone
    if (!commands_.empty()) {   // submitted commands, in order, at the step boundary
        std::vector<std::function<void(World&)>> cmds; cmds.swap(commands_);
        for (auto& c : cmds) c(*this);
    }
    pendingDt_ = dt; remainderPending_ = true;   // the background part (finishRemainder) completes this step
    prefixMark(0);
    // The local pawn's presentation yaw offset (set per render frame by the controller) is not simulation state: the step's
    // meshMatrix / sockets use the simulated yaw only (the next frame's input pass sets the offset again for drawing).
    player_.pawn().setDrawYawOffset(0.0f);
    // Presentation interpolation: remember each pawn's state at the start of the step.
    {
        const double b0 = profNowMs();
        player_.pawn().beginStep();
        core::WorkerPool::get().run((int)opponents_.size(), [&](int i) { opponents_[(size_t)i]->pawn().beginStep(); });   // each its own pawn
        beginStepMsAcc_ += profNowMs() - b0;
    }
    prefixMark(1);
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
    if (matchActive_) {
        // [Systems] the first-time load runs as a background job (startChassisJob) and finishes on a later tick; WFC_CHASSISASYNC=0
        // keeps the synchronous load here (A/B)
        static const bool async = [] { const char* e = std::getenv("WFC_CHASSISASYNC"); return !(e && e[0] == '0'); }();
        if (async && chassisJobReady()) finishChassisJob();
        if (!async || !chassisJob_)
            for (size_t p = 0; p < match_.players().size(); ++p) {
                const MatchPlayer& mp = match_.players()[p];
                if (!mp.hasSelectedCharacter) continue;
                const std::string id = resolveChassis(mp.selection, match_.faction((int)p));
                if (chassisCache_.count(id)) continue;
                if (async) startChassisJob(id);
                else chassisAssets(id);
                break;
            }
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
    prefixMark(2);
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
    if (collision_.valid()) { TickTimer tt(13); mapState_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
                              transforming_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
                              tickTransformingActorSounds();
                              tickDyingBodies(dt); }
    prefixMark(3);
    {   // Audio listener = camera (same pose the app hands to IAudio::setListener).
        render::Camera cam;
        player_.controller().updateCamera(cam);
        listenerPos_ = cam.pos;
    }
    prefixMark(4);
    // Health regeneration (robot blueprint HealthRegenParameters, both forms) for every live pawn.
    {
        Character& lp = player_.pawn();
        if (onlineSeats_) {}   // online: the seat ticks (tickRemoteSeats) run these for every seat
        else {
        lp.regenBuffRemain_ = std::max(0.0f, lp.regenBuffRemain_ - dt);
        lp.fastCooldownRemain_ = std::max(0.0f, lp.fastCooldownRemain_ - dt);
        }
        if (onlineSeats_) {}
        else if (lp.ammoLockRemain_ > 0.0f) {                   // TnBuffLockAmmoClip: the clip does not drain while active
            lp.ammoLockRemain_ = std::max(0.0f, lp.ammoLockRemain_ - dt);
            lp.weapon().ammo = std::max(lp.weapon().ammo, lockedClip_);
        } else lockedClip_ = 0;
        {   // online the seat command carries these (consumed here so nothing accumulates)
            const bool ks = player_.controller().consumeKillstreakRequest(), ml = player_.controller().consumeMeleeRequest(), gr = player_.controller().consumeGrenadeRequest();
            if (!onlineSeats_) {
                if (matchActive_ && ks) triggerLocalKillstreak();
                if (ml) startLocalMelee(false);
                if (gr) startLocalGrenadeToss();
            }
        }
        if (deferredKillstreak_ && lp.moveForm() == Form::Robot && !lp.isTransforming()) { deferredKillstreak_ = false; triggerLocalKillstreak(); }
    }
    prefixMark(5);
    { TickTimer tt(1); tickAbilityEffects(dt); }
    { TickTimer tt(22); tickAbilityAudio(); }   // [Systems M08i]
    { TickTimer tt(23); tickParticipantAudio(dt); }   // [Systems M09d] bots' delayed ability notifies
    // [integration 09c] Systems M09e glue: every participant's (bot's) stateful ability loops, change-driven / idempotent - the
    // cloak buff loop (heard by everyone in the original, team-specific) and the hover lift loop / land; a dead or despawned
    // participant's loops stop silently. Match end / unload go through stopAll.
    { TickTimer tt24(24);   // [integration 09c] WFC_TICKPROF glue.partAudio
    for (MatchOpponent* o : opponents_) {
        const int pl = o->matchPlayer();
        if (!o->spawned()) { onParticipantGone(pl); continue; }
        const Character& bp = o->pawn();
        const int tm = matchActive_ && pl >= 0 && (size_t)pl < match_.players().size() ? match_.players()[(size_t)pl].team : 0;
        static const std::string cloakBuff = "TnBuffCloak";
        setParticipantBuffAudio(pl, cloakBuff, bp.cloakRemain_ > 0.0f, tm, bp.actorLocation());
        setParticipantHoverAudio(pl, bp.hoverState_, bp.actorLocation());
        // [integration 09c] Systems M09f glue: the bot's own body audio (foley / transform notifies / vehicle component), 70 m cull.
        const Character::VehicleState& vst = bp.vehicleState();
        const VehicleFormType ft = bp.vehicleParams().form;
        VehicleFormSignals vsig;
        vsig.kind = ft == VehicleFormType::Car ? VehicleFormSignals::Kind::Car : ft == VehicleFormType::Tank ? VehicleFormSignals::Kind::Tank
                  : ft == VehicleFormType::Jet ? VehicleFormSignals::Kind::Jet : VehicleFormSignals::Kind::Truck;
        vsig.vehicle = bp.form() == Form::Vehicle && !bp.isTransforming();
        vsig.onGround = bp.onGround();
        vsig.boostState = vsig.kind == VehicleFormSignals::Kind::Tank ? vst.tankBoost : vsig.kind == VehicleFormSignals::Kind::Jet ? vst.flying : vst.driving;
        vsig.velocity = bp.velocity();
        vsig.forward = core::forwardFromYawPitch(bp.yaw(), 0.0f);
        const float fwdSpeed = core::dot(core::Vec3{vsig.velocity.x, 0.0f, vsig.velocity.z}, vsig.forward);
        vsig.stickForward = fwdSpeed > 0.5f ? 1.0f : (fwdSpeed < -0.5f ? -1.0f : 0.0f);   // fallback: throttle from motion
        if (const MoveIntent* mi = participantIntent(pl)) vsig.stickForward = mi->moveForward;   // [Systems M09g] the bot's own throttle
        vsig.dashing = vst.dashRemain > 0.0f;
        vsig.rolling = vst.rollRemain > 0.0f;
        vsig.nitroSerial = vst.nitroSerial;                      // [Systems M09k]
        vsig.wheelSlip = std::fabs(vst.slipAngle);               // [Systems M09k]
        tickParticipantBodyAudio(pl, bp.chassis().id, bp, vsig, true, dt);
        {   // [integration 09c] Systems M09j glue: the bot's weapon-mesh notifies (reload mechanics) + action layer (melee / throw)
            const Weapon& bw = bp.weapon();
            static std::unordered_map<const WeaponDef*, std::string> wclsCache;   // no per-bot per-step string building
            static const std::string noClass;
            const std::string* wcp = &noClass;
            if (bp.hasWeapon() && bw.def) {
                auto wit = wclsCache.find(bw.def);
                if (wit == wclsCache.end()) wit = wclsCache.emplace(bw.def, "TransContent.TnWeapon" + std::string(bw.def->id)).first;
                wcp = &wit->second;
            }
            const std::string& wcls = *wcp;
            const assets::SkinnedModel* am = bp.currentModel();
            const int ai = bp.actionClipIndex();
            const std::string& aclip = bp.form() == Form::Robot && am && ai >= 0 && (size_t)ai < am->clips.size() ? am->clips[(size_t)ai].name : noClass;
            tickParticipantWeaponAudio(pl, wcls, bw.shotSerial, bw.reloadSerial, aclip, bp.actionTime(), dt);
        }
    }
    }
    prefixMark(6);
    if (!localPlayerDead()) player_.pawn().health().tickRegen(dt, player_.pawn().regenBuffRemain_ > 0.0f ? 2.0f : 1.0f);
    for (MatchOpponent* o : opponents_) if (o->spawned()) o->health().tickRegen(dt, o->pawn().regenBuffRemain_ > 0.0f ? 2.0f : 1.0f);
    prefixMark(7);
    if (matchActive_) { TickTimer tt(2); tickMatch(dt); }
    prefixMark(8);
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
    prefixMark(9);
    if (!localPlayerDead()) {                       // dead / not yet spawned (match): no pawn simulation
        sysprof::Scope sp(sysprof::Ctrl);
        const std::vector<int> walkOut = barrierIgnoreFor(localPlayer_);   // a barrier that spawned around the player: walk out
        CollisionWorld::IgnoreDynamicScope walk(walkOut.empty() ? nullptr : &walkOut);
        if (!onlineSeats_) {   // online: the local seat moves with every other seat (the participants' pool), in match player order
            forcedDirPre(player_.pawn(), dt, true);       // TnForcedDirVolume touches / pushes, then HardBlock (forcedDirPost)
            player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn
            forcedDirPost(player_.pawn());
        }
    }
    prefixMark(10);
    if (asyncStepEnabled()) preloadHeldWeaponsOfPawns();
    if (matchActive_) ensureAbilityModels();
    if (matchActive_) debugForceStreak();
    prefixMark(11);
}

// WFC_FORCESTREAK=<Id>@<match s>[:<player>] (diagnostic): grants and triggers that killstreak once, for the local player by default
// (e.g. PokeStreak@20 for a first-use hitch check under WFC_SLOWFRAME). Main thread, the step's local part.
void World::debugForceStreak() {
    static const char* env = std::getenv("WFC_FORCESTREAK");
    static bool done = false;
    if (!env || done || match_.state() != Match::State::InProgress) return;
    const std::string spec = env;
    const size_t at = spec.find('@'), colon = spec.find(':');
    const std::string id = spec.substr(0, at);
    const float when = at == std::string::npos ? 0.0f : (float)std::atof(spec.c_str() + at + 1);
    const int who = colon == std::string::npos ? localPlayer_ : std::atoi(spec.c_str() + colon + 1);
    if (match_.matchTime() < when || who < 0 || (size_t)who >= match_.players().size() || !participantPawn(who)) return;
    done = true;
    match_.playerMutable(who).acquiredKillstreaks.push_back(id);
    const std::string fired = who == localPlayer_ ? triggerLocalKillstreak() : triggerKillstreakFor(who);
    LOG_INFO("FORCESTREAK %s for p%d at match t %.1f s: %s", id.c_str(), who, match_.matchTime(), fired.empty() ? "deferred (transforming to robot)" : fired.c_str());
}

// The background part of a step (after the local controller): bots, participants, weapons, FX, audio glue, projectiles, actors.
void World::stepRemainder(float dt) {
    TickTimer tickTotal(14);
    if (anySmart_) smartCollectNoises();   // Smart hearing: the previous step's shots / blasts (before the list is cleared)
    participantShots_.clear();
    { TickTimer tt(1); tickAbilityActors(dt); }
    tickRemoteSeats(dt);                                                 // the Remote seats' weapon / ability ticks
    { TickTimer tt(3); tickBots(dt); }                                  // bot participants: decisions -> intents, weapons
    {   // Participants: movement + animation (every participant animates every step; user decision). Each task touches only its own
        // pawn and reads the static collision, so they run on the worker pool in any order with the same result (deterministic).
        TickTimer tt(4);
        const CollisionWorld* col = collision();
        // WFC_TICKPROF slots 4 / 5 while profiling: oppMove = the pool's wall time; oppAnim = animation CPU time summed over the workers.
        static std::vector<double> animMs;
        const bool prof = TickProf::on();
        if (prof) animMs.assign(opponents_.size(), 0.0);
        static std::vector<std::vector<int>> walkOut;   // per participant: barrier sets it walks out of (built serially, read by its task)
        const bool localTask = onlineSeats_ && !localDead_;   // online: the local seat moves here, like every other seat
        walkOut.assign(opponents_.size() + (localTask ? 1 : 0), {});
        bool anyWalkOut = false;
        for (const BarrierState& br : barriers_) anyWalkOut |= br.alive && !br.passThrough.empty();
        if (anyWalkOut) for (size_t i = 0; i < opponents_.size(); ++i) walkOut[i] = barrierIgnoreFor(opponents_[i]->matchPlayer());
        if (anyWalkOut && localTask) walkOut[opponents_.size()] = barrierIgnoreFor(localPlayer_);
        core::WorkerPool::get().run((int)walkOut.size(), [&](int i) {
            if ((size_t)i == opponents_.size()) {   // the online local seat: the decoded command's intent
                Character& lp = player_.pawn();
                CollisionWorld::IgnoreDynamicScope walk(walkOut[(size_t)i].empty() ? nullptr : &walkOut[(size_t)i]);
                forcedDirPre(lp, dt, true);
                CharacterMovement::update(lp, localSeatIntent_, dt, col);
                forcedDirPost(lp);
                lp.updateAnimation(dt);
                return;
            }
            MatchOpponent* o = opponents_[(size_t)i];
            CollisionWorld::IgnoreDynamicScope walk(walkOut[(size_t)i].empty() ? nullptr : &walkOut[(size_t)i]);
            forcedDirPre(o->pawn(), dt, seatHuman(o->matchPlayer()));   // a human seat is a player (onlyPlayers volumes)
            o->simulateMovement(dt, col);
            forcedDirPost(o->pawn());
            const double a0 = prof ? profNowMs() : 0.0;
            o->simulateAnimation(dt);
            if (prof) animMs[(size_t)i] = profNowMs() - a0;
        });
        if (prof) { double sum = 0.0; for (double m : animMs) sum += m; tickProfAdd(5, sum); }
        if (localTask) {   // the edges are consumed; the presentation-yaw reference for the frames until the next step
            localSeatIntent_.wantJump = false; localSeatIntent_.wantDash = false; localSeatIntent_.dodgeDir = 0; localSeatIntent_.hoverRequest = false;
            player_.controller().presentationStep(localSeatIntent_.faceYaw, dt);
        }
    }
    { TickTimer tt(6); tickParticipantWeapons(dt); }
    for (auto& kv : partBeams_) kv.second.time = std::max(0.0f, kv.second.time - dt);
    player_.controller().tickCameraCollision(dt);   // obstruction behaviour after the pawn moved
    gameplayRamContacts();
    { TickTimer tt(12); separatePawns(); }
    {   // Driving slip angle per pawn (Systems' tire audio for every vehicle): heading vs ground velocity while Driving on the ground.
        auto slip = [](Character& c) {
            auto& vs = c.vehicleState();
            vs.slipAngle = 0.0f;
            if (c.moveForm() != Form::Vehicle || !vs.driving || !vs.onTheGround) return;
            const core::Vec3& v = c.velocity();
            if (v.x * v.x + v.z * v.z < 1.0f) return;
            const core::Vec3 f = core::forwardFromYawPitch(c.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            vs.slipAngle = std::atan2(core::dot(v, r), std::fabs(core::dot(v, f)));
        };
        if (!localDead_) slip(player_.pawn());
        for (MatchOpponent* o : opponents_) if (o->spawned()) slip(o->pawn());
    }
    static const char* aimPitchEnv = std::getenv("WFC_AIMPITCH");
    if (const char* ap = aimPitchEnv)     // diagnostic: force the aim pitch (rad)
        player_.pawn().setAimPitch((float)std::atof(ap));
    if (!onlineSeats_) { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }   // online: in the pool while spawned (as every seat)
    drainAnimNotifies();   // [Systems] authored body notifies: every participant's anim windows of this step
    tickBodyNotifyFx(dt);   // [Systems] attached body effects follow this step's poses
    { sysprof::Scope sp(sysprof::WpnPres); tickWeaponPresentation(dt); }
    { sysprof::Scope sp(sysprof::Vehicle); tickVehicleBoost(dt); }
    static const bool animLog = std::getenv("WFC_ANIMLOG") != nullptr;
    if (animLog) {                       // layering diagnostics
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
        tickTransientLights(dt);
    }
    // Event-driven audio via edge detection on pawn state.
    {   TickTimer tt26(26);   // [integration 09c] WFC_TICKPROF glue.localAudio (character audio, cues, level audio)
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
            LOG_INFO("AMB inaudibleSkipped=%d zone=%s emitters=%d/%d oneShots=%d cues=%zu occluded=%d rays/s=%.0f pending=%zu voices=%d (max %d, dropped %d, stolen %d, virtual %d) wet=%d peak=%.1fdB gr=%.1fdB mix=%.3fms/block live=%zu backendVoices=%d pcm=%.1fMB map=%s script=%d pools=%d music=%d/%d tl=%.1f",
                     cues_.inaudibleSkipped(), levelAudio_.ambient().zoneName(), levelAudio_.ambient().activeEmitters(), levelAudio_.ambient().emitterCount(), levelAudio_.ambient().oneShotsPlayed(),
                     cues_.liveInstances(), cues_.occludedInstances(), occlusionRays_ / 0.5f, cues_.pendingEvents(), have ? ms.voices : -1,
                     have ? ms.peakVoices : -1, have ? ms.droppedVoices : -1, have ? ms.stolenVoices : -1, have ? ms.virtualVoices : -1, have ? ms.wetVoices : -1,
                     have ? ms.peakDb : -96.0f, have ? ms.gainReductionDb : 0.0f, have ? ms.mixMsPerBlock : 0.0f,
                     cues_.liveInstances(), audio_ ? audio_->activeVoices() : -1, audio_ ? audio_->residentBytes() / 1048576.0 : 0.0,
                     levelAudio_.level().c_str(), levelAudio_.ambient().script().liveSounds(), levelAudio_.state().poolsPlaying,
                     levelAudio_.state().musicState, levelAudio_.state().musicInstance, levelAudio_.state().timelinePos);
            if (cues_.pendingEvents() > 30) LOG_INFO("AMB pending: %s", cues_.pendingSummary().c_str());
            occlusionRays_ = 0;
        }
        dialogEvents_.tick(dt, cues_);   // [Systems] the dialog groups' retrigger timers
        cues_.tick(dt);
    }

    { TickTimer th(kTsHazards); tickHazards(dt); }
    { TickTimer tt(9); tickProjectiles(dt); }
    if (player_.pawn().position().y < killZ_ && !localPlayerDead()) {
        // Below KillZ: FellOutOfWorld -> Died with no killer (an environmental death in a match).
        LOG_INFO("World: player fell out of world; %s", matchActive_ ? "killed (KillZ)" : "respawning");
        if (matchActive_) killLocalPlayer(-1, false, "Engine.DmgType_Fell");   // WorldInfo.KillZDamageType [HIGH: stock default]
        else respawnPlayer();
    }
    if (matchActive_ && !cp_.active && !localDead_ && botNav_.valid() && match_.state() == Match::State::InProgress) {
        Character& lp = player_.pawn();
        static const bool strandTest = std::getenv("WFC_STRANDTEST") != nullptr;   // TEST ONLY
        if (strandTest && !strandTestDone_ && match_.matchTime() > 3.0f) {
            strandTestDone_ = true;
            core::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (const BotNav::Cell& c : botNav_.cells()) { lo = {std::min(lo.x, c.bmin.x), std::min(lo.y, c.bmin.y), std::min(lo.z, c.bmin.z)}; hi = {std::max(hi.x, c.bmax.x), std::max(hi.y, c.bmax.y), std::max(hi.z, c.bmax.z)}; }
            bool placed = false;
            for (float x = lo.x; x <= hi.x && !placed; x += 4.0f)
                for (float z = lo.z; z <= hi.z && !placed; z += 4.0f) {
                    float gy; core::Vec3 n;
                    if (!collision_.groundHeight(x, z, hi.y + 60.0f, 0.0f, gy, n) || n.y < 0.8f) continue;
                    if (botNav_.findCell(core::Vec3{x, gy, z}, 25.0f, 34.0f) >= 0) continue;
                    lp.setPosition(core::Vec3{x, gy + 0.05f, z}); lp.velocity() = {0, 0, 0}; lp.groundY = gy; placed = true;
                    LOG_INFO("STRANDTEST: the local pawn placed at (%.1f %.1f %.1f), no nav within 25 m / 34 m below", x, gy, z);
                }
            if (!placed) LOG_INFO("STRANDTEST: no stranded surface found");
        }
        // the bots' stranded rule for the human (playtest 4): no nav floor in reach for 30 s in robot form -> KillZ's handling
        const bool stranded = lp.moveForm() == Form::Robot && !lp.isTransforming() && botNav_.findCell(lp.position(), 25.0f, 34.0f) < 0;
        localStrandRun_ = stranded ? localStrandRun_ + dt : 0.0f;
        if (localStrandRun_ >= 30.0f) {
            LOG_INFO("World: local player stranded off the nav for 30 s at (%.1f %.1f %.1f): out-of-world handling (KillZ)", lp.position().x, lp.position().y, lp.position().z);
            localStrandRun_ = 0.0f;
            killLocalPlayer(-1, false, "Engine.DmgType_Fell");
        }
    }
    { TickTimer tt(10); for (auto& a : actors_) if (a->alive()) a->tick(*this, dt); }
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
    {   // [integration 09b] Systems M09b glue: this step's participant (bot) shots -> one fire sound per player, an impact sound
        // per shot that hit (hitPlayer -1 = world / destructible; else the victim's hit effect).
        std::set<int> fired;
        for (const ParticipantShot& s : participantShots_) {
            if (s.weapon.empty()) continue;
            const std::string cls = "TransContent.TnWeapon" + s.weapon;
            if (fired.insert(s.player).second) onParticipantFired(cls, s.from);
            if (s.impact) onParticipantImpact(cls, s.to, s.hitPlayer, s.physmat);
        }
    }
}

// Local match host glue (TnMultiplayerGame / TnTeamGame on the authority): the Match decides spawns, deaths and the
// end; World applies them to the local pawn and the map actors.
void World::startLocalMatch(const MatchSettings& s, int localTeam) {
    joinStep();
    if (campaignMap_ && s.campaign) cpSetStarts(cpLaunchCheckpoint_);   // the active checkpoint / the chapter start (+ squad aux spots)
    else if (match_.starts().empty()) {
        std::string root = assetRoot();
        match_.loadSpawnData(mapDir() + "gameplay.json");
    }
    match_.setChassisCheck([this](const std::string& id, std::string& err) {
        const ChassisAssets* a = chassisAssets(id);
        if (a && a->ok) return true;
        err = a ? a->error : std::string("unknown chassis");
        return false;
    });
    // Smart squads (TDM): a respawning member takes the team start nearest its squad's first live member (M2: settled squad distance
    // was respawn-driven, 38-52 m). WFC_SMARTOFF=spawnnear turns it off; Classic participants never get an anchor.
    match_.setSquadAnchor([this](int p, core::Vec3& anchor) {
        static const bool spawnOff = std::getenv("WFC_SMARTOFF") && std::strstr(std::getenv("WFC_SMARTOFF"), "spawnnear");
        if (spawnOff || !anySmart_ || !botIsSmart(p) || matchMode_ != MatchMode::TDM || smartTune().squadSize <= 1.0f) return false;
        const int squad = smartSquadId(p);
        for (size_t q = 0; q < smart_.size(); ++q) {
            if (!smart_[q].active || !match_.sameTeam((int)q, p) || (int)q == p || smartSquadId((int)q) != squad) continue;
            if (const Character* c = participantPawn((int)q)) { anchor = c->position(); return true; }
        }
        return false;
    });
    match_.setSquadAnchorScope((int)smartTune().spawnNearScope);
    // Scope 3: a start of another cluster is "near the squad" when its nav path to the leader is short (straight line first: a path is
    // never shorter). One synchronous search per candidate, at respawn only.
    match_.setSquadStartNear([this](int st, const core::Vec3& anchor) {
        const float lim = smartTune().spawnNearPathM;
        const core::Vec3 sp = match_.starts()[(size_t)st].pos;
        if (!botNav_.valid() || core::length(sp - anchor) > lim) return false;
        BotNav::Agent ag; ag.radius = 1.75f;
        // A straight walkable line on about the same floor is a path within the limit (no search buffers used).
        if (std::fabs(sp.y - anchor.y) <= 4.0f && botNav_.directWalkable(sp, anchor, ag)) return true;
        // Else a full search - unless a bot's time-sliced search is in flight (shared buffers; sim state, so still deterministic).
        if (botNav_.searchActive()) return false;
        std::vector<BotNav::Waypoint> path;
        if (!botNav_.findPath(sp, anchor, ag, path) || path.empty()) return false;
        float len = 0.0f; core::Vec3 at = sp;
        for (const BotNav::Waypoint& w : path) { len += core::length(w.pos - at); at = w.pos; if (len > lim) return false; }
        return core::length(path.back().pos - anchor) < 6.0f;   // the corridor reaches the leader (not the nearest reachable cell far off)
    });
    match_.begin(s);
    match_.setSnapshotProvider([this](int p) { return participantSnapshot(p); });
    if (localPlayer_ < 0) localPlayer_ = match_.addPlayer("Player");
    match_.playerMutable(localPlayer_).kind = ParticipantKind::Local;
    if (s.teamGame && (localTeam == 0 || localTeam == 1)) {
        match_.playerMutable(localPlayer_).team = localTeam;   // [integration M08b] the lobby's team (before the login start)
        LOG_INFO("match: local player team %d from the lobby", localTeam);
    }
    matchActive_ = true;
    localDead_ = true;            // PendingMatch: TrySpawnPlayer false -> nobody spawns before the start
    // GameInfo.Login: the controller is created at FindPlayerStart (team start of the initial cluster) and spectates
    // from there until the match spawns its pawn [HIGH]. The (not yet existing) pawn is parked at rest at that start.
    int ls = seatOrdered_ ? match_.loginStartPeek(localPlayer_) : match_.loginStart(localPlayer_);   // online: no round-robin advance
    if (ls >= 0) {
        const Match::Start& st = match_.starts()[(size_t)ls];
        Character& pc = player_.pawn();
        pc.respawnReset();
        core::Vec3 p = st.pos; float gy; core::Vec3 gn;
        if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
        pc.setPosition(p); pc.setYaw(st.yaw); pc.groundY = p.y;
        player_.controller().setCameraYaw(st.yaw);
        player_.controller().setSpectatorView(st.pos + core::Vec3{0, core::config::kPawnHalfHeight, 0}, st.yaw);
        if (renderer_) renderer_->notifyCameraCut("spectate");
    }
    // Under the match load: cache (and prewarm) the four default MP bodies of the local player's faction, so a class pick in
    // the lobby (after this, on a visible frame) is instant. Other participants' bodies (bots, the other faction, custom
    // chassis) cache as soon as their selection exists (World::tick). Keep this after the local team is final.
    // Load scheduling only, not original behaviour.
    if (localPlayer_ >= 0) {
        const int fa = match_.faction(localPlayer_);
        for (int sp = 0; sp < 4; ++sp) {
            const ChassisAssets* pca = chassisAssets(defaultChassis((Specialty)sp, fa));
            preloadHeldWeaponModels(classPresetWeapons(specialtyName((Specialty)sp)));   // its preset weapons too
            if (pca) queueSelectionAudio(defaultChassis((Specialty)sp, fa), classPresetWeapons(specialtyName((Specialty)sp)),
                                           pca->def.iconicVehicleWeapons);              // [integration 09a] + their audio
        }
    }
    // The killstreak reward weapons (PokeStreak -> Poke, SpawnRocketTurretStreak -> HeavyRocketTurret: the only weapons granted
    // mid-match) and the ability actors' meshes, so their first use does not load + upload + first-draw them in one frame
    // (Experimental 4K 64p: a 97 ms frame at the first PokeStreak). weaponModelFor prewarms each. Load scheduling only.
    for (const char* id : {"Poke", "HeavyRocketTurret"})
        if (const WeaponDef* d = findWeaponDef(id)) weaponModelFor(*d);
    ensureAbilityModels();
    ensureGibModels();   // the first gib (a Shockwave mass kill) loaded them in one step
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
    // TnMultiplayerGame.InitGame: Rounds = GetIntOption(Options, "Rounds") - the Frontend's BuildURL sends it for CTF [CONF RE rules check].
    if (out.settings.rounds > 0 && opt.count("Rounds")) { out.settings.rounds = std::max(2, std::atoi(opt["Rounds"].c_str())); out.settings.goalScore = 1000000; }
    if (opt.count("TimeLimit")) out.settings.timeLimit = std::max(0, (int)std::atof(opt["TimeLimit"].c_str()));
    // Private Match Bot Settings (Frontend contract; PC ADAPTATION). Clamped again against the slots when the bots are added.
    if (opt.count("WfcCanGainXp")) out.settings.canGainXp = std::atoi(opt["WfcCanGainXp"].c_str()) != 0;   // GRI.CanGainXp (MVP gate)
    if (opt.count("WfcIdleKick")) out.settings.kickIdle = std::atoi(opt["WfcIdleKick"].c_str()) != 0;   // host option "Kick Idle Players"
    if (opt.count("IconicMode")) out.settings.iconicMode = std::atoi(opt["IconicMode"].c_str()) != 0;   // GRI _bIconicMode (Frontend URL)
    if (opt.count("Checkpoint")) out.checkpoint = opt["Checkpoint"];   // campaign (C6): a menu checkpoint
    if (opt.count("WfcDifficulty")) out.difficulty = std::clamp(std::atoi(opt["WfcDifficulty"].c_str()), 0, 2);
    if (opt.count("MapId")) out.mapId = std::atoi(opt["MapId"].c_str());
    if (opt.count("Character")) out.character = opt["Character"];
    if (opt.count("BotsFriendly")) out.bots.friendly = std::max(0, std::atoi(opt["BotsFriendly"].c_str()));
    if (opt.count("BotsEnemy")) out.bots.enemy = std::max(0, std::atoi(opt["BotsEnemy"].c_str()));
    if (opt.count("BotDifficulty")) out.bots.difficulty = std::clamp(std::atoi(opt["BotDifficulty"].c_str()), 0, kMaxBotDifficulty);
    // CUSTOM-GAME EXTENSION: 16 bots per team (+ the human); off = the original 10-player slots.
    if (opt.count("BotsAutobot")) out.bots.autobot = std::max(0, std::atoi(opt["BotsAutobot"].c_str()));
    if (opt.count("BotsDecepticon")) out.bots.decepticon = std::max(0, std::atoi(opt["BotsDecepticon"].c_str()));
    if (opt.count("BotDifficultyAutobot")) out.bots.difficultyAutobot = std::clamp(std::atoi(opt["BotDifficultyAutobot"].c_str()), 0, kMaxBotDifficulty);
    if (opt.count("BotDifficultyDecepticon")) out.bots.difficultyDecepticon = std::clamp(std::atoi(opt["BotDifficultyDecepticon"].c_str()), 0, kMaxBotDifficulty);
    if (opt.count("BotAI")) out.bots.ai = (int)botAiFromString(opt["BotAI"].c_str(), BotAi::Classic);
    if (opt.count("BotAIAutobot")) out.bots.aiAutobot = (int)botAiFromString(opt["BotAIAutobot"].c_str(), BotAi::Classic);
    if (opt.count("BotAIDecepticon")) out.bots.aiDecepticon = (int)botAiFromString(opt["BotAIDecepticon"].c_str(), BotAi::Classic);
    for (const char* k : {"ExtendedPlayers", "BotsExtended"})
        if (opt.count(k) && std::atoi(opt[k].c_str()) != 0) { out.bots.extended = true; out.settings.applyExtendedSlots(); }
    return !out.map.empty();
}

void World::resetForNewLevel() {
    joinStep();
    // A new match is a fresh load of the map in the original (MatchOver -> ReturnToGameLobby -> ServerTravel).
    mapState_.resetForNewMatch();
    if (collision_.valid()) mapState_.tick(0.0f, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, svLaunching_);   // a fresh level
    syncTransformingActorSounds();   // the reset's snap (e.g. the versus doors start open) plays nothing
    dyingQueue_.clear();
    for (DyingBody& b : dyingBodies_) { b.remain = 0.0f; if (b.pawnSet >= 0) collision_.setDynamicEnabled(b.pawnSet, false); if (b.weaponSet >= 0 && weaponCollision_.valid()) weaponCollision_.setDynamicEnabled(b.weaponSet, false); }
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


void World::fillPresented(PresentedFrame& p) {
    p.players = match_.players();
    const size_t n = p.players.size();
    p.positions.assign(n, core::Vec3{0, 0, 0});
    p.present.assign(n, 0);
    p.faction.assign(n, 255);
    for (size_t i = 0; i < n; ++i) p.faction[i] = match_.faction((int)i);
    for (size_t i = 0; i < n; ++i)
        if (const Character* c = participantPawn((int)i)) { p.positions[i] = c->position(); p.present[i] = 1; }
    p.hud = hudState();
    p.localPlayer = localPlayer_;
    const Character& pc = player_.pawn();
    p.localHealth = pc.health().current; p.localHealthMax = pc.health().max;
    p.localSegmentTops.clear();
    for (int i = 0; i < pc.health().segmentCount; ++i) p.localSegmentTops.push_back(pc.health().segmentTop(i));
    // Capacities of the SAME weapon hudState() reports the clip / reserve of (the vehicle weapon in vehicle form): the robot
    // weapon's here showed e.g. TankCannon 1 / Ion 50 -> the HUD's low-ammo state + reload prompt (playtest 2026-10-13).
    p.localMag = p.hud.clipMax; p.localReserveMax = p.hud.reserveMax;
    p.localWeaponId = pc.weapon().def ? pc.weapon().def->id : "";
    p.campaignMovie = cp_.movie; p.campaignMovieSerial = cp_.movieSerial; p.campaignMovieStop = cp_.movieStop;
    p.localChassis = localPlayer_ >= 0 && (size_t)localPlayer_ < n ? p.players[(size_t)localPlayer_].chassis : std::string();
    p.aim = player_.controller().hudAimState();
    p.matchState = (int)match_.state();
    p.modeTag = match_.settings().modeTag;
    p.teamScore[0] = match_.teamScore(0); p.teamScore[1] = match_.teamScore(1);
    p.elapsedTime = match_.elapsedTime(); p.remainingTime = match_.remainingTime();
    p.extendedLobby = extendedLobby();
    // queues (capped so a build without a consumer cannot grow without bound)
    p.matchEvents.insert(p.matchEvents.end(), matchEvents_.begin(), matchEvents_.end());
    const auto& ge = match_.gameplayEvents();
    if (presentedGameplayEventCount_ > ge.size()) presentedGameplayEventCount_ = 0;   // a new match restarted the record
    p.gameplayEvents.insert(p.gameplayEvents.end(), ge.begin() + (long)presentedGameplayEventCount_, ge.end());
    presentedGameplayEventCount_ = ge.size();
    const auto& kh = match_.killHistory();
    if (presentedKillCount_ > kh.size()) presentedKillCount_ = 0;   // a new match
    p.kills.insert(p.kills.end(), kh.begin() + (long)presentedKillCount_, kh.end());
    presentedKillCount_ = kh.size();
    if (p.kills.size() > 8192) p.kills.erase(p.kills.begin(), p.kills.end() - 8192L);
    // HUD observers
    {
        PresentedFrame::HudObservers& o = p.hud2;
        const HudGameState& h = p.hud;
        o = PresentedFrame::HudObservers{};
        o.attackingTeam = h.attackingTeamIndex >= 0 ? h.attackingTeamIndex : (h.attackingTeam <= 1 ? h.attackingTeam : -1);
        o.killstreakId = h.killstreaks.empty() ? std::string() : h.killstreaks.back();
        o.killstreakAvailable = !h.killstreaks.empty() && h.killstreakImplemented;
        for (const Character::AbilitySlot& a : pc.abilities_) o.abilities.push_back({a.id, a.cooldown, a.cooldownTime, a.pendingCooldown, a.implemented});
        o.grenade.ammo = h.grenades;
        if (const Weapon* gb = grenadeBag(pc)) o.grenade.type = gb->def ? gb->def->id : "";
        for (const Projectile& pr : projectiles_) o.grenade.activeCount += pr.instigator == localPlayer_ && pr.grenade;
        o.lockOn.target = h.lockTarget; o.lockOn.progress = h.lockProgress;
        o.lockOn.state = h.locked ? 2 : (h.lockTarget >= 0 ? 1 : 0);
        if (const Character* lt = h.lockTarget >= 0 ? participantPawn(h.lockTarget) : nullptr) {
            o.lockOn.targetPos = lt->actorLocation();
            o.lockOn.distance = core::length(lt->actorLocation() - pc.actorLocation());
        } else if (o.lockOn.state != 0) { o.lockOn.state = 0; o.lockOn.target = -1; }   // the target is gone
        o.weapon.jammed = pc.jammedRemain_ > 0.0f; o.weapon.spread = pc.effectiveSpread(); o.weapon.message = h.weaponChargeMessage;
        auto buff = [&](const char* id, float t) { if (t > 0.0f) o.buffs.push_back({id, t}); };
        buff("TnBuffCloak", pc.cloakRemain_); buff("TnBuffWarcry", pc.warcryRemain_); buff("TnBuffSeeEnemyObjectiveMarkers", h.seeEnemies);
        buff("TnBuffRefillHealthOnKill", h.refillOnKill); buff("TnBuffAbilityJammer", h.abilitiesJammed); buff("TnBuffHardLocked", h.hardLocked);
        buff("TnBuffHealthRegenKillStreak", h.regenBuff); buff("TnBuffFastAbilityCooldown", h.fastCooldownBuff); buff("TnBuffAmmoLock", h.ammoLockBuff);
        buff("TnBuffAmmoBeaconIncreaseDamage", h.ammoBeaconBuff ? 1.0f : 0.0f); buff("TnBuffRollerSphere", h.rollerSlow); buff("TnBuffDrain", h.drain);
        if (!h.pickupPrompt.empty()) o.contextual.push_back(h.pickupPrompt);
        o.cantTransformCount = h.cantTransformCount;
        // Target [CONF RE e5cb5fd]: the pawn under the crosshair (nearest on the camera's aim ray within 300 m, not behind world
        // geometry; sentries are pawns too), else the repair target. Friend if on the same team, else Enemy; None without a target.
        if (!localDead_ && matchActive_) {
            const core::Vec3 eye = player_.controller().cameraPos();
            const core::Vec3 dir = core::forwardFromYawPitch(player_.controller().camYaw(), player_.controller().camPitch());
            float wall = 300.0f, tw;
            const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
            if (line && line->segmentHit(eye, eye + dir * 300.0f, tw)) wall = 300.0f * tw;
            const int myTeam = localPlayer_ >= 0 && (size_t)localPlayer_ < n ? p.players[(size_t)localPlayer_].team : 255;
            const bool teamGame = match_.settings().teamGame;
            auto friendOf = [&](int team) { return teamGame && team == myTeam; };
            float best = wall;
            bool direct = false;
            for (MatchOpponent* op : opponents_) {
                float th;
                if (op->rayHit(eye, dir, best, th) && th < best) {
                    best = th; direct = true;
                    const auto& mp = match_.players()[(size_t)op->matchPlayer()];
                    o.target = {};
                    o.target.player = op->matchPlayer(); o.target.team = mp.team; o.target.name = mp.name;
                    o.target.health = op->health().max > 0.0f ? op->health().current / op->health().max : 0.0f;
                }
            }
            { float ts; if (sentryRayHit(eye, dir, best, ts) && ts < best && lastSentryHit_ >= 0 && (size_t)lastSentryHit_ < sentries_.size()) {
                const Sentry& se = sentries_[(size_t)lastSentryHit_];
                best = ts; direct = true;
                o.target = {}; o.target.sentry = true; o.target.team = se.owner >= 0 && (size_t)se.owner < n ? p.players[(size_t)se.owner].team : 255;
                o.target.health = std::max(0.0f, se.health / 135.0f);   // no PRI: no name
            } }
            if (!direct && h.repairBeam && h.repairBeamTarget >= 0 && (size_t)h.repairBeamTarget < n) {   // the repair target
                o.target = {}; o.target.player = h.repairBeamTarget; o.target.team = p.players[(size_t)h.repairBeamTarget].team;
                if (const Character* rc = participantPawn(h.repairBeamTarget)) o.target.health = rc->health().max > 0.0f ? rc->health().current / rc->health().max : 0.0f;
            }
            if (o.target.player >= 0 || o.target.sentry) {
                o.target.type = friendOf(o.target.team) ? 0 : 1;
                o.target.healthType = o.target.type;
                if (!direct) o.target.name.clear();   // the name only on a direct crosshair hit
            }
        }
        // Progress bar (TnHudDataObserver rules): DOM capture only on a node the local team does not defend, while the local pawn is
        // inside it and it is being captured; bomb defuse for the defenders on the planted point; single-flag return for a defender
        // touching the dropped flag.
        if (!localDead_ && matchActive_ && localPlayer_ >= 0) {
            const core::Vec3 lp = pc.actorLocation();
            const int myTeam = p.players[(size_t)localPlayer_].team;
            for (const ObjectiveObject& ob : mapState_.objectives()) {
                if (!ob.activeInMode) continue;
                if (ob.cls == "TnDominationPoint" && ob.defenderTeam != myTeam && ob.captureTime > 0.0f && ob.contains(lp)) {
                    o.progress = {"DominationCapture", std::min(1.0f, ob.captureTime / 20.0f), ob.actor};
                }
            }
            const auto& pl = mapState_.planted();
            if (pl.active && pl.team != myTeam && pl.point >= 0 && (size_t)pl.point < mapState_.objectives().size() &&
                mapState_.objectives()[(size_t)pl.point].contains(lp) && pl.defuse > 0.0f)
                o.progress = {"BombDefuseTimer", std::min(1.0f, pl.defuse / 5.0f), mapState_.objectives()[(size_t)pl.point].actor};
            if (match_.settings().singleFlagCTF && myTeam != match_.attackingTeam())
                for (const MapState::Carried& c : mapState_.carried())
                    if (c.kind == 0 && c.dropped && c.returnLeft < 10.0f && core::length(c.pos - lp) < 3.0f)
                        o.progress = {"SingleFlagCTF", std::min(1.0f, 1.0f - c.returnLeft / 10.0f), ""};
        }
    }
    // Markers for the local viewer [CONF RE 7bb8ec1 notes/MILESTONE_E_OBJECTIVE_MARKERS.md §4 visibility, §5 setup]. Pawn tags: the
    // SetupEnemyMarker / cloak rules (hudState), the enemy label also hidden out of line of sight. Constant screen size, no distance
    // fade; the 8 % safe-frame clamp / arrows belong to the drawer.
    const double markersT0 = tickProfOn() ? profNowMs() : 0.0;   // WFC_TICKLOG fill.markers
    p.markers.clear();
    if (matchActive_ && localPlayer_ >= 0) {
        const int myTeam = p.players[(size_t)localPlayer_].team;
        const int attacking = match_.attackingTeam();                         // GRI.AttackingTeam (CTF / EXT)
        const bool scientist = p.players[(size_t)localPlayer_].specialty == "Scientist";
        const core::Vec3 eye = player_.controller().cameraPos();
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        auto param = [](PresentedFrame::Marker& m, const char* n, float v) { m.params.push_back({n, std::array<float, 4>{v, v, v, v}}); };
        // TransformerVersus pawn tags
        for (const HudGameState::Tag& t : p.hud.tags) {
            if (!t.drawn) continue;
            PresentedFrame::Marker m;
            m.key = "pawn:" + std::to_string(t.player); m.type = "TnObjectiveMarkerTypeTransformerVersus";
            m.setup = t.ally ? "AllyMarkerSetup" : "EnemyMarkerSetup"; m.relation = t.ally ? 0 : 1; m.player = t.player;   // the type's authored setups
            m.owner = 100 + t.player;   // the draw owner World::draw passes to IRenderer::setDrawOwner for participants
            m.base = t.pos;
            if (const Character* c = participantPawn(t.player)) {
                m.base = c->actorLocation();
                m.labelZ = c->cylinderHalfHeight(c->moveForm()) * 0.5f;      // Versus: CollisionHeight x 0.5
                m.health = c->health().max > 0.0f ? c->health().current / c->health().max : 0.0f;   // GetNormalizedHealth
            }
            m.drawHealthBar = t.ally && scientist;
            bool label = t.label;
            float th;
            if (label && !t.ally && line && line->segmentHit(eye, m.base, th)) label = false;   // enemy label: line of sight
            if (label) m.label = t.name;
            p.markers.push_back(m);
        }
        // Objectives
        const auto& pl = mapState_.planted();
        const std::string plantedActor = pl.active && pl.point >= 0 && (size_t)pl.point < mapState_.objectives().size() ? mapState_.objectives()[(size_t)pl.point].actor : std::string();
        const bool carrying = mapState_.carriedBy(localPlayer_) >= 0;
        for (const HudGameState::Objective& ob : p.hud.objectives) {
            if (ob.markerType.empty()) continue;
            PresentedFrame::Marker m;
            m.key = "obj:" + ob.actor; m.base = ob.pos; m.setup = "MarkerSetup";
            m.type = "TnObjectiveMarkerType" + ob.markerType;
            if (ob.markerType == "Domination") {                               // always, every node; by the current owner
                const int id = ob.ownerTeam == myTeam ? 0 : (ob.ownerTeam == 255 || ob.ownerTeam == 254 ? 2 : 1);
                param(m, "TeamID", (float)id);
                m.relation = id == 0 ? 0 : (id == 2 ? 2 : 1);
                m.label = std::to_string(ob.pointNumber);                     // NodeID
                param(m, "NodeID", (float)ob.pointNumber);                   // the marker material's point number
                if (ob.beingCaptured) param(m, "Flashing", 1.0f);
                m.action = id == 0 ? "Defend" : "Capture";
            } else if (ob.markerType == "KingOfTheHill") {                     // only the Active zone
                if (!ob.active) continue;
                const bool own = ob.ownerTeam == myTeam || ob.ownerTeam > 1;   // own or contested / neutral
                param(m, "Neutral", own ? 1.0f : 0.0f);
                m.relation = own ? 0 : 1;
                m.action = ob.ownerTeam == myTeam ? "Defend" : "Capture";
            } else if (ob.markerType == "BombPlantPoint") {
                if (attacking != 0 && attacking != 1) continue;               // no attacking team: hidden
                if (!plantedActor.empty()) { if (ob.actor != plantedActor) continue; }   // after a plant: the planted point, to all
                else if (ob.ownerTeam == attacking) continue;                 // before: only the target (not the attackers' own)
                const bool viewerAttacking = myTeam != ob.ownerTeam;           // vs the point's AUTHORED team
                param(m, "Neutral", viewerAttacking ? 0.0f : 1.0f); param(m, "Flashing", 1.0f);
                m.relation = viewerAttacking ? 1 : 0;
                m.action = viewerAttacking ? (plantedActor.empty() ? "Plant" : "Defend") : (plantedActor.empty() ? "Defend" : "Defuse");
            } else if (ob.markerType == "FlagCapturePoint") {                  // the attackers' capture point, only to the carrier
                if (!ob.active || !carrying) continue;
                param(m, "Neutral", 1.0f); m.relation = 0; m.action = "Capture";
            } else continue;   // factories: their carried object's marker stands for them
            p.markers.push_back(m);
        }
        // Flag / bomb: one marker following the holder (home factory / carrier / dropped pickup; none while the factory sleeps), to
        // everyone but the carrier.
        for (size_t i = 0; i < p.hud.carried.size(); ++i) {
            const HudGameState::CarriedObj& c = p.hud.carried[i];
            if (!c.active || c.holder == localPlayer_) continue;
            if (c.holder < 0 && !c.dropped && c.sleep > 0.0f) continue;
            PresentedFrame::Marker m;
            const bool flag = c.kind == 0;
            m.key = "carried:" + std::to_string(i); m.setup = "MarkerSetup";
            m.type = flag ? "TnObjectiveMarkerTypeFlag" : "TnObjectiveMarkerTypeBomb";
            m.base = c.pos;
            const bool viewerAttacking = myTeam == attacking;
            float neutral = 0.0f;
            if (c.holder >= 0) {
                m.player = c.holder;
                if (const Character* hc = participantPawn(c.holder)) m.base = hc->actorLocation();
                if (c.holderTeam == myTeam) { neutral = 1.0f; m.action = "Escort"; }
                else {
                    neutral = 0.0f; m.action = "Kill";
                    const float t = std::fmod(match_.matchTime(), 2.5f);
                    m.pulseT = t;
                    param(m, "PingOpacity", std::pow((2.5f - t) / 2.5f, 5.0f));
                }
            } else if (c.dropped) {
                neutral = 0.0f;
                if (viewerAttacking) param(m, "Flashing", 1.0f);
                m.action = flag ? (viewerAttacking ? "Capture" : "Return") : "Bomb";
            } else {
                neutral = viewerAttacking ? 0.0f : 1.0f;
                m.action = flag ? (viewerAttacking ? "Capture" : "Defend") : "Bomb";
            }
            param(m, "Neutral", neutral);
            m.relation = neutral == 1.0f ? 0 : 1;                              // label colour friendly iff Neutral == 1
            p.markers.push_back(m);
        }
        // ObjectiveMarkers[16] caps only the replicated objective markers (flag / bomb / nodes / plant points / KOTH / capture point);
        // pawn tags and tombstones are client-local (LocalObjectiveMarkers, unbounded) [CONF RE 806f8cd].
        {
            int objectiveCount = 0;
            std::vector<PresentedFrame::Marker> kept;
            kept.reserve(p.markers.size());
            for (PresentedFrame::Marker& mk : p.markers) {
                const bool local = mk.type == "TnObjectiveMarkerTypeTransformerVersus";
                if (!local && ++objectiveCount > 16) continue;
                kept.push_back(std::move(mk));
            }
            p.markers.swap(kept);
        }
        // Tombstones: every viewer sees every one (ShouldDisplayMarker true) until LifeSpan 8 s ends; the drawer fades the last 3 s.
        for (const auto& tb : match_.tombstones()) {
            PresentedFrame::Marker mk;
            mk.key = "tomb:" + std::to_string(tb.serial); mk.type = "TnObjectiveMarkerTypeTombstone"; mk.setup = "MarkerSetup";
            mk.base = tb.pos; mk.relation = (match_.settings().teamGame && tb.team == myTeam) ? 0 : 1;
            mk.lifeSpan = std::max(0.0f, Match::kTombstoneLifeSpan - (match_.matchTime() - tb.time));
            p.markers.push_back(mk);
        }
    }
    // No generic removal fade (RE: removal is explicit by the owner; only Tombstone fades, through its lifeSpan).
    if (tickProfOn()) tickProfAdd(kTsFillMarkers, profNowMs() - markersT0);
    prevMarkers_ = p.markers;
    p.damageTaken.insert(p.damageTaken.end(), pendingDamageTaken_.begin(), pendingDamageTaken_.end()); pendingDamageTaken_.clear();
    p.damageCaused.insert(p.damageCaused.end(), pendingDamageCaused_.begin(), pendingDamageCaused_.end()); pendingDamageCaused_.clear();
    if (p.damageTaken.size() > 1024) p.damageTaken.erase(p.damageTaken.begin(), p.damageTaken.end() - 1024L);
    if (p.damageCaused.size() > 1024) p.damageCaused.erase(p.damageCaused.begin(), p.damageCaused.end() - 1024L);
    fillSeats(p);   // split screen: the per-seat HUD state
    { auto xp = awards_.drainXp(); p.xpAwards.insert(p.xpAwards.end(), xp.begin(), xp.end()); }
    { auto st = awards_.drainStats(); p.statAwards.insert(p.statAwards.end(), st.begin(), st.end()); }
    constexpr size_t kCap = 8192;
    if (p.matchEvents.size() > kCap) p.matchEvents.erase(p.matchEvents.begin(), p.matchEvents.end() - (long)kCap);
    if (p.gameplayEvents.size() > kCap) p.gameplayEvents.erase(p.gameplayEvents.begin(), p.gameplayEvents.end() - (long)kCap);
    ++p.steps;
}

bool World::launchMatch(const MatchLaunch& l) {
    joinStep();
    if (canonicalMapName(l.map) != mapName_ || !usingSlice_) {
        LOG_WARN("match: map %s is not the loaded map (%s); the world loads one map per session", l.map.c_str(), mapName_.c_str());
        return false;
    }
    MatchMode mode = MatchMode::DM;
    bool known = false;
    for (MatchMode m : {MatchMode::DM, MatchMode::TDM, MatchMode::CTF, MatchMode::KOTH, MatchMode::EXT, MatchMode::DOM})
        if (l.modeTag == gameModeName(m)) { mode = m; known = true; }
    if (l.modeTag == "ESC") { mode = MatchMode::TDM; known = true; }   // Escalation: the TDM map state (no versus objectives) + the director
    if (l.settings.campaign) { mode = MatchMode::TDM; known = true; }   // campaign: the TDM map state (no versus objectives) + TnCampaignGame
    if (!known) { LOG_WARN("match: unknown mode %s", l.modeTag.c_str()); return false; }
    if (l.settings.campaign != campaignMap_) {
        LOG_WARN("match: %s %s - campaign modes run on campaign chapters only", l.map.c_str(), l.modeTag.c_str());
        return false;
    }
    // All six versus modes run on the shared framework (CTF rounds / EXT bomb: Pass 22).
    matchMode_ = mode;
    mapState_.setMode(mode);
    svLaunching_ = l.settings.survival;
    resetForNewLevel();
    clearMatchActors();   // the previous match's projectiles and ability actors go with it (a fresh level in the original)
    removeBots();   // the previous match's bots leave with it (a new match is a fresh level in the original)
    if (seatOrdered_ || !l.humans.empty()) clearAllParticipants();   // online seat order: every peer builds the same numbering from scratch
    seatOrdered_ = !l.humans.empty();
    ++cacheGen_;    // from here on, what this match loads or reuses is stamped; the rest is evicted after the roster loads
    awards_.setXpScale(1.0f);
    {   // simulation RNG per match: WFC_SEED (DEV / TEST) or a fixed value, so the same inputs replay the same match
        const char* sd = std::getenv("WFC_SEED");
        hostSeed_ = l.seed;   // online: the host's seed replaces the local source on every peer
        if (l.online && l.seed) match_.seedRandom(l.seed * 2246822519u + 3u);   // the Match RNG too (it never restarted between matches)
        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.online;   // online (a session launch; never from the seed)
        player_.controller().setWireSeat(onlineSeats_);
        localSeatIntent_ = MoveIntent{}; localSeatPickup_ = false;
        core::simRandSeed(l.seed ? l.seed * 2654435761u + 1u : sd ? (uint32_t)std::strtoul(sd, nullptr, 10) * 2654435761u + 1u : 0x5eed2026u);
    }
    int svTeam = -1;   // Escalation: every player on the map's co-op team (CoopInfo faction)
    if (l.settings.survival) { SurvivalData d; if (SurvivalData::load(canonicalMapName(l.map), d)) svTeam = d.coopTeam; }
    if (l.settings.campaign && campaignMap_) svTeam = cpChapter_.faction;   // campaign co-op: every human on the chapter's faction
    cpLaunchCheckpoint_ = l.checkpoint;
    startLocalMatch(l.settings, svTeam >= 0 ? svTeam : l.settings.campaign ? cpChapter_.faction : l.localTeam);   // [integration M08b] the lobby team
    // Real voices: the original's 96 (FmodAudioDevice MaxChannels) for its <= 10-player matches; the ExtendedPlayers extension
    // (up to 64 participants, ours) gets 128 so distant bots' sounds are not refused as often (63 bots, paced: Systems).
    if (audio_) audio_->setChannelLimit(l.settings.extendedSlots ? 128 : 96);
    if (l.settings.extendedSlots) generateExtraStarts(); else match_.setGeneratedStarts({});
    simTick_ = 0; pendingSwaps_.clear(); remoteIdle_.clear(); remoteIdleKicks_ = 0; remoteQueue_.clear(); remoteGrenades_.clear();
    if (!l.humans.empty()) {
        // Online: every human in the host's seat order on every peer. The local player was added first (startLocalMatch); the others
        // join in seat order with their exact teams, then the participants are permuted so match player i = seat i.
        const int ls = std::clamp(l.localSeat, 0, (int)l.humans.size() - 1);
        {
            MatchPlayer& me = match_.playerMutable(localPlayer_);
            me.name = l.humans[(size_t)ls].name; me.sessionMember = l.humans[(size_t)ls].member;
            if (l.humans[(size_t)ls].selected) match_.selectCharacter(localPlayer_, l.humans[(size_t)ls].selection);   // as the remote seats
            if (match_.settings().teamGame) me.team = svTeam >= 0 ? svTeam : l.humans[(size_t)ls].team;
            if (l.humans[(size_t)ls].dormant) { me.dormant = true; me.team = 255; }   // the joiner: spectates until its seat wakes
        }
        std::vector<int> order(l.humans.size(), -1);
        order[(size_t)ls] = localPlayer_;
        for (size_t i = 0; i < l.humans.size(); ++i)
            if ((int)i != ls) { RemoteSeat h = l.humans[i]; if (svTeam >= 0 && !h.dormant) h.team = svTeam; order[i] = addRemoteParticipant(h, true); }
        match_.reorderPlayers(order);
        std::vector<int> newOf(order.size());
        for (size_t i = 0; i < order.size(); ++i) newOf[(size_t)order[i]] = (int)i;
        localPlayer_ = newOf[(size_t)localPlayer_];
        for (MatchOpponent* o : opponents_) o->setMatchPlayer(newOf[(size_t)o->matchPlayer()]);
        rebuildOppIndex();
        remoteIdle_.assign(match_.players().size(), 0.0f);
        launchHumans_ = (int)l.humans.size();
        LOG_INFO("session: %zu humans in the host's seat order, this machine at seat %d", l.humans.size(), ls);
    } else
        for (const RemoteSeat& r : l.remotes) { RemoteSeat h = r; if (svTeam >= 0) h.team = svTeam; addRemoteParticipant(h, svTeam >= 0); }   // online: the other peers' humans (before the bot fill counts them)
    const int nb = addBots(l.bots);
    setupSurvival();   // Escalation: the horde slots (no-op otherwise)
    setupCampaign(l);   // campaign: the human's pick, the AI squad, the level Kismet (no-op otherwise)
    ensureHullMeshes();   // every participant chassis' vehicle-death gib hull (gibCorpse loaded it at the first vehicle death)
    if (nb > 0) awards_.setXpScale(BotXpPolicy::scale(botDifficulty_));   // XP in bot matches by bot difficulty (user decision)
    evictUnusedAssets();
    LOG_INFO("match: launched %s %s (goal %d, time %d s, bots %d: friendly %d enemy %d %s)", l.map.c_str(), l.modeTag.c_str(), l.settings.goalScore,
             l.settings.timeLimit, nb, l.bots.friendly, l.bots.enemy, botDifficultyName(l.bots.difficulty));
    seat2_ = -1; seat2Pad_ = -1;   // a new match: no second local seat until a pad joins
    if (std::getenv("WFC_SEAT2BOT") && localPlayer_ >= 0 && !onlineSeats_)   // test only: seat 2 joins with the first player's pick (offline; online the launch has it)
        addLocalSeat(1, match_.players()[(size_t)localPlayer_].selection, match_.players()[(size_t)localPlayer_].hasSelectedCharacter);
    {   // [integration 09b] Systems M09b / M09c glue: every other participant's (bots') weapon cues and character cue set are
        // decoded at match load, not on a bot's first shot / spawn (PC ADAPTATION, load scheduling).
        std::vector<std::string> cls;
        for (size_t p = 0; p < match_.players().size(); ++p) {
            if ((int)p == localPlayer_) continue;
            const MatchPlayer& mp = match_.players()[p];
            const std::string ch = resolveChassis(mp.selection, match_.faction((int)p));
            const ChassisAssets* ca = chassisAssets(ch);
            if (!ca) continue;
            const std::vector<std::string>& wpn = !mp.selection.weapons.empty() ? mp.selection.weapons : ca->def.iconicWeapons;
            const std::vector<std::string>& veh = !mp.selection.vehicleWeapons.empty() ? mp.selection.vehicleWeapons : ca->def.iconicVehicleWeapons;
            for (const std::vector<std::string>* list : {&wpn, &veh})
                for (const std::string& n : *list)
                    if (const WeaponDef* d = findWeaponDef(n)) cls.push_back("TransContent.TnWeapon" + std::string(d->id));
            queueSelectionAudio(ch, wpn, veh);
        }
        if (!cls.empty()) preloadParticipantWeaponAudio(cls);
    }
    return true;
}

bool World::applyMatchDamage(int victim, int instigator, float amount, bool aoe, const std::string& damageType) {
    const bool hitDirOk = pendingHitDirOk_; const core::Vec3 hitDir = pendingHitDir_; pendingHitDirOk_ = false;
    if (!matchActive_ || match_.state() != Match::State::InProgress || victim < 0 || (size_t)victim >= match_.players().size()) return false;
    static const bool escInvuln = std::getenv("WFC_ESC_INVULN") != nullptr;   // TEST ONLY: the local player cannot die in Escalation
    if ((qaGod_ || (escInvuln && match_.settings().survival)) && victim == localPlayer_) return false;   // DEV / QA TOOLING god mode
    if (!match_.players()[(size_t)victim].alive) return false;
    // TnPlayerPawn.TakeDamage: teammates' damage is discarded except TnDamageTypeAOE (NotifyHitByFriendlyFire).
    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;
    // Escalation E3b: a hit on a Brute's backpack (an attached destructible) never reaches the pawn
    if (sv_.phase != SurvivalState::Phase::Off && !svKillingBrute_ && svBackpackHit(victim, instigator, amount, damageType)) return false;
    if (hitDirOk) lastHitDir_[victim] = hitDir; else lastHitDir_.erase(victim);
    Health* h = nullptr;
    MatchOpponent* opp = nullptr;
    if (victim == localPlayer_) h = &player_.pawn().health();
    else for (MatchOpponent* o : opponents_) if (o->matchPlayer() == victim) { opp = o; h = &o->health(); }
    if (!h) return false;
    if (cp_.active) {   // campaign: an interact's user is invincible; Omega's states route / clamp the damage
        if (cpInteractUserInvincible(victim)) return false;
        if (cpOmegaDamage(victim, instigator, amount, *h)) return false;
    }
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
    if (sv_.phase != SurvivalState::Phase::Off) amount *= svDamageMul(victim, instigator);   // Escalation wave buffs
    float applied = h->applyDamage(amount);
    // TnPawn.PlayHit -> the robot form's hit reaction, unless the damage type has ShouldCauseHitReaction false [C, RE HITREACT 2]
    if (applied > 0.0f && instigator >= 0 && instigator != victim) {
        static const char* const kNoReact[] = {"AudioBlast", "DecoyDot", "Drain", "TransformDisruptorDot", "HealType"};   // Drain covers DownedDrain
        bool react = !(damageType.size() >= 13 && damageType.compare(damageType.size() - 13, 13, ".TnDamageType") == 0) && damageType != "TnDamageType";
        for (const char* k : kNoReact) if (damageType.find(k) != std::string::npos) react = false;
        Character* vc = victim == localPlayer_ ? &player_.pawn() : (opp ? &opp->pawn() : nullptr);
        if (react && vc) vc->playHitReaction(vc->actorLocation() - match_.playerLocation(instigator));
    }
    if (applied > 0.0f) {                                   // TnPlayerPawn.TakeDamage -> ExposeSelf
        if (victim == localPlayer_) player_.pawn().exposeSelf(); else if (opp) opp->pawn().exposeSelf();
    }
    match_.recordDamage(victim, instigator, applied);
    if (victim == localPlayer_ && applied > 0.0f && instigator != victim) {
        ++damageTakenCount_; lastDamageFrom_ = match_.playerLocation(instigator);
        const core::Vec3 d = lastDamageFrom_ - player_.pawn().position();
        const float yawTo = std::atan2(-d.x, -d.z);
        pendingDamageTaken_.push_back({std::remainder(yawTo - player_.controller().camYaw(), 6.2831853f), applied, instigator, lastDamageFrom_});
    }
    if (instigator == localPlayer_ && victim != localPlayer_ && applied > 0.0f) pendingDamageCaused_.push_back({victim, applied, h->isDead()});
    seat2NoteDamage(victim, instigator, applied, h->isDead());   // split screen: seat 2's own queues
    if (h->isDead() && instigator >= 0 && instigator != victim) {
        Character* kp = instigator == localPlayer_ ? (localDead_ ? nullptr : &player_.pawn()) : nullptr;
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == instigator && o->spawned()) kp = &o->pawn();
        if (kp && kp->refillOnKillRemain_ > 0.0f) kp->health().heal(Health::HealType::AddAllSegments, 1.0f);   // TnBuffRefillHealthOnKill
    }
    if (h->isDead()) {
        if (anySmart_ && instigator >= 0 && instigator != victim && (size_t)instigator < match_.players().size() &&
            match_.players()[(size_t)instigator].kind == ParticipantKind::Bot)
            if (const Character* vp = participantPawn(victim)) if (const Character* kp2 = participantPawn(instigator)) {
                const int side = botIsSmart(instigator) ? 0 : 1;
                core::Vec3 d = kp2->position() - vp->position(); d.y = 0.0f;
                const core::Vec3 f = core::forwardFromYawPitch(vp->yaw(), 0.0f);
                ++aiKills_[side];
                if (core::length(d) > 1e-3f && core::dot(core::normalize(d), f) < 0.0f) ++aiBackKills_[side];
            }
        if (anySmart_) {   // Smart hunt outcomes (diagnostics): the hunter killed its enemy, or died first
            const float nowE = match_.matchTime();
            if (instigator >= 0 && (size_t)instigator < smart_.size()) {
                SmartBot& ks = smart_[(size_t)instigator];
                if (ks.active && ks.episodeTarget == victim && nowE <= ks.episodeUntil) { ++(ks.episodeFlank ? ks.flankWins : ks.huntWins); ks.episodeTarget = -1; }
            }
            if (victim >= 0 && (size_t)victim < smart_.size()) {
                SmartBot& vs = smart_[(size_t)victim];
                if (vs.active && vs.episodeTarget >= 0 && nowE <= vs.episodeUntil) { ++(vs.episodeFlank ? vs.flankLosses : vs.huntLosses); }
                vs.episodeTarget = -1;
            }
        }
        const Match::KillContext kc = killContext(instigator, victim, damageType);
        if (victim == localPlayer_) killLocalPlayer(instigator, false, damageType, &kc);
        else {
            match_.killed(instigator, victim, false, damageType, &kc);
            if (opp) {
                // [Gameplay] the victim's death presentation (death sound; the onPawnDeath hook) at the pawn as it died, before the
                // despawn: tickMatch's PlayerKilled event presents only a still-spawned pawn, so every participant killed here went
                // silent. Presentation only (no sim state; the despawn and the event order are unchanged). Same damage type as the event.
                if (opp->spawned()) {
                    const std::string kdt = match_.killHistory().empty() ? damageType : match_.killHistory().back().damageType;
                    requestCorpse(opp->pawn(), victim, 100 + victim, true, kdt);   // [Gameplay] the death body (presentation only)
                    onPawnDeath(opp->pawn().chassis().id, opp->pawn().moveForm() == Form::Vehicle, kdt, opp->pawn().actorLocation());
                }
                if (opp->spawned()) noteVehicleDeath(opp->pawn());   // VehicleDying: the body keeps blocking until the gib
                opp->despawn();
            }
        }
    }
    return true;
}

// A new match is a fresh level in the original (MatchOver -> ReturnToGameLobby -> ServerTravel): nothing the previous match spawned
// survives. Without this, projectiles in flight, sentries, barriers (with their collision), rollers, beacons and buff shots carried
// into the next match under owner indices that now name other players (WFC_ASYNCSTEPTEST: a carried actor hit P30 at step 676).
void World::clearMatchActors() {
    for (Projectile& p : projectiles_) if (p.fxHandle >= 0) { fxStepStop(p.fxHandle); p.fxHandle = -1; }
    projectiles_.clear();
    for (BarrierState& b : barriers_) {
        if (b.dyn >= 0) { collision_.setDynamicEnabled(b.dyn, false); freeBarrierDyn_.push_back(b.dyn); b.dyn = -1; }
        if (b.dynW >= 0 && weaponCollision_.valid()) { weaponCollision_.setDynamicEnabled(b.dynW, false); freeBarrierDynW_.push_back(b.dynW); b.dynW = -1; }
    }
    barriers_.clear();
    sentries_.clear();
    rollers_.clear();
    beacons_.clear();
    decoys_.clear();
    buffShots_.clear();
    for (Corpse& c : corpses_) for (CorpseChunk& k : c.chunks) if (k.fx >= 0) fxStepStop(k.fx);
    for (Corpse& c : corpses_) { for (auto& f : c.playFx) fxStepStop(f.first); c.playFx.clear(); }
    corpses_.clear();
    for (auto& kv : minePres_) if (kv.second.fx >= 0) fxStepStop(kv.second.fx);
    minePres_.clear(); mineFades_.clear();
    mines_.clear();
    missile_ = GuidedMissile{}; missileDelay_ = -1.0f; seatMissiles_.clear();
    // The local player's world-level combat state (the pawn's own buffs reset with its respawn).
    lockCandidate_ = lockTarget_ = -1; lockTimer_ = holdLockTimer_ = 0.0f; locked_ = false;
    grenadeTossDelay_ = -1.0f; grenadeCooldown_ = 0.0f;
    deferredKillstreak_ = false; lockedClip_ = 0;
    repairBeam_.time = 0.0f; repairBeam_.active = false;
    lastSentryHit_ = lastBarrierHit_ = -1;
    partWeapons_.clear();
    participantShots_.clear();
    partShotFx_.clear();
    pendingDamageTaken_.clear();
    pendingDamageCaused_.clear();
    seat2Taken_.clear(); seat2Caused_.clear();
    for (auto& kv : partBeams_) kv.second.time = 0.0f;   // stopped at the next draw
}

void World::removeBots() {
    // Every piece of Smart match state starts fresh with the match (DETERMINISMTEST runs two matches in one process: a threat-field
    // timer phase or last match's field carried over made the second run diverge).
    smart_.clear(); anySmart_ = false; smartNoises_.clear(); smartBlasts_.clear();
    smartThreatTimer_ = 0.0f; smartThreat_[0].clear(); smartThreat_[1].clear(); smartLocalShot_ = 0;
    smartThreatSnap_[0].clear(); smartThreatSnap_[1].clear(); smartThreatCursor_ = 0;
    for (auto& board : smartBoard_) board.clear();
    size_t first = match_.players().size();
    for (size_t i = match_.players().size(); i-- > 0;) { if (match_.players()[i].kind != ParticipantKind::Bot && match_.players()[i].sessionMember.empty()) break; first = i; }
    if (first == match_.players().size()) { bots_.clear(); return; }
    for (size_t i = 0; i < opponents_.size();) {
        if ((size_t)opponents_[i]->matchPlayer() >= first) { opponents_[i]->destroy(); opponents_.erase(opponents_.begin() + (long)i); rebuildOppIndex(); } else ++i;
    }
    for (size_t i = 0; i < actors_.size();) { if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); } else ++i; }
    match_.truncatePlayers(first);
    bots_.clear();
    botSearchOwner_ = -1;
    botSearchQueue_.clear();
    for (auto& kv : partBeams_) kv.second.time = 0.0f;   // stopped at the next draw
}

int World::addBots(const BotLaunch& launch) {
    // Per-faction counts (team modes) become friendly / enemy relative to the human's team (team 0 Autobots, 1 Decepticons).
    BotLaunch b = launch;
    const int refPlayer = seatOrdered_ ? 0 : localPlayer_;   // online: seat 0 (the same reference on every peer)
    if (match_.settings().teamGame && (b.autobot >= 0 || b.decepticon >= 0) && refPlayer >= 0) {
        const bool humanAutobot = match_.players()[(size_t)refPlayer].team != 1;
        const int a = std::max(0, b.autobot), d = std::max(0, b.decepticon);
        b.friendly = humanAutobot ? a : d; b.enemy = humanAutobot ? d : a;
    }
    if (!matchActive_ || (b.friendly <= 0 && b.enemy <= 0)) return 0;
    ensureBotNav();   // under the match load, not on a simulation step
    botDifficulty_ = std::clamp(b.difficulty, 0, kMaxBotDifficulty);
    const MatchSettings& s = match_.settings();
    std::vector<std::string> taken;
    int humans = 0;
    for (const MatchPlayer& p : match_.players()) { taken.push_back(p.name); humans += p.kind != ParticipantKind::Bot && !p.dormant; }
    const int humanTeam = refPlayer >= 0 ? match_.players()[(size_t)refPlayer].team : 0;
    static unsigned matchSeed = 0x5eed;
    // WFC_SEED (DEV / TEST only): offsets every bot / spawn random source so two runs can be compared or varied; unset = unchanged.
    // with WFC_SEED every launch uses the same identity seed (no chaining from earlier matches), so a launch replays exactly
    if (hostSeed_) { matchSeed = hostSeed_ * 2654435761u ^ 0x5eedu; std::srand(hostSeed_); }   // online: the host's seed
    else if (const char* sd = std::getenv("WFC_SEED")) { const unsigned v = (unsigned)std::strtoul(sd, nullptr, 10); matchSeed = v * 2654435761u ^ 0x5eedu; std::srand(v); }
    matchSeed = matchSeed * 1664525U + 1013904223U;
    const std::vector<BotIdentity> ids = makeBotIdentities(b, s.teamGame, humanTeam, s.maxPerTeam, s.maxBotsPerTeam, s.maxPlayers, humans, taken, matchSeed);
    for (const BotIdentity& id : ids) {
        const int p = match_.addPlayer(id.name, id.team);
        MatchPlayer& mp = match_.playerMutable(p);
        mp.kind = ParticipantKind::Bot;
        mp.level = id.level;
        match_.selectCharacter(p, id.selection);
        preloadHeldWeaponModels(id.selection.weapons);   // under the match load, not at the bot's first shot
        // [integration 09c] The bot's body too (robot / vehicle / arm glb + Rendering M53 program / texture prewarm), under the
        // match load instead of at its first spawn (measured: ~180 ms frame at an opponent's first draw). Cached per chassis.
        chassisAssets(resolveChassis(id.selection, match_.faction(p)));
        auto o = std::make_unique<MatchOpponent>(p, mp.team, true);
        o->pressesPickup = true;
        const int perTeam = mp.team == 0 ? b.difficultyAutobot : (mp.team == 1 ? b.difficultyDecepticon : -1);
        addBotBrain(p, perTeam >= 0 ? perTeam : botDifficulty_);
        {   // Bot AI: WFC_BOTAI (DEV / TEST) over the per-faction option over ?BotAI over the default
            static const char* envAi = std::getenv("WFC_BOTAI");
            const int perTeamAi = mp.team == 0 ? b.aiAutobot : (mp.team == 1 ? b.aiDecepticon : -1);
            BotAi ai = perTeamAi >= 0 ? (BotAi)perTeamAi : (b.ai >= 0 ? (BotAi)b.ai : BotAi::Smart);   // default Smart (user, M2 close 2026-10-11)
            ai = botAiFromString(envAi, ai);
            if (smart_.size() <= (size_t)p) smart_.resize((size_t)p + 1);
            smart_[(size_t)p] = SmartBot{};
            smart_[(size_t)p].active = ai == BotAi::Smart;
            smart_[(size_t)p].rng = 0x51a7b07U ^ ((unsigned)p * 2654435761U);
            anySmart_ |= ai == BotAi::Smart;
            LOG_INFO("bots: p%d AI %s", p, botAiName(ai));
        }
        opponents_.push_back(o.get());
        rebuildOppIndex();
        actors_.push_back(std::move(o));
        LOG_INFO("bots: %s team %d %s (%s) level %d abilities %s/%s", id.name.c_str(), mp.team, specialtyName(id.selection.specialty),
                 resolveChassis(id.selection, match_.faction(p)).c_str(), id.level,
                 id.selection.abilities.size() > 0 ? id.selection.abilities[0].c_str() : "-", id.selection.abilities.size() > 1 ? id.selection.abilities[1].c_str() : "-");
    }
    if (anySmart_) ensureSmartTactics();   // under the match load (Smart cover / exposure tables, once per map)
    return (int)ids.size();
}

// ---- Online sessions: Remote participants and slot handovers (see World.h RemoteSeat / SlotSwap) ----
int World::addRemoteParticipant(const RemoteSeat& r, bool exactTeam) {
    const int p = exactTeam ? match_.addPlayerExact(r.name, r.team) : match_.addPlayer(r.name, r.team);
    {
        MatchPlayer& mp = match_.playerMutable(p);
        mp.kind = ParticipantKind::Remote;
        mp.sessionMember = r.member;
        if (r.dormant) { mp.dormant = true; mp.team = 255; }
    }
    if (r.selected) match_.selectCharacter(p, r.selection);
    const MatchPlayer& mp = match_.players()[(size_t)p];
    auto o = std::make_unique<MatchOpponent>(p, mp.team, true);
    opponents_.push_back(o.get());
    rebuildOppIndex();
    actors_.push_back(std::move(o));
    if (mp.hasSelectedCharacter) chassisAssets(resolveChassis(mp.selection, match_.faction(p)));   // under the match load
    if (remoteIdle_.size() <= (size_t)p) remoteIdle_.resize((size_t)p + 1, 0.0f);
    remoteIdle_[(size_t)p] = 0.0f;
    LOG_INFO("session: p%d %s team %d is Remote (member %s)", p, r.name.c_str(), mp.team, r.member.c_str());
    return p;
}

bool World::applySlotSwap(const SlotSwap& s) {
    int p = s.matchPlayer;
    if (p < 0 && !s.member.empty())
        for (size_t i = 0; i < match_.players().size(); ++i) if (match_.players()[i].sessionMember == s.member) { p = (int)i; break; }
    if (p < 0 || (size_t)p >= match_.players().size() || p == localPlayer_) return false;
    const ParticipantKind k = match_.players()[(size_t)p].kind;
    if (s.toBot ? k != ParticipantKind::Remote : (k != ParticipantKind::Bot || s.member.empty())) return false;
    SlotSwap q = s; q.matchPlayer = p;
    if (q.atTick <= 0) q.atTick = simTick_ + 1;
    pendingSwaps_.push_back(q);
    return true;
}

bool World::applyRemoteCommand(int p, const RemoteCommand& c) {
    static_assert((int)platform::Button::Count <= 29, "Button bits must stay below kRemoteBitHover");
    if (p < 0 || (size_t)p >= match_.players().size() || (match_.players()[(size_t)p].kind != ParticipantKind::Remote && !(onlineSeats_ && p == localPlayer_))) return false;
    if ((int64_t)c.tick != simTick_ + 1) return false;   // the step about to run
    // The original's "active": any held / pressed button (the sender's active bit = look axis / team chat) or a non-zero move axis.
    if ((c.buttons & ~kRemoteBitAimPoint) || c.pressed || c.moveForward || c.moveRight) noteRemoteCommand(p);   // bit 31: not an input
    remoteQueue_.push_back({p, c});   // applied after joinStep (the previous step's background part may still be running now)
    return true;
}

void World::applyQueuedRemoteCommands(float dt) {
    std::stable_sort(remoteQueue_.begin(), remoteQueue_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });   // ascending match player
    for (const auto& q : remoteQueue_) {
        if (onlineSeats_ && q.first == localPlayer_) {   // the local seat (online): the decoded command, as every peer applies it
            SeatRef s; s.pc = &player_.pawn(); s.player = localPlayer_; s.spawned = !localDead_;
            applySeat(s, q.second, dt);
            localSeatIntent_ = s.intent; localSeatPickup_ = s.pressesPickup; localSeatAimPitch_ = q.second.aimPitch;
            continue;
        }
        for (MatchOpponent* o : opponents_)
            if (o->matchPlayer() == q.first && (size_t)q.first < match_.players().size() && match_.players()[(size_t)q.first].kind == ParticipantKind::Remote) applyRemoteNow(*o, q.second, dt);
    }
    remoteQueue_.clear();
}

void World::applyRemoteNow(MatchOpponent& o, const RemoteCommand& c, float dt) {
    SeatRef s; s.pc = &o.pawn(); s.player = o.matchPlayer(); s.spawned = o.spawned();
    applySeat(s, c, dt);
    o.setIntent(s.intent); o.pressesPickup = s.pressesPickup;
}

void World::applySeat(SeatRef& s, const RemoteCommand& c, float dt) {
    const int p = s.player;
    auto held = [&](platform::Button b) { return ((c.buttons >> (int)b) & 1u) != 0; };
    auto down = [&](platform::Button b) { return ((c.pressed >> (int)b) & 1u) != 0; };
    s.pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);   // TryPickup
    if (seatInteract_.size() <= (size_t)p) seatInteract_.resize((size_t)p + 1, 0);
    seatInteract_[(size_t)p] = held(platform::Button::Interact) ? 1 : 0;
    {
        MoveIntent in;
        in.moveForward = c.moveForward / 127.0f; in.moveRight = c.moveRight / 127.0f;
        in.faceYaw = c.aimYaw; in.viewPitch = c.aimPitch;
        in.wantJump = down(platform::Button::Jump);   // an edge, as the direct path's wantJumpLatched_
        in.wantBoost = held(platform::Button::FineAim);          // RightMouseButton = "ToggleFineAim | Boost" (the original's move flag 512)
        if (seatFineAim_.size() <= (size_t)p) seatFineAim_.resize((size_t)p + 1, 0);
        seatFineAim_[(size_t)p] = in.wantBoost ? 1 : 0;
        in.wantDash = down(platform::Button::Dash);
        in.ascend = held(platform::Button::Ascend); in.descend = held(platform::Button::Descend);
        in.hoverRequest = (c.buttons & kRemoteBitHover) != 0;     // the original's move flag 1024
        if (!s.spawned) { s.intent = MoveIntent{}; return; }
        Character& pc = *s.pc;
        pc.setAimPitch(c.aimPitch);
        {   // the homing lock from the eye toward the command's aim point
            const Weapon* hw = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();
            const core::Vec3 eye = botEye(pc), at = seatAimPoint(pc, c, hw && hw->rangeM > 0.0f ? hw->rangeM : 300.0f);
            const core::Vec3 d = at - eye;
            tickSeatLock(p, pc, eye, core::length(d) > 1e-4f ? core::normalize(d) : core::forwardFromYawPitch(c.aimYaw, c.aimPitch), dt);
        }
        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }   // TnBuffDecoyTrap: no movement / buttons
        if (SeatMissile* gm = seatMissileMut(p); gm && gm->alive) {   // GuidingMissile: ClearInputs / StopMovement; the axes steer
            gm->lr = core::clampf(c.moveRight / 127.0f, -1.0f, 1.0f); gm->ud = core::clampf(c.moveForward / 127.0f, -1.0f, 1.0f);
            if (down(platform::Button::Dash) || down(platform::Button::Ability1)) gm->detonate = true;   // the ability button again
            MoveIntent z; z.faceYaw = pc.yaw(); z.viewPitch = c.aimPitch; s.intent = z; s.pressesPickup = false;
            return;
        }
        const bool robot = pc.moveForm() == Form::Robot && !pc.isTransforming();
        if (pc.moveForm() == Form::Robot) in.wantDash = false;   // robot form: Dash = Ability0 (the vehicle special move only in vehicle form)
        // Abilities: Ability0 = Dash in robot form (Shift), Ability1 (Ctrl) [CONF Controls card]; Dodge / Hover go into this step's intent.
        const core::Vec3 aimPt = seatAimPoint(pc, c, 300.0f);   // abilities: the homing / buff-shot reach
        if (robot && down(platform::Button::Dash)) remoteTriggerAbility(s, 0, c.aimYaw, aimPt, in);
        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(s, 1, c.aimYaw, aimPt, in);
        if (SeatMissile* gm = seatMissileMut(p); gm && gm->delay >= 0.0f) { gm->yaw = c.aimYaw; gm->pitch = c.aimPitch; }   // spawn rotation
        // Killstreak (TriggerKillstreak, B): the newest earned reward, robot form [the same entry the bots use].
        if (robot && down(platform::Button::Killstreak) && !match_.players()[(size_t)p].acquiredKillstreaks.empty()) triggerKillstreakFor(p);
        s.intent = in;
        if (down(platform::Button::Transform) && !pc.isTransforming()) {   // StartTransform with the direct path's checks (every peer)
            if (p == localPlayer_) player_.controller().tryBeginTransformSeat(pc, collision());
            else PlayerController::tryBeginTransformOn(pc, collision(), nullptr, nullptr);
        }
        if (robot && down(platform::Button::Melee) && !pc.isMeleeing()) startMeleeFor(pc, p, false, c.aimYaw);
        if (robot && down(platform::Button::NextWeapon)) pc.requestWeaponSwitch(1);
        if (robot && down(platform::Button::PrevWeapon)) pc.requestWeaponSwitch(-1);
        Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : (robot ? &pc.weapon() : nullptr);
        if (w && down(platform::Button::Reload) && w->canReload()) w->beginReload();
        // Fire: auto weapons while held, the others on the press (bAutoFire); an empty clip reloads.
        const core::Vec3 fireAt = w ? seatAimPoint(pc, c, w->rangeM > 0.0f ? w->rangeM : 300.0f) : core::Vec3{0, 0, 0};
        if (w && robot && w->charge()) {
            // TnChargeWeapon (the local path's state machine): hold -> Charging, the level by ChargeTime, full charge drains clip ammo,
            // release fires the level's projectile (level 1: a fizzle), an empty clip reloads.
            const bool chargeHeld = held(platform::Button::Fire) || down(platform::Button::Fire);
            const bool usable = !pc.isMeleeing();
            if (w->chargeState == 0) {
                if (chargeHeld && usable && w->ammo > 0 && !w->reloading() && w->sinceCharge >= w->fireInterval) { w->setChargeState(1); w->chargeTime = 0.0f; w->chargeDrained = 0.0f; }
                else if (usable && w->ammo == 0 && w->canReload()) w->beginReload();
            } else if (!usable || w->reloading()) {
                w->setChargeState(0); w->sinceCharge = 0.0f;
            } else {
                w->chargeTime += dt;
                w->setChargeState(w->desiredChargeState());
                bool release = !held(platform::Button::Fire);
                if (w->chargeState == 4 && !release) {
                    const float nd = w->chargeDrained + dt * Weapon::kChargeDrainRate;
                    const int take = (int)nd - (int)w->chargeDrained;
                    w->chargeDrained = nd; w->ammo = std::max(0, w->ammo - take);
                    if (w->ammo == 0) release = true;
                }
                if (release) {
                    if (w->chargeState >= 2) {
                        const int mode = w->chargeState - 2;
                        const Weapon::ChargeLevel& L = Weapon::chargeLevel(mode);
                        w->chargeShotLevel = mode + 1;
                        Weapon shot = *w;
                        shot.projSpeed = L.speed; shot.projDamage = L.damage; shot.projRadiusM = L.radiusM; shot.projClass = mode;
                        w->onFired();
                        w->ammo = std::max(0, w->ammo - (L.shotCost - 1));
                        remoteFire(s, shot, fireAt, false);
                    } else ++w->chargeFizzle;
                    w->setChargeState(0); w->sinceCharge = 0.0f;
                    if (w->ammo == 0 && w->canReload()) w->beginReload();
                }
            }
        } else {
            const bool wantFire = w && !pc.isMeleeing() && (w->autoFire ? held(platform::Button::Fire) : down(platform::Button::Fire));
            if (wantFire && w->canFire()) remoteFire(s, *w, fireAt);
            else if (wantFire && w->ammo == 0 && w->canReload()) w->beginReload();
        }
        // Grenade: TossGrenade along the aim (the throw anim, released 0.4 s later, as the bots').
        if (robot && down(platform::Button::Grenade) && !pc.isMeleeing() && pc.carryingHeavy_ == 0) {
            bool pending = false;
            for (const RemoteGrenade& g : remoteGrenades_) pending |= g.player == p && g.delay >= 0.0f;
            for (Weapon& bag : pc.inventoryMutable())
                if (!pending && bag.grenade() && bag.reserve > 0) {
                    bag.reserve -= 1;
                    pc.playAction("GrenadeThrow", true);
                    pc.exposeSelf();
                    remoteGrenades_.push_back({p, 0.4f, (c.buttons & kRemoteBitAimPoint) ? c.aimPoint : botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * 20.0f});
                    break;
                }
        }
    }
}

bool World::remoteTriggerAbility(SeatRef& s, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in) {
    Character& pc = *s.pc;
    if (slot < 0 || slot > 1 || pc.weapon().reloading() || pc.isDodging() || pc.jammedRemain_ > 0.0f) return false;
    Character::AbilitySlot& a = pc.abilities_[slot];
    const int p = s.player;
    // Escalation: a player's abilities run on the ability resource (>= 100 to trigger; no cooldown) instead of the cooldowns
    const bool resMode = sv_.phase != SurvivalState::Phase::Off && p >= 0 && (size_t)p < match_.players().size() &&
                         match_.players()[(size_t)p].kind != ParticipantKind::Bot;
    if (resMode) { if (!a.implemented || a.spam > 0.0f || (size_t)p >= sv_.abilityResource.size() || sv_.abilityResource[(size_t)p] < 100.0f) return false; }
    else if (!a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) return false;
    if (a.id == "Whirlwind") { if (pc.isMeleeing()) return false; startMeleeFor(pc, p, true, aimYaw); if (!pc.isMeleeing()) return false; }
    else if (a.id == "Dodge") {   // TnPlayerInput.Dodge: |Up| >= |Right| ? (Up < 0 back : forward) : (Right < 0 left : right)
        in.dodgeDir = std::fabs(in.moveForward) >= std::fabs(in.moveRight) ? (in.moveForward < 0.0f ? 4 : 3) : (in.moveRight < 0.0f ? 1 : 2);
    }
    else if (a.id == "Cloaking") pc.cloakRemain_ = 20.0f;
    else if (a.id == "Hover") { in.hoverRequest = true; pc.hoverRequested_ = true; }
    else if (a.id == "Warcry") applyWarcry(pc, p);
    else if (a.id == "Shockwave") pc.shockwaveDelay_ = 0.25f;
    else if (a.id == "SpawnSentry") requestSentry(p);
    else if (a.id == "Barrier") { pc.playAction("Skill_Barrier", false); requestBarrier(p); }
    else if (a.id == "DecoyTrap") { pc.playAction("Skill_Barrier", false); requestDecoyTrap(p); }
    else if (a.id == "SpawnAmmoCrate") { pc.playAction("Skill_Barrier", false); requestAmmoBeacon(p); }
    else if (a.id == "RollerSphere") { pc.playAction("Skill_AbilityJammer", false); requestRoller(p); }
    else if (a.id == "Drain") pc.drainRemain_ = 7.0f;
    else if (a.id == "HardLock" || a.id == "MarkTarget") {   // TnBuffHardLocked 10 s on the homing-lock pick (picker 4) toward the aim; fails without one
        pc.playAction("Skill_MarkTarget", false);
        const core::Vec3 eye = botEye(pc);
        const int tgt = pickTargetFrom(p, eye, core::normalize(aimPt - eye), true, 300.0f);
        if (Character* t = tgt >= 0 ? participantPawnMutable(tgt) : nullptr) { t->hardLockedRemain_ = 10.0f; t->hardLockedByTeam_ = match_.players()[(size_t)p].team; }
    }
    else if (a.id == "AbilityJammer" || a.id == "TransformDisruptor") {   // the buff shot from the owner toward the aim point
        const bool jam = a.id == "AbilityJammer";
        pc.playAction("Skill_AbilityJammer", false);
        const core::Vec3 rt = core::normalize(core::cross(core::forwardFromYawPitch(pc.yaw(), 0.0f), core::Vec3{0, 1, 0}));
        const core::Vec3 from = pc.actorLocation() + (jam ? rt * 1.75f + core::Vec3{0, 0.25f, 0} : core::Vec3{0, 0.25f, 0});
        const core::Vec3 dir = core::normalize(aimPt - from);
        buffShots_.push_back({from, dir * (jam ? 100.0f : 60.0f), jam ? 5.0f : 2.5f, 3.0f, jam ? 0 : 1, p});
    }
    else if (a.id == "GuidedMissile") { if (seatGuiding(p)) return false; startSeatMissile(p, aimYaw, 0.0f); }
    else return false;
    a.spam = 1.0f;
    if (resMode) sv_.abilityResource[(size_t)p] = 0.0f; else a.pendingCooldown = true;
    if (participantAbilityHook) presentHook([this, p, id = std::string(a.id), ch = pc.chassis().id, at = pc.actorLocation()] { participantAbilityHook(p, id, ch, at); });
    return true;
}

core::Vec3 World::seatAimPoint(const Character& pc, const RemoteCommand& c, float range) const {
    if (c.buttons & kRemoteBitAimPoint) return c.aimPoint;   // the sender's camera-trace result (absolute)
    return botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * range;
}

int World::pickTargetFrom(int self, const core::Vec3& eye, const core::Vec3& fwd, bool allowRobots, float range) const {
    // The homing-lock pick (picker 4) from any participant's eye: the enemy nearest the aim inside max(4 deg, atan(3.5 m / d)), in range.
    int best = -1; float bestAng = 1e9f;
    for (size_t i = 0; i < match_.players().size(); ++i) {
        const int q = (int)i;
        if (q == self || match_.sameTeam(q, self)) continue;
        const Character* t = participantPawn(q);
        if (!t || (!allowRobots && t->moveForm() == Form::Robot)) continue;
        const core::Vec3 d = t->actorLocation() - eye; const float dist = core::length(d);
        if (dist < 0.1f || dist > range) continue;
        const float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));
        if (ang > std::max(4.0f * 0.0174533f, std::atan(3.5f / dist)) || ang >= bestAng) continue;
        best = q; bestAng = ang;
    }
    return best;
}

void World::participantBeam(int shooter, const Weapon& w, const core::Vec3& eye, const core::Vec3& dir) {
    // TnWeaponRepair for any participant: GetRepairTarget (picker 6, Angle 4 deg widened by distance, no team filter) along the aim,
    // else the first pawn on the trace; heal a team mate / damage an enemy (RepairAmount 60 / s per fire tick).
    const float range = w.rangeM > 0.0f ? w.rangeM : 35.0f, tickSecs = w.fireInterval > 0.0f ? w.fireInterval : 0.1f;
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    int hit = -1; float bestAng = 1e9f;
    for (size_t i = 0; i < match_.players().size(); ++i) {
        const int q = (int)i;
        const Character* t = q == shooter ? nullptr : participantPawn(q);
        if (!t) continue;
        const core::Vec3 to = t->actorLocation() - eye; const float d = core::length(to);
        if (d < 0.1f || d > range) continue;
        const float allowed = std::max(4.0f * 0.0174533f, std::atan(core::clampf(d * 0.1f, 2.0f, 4.0f) / d));
        const float ang = std::acos(core::clampf(core::dot(to * (1.0f / d), dir), -1.0f, 1.0f));
        float tl;
        if (ang > allowed || ang >= bestAng || (line && line->segmentHit(eye, t->actorLocation(), tl))) continue;
        hit = q; bestAng = ang;
    }
    if (hit < 0 || !matchActive_) return;
    if (match_.sameTeam(hit, shooter)) { if (Character* t = participantPawnMutable(hit)) t->health().heal(Health::HealType::AddHealthToAll, 60.0f * tickSecs); }
    else applyMatchDamage(hit, shooter, 60.0f * tickSecs, false, "TransGame.TnDamageTypeRepairEnemy");
}

void World::remoteFire(SeatRef& s, Weapon& w, const core::Vec3& aimPt, bool consume) {
    Character& pc = *s.pc;
    const int p = s.player;
    pc.exposeSelf();
    if (consume) w.onFired();
    pc.notifyFired(w.def ? w.def->id : nullptr);   // per-shot skeletal recoil, the weapon's values (seat path)
    if (p == localPlayer_) player_.controller().onLocalShot(w.def ? w.def->id : nullptr);   // the local seat: camera recoil + shake
    const core::Vec3 eye = botEye(pc), d = core::normalize(aimPt - eye);   // from the eye toward the aim point
    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming() && &w == pc.vehicleWeapon()) {
        // The vehicle weapon's CurrentSocket: WeaponSocket_Primary, alternating with _Primary2 per shot where both are authored [CONF RE
        // pass 5 9g]; the posed bone x socket (sim state), else the actor + 1 m.
        const bool two = w.alternatesMuzzle() && pc.chassis().vehicleWeapon2.valid;
        if (!two) w.muzzleSocket = 0;
        const int sock = w.muzzleSocket;
        core::Vec3 origin = pc.actorLocation() + core::Vec3{0, 1.0f, 0};
        const SocketDef& vs = sock == 1 ? pc.chassis().vehicleWeapon2 : pc.chassis().vehicleWeapon;
        core::Mat4 bm;
        if (vs.valid && pc.form() == Form::Vehicle && pc.boneWorld(vs.bone, bm)) { const core::Mat4 m = bm * vs.local; origin = core::Vec3{m.m[12], m.m[13], m.m[14]}; }
        if (p == localPlayer_) noteVehicleShot(sock, origin);   // the local muzzle-flash presentation
        if (two) w.muzzleSocket = (sock + 1) % 2;
        if (w.projectile()) {   // RealStartLoc = the socket, aimed at the aim point
            spawnProjectile(origin, addSpread(core::normalize(aimPt - origin), pc.shotSpreadFor(w), w) * w.projSpeed, w, p);
            if (w.projHoming && seatLockTarget(p) >= 0) projectiles_.back().target = seatLockTarget(p);   // SetTarget(locked ? target : none)
            participantShots_.push_back({p, w.def ? w.def->id : "", origin, aimPt, false, -1});
            return;
        }
    }
    if (w.beam()) { participantBeam(p, w, eye, d); return; }
    if (w.projectile()) {
        const core::Vec3 muzzle = eye + d * 1.5f;   // the bots' muzzle stand-in (no posed held weapon on participants)
        spawnProjectile(muzzle, addSpread(d, pc.shotSpreadFor(w), w) * w.projSpeed, w, p);
        if (w.projHoming && seatLockTarget(p) >= 0) projectiles_.back().target = seatLockTarget(p);   // SetTarget(locked ? target : none)
        participantShots_.push_back({p, w.def ? w.def->id : "", muzzle, aimPt, false, -1});
    } else if (w.simulated() && !w.beam()) {
        for (int k = 0; k < std::max(1, w.shots); ++k) { pellet_ = k; fireHitscanAs(p, pc, w, eye, d); }
        pellet_ = 0;
    }
}

void World::setParticipantPosition(int p, const core::Vec3& pos) {
    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p && o->spawned()) o->setPosition(pos);
}

void World::runLocalSeat(float dt) {
    // The direct path's per-step pawn work, from the decoded command (movement intent, aim pitch) instead of the controller.
    Character& pc = player_.pawn();
    CharacterMovement::update(pc, localSeatIntent_, dt, collision());
    localSeatIntent_.wantJump = false; localSeatIntent_.wantDash = false; localSeatIntent_.dodgeDir = 0; localSeatIntent_.hoverRequest = false;   // edges consumed
    pc.setAimPitch(localSeatAimPitch_);
    pc.tickAbilities(dt); pc.tickSpreadModifier(dt); pc.tickWeaponSwitch(dt); pc.weapon().tick(dt); pc.ability().tick(dt);
    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming()) if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);
    player_.controller().presentationStep(localSeatIntent_.faceYaw, dt);   // the per-frame draw yaw between steps
}

RemoteCommand World::encodeLocalCommand() {
    PlayerController& ctl = player_.controller();
    RemoteCommand c; c.tick = (uint32_t)(simTick_ + 1);
    const MoveIntent& mi = ctl.moveIntent();
    auto q8 = [](float v) { return (int8_t)std::lround(core::clampf(v, -1.0f, 1.0f) * 127.0f); };
    c.moveForward = q8(mi.moveForward); c.moveRight = q8(mi.moveRight);
    if (const SeatMissile* gm = seatMissile(localPlayer_); gm && gm->alive) { c.moveForward = q8(ctl.guideUD()); c.moveRight = q8(ctl.guideLR()); }
    const bool veh = !localDead_ && player_.pawn().moveForm() == Form::Vehicle;
    const float yaw = veh ? mi.faceYaw : ctl.camYaw(), pitch = veh ? mi.viewPitch : ctl.camPitch();   // the aim the direct path uses
    const float kR = 65536.0f / 6.2831853f;   // rotator units
    const uint16_t qy = (uint16_t)(std::lround(yaw * kR) & 0xffff);
    const int16_t qp = (int16_t)std::lround(core::clampf(pitch, -1.5707963f, 1.5707963f) * kR);
    c.aimYaw = std::remainder((float)qy / kR, 6.2831853f); c.aimPitch = (float)qp / kR;
    c.buttons = ctl.heldBits(); c.pressed = ctl.takePressedBits();
    if (ctl.takeLookActive()) c.buttons |= kRemoteBitActive;
    if (!localDead_) {   // the crosshair ray's hit, from the direct path's start (the camera line's point nearest the pawn), absolute, 1 cm
        const core::Vec3 camPos = ctl.cameraPos(), dir = core::forwardFromYawPitch(yaw, pitch);
        const core::Vec3 start = camPos + dir * core::dot(player_.pawn().actorLocation() - camPos, dir);
        const Weapon* w = veh ? player_.pawn().vehicleWeapon() : &player_.pawn().weapon();
        const float range = w && w->rangeM > 0.0f ? w->rangeM : 300.0f;
        core::Vec3 end = start + dir * range; float t;
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        if (line && line->segmentHit(start, end, t)) end = start + dir * (range * t);
        c.aimPoint = core::Vec3{std::round(end.x * 100.0f) / 100.0f, std::round(end.y * 100.0f) / 100.0f, std::round(end.z * 100.0f) / 100.0f};
        c.buttons |= kRemoteBitAimPoint;
    }
    return c;
}

// Split screen: the same encoding for seat 2's controller and pawn.
RemoteCommand World::encodeSeatCommand(PlayerController& ctl, Character* pawnp, int p, bool dead) {
    Character& pawn = *pawnp;
    RemoteCommand c; c.tick = (uint32_t)(simTick_ + 1);
    const MoveIntent& mi = ctl.moveIntent();
    auto q8 = [](float v) { return (int8_t)std::lround(core::clampf(v, -1.0f, 1.0f) * 127.0f); };
    c.moveForward = q8(mi.moveForward); c.moveRight = q8(mi.moveRight);
    if (const SeatMissile* gm = seatMissile(p); gm && gm->alive) { c.moveForward = q8(ctl.guideUD()); c.moveRight = q8(ctl.guideLR()); }
    const bool veh = !dead && pawn.moveForm() == Form::Vehicle;
    const float yaw = veh ? mi.faceYaw : ctl.camYaw(), pitch = veh ? mi.viewPitch : ctl.camPitch();   // the aim the direct path uses
    const float kR = 65536.0f / 6.2831853f;   // rotator units
    const uint16_t qy = (uint16_t)(std::lround(yaw * kR) & 0xffff);
    const int16_t qp = (int16_t)std::lround(core::clampf(pitch, -1.5707963f, 1.5707963f) * kR);
    c.aimYaw = std::remainder((float)qy / kR, 6.2831853f); c.aimPitch = (float)qp / kR;
    c.buttons = ctl.heldBits(); c.pressed = ctl.takePressedBits();
    if (ctl.takeLookActive()) c.buttons |= kRemoteBitActive;
    if (!dead) {   // the crosshair ray's hit, from the direct path's start (the camera line's point nearest the pawn), absolute, 1 cm
        const core::Vec3 camPos = ctl.cameraPos(), dir = core::forwardFromYawPitch(yaw, pitch);
        const core::Vec3 start = camPos + dir * core::dot(pawn.actorLocation() - camPos, dir);
        const Weapon* w = veh ? pawn.vehicleWeapon() : &pawn.weapon();
        const float range = w && w->rangeM > 0.0f ? w->rangeM : 300.0f;
        core::Vec3 end = start + dir * range; float t;
        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
        if (line && line->segmentHit(start, end, t)) end = start + dir * (range * t);
        c.aimPoint = core::Vec3{std::round(end.x * 100.0f) / 100.0f, std::round(end.y * 100.0f) / 100.0f, std::round(end.z * 100.0f) / 100.0f};
        c.buttons |= kRemoteBitAimPoint;
    }
    return c;
}

core::Vec3 World::seatEye(int p) const {
    const Character* c = participantPawn(p);
    return c ? botEye(*c) : core::Vec3{0, 0, 0};
}

void World::tickRemoteSeats(float dt) {
    for (size_t i = 0; i < match_.players().size(); ++i) {   // by match player index (the same order on every peer)
        const int p = (int)i;
        const bool local = p == localPlayer_;
        if (local ? (!onlineSeats_ || localDead_) : match_.players()[i].kind != ParticipantKind::Remote) continue;
        Character* seat = local ? &player_.pawn() : nullptr;
        if (!local) { const MatchOpponent* o = (size_t)p < oppByPlayer_.size() ? oppByPlayer_[(size_t)p] : nullptr; if (!o || !o->spawned()) continue; seat = participantPawnMutable(p); }
        if (!seat) continue;
        Character& pc = *seat;
        pc.regenBuffRemain_ = std::max(0.0f, pc.regenBuffRemain_ - dt);
        pc.fastCooldownRemain_ = std::max(0.0f, pc.fastCooldownRemain_ - dt);
        pc.ammoLockRemain_ = std::max(0.0f, pc.ammoLockRemain_ - dt);
        pc.tickAbilities(dt);
        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots' pass and the local tick run their own)
        tickSeatDrain(pc, p, dt);
        pc.ability().tick(dt);     // the legacy single-ability timer (the local path ticks it every step)
        if (pc.shockwaveDelay_ >= 0.0f && (pc.shockwaveDelay_ -= dt) < 0.0f) applyShockwave(pc, p);   // Delay 0.25 -> Shockwave()
        if (pc.moveForm() == Form::Robot) { pc.tickSpreadModifier(dt); pc.tickWeaponSwitch(dt); pc.weapon().tick(dt); }
        else if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);
    }
    for (RemoteGrenade& g : remoteGrenades_) {
        if (g.delay < 0.0f || (g.delay -= dt) >= 0.0f) continue;
        Character* pc = participantPawnMutable(g.player);
        if (pc && pc->moveForm() == Form::Robot) for (Weapon& w : pc->inventoryMutable()) if (w.grenade()) { releaseGrenade(*pc, g.player, w, g.target); break; }
    }
    remoteGrenades_.erase(std::remove_if(remoteGrenades_.begin(), remoteGrenades_.end(), [](const RemoteGrenade& g) { return g.delay < 0.0f; }), remoteGrenades_.end());
}

void World::tickSeatDrain(Character& pc, int p, float dt) {
    // TnBuffDrainSource (the local path's tick, per seat): every enemy pawn within 2000 UU with line of sight takes 25 x dt, the caster
    // heals 35 x dt per target; targets by match player index.
    if (pc.drainRemain_ <= 0.0f) return;
    pc.drainRemain_ = std::max(0.0f, pc.drainRemain_ - dt);
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    int targets = 0;
    if (matchActive_)
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const Character* t = (int)q == p ? nullptr : participantPawn((int)q);
            if (!t || match_.sameTeam((int)q, p) || core::length(t->actorLocation() - pc.actorLocation()) > 20.0f) continue;
            float tt;
            if (line && line->segmentHit(pc.actorLocation(), t->actorLocation(), tt)) continue;
            ++targets;
            const core::Vec3 at = t->actorLocation();
            applyMatchDamage((int)q, p, 25.0f * dt, false, "TransGame.TnDamageTypeDrain");
            if (p == localPlayer_) onDrainVictimTick(at);   // [Systems M08i] presentation
        }
    if (targets > 0) pc.health().heal(Health::HealType::AddHealthToAll, 35.0f * dt * targets);
    if (p == localPlayer_) onDrainTick(targets);
}

void World::startSeatMissile(int p, float yaw, float pitch) {
    Character* pc = participantPawnMutable(p);
    if (!pc) return;
    if (seatMissiles_.size() <= (size_t)p) seatMissiles_.resize((size_t)p + 1);
    SeatMissile& m = seatMissiles_[(size_t)p];
    m = SeatMissile{}; m.delay = 1.0f; m.yaw = yaw; m.pitch = pitch;   // Delay 1.0 s -> SpawnGuidedMissile
    pc->playAction("Skill_GuidedMissile", false);
    pc->missileAlive_ = true;
}

void World::detonateSeatMissile(int p, const core::Vec3& at) {
    SeatMissile* m = seatMissileMut(p);
    if (!m || !m->alive) return;
    m->alive = false; m->detonate = false;
    onAbilityActorExploded(AbilityActor::GuidedMissile, p, p == localPlayer_, at);   // [Systems] the explosion sound
    radiusDamage(at, 10000.0f, 45.0f, p, "TransGame.TnDamageTypeGuidedMissile");   // MP 10000 / 4500 UU
    LOG_INFO("guided missile (p%d) detonated at (%.1f %.1f %.1f)", p, at.x, at.y, at.z);
}

void World::tickSeatMissiles(float dt) {
    for (size_t i = 0; i < seatMissiles_.size(); ++i) {
        const int p = (int)i;
        SeatMissile& m = seatMissiles_[i];
        if (m.delay < 0.0f && !m.alive) continue;
        Character* pc = participantPawnMutable(p);
        const bool local = p == localPlayer_;
        PlayerController& ctl = player_.controller();
        if (m.delay >= 0.0f) {
            if (!pc) { m = SeatMissile{}; continue; }   // the owner died before the launch
            m.delay -= dt;
            if (m.delay < 0.0f) {
                const float pitch = core::clampf(m.pitch, 700.0f * 6.2831853f / 65536.0f, 1.5707963f);
                const core::Vec3 dir = core::forwardFromYawPitch(m.yaw, pitch);
                m.alive = true; m.life = 30.0f; m.vel = dir * 20.0f;   // FuseTime 30 s (MP); InitialSpeed 2000
                m.pos = pc->actorLocation() + core::Vec3{0, pc->robotParams().eyeHeight, 0} + dir * 2.0f;
                if (local) { ctl.setGuiding(true); if (renderer_) renderer_->notifyCameraCut("missile cam"); }
                LOG_INFO("guided missile (p%d) launched (pitch %.1f deg)", p, pitch * 57.2958f);
            }
        }
        if (m.alive && (!pc || m.detonate)) detonateSeatMissile(p, m.pos);   // the owner died / the button
        else if (m.alive) {
            const core::Vec3 f = core::normalize(m.vel);
            core::Vec3 r = core::cross(f, core::Vec3{0, 1, 0});
            if (core::length(r) < 1e-4f) r = core::cross(core::forwardFromYawPitch(m.yaw, 0.0f), core::Vec3{0, 1, 0});   // straight up
            r = core::normalize(r);
            const core::Vec3 u = core::cross(r, f);
            m.vel = m.vel + (r * m.lr + u * m.ud) * (25.0f * dt);   // ControlStrength 2500
            m.vel = core::normalize(m.vel) * 20.0f;                  // MaxSpeed 2000
            const core::Vec3 next = m.pos + m.vel * dt;
            float t; core::Vec3 n;
            const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
            bool hit = line && line->segmentHit(m.pos, next, t, n);
            core::Vec3 at = hit ? m.pos + (next - m.pos) * t : next;
            const core::Vec3 d = next - m.pos; const float len = core::length(d);
            float best = hit ? t * len : len;
            if (len > 1e-5f)
                for (size_t q = 0; q < match_.players().size(); ++q) {   // any pawn but the owner's, by match player index
                    const Character* c = (int)q == p ? nullptr : participantPawn((int)q);
                    float th;
                    if (c && MatchOpponent::pawnRayHit(*c, m.pos, d * (1.0f / len), len, th) && th < best) { best = th; hit = true; at = m.pos + d * (th / len); }
                }
            m.life -= dt;
            if (hit || m.life <= 0.0f || m.pos.y < killZ_) detonateSeatMissile(p, at);
            else m.pos = next;
        }
        if (local) {   // the local seat's missile camera (presentation)
            if (m.alive) {
                const core::Vec3 f = core::normalize(m.vel);
                const float yaw = std::atan2(-f.x, -f.z), pitch = std::asin(core::clampf(f.y, -1.0f, 1.0f));
                core::Vec3 r = core::cross(f, core::Vec3{0, 1, 0});
                r = core::length(r) < 1e-4f ? core::Vec3{1, 0, 0} : core::normalize(r);
                const core::Vec3 u = core::cross(r, f);
                ctl.setSpectatorView(m.pos + f * 1.35f + u * 1.25f, yaw, pitch, 120.0f);   // LocalOffset (135, 0, 125), FOV 120
            } else if (ctl.guiding() && m.delay < 0.0f) {
                ctl.setGuiding(false);
                ctl.clearSpectatorView();
                if (renderer_) renderer_->notifyCameraCut("missile cam end");
            }
        }
        if (pc) pc->missileAlive_ = m.alive || m.delay >= 0.0f;   // TnAbilityCooldownConditionGuidedMissile
    }
}

void World::queueLateSeat(int seat, const RemoteSeat& r, uint32_t atTick) {
    if ((int64_t)atTick <= (int64_t)simTick_) LOG_WARN("session: seat %d join for tick %u arrived at tick %lld: applied at the next step", seat, atTick, (long long)simTick_);
    PendingLateSeat p{seat, atTick, r};
    auto it = std::upper_bound(pendingLateSeats_.begin(), pendingLateSeats_.end(), p, [](const PendingLateSeat& a, const PendingLateSeat& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; });
    pendingLateSeats_.insert(it, p);
}

void World::queueLeave(int seat, uint32_t atTick) {
    const std::pair<uint32_t, int> e{atTick, seat};
    pendingLeaves_.insert(std::upper_bound(pendingLeaves_.begin(), pendingLeaves_.end(), e), e);
}

void World::applyPendingLateSeats() {
    size_t nl = 0;
    for (; nl < pendingLeaves_.size() && (int64_t)pendingLeaves_[nl].first <= (int64_t)simTick_; ++nl) {
        const int seat = pendingLeaves_[nl].second;
        if (!matchActive_ || seatPlayer(seat) < 0 || seat == localPlayer_ || match_.players()[(size_t)seat].dormant) continue;
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == seat && o->spawned()) o->despawn();
        match_.retirePlayer(seat, "left the match");
        LOG_INFO("session: seat %d left at tick %lld", seat, (long long)simTick_);
    }
    if (nl) pendingLeaves_.erase(pendingLeaves_.begin(), pendingLeaves_.begin() + (long)nl);
    size_t n = 0;
    for (; n < pendingLateSeats_.size() && (int64_t)pendingLateSeats_[n].tick <= (int64_t)simTick_; ++n) {
        const PendingLateSeat& q = pendingLateSeats_[n];
        if (!matchActive_ || seatPlayer(q.seat) < 0 || !match_.players()[(size_t)q.seat].dormant) { LOG_WARN("session: seat %d join refused (not a dormant seat)", q.seat); continue; }
        const int team = match_.settings().teamGame ? q.r.team : 255;
        if (team == 0 || team == 1) {   // a team full with bot fill: its last bot leaves (despawned, dormant) so the joiner takes its place
            int count = 0, lastBot = -1;
            for (size_t i = 0; i < match_.players().size(); ++i) {
                const MatchPlayer& mp = match_.players()[i];
                if (mp.dormant || mp.team != team) continue;
                ++count;
                if (mp.kind == ParticipantKind::Bot && !mp.directorSpawned) lastBot = (int)i;
            }
            if (count >= match_.settings().maxPerTeam && lastBot >= 0) {
                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == lastBot && o->spawned()) o->despawn();
                match_.retirePlayer(lastBot);
            }
        }
        match_.wakePlayer(q.seat, q.r.name, q.r.member, team);
        if (remoteIdle_.size() > (size_t)q.seat) remoteIdle_[(size_t)q.seat] = 0.0f;
        LOG_INFO("session: seat %d (%s) joined at tick %lld on team %d%s", q.seat, q.r.name.c_str(), (long long)simTick_, team, q.seat == localPlayer_ ? " (this machine)" : "");
    }
    if (n) pendingLateSeats_.erase(pendingLateSeats_.begin(), pendingLateSeats_.begin() + (long)n);
}

int World::smallerTeam() const {
    int n[2] = {0, 0}, h[2] = {0, 0};
    for (const MatchPlayer& mp : match_.players()) {
        if (mp.dormant || (mp.team != 0 && mp.team != 1)) continue;
        ++n[mp.team];
        h[mp.team] += mp.kind != ParticipantKind::Bot;
    }
    const int cap = match_.settings().maxPerTeam;
    if (n[0] != n[1] && !(n[0] >= cap && n[1] >= cap)) return n[0] < n[1] ? 0 : 1;
    if (h[0] != h[1]) return h[0] < h[1] ? 0 : 1;
    return 0;
}

void World::queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick) {
    if ((int64_t)atTick <= (int64_t)simTick_) LOG_WARN("session: p%d pick for tick %u arrived at tick %lld: applied at the next step", seat, atTick, (long long)simTick_);
    PendingSelection p{seat, atTick, sel};
    auto it = std::upper_bound(pendingSelections_.begin(), pendingSelections_.end(), p, [](const PendingSelection& a, const PendingSelection& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; });
    pendingSelections_.insert(it, p);
}

void World::applyPendingSelections() {
    size_t n = 0;
    for (; n < pendingSelections_.size() && (int64_t)pendingSelections_[n].tick <= (int64_t)simTick_; ++n) {
        const PendingSelection& p = pendingSelections_[n];
        if (!matchActive_ || p.seat < 0 || (size_t)p.seat >= match_.players().size()) continue;
        const bool ok = match_.selectCharacter(p.seat, p.sel);
        LOG_INFO("session: p%d Choose Character at tick %lld: %s", p.seat, (long long)simTick_, ok ? "applied (next spawn)" : "refused");
    }
    if (n) pendingSelections_.erase(pendingSelections_.begin(), pendingSelections_.begin() + (long)n);
}

void World::tickSeatLock(int p, const Character& pc, const core::Vec3& eye, const core::Vec3& fwd, float dt) {
    if (seatLocks_.size() <= (size_t)p) seatLocks_.resize((size_t)p + 1);
    SeatLock& L = seatLocks_[(size_t)p];
    const Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();
    if (!matchActive_ || pc.isTransforming() || !w || !w->projHoming || w->lockOnTime <= 0.0f) { L = SeatLock{}; return; }
    const float deg4 = 4.0f * 0.0174533f;
    int best = -1; float bestAng = 1e9f;
    for (size_t i = 0; i < match_.players().size(); ++i) {   // by match player index
        const int q = (int)i;
        if (q == p || match_.sameTeam(q, p)) continue;
        const Character* t = participantPawn(q);
        if (!t) continue;
        const bool robot = t->moveForm() == Form::Robot;
        if (robot && !w->lockRobots) continue;
        const core::Vec3 d = t->actorLocation() - eye;
        const float dist = core::length(d);
        if (dist < 1e-3f || dist > w->rangeM) continue;
        const float minR = robot ? 6.0f : 5.0f, maxR = 7.0f;
        float half = std::max(std::atan(minR / dist), std::min(deg4, std::atan(maxR / dist)));
        half = std::min(half, std::atan(maxR / dist));
        const float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));
        if (ang <= half && ang < bestAng) { bestAng = ang; best = q; }
    }
    if (best >= 0) {
        L.hold = 0.0f;
        if (best != L.candidate) { L.candidate = best; L.timer = 0.0f; if (best != L.target) L.locked = false; }
        L.timer += dt;
        if (L.timer >= w->lockOnTime && (w->ammo > 0 || w->magSize <= 0)) { L.locked = true; L.target = best; }
    } else {
        L.candidate = -1; L.timer = 0.0f;
        if (L.locked) {
            L.hold += dt;
            if (L.hold >= w->holdLockOnTime || !participantPawn(L.target)) { L.locked = false; L.target = -1; L.hold = 0.0f; }
        }
    }
}

void World::clearAllParticipants() {
    // Every participant leaves (the online seat order rebuilds the numbering identically on every peer).
    for (MatchOpponent* o : opponents_) o->destroy();
    opponents_.clear();
    rebuildOppIndex();
    for (size_t i = 0; i < actors_.size();) { if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); } else ++i; }
    match_.truncatePlayers(0);
    localPlayer_ = -1;
    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear(); seatLocks_.clear(); pendingSelections_.clear(); pendingLateSeats_.clear(); pendingLeaves_.clear(); launchHumans_ = 0;
}

uint64_t World::simStateHash() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* q, size_t n) { const unsigned char* b = (const unsigned char*)q; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } };
    auto pawn = [&](const Character& c) { const core::Vec3 p = c.position(), v = c.velocity(); const float hp = c.health().current, y = c.yaw(); mix(&p, sizeof p); mix(&v, sizeof v); mix(&hp, sizeof hp); mix(&y, sizeof y); };
    for (size_t i = 0; i < match_.players().size(); ++i) if (const Character* c = participantPawn((int)i)) pawn(*c);   // by match player index
    // The bots' decision state (their RNGs, targets, aim): a divergence shows here long before it moves a pawn.
    for (const BotBrain& b : bots_) { mix(&b.player, sizeof b.player); mix(&b.rng, sizeof b.rng); mix(&b.target, sizeof b.target); mix(&b.yaw, sizeof b.yaw); }
    for (const SmartBot& sb : smart_) mix(&sb.rng, sizeof sb.rng);
    const uint32_t r = core::simRandState(); mix(&r, sizeof r);
    return h;
}

void World::noteRemoteCommand(int matchPlayer) {
    if (matchPlayer >= 0 && (size_t)matchPlayer < remoteIdle_.size()) remoteIdle_[(size_t)matchPlayer] = 0.0f;
}

void World::tickSlotSwaps(float dt) {
    if (!matchActive_) return;
    ++simTick_;
    // Host idle rule (only with "Kick Idle Players"): a Remote participant without input for kRemoteIdleKickSeconds of play goes to a bot.
    if (match_.settings().kickIdle && match_.state() == Match::State::InProgress)
        for (size_t i = 0; i < match_.players().size(); ++i) {
            const MatchPlayer& mp = match_.players()[i];
            if (mp.kind != ParticipantKind::Remote || mp.sessionMember.empty()) continue;
            if (remoteIdle_.size() <= i) remoteIdle_.resize(i + 1, 0.0f);
            remoteIdle_[i] += dt;
            if (remoteIdle_[i] < kRemoteIdleKickSeconds) continue;
            remoteIdle_[i] = -1e9f;   // queued once
            SlotSwap s; s.matchPlayer = (int)i; s.toBot = true; s.atTick = simTick_;
            pendingSwaps_.push_back(s);
            match_.notifyRemoteIdle((int)i);   // MatchEvent RemoteIdle: the host's session glue kicks that peer ("Kicked for idling")
            ++remoteIdleKicks_;
            LOG_INFO("session: p%zu idle %.0f s: kicked, the slot becomes a bot", i, kRemoteIdleKickSeconds);
        }
    for (size_t k = 0; k < pendingSwaps_.size();) {
        if (pendingSwaps_[k].atTick > simTick_) { ++k; continue; }
        const SlotSwap s = pendingSwaps_[k];
        pendingSwaps_.erase(pendingSwaps_.begin() + (long)k);
        if (s.toBot) swapSlotToBot(s.matchPlayer); else swapSlotToRemote(s.matchPlayer, s.member);
    }
}

void World::swapSlotToBot(int p) {
    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) return;
    {
        MatchPlayer& mp = match_.playerMutable(p);
        mp.kind = ParticipantKind::Bot;
        mp.sessionMember.clear();
    }
    if (!match_.players()[(size_t)p].hasSelectedCharacter) {   // never selected: the Soldier class preset (bots play class presets)
        CharacterSelection cs; cs.type = 0; cs.specialty = Specialty::Soldier;
        match_.selectCharacter(p, cs);
    }
    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) o->pressesPickup = true;
    ensureBotNav();
    addBotBrain(p, botDifficulty_);
    {   // the bot AI: WFC_BOTAI (DEV / TEST) over the default (Smart) [PROVISIONAL: the lobby's per-faction AI row is not kept per match]
        static const char* envAi = std::getenv("WFC_BOTAI");
        const BotAi ai = botAiFromString(envAi, BotAi::Smart);
        if (smart_.size() <= (size_t)p) smart_.resize((size_t)p + 1);
        smart_[(size_t)p] = SmartBot{};
        smart_[(size_t)p].active = ai == BotAi::Smart;
        smart_[(size_t)p].rng = 0x51a7b07U ^ ((unsigned)p * 2654435761U);
        anySmart_ |= ai == BotAi::Smart;
        if (anySmart_) ensureSmartTactics();
    }
    if ((size_t)p < remoteIdle_.size()) remoteIdle_[(size_t)p] = 0.0f;
    LOG_INFO("session: p%d Remote -> Bot at tick %lld (slot, score row and team kept)", p, (long long)simTick_);
}

void World::swapSlotToRemote(int p, const std::string& member) {
    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Bot) return;
    {
        MatchPlayer& mp = match_.playerMutable(p);
        mp.kind = ParticipantKind::Remote;
        mp.sessionMember = member;
    }
    bots_.erase(std::remove_if(bots_.begin(), bots_.end(), [p](const BotBrain& b) { return b.player == p; }), bots_.end());
    if ((size_t)p < smart_.size()) smart_[(size_t)p].active = false;
    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) o->pressesPickup = false;
    if (remoteIdle_.size() <= (size_t)p) remoteIdle_.resize((size_t)p + 1, 0.0f);
    remoteIdle_[(size_t)p] = 0.0f;
    LOG_INFO("session: p%d Bot -> Remote (member %s) at tick %lld (slot, score row and team kept)", p, member.c_str(), (long long)simTick_);
}

MatchOpponent* World::addMatchOpponent(const std::string& name, bool drawn) {
    int p = match_.addPlayer(name);
    auto o = std::make_unique<MatchOpponent>(p, match_.players()[(size_t)p].team, drawn);
    MatchOpponent* raw = o.get();
    opponents_.push_back(raw);
    rebuildOppIndex();
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
    if (const SeatMissile* sm = seatMissile(localPlayer_); sm && sm->alive) { h.guidedMissile = true; h.guidedMissilePos = sm->pos; h.guidedMissileFuse = sm->life; }
    { const Sentry& ls = sentry(); h.sentry = ls.alive; h.sentryHealth = ls.health; h.sentryPos = ls.pos; h.sentryTarget = ls.target; }
    h.seeEnemies = pc.seeEnemiesRemain_; h.refillOnKill = pc.refillOnKillRemain_; h.abilitiesJammed = pc.jammedRemain_; h.hardLocked = pc.hardLockedRemain_;
    h.buffs.clear();
    h.spectateTarget.clear();
    if (localDead_ && player_.controller().spectatingTarget() && specTarget_ >= 0 && (size_t)specTarget_ < match_.players().size())
        h.spectateTarget = match_.players()[(size_t)specTarget_].name;
    if (!localDead_ && pc.jammedRemain_ > 0.0f) h.buffs.push_back({pc.jammedByKillstreak_ ? "TnBuffAbilityJammedKillstreak" : "TnBuffAbilityJammed", pc.jammedRemain_});
    if (!localDead_ && pc.transformDisruptRemain_ > 0.0f) h.buffs.push_back({"TnBuffTransformDisruptor", pc.transformDisruptRemain_});
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
        if (p.dormant) continue;   // a reserved seat is nobody yet
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
    h.cpCinematic = cp_.active && cp_.cinematicMode;
    h.cpInteract = cp_.active ? cp_.interactPrompt : std::string();
    h.cpActive = cp_.active; h.cpGameOver = cp_.gameOverSent; h.cpMissionComplete = cp_.missionComplete; h.cpCheckpoint = cp_.checkpoint;
    if (sv_.phase != SurvivalState::Phase::Off) {
        h.svWave = std::max(1, sv_.displayWave + 1);
        if (sv_.phase == SurvivalState::Phase::PreGame) h.svNextWaveTime = (int)std::ceil(std::max(0.0f, svData_.preGameTime - sv_.phaseTime));
        else if (sv_.phase == SurvivalState::Phase::BetweenWaves) h.svNextWaveTime = (int)std::ceil(std::max(0.0f, svData_.timeBetweenWaves - sv_.phaseTime));
        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;
        h.svPowerOutage = sv_.mech.outageLeft > 0.0f;
        if (localPlayer_ >= 0 && (size_t)localPlayer_ < sv_.power.size()) {
            h.svPower = (int)sv_.power[(size_t)localPlayer_];
            if ((size_t)localPlayer_ < sv_.abilityResource.size()) h.svAbilityResource = sv_.abilityResource[(size_t)localPlayer_] / 100.0f;
            const int g = sv_.nearGenerator[(size_t)localPlayer_];
            if (g >= 0) { h.svGeneratorName = svData_.generators[(size_t)g].nameKey; h.svGeneratorNeeded = (int)std::ceil(svData_.generators[(size_t)g].cost - sv_.pool[(size_t)g]); }
        }
    }
    return h;
}

void World::killLocalPlayer(int killer, bool suicide, const std::string& damageType, const Match::KillContext* ctx) {
    if (!matchActive_ || localDead_) return;
    static const bool escInvuln = std::getenv("WFC_ESC_INVULN") != nullptr;   // TEST ONLY: no Escalation death (falls / stranded)
    if (escInvuln && match_.settings().survival) { respawnPlayer(); return; }
    const Match::KillContext kc = ctx ? *ctx : killContext(killer, localPlayer_, damageType);
    match_.killed(killer, localPlayer_, suicide, damageType, &kc);
    noteVehicleDeath(player_.pawn());   // VehicleDying body
    localDead_ = true;
    // [Systems] TnPlayerController.DeathSound for the local player's own death (audio only; no sim state).
    const int deathCue = abilityAudio_.localDied(cues_);
    if (deathCue >= 0) ++localDeathCues_;
    static const bool log = std::getenv("WFC_DEATHAUDIOLOG") != nullptr;
    if (log) LOG_INFO("LOCAL DEATH cue %s (%s; %d so far)", deathCue >= 0 ? "started" : "none", damageType.c_str(), localDeathCues_);
}

void World::tickMatch(float dt) {
    Character& pc = player_.pawn();
    if (!localDead_) {
        match_.setPlayerLocation(localPlayer_, pc.position(), pc.cylinderRadius(pc.moveForm()));
        if (pc.health().isDead()) killLocalPlayer(-1, false);   // damage without an instigator
    }
    for (MatchOpponent* o : opponents_) if (o->spawned()) match_.setPlayerLocation(o->matchPlayer(), o->position(), o->pawn().cylinderRadius(o->pawn().moveForm()));
    match_.tick(dt);
    tickSurvival(dt);   // Escalation director (no-op otherwise)
    tickCampaign(dt);   // campaign: Kismet, game over, squad respawn (no-op otherwise)
    // Live objective rules (DOM nodes, KOTH zone) while InProgress: pawns -> captures / scores -> the match rules.
    if (match_.state() == Match::State::InProgress && !match_.betweenRounds()) {
        mapState_.setKillZ(killZ_);
        std::vector<MapState::ObjPawn> pawns;
        // Carriers: Transform to vehicle drops the heavy weapon (TnPawn.Transform -> DropHeavyWeapons) [CONF].
        if (seatOrdered_) {   // online: by match player index
            for (size_t q = 0; q < match_.players().size(); ++q) {
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                if (c->isTransforming() || c->form() == Form::Vehicle) mapState_.dropCarriedBy((int)q, c->actorLocation());
                if ((int)q == localPlayer_ && pc.heavyDropRequested_) mapState_.dropCarriedBy((int)q, c->actorLocation());
            }
            pc.heavyDropRequested_ = false;
        } else {
        if (!localDead_ && (pc.isTransforming() || pc.form() == Form::Vehicle)) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());
        if (!localDead_ && pc.heavyDropRequested_) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());
        pc.heavyDropRequested_ = false;
        for (MatchOpponent* o : opponents_)
            if (o->spawned() && (o->pawn().isTransforming() || o->pawn().form() == Form::Vehicle)) mapState_.dropCarriedBy(o->matchPlayer(), o->pawn().actorLocation());
        }
        auto robotForm = [](const Character& c) { return c.form() == Form::Robot && !c.isTransforming() && !c.isMeleeing(); };
        const bool localPickupCtl = player_.controller().consumePickupRequest();
        const bool localPickup = onlineSeats_ ? localSeatPickup_ : localPickupCtl;   // online: the seat command's press
        localPickupLatch_ = localPickup;   // the decoy trap's pickup path reads it (next ability-actor tick)
        if (seatOrdered_) {   // online: by match player index (pickup contention resolves alike on every peer)
            for (size_t q = 0; q < match_.players().size(); ++q) {
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                const MatchOpponent* o = (int)q == localPlayer_ ? nullptr : opponentByPlayer((int)q);
                pawns.push_back({(int)q, o ? o->team() : match_.players()[q].team, c->actorLocation(), true, robotForm(*c), o ? o->pressesPickup : localPickup});
            }
        } else {
        if (!localDead_) pawns.push_back({localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, robotForm(pc), localPickup});
        for (MatchOpponent* o : opponents_)
            if (o->spawned()) pawns.push_back({o->matchPlayer(), o->team(), o->pawn().actorLocation(), true, robotForm(o->pawn()), o->pressesPickup});
        }
        MapState::ObjectiveScoring sc;
        mapState_.tickObjectives(dt, pawns, sc);
        for (auto& p : sc.personalScores) match_.addPersonalScore(p.first, p.second);
        for (auto& p : sc.objectiveScores) match_.scoreObjective(p.first, p.second);
        for (auto& t : sc.teamScores) match_.scoreTeamObjective(t.first, t.second);
        for (const auto& a : sc.actions) match_.recordObjective(a.kind, a.player, a.team, a.value);
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
        // HurtRadius (bomb detonation, AOE): every match pawn within the radius in sight of the bomb (VisibleCollidingActors).
        for (const auto& rd : sc.radiusDamage) {
            LOG_INFO("match: HurtRadius %.0f within %.0f m (%s)", rd.damage, rd.radius, rd.damageType.c_str());
            if (seatOrdered_) {   // online: by match player index
                for (size_t q = 0; q < match_.players().size(); ++q) {
                    const Character* c = participantPawn((int)q);
                    if (c && core::length(c->position() - rd.pos) <= rd.radius && radiusVisible(rd.pos, c->actorLocation()))
                        { noteHitDir(c->actorLocation() - rd.pos); applyMatchDamage((int)q, rd.instigator, rd.damage, true, rd.damageType); }
                }
                continue;
            }
            if (!localDead_ && core::length(pc.actorLocation() - rd.pos) <= rd.radius && radiusVisible(rd.pos, pc.actorLocation()))
                { noteHitDir(pc.actorLocation() - rd.pos); applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType); }
            for (MatchOpponent* o : opponents_)
                if (o->spawned() && core::length(o->position() - rd.pos) <= rd.radius && radiusVisible(rd.pos, o->pawn().actorLocation()))
                    { noteHitDir(o->pawn().actorLocation() - rd.pos); applyMatchDamage(o->matchPlayer(), rd.instigator, rd.damage, true, rd.damageType); }
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
                        if (sv_.phase != SurvivalState::Phase::Off) svApplySpawnLoadout(pc);   // SetPlayerDefaults (SV)
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
                    if (renderer_) renderer_->notifyCameraCut("spectate end");
                    if (spawnProf()) LOG_INFO("SPAWNPROF local spawn: chassis apply %.1f ms, respawnReset %.1f ms, specialty+loadout %.1f ms, rest %.1f ms (%s)",
                                              tp1 - tp0, tp2 - tp1, tp3 - tp2, profNowMs() - tp3, mp.chassis.c_str());
                    {   // TnSkillReduceRecoil -> TnBuffReduceRecoil: the local camera recoil x 0.35 (the only skill the camera reads)
                        bool rr = false;
                        for (const std::string& sk : mp.selection.skills) rr |= sk == "ReduceRecoil";
                        player_.controller().setRecoilScale(rr ? 0.35f : 1.0f);
                    }
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
                        if (chassisOk && sv_.phase != SurvivalState::Phase::Off && op.kind != ParticipantKind::Bot) svApplySpawnLoadout(o->pawn());
                        const double to2 = profNowMs();
                        core::Vec3 p = match_.starts()[(size_t)e.value].pos;
                        float gy; core::Vec3 gn;
                        if (collision_.valid() && collision_.groundHeight(p.x, p.z, p.y + 0.5f, 1.0f, gy, gn)) p.y = gy;
                        o->spawnAt(p);
                        if (chassisOk && op.directorSpawned) svApplyHordeStats(o->pawn(), o->matchPlayer());   // E3a: weapons + health (after respawnReset)
                        recordSpawnEvent(o->matchPlayer());
                        if (spawnProf()) LOG_INFO("SPAWNPROF opponent %d spawn: chassis apply %.1f ms, character %.1f ms, place+spawnAt %.1f ms (%s)",
                                                  o->matchPlayer(), to1 - to0, to2 - to1, profNowMs() - to2, op.chassis.c_str());
                    }
                break;
            case MatchEvent::Type::PlayerKilled:
                if (e.other == localPlayer_ && e.player != localPlayer_)   // [Systems M08i] the killer's kill confirm
                    for (MatchOpponent* o : opponents_)
                        if (o->matchPlayer() == e.player) {
                            onLocalKilledPawn(false, o->pawn().moveForm() == Form::Robot, o->pawn().chassis().id);   // no headshot state yet
                        }
                {   // [Systems M08o] the victim's death sound (vehicle form / robot melee death), at the victim
                    const std::string dt = match_.killHistory().empty() ? std::string() : match_.killHistory().back().damageType;
                    if (e.player == localPlayer_) {
                        const Character& vp = player_.pawn();
                        requestCorpse(vp, localPlayer_, 0, playerBotDifficulty() >= 0, dt);   // [Gameplay] the death body
                        onPawnDeath(vp.chassis().id, vp.moveForm() == Form::Vehicle, dt, vp.actorLocation());
                    } else
                        for (MatchOpponent* o : opponents_)
                            if (o->matchPlayer() == e.player && o->spawned()) {
                                requestCorpse(o->pawn(), e.player, 100 + e.player, true, dt);
                                onPawnDeath(o->pawn().chassis().id, o->pawn().moveForm() == Form::Vehicle, dt, o->pawn().actorLocation());
                            }
                }
                if (e.player == localPlayer_ && !localDead_) noteVehicleDeath(player_.pawn());
                if (e.player == localPlayer_) localDead_ = true;
                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == e.player) { if (o->spawned()) noteVehicleDeath(o->pawn()); o->despawn(); }
                break;
            case MatchEvent::Type::ReturnToLobby:
                // TnGame.ReturnToGameLobby: the host (front end / Integration) decides what follows; the local
                // runtime stops the match here and returns to free play.
                LOG_INFO("match: return to lobby");
                player_.controller().clearSpectatorView();
                if (renderer_) renderer_->notifyCameraCut("spectate end");
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
    {   // [integration 09c] EmitterPool MaxActiveEffects 50 (original) only in extended lobbies (user decision 2026-10-08)
        const int ext = extendedLobby() ? 1 : 0;
        if (ext != pushedPoolCap_) { r.setEmitterPoolCap(ext != 0); pushedPoolCap_ = ext; }
    }
    for (const MapState::ActorVisibility& v : mapState_.actorVisibility()) r.setActorHidden(v.actor, v.hidden);
    // (Escalation) the weapon factories' pickup meshes (render data render_index_extra.json; hidden by the renderer until told):
    // shown while Escalation runs, the factory is bought (unhidden) and its weapon is there - every frame, as the actors above
    for (size_t i = 0; i < svData_.weaponPickups.size(); ++i) {
        const bool live = sv_.phase != SurvivalState::Phase::Off && i < sv_.weaponPicks.size() && !sv_.weaponPicks[i].hidden &&
                          sv_.weaponPicks[i].available;
        r.setActorHidden(svData_.weaponPickups[i].actor, !live);
    }
    // (Escalation M11) the loot shards: FX_Pickups_p.FX.Shard_FX at each loot entry, 75 UU up (AssetTools
    // escalation_loot_pickup.json). The shards are alike, so a pool of effect handles follows the live entries in order -
    // repositioned every frame, grown / stopped to the count; a handle the renderer no longer knows (map change) respawns
    for (size_t i = 0; i < sv_.loot.size(); ++i) {
        const core::Vec3 p = sv_.loot[i].pos + core::Vec3{0.0f, 0.75f, 0.0f};
        if (i >= lootFx_.size()) lootFx_.push_back(-1);
        int& h = lootFx_[i];
        if (h < 0 || !r.setParticleEffectTransform(h, p, core::Vec3{1, 0, 0}, core::Vec3{0, 1, 0}))
            h = r.spawnParticleEffect("FX_Pickups_p.FX.Shard_FX", p, core::Vec3{1, 0, 0}, core::Vec3{0, 1, 0});
    }
    for (size_t i = sv_.loot.size(); i < lootFx_.size(); ++i)
        if (lootFx_[i] >= 0) { r.stopParticleEffect(lootFx_[i]); lootFx_[i] = -1; }
    lootFx_.resize(sv_.loot.size());
    svMechPresent(r);   // (Escalation DLC) the map mechanisms: matinees, emitters, the outage grade
    // (Escalation / campaign) the dropped weapons: the weapon's pickup mesh where it lies (Rendering spins / tilts / settles it)
    for (const SurvivalState::Drop& d : sv_.drops) {
        if (!d.w.def) continue;
        const std::string g = svDropMesh(d.w.def->id);
        if (!g.empty()) r.drawDroppedPickup(g, core::Vec3{d.pos.x * 100.0f, d.pos.z * 100.0f, d.pos.y * 100.0f}, d.life,
                                            std::strstr(d.w.def->id, "Kamikaze") != nullptr);
    }
    // [integration 09c] The effect keys are built once per factory (rebuilt only when the factory set changes): building
    // "<actor>|custom" / "|highlight" per factory per frame was ~12 % of main-thread allocations at 10 v 10 (Systems' ALLOCPROF).
    if (pickupFxKeys_.size() != pickupFactories_.size()
        || (!pickupFactories_.empty() && (pickupFxKeys_.front().src != pickupFactories_.front() || pickupFxKeys_.back().src != pickupFactories_.back()))) {
        pickupFxKeys_.clear();
        for (const PickupFactory* f : pickupFactories_) {
            std::string a = f->name();
            size_t dot = a.rfind('.');
            if (dot != std::string::npos) a = a.substr(dot + 1);
            pickupFxKeys_.push_back({f, a + "|custom", a + "|highlight"});
        }
    }
    for (size_t i = 0; i < pickupFactories_.size(); ++i) {
        const PickupFactory* f = pickupFactories_[i];
        if (pickupFxKeys_[i].src != f) { pickupFxKeys_.clear(); return syncMapPresentation(r); }   // the set changed in the middle
        r.setMapEffectState(pickupFxKeys_[i].custom, f->customEffectActive(), !f->meshVisible());
        r.setMapEffectState(pickupFxKeys_[i].highlight, f->beamActive(), false);
    }
    // Flag / bomb factories: Pickup state (beam on, ShouldDisplayHighlightFx inherited from TnWeaponPickupFactory)
    // in their mode; Disabled (hidden, no collision) otherwise [CONF RE MILESTONE04 pickup/objective presentation].
    const auto& objs = mapState_.objectives();
    if (objectiveFxKeys_.size() != objs.size() || (!objs.empty() && objectiveFxKeys_.front().src != &objs.front())) {
        objectiveFxKeys_.clear();
        for (const ObjectiveObject& o : objs) {
            const bool flagOrBomb = o.cls == "TnGameObjectivePickupFactoryFlag" || o.cls == "TnGameObjectivePickupFactoryBomb";
            objectiveFxKeys_.push_back({&o, std::string(), flagOrBomb ? o.actor + "|highlight" : std::string()});
        }
    }
    for (size_t i = 0; i < objs.size(); ++i)
        if (!objectiveFxKeys_[i].highlight.empty())
            r.setMapEffectState(objectiveFxKeys_[i].highlight, objs[i].visible, !objs[i].visible);
    // [integration] Destructible presentation: Rendering draws the intact / Chunk02 stump mesh from the
    // HmDestructionState Gameplay simulates (0 intact, 1 destroyed, 2 settled). WFC_DESTRUCTSTATE (Rendering's
    // diagnostic) forces the state instead.
    static const bool forcedDestruct = std::getenv("WFC_DESTRUCTSTATE") != nullptr;
    if (!forcedDestruct)
        for (const Destructible* d : destructibles_) r.setDestructibleState(d->name(), d->state());
}

void World::draw(render::IRenderer& r) const {
    syncMapPresentation(r);
    transformingDraw_.draw(transforming_, r);   // the map's transforming doors / bridges (posed at t; nothing on maps without them)
    if (mapMesh_ != render::kInvalidMesh) {
        r.drawMesh(mapMesh_, core::Mat4::identity(), mapColor_);
    } else {
        r.drawGroundGrid(60.0f, 2.0f, core::Vec3{0.30f, 0.33f, 0.38f});
        for (const auto& b : blocks_) r.drawBox(b.center, b.size, b.color);
    }
    // No gameplay-side participant culling (user decision: off-screen pawns still cast shadows / affect the image): every pawn is
    // submitted and the renderer culls per pass (main view, shadow cascades).
    {
        const bool cull = false;
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
    // WFC_DRAWPROF: the gameplay side of World::draw (actors incl. pawns: palette / submit; participants' weapons), ms per frame.
    static const bool drawProf = std::getenv("WFC_DRAWPROF") != nullptr;
    static double dpActors = 0.0, dpWeapons = 0.0; static long dpN = 0;
    const double dp0 = drawProf ? profNowMs() : 0.0;
    // [integration M08] Each participant pawn is its own character instance for TnCharacterApplier (draw owner 100 + match
    // player). [integration 09a] No customization paint and no energon override: participants carry none, so the chassis
    // material defaults draw (energon is never team-tinted, RE b0d9b22). The local pawn stays owner 0. [integration 09c] The
    // owner comes from a per-frame actor -> player map (it was a scan of opponents_ per actor: quadratic at 64).
    // A reused sorted vector (capacity kept across frames): no per-frame allocation; same lookups as the map it replaced.
    partOwner_.clear();
    for (const MatchOpponent* o : opponents_) partOwner_.emplace_back(static_cast<const void*>(o), o->matchPlayer());
    std::sort(partOwner_.begin(), partOwner_.end());
    for (const auto& a : actors_) {
        if (!a->alive()) continue;
        const void* key = static_cast<const void*>(a.get());
        const auto it = std::lower_bound(partOwner_.begin(), partOwner_.end(), key,
                                         [](const std::pair<const void*, int>& e, const void* k) { return std::less<const void*>()(e.first, k); });
        const int owner = it != partOwner_.end() && it->first == key ? 100 + it->second : 0;
        if (owner) { r.setDrawOwner(owner); r.setCharacterColors(render::CharacterColors{}); }
        a->draw(r);
        if (owner) r.setDrawOwner(0);
    }
    if (drawProf) dpActors += profNowMs() - dp0;
    if (!localPlayerDead()) {
        sysprof::Scope sp(sysprof::DrawPlayer);
        const bool split = seat2_ >= 0;   // split screen: every pawn draws with its absolute owner (the views pick theirs)
        if (split) r.setDrawOwner(100 + localPlayer_);
        player_.draw(r);
        if (split) r.setDrawOwner(0);
    }
    drawCorpses(r);   // [Gameplay] death bodies (their pawn's draw owner and colours)
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
        } else if (!fxLive(p.fxHandle)) {
            r.drawBox(p.pos, core::Vec3{0.25f, 0.25f, 0.25f}, core::Vec3{1.0f, 0.6f, 0.2f});
        }
    }

    // Ion Blaster mesh held at the weapon socket (robot form only).
    {
        sysprof::Scope sp(sysprof::DrawWeapon);
        if (localPlayerDead()) { /* no pawn: no weapon / pawn effects (PendingMatch, dead) */ }
        else if (weaponAnim_.valid() && player_.pawn().hasWeapon()) {
            // TnChargeWeapon.UpdateChargeEffects -> TnWeaponMesh.SetMaterialParameter(1, MaterialGlowAmount): index 1 of the Plasma
            // Cannon WEPMESH MaterialParameterModifiers is MPT_WeaponSpecific "Overheat" [CONF cooked data, Rendering M70]. Set for
            // this draw only and cleared after it, whatever the current draw owner (other weapons author 0 = the default).
            const Weapon& hw = player_.pawn().weapon();
            const bool glow = hw.charge() && hw.chargeGlow() > 0.0f;
            if (glow) { const float g = hw.chargeGlow(); const float rgba[4] = {g, g, g, 1.0f}; fxSetDrawParam(r, "Overheat", rgba, 0); }
            weaponAnim_.draw(r, core::Mat4::translate(player_.pawn().renderOffset()) * player_.pawn().weaponDrawWorld(), core::Vec3{1, 1, 1});   // Gameplay 151bc1f: GPU-skinned when available
            if (glow) fxClearDrawParam(r, "Overheat", 0);
        }
        else if (weaponMesh_ != render::kInvalidMesh && player_.pawn().hasWeapon())
            r.drawMesh(weaponMesh_, core::Mat4::translate(player_.pawn().renderOffset()) * player_.pawn().weaponDrawWorld(), core::Vec3{1, 1, 1});
    }

    // Participants' held weapons (robot form, weapon shown), at their pawn's interpolated weapon socket.
    const double dpw0 = drawProf ? profNowMs() : 0.0;
    for (const MatchOpponent* o : opponents_) {
        if (!o->spawned() || o->culled() || !o->pawn().hasWeapon()) continue;
        auto it = partWeapons_.find(o->matchPlayer());
        if (it == partWeapons_.end() || !it->second.anim.valid()) continue;
        it->second.anim.draw(r, core::Mat4::translate(o->pawn().renderOffset()) * o->pawn().weaponDrawWorld(), core::Vec3{1, 1, 1});
    }
    if (drawProf) {
        dpWeapons += profNowMs() - dpw0;
        if (++dpN % 300 == 0) { LOG_INFO("DRAWPROF ms/frame (%zu participants): actors %.2f, participant weapons %.2f", match_.players().size(), dpActors / 300.0, dpWeapons / 300.0); dpActors = dpWeapons = 0.0; }
    }
    // Participant shots without a presentation hook: the weapon's authored templates by name [CONF WEPMESH data].
    for (const PendingShotFx& s : partShotFx_) {
        const WeaponDef* d = findWeaponDef(s.weapon);
        const core::Vec3 at{s.muzzle.m[12], s.muzzle.m[13], s.muzzle.m[14]};
        const core::Vec3 fwd = core::normalize(core::Vec3{s.muzzle.m[0], s.muzzle.m[1], s.muzzle.m[2]});
        const core::Vec3 up = core::normalize(core::Vec3{s.muzzle.m[4], s.muzzle.m[5], s.muzzle.m[6]});
        const bool muzzle = d && d->muzzleFx && *d->muzzleFx, tracer = s.tracer && d && d->tracerFx && *d->tracerFx;
        if (muzzle) fxTeamParam(r, fxSpawnPoint(r, d->muzzleFx, at, fwd, up, 0), s.team, 0);   // owned (not pooled)
        if (tracer) fxTeamParam(r, fxPooled(r, fxSpawnSegment(r, d->tracerFx, at, s.to, 0), 0), s.team, 0);
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

    // [Systems] Drain beams (TnBuffDrainTarget: DrainRay_Beam_FX, Beam2): spawned while a victim is drained, moved every frame,
    // stopped when the drain ends or the victim leaves it. WFC_BODYNOTIFYFX=0: off (the ability presentation family).
    {
        static const bool off = std::getenv("WFC_BODYNOTIFYFX") && std::getenv("WFC_BODYNOTIFYFX")[0] == '0';
        for (const auto& kv : drainBeams_) {
            const DrainBeam& b = kv.second;
            const bool on = b.live && !off;
            if (on && b.fx < 0) b.fx = fxSpawnSegment(r, "FX_Multiplayer_p.FX.DrainRay_Beam_FX", b.start, b.end, 0);
            else if (on) fxSetSegment(r, b.fx, b.start, b.end, 0);
            else if (b.fx >= 0) { fxStopEffect(r, b.fx, 0); b.fx = -1; }
        }
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

    // Every character is queued by now: draw them before the barrier / sentry meshes (Pipeline::drawDynamic draws those
    // immediately - it queues only skinned characters with a light environment - so queued characters drawn after them
    // covered the translucent barrier grid: user playtest 2026-10-14) and before the effects below (meshes and particle
    // fallbacks, drawn immediately too). One flush per frame: nothing is queued after it.
    r.flushQueuedCharacters();
    if (cp_.active) cpDrawDestructibles(r);   // campaign destructibles
    if (cp_.active) cpDrawSkeletal(r);        // campaign matinee skeletal actors
    for (const BarrierState& bs : barriers_) if (bs.alive && !bs.mesh.positions.empty()) r.drawDynamicMesh(bs.mesh, bs.world, core::Vec3{1, 1, 1});   // TnBarrierSpawnable mesh
    for (const Sentry& se : sentries_)
        if (se.alive && !se.mesh.positions.empty())
            r.drawDynamicMesh(se.mesh, core::Mat4::translate(se.pos) * core::Mat4::rotateY(se.yaw + core::config::kMeshYawOffset), core::Vec3{1, 1, 1});
    svMechDrawSentries(r);   // (Escalation DLC) District's sentry turrets
    svDrawGenerators(r);     // (Escalation) the generators: vending machines, door / floodgate / breaker switches
    drawMines(r);   // [Gameplay] kamikaze mines (owner 0: the local player's mines, its character colours)
    drawCorpseGibs(r);   // [Gameplay] gib chunks and vehicle hull wrecks
    // Weapon + vehicle boost effects last (translucent/additive over the opaque scene): after the barrier / sentry meshes,
    // so the particle fallback's character flush (GLRenderer::drawParticles) has every dynamic mesh queued before it.
    // Transient lights reaching the view only (game-side cull; the renderer additionally masks per draw): the light's sphere
    // (pos, radius: the whole lit volume) must intersect the camera's view cone - half-angle = the diagonal of the horizontal
    // FOV at any aspect >= 1, + 5 deg - and lie within kTransientLightMaxDist of the eye. Presentation only.
    unsigned tlSubmitted = 0;
    if (!transientLights_.empty()) {
        constexpr float kTransientLightMaxDist = 150.0f;   // m: beyond it a light covers a handful of pixels
        const core::Vec3 eye = player_.controller().cameraPos();
        const core::Vec3 fwd = core::forwardFromYawPitch(player_.controller().viewYaw(), player_.controller().camPitch());
        const float halfX = 0.5f * player_.controller().fovXDeg() * core::PI / 180.0f;
        const float half = std::min(std::atan(std::tan(halfX) * 1.41421356f) + 5.0f * core::PI / 180.0f, 1.55f);
        const float sinA = std::sin(half), cosA = std::cos(half);
        static const float kScale = [] { const char* e = std::getenv("WFC_TLIGHTSCALE"); const float v = e ? (float)std::atof(e) : 1.0f;
                                         return std::isfinite(v) && v >= 0.0f ? v : 1.0f; }();   // tune key: brightness x (default 1 = authored)
        for (const LiveTransientLight& l : transientLights_) {
            const float end = std::max(l.lightTime, l.rampIn);
            float k = 1.0f;
            if (l.rampIn > 0.0f && l.age < l.rampIn) k = l.age / l.rampIn;
            else if (l.age > end) k = l.rampOut > 0.0f ? std::max(0.0f, 1.0f - (l.age - end) / l.rampOut) : 0.0f;
            if (k <= 0.0f) continue;
            const core::Vec3 v = l.pos - eye;
            const float dist = core::length(v);
            if (dist > l.radius) {                                      // the eye inside the sphere: always visible
                if (dist - l.radius > kTransientLightMaxDist) continue;
                const float along = core::dot(v, fwd);
                const float axis = std::sqrt(std::max(0.0f, dist * dist - along * along));
                if (axis * cosA - along * sinA > l.radius) continue;   // signed distance to the cone surface beyond the radius
            }
            if (kScale <= 0.0f) continue;
            if (l.rtOnly && r.rayTracingMode() != 2) continue;   // RT-era effect lights: Realistic only
            r.addTransientLight(l.pos, l.color * (k * kScale), l.radius, l.falloff);
            ++tlSubmitted;
        }
    }
    // RT-era continuous effect lights (WFC_FXLIGHTS=1, Realistic only; not original lights): vehicle boost at the chassis'
    // BoostFx sockets while the boost FX run (car / truck driving, tank boost, jet flying), and the pickup factories' FX while
    // available (Pickup_FX on every factory, HealthPickup_FX / OvershieldPickup_FX on those kinds). Same view cull.
    unsigned fxContinuous = 0;
    if (fxLightsOn() && r.rayTracingMode() == 2) {
        const core::Vec3 eye = player_.controller().cameraPos();
        const core::Vec3 vf = core::forwardFromYawPitch(player_.controller().viewYaw(), player_.controller().camPitch());
        const float half = std::min(std::atan(std::tan(0.5f * player_.controller().fovXDeg() * core::PI / 180.0f) * 1.41421356f) +
                                    5.0f * core::PI / 180.0f, 1.55f);
        const float sinA = std::sin(half), cosA = std::cos(half);
        auto submit = [&](const core::Vec3& p, const float* col, float radius, float falloff) {
            const core::Vec3 v = p - eye;
            const float dist = core::length(v);
            if (dist > radius) {
                if (dist - radius > 150.0f) return;
                const float along = core::dot(v, vf);
                if (std::sqrt(std::max(0.0f, dist * dist - along * along)) * cosA - along * sinA > radius) return;
            }
            r.addTransientLight(p, core::Vec3{col[0], col[1], col[2]}, radius, falloff);
            ++fxContinuous;
        };
        auto boostFor = [&](const Character& pc) {
            if (pc.moveForm() != Form::Vehicle || pc.isTransforming()) return;
            const auto& vs = pc.vehicleState();
            const VehicleFormType vt = pc.chassis().vehicle.form;
            const bool on = vt == VehicleFormType::Tank ? vs.tankBoost : vt == VehicleFormType::Jet ? vs.flying : vs.driving;
            if (!on) return;
            for (const BoostLightDef& b : kBoostLights) {
                if (pc.chassis().id != b.chassis) continue;
                core::Mat4 bw;
                if (!pc.boneDrawWorld(b.bone, bw)) continue;   // (the drawn mesh: no per-step jump)
                submit(core::transformPoint(bw, core::Vec3{b.pos[0], b.pos[1], b.pos[2]}), b.color, b.radius, b.falloff);
            }
        };
        if (!localDead_) boostFor(player_.pawn());
        for (const MatchOpponent* o : opponents_) if (o->spawned()) boostFor(o->pawn());
        static const FxLightDef* const kPick = fxLightFor("FX_Pickups_p.FX.Pickup_FX");
        static const FxLightDef* const kHealth = fxLightFor("FX_Pickups_p.FX.HealthPickup_FX");
        static const FxLightDef* const kShield = fxLightFor("FX_Pickups_p.FX.OvershieldPickup_FX");
        for (const PickupFactory* f : pickupFactories_) {
            if (!f->available()) continue;
            const core::Vec3 p = f->position() + core::Vec3{0, 0.5f, 0};
            const FxLightDef* d = f->customEffectActive() ? (f->kind() == PickupFactory::Kind::Health ? kHealth : kShield) : kPick;
            if (d) submit(p, d->color, d->radius, d->falloff);
        }
    }
    // Vehicle headlights (Rendering plan v3 row 3; INVENTED PLACEMENT, no original equivalent): Realistic only (RT mode 2), every
    // pawn in vehicle form (not mid-transform), one spot per lamp of kHeadlights following the body bone, behind the same view
    // cull as the transient lights. WFC_HEADLIGHTS=0 off. Spot shape: Rendering's first pass (18 / 32 deg, 25 m, falloff 2).
    unsigned hlSubmitted = 0;
    fxRtMode_ = r.rayTracingMode();   // effect lights spawned before the next draw follow this frame's mode
    static const bool kHeadlightsOn = [] { const char* e = std::getenv("WFC_HEADLIGHTS"); return !e || std::string(e) != "0"; }();
    if (fxRtMode_ == 2) {             // verify_slim's RT gate requires both on in a default Realistic run (package 6 default flip)
        static bool said = false;
        if (!said) { said = true; LOG_INFO("RT-era lights (first Realistic frame): effect lights %s, headlights %s", fxLightsOn() ? "on" : "off",
                                           kHeadlightsOn ? "on" : "off"); }
    }
    if (kHeadlightsOn && r.rayTracingMode() == 2) {
        constexpr float kRadius = 25.0f, kFalloff = 2.0f, kMaxDist = 150.0f;
        static const float kCosIn = std::cos(18.0f * core::PI / 180.0f), kCosOut = std::cos(32.0f * core::PI / 180.0f);
        const core::Vec3 eye = player_.controller().cameraPos();
        const core::Vec3 vf = core::forwardFromYawPitch(player_.controller().viewYaw(), player_.controller().camPitch());
        const float half = std::min(std::atan(std::tan(0.5f * player_.controller().fovXDeg() * core::PI / 180.0f) * 1.41421356f) +
                                    5.0f * core::PI / 180.0f, 1.55f);
        const float sinA = std::sin(half), cosA = std::cos(half);
        auto lampsFor = [&](const Character& pc) {
            if (pc.moveForm() != Form::Vehicle || pc.isTransforming()) return;
            const std::string& id = pc.chassis().id;
            for (const HeadlightDef& h : kHeadlights) {
                if (id != h.chassis) continue;
                core::Mat4 bw;
                if (!pc.boneDrawWorld(h.bone, bw)) return;   // (the drawn mesh: no per-step jump)
                const core::Vec3 p = core::transformPoint(bw, core::Vec3{h.pos[0], h.pos[1], h.pos[2]});
                core::Vec3 d{bw.m[0] * h.dir[0] + bw.m[4] * h.dir[1] + bw.m[8] * h.dir[2],
                             bw.m[1] * h.dir[0] + bw.m[5] * h.dir[1] + bw.m[9] * h.dir[2],
                             bw.m[2] * h.dir[0] + bw.m[6] * h.dir[1] + bw.m[10] * h.dir[2]};
                if (core::length(d) < 1e-6f) continue;
                d = core::normalize(d);
                const core::Vec3 v = p - eye;
                const float dist = core::length(v);
                if (dist > kRadius) {
                    if (dist - kRadius > kMaxDist) continue;
                    const float along = core::dot(v, vf);
                    if (std::sqrt(std::max(0.0f, dist * dist - along * along)) * cosA - along * sinA > kRadius) continue;
                }
                r.addTransientSpot(p, d, core::Vec3{h.color[0], h.color[1], h.color[2]}, kRadius, kFalloff, kCosIn, kCosOut);
                ++hlSubmitted;
            }
        };
        if (!localDead_) lampsFor(player_.pawn());
        for (const MatchOpponent* o : opponents_) if (o->spawned()) lampsFor(o->pawn());
    }
    if (std::getenv("WFC_MUZZLELOG")) {   // headlight spots per 600 draws (diagnostic)
        static unsigned hf = 0, hs = 0;
        hs += hlSubmitted;
        if (++hf % 600 == 0) { LOG_INFO("HEADLIGHTS %u spots submitted in the last 600 draws (RT mode %d)", hs, r.rayTracingMode()); hs = 0; }
    }
    if (std::getenv("WFC_MUZZLELOG")) {   // live / submitted lights per 600 draws (diagnostic)
        static unsigned frames = 0, lit = 0, live = 0, sub = 0;
        live += (unsigned)transientLights_.size(); sub += tlSubmitted; lit += transientLights_.empty() ? 0u : 1u;
        static unsigned cont = 0;
        cont += fxContinuous;
        if (++frames % 600 == 0) { LOG_INFO("MUZZLE LIGHTS %u of the last 600 draws had lights, %u live, %u submitted after the view cull; %u continuous effect lights (WFC_FXLIGHTS, Realistic)", lit, live, sub, cont); lit = live = sub = cont = 0; }
    }
    { sysprof::Scope sp(sysprof::DrawFx); fx_.draw(r); }
    if (!localPlayerDead()) { sysprof::Scope sp(sysprof::DrawVfx); vehicleFx_.draw(r); }
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

    drawQaBotOverlay(r);   // DEV / QA TOOLING (off unless the panel turns it on)
    // Debug overlay (Button::Debug, F11 on PC): world bounds, player capsule, aim ray, weapon socket.
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
    static const bool debugCam = std::getenv("WFC_DEBUGCAM") != nullptr;
    if (debugCam) {
        core::Vec3 pp = player_.pawn().position();
        r.drawBox(pp + core::Vec3{0, 10, 0}, core::Vec3{0.6f, 20.0f, 0.6f}, core::Vec3{1.0f, 0.1f, 0.9f});
        r.drawBox(pp + core::Vec3{0, 0.1f, 0}, core::Vec3{2.0f, 0.2f, 2.0f}, core::Vec3{0.1f, 1.0f, 0.2f});
    }
}

// Rendering's IRenderer::releaseMeshCaches / releaseTexture (integration 09c), compile-time detected.
template <class R> auto rmReleaseMesh(R& r, const render::MeshData& md, int) -> decltype(r.releaseMeshCaches(md), void()) { r.releaseMeshCaches(md); }
template <class R> void rmReleaseMesh(R&, const render::MeshData&, long) {}
template <class R> auto rmReleaseTex(R& r, render::TextureHandle h, int) -> decltype(r.releaseTexture(h), void()) { r.releaseTexture(h); }
template <class R> void rmReleaseTex(R&, render::TextureHandle, long) {}

void World::releaseModelGpu(const assets::SkinnedModel& m) {
    Character::forgetRig(m);   // a rig cached by this model's address must not serve a later model at the same address
    if (renderer_ && m.gpuBind.mesh) rmReleaseMesh(*renderer_, *m.gpuBind.mesh, 0);
}

// Frees the session caches' entries the new match did not load or reuse (launchMatch, after the roster loaded; no draw or step is
// running). Kept besides what the roster stamped: the local pawn's body and its inventory's weapons. The ability-actor meshes and the
// boot weapon are members, not cache entries. A chassis that returns later reloads under that match's load. Load scheduling only.
void World::evictUnusedAssets() {
    if (chassisJob_) finishChassisJob();   // a background body load completes first (its job reads cached animation files)
    if (!localChassis_.empty()) chassisAssets(localChassis_);
    {   // every participant still present (bots were just added; any non-bot participant keeps its body and weapons)
        for (size_t i = 0; i < match_.players().size(); ++i) {
            const CharacterSelection& sel = match_.players()[i].selection;
            const ChassisAssets* ca = chassisAssets(resolveChassis(sel, match_.faction((int)i)));
            if (sel.type == 0 && !sel.weapons.empty()) preloadHeldWeaponModels(sel.weapons);
            else if (ca) preloadHeldWeaponModels(ca->def.iconicWeapons);
        }
    }
    for (const Weapon& w : player_.pawn().inventory()) if (w.def) weaponUse_[w.def->id] = cacheGen_;
    if (player_.pawn().hasWeapon() && player_.pawn().weapon().def) weaponUse_[player_.pawn().weapon().def->id] = cacheGen_;
    size_t nc = 0, nw = 0, na = 0, nt = 0;
    for (auto it = chassisCache_.begin(); it != chassisCache_.end();) {
        if (it->second->lastUse == cacheGen_) { ++it; continue; }
        for (const assets::SkinnedModel* m : {&it->second->robot, &it->second->vehicle, &it->second->arm}) releaseModelGpu(*m);
        it = chassisCache_.erase(it); ++nc;
    }
    for (auto it = weaponModels_.begin(); it != weaponModels_.end();) {
        auto u = weaponUse_.find(it->first);
        if (u != weaponUse_.end() && u->second == cacheGen_) { ++it; continue; }
        if (it->second) releaseModelGpu(*it->second);
        const std::string id = it->first;
        it = weaponModels_.erase(it); weaponUse_.erase(id); ++nw;
        preloadedDefs_.erase(std::remove_if(preloadedDefs_.begin(), preloadedDefs_.end(), [&](const WeaponDef* d) { return d && id == d->id; }), preloadedDefs_.end());
    }
    if (nw) preloadedSelection_.clear();
    for (auto it = animFiles_.begin(); it != animFiles_.end();) {
        if (it->second->lastUse == cacheGen_) { ++it; continue; }
        it = animFiles_.erase(it); ++na;
    }
    // Textures: kept when pinned (map) or used by a model still cached / owned; the rest are released.
    std::set<render::TextureHandle> used;
    auto useModel = [&](const assets::SkinnedModel& m) { for (const render::Material& M : m.mats) { used.insert(M.tex); used.insert(M.emissiveTexHandle); } };
    for (const auto& kv : chassisCache_) { useModel(kv.second->robot); useModel(kv.second->vehicle); useModel(kv.second->arm); }
    for (const auto& kv : weaponModels_) if (kv.second) useModel(*kv.second);
    useModel(barrierModel_); useModel(sentryModel_); useModel(weaponModel_); useModel(mineModel_);
    for (auto it = texCache_.begin(); it != texCache_.end();) {
        if (pinnedTex_.count(it->first) || used.count(it->second)) { ++it; continue; }
        if (renderer_ && it->second != render::kInvalidTexture) rmReleaseTex(*renderer_, it->second, 0);
        it = texCache_.erase(it); ++nt;
    }
    LOG_INFO("assets: match load evicted %zu chassis, %zu weapon models, %zu anim files, %zu textures (kept %zu / %zu / %zu / %zu)",
             nc, nw, na, nt, chassisCache_.size(), weaponModels_.size(), animFiles_.size(), texCache_.size());
}

// Rendering's IRenderer::prewarmSkinnedMesh (agents/rendering): builds the GPU-skin model (vertex buffers, per-joint bounds hulls) for
// the bind mesh Character later draws, during the match load instead of at first sight (5-25 ms each). Compile-time detected; the
// same MeshData object (SkinnedModel::gpuBind via bindMeshOf), so the renderer's cache key matches. WFC_NOSKINPREWARM=1: off (A/B).
template <class R> auto rmSkinPrewarm(R& r, const render::MeshData& md, const std::vector<uint16_t>& j, const std::vector<float>& w, int)
    -> decltype(r.prewarmSkinnedMesh(md, j, w), void()) { r.prewarmSkinnedMesh(md, j, w); }
template <class R> void rmSkinPrewarm(R&, const render::MeshData&, const std::vector<uint16_t>&, const std::vector<float>&, long) {}

void World::prewarmSkin(const assets::SkinnedModel& m) {
    static const bool off = std::getenv("WFC_NOSKINPREWARM") != nullptr;
    if (off || !renderer_ || !onMainThread() || !m.valid() || m.joints.empty() || m.weights.empty()) return;
    render::MeshData& bind = bindMeshOf(m);
    syncMats(bind.mats, m.mats);   // resolved texture handles, as the draw passes them
    rmSkinPrewarm(*renderer_, bind, m.joints, m.weights, 0);
}

render::TextureHandle World::resolveTexture(const std::string& uri) {
    if (uri.empty() || !renderer_) return render::kInvalidTexture;
    auto it = texCache_.find(uri);
    if (it != texCache_.end()) return it->second;
    render::ImageData img;
    render::TextureHandle th = render::kInvalidTexture;
    const auto td0 = std::chrono::steady_clock::now();
    const bool decoded = platform::decodeImage(uri, img);
    const auto td1 = std::chrono::steady_clock::now();
    core::addLoadTime("texture decode (main-thread wait)", std::chrono::duration<double, std::milli>(td1 - td0).count());
    if (decoded) {
        th = renderer_->uploadTexture(img); ++texLoaded_;
        core::addLoadTime("texture upload", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - td1).count());
    }
    else ++texFailed_;
    texCache_[uri] = th;
    core::loadYield("World: texture");   // [integration M07] the loading screen presents between texture uploads
    return th;
}

void World::prefetchMaterialTextures(const std::vector<render::Material>& mats) {   // [Systems] decode ahead on worker threads
    if (!renderer_) return;
    std::vector<std::string> uris;
    for (const render::Material& M : mats)
        for (const std::string* u : {&M.baseColorUri, &M.emissiveUri})
            if (!u->empty() && !texCache_.count(*u)) uris.push_back(*u);
    platform::prefetchImages(uris);
}

void World::resolveModelTextures(assets::SkinnedModel& m) {
    prefetchMaterialTextures(m.mats);
    for (render::Material& M : m.mats) {
        M.tex = resolveTexture(M.baseColorUri);
        M.emissiveTexHandle = resolveTexture(M.emissiveUri);
    }
}

namespace {
void logAnimHash(const std::string& path, const assets::AnimFile& f);   // (below, with the animation file cache)
// TEST ONLY (WFC_CHASSISHASH=1, the async body load's identity check): FNV-1a over a chassis' CPU-built data - every form's
// vertices / skin / indices / submeshes, material texture URIs and colours, skeleton, clips, bounds, the def's hull - logged per
// chassis by the path that built it (synchronous or background).
uint64_t fnvMix(uint64_t h, const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } return h; }
template <class T> uint64_t fnvVec(uint64_t h, const std::vector<T>& v) { const size_t n = v.size(); h = fnvMix(h, &n, sizeof n); return v.empty() ? h : fnvMix(h, v.data(), v.size() * sizeof(T)); }
uint64_t fnvStr(uint64_t h, const std::string& s) { return fnvMix(h, s.c_str(), s.size() + 1); }
uint64_t hashSkinned(uint64_t h, const assets::SkinnedModel& m) {
    h = fnvVec(h, m.positions); h = fnvVec(h, m.normals); h = fnvVec(h, m.tangents); h = fnvVec(h, m.uv); h = fnvVec(h, m.uv1);
    h = fnvVec(h, m.joints); h = fnvVec(h, m.weights); h = fnvVec(h, m.indices);
    for (const render::SubMesh& sm : m.subs) { h = fnvMix(h, &sm.indexOffset, sizeof sm.indexOffset); h = fnvMix(h, &sm.indexCount, sizeof sm.indexCount); h = fnvMix(h, &sm.material, sizeof sm.material); }
    for (const render::Material& mt : m.mats) {
        for (const std::string* u : {&mt.baseColorUri, &mt.emissiveUri, &mt.normalUri, &mt.specularUri, &mt.wfcName, &mt.sourceName}) h = fnvStr(h, *u);
        h = fnvMix(h, &mt.color, sizeof mt.color);
    }
    for (const std::string& n : m.nodeNames) h = fnvStr(h, n);
    h = fnvVec(h, m.roots); h = fnvVec(h, m.skinJoints); h = fnvVec(h, m.invBind);
    for (const assets::AnimClip& c : m.clips) {
        h = fnvStr(h, c.name); h = fnvMix(h, &c.duration, sizeof c.duration);
        for (const assets::AnimSampler& sp : c.samplers) { h = fnvVec(h, sp.times); h = fnvVec(h, sp.values); const int k[2] = {(int)sp.interp, sp.comps}; h = fnvMix(h, k, sizeof k); }
        for (const assets::AnimChannel& ch : c.channels) { const int v[3] = {ch.sampler, ch.node, (int)ch.path}; h = fnvMix(h, v, sizeof v); }
    }
    h = fnvMix(h, &m.boundsMin, sizeof m.boundsMin); h = fnvMix(h, &m.boundsMax, sizeof m.boundsMax);
    return h;
}
}  // namespace

// The CPU part of a chassis load - no renderer, no World cache write: the chassis def, the robot body (the shared animation sets
// through `robotShared`, else robot.glb), the vehicle body, the mesh hull, the first-person arm. Runs on the main thread (the
// synchronous path) or on a background thread (startChassisJob) with the same code, so both build the same bytes.
void World::buildChassisCpu(const std::string& id, ChassisAssets& a_, const std::function<bool(const ChassisDef&, assets::SkinnedModel&)>& robotShared,
                            double* chProf) const {
    ChassisAssets* a = &a_;
    const std::string root = assetRoot();
    const std::string ext = root + "/../";
    if (!loadChassisDef(root, id, a->def)) {
        a->error = a->def.loadError;
    } else if ((chProf[0] = profNowMs(), !(robotShared(a->def, a->robot) || assets::loadSkinnedGlb(ext + a->def.robotGlb, a->robot))) || (chProf[1] = profNowMs(), !a->robot.valid())) {
        a->error = "robot.glb failed to load for " + id;
    } else if (a->def.vehicleGlb.empty() && (a->def.flyerNoTransform = true, !assets::loadSkinnedGlb(ext + a->def.robotGlb, a->vehicle))) {
        a->error = "robot.glb (as the vehicle stand-in) failed to load for " + id;   // no vehicle form (the Spider): never transforms
    } else if (!a->def.vehicleGlb.empty() && (!assets::loadSkinnedGlb(ext + a->def.vehicleGlb, a->vehicle) || (chProf[2] = profNowMs(), !a->vehicle.valid()))) {
        a->error = "vehicle.glb failed to load for " + id;
    } else {
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
            a->hasArm = true;
        }
        a->ok = true;
    }
}

// The main-thread part: texture resolution (upload; the decodes may already be done by the prefetch pool) in the synchronous
// path's order - robot, vehicle, arm - then the renderer prewarm and the cache entry.
// The main-thread presentation part of a loaded body: its textures (upload) and the presentation data its first death / hit / shot
// needs (below). finishChassis runs it at once on the main thread; a body finished off it (chassisAssets reached from the step's
// background part, Gameplay 2026-10-24) is cached GPU-pending and completes at the join (completeDeferredChassis) - the sim data is
// the same either way, only the uploads wait for the main thread.
void World::chassisGpu(const std::string& id, ChassisAssets& a_) {
    ChassisAssets* a = &a_;
    {
        resolveModelTextures(a->robot);
        resolveModelTextures(a->vehicle);
        if (a->hasArm) resolveModelTextures(a->arm);
        // [Systems, 300 FPS pass] the body's presentation data with it, so its first death / hit / shot does not load them inside the
        // sim step: the voice / death / hit-reaction cue set (a first melee death loaded it in AbilityAudio::pawnDeath: 5.7-6.9 ms,
        // Gameplay 2026-10-24) and the energon colour (chassisEnergonDefault parses character.json on first use: 2.4 ms in a first
        // shot's spawnMuzzleLight). Same functions, same values, earlier; loadCues skips the cues already loaded.
        // WFC_PRESENTPRELOAD=0: the old first-use loads (A/B)
        static const bool preload = [] { const char* e = std::getenv("WFC_PRESENTPRELOAD"); return !(e && e[0] == '0'); }();
        if (preload) {
            if (const CharacterAudioProfile* ap = CharacterAudio::find(id)) CharacterAudio::loadCues(cues_, *ap);
            float energon[3];
            (void)chassisEnergonDefault(id, energon);
        }
    }
}

// Rendering M53: compile the programs / upload the textures of every form's materials now, so the first robot -> vehicle transform
// does not pay for them in one frame (measured 65-166 ms hitch). Once per chassis (cached; bots share it). Main thread.
void World::chassisPrewarm(const ChassisAssets& a) {
    if (!renderer_ || !a.ok) return;
    for (const assets::SkinnedModel* m : {&a.robot, &a.vehicle, a.hasArm ? &a.arm : nullptr}) {
        if (!m || !m->valid()) continue;
        render::MeshData md; md.subs = m->subs; md.mats = m->mats;
        fxPrewarm(*renderer_, md, 0);
        prewarmSkin(*m);   // the GPU-skin model too (robot, vehicle form, arm): no build at first sight / first transform
    }
}

void World::completeDeferredChassis() {   // the join (main thread): the GPU-pending bodies' uploads, prewarm, presentation data
    std::vector<std::string> ids; ids.swap(deferredChassisGpu_);
    for (const std::string& id : ids) {
        auto it = chassisCache_.find(id);
        if (it == chassisCache_.end() || !it->second->gpuPending) continue;
        chassisGpu(id, *it->second);
        chassisPrewarm(*it->second);
        it->second->gpuPending = false;
        LOG_INFO("chassis %s: GPU part completed on the main thread (finished off it)", id.c_str());
    }
}

const World::ChassisAssets* World::finishChassis(const std::string& id, std::unique_ptr<ChassisAssets> a, double* chProf, const char* how) {
    const bool main = onMainThread();
    if (a->ok && main) chassisGpu(id, *a);
    chProf[4] = profNowMs();
    if (a->ok) LOG_INFO("chassis %s (%s): robot %zu clips, vehicle %zu clips, arm %s", id.c_str(), a->def.iconic.c_str(),
                        a->robot.clips.size(), a->vehicle.clips.size(), a->hasArm ? "yes" : "no");
    else LOG_ERROR("chassis %s UNAVAILABLE: %s", id.c_str(), a->error.c_str());
    static const bool hashLog = std::getenv("WFC_CHASSISHASH") != nullptr;
    if (hashLog && a->ok) {
        uint64_t h = 1469598103934665603ULL;
        h = hashSkinned(h, a->robot); h = hashSkinned(h, a->vehicle); if (a->hasArm) h = hashSkinned(h, a->arm);
        const VehicleParams& V = a->def.vehicle;
        const float hull[5] = {V.hullFront, V.hullBack, V.hullHalfWidth, V.hullBottom, V.hullTop};
        h = fnvMix(h, hull, sizeof hull);
        LOG_INFO("CHASSISHASH %016llx %s (%s)", (unsigned long long)h, id.c_str(), how);
    }
    ChassisAssets* raw = a.get();
    if (main) chassisPrewarm(*a);
    else if (a->ok) { a->gpuPending = true; deferredChassisGpu_.push_back(id); }   // the uploads wait for the join
    if (spawnProf() && chProf[0] > 0.0) LOG_INFO("SPAWNPROF chassis %s split: robot glb %.0f ms, vehicle glb %.0f, arm %.0f, textures %.0f, prewarm %.0f",
                                         id.c_str(), chProf[1] - chProf[0], chProf[2] - chProf[1], chProf[3] - chProf[2], chProf[4] - chProf[3], profNowMs() - chProf[4]);
    a->lastUse = cacheGen_; ++chassisLoads_;
    chassisCache_[id] = std::move(a);
    return raw;
}

// [Systems, the 300 FPS pass] A first-time body load mid-match (a late participant, a class change, an Escalation wave's new enemy:
// ~130-165 ms on the main thread) starts as a background job: buildChassisCpu on a worker thread with its own animation files
// (the cached ones by pointer - nothing evicts while a job runs) and the PNG decodes queued to the prefetch pool; a later tick
// finishes it (finishChassis: uploads + prewarm on the main thread). chassisAssets(id) of a body still loading waits for that job,
// so a spawn gets the same data whenever it happens. WFC_CHASSISASYNC=0: the synchronous path (A/B).
void World::startChassisJob(const std::string& id) {
    auto job = std::make_unique<ChassisJob>();
    job->id = id;
    job->a = std::make_unique<ChassisAssets>();
    for (const auto& kv : animFiles_) job->known[kv.first] = kv.second.get();
    for (const auto& kv : texCache_) job->cachedUris.insert(kv.first);
    ChassisJob* j = job.get();
    job->done = std::async(std::launch::async, [this, j] {
        auto lookup = [j](const std::string& path) -> const SharedAnimFile* {
            auto k = j->known.find(path);
            if (k != j->known.end()) { j->used.insert(path); return k->second; }
            auto& f = j->fresh[path];
            if (!f) {
                f = std::make_unique<SharedAnimFile>();
                f->ok = assets::loadAnimationFile(path, f->file);
                for (size_t i = 0; i < f->file.clips.size(); ++i) f->byName.emplace(f->file.clips[i].name, i);   // first clip of a name
                logAnimHash(path, f->file);
            }
            return f.get();
        };
        buildChassisCpu(j->id, *j->a, [this, &lookup](const ChassisDef& d, assets::SkinnedModel& m) { return loadRobotSharedWith(d, m, lookup, false); }, j->prof);
        if (j->a->ok) {   // the bodies' PNG decodes on the prefetch pool (resolveTexture collects them on the main thread)
            std::vector<std::string> uris;
            for (const assets::SkinnedModel* m : {&j->a->robot, &j->a->vehicle, j->a->hasArm ? &j->a->arm : nullptr})
                if (m) for (const render::Material& M : m->mats)
                    for (const std::string* u : {&M.baseColorUri, &M.emissiveUri}) if (!u->empty() && !j->cachedUris.count(*u)) uris.push_back(*u);
            platform::prefetchImages(uris);
        }
    });
    LOG_INFO("chassis %s: loading in the background", id.c_str());
    chassisJob_ = std::move(job);
}

// Threads (Gameplay / Integration review 2026-10-24):
//  - the job body (startChassisJob's std::async lambda) runs on its own worker thread. It writes only the job's own fields (a, fresh,
//    used); it reads the cached animation files by the pointers snapshotted at the start (nothing evicts while a job is pending:
//    evictUnusedAssets finishes it first) and no other World state; no GL / renderer call; no sim RNG (the loaders draw none); the
//    PNG decodes go to the thread-safe prefetch pool; logging is thread-safe; rosterPackage's first init is a magic static.
//  - finishChassisJob / finishChassis run on the thread that calls chassisAssets() or the warm-up: the main thread, or the step's
//    background part when a spawn there needs the body (that part owns World state while it runs, as the synchronous load always
//    did). Off the main thread they touch no GL: the body is cached GPU-pending and completeDeferredChassis does its textures /
//    prewarm / presentation data at the join. The cue preload's std::rand draws (SoundCues::ensurePick) shift presentation-only
//    random picks (accepted, Integration 2026-10-24); the sim never reads std::rand.
const World::ChassisAssets* World::finishChassisJob() {
    if (!chassisJob_) return nullptr;
    std::unique_ptr<ChassisJob> j = std::move(chassisJob_);
    j->done.wait();
    for (auto& kv : j->fresh)   // the job's own animation files join the cache (a file cached meanwhile keeps its entry)
        if (!animFiles_.count(kv.first)) { kv.second->lastUse = cacheGen_; animFiles_[kv.first] = std::move(kv.second); }
    for (const std::string& p : j->used) { auto it = animFiles_.find(p); if (it != animFiles_.end()) it->second->lastUse = cacheGen_; }
    return finishChassis(j->id, std::move(j->a), j->prof, "background");
}

bool World::chassisJobReady() const {
    return chassisJob_ && chassisJob_->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

const World::ChassisAssets* World::chassisAssets(const std::string& id) {
    auto it = chassisCache_.find(id);
    if (it != chassisCache_.end()) { it->second->lastUse = cacheGen_; return it->second.get(); }
    if (chassisJob_ && chassisJob_->id == id) return finishChassisJob();   // loading in the background: wait for that job
    struct ProfScope { const std::string& id; double t0 = profNowMs(); ~ProfScope() { if (spawnProf()) LOG_INFO("SPAWNPROF chassis load %s: %.1f ms", id.c_str(), profNowMs() - t0); } } profScope{id};
    auto a = std::make_unique<ChassisAssets>();
    double chProf[6] = {0, 0, 0, 0, 0, 0};   // SPAWNPROF split: robot glb, vehicle glb, arm, textures, prewarm
    buildChassisCpu(id, *a, [this](const ChassisDef& d, assets::SkinnedModel& m) { return loadRobotShared(d, m); }, chProf);
    return finishChassis(id, std::move(a), chProf, "synchronous");
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
    // [integration M07] Systems' per-character audio (footsteps / body / vehicle sets) follows the spawned body; the
    // profiles are keyed by the roster chassis id (Systems e37c489 handoff: "Gameplay sets the real chassis").
    if (id != localChassis_) setPlayerCharacterAudio(id);
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
        if (&pc == &player_.pawn()) {                  // [Systems M08r] the LOCAL player's loadout only (an opponent's spawn
            preloadWeaponAudio(cls);                   // overwrote it and decoded its weapon cues on the spawn frame: 67-79 ms)
            setPlayerVehicleWeaponAudio(vehCls);
        }
    }
    pc.setAbilities(testAbilities((sel && sel->type == 0 && !sel->abilities.empty()) ? sel->abilities : d.iconicAbilities));
    pc.respawnReset();
    if (mp) { mp->specialty = spec; mp->healthMax = pc.health().max; }
    return refused;
}

bool World::chassisEnergonDefault(const std::string& chassis, float out[3]) const {
    // [integration 09a] The robot mesh material's authored EnergonColor default (AssetTools energon_default, RE b0d9b22),
    // per chassis, read once. Missing: logged loudly (the FX then draws white, a visible defect, not a guess).
    static std::map<std::string, std::array<float, 4>> cache;   // [3] = 1 when found
    auto it = cache.find(chassis);
    if (it == cache.end()) {
        std::array<float, 4> v{0, 0, 0, 0};
        std::string txt;
        assets::Json j;
        // an Escalation horde body ("ESC_<id>") lives in AssetTools out/escalation_characters/<id>/ (loadChassisDef's rule)
        const std::string cj = chassis.rfind("CMP_", 0) == 0 ? assetRoot() + "/../../AssetTools/out/campaign_characters/" + chassis.substr(4) + "/character.json"
                             : chassis.rfind("ESC_", 0) == 0 ? assetRoot() + "/../../AssetTools/out/escalation_characters/" + chassis.substr(4) + "/character.json"
                                                         : assetRoot() + "/Characters/" + chassis + "/character.json";
        if (!chassis.empty() && readTextFile(cj, txt) && assets::Json::parse(txt, j) &&
            j.has("energon_default")) {
            const assets::Json& e = j["energon_default"];
            v = {e["R"].asFloat(), e["G"].asFloat(), e["B"].asFloat(), 1.0f};
        } else LOG_ERROR("chassis %s: energon_default missing in its character.json export (vehicle FX colour unset)", chassis.c_str());
        it = cache.emplace(chassis, v).first;
    }
    if (it->second[3] == 0.0f) return false;
    for (int k = 0; k < 3; ++k) out[k] = it->second[k];
    return true;
}

// Kamikaze-mine fallback ONLY (RE b0d9b22: the team EnergonColor class defaults are used for a mine with no instigator
// pawn, never for bodies or vehicle FX).
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

void World::loadForcedDirVolumes(const std::string& path) {
    forcedDirs_.clear();
    if (std::getenv("WFC_NOFORCEDDIR")) { LOG_INFO("mapstate: TnForcedDirVolume off (WFC_NOFORCEDDIR: solid brushes, no push)"); return; }
    std::string text;
    if (!readTextFile(path, text)) return;
    assets::Json ph;
    if (!assets::Json::parse(text, ph)) return;
    for (size_t v = 0; v < ph["volumes"].size(); ++v) {
        const assets::Json& vol = ph["volumes"][v];
        if (vol["class"].asString() != "TnForcedDirVolume") continue;
        ForcedDirVolume f;
        // CDO Default__TnForcedDirVolume [CONF RE]: Affects{Cars,Tanks,Planes} true, OnlyAffectsPlayers false, bBlockPawns false,
        // ExitSpeed 1100, ArrowDirection (0, 0, 0); placements author the rest.
        const assets::Json& a = vol["authored"];
        if (a.has("bBlockPawns")) f.blockPawns = a["bBlockPawns"].asBool();
        if (a.has("AffectsCars")) f.cars = a["AffectsCars"].asBool();
        if (a.has("AffectsTanks")) f.tanks = a["AffectsTanks"].asBool();
        if (a.has("AffectsPlanes")) f.planes_ = a["AffectsPlanes"].asBool();
        if (a.has("OnlyAffectsPlayers")) f.onlyPlayers = a["OnlyAffectsPlayers"].asBool();
        f.exitSpeed = (a.has("ExitSpeed") ? a["ExitSpeed"].asFloat() : 1100.0f) * 0.01f;   // UU/s -> m/s
        if (a.has("ArrowDirection")) {   // UE (X, Y, Z) -> glTF (X, Z, Y)
            const assets::Json& d = a["ArrowDirection"];
            f.dir = {d["X"].asFloat(), d["Z"].asFloat(), d["Y"].asFloat()};
        }
        std::vector<std::vector<core::Vec3>> polys; core::Vec3 c{0, 0, 0}; int cn = 0;
        for (size_t p = 0; p < vol["polygons_gltf"].size(); ++p) {
            std::vector<core::Vec3> poly;
            for (size_t k = 0; k < vol["polygons_gltf"][p].size(); ++k) {
                const assets::Json& q = vol["polygons_gltf"][p][k];
                poly.push_back({q[0].asFloat(), q[1].asFloat(), q[2].asFloat()}); c = c + poly.back(); ++cn;
            }
            polys.push_back(poly);
        }
        if (cn) c = c * (1.0f / cn);
        for (const auto& poly : polys) {
            if (poly.size() < 3) continue;
            core::Vec3 nrm = core::cross(poly[1] - poly[0], poly[2] - poly[0]);
            const float l = core::length(nrm);
            if (l < 1e-6f) continue;
            nrm = nrm * (1.0f / l);
            if (core::dot(nrm, c - poly[0]) > 0.0f) nrm = nrm * -1.0f;
            f.planes.push_back({nrm, core::dot(nrm, poly[0])});
        }
        if (!f.planes.empty() && forcedDirs_.size() < 32) forcedDirs_.push_back(f);
    }
    if (!forcedDirs_.empty()) LOG_INFO("mapstate: %zu TnForcedDirVolumes", forcedDirs_.size());
}

namespace {
// The pawn's collision cylinder against a convex brush: centre and half extents (radius across, half height up).
float brushPenetration(const std::vector<std::pair<core::Vec3, float>>& planes, const core::Vec3& centre, float radius, float halfH,
                       core::Vec3* outN) {
    float least = 1e9f;
    for (const auto& pl : planes) {
        const float ext = std::fabs(pl.first.y) * halfH + std::sqrt(pl.first.x * pl.first.x + pl.first.z * pl.first.z) * radius;
        const float pen = ext - (core::dot(pl.first, centre) - pl.second);   // > 0: overlapping this plane's inner side
        if (pen <= 0.0f) return 0.0f;
        if (pen < least) { least = pen; if (outN) *outN = pl.first; }
    }
    return least;
}
void pawnCylinder(const Character& c, core::Vec3& centre, float& radius, float& halfH) {
    const bool veh = c.moveForm() == Form::Vehicle;
    radius = veh ? 2.0f : 1.0f; halfH = veh ? 1.0f : 2.0f;
    centre = c.position() + core::Vec3{0, halfH, 0};
}
}

void World::forcedDirPre(Character& c, float dt, bool isPlayer) const {
    if (forcedDirs_.empty()) return;
    core::Vec3 centre; float radius, halfH; pawnCylinder(c, centre, radius, halfH);
    unsigned touch = 0; core::Vec3 override{0, 0, 0};
    const VehicleFormType vt = c.vehicleParams().form;
    for (size_t i = 0; i < forcedDirs_.size(); ++i) {
        const ForcedDirVolume& f = forcedDirs_[i];
        if (brushPenetration(f.planes, centre, radius, halfH, nullptr) <= 0.0f) continue;
        touch |= 1u << i;
        // IsAffected: VehicleType 0 / 1 (car / truck) -> AffectsCars, 2 (plane) -> AffectsPlanes, 3 (tank) -> AffectsTanks.
        const bool affected = (!f.onlyPlayers || isPlayer) &&
                              (vt == VehicleFormType::Jet ? f.planes_ : vt == VehicleFormType::Tank ? f.tanks : f.cars);
        if (!affected || dt <= 0.0f) continue;
        // TickSpecial: acc = clamp((ExitSpeed - v . Arrow) / dt, 0, 10000 UU/s^2) * Arrow.
        const float acc = std::min(100.0f, std::max(0.0f, (f.exitSpeed - core::dot(c.velocity(), f.dir)) / dt));
        if (f.blockPawns && c.moveForm() == Form::Robot) override = f.dir * acc;
        // Mesh.AddLinearAcceleration: moves a simulating body (the vehicle form); a walking robot's mesh does not simulate, so a robot
        // in a volume without bBlockPawns is not pushed.
        else if (c.moveForm() == Form::Vehicle) c.velocity() = c.velocity() + f.dir * (acc * dt);
    }
    if (touch & ~c.forcedDirTouch_)   // a volume newly touched (regression attribution: the first divergent step)
        LOG_INFO("forceddir: a pawn (%s, %s form) touches volume mask %x at (%.1f %.1f %.1f) t %.2f", isPlayer ? "player" : "participant",
                 c.moveForm() == Form::Vehicle ? "vehicle" : "robot", touch & ~c.forcedDirTouch_, c.position().x, c.position().y, c.position().z,
                 match_.matchTime());
    c.forcedDirTouch_ = touch;
    c.overrideAccel_ = override;   // UnTouch clears it
}

void World::forcedDirPost(Character& c) const {
    if (forcedDirs_.empty() || c.moveForm() != Form::Robot) return;
    // HardBlock: a robot-form pawn of an affected type is blocked by a bBlockPawns volume it is not already touching. Every
    // vehicle type is affected by the CDO default; a volume's own Affects* flags are checked like IsAffected.
    core::Vec3 centre; float radius, halfH; pawnCylinder(c, centre, radius, halfH);
    const VehicleFormType vt = c.vehicleParams().form;
    for (size_t i = 0; i < forcedDirs_.size(); ++i) {
        const ForcedDirVolume& f = forcedDirs_[i];
        if (!f.blockPawns || (c.forcedDirTouch_ & (1u << i))) continue;
        if (!(vt == VehicleFormType::Jet ? f.planes_ : vt == VehicleFormType::Tank ? f.tanks : f.cars)) continue;
        core::Vec3 n;
        const float pen = brushPenetration(f.planes, centre, radius, halfH, &n);
        if (pen <= 0.0f) continue;
        const core::Vec3 out = n * (pen + 0.01f);
        c.setPosition(c.position() + out); centre = centre + out;
        const float into = core::dot(c.velocity(), n);
        if (into < 0.0f) c.velocity() = c.velocity() - n * into;
    }
}

std::string World::mapDir() const {
    if (campaignMap_) return cpChapter_.dir + cpChapter_.persistentDir + "/";   // the persistent unit (physics.json "world": KillZ)
    return assetRoot() + "/Maps/" + mapName_ + "/";
}

std::string World::overlayed(const std::string& path) {
    static const char* root = std::getenv("WFC_OVERLAY_ROOT");
    if (!root || !*root) return path;
    std::string p = path; for (char& c : p) if (c == '\\') c = '/';
    const size_t k = p.find("ExtractedAssets/");
    if (k == std::string::npos) return path;
    const std::string o = std::string(root) + "/" + p.substr(k + 16);
    std::ifstream f(o, std::ios::binary);
    if (!f.good()) return path;
    LOG_INFO("overlay: %s", o.c_str());
    return o;
}

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
    { const std::string ch = CampaignChapter::chapterOf(m); if (!ch.empty()) return ch; }   // campaign chapters: by the chapter id
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
        {   // campaign: the unit the volume belongs to
            const std::string o = v["object"].asString();
            const size_t k = o.find(".TheWorld");
            h.unit = k == std::string::npos ? std::string() : o.substr(0, k);
            if (h.unit.size() > 2 && h.unit.compare(h.unit.size() - 2, 2, "_m") == 0) h.unit.resize(h.unit.size() - 2);
        }
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
            for (size_t k = 1; k + 1 < polys[p].size(); ++k) { h.tris.push_back(P(0)); h.tris.push_back(P(k)); h.tris.push_back(P(k + 1)); }
        }
        {   // convex: every vertex on the inner side of every face plane; else the parity test (the centroid can lie outside the solid)
            h.lo = h.hi = h.tris.empty() ? c : h.tris[0];
            for (const core::Vec3& q : h.tris) {
                h.lo = {std::min(h.lo.x, q.x), std::min(h.lo.y, q.y), std::min(h.lo.z, q.z)}; h.hi = {std::max(h.hi.x, q.x), std::max(h.hi.y, q.y), std::max(h.hi.z, q.z)};
                for (const HazardVolume::Plane& pl : h.planes) if (core::dot(pl.n, q) > pl.w + 0.05f) h.concave = true;
            }
            if (!h.concave) h.tris.clear();
        }
        hazards_.push_back(std::move(h));
    }
    LOG_INFO("map %s: %zu hazard volumes", mapName_.c_str(), hazards_.size());
}

int World::hazardAt(const core::Vec3& p) const {
    for (size_t i = 0; i < hazards_.size(); ++i) {
        const std::string& u = hazards_[i].unit;   // campaign: only while its unit is loaded (streaming)
        if (campaignMap_ && !u.empty() && u != cpChapter_.persistentDir && !cpUnitLoaded_.count(u)) continue;
        if (!hazards_[i].on) continue;   // switched off (Escalation mechanisms)
        const HazardVolume& H = hazards_[i];
        bool in = !H.planes.empty();
        if (H.concave) {   // ray-crossing parity (a fixed skewed direction: no edge-aligned rays on axis-aligned brushes)
            in = p.x >= H.lo.x && p.y >= H.lo.y && p.z >= H.lo.z && p.x <= H.hi.x && p.y <= H.hi.y && p.z <= H.hi.z;
            if (in) {
                const core::Vec3 d = core::normalize(core::Vec3{0.3137f, 0.9134f, 0.2593f});
                int crossings = 0;
                for (size_t t = 0; t + 2 < H.tris.size(); t += 3) {   // Moller-Trumbore, hits at distance > 0
                    const core::Vec3 e1 = H.tris[t + 1] - H.tris[t], e2 = H.tris[t + 2] - H.tris[t];
                    const core::Vec3 pv = core::cross(d, e2); const float det = core::dot(e1, pv);
                    if (std::fabs(det) < 1e-9f) continue;
                    const float inv = 1.0f / det; const core::Vec3 tv = p - H.tris[t];
                    const float u = core::dot(tv, pv) * inv; if (u < 0.0f || u > 1.0f) continue;
                    const core::Vec3 qv = core::cross(tv, e1); const float v = core::dot(d, qv) * inv;
                    if (v < 0.0f || u + v > 1.0f) continue;
                    if (core::dot(e2, qv) * inv > 0.0f) ++crossings;
                }
                in = (crossings & 1) != 0;
            }
        } else for (const HazardVolume::Plane& pl : H.planes) if (core::dot(pl.n, p) > pl.w) { in = false; break; }
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

core::Vec3 World::addSpread(const core::Vec3& dirIn, float S, const Weapon& w) const {
    const core::Vec3 X = core::normalize(dirIn);
    core::Vec3 Y = core::cross(X, core::Vec3{0, 1, 0});   // right
    if (core::length(Y) < 1e-5f) Y = core::Vec3{1, 0, 0};
    Y = core::normalize(Y);
    const core::Vec3 Z = core::normalize(core::cross(Y, X));   // up
    // TnWeapon.AddSpread: the weapons with a SpreadPattern (fixed pellet offsets, scaled by GetSpread / GetInitialSpread).
    static const float kShotgun[8][2] = {{0, 0}, {.025f, -.025f}, {-.02f, .02f}, {.03f, .015f}, {-.035f, -.04f}, {.013f, .04f}, {-.013f, -.03f}, {-.03f, -.015f}};
    static const float kEmp[13][2] = {{0, 0}, {.015f, -.015f}, {-.01f, .01f}, {.02f, .015f}, {-.025f, -.035f}, {-.02f, -.03f}, {-.05f, -.02f},
                                      {.035f, .02f}, {-.035f, .01f}, {.015f, -.02f}, {0, .04f}, {-.045f, -.035f}, {0, -.015f}};
    const char* id = w.def ? w.def->id : "";
    const float (*pat)[2] = nullptr; int n = 0;
    if (!std::strcmp(id, "Shotgun")) { pat = kShotgun; n = 8; }
    else if (!std::strcmp(id, "EmpShotgun")) { pat = kEmp; n = 13; }
    if (pat && pellet_ >= 0 && pellet_ < n) {
        const float M = w.spreadMin > 1e-6f ? S / w.spreadMin : 1.0f;
        return core::normalize(X + Y * (pat[pellet_][0] * M) + Z * (pat[pellet_][1] * M));
    }
    // Weapon.AddSpread (stock UE3): RandY = FRand() - 0.5; RandZ = Sqrt(0.5 - RandY^2) * (FRand() - 0.5).
    const float ry = core::simRand01() - 0.5f;
    const float rz = std::sqrt(std::max(0.0f, 0.5f - ry * ry)) * (core::simRand01() - 0.5f);
    return core::normalize(X + Y * (ry * S) + Z * (rz * S));
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
    {
        int vis = -1;
        if (w.def && w.projClass > 0) vis = projectileVisualFor((std::string(w.def->id) + "#" + std::to_string(w.projClass)).c_str());
        projectiles_.back().visual = vis >= 0 ? vis : projectileVisualFor(w.def ? w.def->id : nullptr);
    }
    projectileFxStart(projectiles_.back());
}

// Actor.HurtRadius takes its victims from VisibleCollidingActors(class'Actor', DamageRadius, HurtOrigin): a zero-extent
// TRACE_World | TRACE_StopAtAnyHit line from HurtOrigin to the victim's Location must not hit the world [HIGH stock UE3], so a
// blast does not reach through floors or walls. World = the static line-check geometry; movers and spawned barriers are not
// tested [PARTIAL]. A blast that starts on a surface (an impact point, a mine on the floor) skips a hit within its first 5 cm:
// the start surface crossed outward, which a UE3 line check does not report [PROV numerical].
bool World::radiusVisible(const core::Vec3& origin, const core::Vec3& victimLocation) const {
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    float t;
    if (!line || !line->segmentHit(origin, victimLocation, t)) return true;
    const core::Vec3 d = victimLocation - origin; const float len = core::length(d);
    const bool visible = !(len <= 0.05f || t * len >= 0.05f) && !line->segmentHit(origin + d * (0.05f / len), victimLocation, t);
    static const bool logBlocked = std::getenv("WFC_HURTVISLOG") != nullptr;   // gate diagnostics: each victim the trace removes
    if (!visible && logBlocked)
        LOG_INFO("hurtradius: blocked at t %.2f from (%.1f %.1f %.1f) to (%.1f %.1f %.1f)", match_.matchTime(), origin.x, origin.y, origin.z,
                 victimLocation.x, victimLocation.y, victimLocation.z);
    return visible;
}

void World::radiusDamage(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type) {
    struct RadiusFlag { bool& f; bool was; ~RadiusFlag() { f = was; } } radiusFlag{svRadiusHit_, svRadiusHit_};
    svRadiusHit_ = true;
    if (cp_.active) cpRadiusDes(at, damage, radius, instigator, type);   // campaign destructibles
    if (anySmart_) { std::lock_guard<std::mutex> lk(smartBlastMutex_); smartBlasts_.push_back({at, instigator, 2, match_.matchTime()}); }
    // Actor.HurtRadius -> TakeRadiusDamage: Dist = max(|Location - origin| - collision radius, 0), scale = 1 - Dist / DamageRadius
    // (bFullDamage false) [HIGH stock UE3 Actor.TakeRadiusDamage]; teammates are filtered by applyMatchDamage (projectile
    // damage types are not TnDamageTypeAOE).
    auto falloff = [&](const core::Vec3& p, float colR = 0.0f) {
        float d = std::max(core::length(p - at) - colR, 0.0f);
        return d >= radius ? 0.0f : 1.0f - d / std::max(radius, 1e-3f);
    };
    auto colR = [](const Character& c) { return c.cylinderRadius(c.moveForm()); };
    if (seatOrdered_ && matchActive_) {   // online: by match player index
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const float k = falloff(c->actorLocation(), colR(*c));
            if (k > 0.0f && radiusVisible(at, c->actorLocation())) { noteHitDir(c->actorLocation() - at); applyMatchDamage((int)q, instigator, damage * k, false, type); }
        }
    } else {
    if (matchActive_ && !localDead_) {
        float k = falloff(player_.pawn().actorLocation(), colR(player_.pawn()));
        if (k > 0.0f && radiusVisible(at, player_.pawn().actorLocation())) { noteHitDir(player_.pawn().actorLocation() - at); applyMatchDamage(localPlayer_, instigator, damage * k, false, type); }
    }
    for (MatchOpponent* o : opponents_) {
        if (!o->spawned()) continue;
        float k = falloff(o->pawn().actorLocation(), colR(o->pawn()));
        if (k > 0.0f && radiusVisible(at, o->pawn().actorLocation())) { noteHitDir(o->pawn().actorLocation() - at); applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type); }
    }
    }
    for (Destructible* d : destructibles_)
        if (d->state() == 0) {
            core::Vec3 c = (d->boxMin() + d->boxMax()) * 0.5f; float k = falloff(c);
            if (k > 0.0f && radiusVisible(at, d->position())) d->applyDamage(*this, damage * k);
        }
    for (size_t ri = 0; ri < rollers_.size(); ++ri) {
        if (!rollers_[ri].alive) continue;
        float dd = std::max(0.0f, core::length(rollers_[ri].pos - at) - 1.21f);
        if (dd < radius && radiusVisible(at, rollers_[ri].pos)) damageRollerAt(ri, damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator);
    }
    for (size_t si = 0; si < sentries_.size(); ++si) {
        if (!sentries_[si].alive) continue;
        const core::Vec3 sc = sentries_[si].pos + core::Vec3{0, 2.0f, 0};
        float dd = std::max(0.0f, core::length(sc - at) - 2.0f);
        if (dd < radius && radiusVisible(at, sc)) damageSentryAt(si, damage * (1.0f - dd / std::max(radius, 1e-3f)), instigator, type);
    }
    for (DecoyTrap& d : decoys_)   // TnDroppedPickupDefrag.TakeDamage: any non-team damage destroys it (H: Health 0)
        if (d.alive && instigator >= 0 && instigator != d.owner && !(matchActive_ && match_.settings().teamGame && match_.sameTeam(instigator, d.owner)) &&
            core::length(d.pos - at) < radius && radiusVisible(at, d.pos))
            destroyDecoyTrap(d);
    for (size_t bi = 0; bi < beacons_.size(); ++bi)
        if (beacons_[bi].alive && core::length(beacons_[bi].pos - at) < radius && radiusVisible(at, beacons_[bi].pos))
            damageAmmoBeaconAt(bi, damage * (1.0f - core::length(beacons_[bi].pos - at) / std::max(radius, 1e-3f)), instigator);
    for (size_t bi = 0; bi < barriers_.size(); ++bi) {
        const BarrierState& barrier_ = barriers_[bi];
        if (!barrier_.alive || !radiusVisible(at, barrier_.pos)) continue;
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
                if (sl > 1e-5f && seatOrdered_) {   // online: every pawn but the instigator's, by match player index
                    for (size_t q = 0; q < match_.players().size() && !hitPawn; ++q) {
                        const Character* c = (int)q == p.instigator ? nullptr : participantPawn((int)q);
                        float th;
                        if (c && MatchOpponent::pawnRayHit(*c, p.pos, seg * (1.0f / sl), sl, th)) hitPawn = true;
                    }
                } else
                if (sl > 1e-5f)
                    for (MatchOpponent* o : opponents_) {
                        float th;
                        if (o->matchPlayer() != p.instigator && o->rayHit(p.pos, seg * (1.0f / sl), sl, th)) { hitPawn = true; break; }
                    }
                auto impact = [&](const core::Vec3& n) {
                    onProjectileHitWall(p.weaponClass, p.pos, p.life > 1e8f);   // [Systems M08f] FuseSound (first) + BounceSound
                    if (p.life > 1e8f) p.life = p.fuseMin + (p.fuseMax - p.fuseMin) * (float)(core::simRandU32() % 1000) / 999.0f;
                    p.vel = (p.vel - n * (2.0f * core::dot(p.vel, n))) * p.bounce;
                    if (core::dot(p.vel, p.vel) < 500.0f * 1e-4f) { p.resting = true; p.vel = {0, 0, 0}; p.spinRate = 0.0f; }
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
            if (seatOrdered_) {   // online: every pawn but the instigator's, one test, by match player index
                for (size_t q = 0; q < match_.players().size(); ++q) {
                    const Character* c = (int)q == p.instigator ? nullptr : participantPawn((int)q);
                    float th;
                    if (c && MatchOpponent::pawnRayHit(*c, p.pos, dir, len, th) && th / len < best) { best = th / len; hit = true; worldHit = false; }
                }
            } else {
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
        }
        p.life -= dt;
        if (hit || p.life <= 0.0f || closingExpired) {
            core::Vec3 at = p.pos + d * best;
            if (hit || closingExpired) radiusDamage(at, p.damage, p.radius, p.instigator, p.damageType);
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
    if (id == "GuidedMissileStreak" && seatHuman(player) && (player != localPlayer_ || onlineSeats_)) {   // the seat's guided missile
        if (seatGuiding(player)) return "";
        mp.acquiredKillstreaks.pop_back();
        startSeatMissile(player, pc.yaw(), 0.0f);
        LOG_INFO("killstreak %s triggered (p%d)", id.c_str(), player);
        return id;
    }
    if (player != localPlayer_ && (id == "GuidedMissileStreak" || (id == "MinePooperStreak" && !seatHuman(player)))) return "";   // every human seat (bots: not yet)
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
        otherTeam([](int, Character& c) { c.applyJammed(30.0f, true); });   // ABT_OtherTeam TnBuffAbilityJammedKillstreak 30 s
    } else {
        LOG_WARN("killstreak %s triggered: effect not implemented in the rebuild [PARTIAL]", id.c_str());
    }
    LOG_INFO("killstreak %s triggered (p%d)", id.c_str(), player);
    if (player != localPlayer_) for (BotBrain& b : bots_) if (b.player == player) ++b.streaks;
    if (player == localPlayer_) onLocalKillstreakActivated(id, mp.team);   // [Systems M08n] TnKillstreakActivated* Self announcement
    else onParticipantKillstreakActivated(id, mp.team, match_.settings().teamGame && localPlayer_ >= 0 && match_.sameTeam(player, localPlayer_));   // [Systems M09i] Friendly / Enemy
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
        if (victim == self || core::length(v.actorLocation() - at) > 25.0f || !radiusVisible(at, v.actorLocation())) return;
        const core::Vec3 dir = v.actorLocation() - at;
        noteHitDir(dir);
        applyMatchDamage(victim, self, 65.0f, false, "TransGame.TnDamageTypeShockwave");
        if (core::length(dir) > 1e-4f && !(match_.settings().teamGame && match_.sameTeam(victim, self)))
            applyKnockback(victim, core::normalize(dir) * 700000.0f, "TransGame.TnDamageTypeShockwave");
    };
    if (seatOrdered_) {   // online: by match player index
        for (size_t q = 0; q < match_.players().size(); ++q) if (const Character* c = participantPawn((int)q)) hit((int)q, *c);
    } else {
    for (MatchOpponent* o : opponents_) if (o->spawned()) hit(o->matchPlayer(), o->pawn());
    if (self != localPlayer_ && !localDead_) hit(localPlayer_, player_.pawn());
    }
    for (Destructible* d : destructibles_)
        if (d->state() == 0 && core::length((d->boxMin() + d->boxMax()) * 0.5f - at) <= 25.0f && radiusVisible(at, d->position())) d->applyDamage(*this, 65.0f);
}

// Every participant's ability actors, at the start of the background part (after the local controller, before the bots): off the main
// thread (WFC_SCALETEST: ~0.13 ms / step at 64 participants). Their meshes are loaded by the local part (ensureAbilityModels).
void World::tickAbilityActors(float dt) {
    { TickTimer tt(15); tickBarrier(dt); }
    { TickTimer tt(16); tickAmmoBeacon(dt); tickDecoyTraps(dt); }
    { TickTimer tt(17); tickSentry(dt); }
    { TickTimer tt(18); tickRollerMine(dt); }
    tickBuffShots(dt);
    tickKillstreakItems(dt);
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
    } else if (fx == "DecoyTrap") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        requestDecoyTrap(localPlayer_);                   // SpawnDelay 0.5 -> SpawnInventory (TnDecoyTrap)
    } else if (fx == "SpawnAmmoCrate") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        requestAmmoBeacon(localPlayer_);                  // SpawnDelay 0.5 -> SpawnInventory
    } else if (fx == "Barrier") {
        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier
        requestBarrier(localPlayer_);                     // SpawnDelay 0.5 -> SpawnBarrier
    }
    // (every participant's ability actors - barriers, beacons, sentries, rollers, buff shots, killstreak items - tick in the background
    // part: tickAbilityActors at its start)
    if (!onlineSeats_) tickGuidedMissile(dt);   // the local player's missile (offline; online the local seat's is a seat missile)
    repairBeam_.time = std::max(0.0f, repairBeam_.time - dt);
    if (repairSquibPending_) { repairSquibDraw_ = true; repairSquibPending_ = false; }
    if (repairBeam_.time <= 0.0f) repairBeam_.active = false;
    // [Systems M08d / Gameplay 24c] TnWeaponBeam / TnWeaponRepair sound state from Gameplay's beam: teammate -> heal loop
    // (WP_Fire), enemy -> damage loop (WP_FireSecondary), no pawn -> neither; release -> WP_LoopingTail.
    {
        const bool firing = repairBeam_.active && repairBeam_.time > 0.0f;
        const int target = !firing ? 0 : repairBeam_.healing ? 1 : (repairBeam_.target >= 0 ? 2 : 0);
        if (firing || weaponAudio().beamActive())
            onBeamWeapon("TransContent.TnWeaponRepairRay", firing, target);
    }
    auto tickTD = [dt](Character& p) { p.transformDisruptRemain_ = std::max(0.0f, p.transformDisruptRemain_ - dt); };
    tickTD(pc);
    for (MatchOpponent* o : opponents_) tickTD(o->pawn());
    // TnBuffDrainSource (Blueprints[0]): each tick every enemy TnPawn within Range 2000 UU with line of sight takes
    // DamagePerSecond 25 x dt; the caster heals HealthPerSecond 35 x dt per target [CONF authored + RE §J]. Heal type
    // AddHealthToAll [PROV].
    // [Systems] DrainRay_Beam_FX: one looping beam per victim drained this step (presented in the draw pass, as the Repair Ray).
    for (auto it = drainBeams_.begin(); it != drainBeams_.end();) {
        it->second.live = false;
        if (it->second.fx < 0) it = drainBeams_.erase(it); else ++it;
    }
    if (pc.drainRemain_ > 0.0f && !onlineSeats_) {
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
                onDrainVictimTick(o->pawn().actorLocation());   // [Systems M08i] TnBuffDrainTarget.DamageSound
                {   // [Systems] the beam: victim -> the drainer's MeleeSocket_LeftHand (RE MP_ABILITY_CUES_RE efcd305)
                    DrainBeam& b = drainBeams_[o->matchPlayer()];
                    core::Mat4 hand;
                    b.start = o->pawn().actorLocation();
                    b.end = bodySocketWorld(pc, "MeleeSocket_LeftHand", hand) ? core::Vec3{hand.m[12], hand.m[13], hand.m[14]} : pc.actorLocation();
                    b.live = true;
                }
            }
        if (targets > 0 && !localDead_) pc.health().heal(Health::HealType::AddHealthToAll, 35.0f * dt * targets);
        if (!localDead_) onDrainTick(targets);          // [Systems M08i] TnBuffDrainSource.HealSound (>= 1 target)
    }
    if (!onlineSeats_) tickLocalMelee(dt);   // online: the seat ticks
    if (!onlineSeats_) tickHomingLock(dt);
    else {   // online: the HUD lock is the local seat's (applySeat ticks it on every peer)
        const SeatLock* sl = localPlayer_ >= 0 && (size_t)localPlayer_ < seatLocks_.size() ? &seatLocks_[(size_t)localPlayer_] : nullptr;
        locked_ = sl && sl->locked; lockTarget_ = sl ? sl->target : -1; lockCandidate_ = sl ? sl->candidate : -1; lockTimer_ = sl ? sl->timer : 0.0f;
    }
    grenadeCooldown_ = std::max(0.0f, grenadeCooldown_ - dt);
    if (grenadeTossDelay_ >= 0.0f) {
        grenadeTossDelay_ -= dt;
        Character& gp = player_.pawn();
        const Weapon* gb = grenadeBag(gp);
        if (grenadeTossDelay_ < 0.0f && gb && !localDead_ && gp.moveForm() == Form::Robot) {
            releaseGrenade(gp, localPlayer_, *gb, grenadeTarget_);
        }
    }
    if (pc.shockwaveDelay_ >= 0.0f && !onlineSeats_) {
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

// Hands every participant's anim windows of this step to Systems' notify dispatcher (presentation only).
void World::drainAnimNotifies() {
    static const char* logEnv = std::getenv("WFC_ANIMNOTIFYLOG");   // diagnostic
    const int logMp = !logEnv ? -2 : (logEnv[0] == 'p' ? std::atoi(logEnv + 1) : localPlayer_);
    auto drain = [this, logMp](Character& c, int mp) {
        const std::map<std::string, ChassisDef::ClipSource>& src = c.chassis().clipSources;
        for (const Character::AnimWindow& w : c.animWindows()) {
            auto it = src.find(w.clip->name);
            if (it == src.end()) continue;   // no authored source (not a chassis clip): no notifies to fire
            if (mp == logMp)
                LOG_INFO("ANIMNOTIFY %s.%s [%.3f, %.3f) w %.2f", it->second.animSet.c_str(), it->second.sequence.c_str(), w.t0, w.t1, w.weight);
            onAnimNotifyWindow(mp, it->second.animSet.c_str(), it->second.sequence.c_str(), w.t0, w.t1, false, w.weight);
        }
        c.clearAnimWindows();
    };
    drain(player_.pawn(), localPlayer_);
    for (MatchOpponent* o : opponents_) drain(o->pawn(), o->matchPlayer());
}

// [Systems] fills: fire the authored notify rows of (animSet, sequence) with time in [t0, t1). Gameplay's stub.
namespace {
// [Systems] body notify index: "animSet|sequence" -> its HmAnimNotify_PlayEffect rows (the sounds keep their CharacterAudio path), and
// "mesh|socket" -> the SkeletalMeshSocket row of a body mesh.
struct BodyNotifyIndex {
    std::unordered_map<std::string, std::vector<const AnimNotifyRow*>> effects;
    std::unordered_map<std::string, const MeshSocketRow*> sockets;
};
const BodyNotifyIndex& bodyNotifyIndex() {
    static const BodyNotifyIndex idx = [] {
        BodyNotifyIndex x;
        for (const AnimNotifyRow& r : kAnimNotifies)
            if (r.effect && !std::strncmp(r.animSet, "TR_", 3)) x.effects[std::string(r.animSet) + "|" + r.sequence].push_back(&r);
        for (const MeshSocketRow& s : kMeshSockets) x.sockets.emplace(std::string(s.mesh) + "|" + s.socket, &s);
        return x;
    }();
    return idx;
}
// "content/<pkg>/<mesh>.gltf" -> "<pkg>.<mesh>" (the MeshSockets key; as requestCorpse's death-socket key)
std::string bodyMeshKey(const std::string& g) {
    const size_t s1 = g.rfind('/'), s0 = s1 == std::string::npos || s1 == 0 ? std::string::npos : g.rfind('/', s1 - 1), dot = g.rfind('.');
    if (s0 == std::string::npos || dot == std::string::npos || dot < s1) return "";
    return g.substr(s0 + 1, s1 - s0 - 1) + "." + g.substr(s1 + 1, dot - s1 - 1);
}
}  // namespace

bool World::bodySocketWorld(const Character& c, const std::string& socket, core::Mat4& out) const {
    const bool veh = c.form() == Form::Vehicle;
    const std::string key = bodyMeshKey(veh ? c.chassis().vehicleSkelGltf : c.chassis().robotSkelGltf) + "|" + socket;
    const BodyNotifyIndex& idx = bodyNotifyIndex();
    auto it = idx.sockets.find(key);
    if (it == idx.sockets.end()) return false;   // the socket is not on this body (e.g. Bumblebee's trail sockets on another chassis)
    const MeshSocketRow& r = *it->second;
    core::Mat4 bone;
    if (!c.boneWorld(r.bone, bone)) return false;
    const float loc[3] = {r.x, r.y, r.z};
    const int rot[3] = {r.pitch, r.yaw, r.roll};
    out = bone * ueSocketToGltf(loc, rot);
    return true;
}

// [Systems] HmAnimNotify_PlayEffect on the body (authored AnimSequence.Notifies of the TR_* AnimSets, AnimNotifies.inc): the effect
// at the mesh socket, attached (flag 1) = it follows the socket. UE3 AnimNodeSequence issues notifies for a node with weight above
// its NotifyWeightThreshold (default 0) [HIGH: no per-node thresholds for effects in our trees]. A socket the body does not have
// spawns nothing (AttachComponentToSocket fails). Participants within 40 m of the local pawn, as the weapon notifies [PROV distance].
// WFC_BODYNOTIFYFX=0: off. WFC_NOTIFYLOG: one line per effect.
void World::onAnimNotifyWindow(int matchPlayer, const char* animSet, const char* sequence, float t0, float t1, bool backwards, float weight) {
    static const bool off = std::getenv("WFC_BODYNOTIFYFX") && std::getenv("WFC_BODYNOTIFYFX")[0] == '0';
    static const bool log = std::getenv("WFC_NOTIFYLOG") != nullptr;
    (void)backwards;   // nothing of ours plays backwards (Gameplay's hook)
    // [Systems] HmAnimNotify_Dialog (non-verbal dialog: hit / death reactions, melee swings, warcries) in this window: the speaker's
    // clip notifies (its audio profile, by sequence name), MinWeight gate [C: AnimSeqInstigator.NodeTotalWeight < MinWeight -> none],
    // to the level's dialog manager (DialogEvents: the group rules, then the level / robot / vehicle set's cue) at the pawn.
    static const bool dialogOff = std::getenv("WFC_NODIALOGNOTIFY") != nullptr;   // TEST: silent
    if (audio_ && !dialogOff && !levelAudio_.level().empty() && t1 > t0 && weight > 0.0f) {
        const bool me = matchPlayer == localPlayer_;
        const Character* pc = me ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(matchPlayer);
        auto pb = participantBodies_.find(matchPlayer);
        const CharacterAudioProfile* who = me ? &audioProfile() : pb != participantBodies_.end() ? &pb->second.foley.voiceProfile() : nullptr;
        const CharacterAudioProfile::Clip* clip = who ? who->clip(sequence) : nullptr;
        if (pc && clip)
            for (const CharacterAudioProfile::Notify& n : clip->notifies) {
                if (n.cue.compare(0, 7, "dialog:") != 0 || n.t < t0 || n.t >= t1 || weight < n.minWeight) continue;
                SoundCues::Emitter e = me ? atPawn() : SoundCues::Emitter{};
                if (!me) e.pos = pc->position();
                dialogEvents_.play(*who, matchPlayer, n.cue.substr(7), cues_, e, me ? 0.0f : core::length(pc->position() - listenerPos_));
            }
    }
    if (off || !renderer_ || weight <= 0.0f || !(t1 > t0)) return;
    const BodyNotifyIndex& idx = bodyNotifyIndex();
    auto it = idx.effects.find(std::string(animSet) + "|" + sequence);
    if (it == idx.effects.end()) return;
    const bool local = matchPlayer == localPlayer_;
    const Character* c = local ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(matchPlayer);
    if (!c) return;
    if (!local && core::length(c->position() - player_.pawn().position()) > 40.0f) return;
    for (const AnimNotifyRow* r : it->second) {
        if (r->time < t0 || r->time >= t1) continue;
        core::Mat4 sw;
        if (!bodySocketWorld(*c, r->socket, sw)) {
            if (log) LOG_INFO("BODYNOTIFYFX p%d %s.%s %s @%s: no socket on this body", matchPlayer, animSet, sequence, r->what, r->socket);
            continue;
        }
        const core::Vec3 p{sw.m[12], sw.m[13], sw.m[14]}, f = core::normalize(core::Vec3{sw.m[0], sw.m[1], sw.m[2]}),
                         u = core::normalize(core::Vec3{sw.m[4], sw.m[5], sw.m[6]});
        const int vid = fxStepSpawn(r->what, p, f, u, fxTeamOf(matchPlayer), false);
        if (log) LOG_INFO("BODYNOTIFYFX p%d %s.%s t %.3f %s @%s%s", matchPlayer, animSet, sequence, r->time, r->what, r->socket, (r->flags & 1) ? " attached" : "");
        if ((r->flags & 1) && vid >= 0) bodyFx_.push_back({vid, matchPlayer, r->socket, 0.0f});
    }
}

// [Systems] The MP transforming world actors' sounds [RE MP_TRANSFORMING_ACTOR_CUES_RE, C]: only the HmAnimNotify_Sound rows of the
// actor's transform AnimSequence (no effect / light / shake notify exists on these AnimSets), fired locally on every peer as the clip
// time moves - forwards on Transform (rows flagged 4: the opening cues), backwards on Untransform (flag 2: the backward-only closing
// cues) - at the mesh socket a row names (IAC_InteriorDoor: AUDIO_DOOR_CENTER) else the actor location. MinWeight is moot (one node,
// weight 1). A snap (load, the match reset, the versus doors' open start) re-syncs the times without playing. Rust's level-start
// Transform plays DOOR_INTERIOR + DOOR_TUBE on each peer. WFC_ACTORNOTIFYSOUND=0 turns them off (harness pairs); WFC_NOTIFYLOG logs.
void World::syncTransformingActorSounds() {
    taSoundT_.clear();
    for (const TransformingActors::Actor& a : transforming_.actors()) taSoundT_.push_back(a.t);
}

void World::tickTransformingActorSounds() {
    static const bool off = std::getenv("WFC_ACTORNOTIFYSOUND") && std::getenv("WFC_ACTORNOTIFYSOUND")[0] == '0';
    static const bool log = std::getenv("WFC_NOTIFYLOG") != nullptr;
    const std::vector<TransformingActors::Actor>& as = transforming_.actors();
    if (taSoundT_.size() != as.size()) { syncTransformingActorSounds(); return; }
    for (size_t i = 0; i < as.size(); ++i) {
        const TransformingActors::Actor& a = as[i];
        const float t0 = taSoundT_[i], t1 = a.t;
        taSoundT_[i] = t1;
        if (off || t1 == t0 || a.animSet.empty()) continue;
        const bool fwd = t1 > t0;
        for (const AnimNotifyRow& r : kAnimNotifies) {
            if (r.effect || a.sequence != r.sequence || a.animSet != r.animSet) continue;
            if (fwd ? (!(r.flags & 4) || r.time < t0 || r.time >= t1) : (!(r.flags & 2) || r.time <= t1 || r.time > t0)) continue;
            core::Vec3 at{a.matrix.m[12], a.matrix.m[13], a.matrix.m[14]};
            if (r.socket[0]) {
                const BodyNotifyIndex& idx = bodyNotifyIndex();
                auto it = idx.sockets.find(a.skeletalMesh + "|" + r.socket);
                if (it != idx.sockets.end()) {
                    const MeshSocketRow& s = *it->second;
                    const float loc[3] = {s.x, s.y, s.z};
                    const int rot[3] = {s.pitch, s.yaw, s.roll};
                    const core::Mat4 w = transforming_.boneWorldByName(a, s.bone) * ueSocketToGltf(loc, rot);
                    at = core::Vec3{w.m[12], w.m[13], w.m[14]};
                }
            }
            cues_.play(r.what, at, core::length(at - listenerPos_));
            if (log) LOG_INFO("ACTORNOTIFY %s %s.%s %s t %.3f %s @%s (%.1f %.1f %.1f)", a.name.c_str(), a.animSet.c_str(), a.sequence.c_str(),
                              fwd ? "fwd" : "back", r.time, r.what, r.socket[0] ? r.socket : "actor", at.x, at.y, at.z);
        }
    }
}

// Attached body effects follow their socket; after 2 s [PROV: the systems' own lifetimes are not modelled] or when the pawn is gone
// they are deactivated (live particles finish).
void World::tickBodyNotifyFx(float dt) {
    std::vector<std::pair<int, std::string>> abilities;
    { std::lock_guard<std::mutex> lk(abilityFxMu_); abilities.swap(abilityFxPending_); }
    for (const auto& a : abilities) spawnAbilityTriggerFx(a.first, a.second);
    for (size_t i = 0; i < bodyFx_.size();) {
        BodyFx& b = bodyFx_[i];
        b.age += dt;
        const Character* c = b.player == localPlayer_ ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(b.player);
        core::Mat4 sw;
        if (b.age <= 2.0f && c && bodySocketWorld(*c, b.socket, sw)) {
            fxStepMove(b.vid, core::Vec3{sw.m[12], sw.m[13], sw.m[14]}, core::normalize(core::Vec3{sw.m[0], sw.m[1], sw.m[2]}),
                       core::normalize(core::Vec3{sw.m[4], sw.m[5], sw.m[6]}));
            ++i;
            continue;
        }
        fxStepStop(b.vid);
        bodyFx_[i] = bodyFx_.back();
        bodyFx_.pop_back();
    }
}

// collision_weapon_physmat.json (AssetTools) -> the static weapon collision's per-triangle physical materials (see Collision.h).
void World::loadWeaponPhysmats(const std::string& mapDir, const render::MeshData& glbMesh) {
    std::string path = mapDir + "collision_weapon_physmat.json", text;
    if (!readTextFile(path, text)) {
        const char* mr = std::getenv("WFC_FRONTEND_MANIFESTS");   // the frontend's manifest root (frontend::Catalog)
        path = std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/maps/" + mapName_ + "/collision_weapon_physmat.json";
        if (!readTextFile(path, text)) { LOG_INFO("physmat: no collision_weapon_physmat.json for %s (impacts use the defaults)", mapName_.c_str()); return; }
    }
    assets::Json root;
    if (!assets::Json::parse(text, root)) { LOG_WARN("physmat: cannot parse %s", path.c_str()); return; }
    const std::string want = root["glb_sha256"].asString(), have = core::sha256File(mapDir + "collision_weapon.glb");
    if (want.empty() || want != have) { LOG_WARN("physmat: %s describes another collision_weapon.glb (sha256 %s, loaded %s): skipped", path.c_str(), want.c_str(), have.c_str()); return; }
    std::vector<std::string> names;
    for (size_t i = 0; i < root["physmats"].size(); ++i) names.push_back(root["physmats"][i].asString());
    // Per glb mesh: the expanded per-triangle index (checked against the mesh triangle count we baked).
    std::map<int, std::vector<uint16_t>> perMesh;
    std::map<int, uint32_t> baked;
    for (const render::SubMesh& sm : glbMesh.subs) if (sm.gltfMesh >= 0) baked[sm.gltfMesh] = std::max(baked[sm.gltfMesh], sm.meshTriOffset + sm.indexCount / 3);
    // Alignment check: the file's source_mesh per glb mesh vs the StaticMesh path our bake recorded for that mesh index.
    int nameChecked = 0, nameMismatch = 0;
    for (const render::SubMesh& sm : glbMesh.subs) {
        if (sm.gltfMesh < 0 || sm.sourceMesh.empty()) continue;
        const std::string fileMesh = root["meshes"][std::to_string(sm.gltfMesh)]["source_mesh"].asString();
        if (fileMesh.empty()) continue;
        ++nameChecked; if (fileMesh != sm.sourceMesh) ++nameMismatch;
    }
    if (nameMismatch) { LOG_WARN("physmat: %d of %d submeshes name another source mesh than the file: skipped", nameMismatch, nameChecked); return; }
    int mismatched = 0;
    for (const auto& kv : baked) {
        const assets::Json& m = root["meshes"][std::to_string(kv.first)];
        std::vector<uint16_t> v(kv.second, 0);
        if ((uint32_t)m["triangles"].asInt(-1) != kv.second) { ++mismatched; perMesh[kv.first] = v; continue; }
        const assets::Json& runs = m["runs"];
        for (size_t r = 0; r < runs.size(); ++r) {
            const int first = runs[r][0].asInt(0), count = runs[r][1].asInt(0), idx = runs[r][2].asInt(0);
            for (int t = first; t < first + count && t >= 0 && (size_t)t < v.size(); ++t) v[(size_t)t] = (uint16_t)idx;
        }
        perMesh[kv.first] = std::move(v);
    }
    // The static triangles in build order: splitMovers' filter (movers and TnForcedDirVolume submeshes are not static).
    const std::vector<std::string> movers = mapState_.moverActorNames();
    const bool keepForcedDir = std::getenv("WFC_NOFORCEDDIR") != nullptr;
    std::vector<uint16_t> perTri;
    for (const render::SubMesh& sm : glbMesh.subs) {
        if (std::find(movers.begin(), movers.end(), sm.nodeName) != movers.end()) continue;
        if (!keepForcedDir && sm.nodeName.rfind("TnForcedDirVolume", 0) == 0) continue;
        const std::vector<uint16_t>* v = sm.gltfMesh >= 0 && perMesh.count(sm.gltfMesh) ? &perMesh[sm.gltfMesh] : nullptr;
        for (uint32_t t = 0; t < sm.indexCount / 3; ++t) perTri.push_back(v && sm.meshTriOffset + t < v->size() ? (*v)[sm.meshTriOffset + t] : 0);
    }
    const size_t n = perTri.size();
    if (!weaponCollision_.setPhysmats(std::move(perTri), std::move(names), root["engine_default_physmat"].asString()))
        LOG_WARN("physmat: %zu triangles vs %zu built: skipped", n, weaponCollision_.triangleCount());
    else {
        LOG_INFO("physmat: %s: %zu weapon-collision triangles, %d mesh(es) with a triangle-count mismatch (engine default), %d submesh source names checked",
                 mapName_.c_str(), n, mismatched, nameChecked);
        if (std::getenv("WFC_PHYSMATLOG")) {
            int tested = 0; const int ok = weaponCollision_.physmatSelfTest(2000, tested);
            LOG_INFO("physmat self-test: %d of %d centroid rays report their triangle's material", ok, tested);
        }
    }
}

// The chassis' player TnMeleeSet (CharacterData.MeleeSet): 0 HeavyMedium, 1 HeavySmall, 2 LightMedium, 3 LightSmall, -1 unlisted.
static int meleeSetOf(const char* chassisId) {
    static const struct { const char* id; int set; } kSets[] = {
        {"Tank", 0}, {"Truck", 0}, {"Truck7", 0}, {"Jet5", 0}, {"Jet7", 0}, {"Tank2", 0}, {"Tank3", 0}, {"Tank4", 0}, {"Tank5", 0}, {"Truck5", 0},
        {"Minion2", 1},
        {"Jet", 2}, {"Car5", 2}, {"Jet2", 2}, {"Jet3", 2}, {"Jet4", 2}, {"Jet6", 2}, {"Jet8", 2}, {"Truck2", 2}, {"Truck3", 2}, {"Truck4", 2}, {"Truck6", 2},
        {"Car", 3}, {"Car2", 3}, {"Car3", 3}, {"Car4", 3}, {"Car6", 3}, {"Car7", 3}};
    if (chassisId) for (const auto& s : kSets) if (std::strcmp(s.id, chassisId) == 0) return s.set;
    return -1;
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
        // Speed 0 (no lunge; LightMedium: 3000 for 1 s), GroundSpeedMultiplier 0.75 [CONF TnMeleeSet]. Clip lengths / sweep times
        // from the anim set when the robot export lacks the clip.
        static const char* kMace[3] = {"Melee_Mace_01", "Melee_Mace_02", "Melee_Mace_03"};
        static const float kMaceLen[3] = {1.033f, 1.233f, 1.167f};
        const int k = pc.meleeAlternate_++ % 3;
        pc.meleeVariant_ = k;
        pc.playAction(kMace[k], false);
        pc.meleeLen_ = kMaceLen[k];
        if (const assets::SkinnedModel* m = pc.currentModel()) { int ci = m->clipByName(kMace[k]); if (ci >= 0) pc.meleeLen_ = m->clips[(size_t)ci].duration; }
        if (meleeSetOf(pc.chassis().id.c_str()) != 2) return;   // only LightMedium's flag / bomb attacks dash
    }
    if (pc.meleePoke_ || pc.meleeCarrier_) {
        // fall through to the assist lunge
    } else if (whirlwind) {
        pc.meleeLen_ = 5.9f;
        pc.playAction("Transform_Whirlwind_ROBO", true);
        return;
    }
    // AnimSet chooser: Melee_EnergonSword -> _01 / _03 alternately; clip length (1.0 s authored) from the model when present.
    const char* clip = (pc.meleeAlternate_++ % 2) ? "Melee_EnergonSword_03" : "Melee_EnergonSword_01";
    if (!pc.meleePoke_ && !pc.meleeCarrier_) {
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
    if (best < 1e9f) {
        pc.lungeRemain_ = pc.meleeCarrier_ ? 1.0f : 0.25f; pc.lungeSpeed_ = pc.meleeCarrier_ ? 30.0f : 25.0f;   // AttackDash Time / Speed
        pc.setYaw(std::atan2(-pc.lungeDir_.x, -pc.lungeDir_.z));
    }
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
        for (DecoyTrap& d : decoys_)   // melee kills an enemy decoy trap instantly
            if (d.alive && self != d.owner && !(matchActive_ && match_.settings().teamGame && match_.sameTeam(self, d.owner)) &&
                std::fabs(d.pos.x - at.x) <= ex.x + 0.5f && std::fabs(d.pos.z - at.z) <= ex.z + 0.5f && std::fabs(d.pos.y - at.y) <= ex.y + 0.5f)
                destroyDecoyTrap(d);
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
            noteHitDir(c - pc.actorLocation());
            applyMatchDamage(vp, self, damage, false, type);
            onPawnHitEffect(type, v.chassis().id, vp, c);   // [Systems M08p] the victim's melee hit effect
            // Momentum = normal(victim - attacker) x Impulse (WeaponAttack 30000 / LightMedium 20000, flag / bomb 80000, Whirlwind 2000) [CONF].
            const float impulse = whirl ? 2000.0f : pc.meleeCarrier_ ? 80000.0f : pc.meleePoke_ ? 200000.0f
                                : meleeSetOf(pc.chassis().id.c_str()) == 2 ? 20000.0f : 30000.0f;
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
        {   // [Systems M08o] the grenade's projectile sounds (FlightSound / FuseSound / BounceSound / ExplosionSound), any thrower
            Projectile& np = projectiles_.back();
            np.weaponClass = "TransContent.TnWeapon" + std::string(d.id);
            np.audioKey = ++projAudioKey_;
            onProjectileSpawned(np.audioKey, np.weaponClass, np.pos);
        }
        projectileFxStart(projectiles_.back());
        LOG_INFO("grenade %s: |v| %.1f m/s pitch %.1f deg (aim %.1f)", d.id, core::length(vel), launchPitch, aimPitch);
}

void World::startLocalGrenadeToss() {
    Character& pc = player_.pawn();
    if (localDead_ || pc.moveForm() != Form::Robot || pc.isTransforming() || pc.isMeleeing() || grenadeTossDelay_ >= 0.0f) return;
    Weapon* gb = nullptr;
    for (Weapon& w : pc.inventoryMutable()) if (w.grenade()) { gb = &w; break; }
    const std::string gcls = gb && gb->def ? "TransContent.TnWeapon" + std::string(gb->def->id) : std::string();   // [Systems M08o]
    if (pc.carryingHeavy_ != 0) { if (!gcls.empty()) onGrenadeToss(gcls, true); return; }   // carrying a WT_Heavy weapon: PlayDryFireSound
    if (!gb) return;
    if (gb->reserve <= 0 || grenadeCooldown_ > 0.0f) { onGrenadeToss(gcls, true); return; }   // PlayDryFireSound
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
    onGrenadeToss(gcls, false);                        // [Systems M08o] PerformToss: GrenadeBagMesh.PlaySoundEvent(WP_Fire)
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

// The pawn's collision cylinder against the barrier's collision box (in box space; a small margin so "left" means clear of it).
bool World::barrierOverlaps(const BarrierState& br, const Character& c) const {
    const float r = c.cylinderRadius(c.moveForm()) + 0.05f, hh = c.cylinderHalfHeight(c.moveForm());
    const core::Vec3 q = core::transformPoint(br.boxInv, c.actorLocation());
    return std::fabs(q.x) < br.half.x + r && std::fabs(q.z) < br.half.z + r && std::fabs(q.y) < br.half.y + hh;
}

std::vector<int> World::barrierIgnoreFor(int player) const {
    std::vector<int> v;
    for (const BarrierState& br : barriers_)
        if (br.alive && br.dyn >= 0 && std::find(br.passThrough.begin(), br.passThrough.end(), player) != br.passThrough.end()) v.push_back(br.dyn);
    return v;
}

void World::requestBarrier(int owner) {
    // TnAbilityBarrier for any participant: SpawnDelay 0.5 -> SpawnBarrier; one barrier per owner (the cooldown waits for it).
    BarrierState b; b.owner = owner; b.delay = 0.5f;
    barriers_.push_back(b);
    if (Character* pc = participantPawnMutable(owner)) pc->barrierAlive_ = true;
}

// The barrier / sentry meshes load (textures: GL) on the main thread: the local part calls this every step during a match (a flag
// check after the first time), so an ability actor spawned by the background part never loads.
void World::ensureAbilityModels() {
    svGeneratorsEnsure(assetRoot() + "/../content/");   // (Escalation) the generator meshes, once per map
    svMechEnsureModels(assetRoot() + "/../content/WEP_SentryBlaster01_p/");   // (Escalation DLC) District's sentry turrets
    if (!barrierModelTried_ && onMainThread()) {
        barrierModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_Shield_p/Barrier/";
        if (assets::loadSkinnedGlb(ext + "WEP_Barrier_SKEL.gltf", barrierModel_) && barrierModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_Barrier_ANIM.anim.gltf", barrierModel_);
            // The export carries no extras.wfc_material for the barrier, so the renderer fell back to an opaque lit surface
            // (user 2026-10-14: the grid must be see-through). Its compiled material is the package's
            // WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST (ENV_ForceField_Defrag_MAT: BLEND_Additive, unlit, distortion,
            // depth-biased alpha), present in every MP map's materials_glsl.json.
            for (render::Material& mt : barrierModel_.mats)
                if (mt.wfcName.empty() && !mt.sourceName.empty()) mt.wfcName = "WEP_Shield_p.Barrier." + mt.sourceName;
            resolveModelTextures(barrierModel_);
            if (renderer_) { render::MeshData md; md.subs = barrierModel_.subs; md.mats = barrierModel_.mats; fxPrewarm(*renderer_, md, 0); prewarmSkin(barrierModel_); }
        } else LOG_ERROR("barrier: WEP_Barrier_SKEL unavailable (collision only)");
    }
    if (!sentryModelTried_ && onMainThread()) {
        sentryModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_SentryAbility_p/";
        if (assets::loadSkinnedGlb(ext + "WEP_SentryDeploy_SKEL.gltf", sentryModel_) && sentryModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_DeployedTurret_ANIM.anim.gltf", sentryModel_);
            resolveModelTextures(sentryModel_);
            if (renderer_) { render::MeshData md; md.subs = sentryModel_.subs; md.mats = sentryModel_.mats; fxPrewarm(*renderer_, md, 0); prewarmSkin(sentryModel_); }
        } else LOG_ERROR("sentry: WEP_SentryDeploy_SKEL unavailable");
    }
    if (!mineModelTried_ && onMainThread()) {   // [Gameplay] kamikaze mine (MinePooper) presentation
        mineModelTried_ = true;
        const std::string ext = assetRoot() + "/../content/WEP_KamikazeMine_p/";
        if (assets::loadSkinnedGlb(ext + "WEP_KamikazeMine_SKEL.gltf", mineModel_) && mineModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_KamikazeMine_ANIM.anim.gltf", mineModel_);
            // The export carries no extras.wfc_material: the compiled original is WEP_KamikazeMine_p.WEP_KamikazeMine_MATINST.
            for (render::Material& mt : mineModel_.mats)
                if (mt.wfcName.empty() && !mt.sourceName.empty()) mt.wfcName = "WEP_KamikazeMine_p." + mt.sourceName;
            resolveModelTextures(mineModel_);
            if (renderer_) { render::MeshData md; md.subs = mineModel_.subs; md.mats = mineModel_.mats; fxPrewarm(*renderer_, md, 0); prewarmSkin(mineModel_); }
        } else LOG_ERROR("kamikaze mine: WEP_KamikazeMine_SKEL unavailable (mines not drawn)");
    }
}

void World::spawnBarrier(BarrierState& slot) {
    Character* pcp = participantPawnMutable(slot.owner);
    if (!pcp) return;                                       // IsOwnerDead
    Character& pc = *pcp;
    ensureAbilityModels();
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
    // Pawns standing where it appears walk out: its collision is ignored for them until they have fully left (user decision
    // 2026-10-08; the original has no push-out and no placement test [CONF RE], whether UE3 let them walk out is UNKNOWN).
    b.passThrough.clear();
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (const Character* c = participantPawn((int)i)) if (barrierOverlaps(b, *c)) b.passThrough.push_back((int)i);
    static const bool bwLog = std::getenv("WFC_BARRIERWALKTEST") != nullptr;
    if (bwLog) for (size_t i = 0; i < match_.players().size(); ++i) if (const Character* c = participantPawn((int)i)) {
        const core::Vec3 q = core::transformPoint(b.boxInv, c->actorLocation());
        if (core::length(c->actorLocation() - b.pos) < 15.0f) LOG_INFO("BARRIERWALK p%d box-space (%.2f %.2f %.2f) half (%.2f %.2f %.2f) r %.2f hh %.2f at (%.1f %.1f %.1f)", (int)i, q.x, q.y, q.z, b.half.x, b.half.y, b.half.z, c->cylinderRadius(c->moveForm()), c->cylinderHalfHeight(c->moveForm()), c->actorLocation().x, c->actorLocation().y, c->actorLocation().z);
    }
    LOG_INFO("ability Barrier: wall at (%.1f %.1f %.1f), 1000 HP%s", b.pos.x, b.pos.y, b.pos.z, b.passThrough.empty() ? "" : " (pawns inside walk out)");
}

void World::tickBarrier(float dt) {
    for (BarrierState& b : barriers_) {
        if (b.delay >= 0.0f) { b.delay -= dt; if (b.delay < 0.0f) spawnBarrier(b); }
        if (b.alive && !participantPawn(b.owner)) b.alive = false;   // destroyed on the owner's death
        if (b.alive && !b.passThrough.empty())                // a pawn that has fully left (or died) is blocked normally again
            b.passThrough.erase(std::remove_if(b.passThrough.begin(), b.passThrough.end(), [&](int p) {
                const Character* c = participantPawn(p); return !c || !barrierOverlaps(b, *c); }), b.passThrough.end());
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
// ---- Decoy Trap [CONF RE MP_DECOY_TRAP_RE.md: TnAbilityDecoyTrap / TnDecoyTrap / TnDroppedPickupDecoyTrap / TnBuffDecoyTrap] ----
void World::onDecoyTrapEvent(DecoyEvent e, int owner, int victim, const core::Vec3& pos) {
    (void)e; (void)owner; (void)victim; (void)pos;   // [Systems] fills (sounds / FX); Gameplay's stub
}

void World::requestDecoyTrap(int owner) {
    // ServerTriggerAbility: DECOY_TRAP_DEPLOY now; SpawnDelay 0.5 -> SpawnInventory; one trap per owner (the cooldown waits for it).
    DecoyTrap d; d.owner = owner; d.delay = 0.5f;
    d.team = matchActive_ && owner >= 0 && (size_t)owner < match_.players().size() ? match_.players()[(size_t)owner].team : 255;
    Character* pc = participantPawnMutable(owner);
    decoys_.push_back(d);
    if (pc) pc->decoyAlive_ = true;
    onDecoyTrapEvent(DecoyEvent::Deploy, owner, -1, pc ? pc->actorLocation() : core::Vec3{0, 0, 0});
}

void World::destroyDecoyTrap(DecoyTrap& d) {
    if (!d.alive) return;
    d.alive = false;
    onDecoyTrapEvent(DecoyEvent::Destroyed, d.owner, -1, d.pos);
}

void World::tickDecoyTraps(float dt) {
    // The stun on every pawn: TnBuffDecoyTrap BuffTime 2.0; Unapply -> React_DecoyTrap_Out (robot form), input restored.
    auto tickStun = [&](Character& c) {
        if (c.decoyStunRemain_ <= 0.0f) return;
        c.decoyStunRemain_ -= dt;
        if (c.decoyStunRemain_ > 0.0f) return;
        c.decoyStunRemain_ = 0.0f;
        if (c.form() == Form::Robot && !c.isTransforming()) c.playAction("HitReactions_React_DecoyTrap_Out", false);
        onDecoyTrapEvent(DecoyEvent::StunEnd, c.decoyStunBy_, -1, c.actorLocation());
        c.decoyStunBy_ = -1;
    };
    if (!localDead_) tickStun(player_.pawn());
    for (MatchOpponent* o : opponents_) if (o->spawned()) tickStun(o->pawn());
    const bool teamGame = matchActive_ && match_.settings().teamGame;
    for (DecoyTrap& d : decoys_) {
        const Character* owner = participantPawn(d.owner);
        if (d.delay >= 0.0f) {
            d.delay -= dt;
            if (d.delay < 0.0f && owner) {   // DropFrom(owner location, TossVelocity (2000, 1200, 0) >> owner rotation)
                const core::Vec3 f = core::forwardFromYawPitch(owner->yaw(), 0.0f), r{-f.z, 0.0f, f.x};
                d.alive = true; d.landed = false; d.pos = owner->actorLocation(); d.vel = f * 20.0f + r * 12.0f;
                d.life = 30.0f; d.yaw = owner->yaw(); d.alpha = 0.0f; d.armT = 0.0f; d.glitchT = 0.0f;
                d.weaponId = owner->weapon().def ? owner->weapon().def->id : "";   // SetDecoyWeapon(the owner's current weapon)
                LOG_INFO("ability DecoyTrap (p%d): trap dropped as %s", d.owner, d.weaponId.c_str());
            }
        }
        if (d.alive) {
            d.life -= dt;
            if (d.life <= 0.0f || !owner) destroyDecoyTrap(d);   // lifespan (H: 30 s) / the owner dead -> FadeOut
        }
        if (d.alive && !d.landed) {   // PHYS_Falling until it lands (the beacon's drop physics)
            d.vel.y -= core::config::kGravity * dt;
            core::Vec3 next = d.pos + d.vel * dt;
            float t; core::Vec3 n;
            if (collision_.valid() && collision_.segmentHit(d.pos, next, t, n)) {
                next = d.pos + (next - d.pos) * std::max(0.0f, t - 1e-3f);
                if (n.y > 0.7f) { d.landed = true; d.vel = {0, 0, 0}; } else { d.vel.x = 0.0f; d.vel.z = 0.0f; }
            }
            d.pos = next;
            if (d.pos.y < killZ_) destroyDecoyTrap(d);
        }
        if (!d.alive) continue;
        // Tells: spins the opposite way to a real drop (yaw rate negated; Rendering applies the rate); defrags in over ArmDelay 3 s after
        // landing, then every GlitchDelay 5 s fades out over 1 s and back in [CONF; the fade-in rate H: 1 s].
        if (d.landed) {
            d.armT += dt;
            if (d.armT < 3.0f) d.alpha = d.armT / 3.0f;
            else {
                d.glitchT += dt;
                const float g = std::fmod(d.glitchT, 7.0f);   // 5 s shown, 1 s out, 1 s back in
                d.alpha = g < 5.0f ? 1.0f : (g < 6.0f ? 1.0f - (g - 5.0f) : g - 6.0f);
            }
        }
        // Pickup: an enemy robot pressing pickup inside the touch cylinder (r 200 UU, h 100 UU about +100 Z).
        const core::Vec3 cyl = d.pos + core::Vec3{0, 1.0f, 0};
        for (size_t i = 0; i < match_.players().size() && d.alive; ++i) {
            const int p = (int)i;
            if (p == d.owner || (teamGame && match_.sameTeam(p, d.owner))) continue;   // TnDecoyTrap.PickupAllowed
            Character* c = participantPawnMutable(p);
            if (!c || c->form() != Form::Robot || c->isTransforming() || c->inputLocked()) continue;
            bool presses = false;
            if (p == localPlayer_) presses = localPickupLatch_;
            else for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) { presses = o->pressesPickup; break; }
            if (!presses) continue;
            const core::Vec3 q = c->actorLocation();
            const float dx = q.x - cyl.x, dz = q.z - cyl.z;
            const float reach = 2.0f + c->cylinderRadius(c->moveForm());   // the touch cylinder overlaps the pawn's cylinder
            if (dx * dx + dz * dz > reach * reach || std::fabs(q.y - cyl.y) > 1.0f + c->cylinderHalfHeight(c->moveForm())) continue;
            // TnBuffPickup.GivenTo -> AddBuff(TnBuffDecoyTrap, applier = the creator); the trap is destroyed; DECOY_TRAP_EXPLODE.
            d.alive = false;
            c->decoyStunRemain_ = 2.0f;
            c->decoyStunBy_ = d.owner;
            c->zeroVelocity();                    // Velocity and Acceleration zeroed
            c->exposeSelf();                      // removes Cloak / Disguise
            c->playAction("HitReactions_React_DecoyTrap_In", false);   // robot form (2.17 s: covers the 2 s stun)
            LOG_INFO("ability DecoyTrap (p%d): p%d trapped", d.owner, p);
            onDecoyTrapEvent(DecoyEvent::Trigger, d.owner, p, d.pos);
        }
    }
    for (DecoyTrap& d : decoys_) if (!d.alive && d.delay < 0.0f) ++d.deadTicks;
    decoys_.erase(std::remove_if(decoys_.begin(), decoys_.end(), [](const DecoyTrap& d) { return d.deadTicks >= 2; }), decoys_.end());
    for (size_t i = 0; i < match_.players().size(); ++i)
        if (Character* pc = participantPawnMutable((int)i)) {
            bool any = false;
            for (const DecoyTrap& d : decoys_) any |= d.owner == (int)i && (d.alive || d.delay >= 0.0f);
            pc->decoyAlive_ = any;
        }
}

// ---- VehicleDying collision [CONF RE VEHICLE_DEATH_PRESENTATION_RE.md: the live rigid body and collision stay until the gib] ----
void World::noteVehicleDeath(const Character& pawn) {
    if (pawn.moveForm() != Form::Vehicle || dyingBodies_.empty()) return;
    const bool jet = pawn.vehicleParams().form == VehicleFormType::Jet;
    const core::Vec3 v = pawn.velocity();
    const bool moving = std::sqrt(v.x * v.x + v.z * v.z) > 1.0f;   // [H] the moving / stationary threshold
    DyingRequest r;
    r.pos = pawn.actorLocation(); r.yaw = pawn.yaw();
    r.radius = pawn.cylinderRadius(Form::Vehicle); r.halfHeight = pawn.cylinderHalfHeight(Form::Vehicle);
    const float u = core::simRand01();
    r.time = jet ? 4.0f + u : (moving ? 0.3f + 0.45f * u : 0.2f + 0.3f * u);   // TotalTime: jets U(4, 5); moving U(0.3, 0.75); still U(0.2, 0.5)
    dyingQueue_.push_back(r);
}

void World::tickDyingBodies(float dt) {
    CollisionWorld* weapon = weaponCollision_.valid() ? &weaponCollision_ : nullptr;
    for (DyingBody& b : dyingBodies_) {
        if (b.remain <= 0.0f) continue;
        b.remain -= dt;
        if (b.remain > 0.0f) continue;
        b.remain = 0.0f;   // the gib: the dying actor stops blocking
        if (b.pawnSet >= 0) collision_.setDynamicEnabled(b.pawnSet, false);
        if (weapon && b.weaponSet >= 0) weapon->setDynamicEnabled(b.weaponSet, false);
    }
    for (const DyingRequest& r : dyingQueue_) {
        DyingBody* slot = nullptr;
        for (DyingBody& b : dyingBodies_) if (b.remain <= 0.0f) { slot = &b; break; }
        if (!slot) { LOG_WARN("vehicle dying: body pool full (16)"); break; }
        const core::Mat4 pose = core::Mat4::translate(r.pos) * core::Mat4::rotateY(r.yaw) * core::Mat4::scale(core::Vec3{r.radius, r.halfHeight, r.radius});
        slot->remain = r.time;
        if (slot->pawnSet >= 0) { collision_.setDynamicPose(slot->pawnSet, pose); collision_.setDynamicEnabled(slot->pawnSet, true); }
        if (weapon && slot->weaponSet >= 0) { weapon->setDynamicPose(slot->weaponSet, pose); weapon->setDynamicEnabled(slot->weaponSet, true); }
    }
    dyingQueue_.clear();
}

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
            if (!radiusVisible(eye, c->actorLocation())) continue;   // VisibleCollidingActors
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
    s.poseFinal = false;
    s.seen.clear();
    s.sightCounter = 0.2f * core::simRand01();   // staggered sight checks (SightCounter = 0.2 x FRand)
    Character* pcp = participantPawnMutable(s.owner);
    if (!pcp) return;
    const Character& pc = *pcp;
    ensureAbilityModels();
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
    // Targeting [CONF RE 6756336 MP note addendum 12; the sight interval HIGH]: the native sight pass registers the enemies a sentry can
    // see (periodic, ~0.2 s staggered, one LineOfSightTo per pawn within SightRadius 30000 UU; no per-tick traces); every tick
    // FindBestTarget takes the CLOSEST valid one of that list (not sticky): alive, in the pitch range (+-45 deg, TnSentryPawnAbility),
    // PeripheralVision -1 (any yaw), and claimed by fewer than _MaxNumClaimedEnemies 2 sentries. Fire while within MaxAttackRange.
    auto sightCheck = [&](Sentry& s, const core::Vec3& muzzle) {
        s.seen.clear();
        if (!matchActive_) return;
        for (size_t i = 0; i < match_.players().size(); ++i) {
            const int p = (int)i;
            if (p == s.owner || (match_.settings().teamGame && match_.sameTeam(p, s.owner))) continue;
            const Character* c = participantPawn(p);
            if (!c) continue;
            const core::Vec3 d = c->actorLocation() - muzzle;
            if (core::dot(d, d) > 300.0f * 300.0f) continue;   // SightRadius 30000 UU
            float tt;
            if (line && line->segmentHit(muzzle, c->actorLocation(), tt)) continue;
            s.seen.push_back(p);
        }
    };
    static std::vector<int> claims;   // sentries targeting each match player this tick
    claims.assign(match_.players().size(), 0);
    auto findBestTarget = [&](Sentry& s, const core::Vec3& muzzle) -> const Character* {
        s.target = -1;
        const Character* tgt = nullptr;
        float best = 1e30f;
        for (int p : s.seen) {
            const Character* c = participantPawn(p);
            if (!c || (size_t)p >= claims.size() || claims[(size_t)p] >= 2) continue;
            const core::Vec3 d = c->actorLocation() - muzzle;
            const float dist2 = core::dot(d, d);
            if (dist2 > 300.0f * 300.0f || dist2 < 1e-6f) continue;
            if (std::fabs(std::atan2(d.y, std::hypot(d.x, d.z))) > 45.0f * 0.0174533f) continue;   // CanOwnerSee: the pitch range
            if (dist2 < best) { best = dist2; s.target = p; tgt = c; }   // seen is in player order: ties keep the lower index
        }
        if (s.target >= 0) ++claims[(size_t)s.target];
        return tgt;
    };
    // (1) serial upkeep (spawn / lifetime / owner); (2) target searches on the worker pool (read-only queries, each writes its own
    // sentry); (3) serial aim / fire in order - a target killed earlier in this pass is searched again serially, which gives exactly
    // the serial result (kills only remove candidates, damage moves nobody).
    static std::vector<Sentry*> live, sighting;
    live.clear(); sighting.clear();
    for (Sentry& s : sentries_) {
        if (s.delay >= 0.0f) { s.delay -= dt; if (s.delay < 0.0f) spawnSentry(s); }
        const bool ownerAlive = s.owner >= 0 && participantPawn(s.owner) != nullptr;
        if (s.alive) {
            s.t += dt;
            s.health -= 135.0f / 30.0f * dt;              // Lifetime 30
            if (s.health <= 0.0f || !ownerAlive) { s.alive = false; s.target = -1; }   // dies with the owner
        }
        if (s.alive) {
            live.push_back(&s);
            s.sightCounter -= dt;
            if (s.sightCounter <= 0.0f) { while (s.sightCounter <= 0.0f) s.sightCounter += 0.2f; sighting.push_back(&s); }
        }
    }
    // The sight checks due this step: read-only queries, each writes its own sentry's list (worker pool, deterministic).
    { TickTimer tts(27); core::WorkerPool::get().run((int)sighting.size(), [&](int i) { Sentry& s = *sighting[(size_t)i]; sightCheck(s, s.pos + core::Vec3{0, 2.0f, 0}); }); }
    for (Sentry* sp : live) {
        Sentry& s = *sp;
        if (!s.alive) continue;
        const core::Vec3 muzzle = s.pos + core::Vec3{0, 2.0f, 0};
        const Character* tgt = findBestTarget(s, muzzle);   // every tick, in sentry order (claims; kills earlier in the pass drop out)
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
                if (seatOrdered_) {   // online: by match player index, the owner excluded
                    for (size_t q = 0; q < match_.players().size(); ++q) {
                        const Character* c = (int)q == s.owner ? nullptr : participantPawn((int)q);
                        float th;
                        if (c && MatchOpponent::pawnRayHit(*c, muzzle, dir, wall, th) && th < hd) { hd = th; hit = (int)q; }
                    }
                } else
                for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(muzzle, dir, wall, th) && th < hd) { hd = th; hit = o->matchPlayer(); } }
                if (!seatOrdered_ && !localDead_ && localPlayer_ != s.owner) { float th; if (MatchOpponent::pawnRayHit(player_.pawn(), muzzle, dir, wall, th) && th < hd) { hd = th; hit = localPlayer_; } }
                onSentryShot(muzzle, hit < 0 && wall < 300.0f, muzzle + dir * hd);   // [Systems M08m] WP_Fire + world DefaultImpactSound
                if (hit >= 0 && !(match_.settings().teamGame && match_.sameTeam(hit, s.owner)) && hit != s.owner) {
                    const float mod = hd <= 80.0f ? 1.0f : 0.5f;   // GetRangeDamageModifier steps (1.0 to 8000 UU, else 0.5) [CONF script]
                    applyMatchDamage(hit, s.owner, 8.0f * mod, false, "TransGame.TnDamageTypeSentry");   // kill credit to the owner
                }
            }
        }
        if (sentryModel_.valid() && !s.poseFinal) {   // samplePose clamps a non-looping clip at its end: constant after (skinned once more)
            TickTimer ttp(28);
            int clip = sentryModel_.clipByName("WEP_DeployedTurret_Activate");
            if (clip >= 0) {
                assets::LocalPose lp; std::vector<core::Mat4> g; assets::samplePose(sentryModel_, clip, s.t, false, lp); assets::skinPose(sentryModel_, lp, g, s.mesh);
                if (s.t >= sentryModel_.clips[(size_t)clip].duration) s.poseFinal = true;
            }
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
    onGuidedMissileExploded(at);                       // [Systems M08m] FlightSound fade + ExplosionSound
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
            ctl.setGuiding(true);                               // the missile view below starts this tick: the cut is here, once
            if (renderer_) renderer_->notifyCameraCut("missile cam");
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
        if (renderer_) renderer_->notifyCameraCut("missile cam end");
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
    onAbilityActorExploded(AbilityActor::RollerMine, m.owner, m.owner == localPlayer_, m.pos);   // [Systems M09h] _ExplosionSound
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
                    if (m.owner != localPlayer_) ++participantRollerSpawns_;
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
            for (size_t ti = 0; ti <= opponents_.size(); ++ti) {
                const int owner = s.owner >= 0 ? s.owner : localPlayer_;
                const bool localT = ti == opponents_.size();   // the local pawn last (a Remote seat's shot)
                if (localT && (owner == localPlayer_ || localDead_)) continue;
                MatchOpponent* o = localT ? nullptr : opponents_[ti];
                const int tp = localT ? localPlayer_ : o->matchPlayer();
                if ((!localT && !o->spawned()) || tp == owner || match_.sameTeam(tp, owner)) continue;
                Character& e = localT ? player_.pawn() : o->pawn();
                const core::Vec3 c = e.actorLocation();
                // segment-vs-cylinder approximated by the closest point on the step segment
                core::Vec3 seg = next - s.pos; float sl2 = core::dot(seg, seg);
                float u = sl2 > 1e-8f ? core::clampf(core::dot(c - s.pos, seg) / sl2, 0.0f, 1.0f) : 0.0f;
                core::Vec3 q = s.pos + seg * u;
                if (std::hypot(q.x - c.x, q.z - c.z) > e.cylinderRadius(e.moveForm()) + s.radius * 0.1f ||
                    std::fabs(q.y - c.y) > e.cylinderHalfHeight(e.moveForm()) + s.radius * 0.1f) continue;
                if (s.kind == 0) { e.applyJammed(15.0f); LOG_INFO("ability AbilityJammer: player %d jammed 15 s", tp); }
                else {
                    e.transformDisruptRemain_ = 3.0f;
                    if (!e.isTransforming()) e.beginTransform();   // ForceIntoForm(opposite) [PROV: via the transform path]
                    LOG_INFO("ability TransformDisruptor: player %d forced to transform", tp);
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
    if (!match_.settings().survival && pc.tempWeapon_ == 2 && (pc.isTransforming() || pc.form() == Form::Vehicle || pc.tempDropRequested_ || localDead_)) pc.removeTempWeapon();
    pc.tempDropRequested_ = false;
    for (size_t i = 0; i < mineFades_.size();) {   // [Gameplay] FadingOut ghosts (presentation): FadeOutTime 1 s
        if ((mineFades_[i].age += dt) >= 1.0f) { mineFades_.erase(mineFades_.begin() + (long)i); continue; }
        ++i;
    }
    // MinePooper: every 2.0 s while the 15 s buff lasts, spawn at owner + (400, 100, 0) rotated.
    auto pooper = [&](Character& pc, int owner) {
    if (pc.minePooperRemain_ > 0.0f) {
        pc.minePooperRemain_ -= dt; pc.minePooperTimer_ -= dt;
        if (pc.minePooperTimer_ <= 0.0f) {
            pc.minePooperTimer_ += 2.0f;
            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            KamikazeMine m; m.pos = pc.actorLocation() + f * 4.0f + r * 1.0f; m.vel = {0, 0, 0};
            m.owner = owner == localPlayer_ && !onlineSeats_ ? -1 : owner;   // -1 = the offline local player (the old path)
            m.audioKey = ++projAudioKey_;                  // [Systems M08p]
            mines_.push_back(m);
            static const bool noMinePres = std::getenv("WFC_NOMINEPRES") != nullptr;   // harness (perf bisects): no mine presentation
            if (!noMinePres) {   // [Gameplay] presentation: spawned with the owner's rotation, the flight trail from spawn
                ensureAbilityModels();
                MinePres& mp = minePres_[m.audioKey];
                mp.yaw = pc.yaw();
                if (renderer_) { const core::Vec3 ff = f, uu{0, 1, 0}; mp.fx = fxStepSpawn("FX_KamikazeMine_p.FX.Trail_KamikazeMine_FX", m.pos, ff, uu, fxTeamOf(owner), false); }
            }
        }
    }
    };
    for (size_t q = 0; q < match_.players().size(); ++q)   // every human seat by match player index (offline: the local player only)
        if (seatHuman((int)q)) if (Character* oc = participantPawnMutable((int)q)) pooper(*oc, (int)q);
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (size_t i = 0; i < mines_.size();) {
        KamikazeMine& m = mines_[i];
        m.t += dt;
        const int mo = m.owner >= 0 ? m.owner : localPlayer_;
        bool boom = false, gone = m.t >= 60.0f || m.health <= 0.0f || (m.owner >= 0 ? participantPawn(mo) == nullptr : localDead_);   // LifeSpan 60; destroyed with the owner [PROV]
        if (m.t < 2.0f) {
            // Hover phase (2 s) between 50 and 150 UU above the floor: rise to 1.0 m [PROV height within the range].
            float gy; core::Vec3 gn;
            if (collision_.valid() && collision_.groundHeight(m.pos.x, m.pos.z, m.pos.y + 0.5f, 0.5f, gy, gn)) m.pos.y += (gy + 1.0f - m.pos.y) * std::min(1.0f, 4.0f * dt);
        } else {
            // Seek: closest visible enemy within SearchRadius 2000 UU (disguised pawns ignored), HomingSpeed 2300 UU/s.
            const Character* tgt = nullptr; float best = 20.0f;
            if (matchActive_ && m.owner >= 0) {   // online: the owner's enemies by match player index
                for (size_t q = 0; q < match_.players().size(); ++q) {
                    const Character* c = (int)q == mo || match_.sameTeam((int)q, mo) ? nullptr : participantPawn((int)q);
                    float tt;
                    if (!c) continue;
                    const float d = core::length(c->actorLocation() - m.pos);
                    if (d < best && !(line && line->segmentHit(m.pos, c->actorLocation(), tt))) { best = d; tgt = c; }
                }
            } else if (matchActive_)
                for (const MatchOpponent* o : opponents_) {
                    if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                    float d = core::length(o->pawn().actorLocation() - m.pos), tt;
                    if (d < best && !(line && line->segmentHit(m.pos, o->pawn().actorLocation(), tt))) { best = d; tgt = &o->pawn(); }
                }
            if (tgt) {
                m.vel = core::normalize(tgt->actorLocation() - m.pos) * 23.0f;
                const Character& e = *tgt;
                const core::Vec3 d = e.actorLocation() - m.pos;
                if (std::hypot(d.x, d.z) <= e.cylinderRadius(e.moveForm()) + 0.3f && std::fabs(d.y) <= e.cylinderHalfHeight(e.moveForm()) + 0.3f) boom = true;
            } else m.vel = {0, 0, 0};
            const core::Vec3 next = m.pos + m.vel * dt;
            float tt;
            if (line && core::length(m.vel) > 0.0f && line->segmentHit(m.pos, next, tt)) boom = true;
            else m.pos = next;
        }
        if (boom) radiusDamage(m.pos, 125.0f, 5.0f, mo, "TransGame.TnDamageTypeKamikazeMine");
        // [Systems M08p] FoundTarget once tracking (m.vel set toward a target); explosion (target / wall / shot) vs the fizzle
        if (!boom && !gone) setKamikazeMineAudio(m.audioKey, m.pos, core::length(m.vel) > 0.0f);
        else if (boom || m.health <= 0.0f) onKamikazeMineExploded(m.audioKey, m.pos);
        else onKamikazeMineRemoved(m.audioKey);
        minePresTick(m, boom || gone, boom || m.health <= 0.0f, dt);   // [Gameplay] presentation only
        if (boom || gone) { mines_.erase(mines_.begin() + (long)i); continue; }
        ++i;
    }
}

// ---- Systems M08d: per-form vehicle audio, weapon identity, projectiles, beams (Gameplay reports; Systems plays) ----

// [Gameplay] Kamikaze mine presentation (see World.h MinePres): no sim state is read back from here.
// A (forward, up) frame for its effects: up = world up unless the direction is near vertical (as the projectile frame).
static core::Vec3 mineFrameF(const core::Vec3& d) { const float l = core::length(d); return l > 1e-6f ? d * (1.0f / l) : core::Vec3{1, 0, 0}; }
static core::Vec3 mineFrameU(const core::Vec3& f) { return std::fabs(f.y) > 0.99f ? core::Vec3{0, 0, -1} : core::Vec3{0, 1, 0}; }
void World::minePresTick(const KamikazeMine& m, bool removed, bool exploded, float dt) {
    auto it = minePres_.find(m.audioKey);
    if (it == minePres_.end()) return;
    MinePres& p = it->second;
    const bool tracking = core::length(m.vel) > 0.0f;
    if (m.t >= 2.0f && !tracking && !removed) p.roll += 50000.0f * (core::PI / 32768.0f) * dt;   // Hovering: SetRotation(+ HoverRotationRate x dt)
    if (p.fx >= 0 && !removed) {
        const core::Vec3 f = mineFrameF(tracking ? m.vel : core::forwardFromYawPitch(p.yaw, 0.0f)), u = mineFrameU(f);
        fxStepMove(p.fx, m.pos, f, u);
    }
    if (!removed) return;
    if (p.fx >= 0) { fxStepStop(p.fx); p.fx = -1; }   // the trail finishes its particles
    if (exploded) {
        static const bool testLog = std::getenv("WFC_MINEPRESTEST") != nullptr;
        if (testLog) LOG_INFO("MINEPRES exploded at (%.1f %.1f %.1f) after %.1f s", m.pos.x, m.pos.y, m.pos.z, m.t);
        if (renderer_) { const core::Vec3 f{0, 1, 0}, u = mineFrameU(f); fxStepSpawn("FX_Explosives_p.FX.Exp_2500cb_FX", m.pos, f, u, fxTeamOf(localPlayer_), true); }
    } else {   // FadeOut: FadingOut shrinks the mesh in place
        MineFade fd; fd.pos = m.pos; fd.yaw = p.yaw; fd.roll = p.roll; fd.animT = m.t;
        mineFades_.push_back(fd);
    }
    minePres_.erase(it);
}

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

int World::onWeaponFired(const std::string& weaponClass, bool lowAmmo, bool vehicleForm, const core::Vec3& muzzle, int fireMode) {
    // TnWeapon.PlayFiringSound for any fire type: the robot weapon from its MuzzleFlash socket; a vehicle weapon is the
    // vehicle's (owner-attached at the pawn's audio root) [HIGH: PlaySound on the weapon's owner].
    ensureWeaponAudio(weaponClass);
    SoundCues::Emitter e = vehicleForm ? atPawn({0, 1.4725f, 0}) : atWeapon("MuzzleFlash");
    if (!vehicleForm) e.pos = muzzle;
    return weaponAudio_.fire(cues_, weaponClass, lowAmmo, e, core::length(muzzle - player_.pawn().position()), fireMode);
}

void World::onProjectileSpawned(int key, const std::string& weaponClass, const core::Vec3& pos) {
    ensureWeaponAudio(weaponClass);
    weaponAudio_.projectileSpawned(cues_, key, weaponClass, pos, core::length(pos - listenerPos_));
}

void World::onProjectileMoved(int key, const core::Vec3& pos) { weaponAudio_.projectileMoved(cues_, key, pos); }

int World::preloadSelectionAudio(const std::vector<std::string>& chassisKeys, const std::vector<std::string>& weaponClasses) {
    // Only what this call adds is warmed now (the queue grows one selection at a time); an empty call - loadMapAudio's
    // re-apply after a level load - warms the whole accumulated set for the new level.
    const bool all = chassisKeys.empty() && weaponClasses.empty();
    std::vector<std::string> newChassis, newWeapons;
    for (const std::string& c : chassisKeys)
        if (!c.empty() && std::find(selectionChassis_.begin(), selectionChassis_.end(), c) == selectionChassis_.end()) {
            selectionChassis_.push_back(c); newChassis.push_back(c);
        }
    for (const std::string& w : weaponClasses)
        if (!w.empty() && std::find(selectionWeapons_.begin(), selectionWeapons_.end(), w) == selectionWeapons_.end()) {
            selectionWeapons_.push_back(w); newWeapons.push_back(w);
        }
    const std::string& tag = levelAudio_.level();
    if (!audio_ || tag.empty()) return 0;              // not loaded yet: loadMapAudio re-applies
    const std::vector<std::string>& chassis = all ? selectionChassis_ : newChassis;
    const std::vector<std::string>& weapons = all ? selectionWeapons_ : newWeapons;
    int n = 0;
    for (const std::string& c : chassis)
        if (const CharacterAudioProfile* p = CharacterAudio::find(c)) n += CharacterAudio::warmCues(cues_, *p, tag);
    for (const std::string& w : weapons) {
        n += CharacterAudio::warmWeaponCues(cues_, w, tag);
        n += CharacterAudio::warmHitCues(cues_, CharacterAudio::defaultProfile(), w, tag);
    }
    LOG_INFO("selection audio: +%zu chassis, +%zu weapon classes (%zu / %zu in all) -> %d waves decoding on a worker (level %s)",
             chassis.size(), weapons.size(), selectionChassis_.size(), selectionWeapons_.size(), n, tag.c_str());
    return n;
}

void World::onParticipantAbility(int player, const std::string& id, const std::string& chassisKey, const core::Vec3& pos) {
    {   // [Systems] OnTriggerEffect (every client): queued, spawned by tickBodyNotifyFx (the bot hook may run off the main thread)
        std::lock_guard<std::mutex> lk(abilityFxMu_);
        abilityFxPending_.emplace_back(player, id);
    }
    if (!audio_ || levelAudio_.level().empty()) return;
    const CharacterAudioProfile* prof = CharacterAudio::find(chassisKey);
    const CharacterAudioProfile& p = prof ? *prof : CharacterAudio::defaultProfile();
    // The body's cue set (its footstep / foley events resolve through it), registered once per chassis per level; the
    // match-load selection warm decoded its waves, so this is cache hits.
    if (participantProfiles_.insert(p.key).second) CharacterAudio::loadCues(cues_, p);
    SoundCues::Emitter e;
    e.pos = pos;
    if (participantPositionHook) e.owner = kOwnParticipantBase + player;   // attached: resolveCueOwner asks the hook
    const float dist = core::length(pos - listenerPos_);
    const std::string& trig = CharacterAudio::abilityTriggerSound(AbilityAudio::abilityClass(id));
    if (!trig.empty()) cues_.play(trig.c_str(), e, dist);
    // The Skill_ clip's notifies (the bots play no ability animation; the local pawn's equivalent is onAbilityAnimFallback).
    // The ability's animation: Dodge -> Nav_Boost_* (all variants author the same FS_DEFAULT_JUMP_CHARGED), Whirlwind ->
    // Transform_Whirlwind_ROBO, otherwise Skill_<id> [RE pass 5 s12 addendum table]. (Cloaking's Nav_CloakActivate is not in
    // the exported clips; Hover's audio is the acrobatics loop, a state, not a trigger.)
    const std::string clipName = id == "Dodge" ? "Nav_Boost_F" : id == "Whirlwind" ? "Transform_Whirlwind_ROBO" : "Skill_" + id;
    if (const CharacterAudioProfile::Clip* c = p.clip(clipName)) {
        for (const CharacterAudioProfile::Notify& n : c->notifies) {
            const std::string& cue = p.notifyCue(n);
            if (cue.empty()) continue;
            if (n.t <= 0.0f) cues_.play(cue.c_str(), e, dist);
            else participantNotifies_.push_back({n.t, cue, player, pos});
        }
    }
}

void World::setParticipantBuffAudio(int player, const std::string& buffClass, bool active, int team, const core::Vec3& pos) {
    if (!audio_ || levelAudio_.level().empty()) return;
    AbilityAudio::Owner o;
    o.key = kOwnParticipantBase + player; o.local = false; o.team = team == 1 ? 1 : 0;   // no team: Autobot (faction 0)
    o.at.pos = pos;
    if (participantPositionHook) o.at.owner = kOwnParticipantBase + player;
    o.listenerDist = core::length(pos - listenerPos_);
    abilityAudio_.setBuff(cues_, buffClass, o, active);
    if (buffClass == "TnBuffCloak" && active != (participantCloakAnim_.count(player) != 0)) {
        if (active) participantCloakAnim_.insert(player); else participantCloakAnim_.erase(player);
        playCloakAnimNotifies(active, CharacterAudio::defaultProfile(), o.at, o.listenerDist);
    }
}

void World::setParticipantHoverAudio(int player, int hoverState, const core::Vec3& pos) {
    if (!audio_ || levelAudio_.level().empty()) return;
    SoundCues::Emitter e;
    e.pos = pos;
    if (participantPositionHook) e.owner = kOwnParticipantBase + player;
    abilityAudio_.hoverState(cues_, kOwnParticipantBase + player, hoverState, e, core::length(pos - listenerPos_));
}

void World::tickParticipantBodyAudio(int player, const std::string& chassisKey, const Character& pc,
                                     const VehicleFormSignals& vs, bool alive, float dt) {
    if (!audio_ || levelAudio_.level().empty()) return;
    const CharacterAudioProfile* prof = CharacterAudio::find(chassisKey);
    const CharacterAudioProfile& p = prof ? *prof : CharacterAudio::defaultProfile();
    // At most one new chassis cue set is registered per step (1-2 ms each; 8 at once on the first step of a 16 v 16 was an
    // 11 ms spike). A body whose set is not registered yet stays silent for those few steps.
    if (!participantProfiles_.count(p.key)) {
        if (bodyCueLoadsThisStep_ >= 1) return;
        ++bodyCueLoadsThisStep_;
        participantProfiles_.insert(p.key);
        CharacterAudio::loadCues(cues_, p);                       // waves warmed at match load (preloadSelectionAudio)
    }
    ParticipantBody& b = participantBodies_[player];
    if (b.key != p.key) {                                          // a new body (spawn / class change)
        b.vehicle.stopAll(cues_);
        b.key = p.key;
        b.vehicle = VehicleAudio{};
        b.vehicle.setProfile(p);
        b.foley = RobotFoley{};
        b.foley.setProfile(&p);
        b.form = VehicleFormAudio{};
    }
    const core::Vec3 bodyPos = pc.position() + pc.meshOffset();
    const bool inRange = alive && core::length(bodyPos - listenerPos_) <= kParticipantBodyCullM;
    if (!inRange) {
        if (!b.culled) { b.vehicle.stopAll(cues_); b.form = VehicleFormAudio{}; b.foley = RobotFoley{}; b.foley.setProfile(&p); }
        b.culled = true;
        b.prevTransforming = pc.isTransforming();
        if (vs.nitroSerial >= 0) b.seenNitro = vs.nitroSerial;     // no stale nitro when it comes back into range
        return;
    }
    b.culled = false;
    b.pos = bodyPos;
    auto at = [&](const core::Vec3& up) {
        SoundCues::Emitter e;
        e.pos = bodyPos + up;
        if (participantPositionHook) { e.owner = kOwnParticipantBase + player; e.offset = up; }
        return e;
    };
    const float dist = core::length(bodyPos - listenerPos_);
    // Transform notifies (Transform_ToVehicle_ROBO / Transform_ToRobot_ROBO), as the local pawn's tickCharacterAudio.
    const bool tf = pc.isTransforming();
    if (tf && !b.prevTransforming) { b.transformNotify = 0; b.transformTarget = pc.moveForm(); }
    if (tf) {
        const CharacterAudioProfile::Clip* clip = p.clip(b.transformTarget == Form::Vehicle ? "Transform_ToVehicle_ROBO" : "Transform_ToRobot_ROBO");
        for (int i = b.transformNotify; clip && i < (int)clip->notifies.size(); ++i) {
            const CharacterAudioProfile::Notify& n = clip->notifies[(size_t)i];
            if (clip->length > 0.0f && pc.transformProgress() < n.t / clip->length) break;
            const std::string& cue = p.notifyCue(n);
            if (!cue.empty()) cues_.play(cue.c_str(), at({0, 0, 0}), dist);
            b.transformNotify = i + 1;
        }
    }
    b.prevTransforming = tf;
    // Robot foley from the pawn's own animation.
    std::vector<const char*>& out = partScratch_; out.clear();       // reused: no per-bot allocation
    b.foley.tick(pc, dt, out);
    for (const char* c : out) if (!routeDialogNotify(c, b.foley.voiceProfile(), player, at({0, 0, 0}), dist)) cues_.play(c, at({0, 0, 0}), dist);
    // Vehicle component at AUDIO_ROOT (+147.25 UU), as the local pawn.
    VehicleFormSignals s = vs;
    s.tookOff = s.tookOff || (s.vehicle && b.prevGrounded && !s.onGround && s.velocity.y > 2.0f);   // UpdateJumping take-off edge
    if (vs.nitroSerial >= 0) {                                     // [Systems M09k] StartNitro from the pawn's serial
        s.nitroStarted = s.nitroStarted || (b.seenNitro >= 0 && vs.nitroSerial != b.seenNitro);
        b.seenNitro = vs.nitroSerial;
    }
    b.prevGrounded = s.onGround;
    const VehicleAudio::Input in = b.form.translate(s, nullptr);
    b.vehicle.tick(dt, in, cues_, [&] { return at({0, 1.4725f, 0}); });
}

void World::tickParticipantWeaponAudio(int player, const std::string& cls, unsigned shotSerial, unsigned reloadSerial,
                                       const std::string& actionClip, float actionTime, float dt) {
    if (!audio_ || levelAudio_.level().empty()) return;
    auto it = participantBodies_.find(player);
    if (it == participantBodies_.end()) return;
    ParticipantBody& b = it->second;
    if (cls != b.weaponClass) {                                    // weapon switch / spawn: no replay of old serials
        b.weaponClass = cls;
        ensureWeaponAudio(cls);
        b.weaponSounds.set(cls.empty() ? nullptr : CharacterAudio::weaponAnimSounds(cls));
        b.weaponSeen = false;
    }
    const bool newShot = b.weaponSeen && shotSerial != b.seenShot;
    const bool newReload = b.weaponSeen && reloadSerial != b.seenReload;
    b.seenShot = shotSerial; b.seenReload = reloadSerial; b.weaponSeen = true;
    if (b.culled) { b.weaponSounds.play(WeaponSoundTimeline::Event::Idle); return; }   // out of range: silent, no backlog
    SoundCues::Emitter e;
    e.pos = b.pos + core::Vec3{0, 1.2f, 0};                       // the weapon at chest height (no per-bot weapon socket)
    if (participantPositionHook) { e.owner = kOwnParticipantBase + player; e.offset = {0, 1.2f, 0}; }
    const float dist = core::length(e.pos - listenerPos_);
    if (newReload) b.weaponSounds.play(WeaponSoundTimeline::Event::Reload);
    else if (newShot) b.weaponSounds.play(WeaponSoundTimeline::Event::Fire);
    std::vector<const std::string*>& fired = partFired_; fired.clear();
    b.weaponSounds.tick(dt, fired);
    for (const std::string* q : fired) if (q && !q->empty()) cues_.play(q->c_str(), e, dist);
    const bool viaAbility = actionClip.rfind("Skill_", 0) == 0 || actionClip == "Nav_Boost_F" || actionClip == "Transform_Whirlwind_ROBO";
    std::vector<const char*>& out = partScratch_; out.clear();
    b.foley.actionLayer(viaAbility ? std::string() : actionClip, actionTime, out);
    e.offset = {0, 0, 0}; e.pos = b.pos;
    for (const char* c : out) if (!routeDialogNotify(c, b.foley.voiceProfile(), player, e, dist)) cues_.play(c, e, dist);
}

int World::participantBodiesActive() const {
    int n = 0;
    for (const auto& kv : participantBodies_) if (!kv.second.culled) ++n;
    return n;
}

void World::onParticipantGone(int player) {
    abilityAudio_.pawnDied(cues_, kOwnParticipantBase + player);
    {   // [Systems M09f] its body audio: vehicle loops stop silently; foley / transform state resets
        auto it = participantBodies_.find(player);
        if (it != participantBodies_.end()) { it->second.vehicle.stopAll(cues_); participantBodies_.erase(it); }
    }
    participantCloakAnim_.erase(player);                              // gone: no deactivate notify
    for (size_t i = 0; i < participantNotifies_.size();)            // its pending ability notifies too
        if (participantNotifies_[i].player == player) { participantNotifies_[i] = participantNotifies_.back(); participantNotifies_.pop_back(); }
        else ++i;
}

void World::tickParticipantAudio(float dt) {
    bodyCueLoadsThisStep_ = 0;
    for (size_t i = 0; i < participantNotifies_.size();) {
        ParticipantNotify& n = participantNotifies_[i];
        n.delay -= dt;
        if (n.delay > 0.0f) { ++i; continue; }
        SoundCues::Emitter e;
        e.pos = n.pos;
        if (participantPositionHook) { core::Vec3 now; if (participantPositionHook(n.player, now)) e.pos = now; e.owner = kOwnParticipantBase + n.player; }
        cues_.play(n.cue.c_str(), e, core::length(e.pos - listenerPos_));
        participantNotifies_[i] = participantNotifies_.back();
        participantNotifies_.pop_back();
    }
}

void World::preloadParticipantWeaponAudio(const std::vector<std::string>& classes) {
    for (const std::string& c : classes) {
        if (c.empty()) continue;
        if (std::find(participantWeaponClasses_.begin(), participantWeaponClasses_.end(), c) == participantWeaponClasses_.end())
            participantWeaponClasses_.push_back(c);
        ensureWeaponAudio(c);                        // no-op until the level's audio is loaded (then re-applied there)
    }
}

void World::onParticipantFired(const std::string& weaponClass, const core::Vec3& from) {
    ensureWeaponAudio(weaponClass);                  // safety net; the match-load preload makes this a no-op
    SoundCues::Emitter e;
    e.pos = from;                                    // kWorld at the shot origin (bots draw no held weapon yet)
    weaponAudio_.fire(cues_, weaponClass, false, e, core::length(from - listenerPos_));
}

// [Systems] The weapon impact rule (TnWeaponMesh.CreateImpactEffects / GetImpactSound, RE weapon audit section 1): the surface's
// weapon-specific entry (none authored [CONF data]) -> its PhysicalMaterial's ImpactEffect / ImpactSound (resolved up the Parent chain,
// PhysmatImpacts.inc) -> the weapon mesh's DefaultSquib / DefaultImpactSound. physmat = the hit's PhysicalMaterial path, resolved by
// Gameplay's trace from AssetTools' collision_weapon_physmat.json in the original's order (RE 0x82F1B370: component override -> the
// check result's -> the hit material's -> the first material slot with one for a StaticMeshActor -> the engine default); "" = the
// engine default ENV_PHYSMAT_p.Metal_PHYSMAT, which authors neither. On the MP maps this changes ForceField / ForceField_Blue
// (squib, sound), Water_Coolant (squib), Metal_dest (sound) and Seed's Corruptor squib surface. ImpactThreshold / ImpactReFireDelay
// are the rigid-body collision fields, not the weapon path. WFC_PHYSMATIMPACT=0: the weapon defaults only.
namespace {
const PhysmatImpactRow* physmatImpactRow(const std::string& physmat) {
    static const bool off = std::getenv("WFC_PHYSMATIMPACT") && std::getenv("WFC_PHYSMATIMPACT")[0] == '0';
    if (off || physmat.empty()) return nullptr;
    static const std::unordered_map<std::string, const PhysmatImpactRow*> idx = [] {
        std::unordered_map<std::string, const PhysmatImpactRow*> m;
        for (const PhysmatImpactRow& r : kPhysmatImpacts) m.emplace(r.physmat, &r);
        return m;
    }();
    auto it = idx.find(physmat);
    return it == idx.end() ? nullptr : it->second;
}
}  // namespace

std::string World::impactSquibFor(const std::string& physmat, const std::string& weaponSquib) {
    const PhysmatImpactRow* r = physmatImpactRow(physmat);
    return r && *r->impactEffect ? std::string(r->impactEffect) : weaponSquib;
}

std::string World::impactSoundFor(const std::string& physmat, const std::string& weaponSound) {
    const PhysmatImpactRow* r = physmatImpactRow(physmat);
    return r && *r->impactSound ? std::string(r->impactSound) : weaponSound;
}

void World::onParticipantImpact(const std::string& weaponClass, const core::Vec3& at, int victimPlayer, const std::string& physmat) {
    const float dist = core::length(at - listenerPos_);
    if (victimPlayer >= 0) {
        // A pawn hit: its TnHitEffectPlayer entry for the damage type, retriggered at most every RetriggerTime (the local
        // path's rule, keyed per weapon class) [CONF data; the victim's own SoundEventSet: default profile, as the targets].
        const WeaponHitEffect* he = CharacterAudio::weaponHitEffect(weaponClass);
        if (!he || !he->causesBlood || he->hitEvent.empty()) return;
        const std::pair<int, int> key{victimPlayer, he->index};
        auto it = participantHitEffect_.find(key);
        if (it != participantHitEffect_.end() && hitClock_ - it->second < he->retrigger) return;
        participantHitEffect_[key] = hitClock_;
        const std::string cue = CharacterAudio::defaultProfile().voiceCue(he->hitEvent);
        if (!cue.empty()) cues_.play(cue.c_str(), at, dist);
        return;
    }
    const std::string impact = impactSoundFor(physmat, CharacterAudio::weaponCue(weaponClass, "DefaultImpactSound"));
    if (!impact.empty()) cues_.play(impact.c_str(), at, dist);
}

void World::onProjectileExploded(int key, const std::string& weaponClass, const core::Vec3& pos) {
    weaponAudio_.projectileExploded(cues_, key, weaponClass, pos, core::length(pos - listenerPos_));
}

void World::onProjectileRemoved(int key) { weaponAudio_.projectileRemoved(cues_, key); }

void World::onProjectileHitWall(const std::string& weaponClass, const core::Vec3& pos, bool fuseStarted) {
    weaponAudio_.projectileHitWall(cues_, weaponClass, pos, core::length(pos - listenerPos_), fuseStarted);
}

// TEST ONLY: WFC_TESTABILITIES="Drain,Warcry" replaces the local loadout's ability slots (harness runs of the ability presentation;
// logged once). Unset: the selection's / iconic abilities.
std::vector<std::string> World::testAbilities(const std::vector<std::string>& abilities) {
    static const char* env = std::getenv("WFC_TESTABILITIES");
    if (!env || !*env) return abilities;
    std::vector<std::string> out;
    for (const char* p = env; *p;) {
        const char* e = std::strchr(p, ',');
        const std::string id = e ? std::string(p, e) : std::string(p);
        if (!id.empty()) out.push_back(id);
        if (!e) break;
        p = e + 1;
    }
    static bool logged = false;
    if (!logged) { logged = true; LOG_INFO("TEST ONLY: WFC_TESTABILITIES replaces the local abilities with %s", env); }
    return out;
}

void World::onAbilityTriggered(const std::string& abilityId) {
    abilityAudio_.abilityTriggered(cues_, abilityId, atPawn(), 0.0f);
    spawnAbilityTriggerFx(localPlayer_, abilityId);
}

// [Systems] TnAbility.OnTriggerEffect at OnTriggerEffectSocket (default PositionSocket), attached to the caster's mesh; every client
// plays it (ReplicatedTriggerAbility RepNotify -> ClientOnTriggerAbility; RE MP_ABILITY_CUES_RE efcd305, CONFIRMED CDOs). Only two
// MP abilities author one. Participants within 40 m, as the notify effects. WFC_BODYNOTIFYFX=0 turns it off with the body effects.
void World::spawnAbilityTriggerFx(int player, const std::string& abilityId) {
    struct TriggerFx { const char* ability; const char* effect; const char* socket; };
    static const TriggerFx kTriggerFx[] = {
        {"TnAbilityDrain", "FX_Multiplayer_p.FX.Drain_Blast_FX", "PositionSocket"},   // TransGame.Default__TnAbilityDrain
        {"TnAbilityWarcry", "FX_Multiplayer_p.FX.WarCry_AOE_FX", "Chest_FX"},         // TransGame.Default__TnAbilityWarcry
    };
    static const bool off = std::getenv("WFC_BODYNOTIFYFX") && std::getenv("WFC_BODYNOTIFYFX")[0] == '0';
    static const bool log = std::getenv("WFC_NOTIFYLOG") != nullptr;
    if (off || !renderer_) return;
    const std::string cls = AbilityAudio::abilityClass(abilityId);
    const TriggerFx* t = nullptr;
    for (const TriggerFx& f : kTriggerFx) if (cls == f.ability) t = &f;
    if (!t) return;
    const bool local = player == localPlayer_;
    const Character* c = local ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(player);
    if (!c || (!local && core::length(c->position() - player_.pawn().position()) > 40.0f)) return;
    core::Mat4 sw;
    if (!bodySocketWorld(*c, t->socket, sw)) return;
    const int vid = fxStepSpawn(t->effect, core::Vec3{sw.m[12], sw.m[13], sw.m[14]}, core::normalize(core::Vec3{sw.m[0], sw.m[1], sw.m[2]}),
                                core::normalize(core::Vec3{sw.m[4], sw.m[5], sw.m[6]}), fxTeamOf(player), false);
    if (log) LOG_INFO("ABILITYFX p%d %s %s @%s", player, abilityId.c_str(), t->effect, t->socket);
    if (vid >= 0) bodyFx_.push_back({vid, player, t->socket, 0.0f});
}

void World::setLocalBuffAudio(const std::string& buffClass, bool active, int team) {
    AbilityAudio::Owner o;
    o.key = 0; o.local = true; o.team = team; o.at = atPawn();
    abilityAudio_.setBuff(cues_, buffClass, o, active);
    if (buffClass == "TnBuffCloak" && active != localCloakAnim_) {
        localCloakAnim_ = active;
        playCloakAnimNotifies(active, audioProfile(), o.at, 0.0f);
    }
}

void World::playCloakAnimNotifies(bool on, const CharacterAudioProfile& p, const SoundCues::Emitter& at, float dist) {
    const CharacterAudioProfile::Clip* c = p.clip(on ? "Nav_CloakActivate" : "Nav_CloakDeactivate");
    if (!c) return;                                      // PARTIAL: not in the character export yet
    for (const CharacterAudioProfile::Notify& n : c->notifies) {
        const std::string& cue = p.notifyCue(n);
        if (!cue.empty()) cues_.play(cue.c_str(), at, dist);   // (their small offsets are not delayed here)
    }
}

void World::onLocalPawnBuffsLost() { abilityAudio_.pawnDied(cues_, 0); localCloakAnim_ = false; }   // death: no deactivate notify

void World::onAbilitiesJammed() { abilityAudio_.abilitiesJammed(cues_, atPawn()); }

void World::onLocalKilledPawn(bool headshot, bool robot, const std::string& chassis) {
    abilityAudio_.killedPawn(cues_, headshot, robot, chassis);
}

void World::onTransformFailed() { abilityAudio_.transformFailed(cues_, atPawn()); }

// [Systems M08i glue] Gameplay's ability / buff / hover state -> Systems' AbilityAudio (RE pass 5 s12 / s13).
void World::tickAbilityAudio() {
    const PlayerController& ctl = player_.controller();
    Character& p = player_.pawn();
    if (ctl.abilityTriggerCount() != abilityAudioSerial_) {
        abilityAudioSerial_ = ctl.abilityTriggerCount();
        if (!localDead_) {
            onAbilityTriggered(ctl.lastTriggeredAbility());   // ServerTriggerAbility returns if dead
            // [Systems M08l] The ability's Skill_ animation notifies when Gameplay plays no such clip (OnTriggerAnimParams).
            const assets::SkinnedModel* am = p.currentModel();
            const std::string skill = "Skill_" + ctl.lastTriggeredAbility();
            const bool playing = p.actionClipIndex() >= 0 && am && (size_t)p.actionClipIndex() < am->clips.size() && am->clips[(size_t)p.actionClipIndex()].name == skill;
            if (!playing && (skill == "Skill_Shockwave" || skill == "Skill_Warcry" || skill == "Skill_SpawnSentry" ||
                             skill == "Skill_TransformDisruptor")) onAbilityAnimFallback(skill);
        }
    }
    if (ctl.abilitiesJammedCount() != jammedAudioSerial_) { jammedAudioSerial_ = ctl.abilitiesJammedCount(); onAbilitiesJammed(); }
    if (ctl.transformFailedCount() != transformFailAudioSerial_) {
        transformFailAudioSerial_ = ctl.transformFailedCount();
        onTransformFailed();
    }
    {   // [Systems M08k] TnChargeWeapon (Plasma Cannon) charge loops / fizzle; the local roller mine
        const Weapon& w = p.weapon();
        const std::string wc = w.def ? "TransContent.TnWeapon" + std::string(w.def->id) : std::string();
        setChargeWeaponAudio(wc, w.charge() ? w.chargeState : 0);
        if (w.charge()) {
            if (chargeFizzleAudio_ >= 0 && (long long)w.chargeFizzle != chargeFizzleAudio_) onChargeFizzle(wc);
            chargeFizzleAudio_ = (long long)w.chargeFizzle;
        } else chargeFizzleAudio_ = -1;
        // [integration 09c] Systems M09h glue: per-instance ability-actor audio for any owner (Gameplay 26e per-owner barriers /
        // sentries; the local roller mine / guided missile). An instance not reported this tick stops silently.
        beginAbilityActorAudio();
        for (const BarrierState& b : barriers_)
            if (b.owner >= 0) {
                AbilityActorState st; st.alive = b.alive && b.delay < 0.0f; st.fading = st.alive && b.fade >= 0.0f; st.pos = b.pos;
                setAbilityActorAudio(AbilityActor::Barrier, b.owner, b.owner == localPlayer_, st);
            }
        for (const Sentry& se : sentries_)
            if (se.owner >= 0) {
                AbilityActorState st; st.alive = se.alive && se.delay < 0.0f; st.target = se.target; st.pos = se.pos;
                setAbilityActorAudio(AbilityActor::Sentry, se.owner, se.owner == localPlayer_, st);
            }
        for (const RollerMine& rm : rollers_)   // Gameplay 26l: per owner
            if (rm.owner >= 0) { AbilityActorState st; st.alive = rm.alive && rm.delay < 0.0f; st.age = rm.t; st.pos = rm.pos;
                                 setAbilityActorAudio(AbilityActor::RollerMine, rm.owner, rm.owner == localPlayer_, st); }
        if (!onlineSeats_) { AbilityActorState st; st.alive = missile_.alive; st.pos = missile_.pos;
          setAbilityActorAudio(AbilityActor::GuidedMissile, localPlayer_, true, st); }
        for (size_t q = 0; q < seatMissiles_.size(); ++q)   // the seats' missiles (the flight loop per owner)
            if (seatMissiles_[q].alive) { AbilityActorState st; st.alive = true; st.pos = seatMissiles_[q].pos;
                                          setAbilityActorAudio(AbilityActor::GuidedMissile, (int)q, (int)q == localPlayer_, st); }
        endAbilityActorAudio();
    }
    {   // [Systems M08l] the one-shot action layer's sound notifies (melee, Skill_*, whirlwind, grenade throw)
        const assets::SkinnedModel* am = p.currentModel();
        const bool on = !localDead_ && p.form() == Form::Robot && p.actionClipIndex() >= 0 && am && (size_t)p.actionClipIndex() < am->clips.size();
        onActionClip(on ? am->clips[(size_t)p.actionClipIndex()].name : std::string(), p.actionTime());
    }
    if (localDead_ != abilityAudioDead_) {                          // death: loops stop, no Unapply / land
        abilityAudioDead_ = localDead_;
        if (localDead_) onLocalPawnBuffsLost();
    }
    if (localDead_) return;
    // The buffed pawn's team picks the cloak cues (no team: Autobot 0).
    const int team = matchActive_ && localPlayer_ >= 0 && match_.players()[(size_t)localPlayer_].team <= 1
                         ? match_.players()[(size_t)localPlayer_].team : 0;
    setLocalBuffAudio("TnBuffCloak", p.cloakRemain_ > 0.0f, team);
    setLocalBuffAudio("TnBuffWarcryBase", p.warcryRemain_ > 0.0f, team);
    setLocalBuffAudio("TnBuffHardLocked", p.hardLockedRemain_ > 0.0f, team);
    setLocalBuffAudio("TnBuffTransformDisruptor", p.transformDisruptRemain_ > 0.0f, team);
    setLocalHoverAudio(p.hoverState_);
    if (p.dodgeWallHits_ != dodgeWallAudio_) { dodgeWallAudio_ = p.dodgeWallHits_; if (p.form() == Form::Robot) onDodgeHitWall(); }   // [Systems M08n]
    setOvershieldAudio(p.health().overshield());                 // [Systems M08n] OvershieldOffSound at 0
    if (p.isDodging() && !dodgeAudio_) onDodgeStarted();      // [Systems M08k] TnAbilityDodge: Nav_Boost_* FS_DEFAULT_JUMP_CHARGED
    dodgeAudio_ = p.isDodging();
}

void World::setChargeWeaponAudio(const std::string& cls, int state) { weaponAudio_.chargeState(cues_, cls, state, atWeapon("MuzzleFlash")); }

void World::onChargeFizzle(const std::string& cls) { weaponAudio_.chargeFizzle(cues_, cls, atWeapon("MuzzleFlash")); }

void World::setRollerMineAudio(bool alive, float t, const core::Vec3& pos) {
    abilityAudio_.rollerMine(cues_, alive, t, pos, core::length(pos - listenerPos_));
}

void World::setGuidedMissileAudio(bool alive, const core::Vec3& pos) {
    abilityAudio_.guidedMissile(cues_, alive, pos, core::length(pos - listenerPos_));
}

void World::setBarrierAudio(bool alive, bool fading, const core::Vec3& pos) {
    abilityAudio_.barrier(cues_, alive, fading, pos, core::length(pos - listenerPos_));
}

void World::setSentryAudio(bool alive, int target, const core::Vec3& pos) {
    abilityAudio_.sentry(cues_, alive, target, pos, core::length(pos - listenerPos_));
}

void World::setOvershieldAudio(float os) { abilityAudio_.overshield(cues_, os, atPawn()); }

void World::onDodgeHitWall() { abilityAudio_.dodgeHitWall(cues_, atPawn()); }

void World::onGrenadeToss(const std::string& cls, bool refused) {
    ensureWeaponAudio(cls);
    weaponAudio_.weaponEvent(cues_, cls, refused ? "WP_NoAmmoFire" : "WP_Fire", atPawn());
}

void World::setKamikazeMineAudio(int key, const core::Vec3& pos, bool targetFound) {
    abilityAudio_.kamikazeMine(cues_, key, pos, targetFound, core::length(pos - listenerPos_));
}

void World::onKamikazeMineExploded(int key, const core::Vec3& pos) {
    abilityAudio_.kamikazeMineExploded(cues_, key, pos, core::length(pos - listenerPos_));
}

void World::onKamikazeMineRemoved(int key) { abilityAudio_.kamikazeMineRemoved(cues_, key); }

void World::onPawnHitEffect(const std::string& damageType, const std::string& victimChassis, int victimKey, const core::Vec3& pos) {
    abilityAudio_.pawnHitEffect(cues_, damageType, victimChassis, victimKey, pos, core::length(pos - listenerPos_), hitClock_);
}

void World::onPawnDeath(const std::string& chassisId, bool vehicleForm, const std::string& damageType, const core::Vec3& pos) {
    ++deathPresentations_;
    if (!AbilityAudio::deathCue(chassisId, vehicleForm, damageType).empty()) ++deathCues_;
    abilityAudio_.pawnDeath(cues_, chassisId, vehicleForm, damageType, pos, core::length(pos - listenerPos_));
    // [Gameplay] the death light comes with the corpse (requestCorpse / spawnCorpse: the light of the picked death entry)
}

void World::onLocalKillstreakActivated(const std::string& id, int team) {
    levelAudio_.match().killstreakActivated(id, MatchAudio::StreakRole::Self, team);
}

void World::onParticipantKillstreakActivated(const std::string& id, int activatorTeam, bool sameTeamAsLocal) {
    levelAudio_.match().killstreakActivated(id, sameTeamAsLocal ? MatchAudio::StreakRole::Friendly : MatchAudio::StreakRole::Enemy,
                                            activatorTeam);
}

void World::beginAbilityActorAudio() { abilityAudio_.markActorsUnseen(); }

void World::setAbilityActorAudio(AbilityActor kind, int owner, bool local, const AbilityActorState& s) {
    if (!audio_ || levelAudio_.level().empty()) return;
    const int key = local ? 0 : kOwnParticipantBase + owner;
    const float d = core::length(s.pos - listenerPos_);
    switch (kind) {
    case AbilityActor::RollerMine: abilityAudio_.rollerMine(cues_, key, s.alive, s.age, s.pos, d); break;
    case AbilityActor::GuidedMissile: abilityAudio_.guidedMissile(cues_, key, s.alive, s.pos, d); break;
    case AbilityActor::Barrier: abilityAudio_.barrier(cues_, key, s.alive, s.fading, s.pos, d); break;
    case AbilityActor::Sentry: abilityAudio_.sentry(cues_, key, s.alive, s.target, s.pos, d); break;
    }
}

void World::onAbilityActorExploded(AbilityActor kind, int owner, bool local, const core::Vec3& pos) {
    if (!audio_ || levelAudio_.level().empty()) return;
    const int key = local ? 0 : kOwnParticipantBase + owner;
    const float d = core::length(pos - listenerPos_);
    if (kind == AbilityActor::RollerMine) abilityAudio_.rollerMineExploded(cues_, key, pos, d);
    else if (kind == AbilityActor::GuidedMissile) abilityAudio_.guidedMissileExploded(cues_, key, pos, d);
}

void World::endAbilityActorAudio() { abilityAudio_.sweepUnseenActors(cues_); }

void World::onSentryShot(const core::Vec3& muzzle, bool worldHit, const core::Vec3& hit) {
    abilityAudio_.sentryShot(cues_, muzzle, core::length(muzzle - listenerPos_), worldHit, hit, core::length(hit - listenerPos_));
}

void World::onGuidedMissileExploded(const core::Vec3& pos) {
    abilityAudio_.guidedMissileExploded(cues_, pos, core::length(pos - listenerPos_));
}

void World::onRollerMineExploded(const core::Vec3& pos) { abilityAudio_.rollerMineExploded(cues_, pos, core::length(pos - listenerPos_)); }

void World::onActionClip(const std::string& clip, float t) {
    std::vector<const char*> out;
    robotFoley_.actionLayer(clip, t, out);
    for (const char* c : out) if (!routeDialogNotify(c, audioProfile(), localPlayer_, atPawn(), 0.0f)) cues_.play(c, atPawn(), 0.0f);
}

void World::onAbilityAnimFallback(const std::string& clip) {
    std::vector<const char*> out;
    robotFoley_.clipNotifiesOnce(clip, out);
    for (const char* c : out) if (!routeDialogNotify(c, audioProfile(), localPlayer_, atPawn(), 0.0f)) cues_.play(c, atPawn(), 0.0f);
}

bool World::routeDialogNotify(const char* cue, const CharacterAudioProfile& who, int owner, const SoundCues::Emitter& at, float distM) {
    // The foley paths see only some clips; the dialog notifies fire from every clip's window instead (onAnimNotifyWindow), so here
    // they are only kept from playing as plain cues.
    (void)who; (void)owner; (void)at; (void)distM;
    return std::strncmp(cue, "dialog:", 7) == 0;
}

void World::onDodgeStarted() {
    std::vector<const char*> out;
    robotFoley_.dodgeStarted(out);
    for (const char* c : out) cues_.play(c, atPawn(), 0.0f);
}

void World::setLocalHoverAudio(int hoverState) { abilityAudio_.hoverState(cues_, hoverState, atPawn(), 0.0f); }

void World::onDrainTick(int targets) { abilityAudio_.drainSourceTick(cues_, atPawn(), 0.0f, true, targets); }

void World::onDrainVictimTick(const core::Vec3& p) { abilityAudio_.drainTargetTick(cues_, p, core::length(p - listenerPos_)); }

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

void World::qaFreezeBots(bool on) {
    if (!qaEnabled()) return;
    // Freezing parks the bots: an empty intent alone left a vehicle-form bot coasting ~5 m on its speed (QABOT freeze check).
    // Horizontal speed only; gravity still settles a pawn in the air. DEV / QA tool path, not normal play.
    if (on && !qaBotsFrozen_)
        for (MatchOpponent* o : opponents_) { core::Vec3& v = o->pawn().velocity(); v.x = 0.0f; v.z = 0.0f; }
    qaBotsFrozen_ = on;
}
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
    if (renderer_) renderer_->notifyCameraCut("teleport");
    LOG_INFO("qa: teleported to aim (%.1f %.1f %.1f)", p.x, p.y, p.z);
}

std::vector<World::QaBotLabel> World::qaBotLabels() const {
    std::vector<QaBotLabel> out;
    if (!botOverlay_ && (!qaEnabled() || !qaBotOverlay_)) return out;
    for (const BotBrain& b : bots_) {
        const Character* c = participantPawn(b.player);
        if (!c) continue;
        // Smart bots show their Smart action (hunt / retreat / cover / hold) and the AI; health in percent (playtest / duel review).
        static const char* kAction[] = {"", " HUNT", " RETREAT", " TO-COVER", " IN-COVER"};
        const SmartBot* sb = smartBot(b.player);
        const int hp = c->health().max > 0.0f ? (int)std::lround(100.0f * c->health().current / c->health().max) : 0;
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s [%s %s] %s%s tgt %d hp %d%% wp %zu/%zu%s", match_.players()[(size_t)b.player].name.c_str(), sb ? "Smart" : "Classic",
                      botDifficultyName(b.difficulty), botGoalName(b.goal.kind), sb && sb->action < 5 ? kAction[sb->action] : "", b.target, hp, b.wp, b.path.size(),
                      b.stuckLevel > 0 ? " STUCK" : "");
        QaBotLabel L{c->position() + core::Vec3{0, 5.0f, 0}, buf, b.player, match_.settings().teamGame ? match_.players()[(size_t)b.player].team : -1};
        if (b.target >= 0) if (const Character* t = participantPawn(b.target)) { L.hasTarget = true; L.targetPos = t->position() + core::Vec3{0, 2.0f, 0}; }
        if (sb && sb->action > 0) { L.action = sb->action; L.actionPos = sb->actionPos; }
        out.push_back(L);
    }
    return out;
}

// [Gameplay] Kamikaze mines: KMine_Open once from spawn, KMine_Seek looping from HoverTime (2 s), the spin about the mesh's
// forward axis; FadingOut ghosts at their last pose, scaled 1 -> 0.01 over 1 s (Lerp).
void World::drawMines(render::IRenderer& r) const {
    minePresDraws_ = 0;
    if (!mineModel_.valid()) return;
    const int open = mineModel_.clipByName("KMine_Open"), seek = mineModel_.clipByName("KMine_Seek");
    auto pose = [&](float t, render::MeshData& md) {
        const bool seeking = t >= 2.0f && seek >= 0;
        const int clip = seeking ? seek : open;
        if (clip < 0) { if (md.positions.empty()) { assets::LocalPose lp; std::vector<core::Mat4> g; assets::samplePose(mineModel_, 0, 0.0f, false, lp); assets::skinPose(mineModel_, lp, g, md); } return; }
        assets::LocalPose lp; std::vector<core::Mat4> g;
        assets::samplePose(mineModel_, clip, seeking ? t - 2.0f : t, seeking, lp);
        assets::skinPose(mineModel_, lp, g, md);
    };
    auto place = [&](const core::Vec3& at, float yaw, float roll, float scale) {
        return core::Mat4::translate(at) * core::Mat4::rotateY(yaw + core::config::kMeshYawOffset) * core::Mat4::rotateX(roll) * core::Mat4::scale(core::Vec3{scale, scale, scale});
    };
    for (const KamikazeMine& m : mines_) {
        auto it = minePres_.find(m.audioKey);
        if (it == minePres_.end()) continue;
        const MinePres& p = it->second;
        // KMine_Open clamps at its end (constant until the seek starts): skinned again only while it plays or loops
        const float openDur = open >= 0 ? mineModel_.clips[(size_t)open].duration : 0.0f;
        if (p.mesh.positions.empty() || m.t < openDur + 0.1f || m.t >= 2.0f) { pose(m.t, p.mesh); p.posedAt = m.t; }
        r.drawDynamicMesh(p.mesh, place(m.pos, p.yaw, p.roll, 1.0f), core::Vec3{1, 1, 1});
        ++minePresDraws_;
    }
    for (const MineFade& fd : mineFades_) {
        if (!fd.posed) { pose(fd.animT, fd.mesh); fd.posed = true; }
        const float s = 1.0f + (0.01f - 1.0f) * std::min(1.0f, fd.age / 1.0f);
        r.drawDynamicMesh(fd.mesh, place(fd.pos, fd.yaw, fd.roll, s), core::Vec3{1, 1, 1});
        ++minePresDraws_;
    }
    static const bool testLog = std::getenv("WFC_MINEPRESTEST") != nullptr;   // diagnostics: one line per second with mines up
    static int frames = 0;
    if (testLog && minePresDraws_ > 0 && ++frames % 60 == 1) {
        int seeking = 0; for (const KamikazeMine& m : mines_) seeking += m.t >= 2.0f;
        LOG_INFO("MINEPRES drew %d mine mesh(es): %zu live (%d past the hover, %zu trails), %zu fading; explosion effects so far %d", minePresDraws_, mines_.size(), seeking,
                 (size_t)std::count_if(minePres_.begin(), minePres_.end(), [](const auto& kv) { return kv.second.fx >= 0; }), mineFades_.size(), projectileFxExplosions_);
    }
}

// ---- [Gameplay] corpses (GAMEPLAY_DEATH_BODY_DESIGN.md patch 1; RE pass 5 s12 addenda 10, 17-25) ----
namespace {
const DeathEntryDef* deathBlueprint(const std::string& name, size_t& n) {
    if (name == "SharedCarDeaths") { n = sizeof(kSharedCarDeaths) / sizeof(kSharedCarDeaths[0]); return kSharedCarDeaths; }
    if (name == "SharedTankDeaths") { n = sizeof(kSharedTankDeaths) / sizeof(kSharedTankDeaths[0]); return kSharedTankDeaths; }
    if (name == "SharedJetDeaths") { n = sizeof(kSharedJetDeaths) / sizeof(kSharedJetDeaths[0]); return kSharedJetDeaths; }
    n = sizeof(kSharedRobotDeaths) / sizeof(kSharedRobotDeaths[0]); return kSharedRobotDeaths;   // SharedRobotDeaths (and unknown sets)
}
const ChassisDeathDef* chassisDeath(const std::string& id) {
    for (const ChassisDeathDef& c : kChassisDeaths) if (id == c.chassis) return &c;
    return nullptr;
}
const char* damageDeathType(const std::string& dmg) {
    for (const DamageDeathTypeDef& d : kDamageDeathTypes) if (dmg == d.damage) return d.death;
    return "";
}
}  // namespace

void World::requestCorpse(const Character& pawn, int player, int owner, bool ai, const std::string& damageType) {
    const assets::SkinnedModel* mdl = pawn.currentModel();
    static const bool noCorpse = std::getenv("WFC_NOCORPSE") != nullptr;   // harness (perf bisects): the death light only, no body / gibs / wreck
    if (noCorpse) mdl = nullptr;
    if (!mdl || !mdl->valid()) { spawnDeathLight(pawn.chassis().id, pawn.moveForm() == Form::Vehicle, damageType, pawn.actorLocation()); return; }   // no body: the light alone
    CorpseRequest q;
    q.player = player; q.owner = owner; q.model = mdl; q.world = pawn.meshMatrix(pawn.form()); q.pose = pawn.displayPose();
    q.chassis = pawn.chassis().id; q.damageType = damageType; q.vehicle = pawn.moveForm() == Form::Vehicle;
    q.jet = q.vehicle && pawn.vehicleParams().form == VehicleFormType::Jet; q.ai = ai;
    q.airborne = !q.vehicle && !pawn.onGround(); q.hovering = !q.vehicle && pawn.hoverState_ > 0;
    q.pos = pawn.actorLocation(); q.vel = pawn.velocity(); q.fwd = core::forwardFromYawPitch(pawn.yaw(), 0.0f);
    if (pawn.chassis().positionSocket.valid) { q.socketBone = pawn.chassis().positionSocket.bone; q.socketLocal = pawn.chassis().positionSocket.local; }
    {   // "content/<pkg>/<mesh>.gltf" -> "<pkg>.<mesh>" (Systems' kDeathSockets key)
        const std::string& g = q.vehicle ? pawn.chassis().vehicleSkelGltf : pawn.chassis().robotSkelGltf;
        const size_t s1 = g.rfind('/'), s0 = s1 == std::string::npos ? std::string::npos : g.rfind('/', s1 - 1), dot = g.rfind('.');
        if (s0 != std::string::npos && dot != std::string::npos && dot > s1) q.meshKey = g.substr(s0 + 1, s1 - s0 - 1) + "." + g.substr(s1 + 1, dot - s1 - 1);
    }
    { auto hd = lastHitDir_.find(player); if (hd != lastHitDir_.end()) { q.hitDir = hd->second; q.haveHitDir = true; lastHitDir_.erase(hd); } }
    // The killing hit (the newest kill / suicide record for this victim): headshot and the killer's position (the momentum types).
    for (auto it = match_.gameplayEvents().rbegin(); it != match_.gameplayEvents().rend(); ++it) {
        if (it->victim != player || (it->type != GameplayEventType::Kill && it->type != GameplayEventType::Suicide && it->type != GameplayEventType::EnvironmentDeath)) continue;
        q.headshot = it->headshot;
        const Character* k = it->instigator == localPlayer_ ? (localDead_ ? nullptr : &player_.pawn()) : participantPawn(it->instigator);
        if (k && it->instigator != player) { q.killerPos = k->actorLocation(); q.haveKiller = true; }
        break;
    }
    presentHook([this, q] {
        spawnCorpse(q);
        if (q.player == localPlayer_) player_.controller().beginDeathCamera(q.vehicle, std::atan2(-q.fwd.x, -q.fwd.z));
    });
}

void World::spawnCorpse(const CorpseRequest& q) {
    if (corpses_.size() >= 32) { for (auto& f : corpses_.front().playFx) fxStepStop(f.first); corpses_.erase(corpses_.begin()); }   // a cap (oldest first)
    Corpse c;
    c.player = q.player; c.owner = q.owner; c.model = q.model; c.world = q.world; c.start = q.pose; c.vehicle = q.vehicle; c.jet = q.jet;
    c.pos = q.pos; c.vel = q.vel; c.chassis = q.chassis; c.socketBone = q.socketBone; c.socketLocal = q.socketLocal; c.meshKey = q.meshKey;
    c.rng = 0x9e3779b9u ^ (unsigned)(q.player * 7919) ^ (unsigned)(match_.matchTime() * 1000.0f);
    // TnDeathFactory.Create -> Picker.Pick: the first entry, in authored order, all of whose Types are active; none -> the default death
    // (no animation, TotalTime 1.0). TnVehicleDeathPicker: Car (car / truck) / Tank + Moving (over 50 UU/s) or Stationary; Plane.
    // TnRobotDeathPicker: Martyrdom (not modelled), HeadShot, Hovering, Airborne, one momentum type, the damage type's DamageDeathType.
    const ChassisDeathDef* cd = chassisDeath(q.chassis);
    const std::string bpName = cd ? (q.vehicle ? cd->vehicleDeaths : cd->robotDeaths) : (q.vehicle ? "SharedCarDeaths" : "SharedRobotDeaths");
    size_t n = 0; const DeathEntryDef* bp = deathBlueprint(q.vehicle && !*bpName.c_str() ? std::string("SharedCarDeaths") : bpName, n);
    std::vector<std::string> active;
    const bool moving = std::hypot(q.vel.x, q.vel.z) > 0.5f;
    if (q.vehicle) {
        if (q.jet) active.push_back("Plane");
        else if (bpName == "SharedTankDeaths") { active.push_back("Tank"); active.push_back(moving ? "MovingTank" : "StationaryTank"); }
        else { active.push_back("Car"); active.push_back(moving ? "MovingCar" : "StationaryCar"); }
    } else {
        if (q.headshot) active.push_back("HeadShot");
        if (q.hovering) active.push_back("Hovering");
        if (q.airborne) active.push_back("Airborne");
        // Momentum: TearOffMomentum yaw in the pawn's frame (the push away from the killer; PROV until the hit momentum is kept).
        core::Vec3 m = q.haveHitDir ? q.hitDir : q.haveKiller ? q.pos - q.killerPos : core::Vec3{0, 0, 0}; m.y = 0.0f;
        const float ml = core::length(m);
        const float cosA = ml > 1e-3f ? core::dot(m * (1.0f / ml), core::Vec3{q.fwd.x, 0.0f, q.fwd.z}) : 1.0f;
        const core::Vec3 rgt = core::normalize(core::cross(core::Vec3{q.fwd.x, 0.0f, q.fwd.z}, core::Vec3{0, 1, 0}));
        const float sinA = ml > 1e-3f ? core::dot(m * (1.0f / ml), rgt) : 0.0f;
        active.push_back(cosA >= 0.7071f ? "ForwardMomentum" : cosA <= -0.7071f ? "BackwardMomentum" : sinA > 0.0f ? "RightMomentum" : "LeftMomentum");
        const char* dt = damageDeathType(q.damageType);
        if (*dt) active.push_back(dt);
    }
    const DeathEntryDef* e = nullptr;
    for (size_t i = 0; i < n && !e; ++i) {
        bool all = bp[i].nTypes > 0;
        for (int k = 0; k < bp[i].nTypes && all; ++k) all = std::find(active.begin(), active.end(), bp[i].types[k]) != active.end();
        if (all) e = &bp[i];
    }
    float tMin = 1.0f, tMax = 1.0f;
    if (e) {
        c.entry = e->types[0]; c.animName = e->anim; c.mods = e->mods; c.blendIn = e->blendIn;
        tMin = e->tMin; tMax = e->tMax;
        if (q.ai && (e->aiMin > 0.0f || e->aiMax > 0.0f)) { tMin = e->aiMin; tMax = e->aiMax; }   // TotalTimeForAI when authored
        c.headShrink = (e->mods & 1u) != 0; c.defrag = (e->mods & 2u) != 0;
        if (e->mods & 4u) c.fxAt = c.frand() * 0.5f;   // PlayEffect after Delay U(0, 0.5)
    }
    c.total = tMin + (tMax - tMin) * c.frand();
    // The entry's custom gibs and effects (DeathFx.inc): gibs blueprint effects, chunks, skeletal gibs, decal, PlayEffect.
    if (e) {
        const std::string bpFx = q.vehicle && bpName.empty() ? std::string("SharedCarDeaths") : bpName;
        for (int pass = 0; pass < 2 && c.fxRow < 0; ++pass)
            for (size_t i = 0; i < sizeof(kDeathFx) / sizeof(kDeathFx[0]); ++i)
                if (bpFx == (pass == 0 ? kDeathFx[i].blueprint : (q.vehicle ? "SharedCarDeaths" : "SharedRobotDeaths")) && !std::strcmp(kDeathFx[i].entry, c.entry)) { c.fxRow = (int)i; break; }
    }
    if (c.fxRow >= 0) {
        const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];
        c.noGibs = R.nGibFx == 0 && !R.chunks && !R.skeletalGibs; c.noSkelGib = !R.skeletalGibs; c.noDecal = !R.decals;
        float dm = 0.0f; for (int i = 0; i < R.nPlay; ++i) dm = std::max(dm, R.play[i].delayMax);
        if (c.fxAt >= 0.0f && dm <= 0.0f) c.fxAt = 0.0f;   // no authored Delay (vehicles, Energy): at once (the frand draw stays)
    }
    // TnPawn.Dying.BeginState: ShouldInstaGib after PlayDeath (CanInstaGib on every playable blueprint): the damage type's ShouldGib.
    {   // (both forms: the same pawn and blueprint)
        const std::string& d = q.damageType;
        const bool insta = d == "Engine.DmgType_Fell" || d == "TransGame.TnDamageTypeBruteBackpack" || d == "TransGame.TnDamageTypeDeathAnim" ||
                           (d == "TransGame.TnDamageTypeEmpShotgun" && q.haveKiller && core::length(q.killerPos - q.pos) <= 10.0f);
        if (insta) { c.instaGib = true; c.total = 0.0f; }
    }
    // The animation: an AnimSet chooser group (weighted pick of the anims this model has), else a clip of that name, else none (the
    // animator's lookup fails: the pose holds, as the default death).
    if (*c.animName) {
        int wsum = 0;
        for (const DeathChooserDef& ch : kDeathChoosers) if (!std::strcmp(ch.group, c.animName) && ch.weight > 0 && c.model->clipByName(ch.anim) >= 0) wsum += ch.weight;
        if (wsum > 0) {
            int pick = (int)(c.frand() * (float)wsum);
            for (const DeathChooserDef& ch : kDeathChoosers)
                if (!std::strcmp(ch.group, c.animName) && ch.weight > 0 && c.model->clipByName(ch.anim) >= 0) { if ((pick -= ch.weight) < 0) { c.clip = c.model->clipByName(ch.anim); break; } }
        } else c.clip = c.model->clipByName(c.animName);
    }
    if (c.headShrink) c.headNode = c.model->nodeByName("C_Spine04_Head_XB");
    c.pose = c.start;
    assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
    static const bool log = std::getenv("WFC_CORPSETEST") != nullptr;
    if (log) LOG_INFO("CORPSE p%d %s %s: entry %s, anim %s (clip %d%s), TotalTime %.2f s, damage %s%s", q.player, q.chassis.c_str(), q.vehicle ? (q.jet ? "jet" : "vehicle") : "robot",
                      c.entry, *c.animName ? c.animName : "none", c.clip, c.clip >= 0 ? (" " + c.model->clips[(size_t)c.clip].name).c_str() : "", c.total,
                      q.damageType.c_str(), q.headshot ? ", headshot" : "");
    spawnDeathLight(q.chassis, q.vehicle, q.damageType, q.pos, (c.mods & 8u) ? 1 : 0);   // the light of the same entry
    corpses_.push_back(std::move(c));
    ++corpsesMade_;
}

static bool deathFxLogOn() { static const bool on = std::getenv("WFC_CORPSETEST") || std::getenv("WFC_DEATHFXLOG"); return on; }

bool World::Corpse::deathSocketWorld(const char* socket, core::Vec3& out) const {
    if (meshKey.empty() || !model || glob.empty()) return false;
    for (const DeathSocket& d : kDeathSockets) {
        if (meshKey != d.mesh || std::strcmp(socket, d.socket)) continue;
        const int bn = model->nodeByName(d.bone);
        if (bn < 0 || (size_t)bn >= glob.size()) return false;
        out = core::transformPoint(world * glob[(size_t)bn], core::Vec3{d.x, d.z, d.y} * 0.01f);   // UE bone frame -> the export's
        return true;
    }
    return false;
}

core::Vec3 World::Corpse::playAt(const char* so) const {
    core::Vec3 at = pos + core::Vec3{0, vehicle ? 1.0f : 1.5f, 0};   // BoosterSocket_C / TargetingSocket [PROV]
    const char* bone = !std::strcmp(so, "Head_FX") ? "C_Spine04_Head_XB" : !std::strcmp(so, "Chest_FX") ? "C_Spine02_Lumbar02_XB" : nullptr;
    if (!std::strcmp(so, "VaporTrailSocket_R")) at = pos;
    const int bn = bone && model ? model->nodeByName(bone) : -1;
    if (bn >= 0 && (size_t)bn < glob.size()) { const core::Mat4 w = world * glob[(size_t)bn]; at = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
    else if (bone && !std::strcmp(so, "Head_FX")) at = pos + core::Vec3{0, 3.0f, 0};
    deathSocketWorld(so, at);   // the authored socket (kDeathSockets) overrides the placement above
    return at;
}

// The gib meshes: the static chunks and the two skeletal gib templates (once; at the match load and at the first gib).
// The vehicle-death gib hull of every participant chassis (the cache gibCorpse fills; same load, textures and prewarm).
// PC ADAPTATION (playtest 3, Integration decision 2026-10-24): meshes that collide per-poly (their render triangles at every colliding
// placement of props.json) instead of their authored simple hull. One entry: Streets' ENV_CraterUpNoRing_STAT, whose hull lens is
// 8-10 m wider than the visible crater and whose rim lip our movement model (no cylinder sweep) cannot ramp onto.
void World::applyPerPolyCollision(render::MeshData& m, const std::string& mapDir, bool recordProbe, bool apply) {
    static const struct { const char* map; const char* mesh; } kPerPoly[] = {{"MP_IAC_Streets", "ENV_IAC_Ground_p.Debris.ENV_CraterUpNoRing_STAT"}};
    for (const auto& e : kPerPoly) {
        if (mapName_ != e.map) continue;
        std::string text; assets::Json props;
        if (!readTextFile(mapDir + "props.json", text) || !assets::Json::parse(text, props)) { LOG_WARN("per-poly collision: %sprops.json unreadable", mapDir.c_str()); continue; }
        int removed = 0, added = 0;
        for (render::SubMesh& sm : m.subs) if (sm.sourceMesh == e.mesh && sm.indexCount > 0) {   // the simple hull
            if (recordProbe && !apply) for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) { const uint32_t v = m.indices[i]; perPolyTris_.push_back({m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}); }
            if (apply) { removed += (int)sm.indexCount / 3; sm.indexCount = 0; }
        }
        render::MeshData rm; std::string gltf;
        const assets::Json& list = props["props"];
        for (size_t i = 0; i < list.size(); ++i) {
            const assets::Json& p = list[i];
            if (p["mesh"].asString() != e.mesh || !p["collide"].asBool(false) || !p["block"].asBool(false)) continue;
            if (gltf.empty()) { gltf = p["gltf"].asString(); if (!assets::loadGlb(assetRoot() + "/../" + gltf, rm)) { LOG_WARN("per-poly collision: %s unavailable", gltf.c_str()); break; } }
            const assets::Json& g = p["gltf_matrix"];
            if (g.size() != 16) continue;
            std::array<float, 16> M{}; for (int k = 0; k < 16; ++k) M[(size_t)k] = g[(size_t)k].asFloat(0.0f);   // column-major (glTF)
            if (recordProbe) perPolyProbe_.push_back(M);
            if (!apply) continue;
            render::SubMesh sm; sm.indexOffset = (uint32_t)m.indices.size(); sm.nodeName = p["actor"].asString(); sm.sourceMesh = e.mesh;
            const uint32_t base = (uint32_t)(m.positions.size() / 3);
            for (size_t v = 0; v + 2 < rm.positions.size(); v += 3) {
                const float x = rm.positions[v], y = rm.positions[v + 1], z = rm.positions[v + 2];
                m.positions.push_back(M[0] * x + M[4] * y + M[8] * z + M[12]);
                m.positions.push_back(M[1] * x + M[5] * y + M[9] * z + M[13]);
                m.positions.push_back(M[2] * x + M[6] * y + M[10] * z + M[14]);
            }
            for (uint32_t ix : rm.indices) { m.indices.push_back(base + ix); if (recordProbe) perPolyTris_.push_back({m.positions[(base + ix) * 3], m.positions[(base + ix) * 3 + 1], m.positions[(base + ix) * 3 + 2]}); }
            sm.indexCount = (uint32_t)rm.indices.size(); added += (int)sm.indexCount / 3;
            m.subs.push_back(sm);
        }
        if (apply) LOG_INFO("per-poly collision: %s %s: simple hull %d triangles -> render %d triangles", e.map, e.mesh, removed, added);
    }
}

// WFC_CRATERPROBE (diagnostic): knee-height rays toward each per-poly placement's centre; the nearest first hit in mesh-local units.
void World::perPolyProbe() const {
    render::MeshData cm; CollisionWorld only;   // the crater triangles alone (the hull or the render mesh), floors from the full world
    for (const core::Vec3& v : perPolyTris_) { cm.positions.push_back(v.x); cm.positions.push_back(v.y); cm.positions.push_back(v.z); cm.indices.push_back((uint32_t)cm.indices.size()); }
    { render::SubMesh sm; sm.indexCount = (uint32_t)cm.indices.size(); cm.subs.push_back(sm); }
    only.build(cm);
    for (const std::array<float, 16>& M : perPolyProbe_) {
        const float s = std::sqrt(M[0] * M[0] + M[2] * M[2]);   // the horizontal scale (local -> world)
        const core::Vec3 o{M[12], M[13], M[14]};
        int hits = 0, outside = 0; float outer = 0.0f;
        for (int k = 0; k < 16; ++k) {
            const float a = 6.2831853f * (float)k / 16.0f;
            const core::Vec3 far{o.x + std::cos(a) * 40.0f * s, o.y, o.z + std::sin(a) * 40.0f * s};
            float gy = o.y; core::Vec3 n;
            if (!collision_.groundHeight(far.x, far.z, o.y + 3.0f, 0.0f, gy, n)) gy = o.y;
            const core::Vec3 from{far.x, gy + 0.55f, far.z}, to{o.x, gy + 0.55f, o.z};
            float t;
            if (only.segmentHit(from, to, t)) { const float d = (1.0f - t) * 40.0f; ++hits; outer = std::max(outer, d); outside += d > 21.0f; }
        }
        LOG_INFO("CRATERPROBE at (%.1f %.1f %.1f): %d of 16 knee rays blocked, %d of them outside the visible rim (> 21 local units), outermost %.1f (hull rim 27.7, visible ~18-19.5)",
                 o.x, o.y, o.z, hits, outside, outer);
    }
}

void World::ensureHullMeshes() {
    if (!renderer_) return;
    for (size_t q = 0; q < match_.players().size(); ++q) {
        const ChassisDeathDef* cd = chassisDeath(resolveChassis(match_.players()[q].selection, match_.faction((int)q)));
        if (!cd || !*cd->hull || hullMeshes_.count(cd->hull)) continue;
        std::unique_ptr<render::MeshData> md = std::make_unique<render::MeshData>();
        if (assets::loadGlb(assetRoot() + "/../" + cd->hull, *md)) { for (render::Material& M : md->mats) { M.tex = resolveTexture(M.baseColorUri); M.emissiveTexHandle = resolveTexture(M.emissiveUri); } fxPrewarm(*renderer_, *md, 0); }
        else md.reset();
        hullMeshes_.emplace(cd->hull, std::move(md));
    }
}

void World::ensureGibModels() {
    if (!gibMeshesTried_ && renderer_) {
        gibMeshesTried_ = true;
        for (int i = 1; i <= 11; ++i) {
            char nm[96]; std::snprintf(nm, sizeof nm, "/../content/TR_Death_Gibs_p/Static/RB_Gib_%02d_STAT.gltf", i);
            render::MeshData md;
            if (assets::loadGlb(assetRoot() + nm, md)) { for (render::Material& M : md.mats) { if (M.wfcName.empty() && !M.sourceName.empty()) M.wfcName = "TR_Death_Gibs_p.Materials." + M.sourceName; M.tex = resolveTexture(M.baseColorUri); M.emissiveTexHandle = resolveTexture(M.emissiveUri); } const render::MeshHandle h = renderer_->uploadMesh(md); if (h != render::kInvalidMesh) gibMeshes_.push_back(h); }
        }
    }
    if (renderer_) {
        if (!skelGibModelsTried_) {
            skelGibModelsTried_ = true;
            for (int g = 0; g < 2; ++g) {
                std::unique_ptr<assets::SkinnedModel> m = std::make_unique<assets::SkinnedModel>();
                if (assets::loadSkinnedGlb(assetRoot() + gibMeshPath(g), *m) && m->valid()) {
                    for (render::Material& mt : m->mats) if (mt.wfcName.empty() && !mt.sourceName.empty()) mt.wfcName = "TR_Death_Gibs_p.Materials." + mt.sourceName;
                    resolveModelTextures(*m); skelGibModels_[g] = std::move(m);
                }
                else LOG_INFO("skeletal gib %s: not loaded", gibMeshPath(g));
            }
        }
    }
}

void World::gibCorpse(Corpse& c) {
    c.gibbed = true;
    // [Systems] the attached PlayEffect effects end with the dying actor (its PSCs go with it at the gib); their particles fade
    for (auto& f : c.playFx) fxStepStop(f.first);
    c.playFx.clear();
    for (LiveTransientLight& l : transientLights_) if (l.key == 1000 + c.player && l.cutAt < 0.0f) l.cutAt = l.age + 1.0f;   // LifeSpan 1
    if (!localDead_ && core::length(player_.pawn().actorLocation() - c.pos) < 10.0f) player_.controller().gibbedShake();   // GibbedCameraShake
    if (c.noGibs) {   // Melee: the Defrag dissolve alone
        if (deathFxLogOn()) LOG_INFO("DEATHFX p%d %s entry %s gib: fx [] chunks 0/0 skel 0/0 decal 0", c.player, c.chassis.c_str(), c.entry);
        return;
    }
    std::string fxList;   // DEATHFX: the gibs blueprint's effects as spawned (template @ PositionSocket / pos)
    // TnPawn.Dying.Gib -> the form's PlayGib: the gibs blueprint's effect state at PositionSocket (+100 UU up for the chunk cone).
    const DeathFxEntry* fxr = c.fxRow >= 0 ? &kDeathFx[(size_t)c.fxRow] : nullptr;
    if (fxr) {   // the gibs blueprint's effect state at PositionSocket + its offset (UU: Z up)
        core::Vec3 ps = c.pos; const bool psOk = c.deathSocketWorld("PositionSocket", ps);
        for (int i = 0; i < fxr->nGibFx; ++i) fxList += std::string(fxList.empty() ? "" : ", ") + fxr->gibFx[i].tmpl + (psOk ? "@PositionSocket" : "@pos");
        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)
            fxStepSpawn(fxr->gibFx[i].tmpl, ps + core::Vec3{fxr->gibFx[i].x, fxr->gibFx[i].z, fxr->gibFx[i].y} * 0.01f, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);
    } else {
        const char* fx = c.jet ? "FX_Explosives_p.FX.Exp_Death_Lrg_FX" : c.vehicle ? "FX_Explosives_p.FX.TF_Death_MED_FX" : "FX_Explosives_p.FX.Exp_Death_FX";
        core::Vec3 ps = c.pos; const bool psOk = c.deathSocketWorld("PositionSocket", ps);
        const core::Vec3 at = ps + core::Vec3{0, c.vehicle ? 0.0f : 0.5f, 0};
        fxList = std::string(fx) + (psOk ? "@PositionSocket" : "@pos");
        if (renderer_) fxStepSpawn(fx, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);
    }
    ensureGibModels();
    // HmGibsDestructionEffect: NumChunks 4, LinearSpeed U(800, 2000) UU/s (jets U(2000, 4000)) in a 60 deg half-cone about +Z from a
    // 100 UU sphere 100 UU up, AngularSpeed U(12000, 20000) (rotator units / s) about a random axis, LifeSpan 5, TF_Death_GibsTrail_FX.
    for (int i = 0; i < 4 && !gibMeshes_.empty() && (!fxr || fxr->chunks); ++i) {
        CorpseChunk k;
        k.mesh = (int)(c.frand() * (float)gibMeshes_.size()) % (int)gibMeshes_.size();
        const float th = 6.2831853f * c.frand(), cz = 1.0f - c.frand() * 0.5f, sz = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
        const core::Vec3 dir{std::cos(th) * sz, cz, std::sin(th) * sz};
        const float sp = (c.jet ? 20.0f + 20.0f * c.frand() : 8.0f + 12.0f * c.frand());
        k.p = c.pos + core::Vec3{0, 1.0f, 0} + core::Vec3{c.frand() - 0.5f, c.frand() - 0.5f, c.frand() - 0.5f} * 2.0f;
        k.v = dir * sp + c.vel * (c.vehicle ? 0.5f : 0.0f);
        k.axis = core::normalize(core::Vec3{c.frand() - 0.5f, c.frand() - 0.5f, c.frand() - 0.5f} + core::Vec3{1e-3f, 0, 0});
        k.w = (12000.0f + 8000.0f * c.frand()) * (6.2831853f / 65536.0f);
        if (renderer_) k.fx = fxStepSpawnPlain("FX_Explosives_p.FX.TF_Death_GibsTrail_FX", k.p, core::normalize(k.v + core::Vec3{1e-3f, 0, 0}), core::Vec3{0, 1, 0});
        c.chunks.push_back(k);
    }
    // The death scorch (robots): DeathDecal_MAT on the floor below the pawn origin (actorLocation: the capsule centre, as UE3).
    if (!c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {
        const float y0 = c.pos.y;
        float gy; core::Vec3 n;
        if (collision_.groundHeight(c.pos.x, c.pos.z, y0, 0.0f, gy, n) && gy >= y0 - 3.0f && std::fabs(n.y) >= 0.5f) {
            unsigned h = c.rng ^ 0x5bd1e995u;
            auto hrand = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (float)(h >> 8) / 16777216.0f; };
            const float sz = (800.0f + 150.0f * hrand()) * 0.01f;
            renderer_->spawnDecal("FX_Decals_p.DeathDecal_MAT", core::Vec3{c.pos.x, gy, c.pos.z}, core::Vec3{0, -1, 0}, sz, sz, 3.0f, 360.0f * hrand(), 30.0f);
        }
    }
    // TnSkeletalGibSpawner (robots): one of the two templates, at PositionSocket in the gib mesh's reference pose, at rest.
    if (!c.vehicle && !c.noSkelGib && renderer_) {
        ensureGibModels();
        unsigned h = c.rng ^ 0x27d4eb2fu;
        auto hrand = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (float)(h >> 8) / 16777216.0f; };
        const int g = hrand() < 0.5f ? 0 : 1;   // Templates[Rand(2)]
        const float life = 5.0f + 10.0f * hrand();   // LifeSpan U(5, 15) (Brute's {15, 5}: the same range)
        if (skelGibModels_[g]) {
            core::Vec3 at = c.pos;   // Owner.Mesh.GetSocketWorldLocation('PositionSocket'), the body's pose at the gib
            const int sn = c.socketBone.empty() || !c.model ? -1 : c.model->nodeByName(c.socketBone);
            if (sn >= 0 && (size_t)sn < c.glob.size()) { const core::Mat4 w = c.world * c.glob[(size_t)sn] * c.socketLocal; at = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
            core::Mat4 rot = c.world; rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;   // the pawn's Rotation (mesh yaw convention)
            size_t nb = 0, nj = 0;
            const GibBodyDef* bd = gibBodies(g, nb); const GibJointDef* jd = gibJoints(g, nj);
            std::unique_ptr<GibRagdoll> rd = std::make_unique<GibRagdoll>();
            if (rd->init(*skelGibModels_[g], core::Mat4::translate(at) * rot, bd, nb, jd, nj)) {
                c.skelGib = std::move(rd); c.skelGibModel = skelGibModels_[g].get(); c.skelGibAge = 0.0f; c.skelGibLife = life;
                c.skelGib->globals(c.skelGibGlob); buildPalette(*c.skelGibModel, c.skelGibGlob, c.skelGibPalette); c.skelGibSerial = ++skelGibSerial_;
            }
        }
    }
    // TnSkeletalGibSpawner (vehicles): the form's DefaultSkeletalGibTemplate hull at PositionSocket, TransferVelocityMultiplier x the vehicle's
    // velocity plus the radial force U(300, 700) (PROV: a rigid upward kick; no per-bone physics), LifeSpan U(3, 5) (jets 7), the Defrag
    // energy-death material after Delay U(0.5, 1.0) over TransitionTime 4.5 s (jets: no delay, 6 s).
    if (c.vehicle) {
        const ChassisDeathDef* cd = chassisDeath(c.chassis);
        if (cd && *cd->hull && renderer_) {
            auto it = hullMeshes_.find(cd->hull);
            if (it == hullMeshes_.end()) {
                std::unique_ptr<render::MeshData> md = std::make_unique<render::MeshData>();
                if (assets::loadGlb(assetRoot() + "/../" + cd->hull, *md)) { for (render::Material& M : md->mats) { M.tex = resolveTexture(M.baseColorUri); M.emissiveTexHandle = resolveTexture(M.emissiveUri); } fxPrewarm(*renderer_, *md, 0); }
                else md.reset();
                it = hullMeshes_.emplace(cd->hull, std::move(md)).first;
            }
            if (it->second) {
                c.hull = it->second.get();
                c.hullPos = c.pos;   // the hull keeps the vehicle mesh frame of the death (same skeleton root)
                c.hullVel = c.vel * cd->transfer + core::Vec3{0, 3.0f + 4.0f * c.frand(), 0};
                c.hullLife = c.jet ? 7.0f : 3.0f + 2.0f * c.frand();
                c.hullDefragDelay = c.jet ? 0.0f : 0.5f + 0.5f * c.frand();
                c.hullDefragTime = c.jet ? 6.0f : 4.5f;
            }
        }
    }
    if (deathFxLogOn())
        LOG_INFO("DEATHFX p%d %s entry %s gib: fx [%s] chunks %d/%d skel %d/%d decal %d", c.player, c.chassis.c_str(), c.entry, fxList.c_str(),
                 (int)c.chunks.size(), (!fxr || fxr->chunks) ? 4 : 0, c.skelGib ? 1 : 0, (!c.vehicle && !c.noSkelGib) ? 1 : 0, (!c.vehicle && !c.noDecal) ? 1 : 0);
}

// Death camera anchor: the local corpse's bone (robot C_Spine01_Lumbar01_XB, vehicle C_Reference_XR) while the body stands; once
// the pawn is destroyed (gib) or the body is gone the anchor stays where it was (the original's anchor freezes). Ends at respawn.
// A valid spectator target: a spawned, live pawn, on the spectator's team or the killer (until the first cycle press).
bool World::specValid(int p) const {
    if (p < 0 || p == localPlayer_ || (size_t)p >= match_.players().size() || !match_.players()[(size_t)p].alive) return false;
    if (!participantPawn(p)) return false;
    if (match_.players()[(size_t)localPlayer_].dormant) return true;   // a joiner before its wake follows anyone
    return match_.sameTeam(p, localPlayer_) || (specKillerOk_ && p == specKiller_);
}

void World::tickDeathCamera(float dt) {
    PlayerController& ctl = player_.controller();
    if (!localDead_) {   // respawned: RestartPlayer -> OverTheShoulder + PlayerCamera.Reset (a hard cut)
        if (ctl.deathCamera()) ctl.endDeathCamera();
        if (ctl.spectatingTarget()) { ctl.endSpectateTarget(); if (renderer_) renderer_->notifyCameraCut("spectate end"); }
        specTarget_ = specKiller_ = -1; specKillerOk_ = true;
        return;
    }
    // PlayerSpectating after MinRespawnDelay 3.0 s: the killer cam (SpectateOverTheShoulder on the target).
    if (matchActive_ && localPlayer_ >= 0 && match_.spectating(localPlayer_)) {
        const int n = (int)match_.players().size();
        auto firstValid = [&](int from, int step) { for (int k = 1; k <= n; ++k) { const int p = ((from + step * k) % n + n) % n; if (specValid(p)) return p; } return -1; };
        auto cut = [&](int p) {
            specTarget_ = p;
            if (const Character* t = participantPawn(p)) { ctl.beginSpectateTarget(t->yaw()); ctl.setSpectateAnchor(t->actorLocation()); }
            if (renderer_) renderer_->notifyCameraCut("spectate target");
        };
        if (!ctl.spectatingTarget()) {   // StartSpectating(_LastPawnWhoKilledMe)
            specKiller_ = -1; specKillerOk_ = true; specRecheck_ = 0.0f;
            for (auto it = match_.gameplayEvents().rbegin(); it != match_.gameplayEvents().rend(); ++it)
                if (it->victim == localPlayer_ && (it->type == GameplayEventType::Kill || it->type == GameplayEventType::Suicide || it->type == GameplayEventType::EnvironmentDeath)) {
                    if (it->instigator != localPlayer_) specKiller_ = it->instigator;
                    break;
                }
            const int p = specValid(specKiller_) ? specKiller_ : firstValid(-1, 1);
            if (p >= 0) cut(p);
        } else {
            if (const int c = ctl.takeSpectateCycle()) {   // SwitchSpectatorTarget: the first press drops the killer exemption
                specKillerOk_ = false;
                const int p = firstValid(specTarget_, c > 0 ? 1 : -1);
                if (p >= 0 && p != specTarget_) cut(p);
            }
            if ((specRecheck_ += dt) >= 1.0f) {   // the 1.0 s recheck: a dead / gone target moves to the first valid pawn
                specRecheck_ = 0.0f;
                if (!specValid(specTarget_)) { const int p = firstValid(-1, 1); if (p >= 0) cut(p); }   // none: stay (no LevelCamera)
            }
            if (const Character* t = specTarget_ >= 0 && specValid(specTarget_) ? participantPawn(specTarget_) : nullptr) ctl.setSpectateAnchor(t->actorLocation());
        }
        if (ctl.spectatingTarget()) return;
    }
    if (!ctl.deathCamera()) return;
    for (auto it = corpses_.rbegin(); it != corpses_.rend(); ++it) {
        const Corpse& c = *it;
        if (c.player != localPlayer_) continue;
        if (c.gibbed || !c.model || c.glob.empty()) break;
        const int n = c.model->nodeByName(c.vehicle ? "C_Reference_XR" : "C_Spine01_Lumbar01_XB");
        ctl.setDeathAnchor(n >= 0 && (size_t)n < c.glob.size() ? core::transformPoint(c.world * c.glob[(size_t)n], core::Vec3{0, 0, 0}) : c.pos);
        break;
    }
}

void World::tickCorpses(float dt) {
    if (corpses_.empty() || dt <= 0.0f) return;
    const float g = 9.8f;
    for (size_t i = 0; i < corpses_.size();) {
        Corpse& c = corpses_[i];
        c.t += dt;
        // [Systems] the death clip's HmAnimNotify_Dialog (DeathRX_NonVerbalDialog ...): the dying chassis's clip notifies in this step's
        // window [t - dt, t), to the level's dialog manager at the body (the same rule as a live clip's window, onAnimNotifyWindow)
        if (audio_ && c.clip >= 0 && !c.gibbed && c.model && !levelAudio_.level().empty()) {
            const CharacterAudioProfile* who = CharacterAudio::find(c.chassis);
            const CharacterAudioProfile::Clip* cl = who ? who->clip(c.model->clips[(size_t)c.clip].name) : nullptr;
            if (cl)
                for (const CharacterAudioProfile::Notify& n : cl->notifies) {
                    if (n.cue.compare(0, 7, "dialog:") != 0 || n.t < c.t - dt || n.t >= c.t) continue;
                    SoundCues::Emitter e; e.pos = {c.world.m[12], c.world.m[13], c.world.m[14]};
                    dialogEvents_.play(*who, c.player, n.cue.substr(7), cues_, e, core::length(e.pos - listenerPos_));
                }
        }
        if (c.defrag) c.gpuOk = false;   // the Defrag energy-death material is not applied on the queued GPU draw: CPU path
        if (!c.gibbed && !c.poseStatic) {
            if (c.clip >= 0) {
                assets::LocalPose cp;
                assets::samplePose(*c.model, c.clip, c.t, false, cp);   // the death clip from its start (clamped at its end)
                const float a = c.blendIn > 0.0f ? std::min(1.0f, c.t / c.blendIn) : 1.0f;
                assets::blendPose(c.start, cp, a, c.pose);             // from the pawn's last pose over BlendInTime
            }
            if (c.headNode >= 0 && (size_t)c.headNode < c.pose.s.size()) c.pose.s[(size_t)c.headNode] = core::Vec3{0.1f, 0.1f, 0.1f};   // ControlBone HeadSkelControl 0.1
            assets::poseGlobals(*c.model, c.pose, c.glob);          // joint globals (sockets, the palette); no vertex skin on the GPU path
            buildPalette(*c.model, c.glob, c.palette);
            if (!c.gpuOk) assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;
            const bool clipDone = c.clip < 0 || c.t >= c.model->clips[(size_t)c.clip].duration;
            if (clipDone && c.t >= c.blendIn) c.poseStatic = true;   // the last change: skinned, cached from here on
        }
        if (!c.gibbed && !c.gpuOk && c.mesh.positions.empty() && !c.glob.empty()) {   // fallback latched after the pose settled: skin once
            assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;
        }
        if (!c.gibbed) {
            if (c.fxAt >= 0.0f && c.t >= c.fxAt) {   // TnDeathModifierPlayEffect (robot Exp_Death_Sm_FX at Head_FX + BoosterSocket_C; vehicle A)
                c.fxAt = -1.0f;
                int onlyPlay = -1;   // PET_Random (car / tank): one uniformly picked entry of play[]
                if (c.fxRow >= 0 && kDeathFx[(size_t)c.fxRow].playRandom && kDeathFx[(size_t)c.fxRow].nPlay > 0)
                    onlyPlay = (int)(c.frand() * (float)kDeathFx[(size_t)c.fxRow].nPlay) % kDeathFx[(size_t)c.fxRow].nPlay;
                if (deathFxLogOn() && c.fxRow >= 0) {
                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];
                    std::string pl;
                    for (int i = 0; i < R.nPlay; ++i) { if (onlyPlay >= 0 && i != onlyPlay) continue; core::Vec3 tmp; pl += std::string(pl.empty() ? "" : ", ") + R.play[i].tmpl + "@" + (c.deathSocketWorld(R.play[i].socket, tmp) ? R.play[i].socket : "fallback"); }
                    LOG_INFO("DEATHFX p%d entry %s play: [%s]", c.player, c.entry, pl.c_str());
                }
                if (renderer_ && c.fxRow >= 0) {
                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];
                    for (int i = 0; i < R.nPlay; ++i) {
                        if (onlyPlay >= 0 && i != onlyPlay) continue;
                        const char* so = R.play[i].socket;
                        const core::Vec3 at = c.playAt(so);
                        // attached to the body (HmDeathModifierPlayEffect: a PSC on the dying actor) - kept, moved with it, stopped
                        // at the gib - not a pooled one-shot: a looping entry (the jet's smoke trail) must not emit there forever
                        fxLight(R.play[i].tmpl, at);   // the explosion light the pooled path gave it
                        const int vid = fxStepSpawn(R.play[i].tmpl, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, false);
                        if (vid >= 0) c.playFx.push_back({vid, so});
                    }
                } else if (renderer_) {
                    if (c.vehicle) fxStepSpawn(c.frand() < 0.5f ? "FX_Explosives_p.FX.TF_Death_Buildup_FX" : "FX_Explosives_p.FX.TF_Death_MED_FX", c.pos + core::Vec3{0, 1.0f, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);
                    else {
                        core::Vec3 head = c.pos + core::Vec3{0, 3.0f, 0};
                        const int hn = c.model->nodeByName("C_Spine04_Head_XB");
                        if (hn >= 0 && (size_t)hn < c.glob.size()) { const core::Mat4 w = c.world * c.glob[(size_t)hn]; head = core::Vec3{w.m[12], w.m[13], w.m[14]}; }
                        fxStepSpawn("FX_Explosives_p.FX.Exp_Death_Sm_FX", head, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);
                        fxStepSpawn("FX_Explosives_p.FX.Exp_Death_Sm_FX", c.pos + core::Vec3{0, 1.5f, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);
                    }
                }
            }
            for (auto& f : c.playFx) fxStepMove(f.first, c.playAt(f.second), core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1});   // follow the body
            if (c.t >= c.total) gibCorpse(c);   // _GibTimer = TotalTime
        }
        bool live = !c.gibbed;
        for (CorpseChunk& k : c.chunks) {
            k.age += dt;
            if (k.age >= 5.0f) { if (k.fx >= 0) { fxStepStop(k.fx); k.fx = -1; } continue; }
            live = true;
            if (!k.rest) {
                k.v.y -= g * dt;
                k.p = k.p + k.v * dt;
                k.ang += k.w * dt;
                float gy; core::Vec3 gn;
                if (collision_.valid() && k.v.y < 0.0f && collision_.groundHeight(k.p.x, k.p.z, k.p.y + 0.5f, 0.6f, gy, gn) && k.p.y <= gy + 0.15f) {
                    k.p.y = gy + 0.15f; k.v = core::Vec3{k.v.x * 0.3f, -k.v.y * 0.2f, k.v.z * 0.3f}; k.w *= 0.5f;
                    if (std::fabs(k.v.y) < 0.5f) { k.rest = true; k.w = 0.0f; }
                }
                if (k.fx >= 0) fxStepMove(k.fx, k.p, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1});
            }
        }
        if (c.hull) {
            c.hullAge += dt;
            if (c.hullAge >= c.hullLife) c.hull = nullptr;
            else {
                live = true;
                if (!c.hullRest) {
                    c.hullVel.y -= g * dt;
                    c.hullPos = c.hullPos + c.hullVel * dt;
                    float gy; core::Vec3 gn;
                    if (collision_.valid() && c.hullVel.y < 0.0f && collision_.groundHeight(c.hullPos.x, c.hullPos.z, c.hullPos.y + 1.0f, 1.5f, gy, gn) && c.hullPos.y <= gy) {
                        c.hullPos.y = gy; c.hullVel = core::Vec3{c.hullVel.x * 0.5f, 0.0f, c.hullVel.z * 0.5f};
                        if (std::hypot(c.hullVel.x, c.hullVel.z) < 0.3f) c.hullRest = true;
                    }
                }
            }
        }
        if (c.skelGib) {
            c.skelGibAge += dt;
            if (c.skelGibAge >= c.skelGibLife) c.skelGib.reset();   // LifeSpanExpired: hidden, destroyed (no fade)
            else {
                live = true;
                if (!c.skelGib->asleep()) {
                    c.skelGib->step(dt, collision_.valid() ? &collision_ : nullptr);
                    c.skelGib->globals(c.skelGibGlob); buildPalette(*c.skelGibModel, c.skelGibGlob, c.skelGibPalette); c.skelGibSerial = ++skelGibSerial_;
                }
            }
        }
        if (!live) { for (auto& f : c.playFx) fxStepStop(f.first); corpses_.erase(corpses_.begin() + (long)i); continue; }   // removed (the original: TotalTime + 1 s; chunks / wreck live on)
        ++i;
    }
}

void World::drawCorpses(render::IRenderer& r) const {
    for (const Corpse& c : corpses_) {
        if (c.gibbed || (c.gpuOk ? c.palette.empty() : c.mesh.positions.empty())) continue;
        r.setDrawOwner(c.owner);
        if (c.owner) r.setCharacterColors(render::CharacterColors{});
        // Melee deaths: TnDeathModifierSetMaterial, the form's EnergyDeathMaterial with Defrag 1 -> 0 over TransitionTime 3 s.
        if (c.defrag) r.setDrawEnergyDeath(std::max(0.0f, 1.0f - c.t / 3.0f));
        bool drawn = false;
        if (c.gpuOk) {   // the character path: queued, its shadow caster batched with live robots; the palette row is cached while the serial holds
            render::MeshData& bind = bindMeshOf(*c.model);
            syncMats(bind.mats, c.model->mats);
            drawn = drawSkinnedGpu(r, bind, c.model->joints, c.model->weights, c.palette, nullptr, 1.0f, c.world, core::Vec3{1, 1, 1},
                                   c.gpuKey.get(), c.poseSerial, 0);
            if (!drawn) { c.gpuOk = false; LOG_INFO("corpse p%d %s: GPU-skinned draw refused, CPU fallback", c.player, c.chassis.c_str()); }   // the next tick CPU-skins it
        }
        if (!drawn && !c.gpuOk && !c.mesh.positions.empty())
            r.drawDynamicMeshPosed(c.mesh, c.world, core::Vec3{1, 1, 1}, c.poseSerial);   // CPU fallback, cached while the pose is static
        if (c.defrag) r.setDrawEnergyDeath(-1.0f);
        r.setDrawOwner(0);
    }
}

void World::drawCorpseGibs(render::IRenderer& r) const {
    for (const Corpse& c : corpses_) {
        for (const CorpseChunk& k : c.chunks)
            if (k.age < 5.0f && k.mesh >= 0 && (size_t)k.mesh < gibMeshes_.size()) {
                r.drawMesh(gibMeshes_[(size_t)k.mesh], core::Mat4::translate(k.p) * core::Mat4::rotateY(k.ang) * core::Mat4::rotateX(k.ang * 0.7f), core::Vec3{1, 1, 1});
            }
        if (c.skelGib && c.skelGibModel && !c.skelGibPalette.empty()) {
            r.setDrawOwner(c.owner);
            if (c.owner) r.setCharacterColors(render::CharacterColors{});
            render::MeshData& bind = bindMeshOf(*c.skelGibModel); syncMats(bind.mats, c.skelGibModel->mats);
            drawSkinnedGpu(r, bind, c.skelGibModel->joints, c.skelGibModel->weights, c.skelGibPalette, nullptr, 1.0f, core::Mat4{}, core::Vec3{1, 1, 1},
                           c.skelGibKey.get(), c.skelGibSerial, 0);
            r.setDrawOwner(0);
        }
        if (c.hull) {
            r.setDrawOwner(c.owner);
            if (c.owner) r.setCharacterColors(render::CharacterColors{});
            const float dt0 = c.hullAge - c.hullDefragDelay;
            const bool df = dt0 >= 0.0f;
            if (df) r.setDrawEnergyDeath(std::max(0.0f, 1.0f - dt0 / c.hullDefragTime));
            r.drawDynamicMeshPosed(*c.hull, core::Mat4::translate(c.hullPos - c.pos) * c.world, core::Vec3{1, 1, 1}, 1u);   // bind mesh, never re-posed
            if (df) r.setDrawEnergyDeath(-1.0f);
            r.setDrawOwner(0);
        }
    }
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

int World::fxTeamOf(int player) const {
    if (player < 0 || (size_t)player >= match_.players().size()) return -1;
    return match_.settings().teamGame ? match_.players()[(size_t)player].team : match_.faction(player);
}

int World::fxStepSpawn(const std::string& tmpl, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u, int team, bool explosion) {
    if (!renderer_ || !fxApi<render::IRenderer>(0)) return -1;
    FxOp op; op.kind = 0; op.vid = fxNextVid_++; op.tmpl = tmpl; op.p = p; op.f = f; op.u = u; op.team = team; op.explosion = explosion;
    if (explosion) fxLight(tmpl, p);   // RT-era explosion light (WFC_FXLIGHTS, Realistic only)
    if (remainderRunning_ && !onMainThread()) fxOps_.push_back(op); else runFxOp(op);
    return op.vid;
}
void World::fxStepMove(int vid, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) {
    if (!renderer_ || vid < 0) return;
    FxOp op; op.kind = 1; op.vid = vid; op.p = p; op.f = f; op.u = u;
    if (remainderRunning_ && !onMainThread()) fxOps_.push_back(op); else runFxOp(op);
}
void World::fxStepStop(int vid) {
    if (!renderer_ || vid < 0) return;
    FxOp op; op.kind = 2; op.vid = vid;
    if (remainderRunning_ && !onMainThread()) fxOps_.push_back(op); else runFxOp(op);
}
void World::runFxOp(const FxOp& op) {
    if (op.kind == 0) {
        const int h = fxSpawn(*renderer_, op.tmpl, op.p, op.f, op.u, 0);
        if (h < 0) return;
        if (op.plain) { fxReal_[op.vid] = h; return; }   // vehicle FX: the driver sets its own parameters
        if (op.explosion) fxPooled(*renderer_, h, 0);   // HmProjectile ExplosionPSC via EmitterPool (flight FX: owned)
        fxTeamParam(*renderer_, h, op.team, 0);
        if (op.explosion) ++projectileFxExplosions_;
        else { fxReal_[op.vid] = h; ++projectileFxSpawned_; }
        return;
    }
    auto it = fxReal_.find(op.vid);
    if (it == fxReal_.end()) return;
    if (op.kind == 1) fxMove(*renderer_, it->second, op.p, op.f, op.u, 0);
    else if (op.kind == 3) renderer_->setParticleEffectParam(it->second, op.tmpl, op.v);   // tmpl holds the parameter name
    else { fxStop(*renderer_, it->second, 0); fxReal_.erase(it); }
}

bool World::fxDirect() { static const bool on = std::getenv("WFC_FXDIRECT") != nullptr; return on; }
int World::fxStepSpawnPlain(const std::string& tmpl, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u) {
    if (!renderer_ || !fxApi<render::IRenderer>(0)) return -1;
    FxOp op; op.kind = 0; op.plain = true; op.vid = fxNextVid_++; op.tmpl = tmpl; op.p = p; op.f = f; op.u = u;
    if (remainderRunning_ && !onMainThread()) fxOps_.push_back(op); else runFxOp(op);
    return op.vid;
}
void World::fxStepParam(int vid, const std::string& name, const float v[4]) {
    if (!renderer_ || vid < 0) return;
    FxOp op; op.kind = 3; op.vid = vid; op.tmpl = name; std::copy(v, v + 4, op.v);
    if (remainderRunning_ && !onMainThread()) fxOps_.push_back(op); else runFxOp(op);
}

void World::projectileFxStart(Projectile& p) {
    p.fxHandle = -1;
    if (!renderer_ || p.visual < 0 || projVisuals_[(size_t)p.visual].flight.empty()) return;
    core::Vec3 f, u; fxFrame(p.vel, f, u);
    p.fxHandle = fxStepSpawn(projVisuals_[(size_t)p.visual].flight, p.pos, f, u, fxTeamOf(p.instigator), false);
}

void World::projectileFxMove(const Projectile& p) {
    if (!renderer_ || p.fxHandle < 0) return;
    core::Vec3 f, u;
    // A resting grenade keeps its last orientation (zero velocity).
    if (core::dot(p.vel, p.vel) < 1e-8f) return;
    fxFrame(p.vel, f, u);
    fxStepMove(p.fxHandle, p.pos, f, u);
}

void World::projectileFxEnd(Projectile& p, const core::Vec3& at, const core::Vec3& normal, bool explode) {
    if (!renderer_) return;
    if (p.fxHandle >= 0) { fxStepStop(p.fxHandle); p.fxHandle = -1; }   // trails finish their lifetime
    if (!explode || p.visual < 0 || projVisuals_[(size_t)p.visual].explosion.empty()) return;
    core::Vec3 f, u; fxFrame(normal, f, u);
    fxStepSpawn(projVisuals_[(size_t)p.visual].explosion, at, f, u, fxTeamOf(p.instigator), true);
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
    uint32_t hdr[3] = {0, 0, 0}, ch[2] = {0, 0};
    std::string js;
    std::ifstream f(path, std::ios::binary);
    if (f) {   // plain file: read only the header + JSON chunk
        f.read((char*)hdr, 12); f.read((char*)ch, 8);
        if (!f || hdr[0] != 0x46546C67u || ch[1] != 0x4E4F534Au) return false;   // 'glTF', chunk 'JSON'
        js.assign(ch[0], '\0');
        f.read(&js[0], (std::streamsize)ch[0]);
        if (!f) return false;
    } else {   // [integration 09c] slim package: the lossless .xpr twin, read whole
        std::vector<uint8_t> buf;
        if (!assets::readDataFile(path, buf) || buf.size() < 20) return false;
        std::memcpy(hdr, buf.data(), 12); std::memcpy(ch, buf.data() + 12, 8);
        if (hdr[0] != 0x46546C67u || ch[1] != 0x4E4F534Au || 20 + (size_t)ch[0] > buf.size()) return false;
        js.assign((const char*)buf.data() + 20, ch[0]);
    }
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

namespace {
// TEST ONLY (WFC_ANIMHASH=1, the load-time pass's identity check): FNV-1a over a loaded animation file's whole data - node names, clip
// names / durations, every sampler's times / values / interpolation / components, every channel - logged per file.
void logAnimHash(const std::string& path, const assets::AnimFile& f) {
    static const bool on = std::getenv("WFC_ANIMHASH") != nullptr;
    if (!on) return;
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } };
    for (const std::string& n : f.nodeNames) mix(n.data(), n.size() + 1);
    for (const assets::AnimClip& c : f.clips) {
        mix(c.name.data(), c.name.size() + 1); mix(&c.duration, sizeof c.duration);
        for (const assets::AnimSampler& sm : c.samplers) {
            mix(sm.times.data(), sm.times.size() * sizeof(float)); mix(sm.values.data(), sm.values.size() * sizeof(float));
            const int interp = (int)sm.interp; mix(&interp, sizeof interp); mix(&sm.comps, sizeof sm.comps);
        }
        for (const assets::AnimChannel& ch : c.channels) { const int v[3] = {ch.sampler, ch.node, (int)ch.path}; mix(v, sizeof v); }
    }
    LOG_INFO("ANIMHASH %016llx %zu clips %s", (unsigned long long)h, f.clips.size(), path.c_str());
}
}  // namespace

const World::SharedAnimFile* World::sharedAnimFile(const std::string& path) {
    auto it = animFiles_.find(path);
    if (it != animFiles_.end()) { it->second->lastUse = cacheGen_; return it->second.get(); }
    auto f = std::make_unique<SharedAnimFile>();
    f->ok = assets::loadAnimationFile(path, f->file);
    for (size_t i = 0; i < f->file.clips.size(); ++i) f->byName.emplace(f->file.clips[i].name, i);   // first clip of a name
    logAnimHash(path, f->file);
    const SharedAnimFile* raw = f.get();
    f->lastUse = cacheGen_;
    animFiles_[path] = std::move(f);
    return raw;
}

// [Systems, load-time pass] A chassis' animation files not cached yet load in parallel: each is the same assets::loadAnimationFile
// (an independent file read + JSON parse + key extraction, no shared state), on worker threads; the cache then holds exactly what the
// serial path builds (WFC_ANIMHASH proves it per file). WFC_ANIMTHREADS=<n> (0 = serial, the A/B arm).
void World::prefetchAnimFiles(const std::vector<std::string>& paths) {
    static const int threads = [] {
        const char* e = std::getenv("WFC_ANIMTHREADS");
        if (e) return std::max(0, std::atoi(e));
        const unsigned hw = std::thread::hardware_concurrency();
        return (int)std::max(1u, std::min(6u, hw > 2 ? hw - 2 : 1u));
    }();
    std::vector<std::string> todo;
    for (const std::string& p : paths)
        if (!animFiles_.count(p) && std::find(todo.begin(), todo.end(), p) == todo.end()) todo.push_back(p);
    if (threads <= 0 || todo.size() < 2) return;
    std::vector<std::unique_ptr<SharedAnimFile>> got(todo.size());
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t i; (i = next++) < todo.size();) {
            auto f = std::make_unique<SharedAnimFile>();
            f->ok = assets::loadAnimationFile(todo[i], f->file);
            for (size_t k = 0; k < f->file.clips.size(); ++k) f->byName.emplace(f->file.clips[k].name, k);   // first clip of a name
            got[i] = std::move(f);
        }
    };
    std::vector<std::thread> pool;
    for (int k = 0; k < std::min<int>(threads, (int)todo.size()); ++k) pool.emplace_back(work);
    for (std::thread& t : pool) t.join();
    for (size_t i = 0; i < todo.size(); ++i) {
        logAnimHash(todo[i], got[i]->file);
        got[i]->lastUse = cacheGen_;
        animFiles_[todo[i]] = std::move(got[i]);
    }
}

bool World::loadRobotShared(const ChassisDef& def, assets::SkinnedModel& m) {
    return loadRobotSharedWith(def, m, [this](const std::string& path) { return sharedAnimFile(path); }, true);
}

bool World::loadRobotSharedWith(const ChassisDef& def, assets::SkinnedModel& m, const std::function<const SharedAnimFile*(const std::string&)>& anim,
                                bool prefetch) {
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
    {   // every source file this chassis needs, in parallel when not cached (prefetchAnimFiles)
        std::vector<std::string> srcs;
        for (const ChassisDef::AnimRef& r : def.robotAnims) srcs.push_back(ext + r.source);
        if (prefetch) prefetchAnimFiles(srcs);   // the synchronous path only: a background job must not touch the cache
    }
    // Per source file: file node -> model node by bone name (the matching loadAnimationsByName / appendAnimations use).
    std::map<const SharedAnimFile*, std::vector<int>> remaps;
    for (const ChassisDef::AnimRef& r : def.robotAnims) {
        const SharedAnimFile* f = anim(ext + r.source);
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
bool World::soundBlocked(const core::Vec3& from, const core::Vec3& to) const {
    if (!collision_.valid()) return false;
    const core::Vec3 d = to - from;
    const float len = core::length(d);
    if (len < 1.0f) return false;
    const core::Vec3 n = d * (1.0f / len);
    float t;
    return collision_.segmentHit(from + n * 0.25f, to - n * 0.5f, t);   // as the cue occlusion query (World::setAudio)
}

const Character* World::participantPawn(int player) const {
    if (player < 0) return nullptr;
    if (player == localPlayer_) return localDead_ ? nullptr : &player_.pawn();
    if ((size_t)player < oppByPlayer_.size()) { const MatchOpponent* o = oppByPlayer_[(size_t)player]; return o && o->spawned() ? &o->pawn() : nullptr; }
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
    s.fineAim = onlineSeats_ ? (player >= 0 && (size_t)player < seatFineAim_.size() && seatFineAim_[(size_t)player] != 0)   // online: the seat command
                             : player == localPlayer_ && player_.controller().fineAiming();
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
