#include "game/SoundCues.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
namespace {

struct CurvePt { float x, y; };
struct EventDef {
    float time, volDb, volVarMin, volVarMax, pitchSt, pitchVarMin, pitchVarMax;
    std::vector<const char*> waves;      // random pick
    std::vector<CurvePt> volumeCurve;    // over SOUND_DISTANCE (UU); empty = 1
};
struct CueDef {
    const char* name;
    int maxConcurrent;                   // SoundCue.MaxConcurrentPlayCount (0 = unlimited)
    float volDb, volVarMin, volVarMax, pitchSt, pitchVarMin, pitchVarMax;
    float distMinUU, distMaxUU, rolloff;
    std::vector<EventDef> events;
};

// Generated from the decoded cooked cues (work/audio/cues.inc); [CONF] values.
const CueDef kCues[] = {
    {"SHOOT", 4, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4000.0f, 35000.0f, 1.0f, {
        {0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.5f, -0.5f, {"WL_GUN_ION_BLASTER/GUN_ION_BLASTER_HEAD.wav"}, {{200.0f, 1.0f}, {400.0f, 0.00787402f}, {10000.0f, 0.0f}}},
        {0.0f, 0.0f, -2.0f, 0.0f, 0.0f, -1.0f, 1.0f, {"WL_GUN_NEUTRON_RIFLE/GUN_ASSAULTRIFLE_NRG_DISTANT.wav"}, {{4000.0f, 0.0f}, {8000.0f, 1.0f}}},
        {0.0f, 0.0f, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_01.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_02.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_03.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_04.wav"}, {{200.0f, 1.0f}, {2000.0f, 0.0f}}},
        {0.0f, 0.0f, -3.0f, 0.0f, 0.0f, -1.0f, 1.0f, {"WL_GUN_HANDCANNON/GUN_HANDCANNON_SHOOT_LR_01.wav"}, {{200.0f, 0.0f}, {400.0f, 0.707946f}, {4000.0f, 0.707946f}, {8020.18f, 0.0f}}},
        {0.473889f, -9.0f, -6.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_01.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_02.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_03.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_04.wav"}, {{690.617f, 1.0f}, {4138.7f, 0.0f}}},
    }},
    {"SHOOT_LOW_AMMO", 4, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4000.0f, 35000.0f, 1.0f, {
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f, {"WL_GUN_ION_BLASTER/GUN_ION_BLASTER_HEAD.wav"}, {{100.0f, 1.0f}, {400.0f, 0.00787402f}, {10000.0f, 0.0f}}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, {"WL_GUN_NEUTRON_RIFLE/GUN_ASSAULTRIFLE_NRG_DISTANT.wav"}, {{4000.0f, 0.0f}, {8000.0f, 1.0f}}},
        {0.0f, 0.0f, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_01.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_02.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_03.wav", "WL_GUN_NEUTRON_RIFLE/MTL_SHELL_SLUG_DROP_CONCRT_04.wav"}, {{200.0f, 1.0f}, {2000.0f, 0.0f}}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, {"WL_GUN_HANDCANNON/GUN_HANDCANNON_SHOOT_LR_01.wav"}, {{200.0f, 0.0f}, {400.0f, 0.707946f}, {4000.0f, 0.707946f}, {8020.18f, 0.0f}}},
        {0.0977431f, -6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/LOW_AMMO_WARNING_01.wav"}, {{745.594f, 1.0f}, {3973.69f, 0.00787402f}}},
    }},
    {"SHOOT_TAIL", 4, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3000.0f, 12000.0f, 1.0f, {
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.5f, {"WL_GUN_ION_BLASTER/GUN_ION_BLASTER_TAIL.wav"}, {{200.0f, 1.0f}, {4973.95f, 0.0f}}},
    }},
    {"ANIM_RELOAD_01", 0, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f, 6400.0f, 2.0f, {
        {0.0f, -3.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_SERVO_MECH_05.wav"}, {}},
        {0.04125f, 0.0f, 0.0f, 0.0f, -3.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/GUN_DW2_REDEEM_RELOAD_START.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_AIR_RELEASE_THUMP.wav"}, {}},
    }},
    {"ANIM_RELOAD_02", 0, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f, 6400.0f, 2.0f, {
        {0.74153f, -3.0f, 0.0f, 0.0f, -6.0f, 0.0f, 0.0f, {"WL_FOLY_BOT/SERVO_IDLE_FOLY_01.wav"}, {}},
        {0.581945f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_FOLY_BOT/SERVO_IDLE_FOLY_04.wav"}, {}},
        {0.982224f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/GUN_RIFLE_CLIP_RELOAD.wav"}, {}},
        {0.590814f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_ION_BLASTER/TRANS_ION_UNEQUIP_SERVOS.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_PULSE_RIFLE/GUN_FOLEY_MG_SERVO_MTL_SYNTH_02.wav"}, {}},
        {0.770126f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_BOLT_07.wav"}, {}},
        {0.877013f, 0.0f, 0.0f, 0.0f, -3.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_BOLT_07.wav"}, {}},
        {0.990538f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_BOLT_07.wav"}, {}},
        {1.03291f, -9.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_FLANGE_02.wav"}, {}},
        {0.0f, -6.0f, 0.0f, 0.0f, -6.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_SYNTH_BUBBLE.wav"}, {}},
        {0.126603f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_MTL_SCRAPE_03.wav"}, {}},
        {1.32775f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_BOLT_07.wav"}, {}},
    }},
    {"IDLE_01", 0, -21.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 400.0f, 6400.0f, 1.0f, {
        {0.0f, 0.0f, 0.0f, 0.0f, -7.0f, 0.0f, 0.0f, {"WL_GUN_ION_BLASTER/TRANS_ION_UNEQUIP_SERVOS.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/GUNf_MVMNT_03.wav"}, {}},
        {0.995255f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/GUNf_MVMNT_01.wav"}, {}},
        {0.883299f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, {"WL_GUN_ION_BLASTER/TRANS_ION_EQUIP_SERVOS.wav"}, {}},
    }},
    {"IDLE_02", 0, -21.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 400.0f, 6400.0f, 1.0f, {
        {0.41236f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, {"WL_GUN_ION_BLASTER/TRANS_ION_UNEQUIP_SERVOS.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/GUN_RIFLE_BOLT_ACTION.wav"}, {}},
    }},
    {"IMPT_WORLD", 6, -12.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1500.0f, 25000.0f, 1.0f, {
        {0.0f, 0.0f, 0.0f, 0.0f, 9.0f, 0.0f, 3.0f, {"WL_ELEC/ELEC_SPARKBLAST_FLANGE_01.wav", "WL_ELEC/ELEC_SPARKBLAST_FLANGE_02.wav", "WL_ELEC/ELEC_SPARKBLAST_FLANGE_03.wav", "WL_ELEC/ELEC_SPARKBLAST_FLANGE_04.wav"}, {}},
        {0.0f, 0.0f, -3.0f, 0.0f, 0.0f, -1.0f, 1.0f, {"WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_01.wav", "WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_02.wav", "WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_03.wav", "WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_04.wav", "WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_05.wav", "WL_WPN_BULLET_IMPT/MTL_BULLET_IMPT_SHEET_06.wav"}, {}},
    }},
    {"IMPT_DMG", 6, -9.0f, 0.0f, 0.0f, 0.0f, 2.0f, -2.0f, 800.0f, 25000.0f, 2.0f, {
        {0.0f, 0.0f, 0.0f, 0.0f, 6.0f, -2.0f, 0.0f, {"WL_MTL_MELEE_IMPT/MTL_MELEE_IMPT_01.wav", "WL_MTL_MELEE_IMPT/MTL_MELEE_IMPT_02.wav", "WL_MTL_MELEE_IMPT/MTL_MELEE_IMPT_03.wav", "WL_MTL_MELEE_IMPT/MTL_MELEE_IMPT_04.wav"}, {}},
        {0.0503715f, 0.0f, 0.0f, 0.0f, -6.0f, -2.0f, 0.0f, {"WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_01.wav", "WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_02.wav", "WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_03.wav", "WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_04.wav", "WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_05.wav", "WL_FS_BOT/SRVO_BOT_FOLY_FS_FILTSWEEP_06.wav"}, {}},
    }},
    {"FOLEY.SHOOT_DRY_FIRE_ELECTRICITY", 1, -9.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 800.0f, 6400.0f, 1.0f, {
        {0.0296521f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/DRY_FIRE_ELECTRICITY.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/DRY_FIRE_PLASMA.wav"}, {}},
        {0.133493f, 0.0f, -1.0f, 1.0f, 6.0f, -1.0f, 1.0f, {"WL_ELEC/ELEC_EMP_INTERFERENCE_03.wav", "WL_ELEC/ELEC_EMP_INTERFERENCE_04.wav", "WL_ELEC/ELEC_EMP_INTERFERENCE_05.wav"}, {}},
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/RELOAD_BOLT_07.wav"}, {}},
        {0.0248696f, -12.0f, 0.0f, 0.0f, -7.0f, 0.0f, 0.0f, {"WL_GUN_FOLEY/LOW_AMMO_WARNING_01.wav"}, {}},
    }},
};
constexpr int kCueCount = (int)(sizeof(kCues) / sizeof(kCues[0]));

constexpr float UU = 0.01f;
constexpr float kSmartPan2D = 200 * UU, kSmartPan3D = 400 * UU;   // SoundNodeRoot SmartPanDistance2D/3D
constexpr float kInstanceTail = 2.0f;   // [PROV] seconds an instance counts as live after its last event

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float randRange(float a, float b) { return a + (b - a) * frand(); }   // ranges may be authored inverted
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
float stToRate(float st) { return std::pow(2.0f, st / 12.0f); }

float evalCurve(const std::vector<CurvePt>& c, float x) {
    if (c.empty()) return 1.0f;
    if (x <= c.front().x) return c.front().y;
    if (x >= c.back().x) return c.back().y;
    for (size_t i = 0; i + 1 < c.size(); ++i)
        if (x <= c[i + 1].x) {
            float u = (x - c[i].x) / (c[i + 1].x - c[i].x);
            return c[i].y + (c[i + 1].y - c[i].y) * u;
        }
    return c.back().y;
}

int findCue(const char* name) {
    for (int i = 0; i < kCueCount; ++i) if (std::strcmp(kCues[i].name, name) == 0) return i;
    return -1;
}

float lastEventTime(const CueDef& c) {
    float t = 0; for (const EventDef& e : c.events) t = e.time > t ? e.time : t; return t;
}

} // namespace

void SoundCues::load(audio::IAudio* a, const std::string& contentRoot) {
    audio_ = a;
    waves_.assign(kCueCount, {});
    if (!a) return;
    int ok = 0, fail = 0;
    for (int c = 0; c < kCueCount; ++c) {
        for (const EventDef& e : kCues[c].events) {
            std::vector<audio::Sound> w;
            for (const char* f : e.waves) {
                audio::Sound s = a->load(contentRoot + f);
                if (s == audio::kInvalidSound) ++fail; else ++ok;
                w.push_back(s);
            }
            waves_[(size_t)c].push_back(w);
        }
    }
    LOG_INFO("sound cues: %d cues, %d waves loaded, %d failed", kCueCount, ok, fail);
}

int SoundCues::activeInstances(const char* cue) const {
    int c = findCue(cue), n = 0;
    for (const Instance& in : live_) if (in.cue == c) ++n;
    return n;
}

bool SoundCues::play(const char* name, const core::Vec3& pos, float paramDistM) {
    if (!audio_) return false;
    int c = findCue(name);
    if (c < 0) { LOG_WARN("sound cue %s not in table", name); return false; }
    const CueDef& cd = kCues[c];
    // MaxConcurrentPlayCount: steal the oldest instance of this cue.
    if (cd.maxConcurrent > 0) {
        int count = 0, oldest = -1;
        for (int i = 0; i < (int)live_.size(); ++i) {
            if (live_[(size_t)i].cue != c) continue;
            ++count;
            if (oldest < 0 || live_[(size_t)i].age > live_[(size_t)oldest].age) oldest = i;
        }
        if (count >= cd.maxConcurrent && oldest >= 0) {
            int id = live_[(size_t)oldest].id;
            for (audio::Voice v : live_[(size_t)oldest].voices) audio_->stopVoice(v);
            for (size_t i = 0; i < pending_.size();) {
                if (pending_[i].inst == id) { pending_[i] = pending_.back(); pending_.pop_back(); } else ++i;
            }
            live_[(size_t)oldest] = live_.back(); live_.pop_back();
        }
    }
    Instance in{c, nextId_++, 0.0f, pos, paramDistM, {}};
    live_.push_back(in);
    for (int e = 0; e < (int)cd.events.size(); ++e) {
        if (cd.events[(size_t)e].time <= 0.0f) launch(live_.back(), e);
        else pending_.push_back({in.id, cd.events[(size_t)e].time, e});
    }
    return true;
}

void SoundCues::launch(Instance& in, int e) {
    const CueDef& cd = kCues[in.cue];
    const EventDef& ed = cd.events[(size_t)e];
    const std::vector<audio::Sound>& w = waves_[(size_t)in.cue][(size_t)e];
    if (w.empty()) return;
    audio::Sound s = w[(size_t)(std::rand() % (int)w.size())];
    float curve = evalCurve(ed.volumeCurve, in.paramDist / UU);
    if (curve <= 0.0f) return;
    float db = cd.volDb + randRange(cd.volVarMin, cd.volVarMax) + ed.volDb + randRange(ed.volVarMin, ed.volVarMax);
    float st = cd.pitchSt + randRange(cd.pitchVarMin, cd.pitchVarMax) + ed.pitchSt + randRange(ed.pitchVarMin, ed.pitchVarMax);
    audio::VoiceParams p;
    p.volume = dbToGain(db) * curve;
    p.pitch = stToRate(st);
    p.positional = true;
    p.pos = in.pos;
    p.minDist = cd.distMinUU * UU;
    p.maxDist = cd.distMaxUU * UU;
    p.rolloff = cd.rolloff;
    p.pan2D = kSmartPan2D; p.pan3D = kSmartPan3D;
    audio::Voice v = audio_->playVoice(s, p);
    if (v != audio::kInvalidVoice) in.voices.push_back(v);
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log)
        LOG_INFO("CUE %s ev%d t=%.3f wave=%d gain=%.3f (%.1f dB x curve %.2f @%.0fUU) pitch=%.3f voice=%d",
                 cd.name, e, in.age, s, p.volume, db, curve, in.paramDist / UU, p.pitch, v);
}

void SoundCues::tick(float dt) {
    for (Instance& in : live_) in.age += dt;
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        Instance* in = nullptr;
        for (Instance& x : live_) if (x.id == p.inst) { in = &x; break; }
        if (!in) { pending_[i] = pending_.back(); pending_.pop_back(); continue; }
        if (in->age >= p.t) {
            launch(*in, p.event);
            pending_[i] = pending_.back(); pending_.pop_back();
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i < live_.size();) {
        if (live_[i].age > lastEventTime(kCues[live_[i].cue]) + kInstanceTail) {
            live_[i] = live_.back(); live_.pop_back();
        } else ++i;
    }
}

} // namespace game
