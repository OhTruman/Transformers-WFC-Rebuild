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
};

// UDP over one socket. bind = "host:port"; peers = seat -> "host:port" (where this process sends that seat's packets).
std::unique_ptr<INetTransport> createUdpTransport(int self, const std::string& bind, const std::vector<std::pair<int, std::string>>& peers,
                                                  std::string& error);
// Two (or more) in-process peers sharing queues: createLoopbackGroup(n) -> n transports, seat i = element i.
std::vector<std::unique_ptr<INetTransport>> createLoopbackGroup(int n);

}  // namespace net
