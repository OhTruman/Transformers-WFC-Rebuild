#pragma once
// [Systems] Online transport (ONLINE_MP_ARCHITECTURE.md section 3, NETCODE_HARNESS.md section 2): one INetTransport per peer process,
// two channels - unreliable unordered datagrams (the lockstep commands: one packet per tick carrying the last R ticks) and a reliable
// ordered channel (control: hello / start / hashes / bye). Peers are addressed by seat index (0..N-1); the sender's seat travels in
// every packet, so a relay in between (netshim, a relay server) never has to keep source addresses.
// Implementations: UdpTransport (LAN / direct IP / the netshim harness), LoopbackTransport (two peers in one process, unit tests).
// A WebRTC transport (browser peers) implements the same interface later.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace net {

struct NetPacket {
    int from = -1;                 // sender seat
    bool reliable = false;
    std::vector<uint8_t> data;
};

struct NetStats {
    uint64_t bytesSent = 0, bytesReceived = 0, packetsSent = 0, packetsReceived = 0, resends = 0;
    float rttMs = 0.0f;            // smoothed reliable-channel round trip (0 = unknown)
};

class INetTransport {
public:
    virtual ~INetTransport() = default;
    virtual int self() const = 0;                                       // this process's seat
    virtual void sendUnreliable(int peer, const uint8_t* p, size_t n) = 0;
    virtual void sendReliable(int peer, const uint8_t* p, size_t n) = 0;
    virtual bool poll(NetPacket& out) = 0;                              // non-blocking; drain until false
    virtual void service() = 0;                                         // resends / acks; call often (each loop iteration)
    virtual NetStats stats() const = 0;
    // [Systems, online phase 2] Dynamic peers for the session layer (UdpSession): a lobby seats members as they join. The
    // defaults keep the fixed-peer transports (phase 1 harness, loopback) as they were.
    virtual bool addPeer(int /*seat*/, const std::string& /*hostPort*/) { return false; }   // (re)seat a peer at an address
    virtual void removePeer(int /*seat*/) {}                                                 // forget it (its channel state too)
    virtual void setSelf(int /*seat*/) {}                       // a joiner learns its seat from the host's JOIN_OK
    virtual std::string peerAddress(int /*seat*/) const { return ""; }                      // "a.b.c.d:port" or ""
    virtual std::string localAddress() const { return ""; }     // this machine's LAN IPv4:port as other PCs reach it
    // Out-of-band datagrams to / from addresses that have no seat yet (JOIN before the host seats the member; LAN discovery):
    // sent unframed (kind 3), delivered with the sender's address instead of a seat.
    virtual bool sendTo(const std::string& /*hostPort*/, const uint8_t* /*p*/, size_t /*n*/) { return false; }
    // [Systems] The relay fallback (SYSTEMS_RELAY.md): through the lobby service's relay, addresses "relay:<id>" are peers like any
    // other (a send wraps the datagram for the relay; what arrives through it is presented as from "relay:<src>"). relayHostPort =
    // the service's IP + its relay port; room = the lobby's token; selfId = this client's id (1 = the room's host). addInfra: an
    // endpoint that is not a peer (the lobby service) - WFC_NET_DIRECT_DROP=1 (TEST ONLY: two symmetric NATs on loopback) drops every
    // datagram that is neither through the relay nor to / from an infra endpoint.
    virtual void setRelay(const std::string& /*relayHostPort*/, uint64_t /*room*/, uint32_t /*selfId*/) {}
    virtual uint32_t relayId() const { return 0; }
    virtual void addInfra(const std::string& /*hostPort*/) {}
    virtual bool pollStranger(std::string& /*fromHostPort*/, std::vector<uint8_t>& /*data*/) { return false; }
    // A datagram exactly as given (no framing byte): the STUN query from this socket (its reply comes back via pollStranger).
    virtual bool sendRaw(const std::string& /*hostPort*/, const uint8_t* /*p*/, size_t /*n*/) { return false; }
};

// UDP over one socket. bind = "host:port"; peers = seat -> "host:port" (where this process sends that seat's packets).
std::unique_ptr<INetTransport> createUdpTransport(int self, const std::string& bind, const std::vector<std::pair<int, std::string>>& peers,
                                                  std::string& error);
// Two (or more) in-process peers sharing queues: createLoopbackGroup(n) -> n transports, seat i = element i.
std::vector<std::unique_ptr<INetTransport>> createLoopbackGroup(int n);

}  // namespace net
