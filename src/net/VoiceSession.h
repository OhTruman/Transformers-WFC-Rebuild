#pragma once
// [Systems] The voice lane (ONLINE_MP_ARCHITECTURE.md section 7): this seat's captured 20 ms frames -> Opus -> unreliable VOICE packets
// to every peer (full mesh; the star relay fans out later); each remote talker's packets -> a jitter buffer -> its Opus decoder (loss
// concealment for a missing frame) -> a stereo PCM stream in the audio backend (IAudio::openStream). Radio frames play 2D at the
// voice volume; proximity frames play with the original's dialog-cue distance curve (DX: full within 4 m, -6 dB per doubling, silent
// beyond 64 m) and a stereo pan from the listener's facing [PARTIAL: no occlusion / room reverb yet - the design's 3D emitter path].
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

struct VoiceStats { long sent = 0, received = 0, late = 0, played = 0, concealed = 0, underruns = 0; };

class VoiceSession {
public:
    struct Config {
        bool radio = true, proximity = true;   // this seat's audience flags (Radio + Proximity default)
        float volume = 1.0f;                   // the voice slider
        int jitterFrames = 3;                  // frames buffered before a talker starts playing (60 ms)
    };
    VoiceSession(Lockstep& ls, audio::IAudio* audio, const Config& c);
    ~VoiceSession();
    void setSource(std::unique_ptr<IVoiceCapture> mic);   // the microphone (started on demand)
    void setSyntheticTone(float hz);                      // TEST ONLY: a tone instead of a microphone (no device)
    // Every loop iteration. talking = push-to-talk held / open mic gate open; seatPos(seat, pos) gives a talker's head (false: unknown);
    // sameTeam(seat) = that talker is on the listener's team (radio frames are heard by the team only); the listener pose orients
    // proximity panning.
    using SeatPos = std::function<bool(int, core::Vec3&)>;
    using SameTeam = std::function<bool(int)>;
    void update(bool talking, const SeatPos& seatPos, const SameTeam& sameTeam, const core::Vec3& listener, const core::Vec3& listenerRight);
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
        int stream = -1;
        uint8_t lastFlags = 1;
    };
    void playTalker(int seat, Talker& t, const SeatPos& seatPos, const SameTeam& sameTeam, const core::Vec3& listener,
                    const core::Vec3& right);
    Lockstep& ls_;
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
};

}  // namespace net
