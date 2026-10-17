#pragma once
// [Systems] Lockstep layer (ONLINE_MP_ARCHITECTURE.md sections 4, 5, 10.2): every peer simulates every human seat from the same command
// stream. A seat's command for tick N is produced at N - D (input delay D) and sent to every other peer; tick N steps on a peer only once
// every seat's command for N is in (a missing one stalls the step, never a hole). Commands travel as the 23-byte wire form of
// game::RemoteCommand and every peer, the sender too, simulates the DECODED form - one code path per peer, deterministic by construction.
// Packets (the transport payload, little endian):
//   1 CMDS  (unreliable) u8 type, u8 seat, u32 ack (the highest tick of YOUR seat I hold contiguously), u32 first, u8 count, count x 23
//   2 HELLO (reliable)   u8 type, u8 seat, u64 launch hash (the launch record + sim build: a mismatch refuses the match)
//   3 HASH  (reliable)   u8 type, u8 seat, u32 tick, u64 simStateHash (desync detector, every hashEvery ticks)
//   4 BYE   (reliable)   u8 type, u8 seat, u32 last tick it simulated (0 = left mid-match). A peer that has every command up to that
//                        tick finishes it and ends normally (the run's end point reached a tick apart); 0 from the host = host lost.
//   7 VOICE (unreliable) u8 type, u8 seat, u16 seq, u8 flags, Opus bytes - handed to the voice sink (VoiceSession)
//   5 PING  (unreliable) u8 type, u8 seat, u64 sender clock (us)   6 PONG (unreliable) u8 type, u8 seat, u64 the echoed clock
// Input delay: each peer picks its OWN D (it only decides how far ahead this seat schedules its commands; every peer applies the same
// streams to the same ticks, so the D values need not agree). Auto D (Config::delay <= 0) = ceil((SRTT / 2 + 4 x RTTVAR + 5 ms) / tick),
// clamped 2..15, from PING / PONG every 250 ms to each peer (the worst peer counts); it rises at once and falls at most 1 tick a second.
// Wire command (23 bytes): i8 forward, i8 right, u32 buttons, u32 pressed, u16 yaw, i16 pitch (rotator units, 65536 = 360 deg),
//   i24 x / y / z aim point (cm; valid with kRemoteBitAimPoint).
#include "net/NetTransport.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace game { struct RemoteCommand; }

namespace net {

constexpr size_t kWireCommandBytes = 23;
using WireCommand = std::array<uint8_t, kWireCommandBytes>;
WireCommand encodeCommand(const game::RemoteCommand& c);
void decodeCommand(const WireCommand& w, uint32_t tick, game::RemoteCommand& out);

class Lockstep {
public:
    struct Config {
        int seats = 2;              // humans (seat 0 = host)
        int self = 0;
        int delay = 4;              // D (ticks); <= 0 = auto from the measured RTT
        int maxResend = 32;         // ticks of commands per CMDS packet at most (from the peer's ack); >= 2 x D + 4 is enough
        int hashEvery = 60;         // desync-detector period (ticks)
        double peerTimeoutS = 10.0; // a stall this long = the peer is lost
        uint64_t launchHash = 0;
        std::string logPath;        // per-tick CSV (WFC_NET_LOG)
    };
    enum class Status { Running = 0, Desync = 3, PeerTimeout = 4, HostLost = 5, LaunchMismatch = 6 };

    Lockstep(INetTransport& t, const Config& c);
    ~Lockstep();
    void pump();                                  // transport service + receive; periodic command resend; call every loop iteration
    bool everyoneHere() const;                    // every peer's HELLO arrived (and matched)
    void submitLocal(uint32_t tick, const game::RemoteCommand& c);   // this seat's command for `tick` (encoded, stored, sent)
    bool haveAll(uint32_t tick) const;
    void command(int seat, uint32_t tick, game::RemoteCommand& out) const;   // decoded (also for this seat)
    bool wire(int seat, uint32_t tick, WireCommand& out) const;              // the raw 23 bytes (WFC_NET_RECORD)
    uint64_t bundleHash(uint32_t tick) const;     // FNV over every seat's wire bytes for `tick` (transport / merge check)
    void reportHash(uint32_t tick, uint64_t h);   // own state hash (sent every hashEvery ticks, compared with the peers')
    void stalled(double ms);                      // the loop could not step this long (timeout + log accounting)
    void stepped(uint32_t tick, uint64_t stateHash);   // log row
    void forget(uint32_t belowTick);              // drop stored commands older than this
    void bye(uint32_t lastTick);
    void sendVoice(const uint8_t* p, size_t n);   // a complete VOICE packet to every peer (unreliable)
    void setVoiceSink(std::function<void(int, const uint8_t*, size_t)> f) { voiceSink_ = std::move(f); }
    uint32_t endTick() const { return endTick_; }   // a peer finished the run at this tick (0 = none)
    Status status() const { return status_; }
    const std::string& statusText() const { return statusText_; }
    uint32_t localHigh() const { return localHigh_; }
    int delay() const { return delay_; }          // the current D (fixed, or auto)
    float rttMs() const { return srtt_; }

private:
    void handle(const NetPacket& p);
    void sendCommands(int peer, bool full);   // full = from the peer's ack; else the newest kRedundancy ticks
    std::vector<double> lastFullMs_;          // per peer: the last full-window send
    std::map<uint32_t, double> producedMs_;   // this seat's ticks -> when first sent (ack-overdue test)
    void fail(Status s, const std::string& why);
    INetTransport& t_;
    Config cfg_;
    std::vector<std::map<uint32_t, WireCommand>> cmds_;   // per seat
    std::vector<uint32_t> contiguous_;                    // per seat: highest tick held with no gap from 1
    std::vector<uint32_t> peerAck_;                       // per peer: their ack of MY commands
    std::vector<char> hello_;
    std::map<uint32_t, uint64_t> ownHash_;
    std::map<uint32_t, std::vector<std::pair<int, uint64_t>>> peerHash_;
    uint32_t localHigh_ = 0;
    uint32_t endTick_ = 0;
    std::function<void(int, const uint8_t*, size_t)> voiceSink_;
    int delay_ = 4;
    float srtt_ = 0.0f, rttvar_ = 0.0f;
    double lastPingMs_ = 0.0, lastDelayDropMs_ = 0.0;
    void updateDelay();
    double lastSendMs_ = 0.0, stallMs_ = 0.0, stallTotalMs_ = 0.0;
    Status status_ = Status::Running;
    std::string statusText_;
    FILE* log_ = nullptr;
};

}  // namespace net
