// [Systems] Lockstep layer - see Lockstep.h.
#include "net/Lockstep.h"

#include "core/Log.h"
#include "game/World.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace net {
namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr float kR = 65536.0f / 6.2831853f;   // rotator units per radian (the same constant as World::encodeLocalCommand)

void put8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t get64(const uint8_t* p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }

enum : uint8_t { kCmds = 1, kHello = 2, kHash = 3, kBye = 4, kPing = 5, kPong = 6, kVoice = 7, kEvent = 8,
                 kJoinReq = 9, kHist = 10, kReady = 11, kLeaveQ = 12, kLeaveR = 13, kLeaveF = 14 };
constexpr double kLeaveWaitMs = 1000.0;         // the host merges the members' leave reports for at most this long
enum : uint8_t { kHistTicks = 0, kHistEvent = 1, kHistHash = 2 };   // HIST sub-records
constexpr size_t kHistPacketBytes = 1100;      // under the transport's 2048-byte receive buffer, one datagram
constexpr double kFeedBytesPerMs = 2000.0;     // 2 MB/s history pacing (the reliable channel has no window of its own)
constexpr uint32_t kReadyTicks = 10;           // a joiner within this many ticks of the forwarded history says READY
constexpr uint32_t kFeedPastActive = 120;      // the host forwards until it has stepped the joiner's activeTick + this
constexpr uint32_t kRedundancy = 3;   // ticks per regular CMDS packet: the newest + 2 before it (covers isolated losses)

}  // namespace

WireCommand encodeCommand(const game::RemoteCommand& c) {
    WireCommand w{};
    w[0] = (uint8_t)c.moveForward; w[1] = (uint8_t)c.moveRight;
    for (int i = 0; i < 4; ++i) { w[2 + i] = (uint8_t)(c.buttons >> (8 * i)); w[6 + i] = (uint8_t)(c.pressed >> (8 * i)); }
    const uint16_t qy = (uint16_t)(std::lround(c.aimYaw * kR) & 0xffff);
    const int16_t qp = (int16_t)std::lround(std::max(-1.5707963f, std::min(1.5707963f, c.aimPitch)) * kR);
    w[10] = (uint8_t)qy; w[11] = (uint8_t)(qy >> 8); w[12] = (uint8_t)qp; w[13] = (uint8_t)((uint16_t)qp >> 8);
    const float a[3] = {c.aimPoint.x, c.aimPoint.y, c.aimPoint.z};
    for (int k = 0; k < 3; ++k) {
        long v = std::lround(a[k] * 100.0f);
        v = std::max(-8388607L, std::min(8388607L, v));
        const uint32_t u = (uint32_t)v & 0xffffff;
        w[14 + 3 * k] = (uint8_t)u; w[15 + 3 * k] = (uint8_t)(u >> 8); w[16 + 3 * k] = (uint8_t)(u >> 16);
    }
    return w;
}

void decodeCommand(const WireCommand& w, uint32_t tick, game::RemoteCommand& c) {
    c = game::RemoteCommand{};
    c.tick = tick;
    c.moveForward = (int8_t)w[0]; c.moveRight = (int8_t)w[1];
    c.buttons = (uint32_t)w[2] | ((uint32_t)w[3] << 8) | ((uint32_t)w[4] << 16) | ((uint32_t)w[5] << 24);
    c.pressed = (uint32_t)w[6] | ((uint32_t)w[7] << 8) | ((uint32_t)w[8] << 16) | ((uint32_t)w[9] << 24);
    const uint16_t qy = (uint16_t)(w[10] | (w[11] << 8));
    const int16_t qp = (int16_t)(uint16_t)(w[12] | (w[13] << 8));
    c.aimYaw = std::remainder((float)qy / kR, 6.2831853f);   // as World::encodeLocalCommand decodes its own grid value
    c.aimPitch = (float)qp / kR;
    float a[3];
    for (int k = 0; k < 3; ++k) {
        uint32_t u = (uint32_t)w[14 + 3 * k] | ((uint32_t)w[15 + 3 * k] << 8) | ((uint32_t)w[16 + 3 * k] << 16);
        if (u & 0x800000u) u |= 0xff000000u;   // sign-extend the 24-bit value
        a[k] = (float)(int32_t)u / 100.0f;
    }
    c.aimPoint = core::Vec3{a[0], a[1], a[2]};
}

Lockstep::Lockstep(INetTransport& t, const Config& c) : t_(t), cfg_(c) {
    owner_.resize((size_t)cfg_.seats);
    for (int s = 0; s < cfg_.seats; ++s) {
        const int o = s < (int)cfg_.owner.size() ? cfg_.owner[(size_t)s] : s;
        owner_[(size_t)s] = o >= 0 && o < cfg_.seats ? o : s;
    }
    owned_ = ownedBy(cfg_.self);
    if (owned_ >= 0 && cfg_.lateJoin) { LOG_WARN("NET a late joiner cannot bring a second local seat (seat %d dropped)", owned_); owned_ = -1; }
    peerAck2_.assign((size_t)cfg_.seats, 0);
    cmds_.resize((size_t)cfg_.seats);
    contiguous_.assign((size_t)cfg_.seats, 0);
    peerAck_.assign((size_t)cfg_.seats, 0);
    eventsReceived_.assign((size_t)cfg_.seats, 0); eventsAnnounced_.resize((size_t)cfg_.seats);
    leadCur_.assign((size_t)cfg_.seats, kNoLead); leadPrev_.assign((size_t)cfg_.seats, kNoLead); leadReported_.assign((size_t)cfg_.seats, kNoLead);
    hello_.assign((size_t)cfg_.seats, 0);
    hello_[(size_t)cfg_.self] = 1;
    eventSeen_.resize((size_t)cfg_.seats);
    activeFrom_ = cfg_.activeFrom;
    activeFrom_.resize((size_t)cfg_.seats, 1);
    activeUntil_.assign((size_t)cfg_.seats, 0);
    for (int s = 0; s < cfg_.seats; ++s)
        if (activeFrom_[(size_t)s] == 0 || cfg_.lateJoin || !isPeer(s)) hello_[(size_t)s] = 1;   // reserved / owned seats, a late joiner: no HELLO
    lastFullMs_.assign((size_t)cfg_.seats, 0.0);
    delay_ = cfg_.delay > 0 ? cfg_.delay : 4;   // auto starts at 4 and follows the RTT
    if (!cfg_.logPath.empty()) {
        log_ = std::fopen(cfg_.logPath.c_str(), "w");
        if (log_) std::fprintf(log_, "tick,simhash,bundlehash,stall_ms,bytes_in,bytes_out,rtt_ms,wall_ms,delay\n");   // wall_ms: since the log opened
        logT0_ = nowMs();
    }
    std::vector<uint8_t> b; put8(b, kHello); put8(b, (uint8_t)cfg_.self); put64(b, cfg_.launchHash);
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self && isPeer(s)) t_.sendReliable(s, b.data(), b.size());
    if (cfg_.lateJoin && cfg_.self != 0) {   // join in progress: ask the host for the history
        std::vector<uint8_t> j; put8(j, kJoinReq); put8(j, (uint8_t)cfg_.self);
        t_.sendReliable(0, j.data(), j.size());
        LOG_INFO("NET late join: seat %d asks the host for the match history", cfg_.self);
    }
    LOG_INFO("NET lockstep: seat %d of %d, D %s%d, hash every %d, launch hash %016llx", cfg_.self, cfg_.seats, cfg_.delay > 0 ? "" : "auto from ",
             delay_, cfg_.hashEvery,
             (unsigned long long)cfg_.launchHash);
    for (int s = 0; s < cfg_.seats; ++s)
        if (!isPeer(s)) LOG_INFO("NET seat %d is a second local player of seat %d's peer%s", s, owner_[(size_t)s], s == owned_ ? " (this peer)" : "");
}

Lockstep::~Lockstep() { if (log_) std::fclose(log_); }

int Lockstep::ownedBy(int s) const {
    for (int o = 0; o < cfg_.seats; ++o) if (o != s && owner_[(size_t)o] == s) return o;
    return -1;
}

int Lockstep::peerCount() const {
    int n = 0;
    for (int s = 0; s < cfg_.seats; ++s) n += isPeer(s) ? 1 : 0;
    return n;
}

void Lockstep::fail(Status s, const std::string& why) {
    if (status_ != Status::Running) return;
    status_ = s; statusText_ = why;
    LOG_ERROR("NET %s", why.c_str());
}

bool Lockstep::everyoneHere() const {
    for (char h : hello_) if (!h) return false;
    return true;
}

void Lockstep::pump() {
    t_.service();
    NetPacket p;
    while (t_.poll(p)) handle(p);
    // Full windows (everything the peer has not acked): after 30 ms without any send (a stalled exchange must not deadlock), or when
    // an ack is overdue (older than 1.5 RTT + 10 ms since the last full window) - a loss the redundancy did not cover.
    if (localHigh_ > 0) {
        const double overdue = 1.5 * (double)std::max(srtt_, 20.0f) + 10.0;
        const bool idle = nowMs() - lastSendMs_ >= 30.0;
        for (int s = 0; s < cfg_.seats; ++s) {
            if (!peer(s)) continue;
            // overdue: the peer still lacks a tick I first sent more than `overdue` ago (and no full window went out since); my owned
            // seat's stream rides the same packets, so it lagging behind the main stream's ack is a loss too
            auto next = producedMs_.find(peerAck_[(size_t)s] + 1);
            const bool lostOwned = owned_ >= 0 && peerAck2_[(size_t)s] < std::min(peerAck_[(size_t)s], ownedHigh_);
            const bool lost = (lostOwned || (next != producedMs_.end() && nowMs() - next->second >= overdue)) &&
                              nowMs() - lastFullMs_[(size_t)s] >= overdue;
            if (idle || lost) sendCommands(s, true);
        }
    }
    // RTT probes every 250 ms to every peer (the auto input delay).
    if (nowMs() - lastPingMs_ >= 250.0) {
        lastPingMs_ = nowMs();
        std::vector<uint8_t> b; put8(b, kPing); put8(b, (uint8_t)cfg_.self); put64(b, (uint64_t)(nowMs() * 1000.0));
        for (int s = 0; s < cfg_.seats; ++s) if (peer(s)) t_.sendUnreliable(s, b.data(), b.size());
    }
    if (!feeds_.empty()) pumpFeeds();
    for (size_t i = 0; i < leaves_.size();) {   // the host: a leave is decided once every joined member reported (or 1 s passed)
        Leave& l = leaves_[i];
        bool all = true;
        for (int s = 1; s < cfg_.seats; ++s) if (s != l.seat && s != owner_[(size_t)l.seat] && isPeer(s) && joined(s) && !l.reports.count(s)) all = false;
        if (all || nowMs() - l.startMs >= kLeaveWaitMs) { finishLeave(l); leaves_.erase(leaves_.begin() + (long)i); continue; }
        ++i;
    }
}

bool Lockstep::seatActive(int seat, uint32_t tick) const {
    if (seat < 0 || seat >= cfg_.seats) return false;
    const uint32_t a = activeFrom_[(size_t)seat], u = activeUntil_[(size_t)seat];
    return a != 0 && tick >= a && (u == 0 || tick < u);
}

bool Lockstep::joined(int seat) const {
    return seat >= 0 && seat < cfg_.seats && activeFrom_[(size_t)seat] != 0 && activeUntil_[(size_t)seat] == 0;
}

// A member left mid-match (host only): ask every other member which of the leaver's commands it holds beyond ours.
void Lockstep::dropSeat(int seat) {
    if (cfg_.self != 0 || seat <= 0 || seat >= cfg_.seats || !joined(seat)) return;
    for (const Leave& l : leaves_) if (l.seat == seat) return;
    Leave l; l.seat = seat; l.startMs = nowMs();
    std::vector<uint8_t> b; put8(b, kLeaveQ); put8(b, (uint8_t)cfg_.self); put8(b, (uint8_t)seat); put32(b, contiguous_[(size_t)seat] + 1);
    for (int s = 1; s < cfg_.seats; ++s) if (s != seat && s != owner_[(size_t)seat] && peer(s)) t_.sendReliable(s, b.data(), b.size());
    leaves_.push_back(l);
    LOG_INFO("NET seat %d left mid-match: asking the members for its commands beyond tick %u", seat, contiguous_[(size_t)seat]);
    const int o = ownedBy(seat);
    if (o >= 0) dropSeat(o);   // its second local player leaves with it (the same protocol, its own agreed tick)
}

// Every report in (or the wait over): U = our contiguous tick of the leaver + 1 after the merge; each member gets what it lacks below U;
// then the SeatLeave event.
void Lockstep::finishLeave(Leave& l) {
    const int seat = l.seat;
    const uint32_t until = contiguous_[(size_t)seat] + 1;
    for (const auto& r : l.reports) {
        const uint32_t from = r.second + 1;
        if (from >= until) continue;
        std::vector<uint8_t> b; put8(b, kLeaveF); put8(b, (uint8_t)cfg_.self); put8(b, (uint8_t)seat); put32(b, from);
        const size_t nAt = b.size(); put8(b, 0); put8(b, 0);
        uint16_t n = 0;
        for (uint32_t t = from; t < until && n < 1000; ++t, ++n) {
            auto it = cmds_[(size_t)seat].find(t);
            if (it == cmds_[(size_t)seat].end()) break;
            b.insert(b.end(), it->second.begin(), it->second.end());
        }
        b[nAt] = (uint8_t)n; b[nAt + 1] = (uint8_t)(n >> 8);
        t_.sendReliable(r.first, b.data(), b.size());
    }
    std::vector<uint8_t> pl; pl.push_back((uint8_t)seat); put32(pl, until); pl.push_back(leaveToBot_ && leaveToBot_(seat) ? 1 : 0);
    submitEvent(kEventSeatLeave, pl);   // applies it here (applySeatLeave) and sends it to every joined seat
    LOG_INFO("NET seat %d leaves at tick %u (%zu member reports merged)", seat, until, l.reports.size());
}

void Lockstep::applySeatLeave(const Event& e) {
    if (e.payload.size() < 5) return;
    const int seat = e.payload[0];
    if (seat <= 0 || seat >= cfg_.seats || !joined(seat)) return;
    activeUntil_[(size_t)seat] = get32(&e.payload[1]);
    LOG_INFO("NET seat %d is empty from tick %u", seat, activeUntil_[(size_t)seat]);
}

// The host's history feed to each joining seat, paced (kFeedBytesPerMs): first every event it knows that this joiner has not had,
// then the tick bundles from the cursor up to the host's newest stepped tick (every seat active at the tick: u8 seat + 23 bytes),
// and its own state hashes up to there. The order on the reliable channel guarantees that an event stamped for tick T reaches the
// joiner before T's bundle (the host held the event before it could step T). Fed until the host has stepped the joiner's
// activeTick + kFeedPastActive (by then every peer sends to the joiner directly).
void Lockstep::pumpFeeds() {
    const double t = nowMs();
    feedBudget_ = std::min(65536.0, feedBudget_ + (feedLastMs_ > 0.0 ? (t - feedLastMs_) * kFeedBytesPerMs : 65536.0));
    feedLastMs_ = t;
    for (size_t f = 0; f < feeds_.size();) {
        Feed& fd = feeds_[f];
        const uint32_t act = activeFrom_[(size_t)fd.seat];
        if (act != 0 && hostStepped_ >= act + kFeedPastActive && fd.nextTick > hostStepped_) {
            LOG_INFO("NET late join: seat %d fed through tick %u (active from %u) - feed closed", fd.seat, fd.nextTick - 1, act);
            feeds_.erase(feeds_.begin() + (long)f);
            continue;
        }
        while (feedBudget_ > 0.0 && fd.nextEvent < eventLog_.size()) {   // events first
            const Event& e = eventLog_[fd.nextEvent++];
            std::vector<uint8_t> b; put8(b, kHist); put8(b, (uint8_t)cfg_.self); put8(b, kHistEvent);
            put8(b, (uint8_t)e.seat); put32(b, e.atTick); put8(b, e.kind); put32(b, e.seq);
            put8(b, (uint8_t)e.payload.size()); put8(b, (uint8_t)(e.payload.size() >> 8));
            b.insert(b.end(), e.payload.begin(), e.payload.end());
            t_.sendReliable(fd.seat, b.data(), b.size());
            feedBudget_ -= (double)b.size();
        }
        while (feedBudget_ > 0.0 && fd.nextTick <= hostStepped_) {   // tick bundles, as many as fit one datagram
            std::vector<uint8_t> b; put8(b, kHist); put8(b, (uint8_t)cfg_.self); put8(b, kHistTicks);
            put32(b, hostStepped_);   // the live target: READY only near it (not near what has trickled in so far)
            put32(b, fd.nextTick);
            const size_t nAt = b.size(); put8(b, 0);
            uint8_t n = 0;
            while (fd.nextTick <= hostStepped_ && n < 255) {
                std::vector<uint8_t> one; one.push_back(0);
                for (int s = 0; s < cfg_.seats; ++s) {
                    auto it = cmds_[(size_t)s].find(fd.nextTick);
                    if (!seatActive(s, fd.nextTick) || it == cmds_[(size_t)s].end()) continue;
                    one.push_back((uint8_t)s); one.insert(one.end(), it->second.begin(), it->second.end()); ++one[0];
                }
                if (n > 0 && b.size() + one.size() > kHistPacketBytes) break;
                b.insert(b.end(), one.begin(), one.end());
                ++n; ++fd.nextTick;
            }
            b[nAt] = n;
            t_.sendReliable(fd.seat, b.data(), b.size());
            feedBudget_ -= (double)b.size();
            for (auto it = hashLog_.lower_bound(fd.nextHashTick); it != hashLog_.end() && it->first < fd.nextTick; ++it) {
                std::vector<uint8_t> h; put8(h, kHist); put8(h, (uint8_t)cfg_.self); put8(h, kHistHash); put32(h, it->first); put64(h, it->second);
                t_.sendReliable(fd.seat, h.data(), h.size());
                fd.nextHashTick = it->first + 1;
            }
        }
        ++f;
    }
}

void Lockstep::reportCatchUp(uint32_t nextStep) {
    if (!cfg_.lateJoin || readySent_ || !histStarted_ || nextStep + kReadyTicks < hostLive_ || nextStep + kReadyTicks < histHigh_) return;
    readySent_ = true;
    std::vector<uint8_t> b; put8(b, kReady); put8(b, (uint8_t)cfg_.self); put32(b, nextStep);
    t_.sendReliable(0, b.data(), b.size());
    LOG_INFO("NET late join: caught up at tick %u (history at %u, host at %u) - READY", nextStep, histHigh_, hostLive_);
}

uint32_t Lockstep::activateSeat(int seat, int team) {
    std::vector<uint8_t> pl; pl.push_back((uint8_t)seat);
    const uint32_t at = localHigh_ + 1, active = at + kJoinLeadTicks;
    put32(pl, active); pl.push_back((uint8_t)team);
    submitEvent(kEventSeatJoin, pl);   // applies it here (applySeatJoin) and sends it to every active seat; the feed forwards it
    LOG_INFO("NET late join: seat %d joins at tick %u, its commands from tick %u, team %d", seat, at, active, team);
    return active;
}

void Lockstep::applySeatJoin(const Event& e) {
    if (e.payload.size() < 6) return;
    const int seat = e.payload[0];
    if (seat < 0 || seat >= cfg_.seats || joined(seat)) return;
    const uint32_t active = get32(&e.payload[1]);
    activeFrom_[(size_t)seat] = active;
    activeUntil_[(size_t)seat] = 0;
    hello_[(size_t)seat] = 1;
    LOG_INFO("NET seat %d is active from tick %u (joined at tick %u)", seat, active, e.atTick);
    if (seat == cfg_.self) {   // this joiner: ticks it already passed to submitLocal from activeTick on had no command - empty ones
        for (uint32_t t = active; t <= lastSubmitTick_; ++t) cmds_[(size_t)seat].emplace(t, encodeCommand(game::RemoteCommand{}));
        if (lastSubmitTick_ >= active) localHigh_ = std::max(localHigh_, lastSubmitTick_);
        uint32_t& h = contiguous_[(size_t)seat];
        h = std::max(h, active - 1);
        while (cmds_[(size_t)seat].count(h + 1)) ++h;
    }
}

void Lockstep::handle(const NetPacket& p) {
    const std::vector<uint8_t>& d = p.data;
    if (d.size() < 2 || p.from < 0 || p.from >= cfg_.seats) return;
    const int seat = p.from;
    switch (d[0]) {
    case kCmds: {
        if (d.size() < 11) return;
        const uint32_t ack = get32(&d[2]), first = get32(&d[6]);
        const size_t count = d[10];
        if (d.size() < 11 + count * kWireCommandBytes) return;
        peerAck_[(size_t)seat] = std::max(peerAck_[(size_t)seat], ack);
        auto& m = cmds_[(size_t)seat];
        for (size_t i = 0; i < count; ++i) {
            WireCommand w;
            std::copy(d.begin() + 11 + (long)(i * kWireCommandBytes), d.begin() + 11 + (long)((i + 1) * kWireCommandBytes), w.begin());
            const uint32_t tick = first + (uint32_t)i;
            if (!seatActive(seat, tick)) continue;   // a reserved seat / before its activeTick: not part of the match
            // a first arrival (resends keep the first copy) once stepping. The command the step is waiting for (tick == nextStep_)
            // reports how late it is: minus the ticks this peer has stalled on it, so the sender raises D by the whole shortfall at once
            // (lead 0 alone hid it: D then crept up one tick per report while the match stalled).
            if (m.emplace(tick, w).second && tick >= nextStep_ && nextStep_ > 1) {
                int lead = (int)std::min<int64_t>(kNoLead - 1, (int64_t)tick - (int64_t)nextStep_);
                if (lead == 0 && stallMs_ > 0.0) lead = -(int)std::ceil(stallMs_ / (1000.0 / 60.0));
                leadCur_[(size_t)seat] = std::min(leadCur_[(size_t)seat], std::max(lead, -100));
            }
        }
        uint32_t& c = contiguous_[(size_t)seat];
        if (activeFrom_[(size_t)seat] > 1) c = std::max(c, activeFrom_[(size_t)seat] - 1);
        while (m.count(c + 1)) ++c;
        if (count > 0 && d.size() >= 11 + count * kWireCommandBytes + 2) {   // the event announcement (absent from phase 1 peers)
            const size_t at = 11 + count * kWireCommandBytes;
            const uint32_t n = (uint32_t)d[at] | ((uint32_t)d[at + 1] << 8);
            uint32_t& a = eventsAnnounced_[(size_t)seat][first + (uint32_t)count - 1];
            a = std::max(a, n);
        }
        const size_t tr = 11 + count * kWireCommandBytes + 2;   // the split-screen trailer (after the announcement)
        if (d.size() >= tr + 11 && d[tr] == 0xA2) {
            if (owned_ >= 0) peerAck2_[(size_t)seat] = std::max(peerAck2_[(size_t)seat], get32(&d[tr + 1]));
            const int s2 = d[tr + 5];
            const uint32_t first2 = get32(&d[tr + 6]);
            const size_t count2 = d[tr + 10];
            if (s2 != 0xFF && s2 < cfg_.seats && owner_[(size_t)s2] == seat && s2 != seat && d.size() >= tr + 11 + count2 * kWireCommandBytes) {
                auto& m2 = cmds_[(size_t)s2];
                for (size_t i = 0; i < count2; ++i) {
                    const uint32_t tick = first2 + (uint32_t)i;
                    if (!seatActive(s2, tick)) continue;
                    WireCommand w;
                    std::copy(d.begin() + (long)(tr + 11 + i * kWireCommandBytes), d.begin() + (long)(tr + 11 + (i + 1) * kWireCommandBytes), w.begin());
                    m2.emplace(tick, w);
                }
                uint32_t& c2 = contiguous_[(size_t)s2];
                while (m2.count(c2 + 1)) ++c2;
            }
        }
        break;
    }
    case kEvent: {
        if (d.size() < 9) return;
        Event e;
        e.seat = seat; e.atTick = get32(&d[2]); e.kind = d[6];
        const size_t len = (size_t)d[7] | ((size_t)d[8] << 8);
        if (d.size() < 9 + len) return;
        e.payload.assign(d.begin() + 9, d.begin() + 9 + (long)len);
        e.seq = d.size() >= 13 + len ? get32(&d[9 + len]) : eventsReceived_[(size_t)seat];
        acceptEvent(std::move(e));
        break;
    }
    case kJoinReq: {   // the host: a reserved seat's joiner wants the history
        if (cfg_.self != 0 || activeFrom_[(size_t)seat] != 0) return;
        for (const Feed& f : feeds_) if (f.seat == seat) return;
        Feed f; f.seat = seat;
        feeds_.push_back(f);
        LOG_INFO("NET late join: seat %d asked for the history (host at tick %u, %zu events)", seat, hostStepped_, eventLog_.size());
        break;
    }
    case kReady: {
        if (cfg_.self != 0) return;
        for (Feed& f : feeds_)
            if (f.seat == seat && !f.ready) {
                f.ready = true;
                LOG_INFO("NET late join: seat %d READY at tick %u (host at %u)", seat, d.size() >= 6 ? get32(&d[2]) : 0, hostStepped_);
                if (onJoinReady_) onJoinReady_(seat);
            }
        break;
    }
    case kLeaveQ: {   // a member: report the leaver's commands we hold from `from` on
        if (seat != 0 || d.size() < 7) return;
        const int lv = d[2];
        if (lv <= 0 || lv >= cfg_.seats) return;
        const uint32_t from = get32(&d[3]), have = contiguous_[(size_t)lv];
        std::vector<uint8_t> b; put8(b, kLeaveR); put8(b, (uint8_t)cfg_.self); put8(b, (uint8_t)lv); put32(b, have); put32(b, from);
        const size_t nAt = b.size(); put8(b, 0); put8(b, 0);
        uint16_t n = 0;
        for (uint32_t t = from; t <= have && n < 1000; ++t, ++n) {
            auto it = cmds_[(size_t)lv].find(t);
            if (it == cmds_[(size_t)lv].end()) break;
            b.insert(b.end(), it->second.begin(), it->second.end());
        }
        b[nAt] = (uint8_t)n; b[nAt + 1] = (uint8_t)(n >> 8);
        t_.sendReliable(0, b.data(), b.size());
        break;
    }
    case kLeaveR: {   // the host: a member's report
        if (cfg_.self != 0 || d.size() < 13) return;   // a report with no commands is 13 bytes
        const int lv = d[2];
        const uint32_t have = get32(&d[3]), first = get32(&d[7]);
        const size_t n = (size_t)d[11] | ((size_t)d[12] << 8);
        if (lv <= 0 || lv >= cfg_.seats || d.size() < 13 + n * kWireCommandBytes) return;
        for (size_t i = 0; i < n; ++i) {
            WireCommand w; std::copy(d.begin() + (long)(13 + i * kWireCommandBytes), d.begin() + (long)(13 + (i + 1) * kWireCommandBytes), w.begin());
            cmds_[(size_t)lv].emplace(first + (uint32_t)i, w);
        }
        uint32_t& c = contiguous_[(size_t)lv];
        while (cmds_[(size_t)lv].count(c + 1)) ++c;
        for (Leave& l : leaves_) if (l.seat == lv) l.reports[seat] = have;
        break;
    }
    case kLeaveF: {   // a member: the leaver's commands we lacked below the agreed tick
        if (seat != 0 || d.size() < 9) return;
        const int lv = d[2];
        const uint32_t first = get32(&d[3]);
        const size_t n = (size_t)d[7] | ((size_t)d[8] << 8);
        if (lv <= 0 || lv >= cfg_.seats || d.size() < 9 + n * kWireCommandBytes) return;
        for (size_t i = 0; i < n; ++i) {
            WireCommand w; std::copy(d.begin() + (long)(9 + i * kWireCommandBytes), d.begin() + (long)(9 + (i + 1) * kWireCommandBytes), w.begin());
            cmds_[(size_t)lv].emplace(first + (uint32_t)i, w);
        }
        uint32_t& c = contiguous_[(size_t)lv];
        while (cmds_[(size_t)lv].count(c + 1)) ++c;
        break;
    }
    case kHist: {   // the joiner: the host's history feed
        if (!cfg_.lateJoin || seat != 0 || d.size() < 3) return;
        histStarted_ = true;
        const uint8_t sub = d[2];
        if (sub == kHistTicks && d.size() >= 12) {
            hostLive_ = std::max(hostLive_, get32(&d[3]));
            uint32_t tick = get32(&d[7]);
            const int n = d[11];
            size_t at = 12;
            for (int k = 0; k < n && at < d.size(); ++k, ++tick) {
                const int cnt = d[at++];
                for (int j = 0; j < cnt && at + 1 + kWireCommandBytes <= d.size(); ++j) {
                    const int s = d[at];
                    WireCommand w; std::copy(d.begin() + (long)at + 1, d.begin() + (long)(at + 1 + kWireCommandBytes), w.begin());
                    at += 1 + kWireCommandBytes;
                    if (s < 0 || s >= cfg_.seats || s == cfg_.self) continue;
                    cmds_[(size_t)s].emplace(tick, w);
                    uint32_t& c = contiguous_[(size_t)s];
                    while (cmds_[(size_t)s].count(c + 1)) ++c;
                }
                histHigh_ = std::max(histHigh_, tick);
            }
        } else if (sub == kHistEvent && d.size() >= 14) {
            Event e; e.seat = d[3]; e.atTick = get32(&d[4]); e.kind = d[8]; e.seq = get32(&d[9]);
            const size_t len = (size_t)d[13] | (d.size() > 14 ? (size_t)d[14] << 8 : 0);
            if (d.size() < 15 + len || e.seat < 0 || e.seat >= cfg_.seats) return;
            e.payload.assign(d.begin() + 15, d.begin() + 15 + (long)len);
            acceptEvent(std::move(e));
        } else if (sub == kHistHash && d.size() >= 15) {
            peerHash_[get32(&d[3])].push_back({0, get64(&d[7])});
        }
        break;
    }
    case kHello: {
        if (d.size() < 10) return;
        const uint64_t h = get64(&d[2]);
        if (h != cfg_.launchHash) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "launch mismatch with seat %d (%016llx vs ours %016llx): the match settings changed", seat,
                          (unsigned long long)h, (unsigned long long)cfg_.launchHash);
            fail(Status::LaunchMismatch, buf);
        }
        if (!hello_[(size_t)seat]) LOG_INFO("NET seat %d here", seat);
        hello_[(size_t)seat] = 1;
        break;
    }
    case kHash: {
        if (d.size() < 14) return;
        const uint32_t tick = get32(&d[2]);
        const uint64_t h = get64(&d[6]);
        auto own = ownHash_.find(tick);
        if (own != ownHash_.end() && own->second != h) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "DESYNC at tick %u: seat %d %016llx, ours (seat %d) %016llx", tick, seat, (unsigned long long)h,
                          cfg_.self, (unsigned long long)own->second);
            fail(Status::Desync, buf);
        } else if (own == ownHash_.end()) {
            peerHash_[tick].push_back({seat, h});
        }
        break;
    }
    case kVoice: {
        if (d.size() < 5) return;
        const int talker = d[1] < cfg_.seats ? d[1] : seat;
        if (cfg_.self == 0 && seat != 0) fanOutVoice(talker, d.data(), d.size());   // the host relays a client's frame
        if (voiceSink_) voiceSink_(talker, d.data(), d.size());
        break;
    }
    case kPing: {
        if (d.size() < 10) return;
        std::vector<uint8_t> b(d.begin(), d.begin() + 10);
        b[0] = kPong; b[1] = (uint8_t)cfg_.self;
        b.push_back((uint8_t)(int8_t)std::max(-128, std::min(kNoLead, leadFrom(seat))));   // the lead we saw from the pinger's commands
        t_.sendUnreliable(seat, b.data(), b.size());
        break;
    }
    case kPong: {
        if (d.size() < 10) return;
        const float r = (float)(nowMs() - (double)get64(&d[2]) / 1000.0);
        if (r < 0.0f || r > 5000.0f) return;
        if (srtt_ <= 0.0f) { srtt_ = r; rttvar_ = r * 0.5f; }   // RFC 6298 smoothing
        else { rttvar_ = 0.75f * rttvar_ + 0.25f * std::fabs(srtt_ - r); srtt_ = 0.875f * srtt_ + 0.125f * r; }
        if (d.size() >= 11) leadReported_[(size_t)seat] = (int)(int8_t)d[10];   // older peers send no lead byte
        updateDelay();
        break;
    }
    case kBye: {
        const uint32_t last = d.size() >= 6 ? get32(&d[2]) : 0;
        LOG_INFO("NET seat %d left (its last tick %u)", seat, last);
        if (seat != 0 && peerCount() > 2) { dropSeat(seat); break; }   // a member: the host runs the leave protocol; the match goes on
        if (last > 0) endTick_ = endTick_ ? std::min(endTick_, last) : last;   // finish up to there, then end normally
        else if (seat == 0) fail(Status::HostLost, "the host left");
        break;
    }
    default: break;
    }
}

void Lockstep::updateDelay() {
    if (cfg_.delay > 0) return;
    const float tickMs = 1000.0f / 60.0f;
    int want = std::max(2, std::min(15, (int)std::ceil((srtt_ * 0.5f + 4.0f * rttvar_ + 5.0f) / tickMs)));
    // A peer that saw our commands arrive with lead < 1 (late / just in time: the path to it is slower than srtt / 2) needs D up by
    // the shortfall (and a 2-tick margin); the 1 / s fall waits until every reporting peer sees a lead >= 4.
    int minLead = kNoLead;
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) minLead = std::min(minLead, leadReported_[(size_t)s]);
    // hysteresis: keep a 2-tick margin (jitter ~10 ms is under a tick, a lead of 0 / 1 stalls on it); fall only at lead >= 4
    if (minLead < 2) want = std::max(want, std::min(15, delay_ + (2 - minLead)));
    if (want > delay_) {
        LOG_INFO("NET input delay %d -> %d (rtt %.1f ms, var %.1f, peer lead %d)", delay_, want, srtt_, rttvar_, minLead == kNoLead ? -1 : minLead);
        delay_ = want; lastDelayDropMs_ = nowMs();
        for (int& l : leadReported_) l = kNoLead;   // re-measured at the new D
    } else if (want < delay_ && minLead >= 4 && nowMs() - lastDelayDropMs_ >= 1000.0) {
        --delay_; lastDelayDropMs_ = nowMs(); LOG_INFO("NET input delay -> %d (rtt %.1f ms, peer lead %d)", delay_, srtt_, minLead == kNoLead ? -1 : minLead);
    }
}

void Lockstep::sendCommands(int peer, bool full) {
    // Full: from the tick after the peer's ack (their contiguous copy of my seat) to my latest, at most max(maxResend, 2D + 4) ticks: a
    // peer is never more than about 2D + 2 ticks behind my latest command, so the window always covers what it still needs.
    // Regular: the newest kRedundancy ticks not yet acked (the ack-overdue full send in pump() repairs anything older).
    const uint32_t cap = full ? (uint32_t)std::max(cfg_.maxResend, 2 * delay_ + 4) : kRedundancy;
    uint32_t first = peerAck_[(size_t)peer] + 1;
    if (localHigh_ >= cap && first + cap <= localHigh_) first = localHigh_ - cap + 1;
    if (full) lastFullMs_[(size_t)peer] = nowMs();
    if (first > localHigh_) first = localHigh_;   // nothing new: repeat the latest (keeps the ack flowing back)
    const auto& mine = cmds_[(size_t)cfg_.self];
    std::vector<uint8_t> b;
    put8(b, kCmds); put8(b, (uint8_t)cfg_.self); put32(b, contiguous_[(size_t)peer]); put32(b, first);
    const size_t countAt = b.size(); put8(b, 0);
    uint8_t count = 0;
    for (uint32_t t = first; t <= localHigh_ && count < 255; ++t) {
        auto it = mine.find(t);
        if (it == mine.end()) break;
        b.insert(b.end(), it->second.begin(), it->second.end());
        ++count;
    }
    b[countAt] = count;
    {   // [phase 2] the event announcement: my events stamped at or before the packet's newest tick (u16; read when count > 0)
        const uint32_t last = count > 0 ? first + count - 1 : 0;
        const size_t n = count > 0 ? (size_t)(std::upper_bound(ownEventTicks_.begin(), ownEventTicks_.end(), last) - ownEventTicks_.begin()) : 0;
        b.push_back((uint8_t)n); b.push_back((uint8_t)(n >> 8));
    }
    appendOwned(b, peer, full);   // the split-screen trailer: my ack of the peer's owned seat + my owned seat's commands
    t_.sendUnreliable(peer, b.data(), b.size());
    lastSendMs_ = nowMs();
}

void Lockstep::appendOwned(std::vector<uint8_t>& b, int peer, bool full) const {
    const int theirs = ownedBy(peer);
    put8(b, 0xA2); put32(b, theirs >= 0 ? contiguous_[(size_t)theirs] : 0);
    if (owned_ < 0 || ownedHigh_ == 0) { put8(b, 0xFF); put32(b, 0); put8(b, 0); return; }
    const uint32_t cap = full ? (uint32_t)std::max(cfg_.maxResend, 2 * delay_ + 4) : kRedundancy;
    uint32_t first = peerAck2_[(size_t)peer] + 1;
    if (ownedHigh_ >= cap && first + cap <= ownedHigh_) first = ownedHigh_ - cap + 1;
    if (first > ownedHigh_) first = ownedHigh_;
    put8(b, (uint8_t)owned_); put32(b, first);
    const size_t countAt = b.size(); put8(b, 0);
    uint8_t count = 0;
    const auto& mine = cmds_[(size_t)owned_];
    for (uint32_t t = first; t <= ownedHigh_ && count < 255; ++t) {
        auto it = mine.find(t);
        if (it == mine.end()) break;
        b.insert(b.end(), it->second.begin(), it->second.end());
        ++count;
    }
    b[countAt] = count;
}

void Lockstep::submitOwned(uint32_t tick, const game::RemoteCommand& c) {
    if (owned_ < 0 || !seatActive(owned_, tick)) return;
    cmds_[(size_t)owned_][tick] = encodeCommand(c);
    ownedHigh_ = std::max(ownedHigh_, tick);
    uint32_t& h = contiguous_[(size_t)owned_];
    while (cmds_[(size_t)owned_].count(h + 1)) ++h;
}

void Lockstep::submitLocal(uint32_t tick, const game::RemoteCommand& c) {
    lastSubmitTick_ = std::max(lastSubmitTick_, tick);
    if (!seatActive(cfg_.self, tick)) return;   // a reserved / late seat before its activeTick sends nothing
    cmds_[(size_t)cfg_.self][tick] = encodeCommand(c);
    localHigh_ = std::max(localHigh_, tick);
    producedMs_.emplace(tick, nowMs());
    {   // forget send times every peer has acked
        uint32_t minAck = localHigh_;
        for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self && isPeer(s)) minAck = std::min(minAck, peerAck_[(size_t)s]);
        while (!producedMs_.empty() && producedMs_.begin()->first <= minAck) producedMs_.erase(producedMs_.begin());
    }
    uint32_t& h = contiguous_[(size_t)cfg_.self];
    while (cmds_[(size_t)cfg_.self].count(h + 1)) ++h;
    for (int s = 0; s < cfg_.seats; ++s) if (peer(s)) sendCommands(s, false);
}

bool Lockstep::haveAll(uint32_t tick) const {
    if (cfg_.lateJoin) {   // the history holds the events stamped for the tick (no announcements reach the joiner before it joins)
        const uint32_t a = activeFrom_[(size_t)cfg_.self];
        if ((a == 0 || tick < a + kFeedPastActive / 2) && tick > histHigh_) return false;
    }
    for (int s = 0; s < cfg_.seats; ++s) if (seatActive(s, tick) && !cmds_[(size_t)s].count(tick)) return false;
    for (int s = 0; s < cfg_.seats; ++s) {   // every event stamped for this tick (or earlier) arrived
        if (s == cfg_.self || activeFrom_[(size_t)s] == 0 || (activeUntil_[(size_t)s] != 0 && tick >= activeUntil_[(size_t)s])) continue;   // a leaver: its events up to U still count
        const auto& a = eventsAnnounced_[(size_t)s];
        auto it = a.lower_bound(tick);   // the announcement of a packet that covered this tick
        if (it != a.end() && eventsReceived_[(size_t)s] < it->second) return false;
    }
    return true;
}

uint32_t Lockstep::submitEvent(uint8_t kind, const std::vector<uint8_t>& payload) {
    const uint32_t at = localHigh_ + 1;   // the next command this seat submits: every peer gets the event before it can step it
    const uint32_t seq = (uint32_t)ownEventTicks_.size();
    ownEventTicks_.push_back(at);
    std::vector<uint8_t> b;
    put8(b, kEvent); put8(b, (uint8_t)cfg_.self); put32(b, at); put8(b, kind);
    const size_t len = std::min<size_t>(payload.size(), 65535);
    put8(b, (uint8_t)len); put8(b, (uint8_t)(len >> 8));
    b.insert(b.end(), payload.begin(), payload.begin() + (long)len);
    put32(b, seq);
    for (int s = 0; s < cfg_.seats; ++s) if (peer(s)) t_.sendReliable(s, b.data(), b.size());
    Event e; e.seat = cfg_.self; e.atTick = at; e.kind = kind; e.seq = seq; e.payload.assign(payload.begin(), payload.begin() + (long)len);
    acceptEvent(std::move(e));
    return at;
}

// Every event (own, received, or from the history feed) once per (seat, seq): counted, logged for joiners (the host), SeatJoin
// applied to the seat table, then queued for the driver.
void Lockstep::acceptEvent(Event e) {
    if (e.seat < 0 || e.seat >= cfg_.seats) return;
    std::vector<char>& seen = eventSeen_[(size_t)e.seat];
    if (e.seq < seen.size() && seen[e.seq]) return;
    if (e.seq >= seen.size()) seen.resize((size_t)e.seq + 1, 0);
    seen[e.seq] = 1;
    if (e.seat != cfg_.self) ++eventsReceived_[(size_t)e.seat];
    if (cfg_.keepHistory) eventLog_.push_back(e);
    if (e.kind == kEventSeatJoin && e.seat == 0) applySeatJoin(e);
    if (e.kind == kEventSeatLeave && e.seat == 0) applySeatLeave(e);
    eventQueue_.push_back(std::move(e));
}

bool Lockstep::pollEvent(Event& out) {
    if (eventQueue_.empty()) return false;
    out = std::move(eventQueue_.front());
    eventQueue_.pop_front();
    return true;
}

void Lockstep::command(int seat, uint32_t tick, game::RemoteCommand& out) const {
    const auto& m = cmds_[(size_t)seat];
    auto it = m.find(tick);
    if (it == m.end()) { out = game::RemoteCommand{}; out.tick = tick; return; }
    decodeCommand(it->second, tick, out);
}

bool Lockstep::wire(int seat, uint32_t tick, WireCommand& out) const {
    const auto& m = cmds_[(size_t)seat];
    auto it = m.find(tick);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

uint64_t Lockstep::bundleHash(uint32_t tick) const {
    uint64_t h = 1469598103934665603ULL;
    for (const auto& m : cmds_) {
        auto it = m.find(tick);
        if (it == m.end()) continue;
        for (uint8_t b : it->second) { h ^= b; h *= 1099511628211ULL; }
    }
    return h;
}

void Lockstep::reportHash(uint32_t tick, uint64_t h) {
    if (cfg_.hashEvery <= 0 || tick % (uint32_t)cfg_.hashEvery != 0) return;
    ownHash_[tick] = h;
    if (cfg_.keepHistory) hashLog_[tick] = h;
    std::vector<uint8_t> b; put8(b, kHash); put8(b, (uint8_t)cfg_.self); put32(b, tick); put64(b, h);
    if (seatActive(cfg_.self, tick))   // a joiner still catching up only checks (against the history): it sends no hashes yet
        for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self && joined(s)) t_.sendReliable(s, b.data(), b.size());
    auto it = peerHash_.find(tick);
    if (it != peerHash_.end()) {
        for (const auto& ph : it->second)
            if (ph.second != h) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "DESYNC at tick %u: seat %d %016llx, ours (seat %d) %016llx", tick, ph.first,
                              (unsigned long long)ph.second, cfg_.self, (unsigned long long)h);
                fail(Status::Desync, buf);
            }
        peerHash_.erase(it);
    }
    while (ownHash_.size() > 512) ownHash_.erase(ownHash_.begin());
}

void Lockstep::stalled(double ms) {
    stallMs_ += ms; stallTotalMs_ += ms;
    if (stallMs_ >= cfg_.peerTimeoutS * 1000.0) {
        std::string missing;
        for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) missing += " " + std::to_string(s) + "@" + std::to_string(contiguous_[(size_t)s]);
        fail(Status::PeerTimeout, "peer timeout: no commands for " + std::to_string((int)(stallMs_ / 1000.0)) + " s (seat@tick:" + missing + ")");
    }
}

int Lockstep::leadFrom(int seat) const {
    return std::min(leadCur_[(size_t)seat], leadPrev_[(size_t)seat]);
}

void Lockstep::stepped(uint32_t tick, uint64_t stateHash) {
    nextStep_ = tick + 1;
    hostStepped_ = std::max(hostStepped_, tick);
    if (nowMs() - leadRollMs_ >= 1000.0) { leadRollMs_ = nowMs(); leadPrev_ = leadCur_; std::fill(leadCur_.begin(), leadCur_.end(), kNoLead); }
    if (log_) {
        const NetStats st = t_.stats();
        std::fprintf(log_, "%u,%016llx,%016llx,%.2f,%llu,%llu,%.1f,%.1f,%d\n", tick, (unsigned long long)stateHash, (unsigned long long)bundleHash(tick),
                     stallMs_, (unsigned long long)st.bytesReceived, (unsigned long long)st.bytesSent, st.rttMs, nowMs() - logT0_, delay_);
    }
    stallMs_ = 0.0;
}

void Lockstep::forget(uint32_t belowTick) {
    if (!cfg_.keepHistory)
        for (auto& m : cmds_) while (!m.empty() && m.begin()->first < belowTick) m.erase(m.begin());
    for (auto& a : eventsAnnounced_) while (!a.empty() && a.begin()->first < belowTick) a.erase(a.begin());
}

void Lockstep::sendVoice(const uint8_t* p, size_t n) {
    if (cfg_.self != 0) { t_.sendUnreliable(0, p, n); return; }   // a client: to the host, which fans out
    fanOutVoice(0, p, n);                                         // the host's own voice
}

void Lockstep::fanOutVoice(int talker, const uint8_t* p, size_t n) {
    const uint8_t flags = n > 4 ? p[4] : 0;
    for (int s = 0; s < cfg_.seats; ++s) {
        if (s == talker || !peer(s)) continue;
        if (voiceAudience_ && !voiceAudience_(talker, s, flags)) continue;
        t_.sendUnreliable(s, p, n);
        if (talker != cfg_.self) ++voiceRelayed_;
    }
}

void Lockstep::bye(uint32_t lastTick) {
    std::vector<uint8_t> b; put8(b, kBye); put8(b, (uint8_t)cfg_.self); put32(b, lastTick);
    for (int s = 0; s < cfg_.seats; ++s) if (peer(s)) t_.sendReliable(s, b.data(), b.size());
    for (int i = 0; i < 20; ++i) t_.service();   // flush best-effort (the peers' acks may not come back before exit)
    const NetStats st = t_.stats();
    LOG_INFO("NET bye: sent %llu B / %llu pkts, received %llu B / %llu pkts, resends %llu, rtt %.1f ms, stalled %.0f ms total, D %d",
             (unsigned long long)st.bytesSent, (unsigned long long)st.packetsSent, (unsigned long long)st.bytesReceived,
             (unsigned long long)st.packetsReceived, (unsigned long long)st.resends, srtt_ > 0.0f ? srtt_ : st.rttMs, stallTotalMs_, delay_);
}

}  // namespace net
