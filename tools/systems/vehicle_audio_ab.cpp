// Systems M08 A/B probe (not part of the CMake build): the data-driven VehicleAudio against the previous hand-entered
// Optimus port, fed the same deterministic input script (and rand seed) on recording backends. Every voice start /
// stop is compared in order. Also prints what the new component does for other chassis.
// Old side: git show 4619f2c:src/game/VehicleAudio.{h,cpp} | sed 's/VehicleAudio/OldVehicleAudio/g'
//           -> work/m8_veh/old/game/OldVehicleAudio.{h,cpp}; build with -Iwork/m8_veh/old.
// Usage: vehicle_audio_ab.exe [chassis ...]
#include "audio/Audio.h"
#include "game/CharacterAudio.h"
#include "game/OldVehicleAudio.h"
#include "game/VehicleAudio.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace audio;
using core::Vec3;

struct Rec : IAudio {
    std::vector<std::string> log;
    std::map<int, std::string> voices;
    std::map<std::string, int> paths;
    std::vector<std::string> names;
    int n = 0;
    Sound load(const std::string& path) override {
        auto it = paths.find(path);
        if (it != paths.end()) return it->second;
        int id = (int)paths.size(); paths[path] = id; names.push_back(path.substr(path.find_last_of("/\\") + 1)); return id;
    }
    bool setLoopPoints(Sound, uint32_t, uint32_t) override { return true; }
    void play(Sound, float) override {}
    void playAt(Sound, const Vec3&, float, float, float) override {}
    Voice playVoice(Sound snd, const VoiceParams&) override {
        voices[n] = names[(size_t)snd];
        log.push_back("start " + names[(size_t)snd]);
        return n++;
    }
    void stopVoice(Voice h) override { if (voices.count(h)) log.push_back("stop " + voices[h]); }
    void updateVoice(Voice, float, float, const Vec3&) override {}
    void setListener(const Vec3&, const Vec3&, const Vec3&) override {}
    void update() override {}
    void setEnvironment(const Environment&, float) override {}
};

// Deterministic drive: accelerate, boost, jump, coast, reverse, dash / nitro / ram, a long fall, exit.
template <class VA>
std::vector<std::string> run(VA& va, const game::CharacterAudioProfile& p, const std::string& content) {
    Rec rec;
    game::SoundCues cues;
    cues.load(&rec, content);
    game::CharacterAudio::loadCues(cues, p);
    va.setProfile(p);
    std::srand(12345);                                    // the cues' random wave / pitch picks (std::rand)
    const float dt = 1.0f / 60.0f;
    auto at = [] { game::SoundCues::Emitter e; e.pos = {0, 0, 0}; return e; };
    std::vector<std::string> out;
    for (int f = 0; f < 60 * 24; ++f) {
        const float t = f * dt;
        typename VA::Input in;
        in.entered = t < 22.0f;
        in.onGround = !((t >= 8.0f && t < 9.0f) || (t >= 18.0f && t < 20.0f));
        float spd = t < 5.0f ? t * 8.0f : t < 9.0f ? 40.0f : t < 12.0f ? 40.0f - (t - 9.0f) * 12.0f : t < 15.0f ? -6.0f : 10.0f;
        in.velocity = {0, 0, spd};
        in.forward = {0, 0, 1};
        in.loadState = t < 9.0f ? 1 : t < 12.0f ? 0 : t < 15.0f ? 2 : 1;
        in.boosting = t >= 5.0f && t < 8.0f;
        in.wheelSlip = 0.1f;
        in.ascend = f == 8 * 60;
        in.booster = f == 15 * 60;
        in.nitro = f == 16 * 60;
        va.tick(dt, in, cues, at);
        if (f == 17 * 60) va.ram(cues, at);
        cues.tick(dt);
        for (const std::string& l : rec.log) out.push_back(std::to_string(f) + " " + l);
        rec.log.clear();
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string content = "F:/Transformers Rebuild/ExtractedAssets/content/";
    const game::CharacterAudioProfile& op = game::CharacterAudio::defaultProfile();
    game::OldVehicleAudio oldVa;
    game::VehicleAudio newVa;
    const auto a = run(oldVa, op, content);
    const auto b = run(newVa, op, content);
    size_t diff = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const std::string& x = i < a.size() ? a[i] : std::string("-");
        const std::string& y = i < b.size() ? b[i] : std::string("-");
        if (x != y && diff++ < 10) std::printf("  DIFF %zu: old [%s] new [%s]\n", i, x.c_str(), y.c_str());
    }
    std::printf("Optimus: old %zu events, new %zu, %zu differences -> %s\n", a.size(), b.size(), diff, diff ? "FAIL" : "IDENTICAL");
    for (int i = 1; i < argc; ++i) {
        const game::CharacterAudioProfile* p = game::CharacterAudio::find(argv[i]);
        if (!p) continue;
        game::VehicleAudio va;
        const auto c = run(va, *p, content);
        std::map<std::string, int> starts;
        for (const std::string& l : c) if (l.find(" start ") != std::string::npos) ++starts[l.substr(l.find(" start ") + 7)];
        std::printf("%s (%s):", argv[i], p->name.c_str());
        for (const auto& kv : starts) std::printf(" %s x%d", kv.first.c_str(), kv.second);
        std::printf("\n");
    }
    return diff ? 1 : 0;
}
