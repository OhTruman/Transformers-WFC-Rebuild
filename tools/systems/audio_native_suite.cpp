// Systems M03 pass 5 native-audio validation suite (not part of the CMake build). Build from the repo root:
//   .toolchain/llvm-mingw-*/bin/clang++.exe -std=c++17 -O2 -Isrc tools/systems/audio_native_suite.cpp src/game/SoundCues.cpp \
//     src/game/SoundMixer.cpp src/game/AmbientAudio.cpp src/core/Log.cpp src/platform/win32/Win32Audio.cpp -lwinmm -static -o suite.exe
// Reads ExtractedAssets (read only). Channel-mode checks need an audio device (skipped otherwise).
// Systems M03 native-audio validation suite (RE 7c4a2e0): mixer, zones, emitter geometry, dB, channel modes.
// Deterministic: recording backends for SoundCues / AmbientAudio; the real Win32 backend for channel modes.
#include "game/AmbientAudio.h"
#include "game/SoundCues.h"
#include "game/SoundMixer.h"
#include "assets/Json.h"
#include <chrono>
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
    struct V { VoiceParams p; Vec3 pos; bool live = true; };
    std::map<int, V> v;
    std::vector<Environment> envs;
    int n = 0;
    Sound load(const std::string&) override { return 0; }
    void play(Sound, float) override {}
    void playAt(Sound, const Vec3&, float, float, float) override {}
    Voice playVoice(Sound, const VoiceParams& p) override { v[n] = {p, p.pos, true}; return n++; }
    void stopVoice(Voice h) override { if (v.count(h)) v[h].live = false; }
    void updateVoice(Voice h, float, float, const Vec3& pos) override { if (v.count(h)) v[h].pos = pos; }
    void setListener(const Vec3&, const Vec3&, const Vec3&) override {}
    void update() override {}
    void setEnvironment(const Environment& e, float) override { envs.push_back(e); }
};

// ---------------------------------------------------------------- mixer
static void step(game::SoundMixer& m, float t) { m.tick(t); }
static float vol(game::SoundMixer& m) { return m.categoryVolume("SFX_WET_VEH_ENGINE"); }

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
      CHECK(!std::strcmp(m.categoryTarget(0), "VEHICLE_JUMP"), "higher wins: %s", m.categoryTarget(0));
      step(m, 0.15f); CHECK(near(vol(m), B + (J - B) * 0.5f), "jump mid (from current) %.4f", vol(m));
      step(m, 0.15f); CHECK(near(vol(m), J), "jump end %.4f", vol(m));
      CHECK(near(J, 0.1258925f, 1e-6f), "volume stays linear amplitude (not re-converted)"); }
    { game::SoundMixer m;   // lower-priority activation: no retarget
      m.enable("VEHICLE_JUMP"); step(m, 0.3f);
      m.enable("VEHICLE_BOOST_END");
      CHECK(!std::strcmp(m.categoryTarget(0), "VEHICLE_JUMP"), "lower does not take over: %s", m.categoryTarget(0));
      step(m, 0.1f); CHECK(near(vol(m), J), "unchanged %.4f", vol(m));
      // step down on removal: outgoing FadeOut (JUMP 1.0)
      m.disable("VEHICLE_JUMP", false);
      CHECK(!std::strcmp(m.categoryTarget(0), "VEHICLE_BOOST_END"), "falls to boost_end");
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
      m.enable("REVERB_TRANS_MP_STREETS_EXTERIOR"); m.enable("VEHICLE_JUMP");
      CHECK(!std::strcmp(m.categoryTarget(0), "VEHICLE_JUMP"), "cat0 %s", m.categoryTarget(0));
      CHECK(!std::strcmp(m.categoryTarget(1), "REVERB_TRANS_MP_STREETS_EXTERIOR"), "cat1 falls through: %s", m.categoryTarget(1));
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
      CHECK(!std::strcmp(m.categoryTarget(0), "A"), "equal priority: earlier wins (%s)", m.categoryTarget(0));
      CHECK(m.activeList() == "A(1),B(1),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.4f); CHECK(near(vol(m), 0.5f), "A value %.3f", vol(m));
      m.disable("A", false);   // B equal priority -> incoming FadeIn 0.2
      step(m, 0.1f); CHECK(near(vol(m), 0.375f), "equal step uses incoming FadeIn %.3f", vol(m));
      m.enable("C"); CHECK(!std::strcmp(m.categoryTarget(0), "B"), "C defines no cat0 -> B (%s)", m.categoryTarget(0));
      CHECK(!std::strcmp(m.categoryTarget(1), "C"), "cat1 C"); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("B"); m.enable("A");
      CHECK(!std::strcmp(m.categoryTarget(0), "B"), "equal priority reversed: earlier B wins (%s)", m.categoryTarget(0)); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("Z"); CHECK(m.activeList() == "Z(1),Default(1)", "%s", m.activeList().c_str());
      step(m, 0.016f); CHECK(m.activeList() == "Default(1)", "Duration 0 expires next tick: %s", m.activeList().c_str()); }
    { game::SoundMixer m(P, 4, C0, 4, C1, 2);
      m.enable("A"); m.enable("A"); m.disable("A", true);
      CHECK(m.activeList() == "Default(1)", "force disable removes at RefCount 2: %s", m.activeList().c_str());
      m.enable("A"); m.enable("A"); m.disable("A", false);
      CHECK(m.activeList() == "A(1),Default(1)", "plain disable decrements: %s", m.activeList().c_str()); }
    { game::SoundMixer m;   // Flush (level change)
      m.activateReverb("REVERB_TRANS_MP_STREETS_EXTERIOR"); m.enable("VEHICLE_JUMP"); m.flush();
      CHECK(m.activeList() == "Default(1)" && m.currentReverb().empty(), "flush: %s", m.activeList().c_str()); }
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
        CHECK(!std::strcmp(m.categoryTarget(1), zs[(size_t)z].preset.c_str()), "MASTER_WET target %s", m.categoryTarget(1));
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
    int f3 = rec.n; cues.play("T.3D", Vec3{100, 0, 0}, 100.0f);
    CHECK(rec.v[f3].p.positional && rec.v[f3].p.spatial == 0, "unauthored Spatialization = k3D positional");
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

int main() {
    testMixer();
    testZones();
    testEmitters();
    testGain();
    testChannelModes();
    std::printf("\nsuite: %d pass / %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
