#pragma once
// [Systems] Reaching a host across the internet without a server (user decision 2026-10-17: peer-to-peer only, a lobby service
// later). Two ways to learn / open the host's public endpoint:
// - UPnP IGD (most home routers): SSDP M-SEARCH on 239.255.255.250:1900 -> the router's description URL -> its WANIPConnection /
//   WANPPPConnection control URL; SOAP GetExternalIPAddress + AddPortMapping (UDP, the session port, a 2 h lease, renewed by
//   the host while it hosts) and DeletePortMapping when it stops.
// - STUN (RFC 5389 Binding request) fallback: sent FROM the session's own socket by UdpSession (the reply carries the address the
//   internet sees for that socket); here only the request / response coding.
// Blocking calls with short timeouts (UPnP total <= ~3 s); the session runs them when the player hosts.
#include <cstdint>
#include <string>
#include <vector>

namespace net {

struct PortMapResult {
    bool mapped = false;           // a UPnP mapping exists for the port
    std::string externalIp;        // the router's WAN address ("" unknown)
    std::string controlUrl, serviceType, localIp;   // for the renewal / removal
    std::string error;             // why not (diagnostics)
};
// Find the router and map external UDP `port` -> this machine's `port` (lease seconds; 0 = permanent where allowed).
PortMapResult upnpMapUdp(int port, int leaseSeconds, const std::string& description);
bool upnpUnmapUdp(const PortMapResult& m, int port);

// STUN: a Binding request (transaction id = 12 random bytes) and its parsed XOR-MAPPED-ADDRESS ("a.b.c.d:port").
std::vector<uint8_t> stunRequest(uint8_t txid[12]);
bool stunParse(const uint8_t* p, size_t n, const uint8_t txid[12], std::string& mapped);
bool isStunPacket(const uint8_t* p, size_t n);   // a STUN message (magic cookie at bytes 4-7)
// "name:port" -> "a.b.c.d:port" (IPv4, blocking DNS; "" on failure).
std::string resolveIpv4(const std::string& hostPort);

}  // namespace net
