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
#include "game/VehicleFormAudio.h"
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

// The previous World mapping (Gameplay state -> VehicleAudio::Input, truck-shaped) vs VehicleFormAudio on the same
// truck-form signals, both on the current VehicleAudio: every voice event must match.
static std::vector<std::string> runSignals(bool translator, const game::CharacterAudioProfile& p, const std::string& content) {
    Rec rec;
    game::SoundCues cues;
    cues.load(&rec, content);
    game::CharacterAudio::loadCues(cues, p);
    game::VehicleAudio va;
    va.setProfile(p);
    game::VehicleFormAudio form;
    std::srand(12345);
    const float dt = 1.0f / 60.0f;
    auto at = [] { game::SoundCues::Emitter e; e.pos = {0, 0, 0}; return e; };
    std::vector<std::string> out;
    int loadState = 0; bool prevDash = false, prevGround = true;
    for (int f = 0; f < 60 * 24; ++f) {
        const float t = f * dt;
        const bool vehicle = t < 22.0f;
        const bool ground = !((t >= 8.0f && t < 9.0f) || (t >= 18.0f && t < 20.0f));
        const float stick = t < 9.0f ? 1.0f : t < 12.0f ? 0.0f : t < 15.0f ? -1.0f : 1.0f;
        const float spd = t < 5.0f ? t * 8.0f : t < 9.0f ? 40.0f : t < 12.0f ? 40.0f - (t - 9.0f) * 12.0f : t < 15.0f ? -6.0f : 10.0f;
        const bool boost = vehicle && t >= 5.0f && t < 8.0f;
        const bool tookOff = vehicle && prevGround && !ground;
        const bool dashing = t >= 15.0f && t < 15.3f;
        const bool nitroStart = f == 16 * 60;
        game::VehicleAudio::Input in;
        if (translator) {
            game::VehicleFormSignals s;
            s.kind = game::VehicleFormSignals::Kind::Truck;
            s.vehicle = vehicle; s.onGround = ground; s.boostState = boost; s.stickForward = stick;
            s.velocity = {0, 0, spd}; s.forward = {0, 0, 1}; s.tookOff = tookOff; s.dashing = dashing; s.nitroStarted = nitroStart;
            in = form.translate(s);
        } else {                                   // the M08c World block
            in.entered = vehicle; in.boosting = boost; in.onGround = ground;
            if (!boost) loadState = stick > 0.01f ? 1 : (stick < -0.01f ? 2 : 0);
            in.loadState = loadState; in.wheelSlip = 0.0f;
            in.velocity = {0, 0, spd}; in.forward = {0, 0, 1}; in.ascend = tookOff;
            in.booster = vehicle && !boost && dashing && !prevDash;
            in.nitro = nitroStart;
        }
        prevDash = dashing; prevGround = ground;
        va.tick(dt, in, cues, at);
        cues.tick(dt);
        for (const std::string& l : rec.log) out.push_back(std::to_string(f) + " " + l);
        rec.log.clear();
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string content = "F:/Transformers Rebuild/ExtractedAssets/content/";
    {
        const auto x = runSignals(false, game::CharacterAudio::defaultProfile(), content);
        const auto y = runSignals(true, game::CharacterAudio::defaultProfile(), content);
        std::printf("Optimus truck form: World mapping %zu events, VehicleFormAudio %zu -> %s\n", x.size(), y.size(), x == y ? "IDENTICAL" : "DIFFERENT");
        if (x != y) return 1;
    }
    const game::CharacterAudioProfile& op = game::CharacterAudio::defaultProfile();
    game::OldVehicleAudio oldVa;
    game::VehicleAudio newVa;
    const auto a = run(oldVa, op, content);
    const auto b = run(newVa, op, content);
    // Starts must match exactly; stops may differ only where the port was corrected to the script
    // (HmVehicleAudioComponent.Detached: the speed / tread / squeal loops stop with fade 0 [CONF]).
    auto starts = [](const std::vector<std::string>& v) {
        std::vector<std::string> o;
        for (const auto& l : v) if (l.find(" start ") != std::string::npos) o.push_back(l);
        return o;
    };
    const auto sa = starts(a), sb = starts(b);
    size_t diff = 0;
    for (size_t i = 0; i < std::max(sa.size(), sb.size()); ++i) {
        const std::string& x = i < sa.size() ? sa[i] : std::string("-");
        const std::string& y = i < sb.size() ? sb[i] : std::string("-");
        if (x != y && diff++ < 10) std::printf("  DIFF %zu: old [%s] new [%s]\n", i, x.c_str(), y.c_str());
    }
    size_t stopDiff = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) if (a[i] != b[i] && a[i].find(" stop ") != std::string::npos) ++stopDiff;
    std::printf("Optimus: %zu / %zu starts, %zu differences -> %s (stops differing in order / timing: %zu - Detached fade 0)\n",
                sa.size(), sb.size(), diff, diff ? "FAIL" : "IDENTICAL", stopDiff);
    // Per form: the component calls the form classes make (TnCarForm / TnTruckForm / TnTankForm / TnPlaneForm).
    int formFail = 0;
    auto form = [&](const char* key, auto&& script) {
        const game::CharacterAudioProfile* p = game::CharacterAudio::find(key);
        Rec rec; game::SoundCues cues; cues.load(&rec, content);
        game::CharacterAudio::loadCues(cues, *p);
        game::VehicleAudio va; va.setProfile(*p);
        auto at = [] { game::SoundCues::Emitter e; e.pos = {0, 0, 0}; return e; };
        auto step = [&](game::VehicleAudio::Input in, int frames = 1) {
            for (int k = 0; k < frames; ++k) { va.tick(1.0f / 60.0f, in, cues, at); cues.tick(1.0f / 60.0f); in.booster = in.boosterStop = false;
                                               in.ascendStop = in.descend = in.descendStop = in.roll = in.oneEighty = false; }
        };
        auto active = [&](const char* ev) { const std::string q = p->vehicleCue(ev); return !q.empty() && cues.activeInstances(q.c_str()) > 0; };
        script(step, active, *p);
        (void)rec;
    };
    auto check = [&](bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++formFail; };
    using In = game::VehicleAudio::Input;
    form("Jet", [&](auto& step, auto& active, const game::CharacterAudioProfile&) {
        In in; in.entered = true; in.onGround = false; in.velocity = {0, 0, 5};
        step(in, 2);
        check(active("Auto_Speed"), "Starscream: SpeedSound loop from Attached (Auto_Speed)");
        In b = in; b.booster = true; step(b);
        check(active("Auto_Hover_Boosters"), "Starscream hover: PlayHoverFx -> the hover boosters loop");
        In amt = in; amt.boosterAmount = 0.7f; step(amt, 2);
        In st = in; st.boosterStop = true; step(st, 3);
        check(!active("Auto_Hover_Boosters"), "Starscream hover: StopHoverFx -> the boosters loop stops");
        In d = in; d.descend = true; step(d);
        check(active("Auto_Engine_Hover_Descend"), "Starscream: UpdateDashing down -> DescendSound");
        In r = in; r.roll = true; step(r);
        check(active("AUTO_ROLL_START"), "Starscream: UpdateRolling -> RollSound");
        In e = in; e.entered = false; step(e, 2);
        check(!active("Auto_Speed"), "Starscream: Detached stops the speed loop");
    });
    form("Car", [&](auto& step, auto& active, const game::CharacterAudioProfile&) {
        In in; in.entered = true; in.onGround = true; in.velocity = {0, 0, 10}; in.loadState = 1;
        step(in, 30);
        check(active("Auto_Speed") && active("Auto_Engine_Gear_1_OnLoad"), "Bumblebee: speed loop + drive on-load loop");
        In b = in; b.booster = true; step(b);
        check(active("Auto_Boost_Dash"), "Bumblebee hover: DoDash -> BoosterSound (Auto_Boost_Dash)");
    });
    form("Tank", [&](auto& step, auto& active, const game::CharacterAudioProfile&) {
        In in; in.entered = true; in.onGround = true; in.velocity = {0, 0, 5}; in.loadState = 1;
        step(in, 10);
        In o = in; o.oneEighty = true; step(o);
        check(active("Auto_180_Turn"), "Megatron: ClientPlaySpecialMoveSound -> OneEightySound (Auto_180_Turn)");
    });
    std::printf("forms: %s\n", formFail ? "FAIL" : "OK");
    diff += (size_t)formFail;
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
