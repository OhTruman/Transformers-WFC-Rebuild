#pragma once
// [Systems] The online match in the real window (ONLINE_PHASE2_SPLIT.md item 2a): the per-frame driver of the lockstep match,
// called by the Application's normal frame loop instead of runNetMatch's headless loop (which stays for the harness).
//
// Contract with Gameplay (agreed 2026-10-17):
// - The Application keeps its per-frame World::handleInput (camera / controller; online the pawn moves only through commands).
// - frame(dt): pump the transport; submit this seat's commands up to tick N + D (World::encodeLocalCommand, re-stamped to its
//   target tick); when tick N is complete for every seat, World::applyRemoteCommand(seat, cmd) for each seat, then the step
//   callback once (World::tick + GameMode::tick); hashes / record / BYE as phase 1. While tick N is incomplete nothing steps
//   (stalled(): the HUD may show it); handleInput / drawing keep running and presentation interpolates.
// - Events: a second per-seat reliable ordered stream of tick-stamped events, applied on every peer before the step of their tick
//   and recorded / replayed like commands. Kind 1 = Choose Character: chooseCharacter(sel) sends {seat, atTick = simTick + D + 1,
//   sel (wire: names, not indices)}; every peer calls World::queueSeatSelection(seat, sel, atTick) on arrival.
// - held(): from the session's reportLoaded until AllLoaded's start tick (the load gate): no step, no submit.
// - The match seats are the launch record's humans (match player i = seat i on every peer); localSeat() = MatchLaunch::localSeat.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "net/VoiceSession.h"

namespace audio { class IAudio; }
namespace game { class World; struct CharacterSelection; }

namespace net {

class INetTransport;
class Lockstep;
class VoiceSession;

class NetMatchDriver {
public:
    struct Config {
        int seats = 2;                 // humans in the launch record
        int self = 0;                  // this machine's seat (MatchLaunch::localSeat)
        uint64_t launchHash = 0;       // the launch record's hash (HELLO: a mismatch ends the match, JoinFailed{Version})
        int hashEvery = 60;            // state hash exchange period in ticks (desync detection)
        int delay = 0;                 // input delay in ticks; 0 = auto (RTT + the arrival-lead feedback)
        double peerTimeoutS = 8.0;     // a peer silent this long is dropped (the frontend's ConnectionTimeout)
        std::string recordPath;        // WFC_NET_RECORD-style recording ("" none)
        std::string logPath;           // the per-tick CSV (WFC_NET_LOG layout: simhash, bundle hash, stall, bytes, rtt, wall_ms, D)
        bool voice = true;             // the voice lane (VoiceSession) on this match: hearing others
        bool voiceMic = false;         // this player talks: the microphone opens on the first push / open-mic frame (never under WFC_NOINTERACT)
        VoiceSession::Config voiceCfg; // volume, audience, open mic (the Audio rows; VoiceSession::setConfig keeps them live)
        std::function<bool(int seat)> voiceMuted;   // the local Mute list (seat -> muted)
        std::function<void(float)> step;   // World::tick + GameMode::tick, once per complete tick (fixed dt)
        // A seat's Choose Character (event kind 1), on every peer when it arrives: World::queueSeatSelection(seat, sel, atTick).
        std::function<void(int seat, const game::CharacterSelection& sel, uint32_t atTick)> onSelection;
        // Join in progress (Lockstep.h): seats = the player limit's human seats; reserved ones (activeFrom 0) join later.
        std::vector<uint32_t> activeFrom;   // empty = every seat from tick 1
        bool lateJoin = false;              // this peer joins a running match (replays the history, then goes live)
        bool keepHistory = false;           // the host of a joinable match
        // The host: the team for a joiner that caught up (the original: the smaller team). Unset: seat % 2.
        std::function<int(int seat)> pickJoinTeam;
        // Every peer, on the SeatJoin event: the seat's participant appears before the step of atTick (local = this peer's seat).
        std::function<void(int seat, int team, bool local, uint32_t atTick)> onSeatJoin;
        // Seat -> match player for commands / voice (-1 = no participant yet). Unset: player == seat (the launch's seat order).
        std::function<int(int seat)> seatPlayer;
        // Leaving mid-match (Lockstep.h LEAVEQ): the host decides bot or empty; every peer, on the SeatLeave event: the seat's
        // participant goes to a bot / leaves at untilTick (Gameplay: applySlotSwap toBot / World::queueLeave), the same tick everywhere.
        std::function<bool(int seat)> leaveToBot;
        std::function<void(int seat, uint32_t untilTick, bool toBot)> onSeatLeave;
    };
    // `transport` is the session's match view (seats = match seats); the driver owns the Lockstep built over it.
    NetMatchDriver(game::World& world, INetTransport& transport, audio::IAudio* audio, const Config& cfg);
    ~NetMatchDriver();
    NetMatchDriver(const NetMatchDriver&) = delete;
    NetMatchDriver& operator=(const NetMatchDriver&) = delete;

    // Once per rendered frame (dt = the frame's wall time). Returns the sim steps taken (0 when stalled / held, up to a catch-up
    // cap when behind).
    int frame(double dtSeconds);

    bool stalled() const;              // tick N waits for a seat's commands
    // A late joiner: replaying the history (no pacing, a time budget per frame) until it reaches its activeTick. The loading screen
    // shows catchUpTick() / catchUpTarget() (CatchUpProgress); live() = caught up and playing.
    bool catchingUp() const;
    uint32_t catchUpTick() const;
    uint32_t catchUpTarget() const;
    bool held() const;                 // the load gate (no step until the start tick)
    void setHeld(bool held, uint32_t startTick = 0);
    bool finished() const;             // the match ended normally (every seat's BYE / the match end) or failed (see status)
    int status() const;                // phase 1 exit codes: 0 running / ended, 3 desync, 4 peer timeout, 5 host lost, 6 launch mismatch
    int droppedSeat() const;           // the seat a timeout / leave is about (-1 none)
    int localSeat() const;
    int delay() const;                 // the current input delay D (ticks)
    float rttMs() const;
    void chooseCharacter(const game::CharacterSelection& sel);   // the local seat's in-match pick (Choose Character screen)
    void dropSeat(int seat);           // the host: the session lost this match seat's member mid-match (the leave protocol)
    VoiceSession* voice();             // null without the voice lane
    void setTalking(bool talking);     // push-to-talk / open mic state from the session's voice settings

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace net
