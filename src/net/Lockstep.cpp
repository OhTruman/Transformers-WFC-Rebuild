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

enum : uint8_t { kCmds = 1, kHello = 2, kHash = 3, kBye = 4, kPing = 5, kPong = 6 };

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
    cmds_.resize((size_t)cfg_.seats);
    contiguous_.assign((size_t)cfg_.seats, 0);
    peerAck_.assign((size_t)cfg_.seats, 0);
    hello_.assign((size_t)cfg_.seats, 0);
    hello_[(size_t)cfg_.self] = 1;
    delay_ = cfg_.delay > 0 ? cfg_.delay : 4;   // auto starts at 4 and follows the RTT
    if (!cfg_.logPath.empty()) {
        log_ = std::fopen(cfg_.logPath.c_str(), "w");
        if (log_) std::fprintf(log_, "tick,simhash,bundlehash,stall_ms,bytes_in,bytes_out,rtt_ms\n");
    }
    std::vector<uint8_t> b; put8(b, kHello); put8(b, (uint8_t)cfg_.self); put64(b, cfg_.launchHash);
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) t_.sendReliable(s, b.data(), b.size());
    LOG_INFO("NET lockstep: seat %d of %d, D %s%d, hash every %d, launch hash %016llx", cfg_.self, cfg_.seats, cfg_.delay > 0 ? "" : "auto from ",
             delay_, cfg_.hashEvery,
             (unsigned long long)cfg_.launchHash);
}

Lockstep::~Lockstep() { if (log_) std::fclose(log_); }

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
    // The unacked commands again after 30 ms without a send (a lost packet or a stalled peer must not deadlock the exchange).
    if (localHigh_ > 0 && nowMs() - lastSendMs_ >= 30.0)
        for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) sendCommands(s);
    // RTT probes every 250 ms to every peer (the auto input delay).
    if (nowMs() - lastPingMs_ >= 250.0) {
        lastPingMs_ = nowMs();
        std::vector<uint8_t> b; put8(b, kPing); put8(b, (uint8_t)cfg_.self); put64(b, (uint64_t)(nowMs() * 1000.0));
        for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) t_.sendUnreliable(s, b.data(), b.size());
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
            m.emplace(first + (uint32_t)i, w);   // duplicates / resends: the first copy stays (they are identical)
        }
        uint32_t& c = contiguous_[(size_t)seat];
        while (m.count(c + 1)) ++c;
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
    case kPing: {
        if (d.size() < 10) return;
        std::vector<uint8_t> b(d.begin(), d.begin() + 10);
        b[0] = kPong; b[1] = (uint8_t)cfg_.self;
        t_.sendUnreliable(seat, b.data(), b.size());
        break;
    }
    case kPong: {
        if (d.size() < 10) return;
        const float r = (float)(nowMs() - (double)get64(&d[2]) / 1000.0);
        if (r < 0.0f || r > 5000.0f) return;
        if (srtt_ <= 0.0f) { srtt_ = r; rttvar_ = r * 0.5f; }   // RFC 6298 smoothing
        else { rttvar_ = 0.75f * rttvar_ + 0.25f * std::fabs(srtt_ - r); srtt_ = 0.875f * srtt_ + 0.125f * r; }
        updateDelay();
        break;
    }
    case kBye:
        LOG_INFO("NET seat %d left", seat);
        if (seat == 0) fail(Status::HostLost, "the host left");
        break;
    default: break;
    }
}

void Lockstep::updateDelay() {
    if (cfg_.delay > 0) return;
    const float tickMs = 1000.0f / 60.0f;
    const int want = std::max(2, std::min(15, (int)std::ceil((srtt_ * 0.5f + 4.0f * rttvar_ + 5.0f) / tickMs)));
    if (want > delay_) { LOG_INFO("NET input delay %d -> %d (rtt %.1f ms, var %.1f)", delay_, want, srtt_, rttvar_); delay_ = want; lastDelayDropMs_ = nowMs(); }
    else if (want < delay_ && nowMs() - lastDelayDropMs_ >= 1000.0) { --delay_; lastDelayDropMs_ = nowMs(); LOG_INFO("NET input delay -> %d (rtt %.1f ms)", delay_, srtt_); }
}

void Lockstep::sendCommands(int peer) {
    // From the tick after the peer's ack (their contiguous copy of my seat) to my latest, at most max(maxResend, 2D + 4) ticks: a peer
    // is never more than about 2D + 2 ticks behind my latest command, so the window always covers what it still needs.
    const uint32_t cap = (uint32_t)std::max(cfg_.maxResend, 2 * delay_ + 4);
    uint32_t first = peerAck_[(size_t)peer] + 1;
    if (localHigh_ >= cap && first + cap <= localHigh_) first = localHigh_ - cap + 1;
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
    t_.sendUnreliable(peer, b.data(), b.size());
    lastSendMs_ = nowMs();
}

void Lockstep::submitLocal(uint32_t tick, const game::RemoteCommand& c) {
    cmds_[(size_t)cfg_.self][tick] = encodeCommand(c);
    localHigh_ = std::max(localHigh_, tick);
    uint32_t& h = contiguous_[(size_t)cfg_.self];
    while (cmds_[(size_t)cfg_.self].count(h + 1)) ++h;
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) sendCommands(s);
}

bool Lockstep::haveAll(uint32_t tick) const {
    for (const auto& m : cmds_) if (!m.count(tick)) return false;
    return true;
}

void Lockstep::command(int seat, uint32_t tick, game::RemoteCommand& out) const {
    const auto& m = cmds_[(size_t)seat];
    auto it = m.find(tick);
    if (it == m.end()) { out = game::RemoteCommand{}; out.tick = tick; return; }
    decodeCommand(it->second, tick, out);
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
    std::vector<uint8_t> b; put8(b, kHash); put8(b, (uint8_t)cfg_.self); put32(b, tick); put64(b, h);
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) t_.sendReliable(s, b.data(), b.size());
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

void Lockstep::stepped(uint32_t tick, uint64_t stateHash) {
    if (log_) {
        const NetStats st = t_.stats();
        std::fprintf(log_, "%u,%016llx,%016llx,%.2f,%llu,%llu,%.1f\n", tick, (unsigned long long)stateHash, (unsigned long long)bundleHash(tick),
                     stallMs_, (unsigned long long)st.bytesReceived, (unsigned long long)st.bytesSent, st.rttMs);
    }
    stallMs_ = 0.0;
}

void Lockstep::forget(uint32_t belowTick) {
    for (auto& m : cmds_) while (!m.empty() && m.begin()->first < belowTick) m.erase(m.begin());
}

void Lockstep::bye() {
    std::vector<uint8_t> b; put8(b, kBye); put8(b, (uint8_t)cfg_.self);
    for (int s = 0; s < cfg_.seats; ++s) if (s != cfg_.self) t_.sendReliable(s, b.data(), b.size());
    for (int i = 0; i < 20; ++i) t_.service();   // flush best-effort (the peers' acks may not come back before exit)
    const NetStats st = t_.stats();
    LOG_INFO("NET bye: sent %llu B / %llu pkts, received %llu B / %llu pkts, resends %llu, rtt %.1f ms, stalled %.0f ms total, D %d",
             (unsigned long long)st.bytesSent, (unsigned long long)st.packetsSent, (unsigned long long)st.bytesReceived,
             (unsigned long long)st.packetsReceived, (unsigned long long)st.resends, srtt_ > 0.0f ? srtt_ : st.rttMs, stallTotalMs_, delay_);
}

}  // namespace net
