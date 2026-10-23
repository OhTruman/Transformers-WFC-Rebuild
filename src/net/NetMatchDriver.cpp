// [Systems] NetMatchDriver - see NetMatchDriver.h. The headless runNetMatch loop (Application_Net.cpp) cut into per-frame calls.
#include "net/NetMatchDriver.h"

#include "core/Log.h"
#include "game/CharacterRoster.h"
#include "game/World.h"
#include "net/Lockstep.h"
#include "net/NetTransport.h"
#include "net/VoiceSession.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace net {
namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr int kMaxStepsPerFrame = 8;   // catch-up cap (a long hitch is not replayed at once)
constexpr uint8_t kEventSelection = 1;

void putStr(std::vector<uint8_t>& b, const std::string& s) {
    const size_t n = std::min<size_t>(s.size(), 255);
    b.push_back((uint8_t)n);
    b.insert(b.end(), s.begin(), s.begin() + (long)n);
}
void putStrs(std::vector<uint8_t>& b, const std::vector<std::string>& v) {
    b.push_back((uint8_t)std::min<size_t>(v.size(), 255));
    for (size_t i = 0; i < v.size() && i < 255; ++i) putStr(b, v[i]);
}
void putColor(std::vector<uint8_t>& b, const game::CharacterColor& c) {
    for (int v : {c.r, c.g, c.b, c.a, c.palette}) b.push_back((uint8_t)v);
    for (float f : {c.x, c.y}) { const int16_t q = (int16_t)std::max(-32768.0f, std::min(32767.0f, f * 1000.0f)); b.push_back((uint8_t)q); b.push_back((uint8_t)((uint16_t)q >> 8)); }
}
struct Rd {
    const std::vector<uint8_t>& d; size_t i = 0; bool ok = true;
    explicit Rd(const std::vector<uint8_t>& v) : d(v) {}
    int u8() { if (i >= d.size()) { ok = false; return 0; } return d[i++]; }
    std::string str() { const size_t n = (size_t)u8(); if (i + n > d.size()) { ok = false; return ""; } std::string s((const char*)d.data() + i, n); i += n; return s; }
    std::vector<std::string> strs() { std::vector<std::string> v((size_t)u8()); for (auto& s : v) s = str(); return v; }
    game::CharacterColor color() {
        game::CharacterColor c;
        c.r = u8(); c.g = u8(); c.b = u8(); c.a = u8(); c.palette = u8();
        for (float* f : {&c.x, &c.y}) { const int lo = u8(), hi = u8(); *f = (float)(int16_t)(uint16_t)(lo | (hi << 8)) / 1000.0f; }
        return c;
    }
};

}  // namespace

// The selection on the wire (names, not indices: a data-order change cannot desync). Shared with the recording.
std::vector<uint8_t> encodeSelection(const game::CharacterSelection& s) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)s.type); b.push_back((uint8_t)s.specialty);
    putStr(b, s.chassisId); putStr(b, s.customSlot);
    for (int f = 0; f < 2; ++f) putStr(b, s.chassisByFaction[f]);
    for (int f = 0; f < 2; ++f) { putColor(b, s.primary[f]); putColor(b, s.secondary[f]); }
    putStrs(b, s.weapons); putStrs(b, s.vehicleWeapons); putStrs(b, s.melee); putStrs(b, s.abilities); putStrs(b, s.skills);
    return b;
}
bool decodeSelection(const std::vector<uint8_t>& b, game::CharacterSelection& s) {
    Rd r(b);
    s.type = r.u8(); s.specialty = (game::Specialty)r.u8();
    s.chassisId = r.str(); s.customSlot = r.str();
    for (int f = 0; f < 2; ++f) s.chassisByFaction[f] = r.str();
    for (int f = 0; f < 2; ++f) { s.primary[f] = r.color(); s.secondary[f] = r.color(); }
    s.weapons = r.strs(); s.vehicleWeapons = r.strs(); s.melee = r.strs(); s.abilities = r.strs(); s.skills = r.strs();
    return r.ok;
}

struct NetMatchDriver::Impl {
    game::World& world;
    INetTransport& t;
    Config cfg;
    std::unique_ptr<Lockstep> ls;
    std::unique_ptr<VoiceSession> voice;
    uint32_t nextLocal = 1;
    double accum = 0.0;
    bool stalled = false, held = false, ended = false, talking = false;
    uint32_t startTick = 1;
    FILE* rec = nullptr;
    Impl(game::World& w, INetTransport& tr, const Config& c) : world(w), t(tr), cfg(c) {}
};

NetMatchDriver::NetMatchDriver(game::World& world, INetTransport& transport, audio::IAudio* audio, const Config& cfg)
    : impl_(std::make_unique<Impl>(world, transport, cfg)) {
    Lockstep::Config lc;
    lc.seats = cfg.seats; lc.self = cfg.self; lc.delay = cfg.delay; lc.hashEvery = cfg.hashEvery;
    lc.peerTimeoutS = cfg.peerTimeoutS; lc.launchHash = cfg.launchHash; lc.logPath = cfg.logPath;
    impl_->ls = std::make_unique<Lockstep>(transport, lc);
    if (!cfg.recordPath.empty()) {
        impl_->rec = std::fopen(cfg.recordPath.c_str(), "wb");
        if (impl_->rec) {   // the phase 1 WFC_NET_RECORD layout (WFC_NET_REPLAY reads it); events are not in it yet
            const uint32_t ver = 1, ns = (uint32_t)cfg.seats;
            std::fwrite("WFCR", 1, 4, impl_->rec); std::fwrite(&ver, 4, 1, impl_->rec);
            std::fwrite(&cfg.launchHash, 8, 1, impl_->rec); std::fwrite(&ns, 4, 1, impl_->rec);
        }
    }
    if (cfg.voice) {
        impl_->voice = std::make_unique<VoiceSession>(*impl_->ls, audio, VoiceSession::Config{});
        game::World* w = &world;
        impl_->ls->setVoiceAudience([w](int talker, int listener, uint8_t flags) {   // as runNetMatch: match player = seat
            const auto& ps = w->match().players();
            if (talker < 0 || listener < 0 || (size_t)talker >= ps.size() || (size_t)listener >= ps.size()) return false;
            if ((flags & 1) && ps[(size_t)talker].team == ps[(size_t)listener].team && ps[(size_t)talker].team != 255) return true;
            if (!(flags & 2)) return false;
            const game::Character* x = w->participantPawn(talker);
            const game::Character* y = w->participantPawn(listener);
            return x && y && core::length(x->position() - y->position()) < 64.0f + 4.0f;
        });
    }
    LOG_INFO("NET driver: seat %d of %d, D %s, voice %s", cfg.self, cfg.seats, cfg.delay > 0 ? std::to_string(cfg.delay).c_str() : "auto",
             cfg.voice ? "on" : "off");
}

NetMatchDriver::~NetMatchDriver() {
    if (impl_->ls && !impl_->ended) impl_->ls->bye((uint32_t)impl_->world.simTick());
    if (impl_->rec) std::fclose(impl_->rec);
}

int NetMatchDriver::frame(double dtSeconds) {
    Impl& m = *impl_;
    Lockstep& ls = *m.ls;
    ls.pump();
    // Events (every seat's, this one's too): applied by the World at their tick.
    Lockstep::Event ev;
    while (ls.pollEvent(ev)) {
        if (ev.kind == kEventSelection) {
            game::CharacterSelection sel;
            if (decodeSelection(ev.payload, sel)) { if (m.cfg.onSelection) m.cfg.onSelection(ev.seat, sel, ev.atTick); }
            else LOG_WARN("NET driver: a bad selection event from seat %d", ev.seat);
        }
    }
    if (m.voice) {   // the listener = the local pawn (its right vector from the facing yaw)
        game::World& w = m.world;
        const game::Character* me = w.participantPawn(m.cfg.self);
        const core::Vec3 lp = me ? me->position() + core::Vec3{0, 1.8f, 0} : core::Vec3{0, 0, 0};
        const float yaw = me ? me->yaw() : 0.0f;
        const core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(yaw, 0.0f), core::Vec3{0, 1, 0}));
        const int self = m.cfg.self;
        m.voice->update(m.talking,
                        [&w](int seat, core::Vec3& out) { const game::Character* c = w.participantPawn(seat); if (!c) return false; out = c->position() + core::Vec3{0, 1.8f, 0}; return true; },
                        [&w, self](int seat) { const auto& ps = w.match().players(); return (size_t)seat < ps.size() && (size_t)self < ps.size() && ps[(size_t)seat].team == ps[(size_t)self].team && ps[(size_t)seat].team != 255; },
                        [&w](const core::Vec3& a, const core::Vec3& b) { return w.soundBlocked(a, b); },
                        [&w]() { return w.currentReverbPreset(); }, lp, right);
    }
    if (ls.status() != Lockstep::Status::Running) { m.ended = true; return 0; }
    if (m.held) { m.accum = 0.0; return 0; }
    m.accum = std::min(m.accum + dtSeconds, (double)kStep * kMaxStepsPerFrame);
    int steps = 0;
    m.stalled = false;
    while (m.accum >= kStep && steps < kMaxStepsPerFrame) {
        const uint32_t n = (uint32_t)m.world.simTick() + 1;
        if (ls.endTick() > 0 && n > ls.endTick()) { m.ended = true; break; }
        while (m.nextLocal <= n + (uint32_t)ls.delay()) {   // this seat's commands up to N + D (the local controller's input now)
            game::RemoteCommand c = m.world.encodeLocalCommand();
            c.tick = m.nextLocal;
            ls.submitLocal(m.nextLocal, c);
            ++m.nextLocal;
        }
        if (!ls.haveAll(n)) { m.stalled = true; ls.stalled(dtSeconds * 1000.0); break; }
        for (int s = 0; s < m.cfg.seats; ++s) {
            game::RemoteCommand c;
            ls.command(s, n, c);
            m.world.applyRemoteCommand(s, c);
        }
        if (m.rec) {
            std::fwrite(&n, 4, 1, m.rec);
            for (int s = 0; s < m.cfg.seats; ++s) { WireCommand w{}; ls.wire(s, n, w); std::fwrite(w.data(), 1, w.size(), m.rec); }
        }
        if (m.cfg.step) m.cfg.step(kStep);
        const uint64_t h = m.world.simStateHash();
        ls.stepped(n, h);
        ls.reportHash(n, h);
        if (n > 64) ls.forget(n - 64);
        m.accum -= kStep;
        ++steps;
    }
    if (ls.status() != Lockstep::Status::Running) m.ended = true;
    return steps;
}

bool NetMatchDriver::stalled() const { return impl_->stalled; }
bool NetMatchDriver::held() const { return impl_->held; }
void NetMatchDriver::setHeld(bool held, uint32_t startTick) { impl_->held = held; if (startTick) impl_->startTick = startTick; }
bool NetMatchDriver::finished() const { return impl_->ended; }
int NetMatchDriver::status() const { return (int)impl_->ls->status(); }
int NetMatchDriver::droppedSeat() const { return -1; }   // [PARTIAL] the session reports drops (PeerDropped) for now
int NetMatchDriver::localSeat() const { return impl_->cfg.self; }
int NetMatchDriver::delay() const { return impl_->ls->delay(); }
float NetMatchDriver::rttMs() const { return impl_->ls->rttMs(); }
VoiceSession* NetMatchDriver::voice() { return impl_->voice.get(); }
void NetMatchDriver::setTalking(bool talking) { impl_->talking = talking; }

void NetMatchDriver::chooseCharacter(const game::CharacterSelection& sel) {
    const uint32_t at = impl_->ls->submitEvent(kEventSelection, encodeSelection(sel));
    LOG_INFO("NET driver: Choose Character -> event at tick %u", at);
}

}  // namespace net
