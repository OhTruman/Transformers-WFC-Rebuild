// PC EXTENSION - the online session boundary (docs/handoff/ONLINE_MP_FRONTEND_TESTS.md section 1). Contract between the
// frontend flow (GameFlow) and the session layer (Systems' ONLINE_MP_ARCHITECTURE.md: transport, join codes, lobby
// relay, lockstep). The frontend calls out; the session layer answers with events, polled once per frontend update, in
// order. Nothing here blocks. Every timeout the frontend shows (InitialConnectTimeout 50 s, ConnectionTimeout 8 s, the
// load gate) counts frontend update time, so tests advance it by updating with a fixed dt (FakeSession.h).
//
// Header only, no behaviour yet: GameFlow has no session set, so the offline flow is unchanged. The original's model
// (RE NETWORK_ARCHITECTURE_2026-10-01): a player-hosted listen server, sessions 'Party' and 'Game', no host migration;
// the reservation beacon's packets (request / response 4 = accepted / tell clients to join / host ready / cancelled /
// heartbeat / return to session) map onto host / join / Joined / JoinFailed / CountdownStarted / HostLost below.
//
// Alignment with Systems' ONLINE_MP_ARCHITECTURE.md (section 10) and the frontend flows (docs/handoff/ONLINE_MP_MISMATCHES.md):
// - poll(now) is called once per frontend update in every level, the match included (PeerDropped / HostLost / Desync /
//   Kicked reach the flow mid-match); the session layer hands lockstep-channel packets to its LockstepLayer queue.
// - The host's GameFlow decides teams / slots from MemberJoined / MemberLeft (the original PickTeam) and publishes them in
//   SessionLobby::slots; the session layer relays and owns membership / connectivity.
// - JoinFailed{Version} can also arrive after the join (a sim id / load hash mismatch at reportLoaded): it ends the session
//   for that peer.
// - The session layer owns the join timeout (InitialConnectTimeout 50 s); the frontend's own timer is a backstop.
// - Joined / MatchInProgress carry the joiner's own id in member (the member finds itself in SessionLobby::slots).
// - A MemberJoined for a member already in the lobby is that member's updated info (setMemberInfo on its side).
// - SessionHostFailure::StrictNat is not produced in phases 1-3 (no NAT-type detection without STUN); Web peers are phase 4.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace frontend {

// A peer's identity in a session (CROSSPLAY_PROFILE_STATS.md section 3): the public-key fingerprint plus the display
// name (decision D1: the Accounts screen's account name).
struct SessionMemberId {
    std::string fingerprint;   // "WFC-7QK2-M9TD"
    bool operator==(const SessionMemberId& o) const { return fingerprint == o.fingerprint; }
    bool operator<(const SessionMemberId& o) const { return fingerprint < o.fingerprint; }
};

enum class SessionPlatform { PC, Web };   // decision D11: shown per member; D10: a Web member caps Player Limit to 5 v 5

// What every member publishes about itself (setMemberInfo). The loadout is trusted in phase 1 (decision D3).
struct SessionMemberInfo {
    SessionMemberId id;
    std::string displayName;
    SessionPlatform platform = SessionPlatform::PC;
    int level = 0;                          // displayed level (self-reported)
    bool hasCharacter = false;              // the original lobby gate (D4): "Waiting for all players to choose a character"
    std::string characterSlot;              // custom character slot / specialty ("Soldier", ...)
    std::string iconicCharacter;            // iconic mode: the claimed iconic CharacterName ("" none; one per team)
    std::vector<std::string> loadout;       // weapons / abilities as the lobby message carries them
    int teamRequest = -1;                   // Online.SwitchTeam from this member (-1 none); the host decides
};

// One participant slot of the lobby / match: a human (local or remote) or a bot. Bot counts from Game Mode Options are
// maxima; humans take bot slots (ONLINE_MP_FLOWS.md 3.4).
struct SessionSlot {
    enum class Kind { Bot, Human } kind = Kind::Bot;
    SessionMemberId member;                 // humans only
    int team = 255;                         // 0 Autobots, 1 Decepticons, 255 none (FFA)
    bool connecting = false;                // PlayerList_GFX IsConnecting
};

// The host's lobby, as members receive it (the <CurrentGame:*> / <TnGameSettings:*> answers on a member come from it).
struct SessionLobby {
    std::string gameModeTag;                // "TDM" ...
    std::map<std::string, std::string> settings;   // TnGameSettings field -> value label (incl. the Wfc* bot rows)
    int mapId = -1;
    int mapSelectionMethod = 1;             // 0 rotate / vote, 1 Host's Choice
    int playerLimit = 0;                    // 0 Original, 1 Extended (WfcPlayerLimit)
    std::vector<SessionSlot> slots;
    std::vector<SessionMemberInfo> members;
    bool matchRunning = false;              // decision D6: a joiner waits in the lobby ("Match in progress")
    bool countingDown = false;
    int countdown = 0;                      // lobby exit countdown, seconds
};

// The values Create Game hands to host() (Game Mode Options as the host left them).
struct SessionHostSettings {
    std::string gameModeTag;
    std::map<std::string, std::string> settings;
    int mapSelectionMethod = 1;
    bool isPrivate = true;                  // phase 1: friends / private only (no XP, decision D8)
    std::string relay;                      // phase 3: a self-hosted relay ("host:port[,room]"); "" = direct UDP
};

enum class SessionHostFailure { NoPath, StrictNat };                     // D2: PC text / the original NAT text
// the original texts (ONLINE_MP_UI_STRINGS.md 3); Invalid: the code failed the session layer's decode / check symbol
enum class SessionJoinFailure { Full, Timeout, Version, MapMissing, Invalid };

// Voice (decision D7; ONLINE_MP_FLOWS.md 3.12, architecture section 7). Capture / playback / Opus are Systems'; the
// frontend hands over the player's settings and the push-to-talk key state, and shows MemberVoice as HeadsetState.
enum class SessionVoiceMode { Off, PushToTalk, OpenMic };
// Who hears this player (user addition 2026-10-17: proximity voice, architecture section 7): team radio anywhere, all
// players by radio, positional voice for everyone within range (64 m, room reverb), or team radio plus proximity.
enum class SessionVoiceAudience { TeamRadio, AllRadio, Proximity, RadioPlusProximity };
struct SessionVoiceSettings {
    SessionVoiceMode mode = SessionVoiceMode::PushToTalk;   // the user's default
    SessionVoiceAudience audience = SessionVoiceAudience::RadioPlusProximity;   // the proposed default
    std::string device;                     // capture device name; "" = the system default
    int volume = 70;                        // playback 0-100
};
enum class SessionLeaveReason { Left, Timeout, Kicked, LoadTimeout };
enum class SessionKickReason { Idle, Host };                             // D5: the original 180 s idle kick

// Session -> frontend, in order. `time` is the session's frontend-update time in seconds.
struct SessionEvent {
    enum class Type {
        HostOpened,        // joinCode, address
        HostFailed,        // hostFailure
        JoinProgress,      // connecting
        Joined,            // lobby
        JoinFailed,        // joinFailure
        MatchInProgress,   // lobby (matchRunning true)
        LobbyState,        // lobby (members: replaces the lobby)
        MemberJoined,      // member, info
        MemberLeft,        // member, leaveReason
        CountdownStarted,  // value = start time
        PeerLoaded,        // member
        AllLoaded,         // value = start tick
        PeerDropped,       // member, leaveReason (in a match: kill-feed line + bot swap)
        HostLost,          // D9: end of session
        ConnectionLost,    // member side; timedOut picks ConnectionTimeout vs ConnectionFailed text
        Desync,            // value = tick
        Kicked,            // kickReason
        MemberVoice,       // member, headsetState (0 no voice / 1 voice on / 2 speaking): PlayerList_GFX HeadsetState;
                           // voiceVia: 1 radio / 2 proximity while speaking (both: radio), for the talker cue
    } type = Type::LobbyState;
    double time = 0.0;
    std::string joinCode, address;
    SessionHostFailure hostFailure = SessionHostFailure::NoPath;
    SessionJoinFailure joinFailure = SessionJoinFailure::Full;
    SessionLeaveReason leaveReason = SessionLeaveReason::Left;
    SessionKickReason kickReason = SessionKickReason::Idle;
    SessionMemberId member;
    SessionMemberInfo info;
    SessionLobby lobby;
    int64_t value = 0;
    bool connecting = false, timedOut = false;
    int headsetState = 0, voiceVia = 0;
    // CountdownStarted (member): the host's launch record (ONLINE_MP_ARCHITECTURE 10.3, a requirement for lockstep) - the
    // host's full match URL (map, mode, every option incl. the bot rows / Iconic / WfcIdleKick), the match seed, and `lobby`
    // = the roster at the start. Members launch from it verbatim; empty = none sent (dev FakeSession: the member builds its own).
    std::string launchUrl;
    uint32_t seed = 0;
};
inline const char* sessionEventName(SessionEvent::Type t) {
    static const char* const k[] = {"HostOpened", "HostFailed", "JoinProgress", "Joined", "JoinFailed", "MatchInProgress",
                                    "LobbyState", "MemberJoined", "MemberLeft", "CountdownStarted", "PeerLoaded", "AllLoaded",
                                    "PeerDropped", "HostLost", "ConnectionLost", "Desync", "Kicked", "MemberVoice"};
    const int i = (int)t;
    return i >= 0 && i < (int)(sizeof k / sizeof k[0]) ? k[i] : "?";
}

// Frontend -> session. One instance per frontend runtime; set on GameFlow (setSession), absent = offline.
class ISession {
public:
    virtual ~ISession() = default;
    virtual void host(const SessionHostSettings& settings) = 0;              // Create Game, online
    virtual void join(const std::string& target) = 0;                         // a join code or an address, as typed
    virtual void cancelJoin() = 0;                                            // Escape / Back while joining
    virtual void leave() = 0;                                                 // Quit Game / Quit Match / Back from the game lobby
    virtual void publishLobby(const SessionLobby& lobby) = 0;                 // host: every LobbyState change
    virtual void setMemberInfo(const SessionMemberInfo& info) = 0;            // every member: name, character, team request
    virtual void requestStart() = 0;                                          // host: Start Game
    // host, just before requestStart: the launch record every peer starts from (the session sends it to the members with
    // CountdownStarted). Default: none.
    virtual void setLaunch(const std::string& matchUrl, uint32_t seed, const SessionLobby& roster) { (void)matchUrl; (void)seed; (void)roster; }
    virtual void reportLoaded(uint64_t simId, uint64_t loadHash) = 0;         // after the map load (load gate)
    virtual void kick(const SessionMemberId& member, SessionKickReason why) = 0;   // host: a manual kick (idle: see the doc)
    virtual void setVoiceSettings(const SessionVoiceSettings& v) = 0;          // on start and whenever the Audio rows change
    virtual void setPushToTalk(bool held) = 0;                                 // on each change of the key's state
    virtual void setMuted(const SessionMemberId& member, bool muted) = 0;      // local only: nobody is told
    // The capture device's input level now, 0..1 (Settings > Audio "Mic Test" meter; Systems' capture). Default: none.
    virtual float micLevel() const { return 0.0f; }
    // True from this peer's map load (reportLoaded) until the start tick (AllLoaded): the World must not step (the
    // Application skips World::tick; camera / HUD / audio keep running). ONLINE_MP_ARCHITECTURE 10.4. Default: never held.
    virtual bool simHeld() const { return false; }
    // Events since the last poll, in order. `now` = the frontend's update time (seconds).
    virtual std::vector<SessionEvent> poll(double now) = 0;
};

// Match-side plumbing for WFC_SESSIONTEST (Gameplay's harness, ONLINE_MP_FRONTEND_TESTS.md section 4); declared here so
// both halves use one name. A remote human is a match participant of game::ParticipantKind::Remote (GameplayEvents.h),
// fed one command per sim tick; the flow turns PeerDropped / a late join into a slot swap at an agreed tick.
struct SessionSlotSwap {
    int matchPlayer = -1;                   // the participant slot (its score row, team and index stay)
    enum class To { Bot, Remote } to = To::Bot;
    SessionMemberId member;                 // To::Remote: who takes the slot
    int64_t atTick = 0;                     // the agreed tick (the same on every peer)
};

} // namespace frontend
