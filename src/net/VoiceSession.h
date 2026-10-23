#pragma once
// [Systems] The voice lane (ONLINE_MP_ARCHITECTURE.md section 7): this seat's captured 20 ms frames -> Opus -> unreliable VOICE packets
// to every peer (full mesh; the star relay fans out later); each remote talker's packets -> a jitter buffer -> its Opus decoder (loss
// concealment for a missing frame) -> a stereo PCM stream in the audio backend (IAudio::openStream). Radio frames play 2D at the
// voice volume (a 2D stream outside the cue mix, as the original's non-positional chat); proximity frames are a 3D voice INSIDE the cue
// mix (IAudio positional stream at the talker's head): the original's dialog-cue distance curve (DX: full within 4 m, inverse rolloff,
// silent beyond 64 m), the 3D pan, the MASTER_WET bus - so the LISTENER's current zone reverb / echo (the Kismet SeqAct_Reverb room the
// local pawn is in) colours it like every world sound - and occlusion as the cue mix does it (a line check every 0.25 s; an occluded
// talker fades to -6 dB over 0.5 s, SoundCues' AudioOcclusionVolume / TransitionTime). WFC_VOICELOG: per talker every 2 s - mode,
// distance, occlusion, the listener's room preset.
// Packet (the transport payload): u8 type 7, u8 seat, u16 seq, u8 flags (1 radio, 2 proximity), Opus bytes.
#include "core/Math.h"
#include "net/VoiceCodec.h"
#include "net/VoiceFrames.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace audio { class IAudio; }

namespace net {

class Lockstep;

struct VoiceStats { long sent = 0, received = 0, late = 0, played = 0, concealed = 0, underruns = 0, radio = 0, proximity = 0, occluded = 0, unheard = 0,
                    muted = 0, gated = 0; };

class VoiceSession {
public:
    struct Config {
        bool radio = true, proximity = true;   // this seat's audience flags (Radio + Proximity default)
        float volume = 1.0f;                   // the voice slider
        int jitterFrames = 3;                  // frames buffered before a talker starts playing (60 ms)
        // Open mic: frames go out only while the voice gate is open - a frame above gateDb (dBFS RMS) opens it for hangMs (breathing
        // and room noise do not stream). Push to talk: the key is the gate.
        bool openMic = false;
        float gateDb = -45.0f;
        double hangMs = 300.0;
    };
    // Where the packets go: the match lane is the Lockstep (star relay by audience); the lobby lane is the UdpSession (the host relays
    // to every lobby member). self = this seat; send = one VOICE packet out; setSink = install / remove (nullptr) the receive hook.
    struct Link {
        std::function<int()> self;
        std::function<void(const uint8_t*, size_t)> send;
        std::function<void(std::function<void(int, const uint8_t*, size_t)>)> setSink;
    };
    VoiceSession(Lockstep& ls, audio::IAudio* audio, const Config& c);   // the match lane
    VoiceSession(const Link& link, audio::IAudio* audio, const Config& c);
    ~VoiceSession();
    void setSource(std::unique_ptr<IVoiceCapture> mic);   // the microphone (started on demand)
    void setConfig(const Config& c);                      // live: volume, audience, open mic / push to talk (the Audio rows)
    // Local mute (the lobby / pause menu's Mute): a muted talker's frames are dropped before decoding (never played). Seat -> muted.
    void setMuted(std::function<bool(int seat)> f) { muted_ = std::move(f); }
    // TEST ONLY: every decoded (or concealed) frame of a talker, before routing (WFC_VOICETEST compares it with the source).
    void setPlayTap(std::function<void(int seat, const VoiceFrame& pcm, bool concealed)> f) { tap_ = std::move(f); }
    // The talking indicator (SessionEvent::MemberVoice HeadsetState): 0 no voice heard for 10 s, 1 voice on, 2 speaking (a frame in the
    // last 250 ms); via = the last frame's audience flags (1 radio, 2 proximity). selfSeat = this seat's own sending.
    int headsetState(int seat) const;
    uint8_t lastVia(int seat) const;
    void setSyntheticTone(float hz);                      // TEST ONLY: a tone instead of a microphone (no device)
    // Every loop iteration. talking = push-to-talk held / open mic gate open; seatPos(seat, pos) gives a talker's head (false: unknown);
    // sameTeam(seat) = that talker is on the listener's team (radio frames are heard by the team only); the listener pose orients
    // proximity panning.
    using SeatPos = std::function<bool(int, core::Vec3&)>;
    using SameTeam = std::function<bool(int)>;
    using Occluded = std::function<bool(const core::Vec3& from, const core::Vec3& to)>;   // a world line check (true = blocked)
    using Room = std::function<std::string()>;                                         // the listener's current reverb preset (logs)
    void update(bool talking, const SeatPos& seatPos, const SameTeam& sameTeam, const Occluded& occluded, const Room& room,
                const core::Vec3& listener, const core::Vec3& listenerRight);
    void receive(int seat, const uint8_t* p, size_t n);   // a VOICE packet (Lockstep hands it over)
    float micLevel() const;
    const VoiceStats& stats() const { return stats_; }

private:
    struct Talker {
        std::unique_ptr<VoiceDecoder> dec;
        std::map<uint16_t, std::pair<uint8_t, std::vector<uint8_t>>> buf;   // seq -> (flags, opus)
        bool playing = false;
        uint16_t next = 0;        // next seq to play
        int missesInRow = 0;
        int stream = -1;          // radio: 2D stream
        int pstream = -1;         // proximity: positional stream in the cue mix
        uint8_t lastFlags = 1;
        bool occluded = false;
        float occGain = 1.0f;     // fades 1 <-> 0.5 (-6 dB) over 0.5 s
        double lastOccMs = 0.0, lastLogMs = 0.0;
    };
    void playTalker(int seat, Talker& t, const SeatPos& seatPos, const SameTeam& sameTeam, const Occluded& occluded, const Room& room,
                    const core::Vec3& listener, const core::Vec3& right);
    Link link_;
    audio::IAudio* audio_;
    Config cfg_;
    std::unique_ptr<IVoiceCapture> mic_;
    float toneHz_ = 0.0f;
    double tonePhase_ = 0.0, toneStartMs_ = -1.0, toneSent_ = 0.0;
    VoiceEncoder enc_;
    uint16_t seq_ = 0;
    std::map<int, Talker> talkers_;
    double lastPlayMs_ = 0.0;
    VoiceStats stats_;
    std::function<bool(int)> muted_;
    std::function<void(int, const VoiceFrame&, bool)> tap_;
    struct Heard { double lastMs = -1e9; uint8_t via = 0; };
    std::map<int, Heard> heard_;       // per seat (this seat too: its own sends) - the indicators
    double gateOpenUntil_ = -1.0;
};

}  // namespace net
