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
//   7 VOICE (unreliable) u8 type, u8 TALKER seat, u16 seq, u8 flags, Opus bytes - handed to the voice sink (VoiceSession). Star relay
//                        (ONLINE_MP_ARCHITECTURE 7): a client sends its voice to the host only; the host forwards each frame to the
//                        seats in that frame's audience (setVoiceAudience: radio = the talker's team, proximity = within earshot by
//                        the sim's positions), never back to the talker - a client's upload is one stream whatever the match size.
//   5 PING  (unreliable) u8 type, u8 seat, u64 sender clock (us)   6 PONG (unreliable) u8 type, u8 seat, u64 the echoed clock
// Join in progress (public games, the original: a joiner spawns at once on the smaller team): seats beyond the launch's humans can be
// RESERVED (Config::activeFrom 0) - they contribute implicit empty commands and no HELLO, nobody waits on them. A late joiner runs
// the same launch with its own seat reserved and Config::lateJoin:
//   9 JOINREQ (reliable, joiner -> host)  u8 type, u8 seat
//  10 HIST    (reliable, host -> joiner)  u8 type, u8 seat (0), u8 sub: 0 commands {u8 seat, u32 first, u16 count, count x 23}
//             1 event {u8 seat, u32 atTick, u8 kind, u16 len, payload}  2 hash {u32 tick, u64 h}. The host keeps every tick's
//             commands, every event and every state hash (keepHistory) and sends the joiner all of it, then forwards each tick it
//             steps (and each event it learns) until the joiner's seat is active - the joiner replays from tick 1 at full speed
//             (deterministic sim; the hashes check it) while the match runs on.
//  11 READY   (reliable, joiner -> host)  u8 type, u8 seat, u32 the joiner's next step: it is within a few ticks of the host.
// The host then (onJoinReady -> activateSeat) submits the reliable event kind kEventSeatJoin {u8 seat, u32 activeTick, u8 team}
// stamped E = its next tick; activeTick = E + kJoinLeadTicks. Every peer sets the seat active from activeTick on arrival (no peer can
// have stepped E without the event); the sim adds / wakes the participant at E (NetMatchDriver::Config::onSeatJoin) and the joiner's
// own commands start at activeTick.
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
#include <deque>

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
        // Join in progress: per seat the first tick it sends commands for (1 = from the start, 0 = reserved: not yet joined). Empty =
        // every seat from tick 1. A late joiner (lateJoin) has its own seat reserved, asks the host for the history, and needs no HELLO.
        std::vector<uint32_t> activeFrom;
        bool lateJoin = false;
        bool keepHistory = false;   // the host: keep every command / event / hash for late joiners
    };
    static constexpr uint8_t kEventSeatJoin = 2;     // event kind (1 = Choose Character, NetMatchDriver)
    static constexpr uint32_t kJoinLeadTicks = 30;   // activeTick = the SeatJoin event's tick + this (0.5 s for the joiner's first commands)
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
    // The host's fan-out filter: hears(talker, listenerSeat, flags). Unset: everyone hears everything.
    void setVoiceAudience(std::function<bool(int, int, uint8_t)> f) { voiceAudience_ = std::move(f); }
    long voiceRelayed() const { return voiceRelayed_; }
    uint32_t endTick() const { return endTick_; }   // a peer finished the run at this tick (0 = none)
    // [online phase 2] Tick-stamped reliable events (ONLINE_PHASE2_SPLIT.md 3: Choose Character): a second per-seat ordered stream.
    // submitEvent stamps the next local command tick (localHigh + 1) and sends it to every peer (reliable). Each CMDS packet carries
    // the sender's count of events stamped at or before its newest tick, and haveAll(T) also waits until that many of the seat's
    // events arrived - so no peer steps a tick without the events stamped for it (unreliable commands may overtake the reliable
    // event). pollEvent returns every seat's events (this seat's too) in arrival order; the caller applies them at atTick.
    struct Event { int seat = -1; uint32_t atTick = 0; uint8_t kind = 0; std::vector<uint8_t> payload; uint32_t seq = 0; };   // seq: per seat, from 0 (dedupe: an event may reach a joiner twice)
    uint32_t submitEvent(uint8_t kind, const std::vector<uint8_t>& payload);
    bool pollEvent(Event& out);
    // Join in progress.
    bool seatActive(int seat, uint32_t tick) const;   // the seat sends commands for this tick
    bool selfActive() const { return seatActive(cfg_.self, localHigh_ + 1); }
    uint32_t activeFrom(int seat) const { return seat >= 0 && seat < cfg_.seats ? activeFrom_[(size_t)seat] : 0; }
    // The host: a reserved seat's joiner is caught up (READY); the callback decides the team and calls activateSeat.
    void setJoinReady(std::function<void(int seat)> f) { onJoinReady_ = std::move(f); }
    uint32_t activateSeat(int seat, int team);      // the host: the SeatJoin event; returns the seat's activeTick
    // The joiner: the newest tick the host has forwarded so far (the catch-up target) and whether it said READY.
    uint32_t historyHigh() const { return histHigh_; }
    uint32_t hostLive() const { return hostLive_; }   // the catch-up target (CatchUpProgress)
    bool historyStarted() const { return histStarted_; }
    void reportCatchUp(uint32_t nextStep);           // the joiner's step loop: READY once within kReadyTicks of historyHigh
    Status status() const { return status_; }
    const std::string& statusText() const { return statusText_; }
    uint32_t localHigh() const { return localHigh_; }
    int self() const { return cfg_.self; }
    int delay() const { return delay_; }          // the current D (fixed, or auto)
    float rttMs() const { return srtt_; }

private:
    void handle(const NetPacket& p);
    void sendCommands(int peer, bool full);   // full = from the peer's ack; else the newest kRedundancy ticks
    std::vector<double> lastFullMs_;          // per peer: the last full-window send
    std::map<uint32_t, double> producedMs_;   // this seat's ticks -> when first sent (ack-overdue test)
    void fail(Status s, const std::string& why);
    std::vector<uint32_t> ownEventTicks_;                  // this seat's events, their atTick (ascending)
    std::vector<uint32_t> eventsReceived_;                 // per seat: distinct events arrived
    std::vector<std::vector<char>> eventSeen_;             // per seat: seq -> arrived (dedupe)
    std::vector<std::map<uint32_t, uint32_t>> eventsAnnounced_;   // per seat: newest tick of a CMDS packet -> its event count
    std::deque<Event> eventQueue_;
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
    std::function<bool(int, int, uint8_t)> voiceAudience_;
    long voiceRelayed_ = 0;
    void fanOutVoice(int talker, const uint8_t* p, size_t n);
    int delay_ = 4;
    float srtt_ = 0.0f, rttvar_ = 0.0f;
    double lastPingMs_ = 0.0, lastDelayDropMs_ = 0.0;
    void updateDelay();
    // Arrival lead (asymmetric paths: srtt / 2 is not the one-way time): for each sender, the smallest lead in ticks (its command's
    // target tick minus the next tick this peer steps) of first arrivals over the last 1-2 s, reported back to it in our PONG; and the
    // lead each peer reports about OUR commands (kNoLead = not reported). A reported lead < 1 raises D by the shortfall at once.
    static constexpr int kNoLead = 127;
    std::vector<int> leadCur_, leadPrev_, leadReported_;
    double leadRollMs_ = 0.0;
    uint32_t nextStep_ = 1;           // the next tick this peer steps
    int leadFrom(int seat) const;
    double lastSendMs_ = 0.0, stallMs_ = 0.0, stallTotalMs_ = 0.0, logT0_ = 0.0;
    Status status_ = Status::Running;
    std::string statusText_;
    FILE* log_ = nullptr;
    // Join in progress.
    std::vector<uint32_t> activeFrom_;
    std::vector<Event> eventLog_;                    // the host: every event (any seat), in arrival order
    std::map<uint32_t, uint64_t> hashLog_;           // the host: its own state hash per hashEvery tick
    std::function<void(int)> onJoinReady_;
    uint32_t histHigh_ = 0;                          // the joiner: the newest tick with every seat's command forwarded
    uint32_t hostLive_ = 0;                          // the joiner: the host's newest stepped tick as of its last HIST packet
    bool histStarted_ = false, readySent_ = false;
    uint32_t hostStepped_ = 0;                       // the host: its newest stepped tick (what it can forward)
    uint32_t lastSubmitTick_ = 0;                    // the newest tick submitLocal was called for (stored or not)
    struct Feed { int seat; uint32_t nextTick = 1; size_t nextEvent = 0; uint32_t nextHashTick = 0; bool ready = false; };
    std::vector<Feed> feeds_;                        // the host: per joining seat, the history cursor (paced)
    double feedBudget_ = 0.0, feedLastMs_ = 0.0;
    void pumpFeeds();
    void applySeatJoin(const Event& e);
    void acceptEvent(Event e);
};

}  // namespace net
