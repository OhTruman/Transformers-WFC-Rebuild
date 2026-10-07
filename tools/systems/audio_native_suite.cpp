// Systems M03 pass 5 native-audio validation suite (not part of the CMake build). Build from the repo root:
//   .toolchain/llvm-mingw-*/bin/clang++.exe -std=c++17 -O2 -Isrc tools/systems/audio_native_suite.cpp src/game/SoundCues.cpp \
//     src/game/SoundMixer.cpp src/game/AmbientAudio.cpp src/game/LevelAudioScript.cpp src/game/LevelAudioHost.cpp src/game/MatchAudio.cpp src/game/FrontendAudioRuntime.cpp src/game/MusicPlayer.cpp src/platform/win32/Win32MovieAudio.cpp \n//     src/game/FrontendAudio.cpp src/game/PickupPresentation.cpp src/game/CharacterAudio.cpp src/core/Log.cpp src/platform/win32/Win32Audio.cpp -lwinmm -static -o suite.exe
// Reads ExtractedAssets (read only). Channel-mode checks need an audio device (skipped otherwise).
// Systems M03 native-audio validation suite (RE 7c4a2e0): mixer, zones, emitter geometry, dB, channel modes.
// Deterministic: recording backends for SoundCues / AmbientAudio; the real Win32 backend for channel modes.
#include "game/AmbientAudio.h"
#include "game/SoundCues.h"
#include "game/SoundMixer.h"
#include "game/PickupPresentation.h"
#include "game/FrontendAudio.h"
#include "game/LevelAudioHost.h"
#include "game/FrontendAudioRuntime.h"
#include "game/AbilityAudio.h"
#include "game/CharacterAudio.h"
#include "game/WeaponAudio.h"
#include "audio/MovieAudio.h"
#include <algorithm>
#include <atomic>
#include "assets/Json.h"
#include <chrono>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
using namespace audio;
using core::Vec3;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) ++g_pass; else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// ---- recording backend
struct Rec : IAudio {
    struct V { VoiceParams p; Vec3 pos; bool live = true; float vol = 1.0f; int sndId = -1; };
    std::map<int, V> v;
    std::vector<Environment> envs;
    std::map<std::string, int> paths;
    struct Loop { int sound; uint32_t start, end; };
    std::vector<Loop> loops;
    int n = 0;
    Sound load(const std::string& path) override {
        auto it = paths.find(path);
        if (it != paths.end()) return it->second;
        int id = (int)paths.size(); paths[path] = id; return id;
    }
    bool setLoopPoints(Sound s, uint32_t a, uint32_t b) override { loops.push_back({s, a, b}); return true; }
    void play(Sound, float) override {}
    void playAt(Sound, const Vec3&, float, float, float) override {}
    Voice playVoice(Sound snd, const VoiceParams& p) override { v[n] = {p, p.pos, true, p.volume, snd}; return n++; }
    void stopVoice(Voice h) override { if (v.count(h)) v[h].live = false; }
    void updateVoice(Voice h, float vol, float, const Vec3& pos) override { if (v.count(h)) { v[h].pos = pos; v[h].vol = vol; } }
    void setListener(const Vec3&, const Vec3&, const Vec3&) override {}
    void update() override {}
    void setEnvironment(const Environment& e, float) override { envs.push_back(e); }
};

// ---------------------------------------------------------------- mixer
static void step(game::SoundMixer& m, float t) { m.tick(t); }
static float vol(game::SoundMixer& m) { return m.categoryVolume("SFX_WET_VEH_ENGINE"); }

static assets::Json loadJson(const std::string& p);
static const std::string& streetsAudio();
// A map's reverb presets from its audio.json (same field mapping as AmbientAudio::load).
static int registerMapPresets(game::SoundMixer& m, const assets::Json& aj) {
    int n = 0;
    for (const auto& kv : aj["reverb_presets"].obj) {
        const assets::Json& mp = kv.second["mixer_preset"];
        const assets::Json& d = kv.second["dsp_by_category"]["MASTER_WET"];
        const assets::Json& r = d["Reverb"];
        const assets::Json& e = d["Echo"];
        const float v[game::SoundMixer::kParams] = {
            d["Volume"]["Volume"].asFloat(1.0f), r["Room"].asFloat(-10000.0f), r["RoomHF"].asFloat(-10000.0f),
            r["RoomRolloffFactor"].asFloat(0.0f), r["DecayTime"].asFloat(1.0f), r["DecayHFRatio"].asFloat(1.0f),
            r["ReflectionsLevel"].asFloat(-10000.0f), r["ReflectionsDelay"].asFloat(0.0f), r["Level"].asFloat(-10000.0f),
            r["Delay"].asFloat(0.0f), r["Diffusion"].asFloat(0.0f), r["Density"].asFloat(0.0f), r["HFReference"].asFloat(5000.0f),
            e["Delay"].asFloat(500.0f), e["DecayRatio"].asFloat(0.5f), e["WetMix"].asFloat(0.0f), e["DryMix"].asFloat(1.0f)};
        n += m.addMapPreset(kv.first, mp["Priority"].asFloat(0.0f), mp["FadeInTime"].asFloat(0.0f), mp["FadeOutTime"].asFloat(0.0f),
                            mp["Duration"].asFloat(-1.0f), v) ? 1 : 0;
    }
    return n;
}

static void testMixer() {
    std::printf("[mixer]\n");
    const float J = 0.1258925f, B = 0.6309574f;   // VEHICLE_JUMP -18 dB, VEHICLE_BOOST_END -4 dB (linear amplitude)
    { game::SoundMixer m;
      CHECK(m.activeList() == "Default(1)", "init %s", m.activeList().c_str());
      CHECK(near(vol(m), 1.0f), "default volume");
      // higher-priority activation: Default -> BOOST_END over its FadeIn 0.2, then JUMP (270 > 264) over FadeIn 0.3
      m.enable("VEHICLE_BOOST_END"); step(m, 0.1f);
      CHECK(near(vol(m), 1.0f + (B - 1.0f) * 0.5f), "boost_end mid fade %.4f", vol(m));
      step(m, 0.1f); CHECK(near(vol(m), B), "boost_end end %.4f", vol(m));
      m.enable("VEHICLE_JUMP");
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "VEHICLE_JUMP"), "higher wins: %s", m.categoryTarget("SFX_WET_VEH_ENGINE"));
      step(m, 0.15f); CHECK(near(vol(m), B + (J - B) * 0.5f), "jump mid (from current) %.4f", vol(m));
      step(m, 0.15f); CHECK(near(vol(m), J), "jump end %.4f", vol(m));
      CHECK(near(J, 0.1258925f, 1e-6f), "volume stays linear amplitude (not re-converted)"); }
    { game::SoundMixer m;   // lower-priority activation: no retarget
      m.enable("VEHICLE_JUMP"); step(m, 0.3f);
      m.enable("VEHICLE_BOOST_END");
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "VEHICLE_JUMP"), "lower does not take over: %s", m.categoryTarget("SFX_WET_VEH_ENGINE"));
      step(m, 0.1f); CHECK(near(vol(m), J), "unchanged %.4f", vol(m));
      // step down on removal: outgoing FadeOut (JUMP 1.0)
      m.disable("VEHICLE_JUMP", false);
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "VEHICLE_BOOST_END"), "falls to boost_end");
      step(m, 0.5f); CHECK(near(vol(m), J + (B - J) * 0.5f), "step-down uses outgoing FadeOut 1.0: %.4f", vol(m)); }
    { game::SoundMixer m;   // interrupted fade restarts from the current value with the full new time
      m.enable("VEHICLE_JUMP"); step(m, 0.15f);
      float mid = vol(m);
      CHECK(near(mid, 1.0f + (J - 1.0f) * 0.5f), "mid %.4f", mid);
      m.disable("VEHICLE_JUMP", false);
      CHECK(near(vol(m), mid), "no snap on interrupt %.4f", vol(m));
      step(m, 0.5f); CHECK(near(vol(m), mid + (1.0f - mid) * 0.5f), "linear from current over JUMP FadeOut 1.0: %.4f", vol(m));
      step(m, 0.5f); CHECK(near(vol(m), 1.0f), "reaches Default %.4f", vol(m)); }
    { game::SoundMixer m;   // Duration 1.0 expiry -> Default over the expiring FadeOut
      m.enable("VEHICLE_JUMP"); step(m, 0.5f); CHECK(m.activeList() == "VEHICLE_JUMP(1),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.5f); CHECK(m.activeList() == "Default(1)", "expired at Duration: %s", m.activeList().c_str());
      CHECK(near(vol(m), J + (1.0f - J) * 0.5f), "restore over FadeOut 1.0 %.4f", vol(m));
      step(m, 1.0f); CHECK(near(vol(m), 1.0f), "restored"); }
    { game::SoundMixer m;   // re-enable while active resets the timer; RefCount 2 expires over two ticks
      m.enable("VEHICLE_JUMP"); step(m, 0.8f);
      m.enable("VEHICLE_JUMP"); CHECK(m.activeList() == "VEHICLE_JUMP(2),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.5f); CHECK(m.activeList() == "VEHICLE_JUMP(2),Default(1)", "timer reset on Enable: %s", m.activeList().c_str());
      step(m, 0.5f); CHECK(m.activeList() == "VEHICLE_JUMP(1),Default(1)", "first expiry tick decrements: %s", m.activeList().c_str());
      step(m, 0.016f); CHECK(m.activeList() == "Default(1)", "second tick removes: %s", m.activeList().c_str()); }
    { game::SoundMixer m;   // per-category fall-through: JUMP (270) defines no MASTER_WET -> REVERB EXTERIOR (182)
      CHECK(registerMapPresets(m, loadJson(streetsAudio())) == 10, "Streets map presets registered from audio.json");
      m.enable("REVERB_TRANS_MP_STREETS_EXTERIOR"); m.enable("VEHICLE_JUMP");
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "VEHICLE_JUMP"), "cat0 %s", m.categoryTarget("SFX_WET_VEH_ENGINE"));
      CHECK(!std::strcmp(m.categoryTarget("MASTER_WET"), "REVERB_TRANS_MP_STREETS_EXTERIOR"), "cat1 falls through: %s", m.categoryTarget("MASTER_WET"));
      // reverb parameters ramp linearly in mB: Default Room -10000 -> -800 over FadeIn 0.25
      step(m, 0.125f); Environment e = m.environment();
      CHECK(near(e.room, -5400.0f, 1.0f), "room mid-fade linear in mB %.1f", e.room);
      CHECK(near(e.decayTime, 1.0f + (2.65f - 1.0f) * 0.5f, 1e-3f), "decay mid-fade linear in s %.3f", e.decayTime);
      CHECK(near(e.hfReference, 5000.0f + (6122.0f - 5000.0f) * 0.5f, 0.5f), "HFReference linear in Hz %.1f", e.hfReference);
      // Duration < 0 is infinite
      step(m, 100.0f); CHECK(m.activeList().find("EXTERIOR") != std::string::npos, "Duration -1 stays: %s", m.activeList().c_str()); }

    // Synthetic tables: equal priority, Duration 0, force disable.
    static const game::SoundMixer::PresetDef P[] = {
        {"A", 100, 0.4f, 0.8f, -1}, {"B", 100, 0.2f, 0.6f, -1}, {"Z", 50, 0.1f, 0.1f, 0}, {"C", 200, 0.1f, 0.1f, -1}};
    static const game::SoundMixer::CategoryPreset C0[] = {{"Default", {1}}, {"A", {0.5f}}, {"B", {0.25f}}, {"Z", {0.1f}}};
    static const game::SoundMixer::CategoryPreset C1[] = {{"Default", {1}}, {"C", {0.3f}}};
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("A"); m.enable("B");
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "A"), "equal priority: earlier wins (%s)", m.categoryTarget("SFX_WET_VEH_ENGINE"));
      CHECK(m.activeList() == "A(1),B(1),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.4f); CHECK(near(vol(m), 0.5f), "A value %.3f", vol(m));
      m.disable("A", false);   // B equal priority -> incoming FadeIn 0.2
      step(m, 0.1f); CHECK(near(vol(m), 0.375f), "equal step uses incoming FadeIn %.3f", vol(m));
      m.enable("C"); CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "B"), "C defines no cat0 -> B (%s)", m.categoryTarget("SFX_WET_VEH_ENGINE"));
      CHECK(!std::strcmp(m.categoryTarget("MASTER_WET"), "C"), "cat1 C"); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("B"); m.enable("A");
      CHECK(!std::strcmp(m.categoryTarget("SFX_WET_VEH_ENGINE"), "B"), "equal priority reversed: earlier B wins (%s)", m.categoryTarget("SFX_WET_VEH_ENGINE")); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("Z"); CHECK(m.activeList() == "Z(1),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.016f); CHECK(m.activeList() == "Default(1)", "Duration 0 expires next tick: %s", m.activeList().c_str()); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("A"); m.enable("A"); m.disable("A", true);
      CHECK(m.activeList() == "Default(1)", "force disable removes at RefCount 2: %s", m.activeList().c_str());
      m.enable("A"); m.enable("A"); m.disable("A", false);
      CHECK(m.activeList() == "A(1),Default(1)", "plain disable decrements: %s", m.activeList().c_str()); }
    { game::SoundMixer m;   // Flush (level change)
      registerMapPresets(m, loadJson(streetsAudio()));
      m.activateReverb("REVERB_TRANS_MP_STREETS_EXTERIOR"); m.enable("VEHICLE_JUMP"); m.flush();
      CHECK(m.activeList() == "Default(1)" && m.currentReverb().empty(), "flush: %s", m.activeList().c_str()); }
    { game::SoundMixer m;   // map preset lifecycle: built-in presets only, + map, - map (Flush + forget), + again
      const int base = m.presetCount();
      CHECK(base == 4 && m.mapPresetCount() == 0 && !m.hasPreset("REVERB_TRANS_MP_STREETS_EXTERIOR"),
            "no map preset compiled in (Default + 3 global; %d)", base);
      registerMapPresets(m, loadJson(streetsAudio()));
      m.activateReverb("REVERB_TRANS_MP_STREETS_TRAIN_TUNNEL"); m.enable("VEHICLE_JUMP"); step(m, 0.1f);
      CHECK(m.removeMapPresets() == 10 && m.presetCount() == base && m.activeList() == "Default(1)" && m.currentReverb().empty(),
            "removeMapPresets: Flush + forget (%s)", m.activeList().c_str());
      Environment e = m.environment();
      CHECK(e.room <= -9999.0f && e.reverb <= -9999.0f, "dry Default environment after map removal");
      CHECK(registerMapPresets(m, loadJson(streetsAudio())) == 10 && registerMapPresets(m, loadJson(streetsAudio())) == 0,
            "re-register after removal; a duplicate name is refused"); }
}

// ---------------------------------------------------------------- zones
static bool rayTri(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 e1 = b - a, e2 = c - a, p = core::cross(d, e2);
    float det = core::dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    Vec3 t = o - a;
    float u = core::dot(t, p) * inv; if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = core::cross(t, e1);
    float v = core::dot(d, q) * inv; if (v < 0.0f || u + v > 1.0f) return false;
    return core::dot(e2, q) * inv > 0.0f;
}
struct ZoneGeo { std::string name, preset; std::vector<Vec3> tris; Vec3 mn, mx; float room = 0; };
static bool insideZ(const ZoneGeo& z, const Vec3& p) {
    if (p.x < z.mn.x || p.y < z.mn.y || p.z < z.mn.z || p.x > z.mx.x || p.y > z.mx.y || p.z > z.mx.z) return false;
    const Vec3 d = core::normalize(Vec3{1.0f, 0.0137f, 0.0291f});
    int h = 0;
    for (size_t i = 0; i + 2 < z.tris.size(); i += 3) if (rayTri(p, d, z.tris[i], z.tris[i + 1], z.tris[i + 2])) ++h;
    return h & 1;
}

static const std::string kRoot = "F:/Transformers Rebuild/ExtractedAssets/VerticalSlice";
static const std::string& streetsAudio() { static const std::string p = kRoot + "/Maps/MP_IAC_Streets/audio.json"; return p; }
static assets::Json loadJson(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
    assets::Json j; assets::Json::parse(ss.str(), j); return j;
}

static void testZones() {
    std::printf("[zones]\n");
    assets::Json j = loadJson(kRoot + "/Maps/MP_IAC_Streets/audio.json");
    std::vector<ZoneGeo> zs;
    for (size_t i = 0; i < j["zones"].size(); ++i) {
        const assets::Json& z = j["zones"][i];
        ZoneGeo g; g.name = z["comment"].asString(); g.preset = z["reverb_preset"].asString();
        g.room = j["reverb_presets"][g.preset]["dsp_by_category"]["MASTER_WET"]["Reverb"]["Room"].asFloat(0);
        g.mn = {1e30f, 1e30f, 1e30f}; g.mx = {-1e30f, -1e30f, -1e30f};
        const assets::Json& polys = z["trigger_polygons_gltf"];
        for (size_t p = 0; p < polys.size(); ++p)
            for (size_t v = 1; v + 1 < polys[p].size(); ++v)
                for (size_t k : {(size_t)0, v, v + 1}) {
                    Vec3 t{polys[p][k][0].asFloat(), polys[p][k][1].asFloat(), polys[p][k][2].asFloat()};
                    g.tris.push_back(t);
                    g.mn = {std::min(g.mn.x, t.x), std::min(g.mn.y, t.y), std::min(g.mn.z, t.z)};
                    g.mx = {std::max(g.mx.x, t.x), std::max(g.mx.y, t.y), std::max(g.mx.z, t.z)};
                }
        zs.push_back(g);
    }
    CHECK(zs.size() == 9, "9 Streets zones (%zu)", zs.size());
    // Sample points: an exclusive interior point per zone; one nested pair (inner point inside two zones).
    std::srand(7);
    auto rnd = [](float a, float b) { return a + (b - a) * (float)std::rand() / (float)RAND_MAX; };
    std::vector<Vec3> excl(zs.size(), Vec3{1e9f, 0, 0});
    int nestIn = -1, nestOut = -1; Vec3 nestP{}, outerOnly{};
    for (size_t z = 0; z < zs.size(); ++z)
        for (int k = 0; k < 20000; ++k) {
            Vec3 p{rnd(zs[z].mn.x, zs[z].mx.x), rnd(zs[z].mn.y, zs[z].mx.y), rnd(zs[z].mn.z, zs[z].mx.z)};
            if (!insideZ(zs[z], p)) continue;
            std::vector<int> in;
            for (size_t o = 0; o < zs.size(); ++o) if (insideZ(zs[o], p)) in.push_back((int)o);
            if (in.size() == 1 && excl[z].x > 1e8f) excl[z] = p;
            if (in.size() == 2 && nestIn < 0) { nestIn = (int)z; nestOut = in[0] == (int)z ? in[1] : in[0]; nestP = p; }
            if (excl[z].x < 1e8f && nestIn >= 0) break;
        }
    for (size_t z = 0; z < zs.size(); ++z) CHECK(excl[z].x < 1e8f, "exclusive point for %s", zs[z].name.c_str());
    const Vec3 nowhere{0.0f, 5000.0f, 0.0f};
    for (const ZoneGeo& z : zs) CHECK(!insideZ(z, nowhere), "nowhere outside %s", z.name.c_str());

    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::AmbientAudio amb;
    amb.setPreferManifestZones(true);                 // the AssetTools flattened zones (graph equivalence: testZoneGraph)
    amb.load(kRoot + "/Maps/MP_IAC_Streets/audio.json", kRoot + "/../content/", cues, &rec);
    game::SoundMixer& m = cues.mixer();
    auto frame = [&](const Vec3& pawn, float dt = 1.0f / 60.0f) {
        Vec3 probe = pawn - Vec3{0, 1.0f, 0};   // AmbientAudio probes pawn + 1 m
        amb.tick(dt, probe, probe, cues); cues.tick(dt);
    };
    auto settle = [&](const Vec3& pawn, float secs) { for (float t = 0; t < secs; t += 1.0f / 60.0f) frame(pawn); };
    // Default before the first Touch.
    settle(nowhere, 0.5f);
    CHECK(m.currentReverb().empty() && m.activeList() == "Default(1)", "default before first touch: %s", m.activeList().c_str());
    CHECK(!rec.envs.empty() && rec.envs.back().room <= -9999.0f, "dry default environment");
    // All 9 zones, each switch from the previous zone (explicit Disable: exactly one REVERB_* active).
    int order[9] = {3, 8, 0, 4, 7, 2, 1, 5, 6};
    int prevZ = -1;
    for (int z : order) {
        settle(nowhere, 0.2f);
        frame(excl[(size_t)z]);
        CHECK(!std::strcmp(amb.zoneName(), zs[(size_t)z].name.c_str()), "entered %s (now %s)", zs[(size_t)z].name.c_str(), amb.zoneName());
        CHECK(m.currentReverb() == zs[(size_t)z].preset, "current reverb %s", m.currentReverb().c_str());
        std::string want = zs[(size_t)z].preset + "(1),Default(1)";
        CHECK(m.activeList() == want, "previous preset explicitly disabled: [%s]", m.activeList().c_str());
        CHECK(!std::strcmp(m.categoryTarget("MASTER_WET"), zs[(size_t)z].preset.c_str()), "MASTER_WET target %s", m.categoryTarget("MASTER_WET"));
        settle(excl[(size_t)z], 0.3f);
        CHECK(near(rec.envs.back().room, zs[(size_t)z].room, 0.5f), "%s room %.0f (audio.json %.0f)", zs[(size_t)z].name.c_str(), rec.envs.back().room, zs[(size_t)z].room);
        prevZ = z;
    }
    // Leaving all volumes: no Exit link -> reverb stays.
    settle(nowhere, 2.0f);
    CHECK(m.currentReverb() == zs[(size_t)prevZ].preset && !std::strcmp(amb.zoneName(), zs[(size_t)prevZ].name.c_str()), "leaving keeps %s", m.currentReverb().c_str());
    CHECK(near(rec.envs.back().room, zs[(size_t)prevZ].room, 0.5f), "environment unchanged after leaving");
    // Repeated entry into the same zone: no Enable, no ref-count change, no environment update.
    size_t envN = rec.envs.size();
    frame(excl[(size_t)prevZ]); settle(excl[(size_t)prevZ], 0.5f);
    CHECK(m.activeList() == zs[(size_t)prevZ].preset + "(1),Default(1)" && rec.envs.size() == envN, "re-entry no-op: [%s] env updates %zu", m.activeList().c_str(), rec.envs.size() - envN);
    // Higher -> lower priority switch (DEC_ROOM_UPPER 223 -> EXTERIOR 182): outgoing FadeOut 0.25, linear in mB.
    int up = -1, ex = -1;
    for (size_t z = 0; z < zs.size(); ++z) { if (zs[z].preset.find("DEC_ROOM_UPPER") != std::string::npos) up = (int)z; if (zs[z].preset.find("EXTERIOR") != std::string::npos) ex = (int)z; }
    settle(nowhere, 0.1f); frame(excl[(size_t)up]); settle(excl[(size_t)up], 0.4f);
    settle(nowhere, 0.1f); frame(excl[(size_t)ex]);
    float r0 = zs[(size_t)up].room, r1 = zs[(size_t)ex].room;
    for (int k = 0; k < 7; ++k) frame(excl[(size_t)ex]);   // 8 frames of 1/60 s = 0.1333 s since the switch
    CHECK(near(rec.envs.back().room, r0 + (r1 - r0) * (8.0f / 60.0f) / 0.25f, 2.0f), "step-down mid fade %.1f (expect %.1f)", rec.envs.back().room, r0 + (r1 - r0) * (8.0f / 60.0f) / 0.25f);
    CHECK(m.activeList() == zs[(size_t)ex].preset + "(1),Default(1)", "lower-priority zone wins after explicit disable: [%s]", m.activeList().c_str());
    // Nested / overlapping: outer-only -> overlap (Touch inner) -> outer-only (no new Touch): inner persists.
    CHECK(nestIn >= 0, "found an overlapping pair");
    if (nestIn >= 0) {
        const ZoneGeo& O = zs[(size_t)nestOut];
        bool found = false;
        for (int k = 0; k < 200000 && !found; ++k) {
            Vec3 p{rnd(O.mn.x, O.mx.x), rnd(O.mn.y, O.mx.y), rnd(O.mn.z, O.mx.z)};
            if (!insideZ(O, p)) continue;
            int cnt = 0; for (const ZoneGeo& z : zs) cnt += insideZ(z, p) ? 1 : 0;
            if (cnt == 1) { outerOnly = p; found = true; }
        }
        CHECK(found, "outer-only point");
        // first make some third zone current so the outer entry is a real switch
        int third = 0; while (third == nestIn || third == nestOut) ++third;
        settle(nowhere, 0.1f); frame(excl[(size_t)third]); settle(nowhere, 0.1f);
        frame(outerOnly); settle(outerOnly, 0.1f);
        CHECK(!std::strcmp(amb.zoneName(), O.name.c_str()), "in outer %s (now %s)", O.name.c_str(), amb.zoneName());
        frame(nestP); settle(nestP, 0.1f);
        CHECK(!std::strcmp(amb.zoneName(), zs[(size_t)nestIn].name.c_str()), "touch inner %s wins (now %s)", zs[(size_t)nestIn].name.c_str(), amb.zoneName());
        frame(outerOnly); settle(outerOnly, 0.5f);
        CHECK(!std::strcmp(amb.zoneName(), zs[(size_t)nestIn].name.c_str()) && m.currentReverb() == zs[(size_t)nestIn].preset,
              "back in outer without a new Touch: inner reverb persists (%s)", m.currentReverb().c_str());
        std::printf("  nested pair: inner %s / outer %s\n", zs[(size_t)nestIn].name.c_str(), O.name.c_str());
    }
    // Cost of the per-frame Touch tests + emitter placement (AmbientAudio::tick + SoundCues::tick), inside the
    // largest (non-convex) volume and in the open.
    for (int w = 0; w < 2; ++w) {
        const Vec3 at = w == 0 ? excl[(size_t)ex] : nowhere;
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 600; ++k) frame(at);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 600.0;
        std::printf("  zone+emitter tick cost %.3f ms/frame (%s)\n", ms, w == 0 ? "in EXTERIOR" : "open");
    }
    // Level reset / change: mixer Flush -> Default, no reverb.
    game::AmbientAudio amb2;
    amb2.load(kRoot + "/Maps/MP_IAC_Streets/audio.json", kRoot + "/../content/", cues, &rec);
    cues.tick(0.6f);
    CHECK(m.activeList() == "Default(1)" && m.currentReverb().empty(), "reset: [%s]", m.activeList().c_str());
    CHECK(rec.envs.back().room <= -9999.0f, "reset restores dry environment (%.0f)", rec.envs.back().room);
}

// ---------------------------------------------------------------- emitter geometry
static Vec3 ue2g(const Vec3& u) { return {u.x * 0.01f, u.z * 0.01f, u.y * 0.01f}; }
static Vec3 row(const assets::Json& m, int r) { return {m[(size_t)r][0].asFloat(), m[(size_t)r][1].asFloat(), m[(size_t)r][2].asFloat()}; }

static void testEmitters() {
    std::printf("[emitters]\n");
    assets::Json j = loadJson(kRoot + "/Maps/MP_IAC_Streets/audio.json");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    std::vector<Vec3> occlTo;
    cues.setOcclusion([&](const Vec3&, const Vec3& to, int) { occlTo.push_back(to); return false; });
    game::AmbientAudio amb;
    amb.load(kRoot + "/Maps/MP_IAC_Streets/audio.json", kRoot + "/../content/", cues, &rec);
    auto hasVoiceAt = [&](const Vec3& g, float eps) {
        for (auto& kv : rec.v) if (kv.second.live && core::length(kv.second.pos - g) < eps) return true;
        return false;
    };
    auto occlAt = [&](const Vec3& g) { for (const Vec3& o : occlTo) if (core::length(o - g) < 0.02f) return true; return false; };
    auto occludes = [&](const std::string& cue) { return j["cues"][cue]["tree"]["params"]["EnableOcclusionVolume"].asBool(true); };
    // Occlusion follows the runtime point: occluding cues trace to P (never the actor origin); cues authored
    // EnableOcclusionVolume=False never trace.
    auto checkOccl = [&](const char* what, const std::string& cue, const Vec3& P, const Vec3& origin) {
        bool at = occlAt(P), atOrigin = core::length(P - origin) > 0.5f && occlAt(origin);
        if (occludes(cue)) CHECK(at && !atOrigin, "%s: occlusion traced to the runtime point (%zu traces)", what, occlTo.size());
        else CHECK(!at && !atOrigin, "%s: EnableOcclusionVolume=False -> no trace", what);
    };
    auto tickAt = [&](const Vec3& lg) { for (int k = 0; k < 3; ++k) { cues.setListener(lg); amb.tick(1.0f / 60.0f, lg, Vec3{0, 5000, 0}, cues); cues.tick(1.0f / 60.0f); } };

    // Line emitter: native A = O - X*(L/2), B = O + X*(L/2) (X includes DrawScale3D), clamped projection.
    const assets::Json& line = j["emitters"]["line"][0];
    Vec3 O = row(line["ue_matrix"], 3), X = row(line["ue_matrix"], 0), Y = row(line["ue_matrix"], 1);
    float L = line["linelength"].asFloat(500.0f);
    Vec3 A = O - X * (L * 0.5f), Bp = O + X * (L * 0.5f);
    Vec3 yd = core::normalize(Y), xd = core::normalize(X);
    struct Case { const char* what; float along, off; };
    const Case cases[] = {{"perpendicular at middle", 0.0f, 800.0f}, {"parallel travel 1", -100.0f, 800.0f}, {"parallel travel 2", 120.0f, 800.0f},
                          {"beyond end A", -2000.0f, 600.0f}, {"beyond end B", 2000.0f, 600.0f}, {"perpendicular far", 50.0f, 2500.0f}};
    bool moved = false; Vec3 last{};
    for (const Case& c : cases) {
        Vec3 Lu = O + xd * c.along + yd * c.off;
        Vec3 ab = Bp - A; float t = core::clampf(core::dot(Lu - A, ab) / core::dot(ab, ab), 0.0f, 1.0f);
        Vec3 P = ue2g(A + ab * t);
        occlTo.clear();
        for (int k = 0; k < 7; ++k) tickAt(ue2g(Lu));
        CHECK(hasVoiceAt(P, 0.01f), "line %s: source at closest point t=%.3f", c.what, t);
        if (core::length(P - ue2g(O)) > 0.5f) CHECK(!hasVoiceAt(ue2g(O), 0.01f), "line %s: not at the actor origin", c.what);
        if (std::strstr(c.what, "beyond")) CHECK(t == 0.0f || t == 1.0f, "line %s: clamped to the endpoint (t=%.2f)", c.what, t);
        if (moved && core::length(P - last) > 0.05f) CHECK(!hasVoiceAt(last, 0.01f), "line %s: source moved with the listener", c.what);
        checkOccl(c.what, line["cue"].asString(), P, ue2g(O));
        last = P; moved = true;
    }
    // Volume emitters: listener in actor-local space clamped to +/-Radius per axis (oriented box).
    int nVol = (int)j["emitters"]["volume"].size(), tested = 0, oriented = 0;
    for (int vi = 0; vi < nVol && tested < 4; ++vi) {
        const assets::Json& ve = j["emitters"]["volume"][(size_t)vi];
        Vec3 o = row(ve["ue_matrix"], 3), R[3] = {row(ve["ue_matrix"], 0), row(ve["ue_matrix"], 1), row(ve["ue_matrix"], 2)};
        float rad = ve["radius"].asFloat(500.0f);
        bool rot = std::fabs(ve["rotation_ue"][1].asFloat()) > 1.0f;
        if (tested > 0 && !rot && oriented < 2) continue;   // after the first, prefer rotated boxes
        auto toLocal = [&](const Vec3& w) {   // solve w - o = l.x R0 + l.y R1 + l.z R2
            Vec3 d = w - o, c0 = core::cross(R[1], R[2]);
            float det = core::dot(R[0], c0);
            return Vec3{core::dot(d, c0) / det, core::dot(R[0], core::cross(d, R[2])) / det, core::dot(R[0], core::cross(R[1], d)) / det};
        };
        auto toWorld = [&](const Vec3& l) { return o + R[0] * l.x + R[1] * l.y + R[2] * l.z; };
        // inside: P = listener; outside along a diagonal: P = clamped corner region (not on a sphere)
        Vec3 inside = toWorld({rad * 0.3f, -rad * 0.2f, 0.0f});
        tickAt(ue2g(inside));
        CHECK(hasVoiceAt(ue2g(inside), 0.01f), "volume[%d] listener inside -> source at listener", vi);
        Vec3 out = toWorld({rad * 1.04f, rad * 1.03f, rad * 0.2f});   // just outside a corner edge
        Vec3 l = toLocal(out);
        Vec3 Pl{core::clampf(l.x, -rad, rad), core::clampf(l.y, -rad, rad), core::clampf(l.z, -rad, rad)};
        Vec3 P = toWorld(Pl);
        occlTo.clear();
        for (int k = 0; k < 7; ++k) tickAt(ue2g(out));   // > OcclusionCheckInterval 0.25 s
        CHECK(hasVoiceAt(ue2g(P), 0.01f), "volume[%d]%s outside diagonal -> box corner edge point", vi, rot ? " (rotated)" : "");
        float sphereR = rad * core::length(R[0]);
        CHECK(core::length(P - o) > sphereR * 1.01f, "volume[%d] box, not a sphere of the X extent (%.0f vs %.0f UU)", vi, core::length(P - o), sphereR);
        CHECK(!hasVoiceAt(ue2g(o), 0.01f), "volume[%d] not at the actor origin", vi);
        char nm[64]; std::snprintf(nm, sizeof nm, "volume[%d] %s", vi, ve["cue"].asString().c_str() + 22);
        checkOccl(nm, ve["cue"].asString(), ue2g(P), ue2g(o));
        ++tested; if (rot) ++oriented;
    }
    std::printf("  volume emitters tested %d (rotated %d)\n", tested, oriented);
}

// ---------------------------------------------------------------- dB conversion + k2D through SoundCues
static void testGain() {
    std::printf("[gain]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    const char* js = R"({
      "T.DB": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": 0.0, "SpatializationType": "k2D"}, "children": [
        {"class": "SoundNodeWaveEvent", "params": {"Volume": -96.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]},
        {"class": "SoundNodeWaveEvent", "params": {"Volume": -6.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]},
        {"class": "SoundNodeWaveEvent", "params": {"Volume": 0.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]},
        {"class": "SoundNodeWaveEvent", "params": {"Volume": 3.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]},
        {"class": "SoundNodeWaveEvent", "params": {"Volume": -97.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]}]}},
      "T.3D": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": 0.0}, "children": [
        {"class": "SoundNodeWaveEvent", "params": {"Volume": 0.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]}]}}})";
    assets::Json cj; assets::Json::parse(js, cj);
    cues.addCues(cj, kRoot + "/../content/");
    int first = rec.n;
    cues.play("T.DB", Vec3{100, 0, 0}, 100.0f);
    // -96 / -97 dB layers are exactly 0 -> a silent one-shot layer launches no voice.
    CHECK(rec.n - first == 3, "-96 / -97 dB silent: 3 of 5 layers sound (%d)", rec.n - first);
    const float want[3] = {0.5011872f, 1.0f, 1.0f};
    const char* lab[3] = {"-6 dB", "0 dB", "+3 dB clamps to unity"};
    for (int i = 0; i < 3 && first + i < rec.n; ++i)
        CHECK(near(rec.v[first + i].p.volume, want[i], 1e-5f), "%s: %.6f", lab[i], rec.v[first + i].p.volume);
    CHECK(!rec.v[first].p.positional && rec.v[first].p.spatial == 1, "k2D cue plays non-positional (FMOD_2D)");
    int f3 = rec.n; cues.play("T.3D", Vec3{10, 0, 0}, 10.0f);
    CHECK(rec.n > f3 && rec.v[f3].p.positional && rec.v[f3].p.spatial == 0, "unauthored Spatialization = k3D positional");
    // AActor::PlaySound / USoundCue::IsAudible: a positional one-shot beyond DistanceMax (unauthored: 6400 UU = 64 m) is not
    // started at all; a 2D cue at the same place still is.
    int skip0 = cues.inaudibleSkipped(), f4 = rec.n;
    CHECK(cues.play("T.3D", Vec3{100, 0, 0}, 100.0f) < 0 && rec.n == f4 && cues.inaudibleSkipped() == skip0 + 1,
          "positional one-shot beyond its audible distance: not started (%d voices)", rec.n - f4);
    f4 = rec.n; cues.play("T.DB", Vec3{100, 0, 0}, 100.0f);
    CHECK(rec.n - f4 == 3, "2D cue far away still plays (%d)", rec.n - f4);
    // Authored positive variation (FOLEY.SHOOT_DRY_FIRE_ELECTRICITY, -1..+1 dB): never above the 0 dB level.
    // Root -9 dB x event 0 dB x variation dBToLinear(-1..+1) -> max exactly 10^(-9/20), about half clamped.
    float mx = 0.0f, mn = 1.0f; int atMax = 0, n = 0;
    for (int k = 0; k < 400; ++k) {
        cues.play("FOLEY.SHOOT_DRY_FIRE_ELECTRICITY", Vec3{0, 0, 0}, 0.0f);
        cues.tick(0.1f);                       // events at 0 and 0.0297 s
        int s = rec.n; cues.tick(0.05f);       // the variation layer (Time 0.1335 s) alone
        if (rec.n == s + 1) { float v = rec.v[s].p.volume; mx = std::max(mx, v); mn = std::min(mn, v); ++n; if (near(v, 0.3548134f, 1e-5f)) ++atMax; }
        cues.tick(0.5f);
    }
    CHECK(n > 300 && near(mx, 0.3548134f, 1e-5f), "positive variation never boosts above 0 dB (max %.6f over %d)", mx, n);
    CHECK(atMax > n / 3 && near(mn, 0.3548134f * 0.8912509f, 2e-3f), "about half clamp at 0 dB (%d/%d), min %.5f ~ -1 dB", atMax, n, mn);
}

// ---------------------------------------------------------------- channel modes (real Win32 backend)
static void testChannelModes() {
    std::printf("[channel modes - Win32 backend]\n");
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP: no audio device\n"); return; }
    Sound s = a->load(kRoot + "/../content/WL_ELEC/ELEC_TRANS_TV_03.wav");
    CHECK(s != kInvalidSound, "wave loaded");
    a->setListener(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{1, 0, 0});
    a->setSmartPanPlayer(Vec3{0, 0, 0}, false);
    auto vp = [](int spatial, Vec3 pos) { VoiceParams p; p.volume = 1; p.positional = spatial != 1; p.pos = pos; p.minDist = 4; p.maxDist = 64;
                                          p.rolloff = 1; p.pan2D = 2; p.pan3D = 4; p.loop = true; p.spatial = spatial; return p; };
    auto info = [&](Voice v) { std::this_thread::sleep_for(std::chrono::milliseconds(120)); VoiceInfo i; a->voiceInfo(v, i); return i; };
    { Voice v = a->playVoice(s, vp(1, Vec3{1000, 0, 0})); VoiceInfo i = info(v);
      CHECK(near(i.atten, 1.0f) && near(i.pan, 0.0f), "k2D: no attenuation / no position at 1000 m (atten %.3f pan %.2f)", i.atten, i.pan); a->stopVoice(v); }
    { Voice v = a->playVoice(s, vp(0, Vec3{10, 0, 0})); VoiceInfo i = info(v);
      CHECK(near(i.atten, 0.4f) && near(i.pan, 1.0f), "k3D: inverse rolloff 0.4, pan level 1 (atten %.3f pan %.2f)", i.atten, i.pan);
      a->updateVoice(v, 1, 1, Vec3{-10, 0, 0}); i = info(v);
      CHECK(near(i.pan, -1.0f), "moving source: pan follows (%.2f)", i.pan);
      a->updateVoice(v, 1, 1, Vec3{0, 0, 10}); i = info(v);
      VoiceParams r = vp(0, Vec3{0, 0, 10}); (void)r;
      CHECK(near(i.atten, 0.4f), "behind, RearAttenuation 0 -> no change (%.3f)", i.atten);
      a->updateVoice(v, 1, 1, Vec3{70, 0, 0}); i = info(v);
      CHECK(near(i.atten, 0.0f), "beyond DistanceMax: culled, no floor (%.3f)", i.atten); a->stopVoice(v); }
    { VoiceParams p = vp(0, Vec3{0, 0, 10}); p.rearAttenDb = -6.0f; Voice v = a->playVoice(s, p); VoiceInfo i = info(v);
      CHECK(near(i.atten, 0.4f * 0.5011872f, 1e-3f), "rear attenuation -6 dB directly behind (%.4f)", i.atten); a->stopVoice(v); }
    { VoiceParams p = vp(2, Vec3{3, 0, 0}); p.minDist = 1; p.panAtten3DDb = -6.0f; Voice v = a->playVoice(s, p); VoiceInfo i = info(v);
      float att = 1.0f / ((3 - 1) * 1 + 1) * (1.0f - (1.0f - 0.5011872f) * 0.5f);
      CHECK(near(i.pan, 0.5f, 2e-3f) && near(i.atten, att, 1e-3f), "SmartPan: pan level 0.5 at 3 m, attenuation kept (pan %.3f atten %.4f vs %.4f)", i.pan, i.atten, att);
      a->stopVoice(v); }
    { // PreferPlayer: player at the source (within 14 m of the camera) -> reference = player -> 2D; volume by listener
      a->setSmartPanPlayer(Vec3{10, 0, 0}, true);
      std::this_thread::sleep_for(std::chrono::milliseconds(700));
      VoiceParams p = vp(3, Vec3{10, 0, 0}); p.panAtten3DDb = -3.0f; p.pan2D = 2; p.pan3D = 40;
      Voice v = a->playVoice(s, p); VoiceInfo i = info(v);
      CHECK(near(i.pan, 0.0f, 1e-3f) && near(i.atten, 0.4f, 1e-3f) && near(i.dist, 10.0f, 1e-3f),
            "PreferPlayer near: pan 0, volume by listener->true source (pan %.3f atten %.3f dist %.2f)", i.pan, i.atten, i.dist);
      // camera moves to 20 m from the player (>= 14 m): reference ramps player -> listener linearly over 0.5 s
      a->setListener(Vec3{-10, 0, 0}, Vec3{0, 0, -1}, Vec3{1, 0, 0});
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      VoiceInfo mid; a->voiceInfo(v, mid);
      std::this_thread::sleep_for(std::chrono::milliseconds(700));
      a->voiceInfo(v, i);
      const float amt = (20.0f - 2.0f) / 38.0f;
      CHECK(mid.pan > 0.02f && mid.pan < amt - 0.05f, "PreferPlayer transition in progress at ~0.15 s (pan %.3f, final %.3f)", mid.pan, amt);
      CHECK(near(i.pan, amt, 2e-3f) && near(i.atten, 0.2f * (1.0f - (1.0f - 0.7079458f) * amt), 1e-3f) && near(i.dist, 20.0f, 1e-3f),
            "PreferPlayer far: listener reference (pan %.3f atten %.4f dist %.2f)", i.pan, i.atten, i.dist);
      a->stopVoice(v); }
    delete a;
}

// ---------------------------------------------------------------- AssetTools 7a69756 authored data
static const std::string kMan = "F:/Transformers Rebuild/AssetTools/manifests/";
static std::string tableName(const std::string& full) {   // tools/systems/gen_cues.py short()
    std::string pkg = full.substr(0, full.find('.')), name = full.substr(full.find('.') + 1);
    if (pkg == "BL_WPN_GUN_ION_BLASTER" || pkg == "BL_VEH_OPTIMUS_PRIME" || pkg == "BL_VEH_SOUNDWAVE") return name;
    if (pkg == "BL_WPN_FOLEY") return "FOLEY." + name;
    return full;
}

static void testAuthored() {
    std::printf("[authored data 7a69756]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::AmbientAudio amb;
    amb.load(kRoot + "/Maps/MP_IAC_Streets/audio.json", kRoot + "/../content/", cues, &rec);

    // Footsteps / landing: every Streets surface resolves to the same FS_DEFAULT_* events; Optimus maps them
    // to BL_FS_LRG_BOT. The table holds exactly those cues and nothing surface-specific.
    assets::Json sa = loadJson(kMan + "streets_surface_audio.json");
    CHECK(sa["headline"]["distinct_footstep_tables_across_streets_physmats"].asInt(0) == 1, "one footstep table across Streets physmats");
    int mapped = 0;
    for (const auto& kv : sa["event_to_cue (Optimus robot)"].obj) {
        const std::string cue = kv.second["cue"].asString();
        if (cue.empty()) continue;          // unmapped (JOG / CROUCH_*): silent
        ++mapped;
        CHECK(cues.hasCue(cue.c_str()), "footstep event %s -> %s in the cue table", kv.first.c_str(), cue.c_str());
    }
    CHECK(mapped == 8, "8 mapped Optimus footstep/landing events (%d)", mapped);
    for (const auto& kv : sa["physical_materials"].obj)
        for (const auto& sl : kv.second["slots"].obj) {
            std::string ev = sl.second["event"].asString();
            CHECK(ev.empty() || ev.rfind("SoundEvents_Footsteps.FS_DEFAULT_", 0) == 0, "%s %s -> %s is a default event",
                  kv.first.c_str(), sl.first.c_str(), ev.c_str());
        }
    for (const char* surf : {"CONCRETE", "METAL", "MTL", "DIRT", "WATER", "GRASS", "WOOD"}) {
        std::string c1 = std::string("BL_FS_LRG_BOT.FS_WALK_") + surf;
        CHECK(!cues.hasCue(c1.c_str()), "no surface variant %s", c1.c_str());
    }

    // Concurrency: authored MaxConcurrentPlayCount / InstanceLimiting (or the inherited Engine.Default__SoundCue
    // 5 / kKillFarthest) for every manifest cue the rebuild plays.
    assets::Json cc = loadJson(kMan + "vertical_slice_audio_concurrency.json");
    int compared = 0;
    for (size_t i = 0; i < cc["cues"].size(); ++i) {
        const assets::Json& c = cc["cues"][i];
        std::string tn = tableName(c["cue"].asString());
        const game::cuedata::CueDef* d = cues.cueDef(tn.c_str());
        if (!d) continue;
        ++compared;
        int mc = c["cue_fields"]["MaxConcurrentPlayCount"]["value"].asInt(-1);
        std::string il = c["cue_fields"]["InstanceLimiting"]["value"].asString();
        game::cuedata::Limit want = il == "kKillOldest" ? game::cuedata::Limit::KillOldest
                                  : il == "kKillNewest" ? game::cuedata::Limit::KillNewest : game::cuedata::Limit::KillFarthest;
        CHECK(d->maxConcurrent == mc && d->limit == want, "%s: max %d (authored %d) limit %d (authored %s)", tn.c_str(),
              d->maxConcurrent, mc, (int)d->limit, il.c_str());
    }
    std::printf("  concurrency compared for %d cues\n", compared);
    CHECK(compared >= 40, "concurrency coverage (%d)", compared);

    // Vehicle loops: the FSB header region of every looping vehicle wave (whole sample) is applied at load.
    assets::Json vl = loadJson(kMan + "vehicle_audio_loops.json");
    CHECK(vl["census"]["with_custom_loop_range"].asInt(-1) == 0 && vl["census"]["with_LOOP_mode_flag"].asInt(-1) == 0,
          "FSB census: no custom ranges / loop flags");
    CHECK(rec.loops.size() == 7, "7 looping vehicle waves get their FSB loop region (%zu)", rec.loops.size());
    for (const auto& l : rec.loops) {
        std::string path;
        for (const auto& kv : rec.paths) if (kv.second == l.sound) path = kv.first;
        std::string key = path.substr(path.find("content/") + 8);
        key = key.substr(0, key.size() - 4);
        key[key.find('/')] = '.';
        const assets::Json& w = vl["waves"][key];
        CHECK(l.start == 0 && (int)l.end == w["total_samples"].asInt(-1) - 1 && (int)l.end == w["loop"]["loopend_sample"].asInt(-1),
              "%s loop [%u, %u] = whole sample (%d)", key.c_str(), l.start, l.end, w["total_samples"].asInt(-1));
    }

    // Pickups: every factory placed in the map (gameplay.json) resolves its class's authored PickupSound.
    using PP = game::PickupPresentation;
    assets::Json gp = loadJson(kRoot + "/Maps/MP_IAC_Streets/gameplay.json");
    std::map<std::string, int> placed;
    for (size_t i = 0; i < gp["pickups"].size(); ++i) ++placed[gp["pickups"][i]["class"].asString()];
    CHECK(placed["TnAmmoCratePickupFactory"] == 14 && placed["TnHealthPickupFactory"] == 9 && placed["TnOverShieldPickupFactory"] == 1,
          "placed 14 ammo / 9 health / 1 overshield");
    for (const auto& kv : placed) {
        const char* snd = PP::pickupSoundFor(kv.first.c_str());
        CHECK(snd && cues.hasCue(snd), "%s -> PickupSound %s in the cue table", kv.first.c_str(), snd ? snd : "-");
    }
    CHECK(PP::pickupSoundFor("TnGameObjectivePickupFactoryFlag") == nullptr && PP::pickupSoundFor("TnGameObjectivePickupFactoryBomb") == nullptr,
          "objective inventories author no PickupSound");
    // PickupSound is attached to the recipient pawn: its voices follow the pawn.
    Vec3 pawn{10, 0, 0};
    cues.setResolver([&](int owner, const std::string&, const Vec3& off, Vec3& out) {
        if (owner != 0) return false;
        out = pawn + off; return true;
    });
    int first = rec.n;
    game::SoundCues::Emitter recipient{pawn, 0, {0, 0, 0}, ""};
    int id = PP::onTaken("TnAmmoCratePickupFactory", cues, recipient, 5.0f);
    CHECK(PP::onTaken("NoSuchFactory", cues, recipient, 5.0f) == -1, "unknown factory class ignored");
    CHECK(PP::pickupSoundFor("TnHealthPickupFactory_13806") && !std::strcmp(PP::pickupSoundFor("TnHealthPickupFactory_13806"), "BL_HUD_INTERFACE.HEALTH_PU_ENERGON") &&
          !PP::pickupSoundFor("TnHealthPickupFactoryX") && !PP::pickupSoundFor("TnHealthPickupFactory_") && !PP::pickupSoundFor("TnHealthPickupFactory_12a"),
          "placed actor names resolve to their class (<Class>_<N>); other suffixes do not");
    CHECK(id >= 0 && rec.n > first, "ammo pickup sound plays (%d voices)", rec.n - first);
    pawn = Vec3{14, 0, 3};
    for (int k = 0; k < 3; ++k) cues.tick(1.0f / 60.0f);
    bool follows = rec.n > first;
    for (int v = first; v < rec.n; ++v) if (rec.v[v].live && core::length(rec.v[v].pos - pawn) > 1e-3f) follows = false;
    CHECK(follows, "pickup sound follows the recipient");
}

// ---------------------------------------------------------------- vehicle loop runtime (RE d50c2a9 P1)
static void testLoopRuntime() {
    std::printf("[vehicle loop runtime]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    // Loop enable is per wave event: DRIVE_ONLOAD's events are bLooping, BOOST_END's are not.
    const game::cuedata::CueDef* on = cues.cueDef("VEH_OPTIMUS_DRIVE_ONLOAD");
    const game::cuedata::CueDef* end = cues.cueDef("VEH_OPTIMUS_BOOST_END");
    bool allLoop = on && !on->events.empty(), noneLoop = end && !end->events.empty();
    for (const auto& e : on->events) allLoop = allLoop && e.loop;
    for (const auto& e : end->events) noneLoop = noneLoop && !e.loop;
    CHECK(allLoop && noneLoop, "loop flag per wave event (ONLOAD all loop, BOOST_END none)");
    // Stop at an arbitrary playback position: linear fade from the current level, no loop-boundary wait.
    int first = rec.n;
    int id = cues.play("VEH_OPTIMUS_DRIVE_ONLOAD", Vec3{0, 0, 0}, 0.0f, 30.0f);
    for (int k = 0; k < 22; ++k) cues.tick(1.0f / 60.0f);      // 0.367 s: mid-sample
    std::vector<float> v0;
    for (int v = first; v < rec.n; ++v) { CHECK(rec.v[v].p.loop, "ONLOAD voice %d loops", v - first); v0.push_back(rec.v[v].vol); }
    cues.stop(id, 0.2f);                                          // EngineFadeOutTime
    for (int k = 0; k < 6; ++k) cues.tick(1.0f / 60.0f);       // 0.1 s into the fade
    for (int v = first; v < rec.n; ++v)
        CHECK(rec.v[v].live && near(rec.v[v].vol, v0[(size_t)(v - first)] * 0.5f, v0[(size_t)(v - first)] * 0.06f + 1e-5f),
              "fade at 0.1 s = half the level (%.4f vs %.4f)", rec.v[v].vol, v0[(size_t)(v - first)]);
    for (int k = 0; k < 7; ++k) cues.tick(1.0f / 60.0f);       // past 0.2 s
    bool stopped = true;
    for (int v = first; v < rec.n; ++v) stopped = stopped && !rec.v[v].live;
    CHECK(stopped && !cues.playing(id), "loop stopped when the fade ends (auto-destroy)");
    // Zero fade stops immediately.
    first = rec.n;
    id = cues.play("VEH_OPTIMUS_BOOST_LOOP", Vec3{0, 0, 0}, 0.0f, 30.0f);
    for (int k = 0; k < 40; ++k) cues.tick(1.0f / 60.0f);
    cues.stop(id, 0.0f);
    stopped = rec.n > first;
    for (int v = first; v < rec.n; ++v) stopped = stopped && !rec.v[v].live;
    CHECK(stopped && !cues.playing(id), "zero fade stops immediately");

    // Real backend: a looping wave plays past its length (whole-sample loop), a one-shot ends.
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP backend: no audio device\n"); return; }
    a->setListener(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{1, 0, 0});
    Sound sq = a->load(kRoot + "/../content/WL_TRUCK/MECH_TIRE_SQUEAL_HEAVY_LP.wav");   // 48000 frames @ 24 kHz = 2.0 s
    Sound os = a->load(kRoot + "/../content/WL_TRUCK/SYNTH_AIR_RELEASE_04.wav");        // 20608 frames @ 48 kHz = 0.43 s
    CHECK(a->setLoopPoints(sq, 0, 47999), "FSB region applied to the squeal loop");
    VoiceParams lp; lp.volume = 0.01f; lp.loop = true;
    VoiceParams op; op.volume = 0.01f; op.loop = false;
    Voice vl = a->playVoice(sq, lp), vo = a->playVoice(os, op);
    std::this_thread::sleep_for(std::chrono::milliseconds(2600));
    CHECK(a->isPlaying(vl), "looping wave still playing after 2.6 s (> its 2.0 s length)");
    CHECK(!a->isPlaying(vo), "non-looping wave ended after its 0.43 s length");
    a->stopVoice(vl);
    delete a;
}

// ---------------------------------------------------------------- M04 Streets world bed (AssetTools a23c675)
static void testWorldBed() {
    std::printf("[M04 world bed]\n");
    assets::Json man = loadJson(kMan + "mp_iac_streets_complete.json");
    const assets::Json& ac = man["counts"]["audio"];
    assets::Json aj = loadJson(kRoot + "/Maps/MP_IAC_Streets/audio.json");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::AmbientAudio amb;
    amb.setPreferManifestZones(true);
    amb.load(kRoot + "/Maps/MP_IAC_Streets/audio.json", kRoot + "/../content/", cues, &rec);
    int kinds[3] = {0, 0, 0};
    for (int i = 0; i < amb.emitterCount(); ++i) ++kinds[amb.emitterKind(i)];
    CHECK(amb.emitterCount() == 70 && kinds[0] == ac["point"].asInt(-1) && kinds[2] == ac["line"].asInt(-1) && kinds[1] == ac["volume"].asInt(-1),
          "70 emitters = manifest point %d / line %d / volume %d (%d / %d / %d)", ac["point"].asInt(-1), ac["line"].asInt(-1),
          ac["volume"].asInt(-1), kinds[0], kinds[2], kinds[1]);
    CHECK(amb.zoneCount() == ac["zones"].asInt(-1) && amb.zoneCount() == 9, "9 zones (%d)", amb.zoneCount());
    CHECK(aj["reverb_presets"].size() == (size_t)ac["reverb_presets"].asInt(-1) && ac["reverb_presets"].asInt(-1) == 10, "10 reverb presets");
    int presetsKnown = 0;
    for (const auto& kv : aj["reverb_presets"].obj) presetsKnown += cues.mixer().hasPreset(kv.first) ? 1 : 0;
    CHECK(presetsKnown == 10, "all 10 presets in the mixer (%d)", presetsKnown);
    CHECK(amb.poolCount() == 11, "11 one-shot pools (%d)", amb.poolCount());

    // Level start: every emitter auto-plays once through the native per-cue registration (kKillFarthest).
    Vec3 L{150.0f, -720.0f, -450.0f};             // spawn-area camera (glTF metres)
    cues.setListener(L);
    amb.tick(1.0f / 60.0f, L, Vec3{0, 5000, 0}, cues); cues.tick(1.0f / 60.0f);
    std::map<std::string, std::vector<int>> byCue;
    for (int i = 0; i < amb.emitterCount(); ++i) byCue[amb.emitterCue(i)].push_back(i);
    int playing = 0;
    for (auto& kv : byCue) {
        const game::cuedata::CueDef* d = cues.cueDef(kv.first.c_str());
        int lim = d && d->maxConcurrent > 0 ? d->maxConcurrent : 1 << 30;
        int want = std::min((int)kv.second.size(), lim), have = 0;
        for (int i : kv.second) have += amb.emitterInstance(i) >= 0 ? 1 : 0;
        playing += have;
        CHECK(have == want && cues.activeInstances(kv.first.c_str()) == want, "%s: %d of %d play (limit %d)", kv.first.c_str() + 22, have,
              (int)kv.second.size(), lim);
    }
    CHECK(playing == 50 && amb.activeEmitters() == 50, "50 of 70 play at start (4 point cues over their limit) (%d)", playing);
    // kKillFarthest keeps the instances nearest the level-start listener (positions = the emitters' play positions).
    {
        const char* cue = "BL_LVL_MP_IAC_STREETS.EMIT_FLOURESCENT_LIGHTS";
        std::vector<std::pair<float, bool>> dl;
        for (int i : byCue[cue]) {
            int inst = amb.emitterInstance(i);
            Vec3 pos{}; bool play = inst >= 0 && cues.instancePos(inst, pos);
            const assets::Json* ej = nullptr; (void)ej;
            dl.push_back({0.0f, play});
        }
        // recompute distances from audio.json (point emitters: location_gltf)
        size_t k = 0;
        for (size_t j = 0; j < aj["emitters"]["point"].size(); ++j) {
            const assets::Json& e = aj["emitters"]["point"][j];
            if (e["cue"].asString() != cue) continue;
            Vec3 g{e["location_gltf"][0].asFloat(), e["location_gltf"][1].asFloat(), e["location_gltf"][2].asFloat()};
            if (k < dl.size()) dl[k++].first = core::length(g - L);
        }
        float maxPlay = 0, minSilent = 1e9f;
        for (auto& x : dl) { if (x.second) maxPlay = std::max(maxPlay, x.first); else minSilent = std::min(minSilent, x.first); }
        CHECK(maxPlay <= minSilent, "fluorescent lights: playing ones are the nearest (max playing %.1f m <= min silent %.1f m)", maxPlay, minSilent);
    }
    // Line / volume re-Play when not playing (A7 tick); a point AmbientSound does not restart.
    int lineI = -1, pointI = -1;
    for (int i = 0; i < amb.emitterCount(); ++i) {
        if (lineI < 0 && amb.emitterKind(i) == 2 && amb.emitterInstance(i) >= 0) lineI = i;
        if (pointI < 0 && amb.emitterKind(i) == 0 && amb.emitterInstance(i) >= 0 &&
            amb.emitterCue(i) == "BL_LVL_MP_IAC_STREETS.EMIT_ENERGON_GENERATOR") pointI = i;
    }
    cues.stop(amb.emitterInstance(lineI), 0.0f);
    cues.stop(amb.emitterInstance(pointI), 0.0f);
    amb.tick(1.0f / 60.0f, L, Vec3{0, 5000, 0}, cues); cues.tick(1.0f / 60.0f);
    CHECK(amb.emitterInstance(lineI) >= 0, "line emitter re-plays after a stop");
    CHECK(amb.emitterInstance(pointI) < 0, "point AmbientSound does not restart");
    // All world-owned, all looping.
    bool allWorld = true;
    for (int i = 0; i < amb.emitterCount(); ++i) {
        const game::cuedata::CueDef* d = cues.cueDef(amb.emitterCue(i).c_str());
        bool loops = false; for (const auto& e : d->events) loops = loops || e.loop;
        allWorld = allWorld && loops;
    }
    CHECK(allWorld, "every emitter cue is a looping authored map cue");

    // Pools: authored values; fire only while their zone is current, every U[DelayMin, DelayMax] s, 20 m out.
    int pools = 0;
    for (size_t z = 0; z < aj["zones"].size(); ++z)
        for (size_t q = 0; q < aj["zones"][z]["one_shot_pool"].size(); ++q) {
            const assets::Json& P = aj["zones"][z]["one_shot_pool"][q];
            ++pools;
            CHECK(cues.hasCue(P["cue"].asString().c_str()) && P["distance_min"].asFloat() == 2000.0f && P["distance_max"].asFloat() == 2000.0f &&
                  P["looping"].asBool(false) && P["delay_min"].asFloat() >= 3.0f && P["delay_max"].asFloat() <= 10.0f,
                  "pool %s: authored cue / 2000 UU / looping / delay %.0f-%.0f", P["comment"].asString().c_str(),
                  P["delay_min"].asFloat(), P["delay_max"].asFloat());
        }
    CHECK(pools == 11, "11 pools in audio.json (%d)", pools);
}

// ---------------------------------------------------------------- occlusion line checks skipped out of range (M09m)
static void testOcclusionSkip() {
    std::printf("[occlusion: no line checks beyond the audible distance]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    const char* js = R"({"T.LP": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": 0.0, "DistanceMax": 2000.0}, "children": [
        {"class": "SoundNodeWaveEvent", "params": {"Volume": 0.0, "bLooping": true}, "children": [{"wav": "content/WL_TRUCK/MECH_TIRE_SQUEAL_HEAVY_LP.wav"}]}]}}})";
    assets::Json cj; assets::Json::parse(js, cj);
    cues.addCues(cj, kRoot + "/../content/");
    int rays = 0;
    cues.setOcclusion([&](const Vec3&, const Vec3&, int) { ++rays; return true; });   // a wall everywhere
    cues.setListener(Vec3{0, 0, 0});
    const int id = cues.play("T.LP", Vec3{100, 0, 0}, 100.0f);                        // 100 m: beyond its 20 m
    for (int k = 0; k < 60; ++k) cues.tick(1.0f / 60.0f);
    CHECK(id >= 0 && rays == 0, "loop 100 m away (audible 20 m): no line checks for 1 s (%d)", rays);
    cues.setListener(Vec3{95, 0, 0});
    cues.tick(1.0f / 60.0f);
    CHECK(rays == 1 && cues.occludedInstances() == 1, "listener walks into range: checked at once (%d rays), occluded", rays);
    for (int k = 0; k < 60; ++k) cues.tick(1.0f / 60.0f);
    CHECK(rays >= 4 && rays <= 6, "in range: every OcclusionCheckInterval 0.25 s again (%d rays in 1 s)", rays - 1);
    cues.stop(id, 0.0f);
}

// ---------------------------------------------------------------- 96-channel priority stealing (Win32 backend)
static void testChannelStealing() {
    std::printf("[channel stealing]\n");
    // Cue priorities reach the voice: Ion Blaster SHOOT root 200 (-> 55), its overridden layers, map emitters 15 (-> 240).
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    int f0 = rec.n; cues.play("SHOOT", Vec3{0, 0, 0}, 0.0f);
    bool p55 = false, p60 = false;
    for (int v = f0; v < rec.n; ++v) { p55 = p55 || rec.v[v].p.priority == 55; p60 = p60 || rec.v[v].p.priority == 60; }
    CHECK(p55 && p60, "SHOOT voices carry 255 - Priority (root 200 -> 55, override 195 -> 60)");
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP: no audio device\n"); return; }
    Sound s = a->load(kRoot + "/../content/WL_TRUCK/MECH_TIRE_SQUEAL_HEAVY_LP.wav");
    VoiceParams amb; amb.volume = 0.001f; amb.loop = true; amb.priority = 240;
    std::vector<Voice> vs;
    for (int i = 0; i < 96; ++i) vs.push_back(a->playVoice(s, amb));
    MixStats m0; a->mixStats(m0);
    VoiceParams shot = amb; shot.priority = 55; shot.loop = false;
    Voice vShot = a->playVoice(s, shot);
    VoiceParams low = amb; low.priority = 250;
    Voice vLow = a->playVoice(s, low);
    MixStats m1; a->mixStats(m1);
    CHECK(vShot != kInvalidVoice && a->isPlaying(vShot) && m1.stolenVoices == m0.stolenVoices + 1,
          "full 96 channels: a priority-55 sound takes a priority-240 channel (stolen %d)", m1.stolenVoices - m0.stolenVoices);
    CHECK(vLow == kInvalidVoice && m1.droppedVoices == m0.droppedVoices + 1, "a less important newcomer (250) does not play");
    int alive = 0; for (Voice v : vs) alive += a->isPlaying(v) ? 1 : 0;
    CHECK(alive == 95, "exactly one ambient channel taken (%d alive)", alive);
    for (Voice v : vs) a->stopVoice(v);
    a->stopVoice(vShot);
    // Virtual voices: positional loops beyond their max distance hold no channel. 200 far loops + 96 audible: a new
    // audible sound still gets a channel without stealing, the far loops stay alive, and one comes back when in range.
    a->setListener(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0});
    VoiceParams far = amb; far.positional = true; far.pos = Vec3{500, 0, 0}; far.maxDist = 64.0f; far.volume = 0.5f;
    std::vector<Voice> farV;
    for (int i = 0; i < 200; ++i) farV.push_back(a->playVoice(s, far));
    auto pump = [&] { for (int i = 0; i < 6; ++i) { a->update(); std::this_thread::sleep_for(std::chrono::milliseconds(25)); } };
    pump();
    std::vector<Voice> near96;
    for (int i = 0; i < 95; ++i) near96.push_back(a->playVoice(s, amb));
    MixStats m2; a->mixStats(m2);
    Voice vNew = a->playVoice(s, shot);
    MixStats m3; a->mixStats(m3);
    int farAlive = 0; for (Voice v : farV) farAlive += a->isPlaying(v) ? 1 : 0;
    CHECK(vNew != kInvalidVoice && m3.stolenVoices == m2.stolenVoices && farAlive == 200,
          "200 out-of-range loops are virtual: the 96th audible sound plays without stealing (%d far alive, stolen %d)",
          farAlive, m3.stolenVoices - m2.stolenVoices);
    pump();
    MixStats m4; a->mixStats(m4);
    CHECK(m4.virtualVoices == 200 && m4.voices == 96, "mixer: 96 voices mixed, 200 virtual (%d / %d)", m4.voices, m4.virtualVoices);
    a->updateVoice(farV[0], 0.5f, 1.0f, Vec3{10, 0, 0});
    pump();
    MixStats m5; a->mixStats(m5);
    CHECK(a->isPlaying(farV[0]) && m5.voices == 96 && m5.virtualVoices == 200,
          "a virtual loop back in range is heard again; still 96 mixed, the quietest equal-priority one goes virtual (mixed %d, virtual %d)",
          m5.voices, m5.virtualVoices);
    for (Voice v : farV) a->stopVoice(v);
    for (Voice v : near96) a->stopVoice(v);
    a->stopVoice(vNew);
    // PC ADAPTATION: the local player's own sound (protect) gets a channel from 96 more important others, and is never
    // the victim of a later, more important newcomer.
    VoiceParams top = amb; top.priority = 0;
    std::vector<Voice> tops;
    for (int i = 0; i < 96; ++i) tops.push_back(a->playVoice(s, top));
    VoiceParams mine = amb; mine.priority = 250; mine.protect = true;
    Voice vMine = a->playVoice(s, mine);
    CHECK(vMine != kInvalidVoice && a->isPlaying(vMine), "the player's own priority-250 sound plays over 96 priority-0 channels");
    VoiceParams mine2 = mine;
    std::vector<Voice> mines{vMine};
    for (int i = 0; i < 95; ++i) mines.push_back(a->playVoice(s, mine2));   // now all 96 channels are the player's
    Voice vTop = a->playVoice(s, top);
    int minesAlive = 0; for (Voice v : mines) minesAlive += a->isPlaying(v) ? 1 : 0;
    int topsAlive = 0; for (Voice v : tops) topsAlive += a->isPlaying(v) ? 1 : 0;
    CHECK(vTop == kInvalidVoice && minesAlive == 96, "the player's own channels are never stolen (%d alive, tops alive %d, active %d)", minesAlive, topsAlive, a->activeVoices());
    for (Voice v : tops) a->stopVoice(v);
    for (Voice v : mines) a->stopVoice(v);
    // At most 96 heard: 96 audible loops + 10 virtual ones that come back into range (taking no channel) -> 106 audible;
    // the mixer mixes 96, the least important (priority 240 ones, not the player's own) stay virtual this block.
    std::vector<Voice> near, back;
    VoiceParams mid = amb; mid.priority = 128; mid.volume = 0.5f;
    for (int i = 0; i < 95; ++i) near.push_back(a->playVoice(s, mid));
    VoiceParams own = mid; own.protect = true; own.priority = 250;
    near.push_back(a->playVoice(s, own));
    VoiceParams farLow = far; farLow.priority = 240;
    for (int i = 0; i < 10; ++i) back.push_back(a->playVoice(s, farLow));
    pump();
    for (Voice v : back) a->updateVoice(v, 0.5f, 1.0f, Vec3{5, 0, 0});
    MixStats o0; a->mixStats(o0);
    pump();
    MixStats o1; a->mixStats(o1);
    int backAlive = 0; for (Voice v : back) backAlive += a->isPlaying(v) ? 1 : 0;
    CHECK(o1.voices == 96 && o1.virtualVoices == 10 && backAlive == 10 && a->isPlaying(near.back()) && o1.overflowVirtualized > o0.overflowVirtualized,
          "106 audible: 96 mixed, the 10 least important virtual (kept alive), the player's own heard (mixed %d, virtual %d)",
          o1.voices, o1.virtualVoices);
    for (Voice v : near) a->stopVoice(v);
    for (Voice v : back) a->stopVoice(v);
    delete a;
}

// ---------------------------------------------------------------- map audio lifecycle (M05 frontend transition)
// A synthetic second map: own cue names, emitters of all three kinds, one zone (a 10 m cube at the origin) with
// its own reverb preset and pool. Only existing waves are referenced. Proves the systems are manifest-driven.
static std::string writeFakeMap() {
    const std::string path = "work/fake_map_audio.json";   // work/ is the worktree scratch area
    std::ofstream f(path, std::ios::binary);
    auto face = [](float x0, float y0, float z0, float x1, float y1, float z1, int axis, float c) {
        std::ostringstream o; o.setf(std::ios::fixed); o.precision(2);
        float p[4][3];
        if (axis == 0) { float q[4][3] = {{c, y0, z0}, {c, y1, z0}, {c, y1, z1}, {c, y0, z1}}; std::memcpy(p, q, sizeof q); }
        else if (axis == 1) { float q[4][3] = {{x0, c, z0}, {x1, c, z0}, {x1, c, z1}, {x0, c, z1}}; std::memcpy(p, q, sizeof q); }
        else { float q[4][3] = {{x0, y0, c}, {x1, y0, c}, {x1, y1, c}, {x0, y1, c}}; std::memcpy(p, q, sizeof q); }
        o << "[";
        for (int i = 0; i < 4; ++i) o << (i ? "," : "") << "[" << p[i][0] << "," << p[i][1] << "," << p[i][2] << "]";
        o << "]";
        return o.str();
    };
    const float h = 5.0f;
    std::string polys = "[" + face(-h, -h, -h, h, h, h, 0, -h) + "," + face(-h, -h, -h, h, h, h, 0, h) + "," +
                        face(-h, -h, -h, h, h, h, 1, -h) + "," + face(-h, -h, -h, h, h, h, 1, h) + "," +
                        face(-h, -h, -h, h, h, h, 2, -h) + "," + face(-h, -h, -h, h, h, h, 2, h) + "]";
    const char* hum = R"("BL_LVL_FAKE.EMIT_HUM": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": -6.0, "DistanceMin": 200.0, "DistanceMax": 3000.0, "Category": "SFX_WET_AMB_3D"}, "children": [{"class": "SoundNodeWaveEvent", "params": {"bLooping": true}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]}]}})";
    const char* knock = R"("BL_LVL_FAKE.PP_KNOCK": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": -6.0}, "children": [{"class": "SoundNodeWaveEvent", "params": {}, "children": [{"wav": "content/WL_TRUCK/SYNTH_AIR_RELEASE_04.wav"}]}]}})";
    auto mat = [](float x, float y, float z) {
        std::ostringstream o; o << "[1,0,0,0, 0,1,0,0, 0,0,1,0, " << x << "," << y << "," << z << ",1]"; return o.str();
    };
    f << "{\"map\": \"FAKE_TEST_MAP\", \"cues\": {" << hum << ", " << knock << "},\n"
      << "\"emitters\": {\"point\": [{\"cue\": \"BL_LVL_FAKE.EMIT_HUM\", \"gltf_matrix\": " << mat(10, 0, 0) << "}],"
      << " \"volume\": [{\"cue\": \"BL_LVL_FAKE.EMIT_HUM\", \"radius\": 500.0, \"gltf_matrix\": " << mat(0, 0, 10) << "}],"
      << " \"line\": [{\"cue\": \"BL_LVL_FAKE.EMIT_HUM\", \"linelength\": 500.0, \"gltf_matrix\": " << mat(-10, 0, 0) << "}]},\n"
      << "\"zones\": [{\"comment\": \"ENTER_FAKE_ROOM\", \"reverb_preset\": \"REVERB_FAKE_ROOM\", \"trigger_polygons_gltf\": " << polys
      << ", \"one_shot_pool\": [{\"cue\": \"BL_LVL_FAKE.PP_KNOCK\", \"delay_min\": 1.0, \"delay_max\": 2.0, \"distance_min\": 2000.0, \"distance_max\": 2000.0, \"looping\": true}]}],\n"
      << "\"reverb_presets\": {\"REVERB_FAKE_ROOM\": {\"mixer_preset\": {\"Priority\": 200.0, \"FadeInTime\": 0.25, \"FadeOutTime\": 0.25, \"Duration\": -1.0},"
      << " \"dsp_by_category\": {\"MASTER_WET\": {\"Volume\": {\"Volume\": 1.0}, \"Reverb\": {\"Room\": -500.0, \"RoomHF\": -300.0, \"RoomRolloffFactor\": 0.0,"
      << " \"DecayTime\": 1.5, \"DecayHFRatio\": 0.8, \"ReflectionsLevel\": -700.0, \"ReflectionsDelay\": 0.02, \"Level\": -600.0, \"Delay\": 0.03,"
      << " \"Diffusion\": 50.0, \"Density\": 50.0, \"HFReference\": 5000.0}, \"Echo\": {\"Delay\": 500.0, \"DecayRatio\": 0.5, \"WetMix\": 0.0, \"DryMix\": 1.0}}}}},\n"
      << "\"categories\": {\"Master\": {\"DSPEffectConfig\": 0}}}\n";
    return path;
}

static int liveRecVoices(const Rec& r) { int n = 0; for (const auto& kv : r.v) n += kv.second.live ? 1 : 0; return n; }

static void testLifecycle() {
    std::printf("[map audio lifecycle]\n");
    const std::string fake = writeFakeMap();
    const std::string content = kRoot + "/../content/";
    Rec rec; game::SoundCues cues; cues.load(&rec, content);
    Vec3 pawn{0, 0, 0};
    cues.setResolver([&](int owner, const std::string&, const Vec3& off, Vec3& out) { if (owner != 0) return false; out = pawn + off; return true; });
    game::AmbientAudio amb;
    const size_t baseCues = cues.cueCount();
    const int basePresets = cues.mixer().presetCount();
    const Vec3 streetsSpawn{363.5f, -724.5f, -341.8f};      // a Streets spawn (DEC_ROOM_LOWER)
    struct MapCase { const char* name; std::string path; int cues, presets, emitters, zones, pools; Vec3 spot; const char* reverb; };
    const MapCase maps[2] = {
        {"MP_IAC_Streets", streetsAudio(), 32 + 164, 10, 70, 9, 11, streetsSpawn, "REVERB_TRANS_MP_STREETS_DEC_ROOM_LOWER"},
        {"FAKE_TEST_MAP", fake, 2, 1, 3, 1, 1, Vec3{0, -1.0f, 0}, "REVERB_FAKE_ROOM"}};
    bool allClean = true;
    for (int cycle = 0; cycle < 6; ++cycle)
        for (const MapCase& mc : maps) {
            const bool ok = amb.load(mc.path, content, cues, &rec);
            CHECK(ok && cues.mapCueCount() == mc.cues && cues.mixer().mapPresetCount() == mc.presets && amb.emitterCount() == mc.emitters &&
                  amb.zoneCount() == mc.zones && amb.poolCount() == mc.pools,
                  "cycle %d %s: %d cues / %d presets / %d emitters / %d zones / %d pools (%d/%d/%d/%d/%d)", cycle, mc.name,
                  mc.cues, mc.presets, mc.emitters, mc.zones, mc.pools, cues.mapCueCount(), cues.mixer().mapPresetCount(),
                  amb.emitterCount(), amb.zoneCount(), amb.poolCount());
            pawn = mc.spot;
            for (int k = 0; k < 240; ++k) {                       // 4 s: zone touch, bed start, pools, player sounds
                Vec3 L = mc.spot + Vec3{std::sin(k * 0.05f) * 3.0f, 2.0f, std::cos(k * 0.05f) * 3.0f};
                cues.setListener(L);
                amb.tick(1.0f / 60.0f, L, mc.spot, cues);   // pawn: AmbientAudio probes pawn + 1 m
                if (k % 30 == 0) cues.play("SHOOT", game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f);
                if (k == 10) cues.play("VEH_OPTIMUS_DRIVE_ONLOAD", game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f, 30.0f);
                cues.tick(1.0f / 60.0f);
            }
            CHECK(cues.mixer().currentReverb() == mc.reverb && amb.activeEmitters() > 0,
                  "cycle %d %s: zone reverb %s, %d emitters playing", cycle, mc.name, cues.mixer().currentReverb().c_str(), amb.activeEmitters());
            // unload (map change / return to frontend)
            amb.unload(cues);
            cues.stopAll();
            const bool clean = cues.liveInstances() == 0 && cues.pendingEvents() == 0 && cues.mapCueCount() == 0 &&
                               cues.cueCount() == baseCues && cues.mixer().presetCount() == basePresets &&
                               cues.mixer().activeList() == "Default(1)" && cues.mixer().currentReverb().empty() &&
                               liveRecVoices(rec) == 0 && amb.emitterCount() == 0 && amb.zoneCount() == 0 && amb.poolCount() == 0 &&
                               !amb.loaded();
            allClean = allClean && clean;
            CHECK(clean, "cycle %d %s unload: live %zu pending %zu mapCues %d cues %zu presets %d [%s] voices %d", cycle, mc.name,
                  cues.liveInstances(), cues.pendingEvents(), cues.mapCueCount(), cues.cueCount(), cues.mixer().presetCount(),
                  cues.mixer().activeList().c_str(), liveRecVoices(rec));
        }
    CHECK(allClean, "12 load/play/unload cycles (Streets <-> synthetic map) all return to the baseline");

    // Loading over a loaded map (no explicit unload) replaces it: no duplicate bed / presets / cues.
    amb.load(streetsAudio(), content, cues, &rec);
    cues.setListener(streetsSpawn); amb.tick(1.0f / 60.0f, streetsSpawn, streetsSpawn, cues); cues.tick(1.0f / 60.0f);
    const size_t oneBed = cues.liveInstances();
    amb.load(streetsAudio(), content, cues, &rec);
    cues.setListener(streetsSpawn); amb.tick(1.0f / 60.0f, streetsSpawn, streetsSpawn, cues); cues.tick(1.0f / 60.0f);
    CHECK(cues.liveInstances() == oneBed && cues.mapCueCount() == 32 + 164 && cues.mixer().mapPresetCount() == 10,
          "reload without unload: one bed (%zu instances, was %zu), 32 bank + 164 streamed match cues, 10 presets", cues.liveInstances(), oneBed);

    // Match reset on the same map: player sounds stop, the bed keeps playing, the zone scene re-begins on re-touch
    // (pools restart, the reverb slot and preset ref-counts are unchanged).
    pawn = streetsSpawn;
    for (int k = 0; k < 60; ++k) { cues.setListener(streetsSpawn); amb.tick(1.0f / 60.0f, streetsSpawn, streetsSpawn, cues); cues.tick(1.0f / 60.0f); }
    cues.play("SHOOT", game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f);
    cues.play("VEH_OPTIMUS_DRIVE_ONLOAD", game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f, 30.0f);
    std::vector<int> bed;
    for (int i = 0; i < amb.emitterCount(); ++i) bed.push_back(amb.emitterInstance(i));
    const std::string listBefore = cues.mixer().activeList(), reverbBefore = cues.mixer().currentReverb();
    auto pools = [&] { return amb.poolsRunning() || amb.script().poolsPlaying() > 0; };   // manifest or generated zones
    CHECK(pools(), "pools run before the reset");
    int stopped = cues.stopNonMapInstances();
    amb.resetMatch();
    bool bedSame = true;
    for (int i = 0; i < amb.emitterCount(); ++i) bedSame = bedSame && amb.emitterInstance(i) == bed[(size_t)i] && (bed[(size_t)i] < 0 || cues.playing(bed[(size_t)i]));
    CHECK(stopped >= 2 && cues.activeInstances("SHOOT") == 0 && cues.activeInstances("VEH_OPTIMUS_DRIVE_ONLOAD") == 0,
          "match reset stops the player sounds (%d)", stopped);
    CHECK(bedSame && !pools() && cues.mixer().activeList() == listBefore && cues.mixer().currentReverb() == reverbBefore,
          "match reset keeps the bed and the reverb; pools stop");
    for (int k = 0; k < 3; ++k) { cues.setListener(streetsSpawn); amb.tick(1.0f / 60.0f, streetsSpawn, streetsSpawn, cues); cues.tick(1.0f / 60.0f); }
    CHECK(pools() && cues.mixer().activeList() == listBefore, "re-touch after reset: scene re-begins, no duplicate preset enable [%s]",
          cues.mixer().activeList().c_str());
    amb.unload(cues); cues.stopAll();

    // Real backend: voices and resident decoded PCM return to the baseline after each map unload.
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP backend: no audio device\n"); return; }
    game::SoundCues rc; rc.load(a, content);
    game::AmbientAudio ramb;
    const size_t baseBytes = a->residentBytes();
    size_t peakBytes = 0;
    bool backendClean = true;
    for (int cycle = 0; cycle < 4; ++cycle) {
        const MapCase& mc = maps[cycle % 2];
        ramb.load(mc.path, content, rc, a);
        peakBytes = std::max(peakBytes, a->residentBytes());
        for (int k = 0; k < 30; ++k) {
            rc.setListener(mc.spot); ramb.tick(1.0f / 30.0f, mc.spot, mc.spot, rc);
            if (k % 10 == 0) rc.play("SHOOT", mc.spot, 0.0f);
            rc.tick(1.0f / 30.0f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const int during = a->activeVoices();
        ramb.unload(rc); rc.stopAll();
        const int after = a->activeVoices();
        const size_t bytes = a->residentBytes();
        backendClean = backendClean && after == 0 && bytes == baseBytes;
        std::printf("  backend cycle %d %-14s voices %d -> %d, PCM peak %.1f MB -> %.1f MB (base %.1f MB)\n", cycle, mc.name, during, after,
                    peakBytes / 1048576.0, bytes / 1048576.0, baseBytes / 1048576.0);
        CHECK(during > 0 && after == 0 && bytes == baseBytes, "backend cycle %d %s: %d voices -> %d after unload; PCM %zu -> %zu bytes (base %zu)",
              cycle, mc.name, during, after, peakBytes, bytes, baseBytes);
    }
    CHECK(backendClean && peakBytes > baseBytes, "map samples are released at unload (peak %.1f MB over a %.1f MB base)",
          (peakBytes - baseBytes) / 1048576.0, baseBytes / 1048576.0);
    delete a;
}

// ---------------------------------------------------------------- frontend audio (M05)
static void testFrontend() {
    std::printf("[frontend audio]\n");
    const std::string content = kRoot + "/../content/";
    Rec rec; game::SoundCues cues; cues.load(&rec, content);
    game::FrontendAudio fe(cues);
    auto tick = [&](float secs) { for (float t = 0; t < secs - 1e-4f; t += 1.0f / 30.0f) { fe.tick(1.0f / 30.0f); cues.tick(1.0f / 30.0f); } };
    // UI sounds by GFx name (frontend_audio.json gfx_ui_sounds)
    assets::Json fa = loadJson(kMan + "frontend_audio.json");
    const assets::Json& g = fa["gfx_ui_sounds (names passed to Sound.PlaySound in AS, matched to cue names)"];
    int named = 0;
    for (const auto& kv : g.obj) {
        ++named;
        CHECK(fe.uiCueFor(kv.first.c_str()) == kv.second["cue"].asString(), "GFx sound %s -> %s", kv.first.c_str(), kv.second["cue"].asString().c_str());
    }
    CHECK(named == 16, "16 GFx sound names (%d)", named);
    int f0 = rec.n;
    int b1 = fe.playUiSound("BUTTON_ACCEPT");
    CHECK(b1 >= 0 && rec.n > f0 && !rec.v[f0].p.positional, "BUTTON_ACCEPT plays 2D (%d voices)", rec.n - f0);
    int b2 = fe.playUiSound("BUTTON_ACCEPT");
    CHECK(fe.stopUiSound("BUTTON_ACCEPT", 0.0f) && !cues.playing(b1) && cues.playing(b2), "StopSound stops the first (oldest) instance only");
    CHECK(fe.playUiSound("NOT_A_SOUND") == -1, "unknown UI sound name -> nothing");
    int silent = fe.playUiSound("PICKUP_DMG_MULTIPLIER_DECREASE");
    CHECK(silent >= 0 && cues.cueDef("BL_HUD_INTERFACE.PICKUP_DMG_MULTIPLIER_DECREASE")->events.empty(), "authored-silent cue plays nothing");

    // Music: SeqAct_PlayMusic tracks of the UI levels
    game::MusicTrack fr, lobby, party;
    CHECK(game::FrontendAudio::frontendTrack("UI_FrontEnd_m", fr) && fr.cue == "BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01" && fr.fadeIn == 0.25f &&
          game::FrontendAudio::frontendTrack("UI_Lobby_m", lobby) && game::FrontendAudio::frontendTrack("UI_PartyLobby_m", party) &&
          party.fadeOut == 0.0f, "authored UI-level tracks");
    { game::MusicTrack none; CHECK(!game::FrontendAudio::frontendTrack("MP_IAC_Streets", none), "a match map authors no frontend track"); }
    game::FrontendAudio::frontendTrack("UI_FrontEnd_m", fr);
    game::MusicPlayer& mp = fe.music();
    CHECK(!cues.wavesResident(fr.cue.c_str()), "streamed track not decoded before it plays");
    mp.playMusic(fr);
    CHECK(mp.state() == game::MusicPlayer::State::Queued, "PlayMusic queues");
    tick(1.0f / 30.0f);
    const int m1 = mp.musicInstance();
    CHECK(mp.state() == game::MusicPlayer::State::Playing && m1 >= 0 && cues.wavesResident(fr.cue.c_str()),
          "first track starts at once (SpazTimer 0), decoded on demand");
    mp.playMusic(fr);
    CHECK(mp.musicInstance() == m1 && mp.state() == game::MusicPlayer::State::Playing, "same cue again: no restart");
    game::MusicTrack low = lobby; low.priority = -1;
    mp.playMusic(low);
    CHECK(mp.state() == game::MusicPlayer::State::Playing && mp.queued().cue == fr.cue, "lower priority track ignored");
    mp.playMusic(lobby);
    CHECK(mp.state() == game::MusicPlayer::State::Queued, "new track queued behind the 5 s SpazTimer");
    tick(4.5f);
    CHECK(mp.musicInstance() == m1, "still the old track at 4.5 s");
    tick(0.6f);
    const int m2 = mp.musicInstance();
    CHECK(m2 >= 0 && m2 != m1 && cues.playing(m1), "crossfade at the SpazTimer (old track fading over its FadeOut 1.0)");
    tick(1.1f);
    tick(1.0f / 30.0f);
    CHECK(!cues.playing(m1) && !cues.wavesResident(fr.cue.c_str()) && cues.wavesResident(lobby.cue.c_str()),
          "old track ended; its streamed waves released");
    // root-loop timeline: MP_LOBBY_MX events at 0 / 173.6 / 291.4 s, wrap at 390.7 s
    int launches = 0;
    const int before = rec.n;
    tick(392.0f);
    launches = rec.n - before;
    CHECK(launches == 3, "lobby timeline: waves 2 and 3 at 173.6 / 291.4 s, wave 1 again after the 390.7 s wrap (%d new voices)", launches);
    CHECK(cues.playing(m2), "root-looping music instance never retires");
    // boredom restart
    game::MusicTrack bored = party; bored.boredom = 2.0f;
    mp.playMusic(bored, true);        // IgnoreSpazTimer
    tick(1.0f / 30.0f);
    const int m3 = mp.musicInstance();
    tick(2.1f);
    CHECK(m3 >= 0 && mp.musicInstance() != m3 && mp.current().cue == bored.cue, "BoredomTime re-crossfades the same track");
    mp.stopMusic(0.5f);
    CHECK(mp.state() == game::MusicPlayer::State::Stopped && mp.musicInstance() < 0, "StopMusic -> Stopped");
    tick(0.7f);
    CHECK(cues.activeInstances(bored.cue.c_str()) == 0 && !cues.wavesResident(bored.cue.c_str()), "stopped track faded out and released");
    // stinger priority
    const char* st = "BL_HUD_INTERFACE.STNG_ESCALATION_WAVE_OVER";   // root Priority 0
    CHECK(mp.playStinger(st) && !mp.playStinger(st), "a stinger of equal priority does not interrupt the playing one");
    // level change
    mp.playMusic(fr); tick(0.1f);
    fe.onLevelChange();
    tick(1.0f / 30.0f);
    CHECK(mp.state() == game::MusicPlayer::State::Stopped && cues.activeInstances(fr.cue.c_str()) == 0 && cues.activeInstances(st) == 0,
          "level change: music and stinger stop at once");

    // Real backend: streamed decode cost and release
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP backend: no audio device\n"); return; }
    game::SoundCues rc; rc.load(a, content);
    game::FrontendAudio rfe(rc);
    const size_t base = a->residentBytes();
    {   // prefetch during a "loading screen": the decode happens there, the first play is then free
        auto p0 = std::chrono::steady_clock::now();
        CHECK(rc.prefetch(lobby.cue.c_str()), "prefetch decodes a streamed track");
        const double pms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
        CHECK(pms < 5.0, "prefetch does not block the asking frame (worker decode on a thread-safe backend; %.2f ms)", pms);
        // the loading screen: frames tick while the worker decodes; the warm is adopted by a tick (no wait)
        double worstTick = 0.0;
        for (int k = 0; k < 120 && !rc.wavesResident(lobby.cue.c_str()); ++k) {
            auto t = std::chrono::steady_clock::now();
            rc.tick(1.0f / 30.0f);
            worstTick = std::max(worstTick, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        CHECK(rc.wavesResident(lobby.cue.c_str()) && worstTick < 5.0, "prefetched waves adopted during the loading frames without a stall "
              "(worst tick %.2f ms) and pinned until played", worstTick);
        auto q0 = std::chrono::steady_clock::now();
        rfe.music().playMusic(lobby); rfe.tick(1.0f / 30.0f);
        const double qms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - q0).count();
        std::printf("  MP_LOBBY_MX prefetch %.0f ms, then first play %.1f ms\n", pms, qms);
        CHECK(qms < 20.0, "first play after prefetch has no decode stall (%.1f ms)", qms);
        rfe.music().stopMusic(0.0f); rc.tick(1.0f / 30.0f);
        CHECK(!rc.wavesResident(lobby.cue.c_str()) && a->residentBytes() == base, "released after stop");
    }
    // A play with no prefetch (the match final-stretch music): decoded on the worker, the instance waits and starts when
    // the waves are adopted - no main-thread stall (Integration 08n: 159-686 ms) [rebuild performance; start delay only].
    auto t0 = std::chrono::steady_clock::now();
    rfe.music().playMusic(fr, true);   // IgnoreSpazTimer: the lobby play above left SpazTimer at 5 s (StopMusic keeps it)
    rfe.tick(1.0f / 30.0f);            // the music player starts its cue on its tick
    const double callMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(callMs < 5.0 && rc.waitingInstances() == 1, "unprefetched streamed play: no decode on the starting frame (%.2f ms), the instance waits", callMs);
    double worstTick = 0.0;
    int frames = 0;
    for (; frames < 300 && rc.waitingInstances() > 0; ++frames) {
        auto t = std::chrono::steady_clock::now();
        rfe.tick(1.0f / 30.0f); rc.tick(1.0f / 30.0f);   // the frame: frontend audio + the cue table (adoption)
        worstTick = std::max(worstTick, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const size_t during = a->residentBytes();
    CHECK(rc.waitingInstances() == 0 && rc.activeInstances(fr.cue.c_str()) == 1 && during > base && worstTick < 5.0,
          "it starts after the worker decode (%d frames, worst tick %.2f ms), resident +%.1f MB", frames, worstTick, (during - base) / 1048576.0);
    rfe.music().stopMusic(0.0f);
    rc.tick(1.0f / 30.0f);
    std::printf("  FRONTEND_MX_ORBIT_01 unprefetched play: call %.2f ms, started after %d frames, +%.1f MB, after stop %+.1f MB\n", callMs,
                frames, (during - base) / 1048576.0, ((double)a->residentBytes() - (double)base) / 1048576.0);
    CHECK(a->residentBytes() == base, "released after stop");
    // Stopped while still decoding: it never sounds; the late-adopted waves are released.
    const int v0 = a->activeVoices();
    const int sid = rc.play(fr.cue.c_str(), Vec3{0, 0, 0}, 0.0f);
    rc.stop(sid, 0.5f);
    for (int k = 0; k < 300 && rc.warmingPrefetches() > 0; ++k) { rc.tick(1.0f / 30.0f); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    rc.tick(1.0f / 30.0f);
    CHECK(!rc.playing(sid) && a->activeVoices() == v0 && a->residentBytes() == base, "stopped before its decode finished: silent, nothing left resident");
    delete a;
}

// ---------------------------------------------------------------- pickup / map-event audio ownership (M05 phase 4)
static void testMapEventAudio() {
    std::printf("[pickup / map-event audio]\n");
    assets::Json inv = loadJson(kMan + "streets_actor_inventory.json");
    assets::Json mv = loadJson(kMan + "streets_movers.json");
    assets::Json ks = loadJson(kMan + "streets_kismet.json");
    assets::Json aj = loadJson(streetsAudio());
    // Every placed AudioComponent belongs to an AmbientSound (the 40 point emitters the bed plays); none to a mover
    // or a mode-gated actor.
    std::set<std::string> movers;
    for (size_t i = 0; i < mv["movers"].size(); ++i) movers.insert(mv["movers"][i]["actor"].asString());
    int ambientAc = 0, otherAc = 0, moverAc = 0;
    for (size_t i = 0; i < inv["actors"].size(); ++i) {
        const assets::Json& act = inv["actors"][i];
        for (size_t c = 0; c < act["components"].size(); ++c)
            if (act["components"][c]["class"].asString() == "AudioComponent") {
                if (act["class"].asString() == "AmbientSound") ++ambientAc; else ++otherAc;
                if (movers.count(act["actor"].asString())) ++moverAc;
            }
    }
    CHECK(ambientAc == 40 && (int)aj["emitters"]["point"].size() == 40 && otherAc == 0 && moverAc == 0,
          "placed AudioComponents: %d on AmbientSound (= 40 point emitters), %d elsewhere, %d on movers", ambientAc, otherAc, moverAc);
    // No emitter actor of the bed is a mover; movers reference no sound.
    int emitterOnMover = 0;
    for (const char* k : {"point", "line", "volume"})
        for (size_t i = 0; i < aj["emitters"][k].size(); ++i) {
            std::string a2 = aj["emitters"][k][i]["actor"].asString();
            for (const auto& m : movers) if (!a2.empty() && m.size() >= a2.size() && m.compare(m.size() - a2.size(), a2.size(), a2) == 0) ++emitterOnMover;
        }
    std::ifstream mf(kMan + "streets_movers.json"); std::stringstream mss; mss << mf.rdbuf();
    const std::string mtxt = mss.str();
    CHECK(emitterOnMover == 0 && mtxt.find("SoundCue") == std::string::npos && mtxt.find("AudioComponent") == std::string::npos,
          "movers carry no authored sound (%d bed emitters on movers)", emitterOnMover);
    // Audio Kismet is not mode-gated: no audio op's trigger chain passes a SeqCond (the only GameRuleActive ops
    // gate the objective-base ToggleHidden in BASE).
    int gated = 0;
    for (size_t i = 0; i < ks["audio_ops"].size(); ++i) {
        const assets::Json& op = ks["audio_ops"][i];
        for (size_t c = 0; c < op["trigger_chains"].size(); ++c)
            for (size_t k = 0; k < op["trigger_chains"][c].size(); ++k) {
                const std::string o = op["trigger_chains"][c][k]["op"].asString() + op["trigger_chains"][c][k]["event"].asString();
                if (o.find("SeqCond") != std::string::npos) ++gated;
            }
    }
    CHECK(ks["audio_ops"].size() == 29 && gated == 0, "29 Kismet audio ops, none behind a game-rule condition (%d)", gated);

    // Pickup event stream (Gameplay PickupEvent order: Taken ... Respawned ... Taken): one PickupSound per Taken,
    // on the receiving pawn; Respawned plays nothing (Systems is not called for it).
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    Vec3 pawn{5, 0, 5};
    cues.setResolver([&](int owner, const std::string&, const Vec3& off, Vec3& out) { if (owner != 0) return false; out = pawn + off; return true; });
    struct Ev { const char* cls; bool taken; };
    const Ev evs[] = {{"TnAmmoCratePickupFactory", true}, {"TnAmmoCratePickupFactory", false}, {"TnHealthPickupFactory", true},
                      {"TnHealthPickupFactory", false}, {"TnHealthPickupFactory", true}, {"TnOverShieldPickupFactory", true},
                      {"TnOverShieldPickupFactory", false}, {"TnGameObjectivePickupFactoryFlag", true}};
    int plays = 0, expected = 0;
    for (const Ev& e : evs) {
        if (!e.taken) continue;                                   // Respawned: nothing to play
        const char* snd = game::PickupPresentation::pickupSoundFor(e.cls);
        if (snd) ++expected;
        const int before = cues.activeInstances(snd ? snd : "");
        const int id = game::PickupPresentation::onTaken(e.cls, cues, game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f);
        if (id >= 0) ++plays;
        if (snd) CHECK(cues.activeInstances(snd) == before + 1, "%s Taken: exactly one %s instance", e.cls, snd);
        for (int k = 0; k < 10; ++k) cues.tick(1.0f / 30.0f);
    }
    CHECK(plays == expected && expected == 4, "4 sounding takes (the flag inventory authors no PickupSound) (%d)", plays);
    for (int k = 0; k < 300; ++k) cues.tick(1.0f / 30.0f);
    CHECK(cues.liveInstances() == 0 && cues.pendingEvents() == 0, "pickup sounds retire (no leak)");
}

// ---------------------------------------------------------------- M06: level manifests + frontend lifecycle
// The level-scoped host World uses (LevelAudioHost): UI levels from the compiled-in Systems manifests, Streets from
// the AssetTools manifest + Systems cue limits, a synthetic map from a file; the boot -> frontend -> lobby -> match
// -> lobby -> frontend lifecycle repeated, checking the baseline after every unload and the per-stage maxima.
struct StageMax { size_t live = 0, pending = 0; int voices = 0, scriptSounds = 0, pools = 0; double pcm = 0; };
static void stageTick(game::LevelAudioHost& host, game::SoundCues& cues, const Vec3& L, const Vec3& pawn, float dt, StageMax& m,
                      IAudio* a = nullptr) {
    cues.setListener(L);
    host.tick(dt, L, pawn);
    cues.tick(dt);
    const auto s = host.state();
    m.live = std::max(m.live, cues.liveInstances());
    m.pending = std::max(m.pending, cues.pendingEvents());
    m.scriptSounds = std::max(m.scriptSounds, s.scriptSounds);
    m.pools = std::max(m.pools, s.poolsPlaying);
    if (a) { m.voices = std::max(m.voices, a->activeVoices()); m.pcm = std::max(m.pcm, a->residentBytes() / 1048576.0); }
}
static bool atBaseline(const game::LevelAudioHost& host, const game::SoundCues& cues, size_t baseCues, int basePresets, std::string& why) {
    const auto s = host.state();
    char b[512];
    std::snprintf(b, sizeof b, "live %zu pending %zu levelCues %d cues %zu presets %d [%s] reverb '%s' music %d/%d script %d pools %d timelines %d",
                  cues.liveInstances(), cues.pendingEvents(), cues.mapCueCount(), cues.cueCount(), cues.mixer().presetCount(),
                  cues.mixer().activeList().c_str(), s.reverb.c_str(), s.musicState, s.musicInstance, host.ambient().script().opCount(),
                  s.poolsPlaying, s.timelines);
    why = b;
    return cues.liveInstances() == 0 && cues.pendingEvents() == 0 && cues.mapCueCount() == 0 && cues.cueCount() == baseCues &&
           cues.mixer().presetCount() == basePresets && cues.mixer().activeList() == "Default(1)" && s.reverb.empty() &&
           s.musicState == 0 && s.musicInstance < 0 && host.ambient().script().opCount() == 0 && s.poolsPlaying == 0 &&
           s.timelines == 0 && s.level.empty() && !host.ambient().loaded();
}

static std::string liveRecCues(const Rec& r) {
    std::string s;
    for (const auto& kv : r.v) if (kv.second.live) s += std::to_string(kv.first) + " ";
    return s;
}
static void testLevelLifecycle() {
    std::printf("[level manifests + frontend lifecycle]\n");
    const std::string content = kRoot + "/../content/";
    const std::string fake = writeFakeMap();
    const char* kMusic[3] = {"BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01", "BL_LVL_HUD_INTERFACE.MP_LOBBY_MX", "BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX"};
    auto musicInstances = [&](game::SoundCues& c) { int n = 0; for (const char* m : kMusic) n += c.activeInstances(m); return n; };

    // Manifest inventory.
    CHECK(game::AmbientAudio::levelManifestCount() == 15 && game::AmbientAudio::hasLevelManifest("UI_FrontEnd_m") &&
          game::AmbientAudio::hasLevelManifest("MP_UND_Gorge") && game::AmbientAudio::manifestJson("__match_messages__") &&
          game::AmbientAudio::hasLevelManifest("UI_PartyLobby_m") && game::AmbientAudio::hasLevelManifest("UI_Lobby_m") &&
          game::AmbientAudio::hasLevelManifest("UI_CampaignLobby_m") && game::AmbientAudio::hasLevelManifest("MP_IAC_Streets"),
          "15 Systems manifests (4 UI levels, 10 MP maps, match messages)");
    { game::MusicTrack fr, lob, party, none;
      CHECK(game::FrontendAudio::frontendTrack("UI_FrontEnd_m", fr) && fr.cue == kMusic[0] && fr.fadeIn == 0.25f && fr.fadeOut == 1.0f &&
            game::FrontendAudio::frontendTrack("UI_Lobby_m", lob) && lob.cue == kMusic[1] && lob.fadeIn == 0.0f &&
            game::FrontendAudio::frontendTrack("UI_PartyLobby_m", party) && party.cue == kMusic[2] && party.fadeOut == 0.0f &&
            !game::FrontendAudio::frontendTrack("MP_IAC_Streets", none), "authored level music read from the manifests"); }

    // Mixer: every category's volume; MUSIC_DRY 0.708 [CONF]; Master changes only through masterScale (CINE_MUTE).
    { game::SoundMixer m;
      CHECK(m.categoryCount() == 47 && near(m.categoryVolume("MUSIC_DRY"), 0.7079f) && near(m.categoryVolume("Master"), 0.7079f) &&
            near(m.masterScale(), 1.0f) && near(m.categoryVolume("SFX_DRY_HUD"), 1.0f), "47 categories, MUSIC_DRY 0.708, master scale 1 (%d)", m.categoryCount());
      m.enable("CINE_MUTE_FOR_BINK");
      CHECK(near(m.masterScale(), 0.0f) && !std::strcmp(m.categoryTarget("Master"), "CINE_MUTE_FOR_BINK"), "CINE_MUTE_FOR_BINK: Master 0 at once (FadeIn 0)");
      m.flush();                                       // a level change during the movie
      CHECK(near(m.masterScale(), 0.0f) && m.activeList() == "CINE_MUTE_FOR_BINK(1),Default(1)" && game::SoundMixer::unflushable("CINE_MUTE_FOR_BINK") &&
            !std::strcmp(game::SoundMixer::movieMixerPreset(), "CINE_MUTE_FOR_BINK"),
            "UnflushableMixerPresets: CINE_MUTE_FOR_BINK survives a Flush [%s]", m.activeList().c_str());
      m.disable("CINE_MUTE_FOR_BINK", false); step(m, 0.5f);
      CHECK(m.masterScale() > 0.4f && m.masterScale() < 0.6f, "movie end: Master returns over FadeOut 1 s (%.2f at 0.5 s)", m.masterScale());
      step(m, 0.6f);
      CHECK(near(m.masterScale(), 1.0f), "... and is back at 1 after 1 s");
      float thr, att, rel, mk;
      CHECK(game::SoundMixer::masterCompressor(thr, att, rel, mk) && thr == -6.0f && att == 10.0f && rel == 50.0f && mk == 0.0f,
            "Master compressor is global data (-6 dB / 10 ms / 50 ms)"); }

    Rec rec; game::SoundCues cues; cues.load(&rec, content);
    Vec3 pawn{0, 0, 0};
    cues.setResolver([&](int owner, const std::string&, const Vec3& off, Vec3& out) { if (owner != 0) return false; out = pawn + off; return true; });
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    const size_t baseCues = cues.cueCount();
    const int basePresets = cues.mixer().presetCount();
    const float dt = 1.0f / 30.0f;
    StageMax mx;
    const Vec3 feL{-67.0f, 2.4f, -152.1f};   // CameraActor_6585 (the frontend camera's authored spot)

    // ---- UI_FrontEnd_m: no AssetTools manifest; Systems manifest only.
    CHECK(host.load("UI_FrontEnd_m") && host.ambient().script().opCount() == 21 && host.ambient().script().linkCount() == 35 &&
          cues.mapCueCount() == 18 && cues.mixer().mapPresetCount() == 1 && host.ambient().emitterCount() == 0,
          "UI_FrontEnd_m: 21 ops / 35 links / 18 cues / 1 reverb preset (%d/%d/%d/%d)", host.ambient().script().opCount(),
          host.ambient().script().linkCount(), cues.mapCueCount(), cues.mixer().mapPresetCount());
    for (int k = 0; k < 30; ++k) stageTick(host, cues, feL, feL, dt, mx);
    CHECK(cues.liveInstances() == 0 && host.state().musicState == 0 && host.state().timelines == 0,
          "frontend: nothing before [FRONTEND START] (no GameplayStarted audio; the movie loader decides)");
    CHECK(host.event("FsCommand:enterFrontEnd", feL) == 4 && host.event("FsCommand:notAuthored", feL) == 0,
          "enterFrontEnd reaches music, reveal, reverb, Camera Orbiter");
    stageTick(host, cues, feL, feL, dt, mx);
    auto st = host.state();
    CHECK(st.reverb == "REVERB_TRANS_FRONT_END" && st.music == kMusic[0] && st.musicState == 2 && st.timelines == 1 &&
          cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_01_BED_LP") == 1 && st.scriptSounds == 6 && st.poolsPlaying == 2 &&
          cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_02_BED_LP") == 0,
          "FRONTEND START: reverb %s, music %s (state %d), timeline, Iacon bed + 4 surface emitters + the (authored silent) reveal (%d), 2 pools (%d)",
          st.reverb.c_str(), st.music.c_str(), st.musicState, st.scriptSounds, st.poolsPlaying);
    // Timeline: Kaon at 240.575 s, loop wrap at 393.551 s.
    float t = dt;
    auto runTo = [&](float until) { while (t < until) { stageTick(host, cues, feL, feL, dt, mx); t += dt; } };
    runTo(240.0f);
    const int debris = host.ambient().script().fired();
    runTo(250.0f);
    st = host.state();
    CHECK(cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_02_BED_LP") == 1 &&
          cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_01_BED_LP") == 0 &&
          cues.activeInstances("BL_LVL_HUD_INTERFACE.EMIT_SURFACE_REACTOR") == 0 && st.poolsPlaying == 2,
          "AUDIO_KAON_AMB: Iacon bed + emitters faded out (2 s / 6 s), Kaon bed on, Kaon pools (%d)", st.poolsPlaying);
    runTo(400.0f);
    CHECK(cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_01_BED_LP") == 1 &&
          cues.activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_02_BED_LP") == 0 && host.state().timelinePos < 7.0f,
          "loop wrap: AUDIO_START refires (Iacon back, Kaon stopped); timeline at %.1f s", host.state().timelinePos);
    CHECK(musicInstances(cues) == 0 && host.state().musicState == 2, "FRONTEND_MX_ORBIT_01 (379.6 s, no loop) played once and ended; not restarted");
    // Five more loops: the per-loop state is the same at the same phase (no growth).
    std::vector<size_t> livePerLoop;
    for (int loop = 0; loop < 5; ++loop) {
        runTo(400.0f + 393.551f * (loop + 1));
        livePerLoop.push_back(cues.liveInstances());
    }
    CHECK(*std::max_element(livePerLoop.begin(), livePerLoop.end()) <= livePerLoop.front() + 2 && mx.live < 40 && mx.pending < 40,
          "6 orbit loops: live instances per loop %zu..%zu, max live %zu, max queued %zu", livePerLoop.front(), livePerLoop.back(), mx.live, mx.pending);
    CHECK(host.ambient().script().fired() > debris, "timeline events keep firing (%d)", host.ambient().script().fired());
    // UI sounds + movie mute on top of the level.
    CHECK(host.playUiSound("BUTTON_ACCEPT") >= 0 && host.playUiSound("BUTTON_BACK") >= 0, "UI navigation sounds play over the frontend");
    host.setMoviePlaying(true); stageTick(host, cues, feL, feL, dt, mx);
    CHECK(host.state().masterScale == 0.0f && host.state().movie, "a Bink movie mutes the game mix (CINE_MUTE_FOR_BINK)");
    host.setMoviePlaying(false);
    std::string why;
    host.unload();
    CHECK(atBaseline(host, cues, baseCues, basePresets, why), "leave frontend: baseline (%s)", why.c_str());
    CHECK(near(cues.mixer().masterScale(), 1.0f), "movie ended before the unload: the game mix is back");

    // ---- Lobbies: GameplayStarted -> music, bed, 2 pools.
    for (const char* lv : {"UI_PartyLobby_m", "UI_Lobby_m", "UI_CampaignLobby_m"}) {
        CHECK(host.load(lv), "%s loads", lv);
        stageTick(host, cues, Vec3{0, 0, 0}, Vec3{0, 0, 0}, dt, mx);
        stageTick(host, cues, Vec3{0, 0, 0}, Vec3{0, 0, 0}, dt, mx);
        st = host.state();
        game::MusicTrack tr; game::FrontendAudio::frontendTrack(lv, tr);
        CHECK(st.music == tr.cue && st.musicState == 2 && st.musicInstance >= 0 && st.scriptSounds == 1 && st.poolsPlaying == 2 && st.reverb.empty(),
              "%s: GameplayStarted -> music %s, bed (%d), 2 pools (%d), no reverb", lv, st.music.c_str(), st.scriptSounds, st.poolsPlaying);
        for (int k = 0; k < 30 * 60; ++k) stageTick(host, cues, Vec3{0, 0, 0}, Vec3{0, 0, 0}, dt, mx);
        CHECK(host.ambient().script().oneShots() >= 4, "%s: pool one-shots over 60 s (%d)", lv, host.ambient().script().oneShots());
        host.unload();
        CHECK(atBaseline(host, cues, baseCues, basePresets, why), "%s unload: baseline (%s)", lv, why.c_str());
    }

    // ---- The full lifecycle, repeated: boot -> frontend -> party lobby -> lobby -> Streets (+ resets) -> lobby ->
    //      frontend -> synthetic map -> ... Baseline after every unload; per-stage maxima must not grow.
    const Vec3 streetsSpawn{363.5f, -724.5f, -341.8f};
    struct Stage { const char* level; const char* path; const char* trigger; float secs; Vec3 L; };
    const Stage stages[] = {
        {"UI_FrontEnd_m", "", "FsCommand:enterFrontEnd", 20.0f, feL},
        {"UI_PartyLobby_m", "", nullptr, 8.0f, {0, 0, 0}},
        {"UI_Lobby_m", "", nullptr, 8.0f, {0, 0, 0}},
        {"MP_IAC_Streets", "", nullptr, 12.0f, streetsSpawn},
        {"UI_Lobby_m", "", nullptr, 5.0f, {0, 0, 0}},
        {"UI_FrontEnd_m", "", "MovieStopped:FMV_intro", 10.0f, feL},
        {"FAKE_TEST_MAP", "fake", nullptr, 6.0f, {0, -1.0f, 0}},
    };
    const int kStages = (int)(sizeof(stages) / sizeof(stages[0]));
    const int kCycles = 30;
    std::vector<StageMax> first(kStages), last(kStages);
    bool allBase = true, noMusicInMatch = true;
    for (int cycle = 0; cycle < kCycles; ++cycle)
        for (int si = 0; si < kStages; ++si) {
            const Stage& sg = stages[si];
            StageMax m;
            const bool ok = host.load(sg.level, std::string(sg.path) == "fake" ? fake : std::string());
            if (!ok) { CHECK(false, "cycle %d %s load", cycle, sg.level); continue; }
            if (sg.trigger) host.event(sg.trigger, sg.L);
            pawn = sg.L;
            const int n = (int)(sg.secs * 30.0f);
            for (int k = 0; k < n; ++k) {
                stageTick(host, cues, sg.L, sg.L, dt, m);
                if (!std::strcmp(sg.level, "MP_IAC_Streets")) {
                    if (k % 15 == 0) cues.play("SHOOT", game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f);
                    if (k == 60) game::PickupPresentation::onTaken("TnHealthPickupFactory", cues, game::SoundCues::Emitter{pawn, 0, {0, 0, 0}, ""}, 0.0f);
                    if (k == 180) { cues.stopNonMapInstances(); host.resetMatch(); }      // round reset
                    if (k % 30 == 0) noMusicInMatch = noMusicInMatch && musicInstances(cues) == 0 && host.state().musicState == 0;
                }
                if (k == 20) host.playUiSound("BUTTON_UP");
            }
            host.unload();
            const bool base = atBaseline(host, cues, baseCues, basePresets, why);
            allBase = allBase && base;
            if (!base) CHECK(false, "cycle %d %s unload: %s voices %d [%s]", cycle, sg.level, why.c_str(), liveRecVoices(rec), liveRecCues(rec).c_str());
            if (cycle == 0) first[(size_t)si] = m;
            if (cycle == kCycles - 1) last[(size_t)si] = m;
        }
    CHECK(allBase, "%d lifecycle cycles x %d levels: every unload returns to the baseline", kCycles, kStages);
    CHECK(noMusicInMatch, "no frontend / lobby music underneath the match");
    bool flat = true;
    for (int si = 0; si < kStages; ++si) {
        std::printf("  stage %-16s live max %zu -> %zu, queued max %zu -> %zu, script sounds %d -> %d, pools %d -> %d\n", stages[si].level,
                    first[(size_t)si].live, last[(size_t)si].live, first[(size_t)si].pending, last[(size_t)si].pending,
                    first[(size_t)si].scriptSounds, last[(size_t)si].scriptSounds, first[(size_t)si].pools, last[(size_t)si].pools);
        flat = flat && last[(size_t)si].live <= first[(size_t)si].live + 3 && last[(size_t)si].scriptSounds <= first[(size_t)si].scriptSounds &&
               last[(size_t)si].pools <= first[(size_t)si].pools;
    }
    CHECK(flat, "per-stage maxima do not grow from cycle 1 to cycle %d", kCycles);

    // ---- Real backend: voices, decoded PCM (incl. streamed music) back to the base after every level.
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP backend: no audio device\n"); return; }
    {
        game::SoundCues rc; rc.load(a, content);
        game::LevelAudioHost rh(rc);
        rh.attach(a, kRoot);
        const size_t baseBytes = a->residentBytes();
        bool clean = true;
        std::vector<StageMax> f2(kStages), l2(kStages);
        const int kRealCycles = 12;
        for (int cycle = 0; cycle < kRealCycles; ++cycle)
            for (int si = 0; si < kStages; ++si) {
                const Stage& sg = stages[si];
                StageMax m;
                if (si == 2) rh.prefetch("UI_Lobby_m");     // loading screen into the lobby: decode its music early
                rh.load(sg.level, std::string(sg.path) == "fake" ? fake : std::string());
                if (sg.trigger) rh.event(sg.trigger, sg.L);
                for (int k = 0; k < 45; ++k) {
                    stageTick(rh, rc, sg.L, sg.L, dt, m, a);
                    if (!std::strcmp(sg.level, "MP_IAC_Streets") && k % 15 == 0) rc.play("SHOOT", sg.L, 0.0f);
                    if (k == 5) rh.playUiSound("BUTTON_ACCEPT");
                }
                // Large streamed music decodes on the worker (M08q): a stage lasts seconds in play - let its deferred music start.
                for (int k = 0; k < 300 && rc.waitingInstances() > 0; ++k) { stageTick(rh, rc, sg.L, sg.L, dt, m, a); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                const int during = a->activeVoices();
                rh.unload();
                const int after = a->activeVoices();
                // An unload never waits for a decode (orphaned, 08o freeze): it is released when it finishes.
                for (int k = 0; k < 600 && rc.orphanDecodes() > 0; ++k) { rc.tick(dt); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                const size_t bytes = a->residentBytes();
                const bool ok = after == 0 && bytes == baseBytes && rc.liveInstances() == 0 && rc.mapCueCount() == 0;
                clean = clean && ok;
                if (!ok || cycle == 0)
                    std::printf("  backend cycle %d %-16s voices %d (max %d) -> %d, PCM max %.1f MB -> %.1f MB (base %.1f)\n", cycle, sg.level,
                                during, m.voices, after, m.pcm, bytes / 1048576.0, baseBytes / 1048576.0);
                if (cycle == 0) f2[(size_t)si] = m;
                if (cycle == kRealCycles - 1) l2[(size_t)si] = m;
            }
        CHECK(clean, "backend: %d cycles x %d levels, voices 0 and PCM back to %.1f MB after every unload", kRealCycles, kStages,
              baseBytes / 1048576.0);
        bool flatB = true;
        for (int si = 0; si < kStages; ++si) {
            std::printf("  backend stage %-16s voices max %d -> %d, PCM max %.1f -> %.1f MB\n", stages[si].level, f2[(size_t)si].voices,
                        l2[(size_t)si].voices, f2[(size_t)si].pcm, l2[(size_t)si].pcm);
            flatB = flatB && l2[(size_t)si].voices <= f2[(size_t)si].voices + 4 && l2[(size_t)si].pcm <= f2[(size_t)si].pcm + 0.01;
        }
        CHECK(flatB, "backend per-stage voice / PCM maxima do not grow over %d cycles", kRealCycles);
    }
    delete a;
}

// The frontend seam (agents/frontend IFrontendAudio) driven exactly as FrontendRuntime::updateAudio does:
// levelChange() on every travel, uiLevelStarted(level) at a lobby's arrival / at the frontend's [FRONTEND START].
static void testFrontendSeam() {
    std::printf("[frontend seam (FrontendAudioRuntime)]\n");
    Rec rec;
    game::FrontendAudioRuntime rt(&rec, kRoot);
    const size_t baseCues = rt.cues().cueCount();
    const int basePresets = rt.cues().mixer().presetCount();
    const float dt = 1.0f / 30.0f;
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs * 30.0f); ++k) rt.tick(dt); };
    std::string why;
    // Boot: logos / intro (movies) -> nothing loaded yet; the movie mutes the game mix.
    rt.setMoviePlaying(true); run(1.0f);
    CHECK(rt.state().level.empty() && rt.cues().liveInstances() == 0 && rt.state().masterScale == 0.0f, "boot: movies, no level audio, mix muted");
    rt.setMoviePlaying(false);
    bool ok = true;
    for (int cycle = 0; cycle < 20; ++cycle) {
        rt.uiLevelStarted("UI_FrontEnd_m"); run(0.1f);
        auto s = rt.state();
        const bool fe = s.reverb == "REVERB_TRANS_FRONT_END" && s.music == "BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01" && s.timelines == 1 &&
                        rt.cues().activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_01_BED_LP") == 1;
        rt.uiLevelStarted("UI_FrontEnd_m"); run(0.1f);      // repeated report: nothing new
        const bool once = rt.cues().activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_AMB_ORBIT_01_BED_LP") == 1 && rt.state().musicInstance == s.musicInstance;
        rt.playUiSound("BUTTON_START"); rt.playUiSound("BUTTON_ACCEPT"); run(3.0f);
        rt.levelChange();                                          // -> party lobby (loading)
        const bool b1 = atBaseline(rt.host(), rt.cues(), baseCues, basePresets, why);
        rt.uiLevelStarted("UI_PartyLobby_m"); run(0.1f);
        const bool party = rt.state().music == "BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX" && rt.state().poolsPlaying == 2;
        rt.playUiSound("BUTTON_DOWN"); run(2.0f);
        rt.levelChange();
        const bool b2 = atBaseline(rt.host(), rt.cues(), baseCues, basePresets, why);
        rt.prefetchLevel("UI_Lobby_m");                            // loading screen into the game lobby
        rt.uiLevelStarted("UI_Lobby_m"); run(0.1f);
        const bool lobby = rt.state().music == "BL_LVL_HUD_INTERFACE.MP_LOBBY_MX" && rt.state().poolsPlaying == 2;
        run(2.0f);
        rt.levelChange();                                          // -> the match (its own World / audio)
        const bool b3 = atBaseline(rt.host(), rt.cues(), baseCues, basePresets, why);
        if (!(fe && once && b1 && party && b2 && lobby && b3))
            std::printf("  cycle %d: fe %d once %d b1 %d party %d b2 %d lobby %d b3 %d (%s)\n", cycle, fe, once, b1, party, b2, lobby, b3, why.c_str());
        ok = ok && fe && once && b1 && party && b2 && lobby && b3;
    }
    CHECK(ok, "20 x (frontend -> party lobby -> game lobby -> match): authored start, no duplicate on repeated reports, baseline at every travel");
}

// M07: match / announcer audio (TnAnnouncer, TnGameTypeMessage, TnGameProgressAnnouncementMessage) and the second
// map (MP_UND_Gorge, AssetTools manifest + Systems manifest) through the generic level path.
static std::string recSoundPath(const Rec& r, int snd) {
    for (const auto& kv : r.paths) if (kv.second == snd) return kv.first;
    return std::string();
}
static void testMatchAudio() {
    std::printf("[match / announcer audio + second map]\n");
    const std::string content = kRoot + "/../content/";
    // The match-start line's waves exist (in the merged extraction) only as the French twin: use the _LOC_int twin
    // extracted by work/m8_loc_extract.py (until AssetTools extracts the twins) [RE TARGETED_PASS4 C].
    _putenv_s("WFC_LOC_ROOT", "F:/Transformers Rebuild/Rebuild-Systems/work/m8_loc/tree");
    Rec rec; game::SoundCues cues; cues.load(&rec, content);
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    const size_t baseCues = cues.cueCount();
    const int basePresets = cues.mixer().presetCount();
    const float dt = 1.0f / 30.0f;
    StageMax mx;
    const Vec3 L{363.5f, -724.5f, -341.8f};
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs * 30.0f); ++k) stageTick(host, cues, L, L, dt, mx); };
    std::string why;
    CHECK(host.load("MP_IAC_Streets") && host.ambient().announcerEvents().size() == 140 && cues.mapCueCount() == 32 + 164,
          "Streets: 140 announcer events, 32 bank + 164 streamed match cues (%zu / %d)", host.ambient().announcerEvents().size(), cues.mapCueCount());
    const size_t residentBefore = rec.paths.size();
    game::MatchAudio& m = host.match();
    CHECK(game::MatchAudio::hasMessageClass("TnGameTypeMessageTDM") && m.dialogCharacter() == "DialogCharacters.OPRIME",
          "TDM message class known; announcer starts as Team0 (OPRIME)");
    // Match start: GameTypeDialog now, GameDescriptionDialog queued, DM_START music.
    const int v0 = rec.n;
    CHECK(m.gameTypeMessage("TnGameTypeMessageTDM", 0), "TnGameTypeMessageTDM switch 0");
    run(0.1f);
    std::string first;
    int voicesFirst = 0;
    for (int i = v0; i < rec.n; ++i) {
        const std::string p = recSoundPath(rec, rec.v[i].sndId);
        if (p.find("WL_DX_") != std::string::npos) { ++voicesFirst; first = p; }
    }
    CHECK(m.currentCue() == "BL_DX_SWITCHBOARD.SC002657" && voicesFirst == 1 && first.find("DX_OPRIME_LK002657") != std::string::npos &&
          first.find("/int/") != std::string::npos &&
          m.queuedCue() == host.ambient().announcerEvents().at("SoundEvents_Dialog.Announcer.MP_GameDescriptionTeamDeathMatchDialog"),
          "game type line SC002657 in the Autobot announcer's voice only, from the _LOC_int twin (%d dialogue voice, %s), description queued (%s)",
          voicesFirst, first.c_str(), m.queuedCue().c_str());
    CHECK(host.music().current().cue == "BL_LVL_MP_MX.DM_START" && host.music().state() == game::MusicPlayer::State::Playing &&
          host.music().current().priority == 0 && host.music().current().fadeIn == 0.0f, "DM_START music (fades 0, priority 0)");
    CHECK(rec.paths.size() > residentBefore, "streamed dialogue / music decoded on first play only (%zu -> %zu waves)", residentBefore, rec.paths.size());
    run(12.0f);                                       // the queued description plays after the first line (Rec voices: tail)
    CHECK(m.linesPlayed() == 2 && m.queuedCue().empty(), "queued line played once the first ended (%d lines)", m.linesPlayed());
    // Same cue while speaking: the queue is cleared; a lower-priority cue does not replace the queued one.
    // Progress + team switch: MGTRON voice.
    for (int k = 0; k < 30 * 30 && (m.speaking() || !m.queuedCue().empty()); ++k) stageTick(host, cues, L, L, dt, mx);
    m.setLocalTeam(1);
    const int v1 = rec.n;
    CHECK(m.progressAnnouncement(1) && m.dialogCharacter() == "DialogCharacters.MGTRON", "1 minute left, Decepticon announcer");
    run(0.1f);
    int mg = 0, op = 0;
    for (int i = v1; i < rec.n; ++i) {
        const std::string p = recSoundPath(rec, rec.v[i].sndId);
        if (p.find("WL_DX_MGTRON") != std::string::npos) ++mg;
        if (p.find("WL_DX_OPRIME") != std::string::npos) ++op;
    }
    CHECK(mg == 1 && op == 0, "MP_1MinuteLeftDialog plays the Megatron wave only (%d / %d)", mg, op);
    m.gameTypeMessage("TnGameTypeMessageTDM", 1); run(0.2f);
    CHECK(host.music().current().cue == "BL_LVL_MP_MX.DM_FINALSTRETCH_LP", "nearly complete: DM_FINALSTRETCH_LP");
    m.gameTypeMessage("TnGameTypeMessageTDM", 2, 1); run(0.2f);
    CHECK(host.music().queued().cue == "BL_LVL_MP_MX.DM_END_DECEPTICONS_WIN" && host.music().queued().priority == 1 &&
          host.music().current().cue == "BL_LVL_MP_MX.DM_FINALSTRETCH_LP", "end, Decepticons win: DM_END_DECEPTICONS_WIN queued (priority 1) behind SpazTime");
    run(5.0f);
    CHECK(host.music().current().cue == "BL_LVL_MP_MX.DM_END_DECEPTICONS_WIN", "... and crossfaded in after the 5 s SpazTime");
    CHECK(m.versusGameOver(1) || m.queuedCue() != "", "game over: the winning team's line");
    { game::MusicPlayer probe(cues); (void)probe; }
    CHECK(!m.gameTypeMessage("TnGameTypeMessageNOPE", 0) && !m.progressAnnouncement(9) && !m.announcerEvent("SoundEvents_Dialog.Announcer.NotAnEvent"),
          "unknown message / switch / event: nothing invented");
    CHECK(game::MatchAudio::messageClassForMode("TDM") == "TnGameTypeMessageTDM" && game::MatchAudio::messageClassForMode("DM") == "TnGameTypeMessageDM" &&
          game::MatchAudio::messageClassForMode("CTF") == "TnGameTypeMessageCTF" && game::MatchAudio::messageClassForMode("KOTH") == "TnGameTypeMessageKOTH" &&
          game::MatchAudio::messageClassForMode("DOM") == "TnGameTypeMessageDOM" && game::MatchAudio::messageClassForMode("EXT") == "TnGameTypeMessageEXT",
          "mode tag -> game-type message class (TnOnlineGameSettings<tag>.Rules)");
    host.unload();
    CHECK(atBaseline(host, cues, baseCues, basePresets, why) && m.currentCue().empty() && host.ambient().announcerEvents().empty(),
          "leave Streets: announcer + match cues gone (%s)", why.c_str());
    CHECK(!m.announcerEvent("SoundEvents_Dialog.Announcer.MP_1MinuteLeftDialog"), "no announcer outside a match level");

    // MP_UND_Gorge: the AssetTools manifest + Systems manifest through the same path (not play-ready; audio only).
    CHECK(host.load("MP_UND_Gorge") && host.ambient().emitterCount() == 15 && host.ambient().zoneCount() == 12 &&
          cues.mixer().mapPresetCount() == 6 && cues.mapCueCount() == 13 + 18 + 164 && host.ambient().announcerEvents().size() == 140,
          "Gorge: 15 emitters, 12 zone ops (23 touch volumes), 6 reverb presets, 13 + 157 cues, announcer (%d / %d / %d / %d)", host.ambient().emitterCount(),
          host.ambient().zoneCount(), cues.mixer().mapPresetCount(), cues.mapCueCount());
    run(3.0f);
    CHECK(host.ambient().activeEmitters() > 0, "Gorge bed plays (%d emitters)", host.ambient().activeEmitters());
    host.unload();
    CHECK(atBaseline(host, cues, baseCues, basePresets, why), "leave Gorge: baseline (%s)", why.c_str());
    // Streets <-> Gorge <-> frontend, repeated: baseline at every boundary.
    bool ok = true;
    for (int c = 0; c < 10; ++c)
        for (const char* lv : {"MP_IAC_Streets", "MP_UND_Gorge", "UI_FrontEnd_m"}) {
            host.load(lv);
            if (!std::strcmp(lv, "UI_FrontEnd_m")) host.event("FsCommand:enterFrontEnd", L);
            else {
                host.match().onMatchStarted(c % 2 ? "DM" : "TDM", c % 2);
                host.match().onProgressAnnouncement(c % 8);
                host.match().onGameNearlyComplete();
                host.match().onMatchEnded(c % 3 - 1, c % 2 == 0);
            }
            run(2.0f);
            host.unload();
            ok = ok && atBaseline(host, cues, baseCues, basePresets, why);
        }
    CHECK(ok, "10 x (Streets -> Gorge -> frontend) with match messages: baseline at every unload (%s)", why.c_str());
}

// M08: the generated Kismet graph (touch -> zone -> reverb / pools ...) against the AssetTools flattened zones on
// Streets (the path validated since M04): the same pawn walk must give the same zone, reverb, mixer stack, environment
// and pool cues at every step. Then every processed multiplayer map loads, walks its zones and unloads to the baseline.
static std::vector<ZoneGeo> mapZoneGeos(const assets::Json& j) {
    std::vector<ZoneGeo> zs;
    for (size_t i = 0; i < j["zones"].size(); ++i) {
        const assets::Json& z = j["zones"][i];
        ZoneGeo g; g.name = z["comment"].asString(); g.preset = z["reverb_preset"].asString();
        g.mn = {1e30f, 1e30f, 1e30f}; g.mx = {-1e30f, -1e30f, -1e30f};
        const assets::Json& polys = z["trigger_polygons_gltf"];
        for (size_t p = 0; p < polys.size(); ++p)
            for (size_t v = 1; v + 1 < polys[p].size(); ++v)
                for (size_t k : {(size_t)0, v, v + 1}) {
                    Vec3 t{polys[p][k][0].asFloat(), polys[p][k][1].asFloat(), polys[p][k][2].asFloat()};
                    g.tris.push_back(t);
                    g.mn = {std::min(g.mn.x, t.x), std::min(g.mn.y, t.y), std::min(g.mn.z, t.z)};
                    g.mx = {std::max(g.mx.x, t.x), std::max(g.mx.y, t.y), std::max(g.mx.z, t.z)};
                }
        zs.push_back(g);
    }
    return zs;
}
// One interior point per zone volume (the pawn probe is 1 m above the origin: return the origin).
static std::vector<Vec3> zonePoints(const std::vector<ZoneGeo>& zs, unsigned seed) {
    std::srand(seed);
    auto rnd = [](float a, float b) { return a + (b - a) * (float)std::rand() / (float)RAND_MAX; };
    std::vector<Vec3> pts(zs.size(), Vec3{1e9f, 0, 0});
    for (size_t z = 0; z < zs.size(); ++z)
        for (int k = 0; k < 20000; ++k) {
            Vec3 p{rnd(zs[z].mn.x, zs[z].mx.x), rnd(zs[z].mn.y, zs[z].mx.y), rnd(zs[z].mn.z, zs[z].mx.z)};
            if (insideZ(zs[z], p)) { pts[z] = p - Vec3{0, 1.0f, 0}; break; }
        }
    return pts;
}

static void testZoneGraph() {
    std::printf("[generated zone graph vs manifest zones; every MP map]\n");
    const std::string content = kRoot + "/../content/";
    assets::Json sj = loadJson(streetsAudio());
    std::vector<ZoneGeo> zs = mapZoneGeos(sj);
    std::vector<Vec3> pts = zonePoints(zs, 11);
    const Vec3 nowhere{0.0f, 5000.0f, 0.0f};
    Rec ra, rb; game::SoundCues ca, cb; ca.load(&ra, content); cb.load(&rb, content);
    game::AmbientAudio ga, ma;
    ma.setPreferManifestZones(true);
    CHECK(ga.load(streetsAudio(), content, ca, &ra) && ma.load(streetsAudio(), content, cb, &rb), "Streets loads both ways");
    CHECK(ga.zoneCount() == 9 && ma.zoneCount() == 9 && ga.poolCount() == 11 && ma.poolCount() == 11 && ga.script().hasTouchZones(),
          "graph: 9 zones / 11 pools (%d / %d); manifest 9 / 11", ga.zoneCount(), ga.poolCount());
    std::srand(3);
    int steps = 0, same = 0;
    auto step = [&](const Vec3& p, float secs) {
        for (float t = 0; t < secs; t += 1.0f / 30.0f) {
            ga.tick(1.0f / 30.0f, p, p, ca); ca.tick(1.0f / 30.0f);
            ma.tick(1.0f / 30.0f, p, p, cb); cb.tick(1.0f / 30.0f);
        }
        ++steps;
        const bool eq = ca.mixer().currentReverb() == cb.mixer().currentReverb() && ca.mixer().activeList() == cb.mixer().activeList() &&
                        !std::strcmp(ga.zoneName(), ma.zoneName()) &&
                        (ra.envs.empty() == rb.envs.empty()) && (ra.envs.empty() || near(ra.envs.back().room, rb.envs.back().room, 0.5f));
        if (eq) ++same;
        else std::printf("  step %d: graph %s [%s] vs manifest %s [%s]\n", steps, ga.zoneName(), ca.mixer().activeList().c_str(),
                         ma.zoneName(), cb.mixer().activeList().c_str());
    };
    step(nowhere, 0.5f);
    for (int k = 0; k < 60; ++k) {
        const int z = std::rand() % (int)zs.size();
        if (pts[(size_t)z].x > 1e8f) continue;
        step(pts[(size_t)z], 1.0f + (float)(std::rand() % 40) / 10.0f);   // pools run while inside
        if (k % 5 == 0) step(nowhere, 0.5f);                                 // leaving keeps the last zone
    }
    CHECK(same == steps, "60-step random walk: zone, reverb, mixer stack and environment identical at %d / %d steps", same, steps);
    // Pool one-shots: a 120 s dwell in each pooled zone fires the same pools at the same mean rate on both paths.
    int gaN = 0, maN = 0;
    for (size_t z = 0; z < zs.size(); ++z) {
        if (pts[z].x > 1e8f) continue;
        const int g0 = ga.script().oneShots(), m0 = ma.oneShotsPlayed();
        step(pts[z], 120.0f);
        gaN += ga.script().oneShots() - g0; maN += ma.oneShotsPlayed() - m0;
    }
    CHECK(gaN > 50 && maN > 50 && std::abs(gaN - maN) < (gaN + maN) / 5, "zone pools over 9 x 120 s: graph %d vs manifest %d one-shots (within 20 %%)",
          gaN, maN);
    ga.resetMatch(); ma.resetMatch();
    CHECK(ga.script().poolsPlaying() == 0 && !ma.poolsRunning(), "match reset stops the pools on both paths");
    step(pts[0], 0.5f);
    CHECK(ga.script().poolsPlaying() > 0 && ma.poolsRunning(), "re-touch after reset restarts the scene on both paths");
    ga.unload(ca); ma.unload(cb); ca.stopAll(); cb.stopAll();

    // Every processed MP map: load, walk each zone volume (death / respawn included), unload -> baseline.
    Rec rec; game::SoundCues cues; cues.load(&rec, content);
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    const size_t baseCues = cues.cueCount();
    const int basePresets = cues.mixer().presetCount();
    std::string why;
    const char* maps[] = {"MP_ESC_BrokenHope", "MP_ESC_Remnant", "MP_IAC_Berth", "MP_IAC_Rust", "MP_IAC_Seed", "MP_IAC_Streets",
                          "MP_KON_Molten", "MP_ORB_Debris", "MP_UND_Complex", "MP_UND_Gorge"};
    bool allOk = true, allReverbs = true;
    for (int cycle = 0; cycle < 3; ++cycle)
        for (const char* mname : maps) {
            const bool ok = host.load(mname);
            assets::Json aj = loadJson(kRoot + "/Maps/" + mname + "/audio.json");
            std::vector<ZoneGeo> g = mapZoneGeos(aj);
            std::vector<Vec3> zp = zonePoints(g, 5 + (unsigned)cycle);
            std::set<std::string> reverbs, zones;
            int entered = 0;
            auto run = [&](const Vec3& p, float secs, bool alive = true) {
                for (float t = 0; t < secs; t += 1.0f / 30.0f) { cues.setListener(p); host.tick(1.0f / 30.0f, p, p, alive); cues.tick(1.0f / 30.0f); }
            };
            for (size_t z = 0; z < zp.size(); ++z) {
                if (zp[z].x > 1e8f) continue;
                run(zp[z], 0.4f);
                if (!std::string(host.ambient().zoneName()).empty() && std::strcmp(host.ambient().zoneName(), "-")) { ++entered; zones.insert(host.ambient().zoneName()); }
                if (!cues.mixer().currentReverb().empty()) reverbs.insert(cues.mixer().currentReverb());
                if (z == zp.size() / 2) { run(zp[z], 0.5f, false); host.resetMatch(); run(zp[z], 0.5f); }   // death, round reset, respawn
            }
            const auto st = host.state();
            if (cycle == 0)
                std::printf("  %-18s %s: %d emitters, %d zones, %d pools/flybys, %zu touch volumes walked, %zu zones entered, %zu reverbs, "
                            "%d presets, %d level cues, one-shots %d, mixer [%s]\n", mname, ok ? "ok" : "FAILED", host.ambient().emitterCount(),
                            host.ambient().zoneCount(), host.ambient().poolCount(), zp.size(), zones.size(), reverbs.size(),
                            cues.mixer().mapPresetCount(), cues.mapCueCount(), host.ambient().script().oneShots(), st.mixer.c_str());
            // Every authored REVERB_* preset of the map is reached by some zone (Experimental sweep M08c: the flattened
            // manifest zones of six maps name none - the graph must still activate them all).
            int authoredReverbs = 0;
            if (const char* mj = game::AmbientAudio::manifestJson(mname)) {
                assets::Json sj2; assets::Json::parse(mj, sj2);
                for (const auto& kv : sj2["reverb_presets"].obj)
                    if (kv.first.rfind("REVERB", 0) == 0) { ++authoredReverbs; if (!reverbs.count(kv.first)) { allReverbs = false;
                        if (cycle == 0) std::printf("  %s: reverb %s never activated\n", mname, kv.first.c_str()); } }
            }
            if (cycle == 0) std::printf("  %-18s reverbs reached %zu / %d authored\n", mname, reverbs.size(), authoredReverbs);
            const bool hasReverbOps = cues.mixer().mapPresetCount() > 0;
            const bool good = ok && host.ambient().emitterCount() > 0 && (!hasReverbOps || !reverbs.empty()) && entered > 0 &&
                              host.ambient().announcerEvents().size() == 140;
            if (!good) std::printf("  FAILED map %s (cycle %d): ok %d emitters %d reverbs %zu entered %d announcer %zu\n", mname, cycle, ok,
                                   host.ambient().emitterCount(), reverbs.size(), entered, host.ambient().announcerEvents().size());
            host.unload();
            const bool base = atBaseline(host, cues, baseCues, basePresets, why);
            if (!base) std::printf("  FAILED baseline after %s: %s\n", mname, why.c_str());
            allOk = allOk && good && base;
        }
    CHECK(allOk, "3 cycles x 10 MP maps: each loads its bed, enters zones, switches reverb, survives death / reset, unloads to the baseline");
    CHECK(allReverbs, "every MP map: walking its zone volumes activates every authored REVERB_* preset (graph zones, not the manifest's)");
}

// M08: character audio profiles + the movie language track rule (RE 433ef9e).
static void testCharacterAudio() {
    std::printf("[character audio profiles; movie language tracks]\n");
    using audio::movieLanguageSlot;
    CHECK(movieLanguageSlot("INT") == 0 && movieLanguageSlot("int") == 0 && movieLanguageSlot("FRA") == 1 && movieLanguageSlot("ITA") == 2 &&
          movieLanguageSlot("DEU") == 3 && movieLanguageSlot("ESN") == 4 && movieLanguageSlot("RUS") == 5 && movieLanguageSlot("POL") == 6 &&
          movieLanguageSlot("JPN") == 0, "GLanguage -> L (0x82CBEEE0)");
    CHECK(audio::movieTrackLayout(10, 0).c == 5 && audio::movieTrackLayout(10, 1).c == 6 && audio::movieTrackLayout(10, 4).c == 9 &&
          audio::movieTrackLayout(10, 5).c == -1 && audio::movieTrackLayout(6, 0).c == 5 && audio::movieTrackLayout(6, 1).c == -1 &&
          audio::movieTrackLayout(10, 0).lfe == 4, "tracks [0..4, 5 + L], a missing index ignored");
    CHECK(game::CharacterAudio::profileCount() == 33, "33 roster chassis profiles (%d)", game::CharacterAudio::profileCount());
    const game::CharacterAudioProfile& op = game::CharacterAudio::defaultProfile();
    CHECK(op.key == "Truck" && op.voiceCue("FS_DEFAULT_WALK") == "BL_FS_LRG_BOT.FS_WALK_DEFAULT" &&
          op.vehicleCue("Auto_Boost_Start") == "BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_START" &&
          op.vehicleCue("Auto_Ram_Impact") == "BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT" && op.clip("Transform_ToVehicle_ROBO") &&
          op.notifyCue(op.clip("Transform_ToVehicle_ROBO")->notifies[0]) == "BL_TRANSFORM.OPTIMUS_BOT2VEH",
          "default profile = Optimus: walk, boost, ram, transform cues");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    CHECK(cues.hasCue("BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_START") && cues.hasCue("BL_WPN_GUN_ION_BLASTER.SHOOT") &&
          cues.hasCue("BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY") && !cues.hasCue("BL_WPN_GUN_NEUTRON_RIFLE.SHOOT"),
          "full asset names resolve to the compiled short names (exact packages only)");
    int missingDefault = 0;
    for (const auto& e : op.vehicle) missingDefault += cues.hasCue(e.second.c_str()) ? 0 : 1;
    CHECK(game::CharacterAudio::loadCues(cues, op) >= 0, "default profile loads");
    for (const char* key : {"Car", "Tank", "Jet"}) {
        const game::CharacterAudioProfile* p = game::CharacterAudio::find(key);
        const int before = cues.mapCueCount();
        const int added = p ? game::CharacterAudio::loadCues(cues, *p) : -1;
        int resolved = 0, total = 0;
        if (p) for (const auto& e : p->vehicle) { ++total; resolved += cues.hasCue(e.second.c_str()) ? 1 : 0; }
        CHECK(p && added > 0 && cues.mapCueCount() == before + added && resolved == total && !p->voiceCue("FS_DEFAULT_WALK").empty() &&
              cues.hasCue(p->voiceCue("FS_DEFAULT_WALK").c_str()),
              "%s (%s): %d cues loaded, vehicle %d / %d resolve, walk %s", key, p ? p->voiceSet.c_str() : "-", added, resolved, total,
              p ? p->voiceCue("FS_DEFAULT_WALK").c_str() : "-");
    }
    CHECK(game::CharacterAudio::weaponCue("TransContent.TnWeaponHeavyPistol", "WP_Fire") == "BL_WPN_GUN_PISTOL_HVY.SHOOT" &&
          game::CharacterAudio::loadWeaponCues(cues, "TransContent.TnWeaponHeavyPistol") >= 0 && cues.hasCue("BL_WPN_GUN_PISTOL_HVY.SHOOT"),
          "weapon class WeaponSounds load and resolve");
    {   // impacts: the weapon mesh's DefaultImpactSound (world) + the victim's HitEffectPlayer HitSound event
        const game::WeaponHitEffect* ion = game::CharacterAudio::weaponHitEffect("TransContent.TnWeaponIonBlaster");
        const game::WeaponHitEffect* hp = game::CharacterAudio::weaponHitEffect("TransContent.TnWeaponHeavyPistol");
        CHECK(game::CharacterAudio::weaponCue("TransContent.TnWeaponIonBlaster", "DefaultImpactSound") == "BL_WPN_GUN_ION_BLASTER.IMPT_WORLD" &&
              game::CharacterAudio::weaponCue("TransContent.TnWeaponSniperRifle", "DefaultImpactSound") == "BL_WPN_GUN_SNIPER.IMPT_WORLD",
              "world impact = the weapon's DefaultImpactSound");
        CHECK(ion && ion->hitEvent == "IMPT_DMG_ION" && ion->blockEvent == "IMPT_BLOCK_ION" && ion->index == 0 && ion->causesBlood &&
              std::fabs(ion->retrigger - 0.1f) < 1e-6f && hp && hp->hitEvent == "IMPT_DMG_PISTOL_HVY" &&
              game::CharacterAudio::defaultProfile().voiceCue(ion->hitEvent) == "BL_WPN_GUN_ION_BLASTER.IMPT_DMG",
              "hit effect by damage type (SharedHitEffectPlayer) -> the victim's IMPT_DMG event cue, retrigger 0.1 s");
        CHECK(!game::CharacterAudio::weaponHitEffect("TransContent.TnWeaponShotgun"),
              "TnDamageTypeShotgun has no entry (nor its parents): no hit sound, as FindEffect returns -1");
        game::CharacterAudio::loadHitCues(cues, game::CharacterAudio::defaultProfile(), "TransContent.TnWeaponSniperRifle");
        CHECK(cues.hasCue("BL_WPN_GUN_SNIPER.IMPT_DMG") && cues.hasCue("BL_WPN_GUN_SNIPER.IMPT_BLOCK"), "victim hit / block cues load");
    }
    {   // vehicle components: Optimus = the previous hand-entered values; other chassis their own slots / tunables
        const game::VehicleAudioComponentData& o = game::CharacterAudio::defaultProfile().vehicleComponent;
        const game::CharacterAudioProfile* tank = game::CharacterAudio::find("Tank");
        const game::CharacterAudioProfile* jet = game::CharacterAudio::find("Jet");
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
        CHECK(o.valid && near(o.boostFadeIn, 0.1f) && near(o.boostFadeOut, 0.15f) && near(o.boostWheelsDelay, 0.27f) &&
              near(o.squealMinMph, 20.0f) && near(o.squealFade, 0.5f) && near(o.engineFadeIn, 0.1f) && near(o.engineFadeOut, 0.2f) &&
              near(o.jumpRevTime, 0.25f) && o.useJumpRev && o.speedHistory == 15 && o.drive.size() == 2 && near(o.drive[0].maxSpeed, 20.0f) &&
              o.hoverLand.size() == 2 && near(o.hoverLand[1].t, 2.0f) && o.ram == "Auto_Ram_Impact" && o.squeal == "Auto_Tire_Squeal_Default",
              "Optimus vehicle component = the previous hand-entered tunables / slots");
        CHECK(tank && tank->vehicleComponent.valid && tank->vehicleComponent.ram.empty() && tank->vehicleComponent.boostWheels.empty() &&
              !tank->vehicleComponent.useJumpRev && near(tank->vehicleComponent.squealMinMph, 3.0f) &&
              tank->vehicleComponent.boostOneshots.size() == 1 && near(tank->vehicleComponent.drive[1].maxSpeed, 100.0f),
              "Megatron: no ram / boost-wheels slot, no jump rev, class-default squeal speed, a boost one-shot, gear 2 to 100 mph");
        CHECK(jet && jet->vehicleComponent.ascend == "Auto_Engine_Hover_Ascend" && jet->vehicleComponent.booster == "Auto_Hover_Boosters",
              "Starscream: hover ascend / boosters slots");
    }
    {   // weapon-mesh animation sounds: the Ion Blaster's generated table = the hand-checked M03 table; timeline rules
        const game::WeaponAnimSounds* ion = game::CharacterAudio::weaponAnimSounds("TransContent.TnWeaponIonBlaster");
        auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
        CHECK(ion && ion->reload.name == "Shooting_Reload_IonBlaster_AP" && ion->reload.sounds.size() == 2 &&
              near(ion->reload.sounds[0].first, 0.0f) && near(ion->reload.sounds[1].first, 0.137f) &&
              ion->reload.sounds[1].second == "BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_02" && ion->idle.name == "IonBlaster_Idle" &&
              near(ion->idle.length, 4.2f) && ion->idle.sounds.size() == 2 && near(ion->idle.sounds[1].first, 2.751f) &&
              ion->equip.sounds.size() == 1 && ion->putDown.sounds.size() == 1 && ion->fire.sounds.empty(),
              "Ion Blaster weapon-anim sounds = the M03 hand table (reload 0 / 0.137, idle 0.022 / 2.751, equip, holster)");
        game::WeaponSoundTimeline tl;
        tl.set(ion);
        std::vector<const std::string*> out;
        std::vector<float> at;
        tl.play(game::WeaponSoundTimeline::Event::Reload);
        for (int k = 0; k < 120; ++k) { const size_t n = out.size(); tl.tick(1.0f / 60.0f, out); for (size_t i = n; i < out.size(); ++i) at.push_back(k / 60.0f); }
        CHECK(out.size() == 3 && *out[0] == "BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_01" && near(at[0], 0.0f) &&
              *out[1] == "BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_02" && std::fabs(at[1] - 0.137f) < 1.0f / 60.0f + 1e-4f &&
              *out[2] == "BL_WPN_GUN_ION_BLASTER.IDLE_01" && at[2] > 1.66f && at[2] < 1.72f,
              "timeline: reload notifies at their times, then back to the looping idle (%zu sounds)", out.size());
        const game::WeaponAnimSounds* hp = game::CharacterAudio::weaponAnimSounds("TransContent.TnWeaponHeavyPistol");
        game::CharacterAudio::loadWeaponCues(cues, "TransContent.TnWeaponSniperRifle");
        CHECK(hp && hp->reload.name == "heavypistol_reload" && hp->reload.sounds.size() == 1 &&
              hp->reload.sounds[0].second == "BL_WPN_GUN_PISTOL_HVY.ANIM_RELOAD" && cues.hasCue("BL_WPN_GUN_SNIPER.ANIM_RELOAD") &&
              cues.hasCue("BL_WPN_GUN_SNIPER.IDLE_02"), "Heavy Pistol / Sniper: their own reload / idle sounds, loaded with the weapon");
    }
    cues.unloadMapCues();
    CHECK(cues.mapCueCount() == 0, "character / weapon cues are level-owned (released with the level)");
}

// M08: objective / round message audio (TnFlagMessage, TnBombMessage, TnDominationMessage, TnCTFMessage,
// TnRoundBasedGameMessage, TnKingOfTheHillZoneBase) - which announcer cue / stinger / music each switch plays.
static void testObjectiveMessages() {
    std::printf("[objective / round messages]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    CHECK(host.load("MP_IAC_Seed"), "Seed loads (announcer + match cues)");
    game::MatchAudio& m = host.match();
    const auto& ev = host.ambient().announcerEvents();
    auto cueOf = [&](const char* e) { auto it = ev.find(std::string("SoundEvents_Dialog.Announcer.") + e); return it == ev.end() ? std::string() : it->second; };
    auto idle = [&] { for (int k = 0; k < 30 * 30 && (m.speaking() || !m.queuedCue().empty()); ++k) { host.tick(1.0f / 30.0f, {0, 0, 0}, {0, 0, 0}); cues.tick(1.0f / 30.0f); } };
    struct Case { const char* what; std::function<bool()> fire; const char* event; };
    const Case cases[] = {
        {"flag picked up", [&] { return m.flagMessage(1); }, "MP_FlagPickedUpDialog"},
        {"flag scored", [&] { return m.flagMessage(3); }, "MP_FlagScoredDialog"},
        {"bomb picked up by Decepticons", [&] { return m.bombMessage(1, 1); }, "MP_DecepticonPickupBombDialog"},
        {"bomb planted", [&] { return m.bombMessage(5, 0); }, "MP_BombPlantedDialog"},
        {"Autobots capturing C", [&] { return m.dominationMessage(2 * 10 + 2); }, "MP_AutobotsCapturing_C_Dialog"},
        {"CTF attacker line", [&] { return m.ctfMessage(true); }, "MP_GameTypeCaptureCodeOfPowerDialog"},
        {"round time up", [&] { return m.roundMessage(2); }, "MP_RoundTimeUp"},
        {"hill moved", [&] { return m.kothZoneActivated(); }, "MP_HillMovedDialog"},
        {"hill contested", [&] { return m.kothDefenderChanged(254); }, "MP_HillContestedDialog"},
        // TnKillstreakActivated* by role (RE pass 5 s12 addendum 11)
        {"Orbital Recon, activator", [&] { return m.killstreakActivated("OrbitalReconStreak", game::MatchAudio::StreakRole::Self, 0); },
         "MP_OrbitalReconActivatedSelfDialog"},
        {"Orbital Recon, activator's team", [&] { return m.killstreakActivated("OrbitalReconStreak", game::MatchAudio::StreakRole::Friendly, 0); },
         "MP_OrbitalReconActivatedFriendlyDialog"},
        {"Orbital Recon, the other team", [&] { return m.killstreakActivated("OrbitalReconStreak", game::MatchAudio::StreakRole::Enemy, 0); },
         "MP_OrbitalReconActivatedEnemyDialog"},
        {"Ammo Matrix, activator", [&] { return m.killstreakActivated("RefillAmmoStreak", game::MatchAudio::StreakRole::Self, 1); },
         "MP_AmmoMatrixOnlineDialog"},
        {"Omega Missile, the other team: FactionAnnouncementSound[Decepticons] fallback",
         [&] { return m.killstreakActivated("GuidedMissileStreak", game::MatchAudio::StreakRole::Enemy, 1); }, "MP_DecepticonFiredOmegaMissleDialog"},
    };
    for (const Case& c : cases) {
        idle();
        const bool ok = c.fire();
        const std::string want = cueOf(c.event);
        CHECK(ok && !want.empty() && m.currentCue() == want, "%s -> %s (%s, now %s)", c.what, c.event, want.c_str(), m.currentCue().c_str());
    }
    idle();
    CHECK(!m.killstreakActivated("RefillAmmoStreak", game::MatchAudio::StreakRole::Enemy, 0) &&
          !m.killstreakActivated("NoSuchStreak", game::MatchAudio::StreakRole::Self, 0),
          "a role with no sound and no faction list (Ammo Matrix, enemy) and an unknown streak: silent");
    idle();
    const int before = cues.activeInstances("BL_HUD_INTERFACE.CTF_FLAG_CAPTURE");
    m.flagMessage(3);
    CHECK(cues.activeInstances("BL_HUD_INTERFACE.CTF_FLAG_CAPTURE") == before + 1, "flag scored: the HUD stinger CTF_FLAG_CAPTURE plays");
    CHECK(!m.kothDefenderChanged(0, true) && !m.kothZoneActivated(true), "KOTH: ignored team change / match over -> no line");
    CHECK(host.music().queued().cue == "BL_LVL_MP_MX.COP_ROUND_OVER" || host.music().current().cue == "BL_LVL_MP_MX.COP_ROUND_OVER",
          "round time up queues TimeUpMusic COP_ROUND_OVER");
    CHECK(game::SoundMixer::movieAlwaysPlaysSound("Logo_Hasbro") && !game::SoundMixer::movieAlwaysPlaysSound("FMV_intro"),
          "MoviesToAlwaysPlaySound: the three logos (fixed Bink volume 0xCCCC = 0.8)");
    const float def = host.movieSfxVolume();
    host.setMovieFxSlider(55);
    const float s55 = host.movieSfxVolume();
    host.setMovieSfxVolume(1.7f);
    const float hi = host.movieSfxVolume();
    host.setMovieSfxVolume(0.6f);
    CHECK(def == 1.0f && s55 == 0.55f && hi == 1.0f && host.movieSfxVolume() == 0.6f && game::SoundMixer::groupVolume("SFX") == 0.6f,
          "GetMovieVolume: the SFX class volume = the 'SFX' group = FX slider / 100, clamped [0,1] (suite runs at 1)");
    game::SoundMixer::resetGroupVolumes();
    CHECK(host.movieSfxVolume() == 0.8f, "profile default FX 80 -> movie volume 0.8");
    for (const char* g : {"SFX", "DIALOG", "MUSIC"}) game::SoundMixer::setGroupVolume(g, 1.0f);
    host.unload();
}

// Localized announcer waves (RE TARGETED_PASS4 C): the TDM match-start line SC002657 must come from the selected
// language's _LOC twin - never the French copy the merged extraction holds.
static void testLocalizedWaves() {
    std::printf("[localized waves: _LOC twin of GLanguage]\n");
    const std::string content = kRoot + "/../content/";
    auto loadedPaths = [&](const char* locRoot) {
        if (locRoot) _putenv_s("WFC_LOC_ROOT", locRoot); else _putenv_s("WFC_LOC_ROOT", "");
        Rec rec; game::SoundCues cues; cues.load(&rec, content);
        game::LevelAudioHost host(cues);
        host.attach(&rec, kRoot);
        host.load("MP_IAC_Streets");
        host.match().onMatchStarted("TDM", 0);
        for (int k = 0; k < 60; ++k) { host.tick(1.0f / 30.0f, {0, 0, 0}, {0, 0, 0}); cues.tick(1.0f / 30.0f); }
        std::vector<std::string> out;
        for (const auto& kv : rec.paths) if (kv.first.find("LK002657") != std::string::npos || kv.first.find("LK002666") != std::string::npos) out.push_back(kv.first);
        host.unload();
        return out;
    };
    // AssetTools extracts both twins into content/_LOC/<twin>/ (int: the English line).
    auto shipped = loadedPaths(nullptr);
    bool shippedInt = !shipped.empty();
    for (const std::string& p : shipped) shippedInt = shippedInt && p.find("/_LOC/int/") != std::string::npos;
    CHECK(shippedInt, "match-start line from content/_LOC/int (AssetTools twins), never the merged French copy (%zu loaded: %s)",
          shipped.size(), shipped.empty() ? "-" : shipped[0].c_str());
    const std::string tree = "F:/Transformers Rebuild/Rebuild-Systems/work/m8_loc/tree";
    std::FILE* fp = std::fopen((tree + "/int/WL_DX_OPRIME/DX_OPRIME_LK002657.wav").c_str(), "rb");
    if (fp) {
        std::fclose(fp);
        auto withTree = loadedPaths(tree.c_str());
        bool intOnly = !withTree.empty();
        for (const std::string& p : withTree) intOnly = intOnly && p.find("/int/") != std::string::npos;
        CHECK(intOnly, "with the _LOC_int twin: the match-start line loads from it (%s)", withTree.empty() ? "-" : withTree[0].c_str());
    } else std::printf("  (skip: work/m8_loc/tree not generated - run work/m8_loc_extract.py)\n");
    _putenv_s("WFC_LOC_ROOT", "");
    CHECK(game::SoundCues::localizedWave("WL_DX_X/A.wav", "int", content) == "WL_DX_X/A.wav" &&
          game::SoundCues::localizedWave("WL_DX_X/A.wav", "FRA", content).empty(), "an int-owned copy plays as is; a FRA-owned copy never substitutes");
}

// M08d: weapon sounds by identity, projectile flight / explosion, the Repair Ray beam (WeaponAudio).
static void testWeaponAudio() {
    std::printf("[weapon audio: identity, projectiles, beam]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    host.load("MP_IAC_Streets");
    for (const char* c : {"TransContent.TnWeaponRepairRay", "TransContent.TnWeaponRocketLauncher", "TransContent.TnWeaponAssaultRifleVehicle"})
        game::CharacterAudio::loadWeaponCues(cues, c);
    game::WeaponAudio wa;
    game::SoundCues::Emitter e; e.pos = {0, 0, 0};
    auto step = [&](float secs) { for (int k = 0; k < (int)(secs * 60.0f); ++k) cues.tick(1.0f / 60.0f); };
    auto live = [&](const char* q) { return cues.activeInstances(q); };
    // identity: the vehicle rifle's own shot; a beam weapon has no per-shot sound
    CHECK(wa.fire(cues, "TransContent.TnWeaponAssaultRifleVehicle", false, e, 1.0f) >= 0 && live("BL_WPN_GUN_NEUTRON_RIFLE.VEH_SHOOT") == 1 &&
          wa.fire(cues, "TransContent.TnWeaponRepairRay", false, e, 1.0f) < 0, "fire by weapon identity; beam weapons: no per-shot sound");
    // projectile: flight loop from spawn, moved, explosion + 0.25 s fade on Explode, nothing left
    wa.projectileSpawned(cues, 7, "TransContent.TnWeaponRocketLauncher", {0, 0, 0}, 5.0f);
    step(0.2f);
    wa.projectileMoved(cues, 7, {0, 0, 20});
    const bool flying = live("BL_WPN_GUN_ROCKET.TRAIL_LP") == 1 && wa.flightLoops() == 1;
    const int ex = wa.projectileExploded(cues, 7, "TransContent.TnWeaponRocketLauncher", {0, 0, 30}, 30.0f);
    step(0.5f);
    CHECK(flying && ex >= 0 && live("BL_WPN_GUN_ROCKET.TRAIL_LP") == 0 && wa.flightLoops() == 0,
          "rocket: TRAIL_LP from spawn, EXPL_IMPT_WORLD on Explode, flight loop faded out");
    wa.projectileSpawned(cues, 8, "TransContent.TnWeaponRocketLauncher", {0, 0, 0}, 5.0f);
    wa.projectileRemoved(cues, 8); step(0.5f);
    CHECK(live("BL_WPN_GUN_ROCKET.TRAIL_LP") == 0 && wa.flightLoops() == 0, "projectile destroyed without exploding: flight loop stops, no explosion");
    // beam: WP_Looping + heal loop on a teammate, damage loop on an enemy, tail on release
    const char* RR = "TransContent.TnWeaponRepairRay";
    wa.beam(cues, RR, true, game::WeaponAudio::BeamTarget::Friendly, e); step(0.3f);
    const bool heal = live("BL_WPN_GUN_REPAIR_WPN.SHOOT_LP") == 1 && live("BL_WPN_GUN_REPAIR_WPN.SHOOT_HEAL_LP") == 1 &&
                      live("BL_WPN_GUN_REPAIR_WPN.SHOOT_DMG_LP") == 0;
    for (int k = 0; k < 10; ++k) { wa.beam(cues, RR, true, game::WeaponAudio::BeamTarget::Friendly, e); step(0.05f); }
    const bool once = live("BL_WPN_GUN_REPAIR_WPN.SHOOT_LP") == 1 && live("BL_WPN_GUN_REPAIR_WPN.SHOOT_HEAL_LP") == 1;
    wa.beam(cues, RR, true, game::WeaponAudio::BeamTarget::Enemy, e); step(0.5f);
    const bool dmg = live("BL_WPN_GUN_REPAIR_WPN.SHOOT_HEAL_LP") == 0 && live("BL_WPN_GUN_REPAIR_WPN.SHOOT_DMG_LP") == 1;
    wa.beam(cues, RR, false, game::WeaponAudio::BeamTarget::None, e);
    const bool tail = live("BL_WPN_GUN_REPAIR_WPN.SHOOT_LAST_SHOT") == 1;
    step(0.5f);
    CHECK(heal && once && dmg && tail && live("BL_WPN_GUN_REPAIR_WPN.SHOOT_LP") == 0 && live("BL_WPN_GUN_REPAIR_WPN.SHOOT_DMG_LP") == 0 && !wa.beamActive(),
          "Repair Ray: WP_Looping + heal (teammate) / damage (enemy) loops, no duplicates, LoopingTail on release (heal %d once %d dmg %d tail %d)",
          heal, once, dmg, tail);
    wa.beam(cues, RR, true, game::WeaponAudio::BeamTarget::Enemy, e);
    wa.projectileSpawned(cues, 9, "TransContent.TnWeaponRocketLauncher", {0, 0, 0}, 5.0f);
    wa.stopAll(cues); step(0.1f);
    CHECK(live("BL_WPN_GUN_REPAIR_WPN.SHOOT_LP") == 0 && live("BL_WPN_GUN_ROCKET.TRAIL_LP") == 0 && !wa.beamActive() && wa.flightLoops() == 0,
          "stopAll (death / class change / restart / unload): no beam or flight loop survives");
    host.unload();
}

// M08f: countdown ticks, KOTH match-start hysteresis, grenade fuse / bounce.
static void testCountdownAndGrenades() {
    std::printf("[countdown ticks, KOTH hysteresis, grenade fuse / bounce]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::LevelAudioHost host(cues);
    host.attach(&rec, kRoot);
    host.load("MP_IAC_Streets");
    game::MatchAudio& m = host.match();
    int ticks = 0;
    for (int c = 12; c >= -1; --c) ticks += m.countdownChanged(c, true) ? 1 : 0;
    CHECK(ticks == 11 && !m.countdownChanged(5, false) && cues.activeInstances("BL_HUD_INTERFACE.CTF_ROUND_TIMER_01") > 0,
          "pre-match countdown: CTF_ROUND_TIMER_01 for 10..0 only (%d ticks), none when not counting down", ticks);
    int objTicks = 0;
    for (int c = 8; c >= -1; --c) objTicks += m.objectiveCountdownChanged(c) ? 1 : 0;
    CHECK(objTicks == 6, "objective countdown: EXTINCTION_ROUND_TIMER_01 for 5..0 (%d), none at -1", objTicks);
    m.kothMatchStarting();
    const bool early = m.kothZoneActivated();
    for (int k = 0; k < 100; ++k) { host.tick(1.0f / 30.0f, {0, 0, 0}, {0, 0, 0}); cues.tick(1.0f / 30.0f); }
    CHECK(!early && m.kothZoneActivated(), "KOTH: hill lines ignored for AnnouncerMatchStartHysteresisTime (3 s) from the match start");
    const char* GL = "TransContent.TnWeaponGrenadeLauncher";
    game::CharacterAudio::loadWeaponCues(cues, GL);
    game::WeaponAudio wa;
    wa.projectileHitWall(cues, GL, {0, 0, 0}, 5.0f, true);
    const bool first = cues.activeInstances("BL_WPN_GUN_GRENADE_LAUNCHER.PROJ_IMPT") == 1 &&
                       cues.activeInstances("BL_WPN_GRENADE.GRENADE_FOLEY_SHELL_BOUNCE_HEAVY") == 1;
    wa.projectileHitWall(cues, GL, {0, 0, 1}, 5.0f, false);
    CHECK(first && cues.activeInstances("BL_WPN_GUN_GRENADE_LAUNCHER.PROJ_IMPT") == 1 &&
          cues.activeInstances("BL_WPN_GRENADE.GRENADE_FOLEY_SHELL_BOUNCE_HEAVY") == 2,
          "grenade: first impact FuseSound + BounceSound, later impacts BounceSound only");
    host.unload();
}

// Profile volume sliders: SetAudioGroupVolume -> SoundGroupCategoryMappings categories and their whole subtree.
// Async sim step: the sound groups are read by World's cue table on the worker while the main thread sets sliders and the
// frontend's table reads them; no crash, no torn values, the last write wins.
static void testSoundGroupsThreaded() {
    std::printf("[sound groups: concurrent readers / writers]\n");
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};
    std::atomic<bool> bad{false};
    const char* cats[] = {"SFX_DRY_HUD", "SFX_WET_WEAPONS", "MUSIC", "DIALOG", "SFX_WET_VEHICLES", "SFX_DRY_UI", "MASTER"};
    auto reader = [&] {
        while (!stop.load()) {
            for (const char* c : cats) { const float v = game::SoundMixer::groupScale(c); if (!(v >= 0.0f && v <= 1.0f)) bad = true; }
            ++reads;
        }
    };
    std::thread r1(reader), r2(reader);
    for (int k = 0; k < 20000; ++k) {
        game::SoundMixer::setGroupVolume("SFX", (k % 101) / 100.0f);
        game::SoundMixer::setGroupVolume("MUSIC", ((k * 7) % 101) / 100.0f);
    }
    game::SoundMixer::setGroupVolume("SFX", 0.25f);
    stop = true; r1.join(); r2.join();
    CHECK(!bad && reads > 0 && std::fabs(game::SoundMixer::groupVolume("SFX") - 0.25f) < 1e-6f,
          "2 reader threads x %ld reads during 40000 slider writes: values in range, last write wins", reads.load());
    for (const char* g : {"SFX", "DIALOG", "MUSIC"}) game::SoundMixer::setGroupVolume(g, 1.0f);   // the rest of the suite: authored levels
}

static void testSoundGroups() {
    std::printf("[sound groups]\n");
    using M = game::SoundMixer;
    M::resetGroupVolumes();
    CHECK(M::profileDefaultSlider("MUSIC") == 80 && M::profileDefaultSlider("SFX") == 80 && M::profileDefaultSlider("DIALOG") == 80,
          "TnProfileSettings defaults: Music / FX / Dialogue Volume 80");
    CHECK(near(M::groupVolume("SFX"), 0.8f, 1e-6f) && near(M::groupVolume("Dialog"), 0.8f, 1e-6f) &&
          near(M::groupVolume("MUSIC"), 0.8f, 1e-6f) && M::groupVolume("MASTER") == 1.0f,
          "before any profile: the defaults 80 -> 0.8 (Master has no slider: 1)");
    CHECK(!M::setGroupVolume("VOICE", 0.5f), "unknown group: no-op");
    // the authored tree: a group scales its categories' whole subtree, nothing else
    M::setGroupVolume("SFX", 0.5f); M::setGroupVolume("dialog", 0.25f); M::setGroupVolume("Music", 0.75f);
    CHECK(near(M::groupScale("SFX_SWORD_HUM"), 0.5f, 1e-6f) && near(M::groupScale("SFX_WET_COMBAT_ROBOT_WPN_SHOOT"), 0.5f, 1e-6f) &&
          near(M::groupScale("SFX_DRY_HUD"), 0.5f, 1e-6f) && near(M::groupScale("SFX_WET_AMB_3D"), 0.5f, 1e-6f),
          "SFX reaches SFX_DRY / SFX_WET descendants (sword hum 5 deep, robot weapon shoot, HUD, 3D ambience)");
    CHECK(near(M::groupScale("DX_DRY_RADIO"), 0.25f, 1e-6f) && near(M::groupScale("DX_WET_PA"), 0.25f, 1e-6f) &&
          near(M::groupScale("MUSIC_STINGER"), 0.75f, 1e-6f) && near(M::groupScale("MUSIC_DUCK_SFX"), 0.75f, 1e-6f),
          "Dialog -> DX_DRY / DX_WET subtrees; MUSIC -> MUSIC_DRY subtree (stingers too); names case-insensitive");
    CHECK(M::groupScale("Master") == 1.0f && M::groupScale("") == 1.0f && M::groupScale("NOT_A_CATEGORY") == 1.0f,
          "Master / uncategorized: no slider applies");
    M::setGroupVolume("SFX", 1.7f);
    CHECK(M::groupVolume("SFX") == 1.0f, "clamped to [0,1] (GetNormalizedPropertyValue FClamp)");
    game::LevelAudioHost::applyProfileVolumes(30, 120, -5);
    CHECK(near(M::groupVolume("MUSIC"), 0.3f, 1e-6f) && M::groupVolume("SFX") == 1.0f && M::groupVolume("DIALOG") == 0.0f,
          "applyProfileVolumes(music, fx, dialog) = SetAudioGroupVolume(slider / 100, clamped)");

    // A playing voice follows at once (gain re-evaluated every tick); the mixer preset volume still multiplies.
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    const char* js = R"({
      "T.SFXLOOP": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": 0.0, "SpatializationType": "k2D", "Category": "SFX_WET_VEH_ENGINE"},
        "children": [{"class": "SoundNodeWaveEvent", "params": {"Volume": 0.0, "Loop": true}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]}]}},
      "T.DX": {"tree": {"class": "SoundNodeRoot", "params": {"Volume": 0.0, "SpatializationType": "k2D", "Category": "DX_DRY_RADIO"},
        "children": [{"class": "SoundNodeWaveEvent", "params": {"Volume": 0.0}, "children": [{"wav": "content/WL_ELEC/ELEC_TRANS_TV_03.wav"}]}]}}})";
    assets::Json cj; assets::Json::parse(js, cj);
    cues.addCues(cj, kRoot + "/../content/");
    M::setGroupVolume("SFX", 0.8f); M::setGroupVolume("DIALOG", 0.5f);
    int f = rec.n;
    const int id = cues.play("T.SFXLOOP", Vec3{0, 0, 0}, 0.0f);
    cues.play("T.DX", Vec3{0, 0, 0}, 0.0f);
    CHECK(rec.n - f == 2 && near(rec.v[f].p.volume, 0.8f, 1e-5f) && near(rec.v[f + 1].p.volume, 0.5f, 1e-5f),
          "new voices: authored level x group volume (SFX 0.8, Dialog 0.5) (%.4f, %.4f)", rec.v[f].p.volume, rec.v[f + 1].p.volume);
    M::setGroupVolume("SFX", 0.4f);
    cues.tick(1.0f / 30.0f);
    CHECK(cues.playing(id) && near(rec.v[f].vol, 0.4f, 1e-5f), "slider change: the playing loop follows at once (%.4f)", rec.v[f].vol);
    cues.mixer().enable("VEHICLE_JUMP");
    for (int k = 0; k < 15; ++k) cues.tick(1.0f / 30.0f);     // fade-in 0.3 s, duration 1 s: sample at 0.5 s
    const float jump = cues.mixer().categoryVolume("SFX_WET_VEH_ENGINE");
    CHECK(jump < 1.0f && near(rec.v[f].vol, 0.4f * jump, 1e-4f), "mixer preset (VEHICLE_JUMP %.3f) x group volume (%.4f)", jump, rec.v[f].vol);
    cues.stopAll();

    // Movies: GetMovieVolume's SFX class volume is the same group value; a running movie follows.
    M::resetGroupVolumes();
    for (const char* g : {"SFX", "DIALOG", "MUSIC"}) M::setGroupVolume(g, 1.0f);   // the rest of the suite: authored levels
}

// Ability / buff sounds (RE pass 5 s12): OnTriggerSound on success; buff Apply loop / Unapply; CanPlaySounds; cloak team cues.
static void testAbilityAudio() {
    std::printf("[ability / buff sounds]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    const int added = game::CharacterAudio::loadAbilityCues(cues);
    CHECK(added >= 29 && game::CharacterAudio::loadAbilityCues(cues) == 0, "ability / buff / class cues load once (%d)", added);
    game::AbilityAudio aa;
    const game::SoundCues::Emitter at{Vec3{0, 0, 0}, game::SoundCues::kWorld, {0, 0, 0}, ""};
    auto active = [&](const char* q) { return cues.activeInstances(q); };
    // OnTriggerSound by class; abilities without one play nothing (their audio is anim notifies / buffs / spawned actors).
    CHECK(aa.abilityTriggered(cues, "Barrier", at, 5.0f) >= 0 && active("BL_TRANS_POWER.BARRIER_DEPLOY") == 1,
          "Barrier trigger: OnTriggerSound BARRIER_DEPLOY at the pawn");
    CHECK(aa.abilityTriggered(cues, "TnAbilitySpawnAmmoCrate", at, 5.0f) >= 0 && active("BL_TRANS_POWER.AMMO_DEPLOY") == 1 &&
          aa.abilityTriggered(cues, "Drain", at, 5.0f) >= 0 && active("BL_TRANS_POWER.DRAIN_INITIATE") == 1,
          "SpawnAmmoCrate / Drain triggers: AMMO_DEPLOY / DRAIN_INITIATE");
    CHECK(aa.abilityTriggered(cues, "Cloaking", at, 5.0f) < 0 && aa.abilityTriggered(cues, "Hover", at, 5.0f) < 0 &&
          aa.abilityTriggered(cues, "Dodge", at, 5.0f) < 0, "Cloaking / Hover / Dodge author no OnTriggerSound: nothing");
    cues.stopAll();

    // Cloak (OnlyPlaySoundOnLocalPlayer False): heard for any pawn, the BUFFED pawn's team picks the cue; a loop until unapply.
    game::AbilityAudio::Owner me; me.key = 1; me.local = true; me.team = 0; me.at = at;
    game::AbilityAudio::Owner foe; foe.key = 2; foe.local = false; foe.team = 1; foe.at = at;
    CHECK(aa.setBuff(cues, "TnBuffCloak", me, true) && active("BL_INTRFC_TECH_TREE.CLOAK_AUTOBOT_START_LP") == 1,
          "local Autobot cloak: CLOAK_AUTOBOT_START_LP");
    CHECK(aa.setBuff(cues, "TnBuffCloak", foe, true) && active("BL_INTRFC_TECH_TREE.CLOAK_DECEPTICON_START_LP") == 1,
          "a remote Decepticon's cloak is heard too (OnlyPlaySoundOnLocalPlayer False), Decepticon cue by the buffed pawn's team");
    CHECK(!aa.setBuff(cues, "TnBuffCloak", me, true) && active("BL_INTRFC_TECH_TREE.CLOAK_AUTOBOT_START_LP") == 1,
          "re-apply while playing: no restart, no second instance");
    for (int k = 0; k < 90; ++k) cues.tick(1.0f / 30.0f);
    CHECK(active("BL_INTRFC_TECH_TREE.CLOAK_AUTOBOT_START_LP") == 1 && aa.liveLoops(cues) == 2, "3 s later both cloak loops still play");
    aa.setBuff(cues, "TnBuffCloak", me, false);
    cues.tick(1.0f / 30.0f);
    CHECK(active("BL_INTRFC_TECH_TREE.CLOAK_AUTOBOT_START_LP") == 0 && active("BL_INTRFC_TECH_TREE.CLOAK_AUTOBOT_OFF") == 1,
          "unapply: the loop stops, CLOAK_AUTOBOT_OFF plays");

    // Default buffs (OnlyPlaySoundOnLocalPlayer True): only the buffed local player hears them.
    CHECK(!aa.setBuff(cues, "TnBuffWarcryBase", foe, true) && active("BL_TRANS_POWER.WAR_CRY_STATE_START") == 0,
          "a remote pawn's Warcry buff: silent here");
    CHECK(aa.setBuff(cues, "TnBuffWarcry1", me, true) && active("BL_TRANS_POWER.WAR_CRY_STATE_START") == 1,
          "the local player's Warcry1 (inherits WarcryBase): WAR_CRY_STATE_START");
    aa.setBuff(cues, "TnBuffWarcry1", me, false);
    CHECK(active("BL_TRANS_POWER.WAR_CRY_STATE_STOP") == 1, "Warcry ends: WAR_CRY_STATE_STOP");
    // Death: the loop stops with no Unapply sound.
    CHECK(aa.setBuff(cues, "TnBuffFlashBangRobot", me, true) && active("BL_WPN_GRENADE.EMP_STATIC_DISCHARGE_VICTIM_LP") == 1,
          "EMP victim (local): EMP_STATIC_DISCHARGE_VICTIM_LP");
    aa.pawnDied(cues, me.key);
    aa.setBuff(cues, "TnBuffFlashBangRobot", me, false);
    cues.tick(1.0f / 30.0f);
    CHECK(active("BL_WPN_GRENADE.EMP_STATIC_DISCHARGE_VICTIM_LP") == 0 && active("BL_WPN_GRENADE.EMP_STATIC_DISCHARGE_VICTIM_STOP") == 0,
          "owner death: the loop stops, no _STOP (Unapply) sound");

    // Drain: HealSound per tick on the drainer's own machine while it has targets; DamageSound per tick at each victim.
    const int h0 = rec.n;
    CHECK(aa.drainSourceTick(cues, at, 0.0f, true, 1) >= 0 && aa.drainSourceTick(cues, at, 0.0f, true, 0) < 0 &&
          aa.drainSourceTick(cues, at, 0.0f, false, 2) < 0, "DRAIN_HEAL: local drainer with >= 1 target only");
    CHECK(aa.drainTargetTick(cues, Vec3{3, 0, 0}, 3.0f) >= 0 && rec.n > h0, "DRAIN_DAMAGE at the victim each tick");
    for (int k = 0; k < 30; ++k) { aa.drainSourceTick(cues, at, 0.0f, true, 1); cues.tick(1.0f / 30.0f); }
    CHECK(active("BL_TRANS_POWER.DRAIN_HEAL") <= 6, "per-tick DRAIN_HEAL stays within MaxConcurrentPlayCount 6 (%d live)",
          active("BL_TRANS_POWER.DRAIN_HEAL"));
    // Abilities blocked (jammed): AbilitiesJammedSound.
    CHECK(aa.abilitiesJammed(cues, at) >= 0 && active("BL_TRANS_POWER.ENERGON_SLING_ABILITYFAILURE") == 1,
          "jammed ability press: ENERGON_SLING_ABILITYFAILURE");
    // Hover: JumpingToHover starts the loop, it carries into Hovering, leaving Hovering fades it (0.5 s) + HOVER_JUMP_LAND.
    aa.hoverState(cues, 1, at, 0.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 1, "JumpingToHover: HOVER_JUMP_LIFT loop starts");
    for (int k = 0; k < 30; ++k) cues.tick(1.0f / 30.0f);
    aa.hoverState(cues, 2, at, 0.0f);
    for (int k = 0; k < 60; ++k) cues.tick(1.0f / 30.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 1 && active("BL_TRANS_POWER.HOVER_JUMP_LAND") == 0, "into Hovering: the loop carries on, no land yet");
    aa.hoverState(cues, 0, at, 0.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LAND") == 1, "Hovering ends: HOVER_JUMP_LAND");
    for (int k = 0; k < 20; ++k) cues.tick(1.0f / 30.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 0, "the lift loop faded out (0.5 s)");
    aa.hoverState(cues, 1, at, 0.0f); aa.hoverState(cues, 0, at, 0.0f);
    cues.tick(1.0f / 30.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LAND") <= 1, "JumpingToHover aborted (not via Hovering): fade only, no extra land");
    for (int k = 0; k < 20; ++k) cues.tick(1.0f / 30.0f);          // the aborted loop's 0.5 s fade completes
    aa.hoverState(cues, 1, at, 0.0f);
    aa.pawnDied(cues, 0);
    cues.tick(1.0f / 30.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 0, "death while hovering: the loop stops, no land");
    // Kill confirm (the killer, 2D): headshot > victim robot form > SoldierJet > SoldierCar > other vehicle (by character class).
    const struct { bool hs, robot; const char* chassis; const char* cue; } kc[] = {
        {true, true, "Car2", "BL_HUD_INTERFACE.KilledWithHeadshotSound"}, {false, true, "Jet4", "BL_HUD_INTERFACE.KilledRobotSound"},
        {false, false, "Jet4", "BL_HUD_INTERFACE.KilledJetSound"}, {false, false, "Car2", "BL_HUD_INTERFACE.KilledCarSound"},
        {false, false, "Tank3", "BL_HUD_INTERFACE.KilledVehicleSound"}, {false, false, "Truck5", "BL_HUD_INTERFACE.KilledVehicleSound"}};
    bool kcOk = true;
    for (const auto& k : kc) {
        const int before = active(k.cue);
        const int id = aa.killedPawn(cues, k.hs, k.robot, k.chassis);
        kcOk = kcOk && id >= 0 && active(k.cue) >= 1 && active(k.cue) <= before + 1;   // instance limits may replace
        if (id < 0 || active(k.cue) < 1) std::printf("  kill confirm %s / %d / %s: expected %s\n", k.hs ? "hs" : "-", k.robot, k.chassis, k.cue);
    }
    CHECK(kcOk, "kill confirm: headshot > robot > Jet > Car > Vehicle (tank / truck) by the victim's character class");
    CHECK(aa.transformFailed(cues, at) >= 0 && active("BL_TRANS_POWER.TRANSFORM_DISABLED") == 1, "refused transform: TRANSFORM_DISABLED");
    aa.stopAll(cues);
    cues.tick(1.0f / 30.0f);
    CHECK(aa.liveLoops(cues) == 0 && active("BL_INTRFC_TECH_TREE.CLOAK_DECEPTICON_START_LP") == 0, "stopAll: every buff loop stops");
}

// Level-start warming (Frontend: the title's level start decoded ~55 ms of waves on the menu's first frame).
static void testLevelWarm() {
    std::printf("[level-start warming]\n");
    IAudio* a = createAudio();
    if (!a || !a->reportsVoices()) { std::printf("  SKIP backend: no audio device\n"); return; }
    {
        game::SoundCues cues; cues.load(a, kRoot + "/../content/");
        game::LevelAudioHost host(cues);
        host.attach(a, kRoot);
        const size_t base = a->residentBytes();
        auto ms = [](auto t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
        auto p0 = std::chrono::steady_clock::now();
        CHECK(host.prefetch("UI_FrontEnd_m"), "prefetch UI_FrontEnd_m (level waves + music)");
        const double pre = ms(p0);
        for (int k = 0; k < 60 && cues.warmLevels() > 0; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto l0 = std::chrono::steady_clock::now();
        CHECK(host.load("UI_FrontEnd_m"), "load UI_FrontEnd_m");
        const double load = ms(l0);
        CHECK(pre < 5.0 && load < 15.0, "prefetch call %.2f ms; warmed level load %.2f ms (cold ~55 ms of decode)", pre, load);
        host.unload();
        for (int k = 0; k < 3; ++k) { host.tick(1.0f / 30.0f, Vec3{0, 0, 0}, Vec3{0, 0, 0}); cues.tick(1.0f / 30.0f); }
        // A prefetched level that never loads: the next level's load releases its warm samples and unpins its music.
        host.prefetch("UI_Lobby_m");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        host.load("UI_PartyLobby_m");
        host.unload();
        for (int k = 0; k < 3; ++k) { host.tick(1.0f / 30.0f, Vec3{0, 0, 0}, Vec3{0, 0, 0}); cues.tick(1.0f / 30.0f); }
        CHECK(a->residentBytes() == base && cues.warmLevels() == 0, "abandoned UI_Lobby_m prefetch: nothing left resident (%.1f MB over base)",
              (a->residentBytes() - base) / 1048576.0);
        // The 08o freeze scenario: a large streamed cue starts its worker decode, then the level unloads at once (match end ->
        // travel). The unload must not wait for the decode; the orphaned decode is released when it finishes.
        CHECK(host.load("UI_FrontEnd_m"), "load UI_FrontEnd_m again");
        game::MusicTrack mt;
        CHECK(game::AmbientAudio::levelMusicTrack("UI_FrontEnd_m", mt) && cues.play(mt.cue.c_str(), Vec3{0, 0, 0}, 0.0f) >= 0 &&
              cues.waitingInstances() == 1, "unprefetched title music: worker decode started, the instance waits");
        auto u0 = std::chrono::steady_clock::now();
        host.unload();
        const double unloadMs = ms(u0);
        CHECK(unloadMs < 50.0 && cues.orphanDecodes() == 1, "level unload during that decode returns at once (%.1f ms; was a wait on the decode), decode orphaned",
              unloadMs);
        double worst = 0.0;
        for (int k = 0; k < 600 && cues.orphanDecodes() > 0; ++k) {
            auto t = std::chrono::steady_clock::now();
            host.tick(1.0f / 30.0f, Vec3{0, 0, 0}, Vec3{0, 0, 0}); cues.tick(1.0f / 30.0f);
            worst = std::max(worst, ms(t));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(cues.orphanDecodes() == 0 && a->residentBytes() == base && worst < 20.0,
              "the orphaned decode finishes and is released (worst tick %.1f ms; %.1f MB over base)", worst, (a->residentBytes() - base) / 1048576.0);
    }
    delete a;
}

// TnChargeWeapon (Plasma Cannon) charge loops / shot level / fizzle, and the roller mine timeline (M08k).
static void testChargeAndRoller() {
    std::printf("[charge weapon, roller mine]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    const std::string pc = "TransContent.TnWeaponPlasmaCannon", rr = "TransContent.TnWeaponRepairRay";
    game::CharacterAudio::loadWeaponCues(cues, pc);
    game::CharacterAudio::loadWeaponCues(cues, rr);
    game::CharacterAudio::loadAbilityCues(cues);
    game::WeaponAudio wa;
    const game::SoundCues::Emitter at{Vec3{0, 0, 0}, game::SoundCues::kWorld, {0, 0, 0}, ""};
    auto active = [&](const char* q) { return cues.activeInstances(q); };
    auto settle = [&](float s) { for (float t = 0; t < s; t += 1.0f / 30.0f) cues.tick(1.0f / 30.0f); };
    // Idle -> idle on another weapon (the per-tick glue for a non-charge weapon): nothing, not the Repair Ray's LoopingTail.
    const int n0 = rec.n;
    wa.chargeState(cues, rr, 0, at); wa.chargeState(cues, pc, 0, at); wa.chargeState(cues, rr, 0, at);
    CHECK(rec.n == n0, "idle on any weapon plays nothing (%d voices)", rec.n - n0);
    wa.chargeState(cues, pc, 1, at);
    CHECK(active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_SHOT") == 1, "-> charging: WP_Looping CHARGE_SHOT");
    wa.chargeState(cues, pc, 2, at);
    CHECK(wa.chargeLoops(cues) == 1, "-> level 1: no sound change");
    wa.chargeState(cues, pc, 3, at); settle(0.4f);
    CHECK(active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_SHOT") == 0 && active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_LP_02") == 1,
          "-> level 2: stop 9 (0.25 s fade), play 10 CHARGE_LP_02");
    wa.chargeState(cues, pc, 4, at); settle(0.4f);
    CHECK(active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_LP_02") == 0 && active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_LP_03") == 1,
          "-> level 3: stop 10, play 11 CHARGE_LP_03");
    wa.chargeState(cues, pc, 0, at); settle(0.4f);
    CHECK(wa.chargeLoops(cues) == 0, "-> idle: every charge loop stops (no LoopingTail authored)");
    const struct { int mode; const char* cue; } lv[] = {{0, "BL_WPN_GUN_PLASMA_CANNON.SHOOT_CHARGE_SHOT"},
        {1, "BL_WPN_GUN_PLASMA_CANNON.SHOOT_CHARGE_SHOT_02"}, {2, "BL_WPN_GUN_PLASMA_CANNON.SHOOT_CHARGE_SHOT_03"}};
    bool lvOk = true;
    for (const auto& l : lv) lvOk = lvOk && wa.fire(cues, pc, false, at, 0.0f, l.mode) >= 0 && active(l.cue) >= 1;
    CHECK(lvOk, "FireCharge mode 0 / 1 / 2: SHOOT_CHARGE_SHOT / _02 / _03");
    CHECK(wa.chargeFizzle(cues, pc, at) >= 0 && active("BL_WPN_FOLEY.SHOOT_DRY_FIRE_PLASMA_01") == 1,
          "released before level 1: WP_NoAmmoFire (22) SHOOT_DRY_FIRE_PLASMA_01");
    wa.chargeState(cues, pc, 1, at); wa.stopAll(cues); settle(0.1f);
    CHECK(wa.chargeLoops(cues) == 0 && active("BL_WPN_GUN_PLASMA_CANNON.CHARGE_SHOT") == 0, "stopAll stops a charge");
    cues.stopAll();

    // Roller mine: loop from spawn, ArmSound at 3 s, buildup at 8.5 s, explosion stops the loop.
    game::AbilityAudio aa;
    const Vec3 mp{10, 0, 0};
    aa.rollerMine(cues, true, 0.0f, mp, 10.0f);
    CHECK(active("BL_WPN_MINE.ROLLER_MINE_LP") == 1, "spawn: ROLLER_MINE_LP");
    aa.rollerMine(cues, true, 2.9f, mp, 10.0f); aa.rollerMine(cues, true, 3.05f, mp, 10.0f); aa.rollerMine(cues, true, 3.2f, mp, 10.0f);
    CHECK(active("BL_WPN_GRENADE.KAMIKAZE_FUSE_START") == 1, "armed at 3 s: KAMIKAZE_FUSE_START once");
    aa.rollerMine(cues, true, 8.6f, mp, 10.0f); aa.rollerMine(cues, true, 9.0f, mp, 10.0f);
    CHECK(active("BL_WPN_MINE.ROLLER_MINE_FUSE_BUILD") == 1, "fuse <= 1.5 s: ROLLER_MINE_FUSE_BUILD once");
    aa.rollerMineExploded(cues, mp, 10.0f); aa.rollerMine(cues, false, 0.0f, mp, 10.0f); settle(0.1f);
    CHECK(active("BL_WPN_MINE.ROLLER_MINE_LP") == 0 && active("BL_WPN_MINE.ROLLER_MINE_EXPL") == 1, "destroyed: loop stops, ROLLER_MINE_EXPL");
    aa.rollerMine(cues, true, 0.0f, mp, 10.0f); aa.rollerMine(cues, false, 0.0f, mp, 10.0f); settle(0.1f);
    CHECK(active("BL_WPN_MINE.ROLLER_MINE_LP") == 0 && active("BL_WPN_MINE.ROLLER_MINE_EXPL") <= 1,
          "removed without exploding (owner death / kill-Z): loop stops, nothing else");
}

// Guided missile, barrier, sentry (M08m; RE pass 5 s12 addenda 3 / 4).
static void testAbilityActors() {
    std::printf("[guided missile, barrier, sentry]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::CharacterAudio::loadAbilityCues(cues);
    game::AbilityAudio aa;
    auto active = [&](const char* q) { return cues.activeInstances(q); };
    auto settle = [&](float s) { for (float t = 0; t < s; t += 1.0f / 30.0f) cues.tick(1.0f / 30.0f); };
    const Vec3 p{5, 0, 0};
    aa.guidedMissile(cues, true, p, 5.0f);
    CHECK(active("BL_WPN_GUN_GUIDED_MISSILE.SHOOT_TRAIL") == 1, "missile launched: FlightSound SHOOT_TRAIL");
    aa.guidedMissileExploded(cues, p, 5.0f); aa.guidedMissile(cues, false, p, 5.0f); settle(0.4f);
    CHECK(active("BL_WPN_GUN_GUIDED_MISSILE.SHOOT_TRAIL") == 0 && active("BL_WPN_GUN_GUIDED_MISSILE.EXPL_IMPT_WORLD") == 1,
          "detonated: flight faded (0.25 s), ExplosionSound");
    // Barrier: loop from spawn; retract once at health 0; the loop stops with the actor; a silent removal plays nothing.
    aa.barrier(cues, true, false, p, 5.0f);
    CHECK(active("BL_TRANS_POWER.BARRIER_LP") == 1, "barrier up: BARRIER_LP");
    aa.barrier(cues, true, true, p, 5.0f); aa.barrier(cues, true, true, p, 5.0f);
    CHECK(active("BL_TRANS_POWER.BARRIER_RETRACT") == 1 && active("BL_TRANS_POWER.BARRIER_LP") == 1, "health 0: BARRIER_RETRACT once, loop until gone");
    aa.barrier(cues, false, false, p, 5.0f); settle(0.1f);
    CHECK(active("BL_TRANS_POWER.BARRIER_LP") == 0, "fade over: the loop stops");
    const int retract0 = active("BL_TRANS_POWER.BARRIER_RETRACT");
    aa.barrier(cues, true, false, p, 5.0f); aa.barrier(cues, false, false, p, 5.0f); settle(0.1f);
    CHECK(active("BL_TRANS_POWER.BARRIER_LP") == 0 && active("BL_TRANS_POWER.BARRIER_RETRACT") <= retract0,
          "re-cast / owner death removal: the loop stops, no retract");
    // Sentry: idle loop, POSTDEPLOY on every EnemyAcquired, shots + world impacts, destroyed -> fade + SENTRY_EXPL.
    aa.sentry(cues, true, -1, p, 5.0f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 1, "sentry deployed: SENTRY_ACTIVATE_LP");
    aa.sentry(cues, true, 3, p, 5.0f); aa.sentry(cues, true, 3, p, 5.0f); aa.sentry(cues, true, -1, p, 5.0f); aa.sentry(cues, true, 4, p, 5.0f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_POSTDEPLOY") == 2, "EnemyAcquired twice (target 3, lost, target 4): POSTDEPLOY x2 (%d)",
          active("BL_TRANS_POWER.SENTRY_ACTIVATE_POSTDEPLOY"));
    aa.sentryShot(cues, p, 5.0f, true, Vec3{20, 0, 0}, 20.0f); aa.sentryShot(cues, p, 5.0f, false, Vec3{20, 0, 0}, 20.0f);
    CHECK(active("BL_TRANS_POWER.SENTRY_SHOOT") >= 1 && active("BL_TRANS_POWER.SENTRY_IMPT") == 1, "shots: SENTRY_SHOOT each, SENTRY_IMPT on the world hit only");
    aa.sentry(cues, false, -1, p, 5.0f); settle(0.4f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 0 && active("BL_TRANS_POWER.SENTRY_EXPL") == 1, "destroyed: loop fades, SENTRY_EXPL");
    // Overshield off at 0 (depleted or expired), not while it is 0; dodge wall hit.
    const game::SoundCues::Emitter at{p, game::SoundCues::kWorld, {0, 0, 0}, ""};
    aa.overshield(cues, 0.0f, at); aa.overshield(cues, 300.0f, at); aa.overshield(cues, 120.0f, at);
    CHECK(active("BL_HUD_INTERFACE.OVERSHIELD_POWER_DOWN") == 0, "overshield up / draining: no power-down yet");
    aa.overshield(cues, 0.0f, at); aa.overshield(cues, 0.0f, at);
    CHECK(active("BL_HUD_INTERFACE.OVERSHIELD_POWER_DOWN") == 1, "overshield reaches 0: OVERSHIELD_POWER_DOWN once");
    CHECK(aa.dodgeHitWall(cues, at) >= 0 && active("BL_MELEE_IMPT.MTL_DASH_WALL_IMPT") == 1, "dodge into a wall: MTL_DASH_WALL_IMPT");
    aa.sentry(cues, true, -1, p, 5.0f); aa.barrier(cues, true, false, p, 5.0f); aa.guidedMissile(cues, true, p, 5.0f);
    aa.stopAll(cues); settle(0.1f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 0 && active("BL_TRANS_POWER.BARRIER_LP") == 0 &&
          active("BL_WPN_GUN_GUIDED_MISSILE.SHOOT_TRAIL") == 0, "stopAll: sentry / barrier / missile loops stop");
    // [M09h] Per owner: the local player's (key 0) and two bots' (1001, 1002) sentries / barriers are independent; an
    // instance no longer reported is swept silently; the others keep playing.
    const Vec3 q{8, 0, 0};
    auto frame = [&](bool bot2) {
        aa.markActorsUnseen();
        aa.sentry(cues, 0, true, -1, p, 5.0f); aa.sentry(cues, 1001, true, -1, q, 8.0f);
        aa.barrier(cues, 1001, true, false, q, 8.0f);
        if (bot2) aa.sentry(cues, 1002, true, -1, q, 8.0f);
        return aa.sweepUnseenActors(cues);
    };
    settle(12.0f);                                          // earlier one-shots (SENTRY_EXPL is long) finish
    frame(true);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 3 && active("BL_TRANS_POWER.BARRIER_LP") == 1 && aa.liveActors() == 4,
          "three owners' sentries loop at once, a bot's barrier too (%d sentries, %d actors)",
          active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP"), aa.liveActors());
    const int post0 = active("BL_TRANS_POWER.SENTRY_ACTIVATE_POSTDEPLOY");
    aa.sentry(cues, 1001, true, 7, q, 8.0f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_POSTDEPLOY") == post0 + 1, "one bot's sentry acquires an enemy: POSTDEPLOY for it only (%d -> %d)",
          post0, active("BL_TRANS_POWER.SENTRY_ACTIVATE_POSTDEPLOY"));
    const int expl0 = active("BL_TRANS_POWER.SENTRY_EXPL");
    const int swept = frame(false); settle(0.1f);
    CHECK(swept == 1 && active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 2 && active("BL_TRANS_POWER.SENTRY_EXPL") == expl0,
          "bot 2's sentry no longer reported: its loop stops silently, the others play on (swept %d)", swept);
    aa.markActorsUnseen(); aa.sentry(cues, 1001, false, -1, q, 8.0f); aa.sentry(cues, 0, true, -1, p, 5.0f);
    aa.barrier(cues, 1001, true, false, q, 8.0f); aa.sweepUnseenActors(cues); settle(0.4f);
    CHECK(active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP") == 1 && active("BL_TRANS_POWER.SENTRY_EXPL") == expl0 + 1,
          "bot 1's sentry destroyed: its SENTRY_EXPL; the local sentry plays on (LP %d, EXPL %d -> %d)",
          active("BL_TRANS_POWER.SENTRY_ACTIVATE_LP"), expl0, active("BL_TRANS_POWER.SENTRY_EXPL"));
    aa.stopAll(cues);
}

// Death sounds and grenade toss (M08o; RE pass 5 s12 addenda 12 / 13).
static void testDeathAndGrenade() {
    std::printf("[death sounds, grenade toss]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::CharacterAudio::loadAbilityCues(cues);
    game::AbilityAudio aa;
    auto active = [&](const std::string& q) { return cues.activeInstances(q.c_str()); };
    const Vec3 p{8, 0, 0};
    const game::CharacterAudioProfile* car = game::CharacterAudio::find("Car2");
    CHECK(car && !car->vehicleDeath.empty(), "Car2 authors a vehicle death sound (%s)", car ? car->vehicleDeath.c_str() : "-");
    CHECK(car && aa.pawnDeath(cues, "Car2", true, "TransGame.TnDamageTypeIonBlaster", p, 8.0f) >= 0 && active(car->vehicleDeath) == 1,
          "vehicle-form death: the chassis' _Blueprint.DeathSound at the wreck, whatever the damage type");
    const std::string melee = game::CharacterAudio::classSound("TnDeathTypeMelee", "DeathSound");
    CHECK(melee == "BL_LVL_HUD_INTERFACE.hud_melee_death_disintegrate", "the melee death entry's sound (%s)", melee.c_str());
    CHECK(aa.pawnDeath(cues, "Car2", false, "TransGame.TnDamageTypeMelee", p, 8.0f) >= 0 && active(melee) == 1,
          "robot melee death (DamageDeathType TnDeathTypeMelee): hud_melee_death_disintegrate");
    CHECK(game::CharacterAudio::isMeleeDamageType("TransGame.TnDamageTypeWhirlwind") && game::CharacterAudio::isMeleeDamageType("TransGame.TnDamageTypeRammed") &&
          !game::CharacterAudio::isMeleeDamageType("TransGame.TnDamageTypePoke"), "Whirlwind / Rammed inherit TnDeathTypeMelee; Poke overrides it (TnDeathTypePoke)");
    CHECK(aa.pawnDeath(cues, "Car2", false, "TransGame.TnDamageTypePoke", p, 8.0f) < 0 &&
          aa.pawnDeath(cues, "Car2", false, "TransGame.TnDamageTypeIonBlaster", p, 8.0f) < 0, "robot non-melee death (Poke, Ion Blaster): no sound");
    game::WeaponAudio wa;
    const std::string fb = "TransContent.TnWeaponFlashBangs";
    game::CharacterAudio::loadWeaponCues(cues, fb);
    const game::SoundCues::Emitter at{p, game::SoundCues::kWorld, {0, 0, 0}, ""};
    CHECK(wa.weaponEvent(cues, fb, "WP_Fire", at) >= 0 && active("BL_WPN_GRENADE.EMP_DEPLOY") == 1, "grenade toss: WP_Fire EMP_DEPLOY");
    CHECK(wa.weaponEvent(cues, fb, "WP_NoAmmoFire", at) >= 0 && active("BL_WPN_GRENADE.GRENADE_DRY_FIRE") == 1, "refused toss: WP_NoAmmoFire GRENADE_DRY_FIRE");
}

// Melee hit effects on the victim and kamikaze mines (M08p).
static void testMeleeHitAndMines() {
    std::printf("[melee hit effect, kamikaze mines]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::CharacterAudio::loadAbilityCues(cues);
    game::AbilityAudio aa;
    auto active = [&](const char* q) { return cues.activeInstances(q); };
    auto settle = [&](float s) { for (float t = 0; t < s; t += 1.0f / 30.0f) cues.tick(1.0f / 30.0f); };
    const Vec3 p{6, 0, 0};
    CHECK(aa.pawnHitEffect(cues, "TransGame.TnDamageTypeMelee", "Jet", 7, p, 6.0f, 1.0f) >= 0 && active("BL_MELEE_IMPT.MTL_HV") == 1,
          "melee hit: IMPT_DMG_MELEE_HV of the victim's set (Jet: MTL_HV)");
    CHECK(aa.pawnHitEffect(cues, "TransGame.TnDamageTypeMelee", "Jet", 7, p, 6.0f, 1.05f) < 0 &&
          aa.pawnHitEffect(cues, "TransGame.TnDamageTypeMelee", "Jet", 8, p, 6.0f, 1.05f) >= 0 &&
          aa.pawnHitEffect(cues, "TransGame.TnDamageTypeMelee", "Jet", 7, p, 6.0f, 1.2f) >= 0,
          "RetriggerTime 0.1 s per victim per entry (another victim is independent)");
    CHECK(aa.pawnHitEffect(cues, "TransGame.TnDamageTypeWeakMelee", "Truck", 9, p, 6.0f, 2.0f) >= 0 && active("BL_MELEE_IMPT.MTL_LT") >= 1,
          "weak melee: IMPT_DMG_MELEE_LT (MTL_LT)");
    CHECK(aa.pawnHitEffect(cues, "TransGame.TnDamageTypeNoSuchThing", "Jet", 7, p, 6.0f, 3.0f) < 0, "a damage type without a hit-effect entry: nothing");
    settle(3.0f);
    // Kamikaze mine: idle loop -> target found (fuse start, cross-fade to tracking) -> explosion; a second one fizzles.
    aa.kamikazeMine(cues, 1, p, false, 6.0f);
    CHECK(active("BL_WPN_GRENADE.KAMIKAZE_FLIGHT_LP_IDLE") == 1, "mine thrown: KAMIKAZE_FLIGHT_LP_IDLE");
    aa.kamikazeMine(cues, 1, p, true, 6.0f); aa.kamikazeMine(cues, 1, p, false, 6.0f); settle(0.4f);
    CHECK(active("BL_WPN_GRENADE.KAMIKAZE_FUSE_START") == 1 && active("BL_WPN_GRENADE.KAMIKAZE_FLIGHT_LP_TRACKING") == 1 &&
          active("BL_WPN_GRENADE.KAMIKAZE_FLIGHT_LP_IDLE") == 0, "target found once: FUSE_START, idle faded, tracking loop (stays after losing it)");
    aa.kamikazeMineExploded(cues, 1, p, 6.0f); settle(0.4f);
    CHECK(active("BL_WPN_GRENADE.KAMIKAZE_FLIGHT_LP_TRACKING") == 0 && active("BL_WPN_GRENADE.KAMIKAZE_EXPL_IMPT_WORLD") == 1, "exploded: loop fades, KAMIKAZE_EXPL");
    aa.kamikazeMine(cues, 2, p, false, 6.0f); aa.kamikazeMineRemoved(cues, 2); settle(0.1f);
    CHECK(active("BL_WPN_GRENADE.KAMIKAZE_FLIGHT_LP_IDLE") == 0 && active("BL_WPN_GRENADE.KAMIKAZE_EXPL_IMPT_WORLD") <= 1 && aa.kamikazeMines() == 0,
          "fizzle (LifeSpan / owner death): the loop stops, no explosion");
}

// Participant (bot) cloak / hover loops (M09e): per-pawn keys, idempotent, gone = silent stop.
static void testParticipantLoops() {
    std::printf("[participant cloak / hover loops]\n");
    Rec rec; game::SoundCues cues; cues.load(&rec, kRoot + "/../content/");
    game::CharacterAudio::loadAbilityCues(cues);
    game::AbilityAudio aa;
    const game::SoundCues::Emitter at{Vec3{0, 0, 0}, game::SoundCues::kWorld, {0, 0, 0}, ""};
    auto active = [&](const char* q) { return cues.activeInstances(q); };
    aa.hoverState(cues, 1001, 1, at, 0.0f); aa.hoverState(cues, 1002, 1, at, 0.0f); aa.hoverState(cues, 1001, 1, at, 0.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 2, "two participants hovering: two lift loops (idempotent per tick)");
    aa.pawnDied(cues, 1001); cues.tick(1.0f / 30.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 1 && active("BL_TRANS_POWER.HOVER_JUMP_LAND") == 0,
          "one participant gone: only its loop stops, no land sound");
    aa.hoverState(cues, 1002, 2, at, 0.0f); aa.hoverState(cues, 1002, 0, at, 0.0f);
    CHECK(active("BL_TRANS_POWER.HOVER_JUMP_LAND") == 1, "the other lands normally (Hovering end -> HOVER_JUMP_LAND)");
    game::AbilityAudio::Owner bot; bot.key = 1003; bot.local = false; bot.team = 1; bot.at = at;
    CHECK(aa.setBuff(cues, "TnBuffCloak", bot, true) && active("BL_INTRFC_TECH_TREE.CLOAK_DECEPTICON_START_LP") == 1 &&
          !aa.setBuff(cues, "TnBuffCloak", bot, true), "a Decepticon bot's cloak: heard by everyone, started once");
    CHECK(!aa.setBuff(cues, "TnBuffWarcryBase", bot, true) && active("BL_TRANS_POWER.WAR_CRY_STATE_START") == 0,
          "a bot's Warcry buff: local-player only, silent");
    aa.stopAll(cues); for (int k = 0; k < 20; ++k) cues.tick(1.0f / 30.0f);   // the landed loop's 0.5 s fade too
    CHECK(aa.liveLoops(cues) == 0 && active("BL_TRANS_POWER.HOVER_JUMP_LIFT") == 0 &&
          active("BL_INTRFC_TECH_TREE.CLOAK_DECEPTICON_START_LP") == 0, "stopAll (match end / unload): every participant loop stops");
}

int main() {
    for (const char* g : {"SFX", "DIALOG", "MUSIC"}) game::SoundMixer::setGroupVolume(g, 1.0f);   // authored levels
    testSoundGroups();
    testSoundGroupsThreaded();
    testAbilityAudio();
    testLevelWarm();
    testChargeAndRoller();
    testParticipantLoops();
    testAbilityActors();
    testDeathAndGrenade();
    testMeleeHitAndMines();
    testCountdownAndGrenades();
    testWeaponAudio();
    testLocalizedWaves();
    testObjectiveMessages();
    testCharacterAudio();
    testZoneGraph();
    testMatchAudio();
    testFrontendSeam();
    testLevelLifecycle();
    testMapEventAudio();
    testFrontend();
    testLifecycle();
    testOcclusionSkip();
    testChannelStealing();
    testWorldBed();
    testLoopRuntime();
    testAuthored();
    testMixer();
    testZones();
    testEmitters();
    testGain();
    testChannelModes();
    std::printf("\nsuite: %d pass / %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
