// [Systems] UdpSession - see UdpSession.h. Wire (little endian; strings u16 length + bytes):
//   out of band (INetTransport::sendTo / pollStranger, before the host seats a member):
//     0x50 JOIN      u32 protocol, member info            0x51 JOIN_OK u8 seat            0x52 JOIN_FAIL u8 SessionJoinFailure
//   reliable channel, seated (types 0x40-0x4B; the lockstep's own packets are 1-7 on the same socket):
//     0x40 LOBBY     lobby (host -> members; the first one completes a join)
//     0x41 PEERS     u8 n, n x {u8 seat, str fingerprint, str address} (host -> members: who to reach directly)
//     0x42 INFO      member info (member -> host: MemberJoined on the host)
//     0x43 LAUNCH    str url, u32 seed, lobby roster, u8 n, n x u8 session seat (match seat order) -> CountdownStarted
//     0x44 LOADED    u64 simId, u64 loadHash (member -> host)
//     0x45 PEERLOAD  str fingerprint (host -> members)      0x46 ALLLOADED u32 start tick
//     0x47 LEAVE     (member -> host: it left; host -> members: the host ended the session)
//     0x48 KICK      u8 SessionKickReason (host -> member)  0x49 DROPPED str fingerprint, u8 SessionLeaveReason, u8 inMatch
//     0x4A VERSION   (host -> member: its load hash differs; JoinFailed{Version})
//     0x4B RETURN    (host -> members: the match ended; a LOBBY follows)
//   unreliable: 0x4F KEEPALIVE (every 1 s to every peer; also a LEAVE / KICK copy goes unreliable twice before a socket closes)
#include "net/UdpSession.h"

#include "core/Log.h"
#include "net/NetTransport.h"
#include "assets/Json.h"

#include <future>
#include <thread>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>

namespace net {
namespace {

using namespace frontend;
constexpr uint32_t kProtocol = 0x57464303;   // "WFC" v3 (the map vote fields appended to the member / lobby records)
enum : uint8_t {
    kJoin = 0x50, kJoinOk = 0x51, kJoinFail = 0x52,
    kLobby = 0x40, kPeers = 0x41, kInfo = 0x42, kLaunch = 0x43, kLoaded = 0x44, kPeerLoaded = 0x45, kAllLoaded = 0x46,
    kLeave = 0x47, kKick = 0x48, kDropped = 0x49, kVersion = 0x4A, kReturn = 0x4B, kKeepalive = 0x4F,
    kDiscover = 0x53, kAnnounce = 0x54, kPing = 0x55, kPong = 0x56,   // out of band: LAN discovery, candidate pings
};
constexpr double kSearchCollectS = 1.0, kPingRoundS = 0.3;
constexpr int kPingRounds = 3, kMaxPingMs = 250;
std::string versionTag() { char b[64]; std::snprintf(b, sizeof b, "%08x-%s", kProtocol, __DATE__); return b; }
std::string jsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if ((unsigned char)c >= 0x20) o += c; }
    return o;
}
bool isSessionType(uint8_t t) { return t >= 0x40 && t <= 0x4F; }

struct W {
    std::vector<uint8_t> b;
    explicit W(uint8_t type) { b.push_back(type); }
    void u8(int v) { b.push_back((uint8_t)v); }
    void u16(int v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
    void i32(int v) { u32((uint32_t)v); }
    void str(const std::string& s) { const size_t n = std::min<size_t>(s.size(), 65535); u16((int)n); b.insert(b.end(), s.begin(), s.begin() + (long)n); }
    void strs(const std::vector<std::string>& v) { u16((int)v.size()); for (const auto& s : v) str(s); }
    void info(const SessionMemberInfo& m) {
        str(m.id.fingerprint); str(m.displayName); u8((int)m.platform); i32(m.level); u8(m.hasCharacter ? 1 : 0); str(m.characterSlot);
        str(m.iconicCharacter); strs(m.loadout); i32(m.teamRequest);
        i32(m.mapVote);                                   // v3: the public lobby map vote, appended last (Frontend 5f19f40e)
    }
    void lobby(const SessionLobby& l) {
        str(l.gameModeTag); u16((int)l.settings.size()); for (const auto& kv : l.settings) { str(kv.first); str(kv.second); }
        i32(l.mapId); i32(l.mapSelectionMethod); i32(l.playerLimit);
        u16((int)l.slots.size());
        for (const SessionSlot& s : l.slots) { u8((int)s.kind); str(s.member.fingerprint); i32(s.team); u8(s.connecting ? 1 : 0); }
        u16((int)l.members.size()); for (const SessionMemberInfo& m : l.members) info(m);
        u8(l.matchRunning ? 1 : 0); u8(l.countingDown ? 1 : 0); i32(l.countdown);
        u8((int)l.voteMaps.size()); for (int m : l.voteMaps) i32(m);       // v3: the map vote, appended last
        u8((int)l.voteCounts.size()); for (int c : l.voteCounts) i32(c);
        u8(l.voteRound); u8(l.voteOpen ? 1 : 0); i32(l.voteWinner);
    }
};
struct R {
    const uint8_t* p; size_t n, i = 1; bool ok = true;
    R(const std::vector<uint8_t>& d) : p(d.data()), n(d.size()) {}
    bool need(size_t k) { if (i + k > n) ok = false; return ok; }
    int u8() { return need(1) ? p[i++] : 0; }
    int u16() { if (!need(2)) return 0; const int v = p[i] | (p[i + 1] << 8); i += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t)p[i + k] << (8 * k); i += 4; return v; }
    uint64_t u64() { if (!need(8)) return 0; uint64_t v = 0; for (int k = 0; k < 8; ++k) v |= (uint64_t)p[i + k] << (8 * k); i += 8; return v; }
    int i32() { return (int)u32(); }
    std::string str() { const int k = u16(); if (!need((size_t)k)) return ""; std::string s((const char*)p + i, (size_t)k); i += (size_t)k; return s; }
    std::vector<std::string> strs() { std::vector<std::string> v((size_t)u16()); for (auto& s : v) s = str(); return v; }
    SessionMemberInfo info() {
        SessionMemberInfo m;
        m.id.fingerprint = str(); m.displayName = str(); m.platform = (SessionPlatform)u8(); m.level = i32(); m.hasCharacter = u8() != 0;
        m.characterSlot = str(); m.iconicCharacter = str(); m.loadout = strs(); m.teamRequest = i32();
        m.mapVote = i32();
        return m;
    }
    SessionLobby lobby() {
        SessionLobby l;
        l.gameModeTag = str();
        for (int k = u16(); k > 0 && ok; --k) { std::string a = str(); l.settings[a] = str(); }
        l.mapId = i32(); l.mapSelectionMethod = i32(); l.playerLimit = i32();
        l.slots.resize((size_t)u16());
        for (SessionSlot& s : l.slots) { s.kind = (SessionSlot::Kind)u8(); s.member.fingerprint = str(); s.team = i32(); s.connecting = u8() != 0; }
        l.members.resize((size_t)u16());
        for (SessionMemberInfo& m : l.members) m = info();
        l.matchRunning = u8() != 0; l.countingDown = u8() != 0; l.countdown = i32();
        l.voteMaps.resize((size_t)u8()); for (int& m : l.voteMaps) m = i32();
        l.voteCounts.resize((size_t)u8()); for (int& c : l.voteCounts) c = i32();
        l.voteRound = u8(); l.voteOpen = u8() != 0; l.voteWinner = i32();
        return l;
    }
};

const char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford base32: no I L O U

std::string randomFingerprint() {
    std::random_device rd;
    std::string s = "WFC-";
    for (int i = 0; i < 8; ++i) { if (i == 4) s += '-'; s += kB32[rd() % 32]; }
    return s;
}

}  // namespace

// ---- the lockstep's view of the session socket, in match seats ----
class UdpSession::MatchView final : public INetTransport {
public:
    explicit MatchView(UdpSession& s) : s_(s) {}
    int self() const override { return s_.matchSelf_; }
    void sendUnreliable(int peer, const uint8_t* p, size_t n) override { if (const int ss = toSession(peer); ss >= 0) s_.t_->sendUnreliable(ss, p, n); }
    void sendReliable(int peer, const uint8_t* p, size_t n) override { if (const int ss = toSession(peer); ss >= 0) s_.t_->sendReliable(ss, p, n); }
    bool poll(NetPacket& out) override {
        s_.pump();
        if (inbox.empty()) return false;
        out = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }
    void service() override { if (s_.t_) s_.t_->service(); }
    NetStats stats() const override { return s_.t_ ? s_.t_->stats() : NetStats{}; }
    std::deque<NetPacket> inbox;

private:
    int toSession(int matchSeat) const {
        return matchSeat >= 0 && (size_t)matchSeat < s_.matchToSession_.size() && s_.t_ ? s_.matchToSession_[(size_t)matchSeat] : -1;
    }
    UdpSession& s_;
};

struct UdpSession::Discovery {
    std::future<PortMapResult> map;
    std::future<std::string> stun;   // the STUN server's resolved address
    PortMapResult result;
    int port = 0;
};

UdpSession::UdpSession() : UdpSession(Options{}) {}
UdpSession::UdpSession(const Options& o) : opt_(o) { self_.id.fingerprint = randomFingerprint(); }
UdpSession::~UdpSession() { if (role_ != Role::Offline) leave(); }

void UdpSession::emit(SessionEvent e) {
    if (search_ == Search::Quick && e.type == SessionEvent::Type::JoinFailed) { tryNextCandidate(); return; }
    if (search_ == Search::Quick && (e.type == SessionEvent::Type::Joined || e.type == SessionEvent::Type::MatchInProgress)) search_ = Search::None;
    e.time = now_;
    out_.push_back(std::move(e));
}

std::string UdpSession::lanCode(const std::string& ipPort) {
    const size_t c = ipPort.rfind(':');
    unsigned a[4] = {0, 0, 0, 0};
    if (c == std::string::npos || std::sscanf(ipPort.c_str(), "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4) return "";
    const unsigned port = (unsigned)std::atoi(ipPort.c_str() + c + 1);
    uint64_t v = ((uint64_t)a[0] << 40) | ((uint64_t)a[1] << 32) | ((uint64_t)a[2] << 24) | ((uint64_t)a[3] << 16) | (port & 0xffff);
    std::string s(10, '0');
    for (int i = 9; i >= 0; --i) { s[(size_t)i] = kB32[v & 31]; v >>= 5; }   // 48 bits in 10 x 5 (2 spare high bits)
    return s.substr(0, 5) + "-" + s.substr(5);
}

namespace {
bool packEndpoint(const std::string& ipPort, uint64_t& v48) {
    unsigned a[4];
    const size_t c = ipPort.rfind(':');
    if (c == std::string::npos || std::sscanf(ipPort.c_str(), "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4) return false;
    v48 = ((uint64_t)a[0] << 40) | ((uint64_t)a[1] << 32) | ((uint64_t)a[2] << 24) | ((uint64_t)a[3] << 16) | ((uint64_t)std::atoi(ipPort.c_str() + c + 1) & 0xffff);
    return true;
}
std::string unpackEndpoint(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", (unsigned)(v >> 40) & 255, (unsigned)(v >> 32) & 255, (unsigned)(v >> 24) & 255,
                  (unsigned)(v >> 16) & 255, (unsigned)v & 0xffff);
    return b;
}
}  // namespace

std::string UdpSession::internetCode(const std::string& publicIpPort, const std::string& lanIpPort) {
    uint64_t p = 0, l = 0;
    if (!packEndpoint(publicIpPort, p) || !packEndpoint(lanIpPort, l)) return lanCode(lanIpPort);
    // 96 bits -> 20 base32 symbols (100 bits; the top 4 must be 0): public endpoint first, then the LAN one
    std::string s(20, '0');
    for (int i = 19; i >= 0; --i) {
        s[(size_t)i] = kB32[l & 31];
        l = (l >> 5) | ((p & 31) << 43);
        p >>= 5;
    }
    return s.substr(0, 5) + "-" + s.substr(5, 5) + "-" + s.substr(10, 5) + "-" + s.substr(15);
}

bool UdpSession::decodeTargets(const std::string& typed, std::vector<std::string>& out) {
    out.clear();
    std::string t;
    for (char ch : typed) if (!std::isspace((unsigned char)ch) && ch != '-') t += (char)std::toupper((unsigned char)ch);
    if (t.size() > 3 && t.compare(0, 3, "WFC") == 0 && t.size() != 10 && t.size() != 20) t = t.substr(3);
    if (t.size() == 20 && t.find('.') == std::string::npos) {   // an internet code: public + LAN
        uint64_t p = 0, l = 0;
        for (char ch : t) {
            if (ch == 'O') ch = '0';
            if (ch == 'I' || ch == 'L') ch = '1';
            const char* f = std::strchr(kB32, ch);
            if (!f || !*f) return false;
            const uint64_t d = (uint64_t)(f - kB32);
            p = (p << 5) | (l >> 43);
            l = ((l << 5) & 0xFFFFFFFFFFFFull) | d;
        }
        if (p >> 48) return false;   // the 4 spare bits must be 0 (a typo check)
        out.push_back(unpackEndpoint(l));
        if (unpackEndpoint(p) != out[0]) out.push_back(unpackEndpoint(p));
        return true;
    }
    std::string one;
    if (!decodeTarget(typed, one)) return false;
    out.push_back(one);
    return true;
}

bool UdpSession::decodeTarget(const std::string& typed, std::string& ipPort) {
    std::string t;
    for (char ch : typed) if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') t += ch;
    if (t.size() > 4 && (t.compare(0, 4, "WFC-") == 0 || t.compare(0, 4, "wfc-") == 0)) t = t.substr(4);   // an optional prefix
    if (t.empty()) return false;
    if (t.find('.') != std::string::npos) {   // an address: a.b.c.d[:port]
        unsigned a[4];
        if (std::sscanf(t.c_str(), "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4 || a[0] > 255 || a[1] > 255 || a[2] > 255 || a[3] > 255) return false;
        ipPort = t.find(':') == std::string::npos ? t + ":14000" : t;
        return true;
    }
    std::string c;
    for (char ch : t) if (ch != '-') c += (char)std::toupper((unsigned char)ch);
    if (c.size() != 10) return false;
    uint64_t v = 0;
    for (char ch : c) {
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        const char* f = std::strchr(kB32, ch);
        if (!f || !*f) return false;
        v = (v << 5) | (uint64_t)(f - kB32);
    }
    if (v >> 48) return false;   // the 2 spare bits must be 0 (a typo check)
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", (unsigned)(v >> 40) & 255, (unsigned)(v >> 32) & 255, (unsigned)(v >> 24) & 255,
                  (unsigned)(v >> 16) & 255, (unsigned)v & 0xffff);
    ipPort = b;
    return true;
}

// ---- host ----
void UdpSession::host(const SessionHostSettings& settings) {
    hostSettings_ = settings;
    serviceCode_.clear(); registeredAt_ = -1.0; lastRegister_ = -1e9;
    if (role_ != Role::Offline) leave();
    std::string err;
    for (int p = opt_.hostPort; p < opt_.hostPort + 10 && !t_; ++p)
        t_ = createUdpTransport(0, opt_.bindIp + ":" + std::to_string(p), {}, err);
    if (!t_) {
        LOG_WARN("session: cannot open UDP %d-%d (%s)", opt_.hostPort, opt_.hostPort + 9, err.c_str());
        SessionEvent e; e.type = SessionEvent::Type::HostFailed; e.hostFailure = SessionHostFailure::NoPath; emit(e);
        return;
    }
    role_ = Role::Host;
    peers_.clear(); nextSeat_ = 1; matchToSession_.clear(); matchSelf_ = -1; view_.reset(); matchRunning_ = false;
    publicEp_.clear(); stunAddr_.clear(); stunSentAt_ = -1.0;
    hostPending_ = true; hostStart_ = now_; discDone_ = false;
    if (opt_.internet) {   // UPnP + DNS on a worker (blocking calls, ~3 s at most); the STUN query goes from the session socket
        disc_ = std::make_shared<Discovery>();
        const std::string la = t_->localAddress();
        disc_->port = std::atoi(la.c_str() + la.rfind(':') + 1);
        const int port = disc_->port;
        disc_->map = std::async(std::launch::async, [port] { return upnpMapUdp(port, 7200, "WFC Rebuild"); });
        const std::string stun = opt_.stunServer;
        disc_->stun = std::async(std::launch::async, [stun] { return stun.empty() ? std::string() : resolveIpv4(stun); });
    } else {
        finishHostOpened();
    }
}

void UdpSession::finishHostOpened() {
    if (!hostPending_ || !t_) return;
    if (!serviceAddr_.empty() && serviceCode_.empty() && now_ - hostStart_ < 1.5) return;   // the service code first (tickService registers)
    hostPending_ = false;
    SessionEvent e; e.type = SessionEvent::Type::HostOpened; e.member = self_.id;   // the host learns its own id (Frontend: its lobby slot)
    e.address = t_->localAddress();
    e.joinCode = !serviceCode_.empty() ? serviceCode_ : publicEp_.empty() ? lanCode(e.address) : internetCode(publicEp_, e.address);
    LOG_INFO("session: hosting at %s, public %s (code %s), member id %s", e.address.c_str(), publicEp_.empty() ? "unknown (LAN only)" : publicEp_.c_str(),
             e.joinCode.c_str(), self_.id.fingerprint.c_str());
    emit(e);
}

// ---- join ----
void UdpSession::join(const std::string& target) {
    if (role_ != Role::Offline) leave();
    std::vector<std::string> cands;
    std::string letters;
    for (char ch : target) if (std::isalpha((unsigned char)ch)) letters += (char)std::toupper((unsigned char)ch);
    if (letters.size() == 6 && target.find('.') == std::string::npos && !decodeTargets(target, cands)) {   // a lobby service code
        if (serviceAddr_.empty()) { SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Invalid; emit(e); return; }
        std::string err;
        t_ = createUdpTransport(255, opt_.bindIp + ":" + std::to_string(opt_.memberPort), {}, err);
        if (!t_) { SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Timeout; emit(e); return; }
        joinCandidates_.clear(); joining_ = true; joinStart_ = now_; lastJoinSend_ = -1e9; joinTarget_.clear();
        peers_.clear(); matchToSession_.clear(); matchSelf_ = -1; view_.reset(); matchRunning_ = false;
        sendService("{\"op\":\"lookup\",\"code\":\"" + letters + "\",\"lan\":\"" + t_->localAddress() + "\"}");
        SessionEvent e; e.type = SessionEvent::Type::JoinProgress; e.connecting = true; emit(e);
        LOG_INFO("session: joining service code %s via %s", letters.c_str(), serviceAddr_.c_str());
        return;
    }
    if (!decodeTargets(target, cands) || cands.empty()) {
        SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Invalid; emit(e);
        return;
    }
    joinCandidates_ = cands;
    const std::string addr = cands[0];
    std::string err;
    t_ = createUdpTransport(255, opt_.bindIp + ":" + std::to_string(opt_.memberPort), {}, err);
    if (!t_) {
        SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Timeout; emit(e);
        return;
    }
    joinTarget_ = addr; joining_ = true; joinStart_ = now_; lastJoinSend_ = -1e9;
    peers_.clear(); matchToSession_.clear(); matchSelf_ = -1; view_.reset(); matchRunning_ = false;
    SessionEvent e; e.type = SessionEvent::Type::JoinProgress; e.connecting = true; emit(e);
    LOG_INFO("session: joining %s%s (typed '%s')", addr.c_str(), cands.size() > 1 ? (" / " + cands[1]).c_str() : "", target.c_str());
}

void UdpSession::cancelJoin() {
    if (!joining_) return;
    joining_ = false;
    t_.reset();
    role_ = Role::Offline;
}

void UdpSession::leave() {
    if (!t_) { role_ = Role::Offline; return; }
    W m(kLeave);
    if (role_ == Role::Host) sendAllMembers(m.b);
    else if (peers_.count(0)) sendSeat(0, m.b);
    for (int k = 0; k < 2; ++k) for (const auto& kv : peers_) t_->sendUnreliable(kv.first, m.b.data(), m.b.size());
    t_->service();
    endSession();
}

void UdpSession::endSession() {
    unseated_.clear();
    if (disc_ && disc_->result.mapped) {
        const PortMapResult m = disc_->result;
        const int port = disc_->port;
        std::thread([m, port] { upnpUnmapUdp(m, port); }).detach();
    }
    disc_.reset(); hostPending_ = false;
    t_.reset();   // the match view stays (a NetMatchDriver may still hold it: its calls become no-ops without the socket)
    role_ = Role::Offline; joining_ = false; held_ = false; matchRunning_ = false;
    peers_.clear(); matchToSession_.clear(); matchSelf_ = -1;
}

// ---- lobby ----
std::vector<uint8_t> UdpSession::lobbyMsg(uint8_t type, const SessionLobby& l) const { W w(type); w.lobby(l); return w.b; }

void UdpSession::publishLobby(const SessionLobby& lobby) {
    if (role_ != Role::Host) return;
    lobby_ = lobby;
    sendAllMembers(lobbyMsg(kLobby, lobby_));
}

void UdpSession::setMemberInfo(const SessionMemberInfo& info) {
    const std::string fp = self_.id.fingerprint;
    self_ = info;
    if (self_.id.fingerprint.empty()) self_.id.fingerprint = fp;
    if (role_ == Role::Member && peers_.count(0)) { W w(kInfo); w.info(self_); sendSeat(0, w.b); }
}

void UdpSession::sendSeat(int seat, const std::vector<uint8_t>& msg) { if (t_) t_->sendReliable(seat, msg.data(), msg.size()); }
void UdpSession::sendAllMembers(const std::vector<uint8_t>& msg) { for (const auto& kv : peers_) sendSeat(kv.first, msg); }

void UdpSession::sendPeerTable() {   // host: every member learns every other member's address (the match is a full mesh)
    W w(kPeers);
    w.u8((int)peers_.size() + 1);
    w.u8(0); w.str(self_.id.fingerprint); w.str("");   // the host: each member already has it as seat 0
    for (const auto& kv : peers_) { w.u8(kv.first); w.str(kv.second.info.id.fingerprint); w.str(kv.second.address); }
    sendAllMembers(w.b);
}

int UdpSession::seatOf(const SessionMemberId& m) const {
    if (m == self_.id) return t_ ? t_->self() : -1;
    for (const auto& kv : peers_) if (kv.second.info.id == m) return kv.first;
    return -1;
}

void UdpSession::kick(const SessionMemberId& member, SessionKickReason why) {
    if (role_ != Role::Host) return;
    const int seat = seatOf(member);
    if (seat <= 0) return;
    W w(kKick); w.u8((int)why);
    sendSeat(seat, w.b);
    for (int k = 0; k < 2; ++k) t_->sendUnreliable(seat, w.b.data(), w.b.size());
    t_->service();
    dropPeer(seat, SessionLeaveReason::Kicked, true);
}

void UdpSession::setMuted(const SessionMemberId& member, bool muted) {
    if (muted) muted_[member.fingerprint] = true;
    else muted_.erase(member.fingerprint);
}

// A member is gone: in the lobby MemberLeft (host), during the load gate / the match PeerDropped (everyone).
void UdpSession::dropPeer(int seat, SessionLeaveReason why, bool notify) {
    auto it = peers_.find(seat);
    if (it == peers_.end()) return;
    const SessionMemberId id = it->second.info.id;
    peers_.erase(it);
    if (t_) t_->removePeer(seat);
    const bool inMatch = matchReady();
    LOG_INFO("session: member %s (seat %d) gone (%d)%s", id.fingerprint.c_str(), seat, (int)why, inMatch ? " in the match" : "");
    if (role_ == Role::Host && notify) {
        W w(kDropped); w.str(id.fingerprint); w.u8((int)why); w.u8(inMatch ? 1 : 0);
        sendAllMembers(w.b);
        sendPeerTable();
    }
    SessionEvent e;
    e.type = inMatch ? SessionEvent::Type::PeerDropped : SessionEvent::Type::MemberLeft;
    e.member = id; e.leaveReason = why; e.value = 0;
    if (role_ == Role::Host || inMatch) emit(e);
    if (inMatch) checkAllLoaded();
}

// ---- start ----
void UdpSession::setLaunch(const std::string& matchUrl, uint32_t seed, const SessionLobby& roster) {
    launchUrl_ = matchUrl; launchSeed_ = seed; launchRoster_ = roster;
}

void UdpSession::requestStart() {
    if (role_ != Role::Host) return;
    // The match seats: the roster's humans in slot order (the launch record's humans); the host is among them.
    matchToSession_.clear();
    const SessionLobby& r = launchRoster_.slots.empty() ? lobby_ : launchRoster_;
    for (const SessionSlot& s : r.slots) {
        if (s.kind != SessionSlot::Kind::Human) continue;
        const int seat = s.member.fingerprint.empty() ? 0 : seatOf(s.member);   // "" = the host itself (the flow's MatchLaunch.self convention)
        if (seat >= 0) matchToSession_.push_back(seat);
        else LOG_WARN("session: roster member %s is not in the session - left out of the match", s.member.fingerprint.c_str());
    }
    if (std::find(matchToSession_.begin(), matchToSession_.end(), 0) == matchToSession_.end()) matchToSession_.insert(matchToSession_.begin(), 0);
    matchSelf_ = (int)(std::find(matchToSession_.begin(), matchToSession_.end(), 0) - matchToSession_.begin());
    view_ = std::make_unique<MatchView>(*this);
    W w(kLaunch); w.str(launchUrl_); w.u32(launchSeed_); w.lobby(r);
    w.u8((int)matchToSession_.size()); for (int s : matchToSession_) w.u8(s);
    sendAllMembers(w.b);
    for (auto& kv : peers_) kv.second.loaded = false;
    loadedSelf_ = false; loadStart_ = now_;
    LOG_INFO("session: start - %zu match seats, url %s, seed %u", matchToSession_.size(), launchUrl_.c_str(), launchSeed_);
}

void UdpSession::reportLoaded(uint64_t simId, uint64_t loadHash) {
    held_ = true;
    if (role_ == Role::Member) { W w(kLoaded); w.u64(simId); w.u64(loadHash); sendSeat(0, w.b); return; }
    if (role_ != Role::Host) return;
    loadedSelf_ = true;
    W w(kPeerLoaded); w.str(self_.id.fingerprint); sendAllMembers(w.b);
    SessionEvent e; e.type = SessionEvent::Type::PeerLoaded; e.member = self_.id; emit(e);
    (void)simId;
    checkAllLoaded();
}

void UdpSession::checkAllLoaded() {
    if (role_ != Role::Host || !loadedSelf_ || matchRunning_) return;
    for (int s : matchToSession_) {
        if (s == 0) continue;
        auto it = peers_.find(s);
        if (it != peers_.end() && !it->second.loaded) return;
    }
    matchRunning_ = true; held_ = false;
    W w(kAllLoaded); w.u32(1); sendAllMembers(w.b);
    SessionEvent e; e.type = SessionEvent::Type::AllLoaded; e.value = 1; emit(e);
    LOG_INFO("session: all loaded - the match starts");
}

void UdpSession::matchEnded() {
    matchRunning_ = false; held_ = false; view_.reset(); matchToSession_.clear(); matchSelf_ = -1;
    if (role_ == Role::Host) { W w(kReturn); sendAllMembers(w.b); lobby_.matchRunning = false; sendAllMembers(lobbyMsg(kLobby, lobby_)); }
}

INetTransport* UdpSession::matchTransport() { return view_.get(); }

void UdpSession::matchFailed(int status, int64_t tick) {
    SessionEvent e;
    if (status == 3) { e.type = SessionEvent::Type::Desync; e.value = tick; }
    else if (status == 5) e.type = SessionEvent::Type::HostLost;
    else { e.type = SessionEvent::Type::ConnectionLost; e.timedOut = status == 4; }
    LOG_WARN("session: the match failed (status %d at tick %lld)", status, (long long)tick);
    emit(e);
}

const SessionMemberId& UdpSession::matchMember(int matchSeat) const {
    static const SessionMemberId none;
    if (matchSeat < 0 || (size_t)matchSeat >= matchToSession_.size()) return none;
    const int s = matchToSession_[(size_t)matchSeat];
    if (t_ && s == t_->self()) return self_.id;
    auto it = peers_.find(s);
    return it == peers_.end() ? none : it->second.info.id;
}

// ---- receive ----
void UdpSession::handleStranger(const std::string& from, const std::vector<uint8_t>& d) {
    if (d.empty()) return;
    if (d.size() >= 4 && d[0] == 'W' && d[1] == 'F' && d[2] == 'C' && d[3] == 'L') { handleService(from, d); return; }
    if (d[0] == kDiscover && role_ == Role::Host && hostSettings_.isPublic) {   // LAN discovery: a public lobby answers
        W w(kAnnounce); w.u32(kProtocol);
        w.str(self_.displayName); w.str(lobby_.gameModeTag.empty() ? hostSettings_.gameModeTag : lobby_.gameModeTag); w.str(hostSettings_.playlist);
        w.i32(lobby_.mapId); w.i32((int)peers_.size() + 1); w.i32(lobby_.slots.empty() ? 10 : (int)lobby_.slots.size());
        w.str(lanCode(t_->localAddress())); w.u8(matchRunning_ ? 1 : 0);
        t_->sendTo(from, w.b.data(), w.b.size());
        return;
    }
    if (d[0] == kPing && t_) { std::vector<uint8_t> p = d; p[0] = kPong; t_->sendTo(from, p.data(), p.size()); return; }
    if (d[0] == kPong) {
        R pr(d); const uint32_t nonce = pr.u32(); (void)nonce;
        for (Candidate& c : candidates_)
            for (const std::string& ep : c.endpoints)
                if (ep == from && c.pingSentAt > 0.0) {
                    const int ms = (int)((now_ - c.pingSentAt) * 1000.0 + 0.5);
                    if (c.bestPing < 0 || ms < c.bestPing) c.bestPing = ms;
                }
        return;
    }
    if (d[0] == kAnnounce && search_ != Search::None) {
        R ar(d);
        if (ar.u32() != kProtocol) return;
        Candidate c;
        c.entry.hostName = ar.str(); c.entry.modeTag = ar.str(); c.entry.playlist = ar.str();
        c.entry.mapId = ar.i32(); c.entry.players = ar.i32(); c.entry.maxPlayers = ar.i32();
        c.entry.code = ar.str(); c.state = ar.u8() ? "inprogress" : "pregame"; c.entry.lan = true;
        if (!ar.ok) return;
        c.endpoints = {from};
        addCandidate(c);
        return;
    }
    if (isStunPacket(d.data(), d.size())) {   // the reply to our public-address query
        std::string mapped;
        if (hostPending_ && from == stunAddr_ && stunParse(d.data(), d.size(), stunTx_, mapped)) {
            publicEp_ = mapped; discDone_ = true;
            LOG_INFO("session: STUN %s sees this host as %s", stunAddr_.c_str(), mapped.c_str());
            finishHostOpened();
        }
        return;
    }
    R r(d);
    if (d[0] == kJoin && role_ == Role::Host) {
        const uint32_t proto = r.u32();
        SessionMemberInfo info = r.info();
        if (!r.ok) return;
        if (proto != kProtocol) { std::vector<uint8_t> f = {kJoinFail, (uint8_t)SessionJoinFailure::Version}; t_->sendTo(from, f.data(), f.size()); return; }
        int seat = -1;
        for (const auto& kv : peers_) if (kv.second.address == from || kv.second.info.id == info.id) seat = kv.first;   // a JOIN resend
        const bool fresh = seat < 0;
        if (fresh) {
            if (peers_.size() >= 15) { std::vector<uint8_t> f = {kJoinFail, (uint8_t)SessionJoinFailure::Full}; t_->sendTo(from, f.data(), f.size()); return; }
            seat = nextSeat_++;
            t_->addPeer(seat, from);
            Peer& p = peers_[seat];
            p.info = info; p.address = from; p.lastHeard = now_;
        }
        std::vector<uint8_t> ok = {kJoinOk, (uint8_t)seat};
        t_->sendTo(from, ok.data(), ok.size());
        if (fresh) {
            SessionLobby l = lobby_;
            l.matchRunning = matchRunning_ || matchReady();
            sendSeat(seat, lobbyMsg(kLobby, l));
            sendPeerTable();
            SessionEvent e; e.type = SessionEvent::Type::MemberJoined; e.member = info.id; e.info = info; emit(e);
            LOG_INFO("session: %s (%s) joined from %s as seat %d", info.displayName.c_str(), info.id.fingerprint.c_str(), from.c_str(), seat);
        }
    } else if (d[0] == kJoinOk && joining_ && std::find(joinCandidates_.begin(), joinCandidates_.end(), from) != joinCandidates_.end()) {
        joinTarget_ = from;   // the endpoint that answered
        const int seat = r.u8();
        if (!r.ok || seat <= 0) return;
        if (t_->self() != seat) {
            t_->setSelf(seat);
            t_->addPeer(0, joinTarget_);
            Peer& h = peers_[0];
            h.address = joinTarget_; h.lastHeard = now_;
            LOG_INFO("session: seated as %d by the host at %s", seat, joinTarget_.c_str());
            std::vector<std::pair<int, std::vector<uint8_t>>> held;
            held.swap(unseated_);
            for (const auto& h : held) handleSeated(h.first, h.second);
        }
    } else if (d[0] == kJoinFail && joining_ && std::find(joinCandidates_.begin(), joinCandidates_.end(), from) != joinCandidates_.end()) {
        joining_ = false;
        SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = (SessionJoinFailure)r.u8(); emit(e);
        endSession();
    }
}

void UdpSession::handleSeated(int seat, const std::vector<uint8_t>& d) {
    // A member not yet seated (its JOIN_OK lost: one out-of-band datagram) keeps the host's messages until JOIN_OK names its seat -
    // completing the join on the LOBBY alone left it without a seat (no place in the launch roster).
    if (role_ != Role::Host && t_ && t_->self() == 255) {
        if (unseated_.size() < 64) unseated_.push_back({seat, d});
        return;
    }
    R r(d);
    switch (d[0]) {
    case kLobby:
        if (role_ == Role::Host) break;
        lobby_ = r.lobby();
        if (!r.ok) break;
        if (joining_) {
            joining_ = false; role_ = Role::Member;
            SessionEvent e; e.type = lobby_.matchRunning ? SessionEvent::Type::MatchInProgress : SessionEvent::Type::Joined;
            e.lobby = lobby_; e.member = self_.id; emit(e);
            W w(kInfo); w.info(self_); sendSeat(0, w.b);
        } else {
            SessionEvent e; e.type = SessionEvent::Type::LobbyState; e.lobby = lobby_; emit(e);
        }
        break;
    case kPeers: {
        if (role_ == Role::Host || seat != 0) break;
        std::map<int, Peer> keep;
        if (peers_.count(0)) keep[0] = peers_[0];
        for (int k = r.u8(); k > 0 && r.ok; --k) {
            const int s = r.u8();
            const std::string fp = r.str(), addr = r.str();
            if (!r.ok) break;
            if (s == 0) { keep[0].info.id.fingerprint = fp; continue; }
            if (t_ && s == t_->self()) continue;
            Peer p = peers_.count(s) ? peers_[s] : Peer{};
            p.info.id.fingerprint = fp; p.address = addr;
            if (p.lastHeard <= 0.0) p.lastHeard = now_;
            if (t_) t_->addPeer(s, addr);
            keep[s] = p;
        }
        for (const auto& kv : peers_) if (!keep.count(kv.first) && t_) t_->removePeer(kv.first);
        peers_ = keep;
        break;
    }
    case kInfo: {
        if (role_ != Role::Host || !peers_.count(seat)) break;
        SessionMemberInfo info = r.info();
        if (!r.ok) break;
        info.id = peers_[seat].info.id;   // the id is the one it joined with
        peers_[seat].info = info;
        SessionEvent e; e.type = SessionEvent::Type::MemberJoined; e.member = info.id; e.info = info; emit(e);
        break;
    }
    case kLaunch: {
        if (role_ != Role::Member || seat != 0) break;
        launchUrl_ = r.str(); launchSeed_ = r.u32(); launchRoster_ = r.lobby();
        matchToSession_.clear();
        for (int k = r.u8(); k > 0 && r.ok; --k) matchToSession_.push_back(r.u8());
        if (!r.ok) break;
        auto me = std::find(matchToSession_.begin(), matchToSession_.end(), t_ ? t_->self() : -1);
        matchSelf_ = me == matchToSession_.end() ? -1 : (int)(me - matchToSession_.begin());
        if (matchSelf_ < 0) { LOG_WARN("session: the launch roster does not include this member"); break; }
        view_ = std::make_unique<MatchView>(*this);
        SessionEvent e; e.type = SessionEvent::Type::CountdownStarted;
        e.launchUrl = launchUrl_; e.seed = launchSeed_; e.lobby = launchRoster_;
        emit(e);
        break;
    }
    case kLoaded: {
        if (role_ != Role::Host || !peers_.count(seat)) break;
        (void)r.u64(); (void)r.u64();   // simId / loadHash: the lockstep's HELLO launch hash checks the launch itself
        peers_[seat].loaded = true;
        W w(kPeerLoaded); w.str(peers_[seat].info.id.fingerprint); sendAllMembers(w.b);
        SessionEvent e; e.type = SessionEvent::Type::PeerLoaded; e.member = peers_[seat].info.id; emit(e);
        checkAllLoaded();
        break;
    }
    case kPeerLoaded: {
        if (role_ != Role::Member) break;
        SessionEvent e; e.type = SessionEvent::Type::PeerLoaded; e.member.fingerprint = r.str(); emit(e);
        break;
    }
    case kAllLoaded: {
        if (role_ != Role::Member) break;
        held_ = false; matchRunning_ = true;
        SessionEvent e; e.type = SessionEvent::Type::AllLoaded; e.value = (int64_t)r.u32(); emit(e);
        break;
    }
    case kLeave:
        if (role_ == Role::Host) dropPeer(seat, SessionLeaveReason::Left, true);
        else if (seat == 0) { SessionEvent e; e.type = SessionEvent::Type::HostLost; emit(e); endSession(); }
        break;
    case kKick:
        if (role_ == Role::Member && seat == 0) {
            SessionEvent e; e.type = SessionEvent::Type::Kicked; e.kickReason = (SessionKickReason)r.u8(); emit(e);
            endSession();
        }
        break;
    case kDropped: {
        if (role_ != Role::Member || seat != 0) break;
        SessionEvent e;
        e.member.fingerprint = r.str(); e.leaveReason = (SessionLeaveReason)r.u8();
        const bool inMatch = r.u8() != 0;
        if (inMatch) { e.type = SessionEvent::Type::PeerDropped; emit(e); }   // in the lobby the host's next LOBBY shows it
        break;
    }
    case kVersion:
        if (role_ == Role::Member && seat == 0) {
            SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Version; emit(e);
            endSession();
        }
        break;
    case kReturn:
        if (role_ == Role::Member && seat == 0) { matchRunning_ = false; held_ = false; matchToSession_.clear(); matchSelf_ = -1; }   // the view stays while a driver may hold it
        break;
    default: break;
    }
}

void UdpSession::pump() {
    if (!t_) return;
    t_->service();
    std::string from;
    std::vector<uint8_t> d;
    while (t_ && t_->pollStranger(from, d)) handleStranger(from, d);
    NetPacket p;
    while (t_ && t_->poll(p)) {
        if (p.data.empty()) continue;
        auto pi = peers_.find(p.from);
        if (pi != peers_.end()) pi->second.lastHeard = now_;
        const uint8_t type = p.data[0];
        if (type == kKeepalive) continue;
        if (isSessionType(type)) { if (p.reliable) handleSeated(p.from, p.data); else if (type == kLeave || type == kKick) handleSeated(p.from, p.data); continue; }
        if (view_) {   // a lockstep packet: to the match view in match seats
            auto m = std::find(matchToSession_.begin(), matchToSession_.end(), p.from);
            if (m == matchToSession_.end()) continue;
            p.from = (int)(m - matchToSession_.begin());
            view_->inbox.push_back(std::move(p));
        }
    }
}

std::vector<SessionEvent> UdpSession::poll(double now) {
    now_ = now;
    if (joining_ && t_) {
        if (now_ - lastJoinSend_ >= 1.0 && t_->self() == 255) {   // JOIN until seated (out of band: no seat yet)
            lastJoinSend_ = now_;
            W w(kJoin); w.u32(kProtocol); w.info(self_);
            for (const std::string& c : joinCandidates_) t_->sendTo(c, w.b.data(), w.b.size());   // the LAN endpoint and the public one
        }
        if (now_ - joinStart_ >= opt_.joinTimeoutS) {
            SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Timeout; emit(e);
            endSession();
        }
    }
    pump();
    if (hostPending_ && disc_ && t_) {
        using namespace std::chrono_literals;
        if (disc_->map.valid() && disc_->map.wait_for(0s) == std::future_status::ready) {
            disc_->result = disc_->map.get();
            if (disc_->result.mapped && !disc_->result.externalIp.empty()) { publicEp_ = disc_->result.externalIp + ":" + std::to_string(disc_->port); discDone_ = true; finishHostOpened(); }
        }
        if (hostPending_ && !disc_->map.valid() && stunSentAt_ < 0.0 && disc_->stun.valid() && disc_->stun.wait_for(0s) == std::future_status::ready) {
            stunAddr_ = disc_->stun.get();   // UPnP gave nothing: ask STUN from the session socket
            if (!stunAddr_.empty()) { const std::vector<uint8_t> q = stunRequest(stunTx_); t_->sendRaw(stunAddr_, q.data(), q.size()); stunSentAt_ = now_; }
        }
        if (hostPending_ && (now_ - hostStart_ >= opt_.discoverTimeoutS || (!disc_->map.valid() && !disc_->stun.valid() && stunSentAt_ < 0.0)))
            { discDone_ = true; finishHostOpened(); }   // no public endpoint in time: the LAN code
    }
    if (t_ && now_ - lastKeepalive_ >= 1.0) {
        lastKeepalive_ = now_;
        const uint8_t k = kKeepalive;
        for (const auto& kv : peers_) t_->sendUnreliable(kv.first, &k, 1);
    }
    // Timeouts: the host drops silent members; a member ends the session when the host is silent (other members: the host decides).
    if (role_ == Role::Host) {
        std::vector<int> gone;
        for (const auto& kv : peers_) if (now_ - kv.second.lastHeard > opt_.connectionTimeoutS) gone.push_back(kv.first);
        for (int s : gone) dropPeer(s, SessionLeaveReason::Timeout, true);
        if (matchReady() && !matchRunning_ && loadedSelf_ && now_ - loadStart_ > opt_.loadTimeoutS)
            for (int s : matchToSession_) if (s != 0 && peers_.count(s) && !peers_[s].loaded) dropPeer(s, SessionLeaveReason::LoadTimeout, true);
    } else if (role_ == Role::Member && peers_.count(0) && now_ - peers_[0].lastHeard > opt_.connectionTimeoutS) {
        SessionEvent e; e.type = SessionEvent::Type::ConnectionLost; e.timedOut = true; emit(e);
        endSession();
    }
    tickService();
    tickSearch();
    std::vector<SessionEvent> out;
    out.swap(out_);
    return out;
}

// ---- public matches ----
struct ServiceResolve { std::future<std::string> addr; };

void UdpSession::setLobbyService(const std::string& address) {
    if (address == serviceName_) return;
    serviceName_ = address; serviceAddr_.clear(); serviceReplied_ = false;
    if (address.empty()) { serviceResolve_.reset(); return; }
    serviceResolve_ = std::make_shared<ServiceResolve>();
    serviceResolve_->addr = std::async(std::launch::async, [address] { return resolveIpv4(address.find(':') == std::string::npos ? address + ":14010" : address); });
    LOG_INFO("session: lobby service %s", address.c_str());
}

bool UdpSession::ensureSocket() {
    if (t_) return true;
    std::string err;
    t_ = createUdpTransport(255, opt_.bindIp + ":0", {}, err);   // browsing: any port, no seat
    return t_ != nullptr;
}

void UdpSession::sendService(const std::string& json) {
    if (serviceAddr_.empty() || !t_) return;
    std::vector<uint8_t> b = {'W', 'F', 'C', 'L'};
    b.insert(b.end(), json.begin(), json.end());
    t_->sendRaw(serviceAddr_, b.data(), b.size());
    serviceWanted_ = true; serviceAskedAt_ = now_;
}

void UdpSession::tickService() {
    if (serviceResolve_ && serviceResolve_->addr.valid()) {
        using namespace std::chrono_literals;
        if (serviceResolve_->addr.wait_for(0s) == std::future_status::ready) {
            serviceAddr_ = serviceResolve_->addr.get();
            LOG_INFO("session: lobby service %s -> %s", serviceName_.c_str(), serviceAddr_.empty() ? "(unresolved)" : serviceAddr_.c_str());
        }
    }
    // The host registers its lobby every 5 s (public: listed for Quick Match; private: its 6-letter code works).
    if (role_ == Role::Host && t_ && !serviceAddr_.empty() && now_ - lastRegister_ >= 5.0) {
        lastRegister_ = now_;
        const std::string mode = lobby_.gameModeTag.empty() ? hostSettings_.gameModeTag : lobby_.gameModeTag;
        char b[1024];
        std::snprintf(b, sizeof b,
                      "{\"op\":\"register\",\"name\":\"%s\",\"mode\":\"%s\",\"playlist\":\"%s\",\"map\":%d,\"players\":%d,\"max\":%d,"
                      "\"version\":\"%s\",\"region\":\"\",\"lan\":\"%s\",\"public\":%s,\"state\":\"%s\"%s%s%s}",
                      jsonEsc(self_.displayName).c_str(), jsonEsc(mode).c_str(), jsonEsc(hostSettings_.playlist).c_str(), lobby_.mapId,
                      (int)peers_.size() + 1, lobby_.slots.empty() ? 10 : (int)lobby_.slots.size(), versionTag().c_str(), t_->localAddress().c_str(),
                      hostSettings_.isPublic ? "true" : "false", matchRunning_ ? "inprogress" : "pregame",
                      serviceCode_.empty() ? "" : ",\"code\":\"", serviceCode_.c_str(), serviceCode_.empty() ? "" : "\"");
        sendService(b);
    }
    // HostOpened waits a moment for the service code (when a service is set), else the internet / LAN code goes out. With the internet
    // discovery still running (disc_), its own completion calls finishHostOpened.
    if (hostPending_ && (!disc_ || discDone_) && (!serviceCode_.empty() || serviceAddr_.empty() || now_ - hostStart_ >= 1.5)) finishHostOpened();
}

void UdpSession::handleService(const std::string& from, const std::vector<uint8_t>& d) {
    if (from != serviceAddr_) return;
    assets::Json m;
    if (!assets::Json::parse(std::string((const char*)d.data() + 4, d.size() - 4), m)) return;
    serviceReplied_ = true;
    const std::string op = m["op"].asString();
    if (op == "registered") {
        const bool first = serviceCode_.empty();
        serviceCode_ = m["code"].asString();
        if (first) LOG_INFO("session: lobby service code %s (the service sees this host as %s)", serviceCode_.c_str(), m["seen"].asString().c_str());
        if (publicEp_.empty() && !m["seen"].asString().empty()) publicEp_ = m["seen"].asString();
    } else if (op == "punch" && t_) {   // a joiner looked us up: send to it so our router lets its packets in
        W w(kPing); w.u32(0);
        for (const char* k : {"peer_public", "peer_lan"}) { const std::string a = m[k].asString(); if (!a.empty()) t_->sendTo(a, w.b.data(), w.b.size()); }
    } else if (op == "endpoints" && joining_) {   // a 6-letter code: the host's endpoints, LAN first
        if (m.has("error")) { joining_ = false; SessionEvent e; e.type = SessionEvent::Type::JoinFailed; e.joinFailure = SessionJoinFailure::Invalid; emit(e); return; }
        joinCandidates_.clear();
        for (const char* k : {"host_lan", "host_public"}) { const std::string a = m[k].asString(); if (!a.empty()) joinCandidates_.push_back(a); }
        joinTarget_ = joinCandidates_.empty() ? std::string() : joinCandidates_[0];
    } else if ((op == "games" || op == "quick") && search_ != Search::None) {
        const assets::Json& list = op == "games" ? m["games"] : m["candidates"];
        for (size_t i = 0; i < list.size(); ++i) {
            const assets::Json& g = list[i];
            Candidate c;
            c.entry.code = g["code"].asString(); c.entry.hostName = g["name"].asString(); c.entry.modeTag = g["mode"].asString();
            c.entry.playlist = g["playlist"].asString(); c.entry.mapId = g["map"].asInt(-1); c.entry.players = g["players"].asInt();
            c.entry.maxPlayers = g["max"].asInt(); c.state = g["state"].asString();
            for (const char* k : {"host_lan", "host_public"}) { const std::string a = g[k].asString(); if (!a.empty()) c.endpoints.push_back(a); }
            addCandidate(c);
        }
    }
}

void UdpSession::addCandidate(Candidate c) {
    for (Candidate& o : candidates_)
        if ((!c.entry.code.empty() && o.entry.code == c.entry.code) || (!c.endpoints.empty() && !o.endpoints.empty() && o.endpoints[0] == c.endpoints[0])) {
            for (const std::string& e : c.endpoints) if (std::find(o.endpoints.begin(), o.endpoints.end(), e) == o.endpoints.end()) o.endpoints.push_back(e);
            return;
        }
    if (!searchQuery_.modeTag.empty() && c.entry.modeTag != searchQuery_.modeTag) return;
    if (!searchQuery_.playlist.empty() && c.entry.playlist != searchQuery_.playlist) return;
    candidates_.push_back(std::move(c));
}

void UdpSession::startSearch(Search kind) {
    if (!ensureSocket()) { SessionEvent e; e.type = SessionEvent::Type::GameList; e.serviceReachable = false; emit(e); return; }
    search_ = kind; searchStart_ = now_; candidates_.clear(); order_.clear(); pingRounds_ = 0; tried_ = 0; serviceReplied_ = false;
    const std::string lan = t_->localAddress();
    if (!serviceAddr_.empty()) {
        if (kind == Search::Quick)
            sendService("{\"op\":\"quick\",\"version\":\"" + versionTag() + "\",\"playlist\":\"" + jsonEsc(searchPlaylist_) + "\",\"lan\":\"" + lan + "\"}");
        else
            sendService("{\"op\":\"list\",\"version\":\"" + versionTag() + "\",\"playlist\":\"" + jsonEsc(searchQuery_.playlist) + "\",\"mode\":\"" +
                        jsonEsc(searchQuery_.modeTag) + "\"}");
    }
    W w(kDiscover); w.u32(kProtocol);   // LAN discovery always (a LAN lobby needs no service)
    for (int p = opt_.hostPort; p < opt_.hostPort + 10; ++p) t_->sendTo(opt_.discoverAddr + ":" + std::to_string(p), w.b.data(), w.b.size());
    LOG_INFO("session: %s (%s)", kind == Search::Quick ? "quick match" : "find games", serviceAddr_.empty() ? "LAN only" : ("LAN + " + serviceAddr_).c_str());
}

void UdpSession::findGames(const SessionGameQuery& q) { searchQuery_ = q; startSearch(Search::Find); }

void UdpSession::quickMatch(const std::string& playlist) {
    searchQuery_ = SessionGameQuery{}; searchQuery_.playlist = playlist; searchPlaylist_ = playlist;
    startSearch(Search::Quick);
}

void UdpSession::cancelSearch() {
    if (search_ == Search::None) return;
    search_ = Search::None;
    if (joining_) cancelJoin();
    SessionEvent e; e.type = SessionEvent::Type::SearchCancelled; emit(e);
}

void UdpSession::tickSearch() {
    if (search_ == Search::None || !t_) return;
    if (now_ - searchStart_ < kSearchCollectS) return;   // collect the service's answer and the LAN announcements
    if (pingRounds_ < kPingRounds && !candidates_.empty()) {
        if (now_ - lastPingRound_ >= kPingRoundS) {
            lastPingRound_ = now_; ++pingRounds_;
            for (Candidate& c : candidates_) {
                c.pingSentAt = now_;
                W w(kPing); w.u32(pingNonce_++);
                for (const std::string& e : c.endpoints) t_->sendTo(e, w.b.data(), w.b.size());
            }
            if (search_ == Search::Quick && pingRounds_ == 1) { SessionEvent e; e.type = SessionEvent::Type::SearchProgress; e.searchTried = 0; e.searchOf = (int)candidates_.size(); emit(e); }
        }
        return;
    }
    if (now_ - lastPingRound_ < kPingRoundS && !candidates_.empty()) return;   // the last round's replies
    if (search_ == Search::Find) { finishFind(); return; }
    if (order_.empty() && tried_ == 0) {   // quick: the best ping first (pregame before in progress at equal ping class), then fuller
        for (size_t i = 0; i < candidates_.size(); ++i)
            if (candidates_[i].bestPing >= 0 && candidates_[i].bestPing <= kMaxPingMs) order_.push_back(i);
        std::sort(order_.begin(), order_.end(), [this](size_t a, size_t b) {
            const Candidate& x = candidates_[a]; const Candidate& y = candidates_[b];
            if (x.bestPing / 50 != y.bestPing / 50) return x.bestPing < y.bestPing;   // 50 ms classes: then the pregame / fuller one
            if ((x.state != "inprogress") != (y.state != "inprogress")) return x.state != "inprogress";
            return x.entry.players > y.entry.players;
        });
        tryNextCandidate();
    }
}

void UdpSession::tryNextCandidate() {
    if ((size_t)tried_ >= order_.size()) {   // none fits: host a public lobby for the playlist
        search_ = Search::None;
        SessionHostSettings s = hostSettings_;
        s.isPublic = true; s.playlist = searchPlaylist_;
        LOG_INFO("session: quick match - no lobby fits (%zu candidates): hosting a public lobby for %s", candidates_.size(), searchPlaylist_.c_str());
        host(s);
        return;
    }
    const Candidate& c = candidates_[order_[(size_t)tried_++]];
    SessionEvent e; e.type = SessionEvent::Type::SearchProgress; e.searchTried = tried_; e.searchOf = (int)order_.size();
    e.time = now_; out_.push_back(e);
    LOG_INFO("session: quick match - trying %s (%s, %d ms, %s)", c.entry.code.c_str(), c.entry.hostName.c_str(), c.bestPing, c.state.c_str());
    const Search keep = search_;
    std::string list;
    for (const std::string& ep : c.endpoints) list += (list.empty() ? "" : ",") + ep;
    const std::vector<std::string> eps = c.endpoints;
    search_ = Search::None;   // join() resets the socket; the search state survives in the members
    join(eps.empty() ? c.entry.code : eps[0]);
    joinCandidates_ = eps;    // every endpoint of the candidate
    search_ = keep;
}

void UdpSession::finishFind() {
    SessionEvent e; e.type = SessionEvent::Type::GameList;
    e.serviceReachable = !serviceAddr_.empty() && serviceReplied_;
    for (const Candidate& c : candidates_) { frontend::SessionGameEntry g = c.entry; g.pingMs = c.bestPing; e.games.push_back(g); }
    std::sort(e.games.begin(), e.games.end(), [](const frontend::SessionGameEntry& a, const frontend::SessionGameEntry& b) {
        return (a.pingMs < 0 ? 99999 : a.pingMs) < (b.pingMs < 0 ? 99999 : b.pingMs);
    });
    search_ = Search::None;
    LOG_INFO("session: find games - %zu found (service %s)", e.games.size(), e.serviceReachable ? "answered" : "none / silent");
    emit(e);
}

}  // namespace net
