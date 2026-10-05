#include "game/AmbientAudio.h"
#include "game/SoundCues.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {
namespace {

constexpr float UU = 0.01f;

float frand() { return (float)std::rand() / (float)RAND_MAX; }

core::Vec3 vec(const assets::Json& a) { return {a[0].asFloat(), a[1].asFloat(), a[2].asFloat()}; }

struct LevelAudioManifest { const char* level; const char* json; };
#include "game/LevelAudio.inc"
const char* manifestFor(const std::string& level) {
    for (const LevelAudioManifest& m : kLevelAudioManifests) if (level == m.level) return m.json;
    return nullptr;
}

// A manifest's mixer presets (SoundMixerProperties MixerPresets + the DSPPresets of that name per category): the
// REVERB_* presets of SeqAct_Reverb (MASTER_WET) and the SeqAct_Mixer presets (category volumes, e.g. EXT DUCK), owned
// by the level for its lifetime.
std::vector<float> dspValues(const assets::Json& d) {
    const assets::Json& r = d["Reverb"];
    const assets::Json& e = d["Echo"];
    return {d["Volume"]["Volume"].asFloat(1.0f), r["Room"].asFloat(-10000.0f), r["RoomHF"].asFloat(-10000.0f),
            r["RoomRolloffFactor"].asFloat(0.0f), r["DecayTime"].asFloat(1.0f), r["DecayHFRatio"].asFloat(1.0f),
            r["ReflectionsLevel"].asFloat(-10000.0f), r["ReflectionsDelay"].asFloat(0.0f), r["Level"].asFloat(-10000.0f),
            r["Delay"].asFloat(0.0f), r["Diffusion"].asFloat(0.0f), r["Density"].asFloat(0.0f), r["HFReference"].asFloat(5000.0f),
            e["Delay"].asFloat(500.0f), e["DecayRatio"].asFloat(0.5f), e["WetMix"].asFloat(0.0f), e["DryMix"].asFloat(1.0f)};
}
int addReverbPresets(const assets::Json& presets, SoundMixer& mixer) {
    int np = 0;
    for (const auto& kv : presets.obj) {
        if (mixer.hasPreset(kv.first)) continue;                     // both manifests may list it
        const assets::Json& mp = kv.second["mixer_preset"];
        std::vector<std::pair<std::string, std::vector<float>>> rows;
        for (const auto& cat : kv.second["dsp_by_category"].obj) rows.push_back({cat.first, dspValues(cat.second)});
        if (mixer.addMapPresetRows(kv.first, mp["Priority"].asFloat(0.0f), mp["FadeInTime"].asFloat(0.0f),
                                   mp["FadeOutTime"].asFloat(0.0f), mp["Duration"].asFloat(-1.0f), rows)) ++np;
    }
    return np;
}

// Moller-Trumbore, ray (any t > 0).
bool rayTri(const core::Vec3& o, const core::Vec3& d, const core::Vec3& a, const core::Vec3& b, const core::Vec3& c) {
    core::Vec3 e1 = b - a, e2 = c - a, p = core::cross(d, e2);
    float det = core::dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    core::Vec3 tv = o - a;
    float u = core::dot(tv, p) * inv;
    if (u < 0 || u > 1) return false;
    core::Vec3 q = core::cross(tv, e1);
    float v = core::dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    return core::dot(e2, q) * inv > 0.0f;
}

} // namespace

bool AmbientAudio::hasLevelManifest(const std::string& level) { return manifestFor(level) != nullptr; }
int AmbientAudio::levelManifestCount() { return (int)(sizeof(kLevelAudioManifests) / sizeof(kLevelAudioManifests[0])); }
const char* AmbientAudio::levelManifestName(int i) { return kLevelAudioManifests[i].level; }
const char* AmbientAudio::manifestJson(const std::string& name) { return manifestFor(name); }

bool AmbientAudio::levelMusicTrack(const std::string& level, MusicTrack& out) {
    const char* sj = manifestFor(level);
    assets::Json sys;
    if (!sj || !assets::Json::parse(std::string(sj), sys)) return false;
    const assets::Json& ops = sys["kismet"]["ops"];
    for (size_t i = 0; i < ops.size(); ++i) {
        const assets::Json& j = ops[i];
        if (j["type"].asString() != "play_music") continue;
        out = MusicTrack{};
        out.cue = j["cue"].asString();
        out.fadeIn = j["fade_in"].asFloat(1.0f); out.fadeOut = j["fade_out"].asFloat(1.0f);
        out.boredom = j["boredom"].asFloat(0.0f); out.priority = j["priority"].asInt(0);
        return true;
    }
    return false;
}

bool AmbientAudio::load(const std::string& path, const std::string& contentRoot, SoundCues& cues, audio::IAudio* a,
                        const std::string& levelName) {
    unload(cues);                                        // a previous level's bed, zones, pools, script, cues, presets
    audio_ = a;
    assets::Json root;                                   // the AssetTools map manifest (optional)
    bool haveFile = false;
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::stringstream ss; ss << f.rdbuf();
            if (!assets::Json::parse(ss.str(), root)) { LOG_WARN("ambient: bad json %s", path.c_str()); return false; }
            haveFile = true;
        }
    }
    const std::string name = !levelName.empty() ? levelName : root["map"].asString();
    assets::Json sys;                                    // the compiled-in Systems level manifest (optional)
    const char* sj = manifestFor(name);
    const bool haveSys = sj && assets::Json::parse(std::string(sj), sys);
    if (!haveFile && !haveSys) { LOG_WARN("ambient: no audio manifest for %s (%s)", name.c_str(), path.c_str()); return false; }
    // Sound banks. Concurrency: each cue asset's MaxConcurrentPlayCount / InstanceLimiting from the level's cue_limits
    // (the AssetTools bank entries do not carry them), else Engine.Default__SoundCue.
    assets::Json bank = root["cues"];
    for (auto& kv : bank.obj) {
        const assets::Json& lim = sys["cue_limits"][kv.first];
        for (const char* k : {"MaxConcurrentPlayCount", "InstanceLimiting"})
            if (!kv.second.has(k) && lim.has(k)) kv.second.obj[k] = lim[k];
    }
    int nc = cues.addCues(bank, contentRoot) + cues.addCues(sys["cues"], contentRoot);
    // A multiplayer map's announcer (TnWorldInfo.AnnouncerSoundEventSet): the set and the match cues (dialogue + mode
    // music, streamed) are shared by every map (the "__match_messages__" manifest) and owned by the level while loaded.
    const assets::Json& an = root.has("announcer") ? root["announcer"] : sys["announcer"];
    if (an.has("event_set")) {
        static assets::Json shared;
        static bool parsed = false;
        if (!parsed) { parsed = true; if (const char* mj = manifestFor("__match_messages__")) assets::Json::parse(std::string(mj), shared); }
        for (const auto& kv : shared["announcer_sets"][an["event_set"].asString()].obj) announcer_[kv.first] = kv.second.asString();
        nc += cues.addCues(shared["cues"], contentRoot);
    }
    for (const auto& kv : an["events"].obj) announcer_[kv.first] = kv.second.asString();
    int np = addReverbPresets(root["reverb_presets"], cues.mixer()) + addReverbPresets(sys["reverb_presets"], cues.mixer());

    // Emitters.
    const char* kinds[3] = {"point", "volume", "line"};
    for (int k = 0; k < 3; ++k) {
        const assets::Json& list = root["emitters"][kinds[k]];
        for (size_t i = 0; i < list.size(); ++i) {
            const assets::Json& j = list[i];
            Emitter e;
            e.kind = (Emitter::Kind)k;
            e.cue = j["cue"].asString();
            const assets::Json& m = j["gltf_matrix"];      // column-major, metres, includes the actor scale
            for (int c = 0; c < 3; ++c) e.axis[c] = {m[(size_t)(c * 4)].asFloat(), m[(size_t)(c * 4 + 1)].asFloat(), m[(size_t)(c * 4 + 2)].asFloat()};
            e.origin = {m[12].asFloat(), m[13].asFloat(), m[14].asFloat()};
            if (k == Emitter::Volume) e.half = j["radius"].asFloat(500.0f) * UU;
            if (k == Emitter::Line) e.half = j["linelength"].asFloat(500.0f) * UU * 0.5f;
            const cuedata::CueDef* cd = cues.cueDef(e.cue.c_str());
            if (!cd) continue;
            emitters_.push_back(e);
        }
    }

    // Zones + their reverb presets and one-shot pools.
    const assets::Json& presets = root["reverb_presets"];
    const assets::Json& zl = root["zones"];
    for (size_t i = 0; i < zl.size(); ++i) {
        const assets::Json& j = zl[i];
        Zone z;
        z.name = j["comment"].asString();
        const assets::Json& polys = j["trigger_polygons_gltf"];
        z.bmin = {1e30f, 1e30f, 1e30f}; z.bmax = {-1e30f, -1e30f, -1e30f};
        for (size_t p = 0; p < polys.size(); ++p) {
            const assets::Json& poly = polys[p];
            for (size_t v = 1; v + 1 < poly.size(); ++v) {   // fan-triangulate the planar face
                core::Vec3 tri[3] = {vec(poly[0]), vec(poly[v]), vec(poly[v + 1])};
                for (const core::Vec3& t : tri) {
                    z.tris.push_back(t);
                    z.bmin = {std::min(z.bmin.x, t.x), std::min(z.bmin.y, t.y), std::min(z.bmin.z, t.z)};
                    z.bmax = {std::max(z.bmax.x, t.x), std::max(z.bmax.y, t.y), std::max(z.bmax.z, t.z)};
                }
            }
        }
        const assets::Json& pr = presets[j["reverb_preset"].asString()];
        z.priority = pr["mixer_preset"]["Priority"].asFloat(0.0f);   // diagnostics; the mixer orders presets
        z.preset = j["reverb_preset"].asString();
        if (!cues.mixer().hasPreset(z.preset)) LOG_WARN("ambient: zone %s preset %s not in the mixer", z.name.c_str(), z.preset.c_str());
        const assets::Json& pools = j["one_shot_pool"];
        for (size_t p = 0; p < pools.size(); ++p) {
            const assets::Json& q = pools[p];
            z.pools.push_back({q["cue"].asString(), q["delay_min"].asFloat(), q["delay_max"].asFloat(),
                               q["distance_min"].asFloat() * UU, q["distance_max"].asFloat() * UU, q["looping"].asBool(true)});
        }
        zones_.push_back(z);
    }

    // Kismet audio ops (Systems manifest; an AssetTools manifest may carry the same section).
    const int ns = script_.load(root.has("kismet") ? root["kismet"] : sys["kismet"]);
    // A generated Kismet graph with the map's touch volumes runs the zones itself (Out:<touch> -> zone -> reverb /
    // pools / mixer...); the AssetTools manifest's flattened zones are then not used (no double zone logic).
    if (script_.hasTouchZones()) {
        if (preferManifestZones_) script_.unload();      // validation: the flattened zones instead of the graph
        else zones_.clear();
    }
    sceneActive_.assign(zones_.size(), 0);
    touching_.assign(zones_.size(), 0);
    level_ = name;
    LOG_INFO("ambient: %s: %d level cues, %d reverb presets, %zu emitters, %zu zones, %d pools, %d Kismet audio ops / %d links "
             "(manifests: %s%s)", name.c_str(), nc, np, emitters_.size(), (size_t)zoneCount(), poolCount(), ns, script_.linkCount(),
             haveFile ? "AssetTools " : "", haveSys ? "Systems" : "");
    loaded_ = true;
    return true;
}

void AmbientAudio::unload(SoundCues& cues) {
    // Map unload / level change: the map's AudioComponents go with its actors and Kismet; the mixer flushes
    // (0x8276AB60: Default only, current-reverb slot None) and forgets the map's presets; the map bank leaves
    // the cue table and its samples are released.
    for (Emitter& e : emitters_) if (e.instance >= 0) cues.stop(e.instance, 0.0f);
    cues.unloadMapCues();                                // also stops pool one-shots still sounding
    cues.mixer().removeMapPresets();
    script_.unload();
    level_.clear();
    announcer_.clear();
    emitters_.clear();
    zones_.clear();
    touching_.clear();
    sceneActive_.clear();
    poolTimers_.clear();
    zone_ = -1;
    active_ = 0;
    started_ = false;
    refusedAtStart_ = 0;
    oneShots_ = 0;
    loaded_ = false;
}

// Round / match reset without a level change (GameInfo.ResetLevel -> Kismet Reset) [HIGH: Kismet objects reset
// through their Reset(); CONF bodies]: SeqAct_AmbientAudioZone.Reset IsEntered=false, SceneIndexCurrent=-1 (the
// scenes end: pools stop); SeqAct_PlayPlayerPositionalSound.Reset IsPlaying=false. Not reset: the map's ambient
// AudioComponents (AmbientSound has no Reset), the mixer (no Flush without a level change) and
// PlayerController.AmbientAudioZone. The (re)spawned pawn's first Touch re-enters a zone: a different zone
// switches the reverb; the same zone only re-begins its scene (pools restart, the reverb slot is unchanged).
void AmbientAudio::resetMatch() {
    script_.resetMatch();
    sceneActive_.assign(zones_.size(), 0);
    touching_.assign(zones_.size(), 0);
    poolTimers_.clear();
}

int AmbientAudio::fireEvent(const std::string& trigger, SoundCues& cues, const core::Vec3& listener) {
    return loaded_ ? script_.fire(trigger, cues, music_, listener) : 0;
}

bool AmbientAudio::inside(const Zone& z, const core::Vec3& p) const {
    if (p.x < z.bmin.x || p.y < z.bmin.y || p.z < z.bmin.z || p.x > z.bmax.x || p.y > z.bmax.y || p.z > z.bmax.z)
        return false;
    // Ray parity (the Exterior volume is non-convex). Slightly skewed direction avoids edges.
    const core::Vec3 d = core::normalize(core::Vec3{1.0f, 0.0137f, 0.0291f});
    int hits = 0;
    for (size_t i = 0; i + 2 < z.tris.size(); i += 3)
        if (rayTri(p, d, z.tris[i], z.tris[i + 1], z.tris[i + 2])) ++hits;
    return (hits & 1) != 0;
}

// SeqAct_AmbientAudioZone.OnInput(Enter) (0x827892D8) -> Update: "Scene 0 Begun" -> SeqAct_Reverb + START
// [CONF, RE A1]. A zone whose scene already runs (IsEntered, SceneIndexCurrent 0) does nothing again.
void AmbientAudio::enterZone(int z, SoundCues& cues) {
    const int prev = zone_;
    if (z != zone_) {                                    // PC.AmbientAudioZone changes: prev IsEntered=false
        if (prev >= 0) sceneActive_[(size_t)prev] = 0;   // -> "Scene 0 Ended" -> STOP (its pools stop)
        zone_ = z;
    }
    if (sceneActive_[(size_t)z]) return;                 // scene already begun: no-op
    sceneActive_[(size_t)z] = 1;
    cues.mixer().activateReverb(zones_[(size_t)z].preset);
    poolTimers_.clear();
    for (const Pool& p : zones_[(size_t)z].pools) poolTimers_.push_back(p.delayMin + frand() * (p.delayMax - p.delayMin));
    LOG_INFO("ambient: entered zone %s (from %s; preset %s, priority %.0f; mixer [%s])", zones_[(size_t)z].name.c_str(),
             prev >= 0 ? zones_[(size_t)prev].name.c_str() : "None", zones_[(size_t)z].preset.c_str(),
             zones_[(size_t)z].priority, cues.mixer().activeList().c_str());
}

core::Vec3 AmbientAudio::placeFor(const Emitter& e, const core::Vec3& l) const {
    if (e.kind == Emitter::Point) return e.origin;
    core::Vec3 r = l - e.origin;
    if (e.kind == Emitter::Line) {
        float len2 = core::dot(e.axis[0], e.axis[0]);
        float u = len2 > 0.0f ? core::dot(r, e.axis[0]) / len2 : 0.0f;
        u = core::clampf(u, -e.half, e.half);
        return e.origin + e.axis[0] * u;
    }
    core::Vec3 out = e.origin;
    for (int c = 0; c < 3; ++c) {
        float len2 = core::dot(e.axis[c], e.axis[c]);
        float u = len2 > 0.0f ? core::dot(r, e.axis[c]) / len2 : 0.0f;
        out = out + e.axis[c] * core::clampf(u, -e.half, e.half);
    }
    return out;
}

void AmbientAudio::tick(float dt, const core::Vec3& listener, const core::Vec3& pawn, SoundCues& cues, bool pawnAlive) {
    if (!loaded_) return;

    // Zones: SeqEvent_Touch (local player pawn, client side, camera ignored) fires on the frame the pawn
    // starts overlapping a trigger volume; UnTouched is unlinked. Several Touches in one frame run in zone
    // order, so the last one wins [HIGH: same-frame Touch order is the engine's touch list order].
    // [PROVISIONAL] the pawn is a point 1 m above its origin, not its collision cylinder.
    touching_.resize(zones_.size(), 0);
    const core::Vec3 p = pawn + core::Vec3{0, 1.0f, 0};
    for (int z = 0; z < (int)zones_.size(); ++z) {
        const bool in = inside(zones_[(size_t)z], p);
        if (in && !touching_[(size_t)z]) enterZone(z, cues);
        touching_[(size_t)z] = in ? 1 : 0;
    }
    // Zone one-shot pools (SeqAct_PlayPlayerPositionalSound): world one-shots around the player.
    if (zone_ >= 0 && sceneActive_[(size_t)zone_]) {
        const Zone& z = zones_[(size_t)zone_];
        for (size_t i = 0; i < z.pools.size() && i < poolTimers_.size(); ++i) {
            poolTimers_[i] -= dt;
            if (poolTimers_[i] > 0.0f) continue;
            const Pool& p = z.pools[i];
            // [CONF SeqAct_PlayPlayerPositionalSound.CalculatePosition] random yaw 0..359 deg, distance
            // RandRange(DistanceMin, DistanceMax) in the horizontal plane, around GetReferencePoint(): the
            // "Source Actor" variable, else AudioDevice Listeners[0].Location. The recovered pools link no
            // Source Actor [HIGH], so the reference is the listener. World-fixed (bUseLocation, not attached).
            float ang = frand() * (359.0f / 360.0f) * 6.2831853f;
            float d = p.distMinM + frand() * (p.distMaxM - p.distMinM);
            core::Vec3 at = listener + core::Vec3{std::cos(ang) * d, 0.0f, std::sin(ang) * d};
            cues.play(p.cue.c_str(), at, core::length(at - listener));
            ++oneShots_;
            poolTimers_[i] = p.looping ? p.delayMin + frand() * (p.delayMax - p.delayMin) : 1e30f;
        }
    }

    // Emitters [CONF native, see AmbientAudio.h]: every authored emitter's AudioComponent auto-plays once, in
    // authored order, at level start (the first tick, once the listener exists); SoundCues::play applies the
    // native per-cue registration (global per cue, kKillFarthest against this listener, refused = -1).
    // Line / volume emitters re-Play every tick while not playing (A7); point AmbientSounds never restart.
    // Volume, cull, pan, SmartPan and occlusion are the per-voice native rules; there is no budget or fade.
    if (!started_) {
        started_ = true;
        for (Emitter& e : emitters_) {
            core::Vec3 at = placeFor(e, listener);
            e.instance = cues.play(e.cue.c_str(), at, core::length(at - listener));
            if (e.instance < 0) ++refusedAtStart_;
        }
        script_.fire("GameplayStarted", cues, music_, listener);    // SeqEvent_GameplayStarted
    }
    script_.tick(dt, listener, cues, music_, &pawn, pawnAlive);
    active_ = 0;
    for (Emitter& e : emitters_) {
        core::Vec3 at = placeFor(e, listener);
        if (e.instance >= 0 && !cues.playing(e.instance)) e.instance = -1;      // killed by instance limiting
        if (e.instance < 0 && e.kind != Emitter::Point)
            e.instance = cues.play(e.cue.c_str(), at, core::length(at - listener));
        if (e.instance >= 0) {
            cues.update(e.instance, at, 0.0f);
            ++active_;
        }
    }
}

} // namespace game
