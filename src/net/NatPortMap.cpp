// [Systems] UPnP IGD port mapping + STUN coding - see NatPortMap.h. Plain sockets; nothing here keeps state between calls.
#include "net/NatPortMap.h"

#include "core/Log.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {
namespace {

#ifdef _WIN32
using Sock = SOCKET;
const Sock kNo = INVALID_SOCKET;
void closeS(Sock s) { closesocket(s); }
#else
using Sock = int;
const Sock kNo = -1;
void closeS(Sock s) { close(s); }
#endif

bool wsa() {
#ifdef _WIN32
    static bool ok = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    return ok;
#else
    return true;
#endif
}

bool waitReadable(Sock s, int ms) {
    fd_set f; FD_ZERO(&f); FD_SET(s, &f);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select((int)s + 1, &f, nullptr, nullptr, &tv) > 0;
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// "http://192.168.1.1:5000/rootDesc.xml" -> host, port, path
bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    const size_t a = url.find("://");
    const size_t h = a == std::string::npos ? 0 : a + 3;
    const size_t p = url.find('/', h);
    std::string hp = url.substr(h, p == std::string::npos ? std::string::npos : p - h);
    path = p == std::string::npos ? "/" : url.substr(p);
    const size_t c = hp.find(':');
    host = hp.substr(0, c);
    port = c == std::string::npos ? 80 : std::atoi(hp.c_str() + c + 1);
    return !host.empty() && port > 0;
}

// One HTTP/1.0 request over TCP with a 1.5 s budget; returns the body ("" on failure). localIp: this side of the connection.
std::string http(const std::string& host, int port, const std::string& request, std::string* localIp = nullptr) {
    Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNo) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { closeS(s); return ""; }
#ifdef _WIN32
    DWORD to = 1500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof to);
#else
    timeval to{1, 500000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
#endif
    if (connect(s, (const sockaddr*)&a, sizeof a) != 0) { closeS(s); return ""; }
    if (localIp) {
        sockaddr_in me{}; socklen_t ml = sizeof me;
        if (getsockname(s, (sockaddr*)&me, &ml) == 0) { char b[INET_ADDRSTRLEN] = {0}; inet_ntop(AF_INET, &me.sin_addr, b, sizeof b); *localIp = b; }
    }
    send(s, request.data(), (int)request.size(), 0);
    std::string resp;
    char buf[4096];
    for (;;) {
        const int n = (int)recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        resp.append(buf, (size_t)n);
        if (resp.size() > (1 << 20)) break;
    }
    closeS(s);
    const size_t body = resp.find("\r\n\r\n");
    if (resp.compare(0, 12, "HTTP/1.1 200") != 0 && resp.compare(0, 12, "HTTP/1.0 200") != 0) return "";
    return body == std::string::npos ? "" : resp.substr(body + 4);
}

std::string tag(const std::string& xml, const std::string& name, size_t from = 0, size_t* at = nullptr) {
    const std::string lx = lower(xml), ln = lower(name);
    size_t a = lx.find("<" + ln + ">", from);
    if (a == std::string::npos) {   // with a namespace prefix: <u:NewExternalIPAddress>
        a = lx.find(":" + ln + ">", from);
        if (a == std::string::npos) return "";
        a = lx.find('>', a);
    } else {
        a = lx.find('>', a);
    }
    const size_t e = lx.find("</", a);
    if (e == std::string::npos) return "";
    if (at) *at = e;
    return xml.substr(a + 1, e - a - 1);
}

std::string soap(const PortMapResult& m, const std::string& host, int port, const std::string& action, const std::string& args) {
    const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" +
                             m.serviceType + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    const std::string req = "POST " + m.controlUrl + " HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) +
                            "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + m.serviceType + "#" + action +
                            "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    return http(host, port, req);
}

// Router location from SSDP (1.2 s).
std::string ssdpLocation() {
    Sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNo) return "";
    sockaddr_in to{};
    to.sin_family = AF_INET; to.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1"}) {
        const std::string q = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ") + st + "\r\n\r\n";
        sendto(s, q.data(), (int)q.size(), 0, (const sockaddr*)&to, sizeof to);
    }
    std::string loc;
    const auto t0 = std::chrono::steady_clock::now();
    while (loc.empty() && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1200)) {
        if (!waitReadable(s, 200)) continue;
        char buf[2048];
        const int n = (int)recvfrom(s, buf, sizeof buf - 1, 0, nullptr, nullptr);
        if (n <= 0) continue;
        buf[n] = 0;
        const std::string r = buf, lr = lower(r);
        const size_t a = lr.find("\nlocation:");
        if (a == std::string::npos) continue;
        size_t b = a + 10;
        while (b < r.size() && r[b] == ' ') ++b;
        const size_t e = r.find("\r", b);
        loc = r.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    closeS(s);
    return loc;
}

}  // namespace

PortMapResult upnpMapUdp(int port, int leaseSeconds, const std::string& description) {
    PortMapResult m;
    if (!wsa()) { m.error = "WSAStartup"; return m; }
    const std::string loc = ssdpLocation();
    if (loc.empty()) { m.error = "no UPnP router answered"; return m; }
    std::string host, path;
    int hport = 0;
    if (!splitUrl(loc, host, hport, path)) { m.error = "bad location " + loc; return m; }
    const std::string desc = http(host, hport, "GET " + path + " HTTP/1.0\r\nHost: " + host + ":" + std::to_string(hport) + "\r\nConnection: close\r\n\r\n", &m.localIp);
    if (desc.empty()) { m.error = "no device description at " + loc; return m; }
    // The first WANIPConnection (or WANPPPConnection) service and its control URL.
    for (const char* svc : {"urn:schemas-upnp-org:service:WANIPConnection:", "urn:schemas-upnp-org:service:WANPPPConnection:"}) {
        const size_t at = lower(desc).find(lower(svc));
        if (at == std::string::npos) continue;
        const size_t e = desc.find('<', at);
        m.serviceType = desc.substr(at, e - at);
        m.controlUrl = tag(desc, "controlURL", at);
        break;
    }
    if (m.controlUrl.empty()) { m.error = "the router has no WAN connection service"; return m; }
    if (m.controlUrl.find("://") != std::string::npos) { std::string h; int p = 0; splitUrl(m.controlUrl, h, p, m.controlUrl); }
    if (m.controlUrl[0] != '/') m.controlUrl = "/" + m.controlUrl;
    m.externalIp = tag(soap(m, host, hport, "GetExternalIPAddress", ""), "NewExternalIPAddress");
    const std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
                             "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" + std::to_string(port) +
                             "</NewInternalPort><NewInternalClient>" + m.localIp + "</NewInternalClient><NewEnabled>1</NewEnabled>"
                             "<NewPortMappingDescription>" + description + "</NewPortMappingDescription><NewLeaseDuration>" +
                             std::to_string(leaseSeconds) + "</NewLeaseDuration>";
    const std::string r = soap(m, host, hport, "AddPortMapping", args);
    m.mapped = r.find("AddPortMappingResponse") != std::string::npos;
    if (!m.mapped) m.error = "AddPortMapping refused";
    m.controlUrl = host + ":" + std::to_string(hport) + m.controlUrl;   // keep the router address for the removal
    LOG_INFO("upnp: router %s, external %s, UDP %d %s%s", loc.c_str(), m.externalIp.empty() ? "?" : m.externalIp.c_str(), port,
             m.mapped ? "mapped" : "NOT mapped: ", m.mapped ? "" : m.error.c_str());
    return m;
}

bool upnpUnmapUdp(const PortMapResult& m, int port) {
    if (!m.mapped) return false;
    const size_t slash = m.controlUrl.find('/');
    const std::string hp = m.controlUrl.substr(0, slash);
    PortMapResult c = m;
    c.controlUrl = m.controlUrl.substr(slash);
    const size_t colon = hp.find(':');
    const std::string r = soap(c, hp.substr(0, colon), std::atoi(hp.c_str() + colon + 1), "DeletePortMapping",
                               "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>");
    return r.find("DeletePortMappingResponse") != std::string::npos;
}

std::string resolveIpv4(const std::string& hostPort) {
    if (!wsa()) return "";
    const size_t c = hostPort.rfind(':');
    if (c == std::string::npos) return "";
    addrinfo hints{};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(hostPort.substr(0, c).c_str(), hostPort.c_str() + c + 1, &hints, &res) != 0 || !res) return "";
    char b[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &((const sockaddr_in*)res->ai_addr)->sin_addr, b, sizeof b);
    freeaddrinfo(res);
    return std::string(b) + hostPort.substr(c);
}

// ---- STUN (RFC 5389): 20-byte header (type, length, magic cookie 0x2112A442, transaction id), attributes TLV ----
std::vector<uint8_t> stunRequest(uint8_t txid[12]) {
    std::random_device rd;
    for (int i = 0; i < 12; ++i) txid[i] = (uint8_t)rd();
    std::vector<uint8_t> b = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    b.insert(b.end(), txid, txid + 12);
    return b;
}

bool isStunPacket(const uint8_t* p, size_t n) {
    return n >= 20 && (p[0] & 0xC0) == 0 && p[4] == 0x21 && p[5] == 0x12 && p[6] == 0xA4 && p[7] == 0x42;
}

bool stunParse(const uint8_t* p, size_t n, const uint8_t txid[12], std::string& mapped) {
    if (!isStunPacket(p, n) || p[0] != 0x01 || p[1] != 0x01 || std::memcmp(p + 8, txid, 12) != 0) return false;   // Binding success
    size_t i = 20;
    while (i + 4 <= n) {
        const int type = (p[i] << 8) | p[i + 1], len = (p[i + 2] << 8) | p[i + 3];
        const uint8_t* v = p + i + 4;
        if (i + 4 + (size_t)len > n) break;
        if ((type == 0x0020 || type == 0x0001) && len >= 8 && v[1] == 0x01) {   // (XOR-)MAPPED-ADDRESS, IPv4
            uint16_t port = (uint16_t)((v[2] << 8) | v[3]);
            uint8_t ip[4] = {v[4], v[5], v[6], v[7]};
            if (type == 0x0020) { port ^= 0x2112; ip[0] ^= 0x21; ip[1] ^= 0x12; ip[2] ^= 0xA4; ip[3] ^= 0x42; }
            char b[32];
            std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned)port);
            mapped = b;
            if (type == 0x0020) return true;
        }
        i += 4 + (((size_t)len + 3) & ~(size_t)3);
    }
    return !mapped.empty();
}

}  // namespace net
