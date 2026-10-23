// [Systems] UdpTransport + LoopbackTransport (NetTransport.h). Datagram framing (little endian):
//   u8 kind (0 unreliable, 1 reliable data, 2 ack), u8 from (seat), u16 reserved, u32 seq (reliable / ack; 0 for unreliable), payload.
//   kind 3 (stranger, online phase 2): u8 3, then the payload - out of band, to / from an address with no seat yet (a JOIN before
//   the host seats the member, LAN discovery); delivered by pollStranger with the sender's address.
// Reliable channel: per peer a send sequence, the unacked packets resent every max(2 x RTT, 40 ms) (cap 250 ms) until acked; the
// receiver acks every reliable packet (also duplicates: the first ack may have been lost), delivers in sequence order and buffers
// out-of-order ones. Unreliable packets are delivered as they come (the lockstep layer tolerates loss, reorder and duplicates).
#include "net/NetTransport.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {
namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr size_t kHeader = 8;
enum : uint8_t { kUnreliable = 0, kReliable = 1, kAck = 2, kStranger = 3 };

void putHeader(std::vector<uint8_t>& b, uint8_t kind, int from, uint32_t seq) {
    b.resize(kHeader);
    b[0] = kind; b[1] = (uint8_t)from; b[2] = 0; b[3] = 0;
    b[4] = (uint8_t)seq; b[5] = (uint8_t)(seq >> 8); b[6] = (uint8_t)(seq >> 16); b[7] = (uint8_t)(seq >> 24);
}

// The reliable-channel state machine, shared by both transports (the loopback one runs it too, so reorder / loss tests can use it).
struct PeerChannel {
    uint32_t nextSend = 1;                       // next outgoing reliable seq
    struct Pending { std::vector<uint8_t> datagram; double lastSent = 0.0, firstSent = 0.0; };
    std::map<uint32_t, Pending> unacked;
    uint32_t nextDeliver = 1;                    // next in-order reliable seq to hand up
    std::map<uint32_t, std::vector<uint8_t>> early;   // received ahead of nextDeliver
};

class ChannelSet {
public:
    explicit ChannelSet(int self) : self_(self) {}
    // Wraps a payload; returns the datagram to send.
    std::vector<uint8_t> wrap(int peer, bool reliable, const uint8_t* p, size_t n) {
        std::vector<uint8_t> d;
        uint32_t seq = 0;
        if (reliable) seq = ch_[peer].nextSend++;
        putHeader(d, reliable ? kReliable : kUnreliable, self_, seq);
        d.insert(d.end(), p, p + n);
        if (reliable) { auto& u = ch_[peer].unacked[seq]; u.datagram = d; u.lastSent = u.firstSent = nowMs(); }
        return d;
    }
    // Handles one received datagram: queues deliverable packets; may produce an ack to send back (ackOut non-empty).
    void receive(const uint8_t* d, size_t n, std::deque<NetPacket>& inbox, std::vector<uint8_t>& ackOut, int& ackTo) {
        ackOut.clear(); ackTo = -1;
        if (n < kHeader) return;
        const uint8_t kind = d[0];
        const int from = d[1];
        const uint32_t seq = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
        if (kind == kUnreliable) {
            NetPacket p; p.from = from; p.reliable = false; p.data.assign(d + kHeader, d + n);
            inbox.push_back(std::move(p));
        } else if (kind == kReliable) {
            putHeader(ackOut, kAck, self_, seq); ackTo = from;
            PeerChannel& c = ch_[from];
            if (seq >= c.nextDeliver && !c.early.count(seq)) c.early[seq].assign(d + kHeader, d + n);
            while (!c.early.empty() && c.early.begin()->first == c.nextDeliver) {
                NetPacket p; p.from = from; p.reliable = true; p.data = std::move(c.early.begin()->second);
                inbox.push_back(std::move(p));
                c.early.erase(c.early.begin());
                ++c.nextDeliver;
            }
        } else if (kind == kAck) {
            PeerChannel& c = ch_[from];
            auto it = c.unacked.find(seq);
            if (it != c.unacked.end()) {
                if (it->second.lastSent == it->second.firstSent) {   // only first transmissions time the RTT (Karn)
                    const float s = (float)(nowMs() - it->second.firstSent);
                    rttMs_ = rttMs_ <= 0.0f ? s : rttMs_ * 0.875f + s * 0.125f;
                }
                c.unacked.erase(it);
            }
        }
    }
    // Datagrams due for resend now: (peer, bytes).
    template <class F> void resendDue(F&& send) {
        const double t = nowMs();
        const double rto = std::min(250.0, std::max(40.0, 2.0 * (double)rttMs_));
        for (auto& kv : ch_)
            for (auto& u : kv.second.unacked)
                if (t - u.second.lastSent >= rto) { u.second.lastSent = t; ++resends_; send(kv.first, u.second.datagram); }
    }
    float rttMs() const { return rttMs_; }
    uint64_t resends() const { return resends_; }
    void setSelf(int self) { self_ = self; }
    void reset(int peer) { ch_.erase(peer); }   // a re-seated peer starts its reliable channel afresh

private:
    int self_;
    std::map<int, PeerChannel> ch_;
    float rttMs_ = 0.0f;
    uint64_t resends_ = 0;
};

#ifdef _WIN32
using Socket = SOCKET;
const Socket kBad = INVALID_SOCKET;
void closeSock(Socket s) { closesocket(s); }
#else
using Socket = int;
const Socket kBad = -1;
void closeSock(Socket s) { close(s); }
#endif

std::string addrString(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

bool parseAddr(const std::string& hp, sockaddr_in& out) {
    const size_t c = hp.rfind(':');
    if (c == std::string::npos) return false;
    std::memset(&out, 0, sizeof out);
    out.sin_family = AF_INET;
    out.sin_port = htons((uint16_t)std::atoi(hp.c_str() + c + 1));
    return inet_pton(AF_INET, hp.substr(0, c).c_str(), &out.sin_addr) == 1;
}

class UdpTransport final : public INetTransport {
public:
    UdpTransport(int self, Socket s, std::map<int, sockaddr_in> peers) : self_(self), sock_(s), peers_(std::move(peers)), ch_(self) {
        const char* dd = std::getenv("WFC_NET_DIRECT_DROP");
        directDrop_ = dd && *dd && dd[0] != '0';
        // "ports:A+B": only the direct path to / from those UDP ports is blocked (members that cannot reach each other, the host can)
        if (directDrop_ && std::strncmp(dd, "ports:", 6) == 0)
            for (const char* q = dd + 6; *q;) { dropPorts_.push_back(std::atoi(q)); while (*q && *q != '+') ++q; if (*q) ++q; }
        if (directDrop_) LOG_WARN("NET transport: WFC_NET_DIRECT_DROP - only the relay and the lobby service are reachable (TEST ONLY)");
    }
    ~UdpTransport() override { if (sock_ != kBad) closeSock(sock_); }
    int self() const override { return self_; }
    void sendUnreliable(int peer, const uint8_t* p, size_t n) override { raw(peer, ch_.wrap(peer, false, p, n)); }
    void sendReliable(int peer, const uint8_t* p, size_t n) override { raw(peer, ch_.wrap(peer, true, p, n)); }
    bool poll(NetPacket& out) override {
        drainSocket();
        if (inbox_.empty()) return false;
        out = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }
    void service() override {
        drainSocket();
        if (haveRelay_ && nowMs() - relayKeepMs_ >= 5000.0) relayKeepalive();   // keeps our NAT mapping to the relay open
        ch_.resendDue([this](int peer, const std::vector<uint8_t>& d) { raw(peer, d); });
    }
    NetStats stats() const override { NetStats s = stats_; s.rttMs = ch_.rttMs(); s.resends = ch_.resends(); return s; }
    bool addPeer(int seat, const std::string& hostPort) override {
        if (seat < 0 || seat > 254) return false;
        sockaddr_in a{};
        uint32_t rid = 0;
        if (!relayAlias(hostPort, rid) && !parseAddr(hostPort, a)) return false;
        if (rid && !haveRelay_) return false;   // a relay alias without a relay
        // only a re-seat at another address starts the channel afresh: a new peer keeps what already arrived from it (a member seated
        // after the host's first reliable message must not re-deliver its retransmission). A switch to the relay keeps the channel
        // (the same peer, another path: the unacked datagrams resend through it).
        auto it = peers_.find(seat);
        const std::string before = peerAddress(seat);
        if (it != peers_.end() && before != hostPort && !rid && !peerRelay_.count(seat)) ch_.reset(seat);   // relay -> direct: the same peer too
        if (rid) { peerRelay_[seat] = rid; peers_[seat] = relay_; }
        else { peerRelay_.erase(seat); peers_[seat] = a; }
        return true;
    }
    void removePeer(int seat) override { peers_.erase(seat); peerRelay_.erase(seat); ch_.reset(seat); }
    void setRelay(const std::string& relayHostPort, uint64_t room, uint32_t selfId) override {
        sockaddr_in r{};
        if (!parseAddr(relayHostPort, r) || room == 0 || selfId == 0) return;
        relay_ = r; room_ = room; rid_ = selfId; haveRelay_ = true;
        relayKeepalive();
        LOG_INFO("NET transport: relay %s, room %016llx, id %u", relayHostPort.c_str(), (unsigned long long)room, selfId);
    }
    uint32_t relayId() const override { return haveRelay_ ? rid_ : 0; }
    void addInfra(const std::string& hostPort) override { sockaddr_in a{}; if (parseAddr(hostPort, a)) infra_.push_back(a); }
    std::string takeDirectFrom(int seat) override {
        auto it = directFrom_.find(seat);
        if (it == directFrom_.end()) return {};
        std::string a = it->second; directFrom_.erase(it); return a;
    }
    void setSelf(int seat) override { self_ = seat; ch_.setSelf(seat); }
    std::string peerAddress(int seat) const override {
        auto r = peerRelay_.find(seat);
        if (r != peerRelay_.end()) return "relay:" + std::to_string(r->second);
        auto it = peers_.find(seat);
        return it == peers_.end() ? std::string() : addrString(it->second);
    }
    std::string localAddress() const override {
        // The LAN IPv4 other PCs reach (the interface a route to the internet would use; no packet is sent) + the bound port.
        sockaddr_in me{};
        socklen_t ml = sizeof me;
        getsockname(sock_, (sockaddr*)&me, &ml);
        std::string ip = "127.0.0.1";
        if (me.sin_addr.s_addr != 0) {   // bound to one address (e.g. loopback in harness runs): that one is what peers reach
            char b[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &me.sin_addr, b, sizeof b);
            return std::string(b) + ":" + std::to_string(ntohs(me.sin_port));
        }
        Socket probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe != kBad) {
            sockaddr_in dst{};
            if (parseAddr("8.8.8.8:53", dst) && connect(probe, (const sockaddr*)&dst, sizeof dst) == 0) {
                sockaddr_in out{};
                socklen_t ol = sizeof out;
                if (getsockname(probe, (sockaddr*)&out, &ol) == 0 && out.sin_addr.s_addr != 0) {
                    char b[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &out.sin_addr, b, sizeof b);
                    ip = b;
                }
            }
            closeSock(probe);
        }
        return ip + ":" + std::to_string(ntohs(me.sin_port));
    }
    bool sendTo(const std::string& hostPort, const uint8_t* p, size_t n) override {
        std::vector<uint8_t> d;
        d.reserve(n + 1);
        d.push_back(kStranger);
        d.insert(d.end(), p, p + n);
        return sendRaw(hostPort, d.data(), d.size());
    }
    bool sendRaw(const std::string& hostPort, const uint8_t* p, size_t n) override {
        uint32_t rid = 0;
        if (relayAlias(hostPort, rid)) { if (!haveRelay_) return false; sendRelayed(rid, p, n); return true; }
        sockaddr_in a{};
        if (!parseAddr(hostPort, a)) return false;
        if (blocked(a)) return true;   // TEST ONLY: the direct path does not exist
        sendto(sock_, (const char*)p, (int)n, 0, (const sockaddr*)&a, sizeof a);
        stats_.bytesSent += n; ++stats_.packetsSent;
        return true;
    }
    bool pollStranger(std::string& from, std::vector<uint8_t>& data) override {
        drainSocket();
        if (strangers_.empty()) return false;
        from = std::move(strangers_.front().first);
        data = std::move(strangers_.front().second);
        strangers_.pop_front();
        return true;
    }

private:
    void raw(int peer, const std::vector<uint8_t>& d) {
        auto r = peerRelay_.find(peer);
        if (r != peerRelay_.end()) { sendRelayed(r->second, d.data(), d.size()); return; }
        auto it = peers_.find(peer);
        if (it == peers_.end()) return;
        if (blocked(it->second)) return;   // TEST ONLY
        sendto(sock_, (const char*)d.data(), (int)d.size(), 0, (const sockaddr*)&it->second, sizeof it->second);
        stats_.bytesSent += d.size(); ++stats_.packetsSent;
    }
    static constexpr size_t kRelayHdr = 18;   // "WR" | room u64 | src u32 | dst u32
    void sendRelayed(uint32_t dst, const uint8_t* p, size_t n) {
        std::vector<uint8_t> b(kRelayHdr + n);
        b[0] = 'W'; b[1] = 'R';
        for (int i = 0; i < 8; ++i) b[2 + i] = (uint8_t)(room_ >> (8 * i));
        for (int i = 0; i < 4; ++i) { b[10 + i] = (uint8_t)(rid_ >> (8 * i)); b[14 + i] = (uint8_t)(dst >> (8 * i)); }
        if (n) std::memcpy(b.data() + kRelayHdr, p, n);
        sendto(sock_, (const char*)b.data(), (int)b.size(), 0, (const sockaddr*)&relay_, sizeof relay_);
        stats_.bytesSent += b.size(); ++stats_.packetsSent;
    }
    void relayKeepalive() { relayKeepMs_ = nowMs(); sendRelayed(0, nullptr, 0); }
    static bool relayAlias(const std::string& a, uint32_t& id) {
        if (a.compare(0, 6, "relay:") != 0) return false;
        id = (uint32_t)std::strtoul(a.c_str() + 6, nullptr, 10);
        return id != 0;
    }
    static bool sameAddr(const sockaddr_in& x, const sockaddr_in& y) { return x.sin_addr.s_addr == y.sin_addr.s_addr && x.sin_port == y.sin_port; }
    bool isInfra(const sockaddr_in& a) const {
        if (haveRelay_ && sameAddr(a, relay_)) return true;
        for (const sockaddr_in& i : infra_) if (sameAddr(a, i)) return true;
        return false;
    }
    void drainSocket() {
        uint8_t buf[2048];
        for (;;) {
            sockaddr_in from{};
#ifdef _WIN32
            int fl = sizeof from;
#else
            socklen_t fl = sizeof from;
#endif
            const int n0 = (int)recvfrom(sock_, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (n0 <= 0) break;
            stats_.bytesReceived += (uint64_t)n0; ++stats_.packetsReceived;
            // Through the relay: unwrap, the sender is "relay:<src>" (a datagram of another room / not ours is dropped).
            const uint8_t* bp = buf;
            int n = n0;
            std::string fromStr;
            if (haveRelay_ && sameAddr(from, relay_)) {
                if (n < (int)kRelayHdr || buf[0] != 'W' || buf[1] != 'R') continue;
                uint64_t room = 0; uint32_t src = 0, dst = 0;
                for (int i = 0; i < 8; ++i) room |= (uint64_t)buf[2 + i] << (8 * i);
                for (int i = 0; i < 4; ++i) { src |= (uint32_t)buf[10 + i] << (8 * i); dst |= (uint32_t)buf[14 + i] << (8 * i); }
                if (room != room_ || dst != rid_ || src == 0) continue;
                bp = buf + kRelayHdr; n -= (int)kRelayHdr;
                fromStr = "relay:" + std::to_string(src);
                if (n <= 0) continue;
            } else {
                if (blocked(from)) continue;   // TEST ONLY: the direct path does not exist
                fromStr = addrString(from);
            }
            if (bp[0] == kStranger) {   // out of band: keep the sender's address
                if (strangers_.size() < 256) strangers_.push_back({fromStr, std::vector<uint8_t>(bp + 1, bp + n)});
                continue;
            }
            // the lobby service's replies (b"WFCL" + JSON) and a STUN reply: handed up whole as stranger datagrams
            if (n >= 4 && bp[0] == 'W' && bp[1] == 'F' && bp[2] == 'C' && bp[3] == 'L') {
                if (strangers_.size() < 256) strangers_.push_back({fromStr, std::vector<uint8_t>(bp, bp + n)});
                continue;
            }
            if (n >= 20 && (bp[0] & 0xC0) == 0 && bp[4] == 0x21 && bp[5] == 0x12 && bp[6] == 0xA4 && bp[7] == 0x42) {
                if (strangers_.size() < 256) strangers_.push_back({fromStr, std::vector<uint8_t>(bp, bp + n)});
                continue;
            }
            if (haveRelay_ && bp == buf && n >= 2) directFrom_[bp[1]] = fromStr;   // the sender seat (channel header): its direct path works
            std::vector<uint8_t> ack; int ackTo = -1;
            ch_.receive(bp, (size_t)n, inbox_, ack, ackTo);
            if (!ack.empty()) raw(ackTo, ack);
        }
    }
    int self_;
    Socket sock_;
    std::map<int, sockaddr_in> peers_;
    std::map<int, std::string> directFrom_;   // seat -> its latest direct datagram's address (with a relay only; takeDirectFrom)
    std::map<int, uint32_t> peerRelay_;   // seat -> its relay id (reached through the relay)
    bool haveRelay_ = false, directDrop_ = false;
    std::vector<int> dropPorts_;   // WFC_NET_DIRECT_DROP=ports:A+B (empty = every direct path)
    bool blocked(const sockaddr_in& a) const {   // TEST ONLY
        if (!directDrop_ || isInfra(a)) return false;
        if (dropPorts_.empty()) return true;
        const int port = ntohs(a.sin_port);
        for (int q : dropPorts_) if (q == port) return true;
        return false;
    }
    sockaddr_in relay_{};
    uint64_t room_ = 0;
    uint32_t rid_ = 0;
    double relayKeepMs_ = 0.0;
    std::vector<sockaddr_in> infra_;
    ChannelSet ch_;
    std::deque<NetPacket> inbox_;
    std::deque<std::pair<std::string, std::vector<uint8_t>>> strangers_;
    NetStats stats_;
};

// In-process group: datagrams go straight into the destination's queue (no loss); the reliable framing still runs.
struct LoopbackHub;
class LoopbackTransport final : public INetTransport {
public:
    LoopbackTransport(int self, std::shared_ptr<LoopbackHub> hub) : self_(self), hub_(std::move(hub)), ch_(self) {}
    int self() const override { return self_; }
    void sendUnreliable(int peer, const uint8_t* p, size_t n) override { deliver(peer, ch_.wrap(peer, false, p, n)); }
    void sendReliable(int peer, const uint8_t* p, size_t n) override { deliver(peer, ch_.wrap(peer, true, p, n)); }
    bool poll(NetPacket& out) override;
    void service() override;
    NetStats stats() const override { NetStats s = stats_; s.rttMs = ch_.rttMs(); s.resends = ch_.resends(); return s; }
    void deliver(int peer, const std::vector<uint8_t>& d);
    std::deque<std::vector<uint8_t>> wire;   // datagrams addressed to this transport

private:
    int self_;
    std::shared_ptr<LoopbackHub> hub_;
    ChannelSet ch_;
    std::deque<NetPacket> inbox_;
    NetStats stats_;
};
struct LoopbackHub { std::vector<LoopbackTransport*> members; };

void LoopbackTransport::deliver(int peer, const std::vector<uint8_t>& d) {
    if (peer < 0 || peer >= (int)hub_->members.size() || !hub_->members[(size_t)peer]) return;
    hub_->members[(size_t)peer]->wire.push_back(d);
    stats_.bytesSent += d.size(); ++stats_.packetsSent;
}
bool LoopbackTransport::poll(NetPacket& out) {
    service();
    if (inbox_.empty()) return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}
void LoopbackTransport::service() {
    while (!wire.empty()) {
        std::vector<uint8_t> d = std::move(wire.front());
        wire.pop_front();
        stats_.bytesReceived += d.size(); ++stats_.packetsReceived;
        std::vector<uint8_t> ack; int ackTo = -1;
        ch_.receive(d.data(), d.size(), inbox_, ack, ackTo);
        if (!ack.empty()) deliver(ackTo, ack);
    }
    ch_.resendDue([this](int peer, const std::vector<uint8_t>& d) { deliver(peer, d); });
}

}  // namespace

std::unique_ptr<INetTransport> createUdpTransport(int self, const std::string& bind, const std::vector<std::pair<int, std::string>>& peers,
                                                  std::string& error) {
#ifdef _WIN32
    static bool wsa = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    if (!wsa) { error = "WSAStartup failed"; return nullptr; }
#endif
    sockaddr_in local{};
    if (!parseAddr(bind, local)) { error = "bad bind address " + bind; return nullptr; }
    std::map<int, sockaddr_in> to;
    for (const auto& pr : peers) {
        sockaddr_in a{};
        if (!parseAddr(pr.second, a)) { error = "bad peer address " + pr.second; return nullptr; }
        to[pr.first] = a;
    }
    Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBad) { error = "socket() failed"; return nullptr; }
    if (::bind(s, (const sockaddr*)&local, sizeof local) != 0) { closeSock(s); error = "bind " + bind + " failed"; return nullptr; }
    int bc = 1; setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&bc, sizeof bc);   // LAN discovery (online phase 2)
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    int rcv = 1 << 20; setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof rcv);
    // [online phase 2] no WSAECONNRESET from an ICMP port-unreachable (a peer not up yet / gone): it would fail the next recvfrom
    BOOL noReset = FALSE; DWORD got = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &noReset, sizeof noReset, nullptr, 0, &got, nullptr, nullptr);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    return std::make_unique<UdpTransport>(self, s, std::move(to));
}

std::vector<std::unique_ptr<INetTransport>> createLoopbackGroup(int n) {
    auto hub = std::make_shared<LoopbackHub>();
    std::vector<std::unique_ptr<INetTransport>> out;
    hub->members.resize((size_t)n, nullptr);
    for (int i = 0; i < n; ++i) {
        auto t = std::make_unique<LoopbackTransport>(i, hub);
        hub->members[(size_t)i] = t.get();
        out.push_back(std::move(t));
    }
    return out;
}

}  // namespace net
