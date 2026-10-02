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
constexpr int kMaxActive = 24;          // [PROV] simultaneously playing map emitters
constexpr float kAudibleDb = -48.0f;    // [PROV] below this estimated level an emitter goes virtual
constexpr float kEmitterFade = 0.5f;    // [PROV] virtual <-> real fade (s)

float frand() { return (float)std::rand() / (float)RAND_MAX; }

core::Vec3 vec(const assets::Json& a) { return {a[0].asFloat(), a[1].asFloat(), a[2].asFloat()}; }

// FMOD inverse rolloff as the mixer applies it (Win32Audio::resolveGains).
float inverseGain(float d, float minM, float maxM, float rolloff) {
    float dd = std::min(d, maxM);
    return dd <= minM ? 1.0f : minM / (minM + rolloff * (dd - minM));
}

audio::Environment envFrom(const assets::Json& dsp) {
    audio::Environment e;
    const assets::Json& r = dsp["Reverb"];
    e.room = r["Room"].asFloat(-10000.0f); e.roomHF = r["RoomHF"].asFloat(-10000.0f);
    e.decayTime = r["DecayTime"].asFloat(1.0f); e.decayHFRatio = r["DecayHFRatio"].asFloat(1.0f);
    e.reflections = r["ReflectionsLevel"].asFloat(-10000.0f); e.reflectionsDelay = r["ReflectionsDelay"].asFloat(0.0f);
    e.reverb = r["Level"].asFloat(-10000.0f); e.reverbDelay = r["Delay"].asFloat(0.0f);
    e.diffusion = r["Diffusion"].asFloat(100.0f); e.density = r["Density"].asFloat(100.0f);
    e.hfReference = r["HFReference"].asFloat(5000.0f);
    const assets::Json& ec = dsp["Echo"];
    e.echoDelayMs = ec["Delay"].asFloat(500.0f); e.echoDecay = ec["DecayRatio"].asFloat(0.5f);
    e.echoWet = ec["WetMix"].asFloat(0.0f); e.echoDry = ec["DryMix"].asFloat(1.0f);
    return e;
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

bool AmbientAudio::load(const std::string& path, const std::string& contentRoot, SoundCues& cues, audio::IAudio* a) {
    audio_ = a;
    std::ifstream f(path, std::ios::binary);
    if (!f) { LOG_WARN("ambient: %s not found", path.c_str()); return false; }
    std::stringstream ss; ss << f.rdbuf();
    assets::Json root;
    if (!assets::Json::parse(ss.str(), root)) { LOG_WARN("ambient: bad json %s", path.c_str()); return false; }
    int nc = cues.addCues(root["cues"], contentRoot);
    // [CONF] SoundCue.MaxConcurrentPlayCount of the map bank (MP_IAC_Streets_AUDIO_m; not in audio.json,
    // read from the cooked cues): the only two non-zero entries.
    cues.setMaxConcurrent("BL_LVL_MP_IAC_STREETS.EMIT_FLOOD_LIGHTS", 3);
    cues.setMaxConcurrent("BL_LVL_MP_IAC_STREETS.EMIT_MONRAIL_IDLE_LP", 3);

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
            e.volDb = cd->volDb; e.minM = cd->distMinUU * UU; e.maxM = cd->distMaxUU * UU; e.rolloff = cd->rolloff;
            e.loops = false;
            for (const cuedata::EventDef& ev : cd->events) e.loops = e.loops || ev.loop;
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
        z.env = envFrom(pr["dsp_by_category"]["MASTER_WET"]);
        z.fadeIn = pr["mixer_preset"]["FadeInTime"].asFloat(0.25f);
        z.fadeOut = pr["mixer_preset"]["FadeOutTime"].asFloat(0.25f);
        z.priority = pr["mixer_preset"]["Priority"].asFloat(0.0f);
        z.preset = j["reverb_preset"].asString();
        const assets::Json& pools = j["one_shot_pool"];
        for (size_t p = 0; p < pools.size(); ++p) {
            const assets::Json& q = pools[p];
            z.pools.push_back({q["cue"].asString(), q["delay_min"].asFloat(), q["delay_max"].asFloat(),
                               q["distance_min"].asFloat() * UU, q["distance_max"].asFloat() * UU, q["looping"].asBool(true)});
        }
        zones_.push_back(z);
    }

    // Master category DSP (Default preset): the compressor [CONF values; MED DSPEffectConfig bit].
    const assets::Json& master = root["categories"]["Master"];
    const assets::Json& comp = master["Default_DSP"]["Compressor"];
    if (a && (master["DSPEffectConfig"].asInt(0) & 32))
        a->setMasterCompressor(comp["Threshold"].asFloat(0.0f), comp["Attack"].asFloat(50.0f),
                               comp["Release"].asFloat(50.0f), comp["GainMakeup"].asFloat(0.0f));
    LOG_INFO("ambient: %d map cues, %zu emitters, %zu zones (master compressor %.1f dB)", nc, emitters_.size(),
             zones_.size(), comp["Threshold"].asFloat(0.0f));
    loaded_ = true;
    return true;
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

// SeqAct_AmbientAudioZone "Enter" -> SeqAct_Reverb -> EnableMixerPreset(REVERB_*) [CONF path]: the
// MASTER_WET reverb comes from the highest-Priority enabled REVERB_* preset, cross-fading 0.25 s [CONF data].
// [INFERRED, AssetTools / ReVa request] entering a zone ends the previous zone's scene, i.e. its preset is
// disabled, so one zone preset is enabled at a time; the priority rule decides any overlap.
void AmbientAudio::enterZone(int z) {
    int prev = zone_;
    zone_ = z;
    if (prev >= 0)
        for (size_t i = 0; i < enabledZones_.size(); ++i)
            if (enabledZones_[i] == prev) { enabledZones_.erase(enabledZones_.begin() + (long)i); break; }
    bool have = false;
    for (int e : enabledZones_) have = have || e == z;
    if (!have) enabledZones_.push_back(z);
    int top = z;
    for (int e : enabledZones_) if (zones_[(size_t)e].priority > zones_[(size_t)top].priority) top = e;
    if (audio_) audio_->setEnvironment(zones_[(size_t)top].env, zones_[(size_t)top].fadeIn);
    poolTimers_.clear();
    for (const Pool& p : zones_[(size_t)z].pools) poolTimers_.push_back(p.delayMin + frand() * (p.delayMax - p.delayMin));
    LOG_INFO("ambient: entered zone %s (preset %s, priority %.0f; active reverb %s)", zones_[(size_t)z].name.c_str(),
             zones_[(size_t)z].preset.c_str(), zones_[(size_t)z].priority, zones_[(size_t)top].preset.c_str());
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

void AmbientAudio::tick(float dt, const core::Vec3& listener, const core::Vec3& pawn, SoundCues& cues) {
    if (!loaded_) return;

    // Zones: TriggerVolume touch by the pawn (checked a few times per second).
    zoneTimer_ -= dt;
    if (zoneTimer_ <= 0.0f || !zoneChecked_) {
        zoneTimer_ = 0.2f;
        zoneChecked_ = true;
        core::Vec3 p = pawn + core::Vec3{0, 1.0f, 0};
        for (int z = 0; z < (int)zones_.size(); ++z)
            if (z != zone_ && inside(zones_[(size_t)z], p)) { enterZone(z); break; }
    }
    // Zone one-shot pools (SeqAct_PlayPlayerPositionalSound): world one-shots around the player.
    if (zone_ >= 0) {
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

    // Emitters: estimate each one's level at the listener and keep the most audible ones real.
    std::vector<std::pair<float, int>> rank;
    rank.reserve(emitters_.size());
    for (int i = 0; i < (int)emitters_.size(); ++i) {
        Emitter& e = emitters_[(size_t)i];
        float db = e.volDb + 20.0f * std::log10(std::max(1e-6f, inverseGain(core::length(placeFor(e, listener) - listener),
                                                                            e.minM, e.maxM, e.rolloff)));
        e.want = false;
        if (db > kAudibleDb) rank.push_back({db, i});
    }
    std::sort(rank.begin(), rank.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    // Most audible first, at most MaxConcurrentPlayCount instances per cue (the cue's own limit), within
    // the voice budget.
    std::vector<std::pair<std::string, int>> perCue;
    int taken = 0;
    for (size_t r = 0; r < rank.size() && taken < kMaxActive; ++r) {
        Emitter& e = emitters_[(size_t)rank[r].second];
        const cuedata::CueDef* cd = cues.cueDef(e.cue.c_str());
        int* count = nullptr;
        for (auto& pc : perCue) if (pc.first == e.cue) count = &pc.second;
        if (!count) { perCue.push_back({e.cue, 0}); count = &perCue.back().second; }
        if (cd && cd->maxConcurrent > 0 && *count >= cd->maxConcurrent) continue;
        ++*count; ++taken;
        e.want = true;
    }

    active_ = 0;
    for (Emitter& e : emitters_) {
        float target = e.want ? 1.0f : 0.0f;
        e.level += core::clampf(target - e.level, -dt / kEmitterFade, dt / kEmitterFade);
        core::Vec3 at = placeFor(e, listener);
        if (e.level > 0.0f && e.instance < 0 && !e.done) e.instance = cues.play(e.cue.c_str(), at, core::length(at - listener));
        if (e.instance >= 0) {
            if (e.level <= 0.0f || !cues.playing(e.instance)) {
                if (!e.loops && !cues.playing(e.instance)) e.done = true;
                cues.stop(e.instance, 0.0f);
                e.instance = -1;
                continue;
            }
            cues.update(e.instance, at, 0.0f);
            cues.setVolume(e.instance, e.level);
            ++active_;
        }
    }
}

} // namespace game
