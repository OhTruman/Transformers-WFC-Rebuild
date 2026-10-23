#pragma once
// [Systems] The real online session (ONLINE_PHASE2_SPLIT.md item 1): frontend::ISession (Session.h, as is) over one UDP socket.
// Phase 2a: LAN / direct IP. The host binds UDP 14000 (14001-14009 if taken) and shows its LAN IPv4:port plus a LAN code (the
//   address, base32: "XXXXX-XXXXX"); a member joins by that code or by "a.b.c.d[:port]". The lobby service (public lists, short
// codes, NAT punch, relay: ONLINE_MATCHMAKING_DESIGN.md) comes later behind the same calls.
// Internet (user decision 2026-10-17: peer-to-peer, no server): with Options::internet the host maps its UDP port on the router
// (UPnP IGD) and reads the router's WAN address, or asks a STUN server (Options::stunServer) for the address the internet sees;
// HostOpened then carries the INTERNET code - the public endpoint + the LAN endpoint, "XXXXX-XXXXX-XXXXX-XXXXX" - which works at
// home and across the internet: the joiner tries the LAN endpoint and the public one. Without a mapping (no UPnP) a STUN address
// is reachable only through routers that keep the port open to anyone (full-cone); the lobby service's punch / relay covers the rest.
//
// Membership: session seat 0 = the host; the host seats each joiner (1, 2, ...) on its JOIN and tells everyone every member's
// address, so the members also reach each other directly (the lockstep match is a full mesh, as phase 1). Control messages go on
// the transport's reliable channel; a keepalive every 1 s on the unreliable one; a peer silent ConnectionTimeout (8 s) is gone.
// At the host's start (setLaunch + requestStart) the roster fixes the MATCH seats (the launch record's humans in slot order);
// matchTransport() is then the lockstep's view of the same socket in match seats (phase 1's Lockstep runs on it unchanged).
//
// Events follow the frontend's flows (GameFlow_Online.cpp): host - HostOpened / HostFailed, MemberJoined (new or updated info),
// MemberLeft, PeerLoaded, AllLoaded, PeerDropped; member - JoinProgress, Joined (lobby + own id) / JoinFailed, LobbyState,
// CountdownStarted (launch record), PeerLoaded, AllLoaded, PeerDropped, HostLost, ConnectionLost, Kicked.
#include "frontend/Session.h"
#include "net/NatPortMap.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace net {

class INetTransport;

class UdpSession final : public frontend::ISession {
public:
    struct Options {
        int hostPort = 14000;            // the host's UDP port (the next 9 tried if taken)
        double connectionTimeoutS = 8.0;   // a peer silent this long is gone (the frontend's ConnectionTimeout)
        double joinTimeoutS = 50.0;      // InitialConnectTimeout
        double loadTimeoutS = 90.0;      // the load gate: a member not loaded in this time is dropped (LoadTimeout)
        std::string bindIp = "0.0.0.0";
        bool internet = true;            // UPnP mapping + STUN when hosting (tests: false - no router / internet contact)
        std::string stunServer = "stun.l.google.com:19302";   // "" = no STUN query
        double discoverTimeoutS = 4.0;   // HostOpened waits at most this long for the public endpoint
        std::string discoverAddr = "255.255.255.255";   // LAN discovery broadcast (tests: 127.0.0.1)
    };
    UdpSession();
    explicit UdpSession(const Options& o);
    ~UdpSession() override;

    // ---- frontend::ISession ----
    void host(const frontend::SessionHostSettings& settings) override;
    void join(const std::string& target) override;
    void cancelJoin() override;
    void leave() override;
    void publishLobby(const frontend::SessionLobby& lobby) override;
    void setMemberInfo(const frontend::SessionMemberInfo& info) override;
    void requestStart() override;
    void setLaunch(const std::string& matchUrl, uint32_t seed, const frontend::SessionLobby& roster) override;
    void reportLoaded(uint64_t simId, uint64_t loadHash) override;
    void kick(const frontend::SessionMemberId& member, frontend::SessionKickReason why) override;
    void setVoiceSettings(const frontend::SessionVoiceSettings& v) override { voice_ = v; }
    void setPushToTalk(bool held) override { pushToTalk_ = held; }
    void setMuted(const frontend::SessionMemberId& member, bool muted) override;
    // Public matches (ONLINE_MATCHMAKING_DESIGN.md): the lobby service when set (setLobbyService "host:port"), else LAN broadcast.
    void findGames(const frontend::SessionGameQuery& q) override;
    void quickMatch(const std::string& playlist) override;
    void cancelSearch() override;
    void setLobbyService(const std::string& address) override;
    bool simHeld() const override { return held_; }
    std::vector<frontend::SessionEvent> poll(double now) override;

    // ---- the match (the NetMatchDriver's side) ----
    bool matchReady() const { return matchSelf_ >= 0; }   // the roster is fixed (CountdownStarted / requestStart)
    int matchSeats() const { return (int)matchToSession_.size(); }
    int matchSelf() const { return matchSelf_; }
    INetTransport* matchTransport();                       // lockstep packets in match seats (null before the roster)
    const frontend::SessionMemberId& matchMember(int matchSeat) const;
    void matchEnded();                                     // back to the lobby (host: LobbyState with matchRunning false)
    // The driver's lockstep failed (phase 1 codes): 3 desync -> Desync{tick}, 5 host lost -> HostLost, 4 / 6 -> ConnectionLost.
    void matchFailed(int status, int64_t tick);
    const std::string& launchUrl() const { return launchUrl_; }
    uint32_t launchSeed() const { return launchSeed_; }
    const frontend::SessionVoiceSettings& voiceSettings() const { return voice_; }
    bool pushToTalk() const { return pushToTalk_; }
    bool muted(const frontend::SessionMemberId& m) const { return muted_.count(m.fingerprint) != 0; }

    // ---- codes (static: tests, the join prompt) ----
    static std::string lanCode(const std::string& ipPort);              // "192.168.1.23:14000" -> "XXXXX-XXXXX"
    static bool decodeTarget(const std::string& typed, std::string& ipPort);   // a LAN code or "a.b.c.d[:port]"
    static std::string internetCode(const std::string& publicIpPort, const std::string& lanIpPort);   // 4 groups
    // Every endpoint a typed target names, the LAN one first (an internet code -> LAN + public; a LAN code / address -> one).
    static bool decodeTargets(const std::string& typed, std::vector<std::string>& ipPorts);
    const std::string& publicEndpoint() const { return publicEp_; }   // host: "" until known
    const frontend::SessionMemberId& selfId() const { return self_.id; }
    int role() const { return (int)role_; }   // 0 offline, 1 host, 2 member (diagnostics)
    void pump();                              // drains the socket: session messages here, lockstep packets to the match view

private:
    enum class Role { Offline, Host, Member };
    struct Peer {
        frontend::SessionMemberInfo info;
        std::string address;     // as the host sees it
        double lastHeard = 0.0;
        bool loaded = false;
    };
    class MatchView;
    friend class MatchView;

    void emit(frontend::SessionEvent e);
    void finishHostOpened();
    // ---- public matches ----
    struct Candidate {
        frontend::SessionGameEntry entry;
        std::vector<std::string> endpoints;   // LAN first
        std::string state;                    // "pregame" / "inprogress" (service)
        double pingSentAt = 0.0;
        int bestPing = -1;
    };
    enum class Search { None, Find, Quick };
    bool ensureSocket();                      // a socket for browsing (bound to any port) when not hosting / joining
    void sendService(const std::string& json);
    void handleService(const std::string& from, const std::vector<uint8_t>& d);
    void addCandidate(Candidate c);
    void tickSearch();
    void tickService();
    void startSearch(Search kind);
    void tryNextCandidate();
    void finishFind();
    void handleStranger(const std::string& from, const std::vector<uint8_t>& d);
    void handleSeated(int seat, const std::vector<uint8_t>& d);
    void sendSeat(int seat, const std::vector<uint8_t>& msg);
    void sendAllMembers(const std::vector<uint8_t>& msg);
    void sendPeerTable();
    void dropPeer(int seat, frontend::SessionLeaveReason why, bool notify);
    void endSession();
    void checkAllLoaded();
    std::vector<uint8_t> lobbyMsg(uint8_t type, const frontend::SessionLobby& l) const;
    int seatOf(const frontend::SessionMemberId& m) const;

    Options opt_;
    Role role_ = Role::Offline;
    std::unique_ptr<INetTransport> t_;
    std::unique_ptr<MatchView> view_;
    double now_ = 0.0, lastKeepalive_ = 0.0, joinStart_ = 0.0, lastJoinSend_ = 0.0, loadStart_ = 0.0;
    std::string joinTarget_;
    std::vector<std::string> joinCandidates_;
    // Internet endpoint discovery (host): the UPnP / DNS worker, then a STUN query from the session socket.
    struct Discovery;
    std::shared_ptr<Discovery> disc_;
    bool hostPending_ = false, discDone_ = false;
    double hostStart_ = 0.0, stunSentAt_ = -1.0;
    uint8_t stunTx_[12] = {0};
    std::string publicEp_, stunAddr_;
    // public matches: the lobby service, the host's settings, the search
    std::string serviceName_, serviceAddr_, serviceCode_;
    std::shared_ptr<struct ServiceResolve> serviceResolve_;
    bool serviceReplied_ = false, serviceWanted_ = false;
    double lastRegister_ = -1e9, serviceAskedAt_ = 0.0, registeredAt_ = -1.0;
    frontend::SessionHostSettings hostSettings_;
    Search search_ = Search::None;
    frontend::SessionGameQuery searchQuery_;
    std::string searchPlaylist_;
    double searchStart_ = 0.0, lastPingRound_ = 0.0;
    int pingRounds_ = 0, tried_ = 0;
    uint32_t pingNonce_ = 1;
    std::vector<Candidate> candidates_;
    std::vector<size_t> order_;               // quick match: the candidates by ping
    bool joining_ = false, held_ = false, loadedSelf_ = false, matchRunning_ = false;
    frontend::SessionMemberInfo self_;
    std::map<int, Peer> peers_;            // session seat -> peer (host: every member; member: every other member incl. the host)
    int nextSeat_ = 1;
    frontend::SessionLobby lobby_;         // the host's published lobby / the member's last LobbyState
    std::string launchUrl_;
    uint32_t launchSeed_ = 0;
    frontend::SessionLobby launchRoster_;
    std::vector<int> matchToSession_;      // match seat -> session seat
    int matchSelf_ = -1;
    frontend::SessionVoiceSettings voice_;
    bool pushToTalk_ = false;
    std::map<std::string, bool> muted_;
    std::vector<frontend::SessionEvent> out_;
};

}  // namespace net
