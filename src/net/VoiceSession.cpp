// [Systems] The voice lane - see VoiceSession.h.
#include "net/VoiceSession.h"

#include "audio/Audio.h"
#include "core/Log.h"
#include "net/Lockstep.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace net {
namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr uint8_t kVoicePacket = 7, kFlagRadio = 1, kFlagProximity = 2;
constexpr double kFrameMs = 20.0;
// The original's dialog cue (BL_HUD_INTERFACE.DX_SLIDER_VOLUME, DX_DRY): DistanceMin 400 UU, DistanceMax 6400 UU, the FMOD inverse
// (log) rolloff: full within 4 m, -6 dB per doubling, silent beyond 64 m.
constexpr float kProxMin = 4.0f, kProxMax = 64.0f;
int16_t seqDiff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }

}  // namespace

VoiceSession::VoiceSession(Lockstep& ls, audio::IAudio* audio, const Config& c) : ls_(ls), audio_(audio), cfg_(c) {
    ls_.setVoiceSink([this](int seat, const uint8_t* p, size_t n) { receive(seat, p, n); });
    if (!voiceCodecAvailable()) LOG_WARN("voice: Opus not in this build - voice unavailable");
}

VoiceSession::~VoiceSession() {
    ls_.setVoiceSink(nullptr);
    if (mic_) mic_->stop();
    for (auto& kv : talkers_) {
        if (audio_ && kv.second.stream >= 0) audio_->closeStream(kv.second.stream);
        if (audio_ && kv.second.pstream >= 0) audio_->closePositionalStream(kv.second.pstream);
    }
    LOG_INFO("voice: sent %ld frames, received %ld (late %ld), played %ld, concealed %ld, underruns %ld; heard as radio %ld, proximity %ld "
             "(occluded %ld), not for this listener %ld", stats_.sent, stats_.received, stats_.late, stats_.played, stats_.concealed,
             stats_.underruns, stats_.radio, stats_.proximity, stats_.occluded, stats_.unheard);
}

void VoiceSession::setSource(std::unique_ptr<IVoiceCapture> mic) { mic_ = std::move(mic); }
void VoiceSession::setConfig(const Config& c) { const int j = cfg_.jitterFrames; cfg_ = c; cfg_.jitterFrames = j; }

int VoiceSession::headsetState(int seat) const {
    auto it = heard_.find(seat);
    if (it == heard_.end()) return 0;
    const double age = nowMs() - it->second.lastMs;
    return age < 250.0 ? 2 : age < 10000.0 ? 1 : 0;
}
uint8_t VoiceSession::lastVia(int seat) const { auto it = heard_.find(seat); return it == heard_.end() ? 0 : it->second.via; }
void VoiceSession::setSyntheticTone(float hz) { toneHz_ = hz; }

float VoiceSession::micLevel() const { return mic_ ? mic_->level() : 0.0f; }

void VoiceSession::update(bool talking, const SeatPos& seatPos, const SameTeam& sameTeam, const Occluded& occluded, const Room& room,
                          const core::Vec3& listener, const core::Vec3& listenerRight) {
    const double now = nowMs();
    // ---- send: this seat's 20 ms frames while talking ----
    if (voiceCodecAvailable() && (cfg_.radio || cfg_.proximity)) {
        std::vector<VoiceFrame> frames;
        if (toneHz_ > 0.0f) {   // TEST ONLY: a synthetic talker at real time (50 frames / s) while talking
            if (!talking) toneStartMs_ = -1.0;
            else {
                if (toneStartMs_ < 0.0) { toneStartMs_ = now; toneSent_ = 0.0; }
                while ((toneSent_ + 1.0) * kFrameMs <= now - toneStartMs_) {
                    VoiceFrame f;
                    for (int i = 0; i < kVoiceFrameSamples; ++i) {
                        f[(size_t)i] = (int16_t)std::lround(0.3 * 32767.0 * std::sin(tonePhase_));
                        tonePhase_ += 6.283185307 * toneHz_ / kVoiceRate;
                    }
                    frames.push_back(f);
                    toneSent_ += 1.0;
                }
            }
        } else if (mic_) {
            if (talking && !mic_->running()) mic_->start();
            if (!talking && mic_->running()) mic_->stop();
            VoiceFrame f;
            while (mic_->pop(f)) {
                if (!talking) continue;
                if (cfg_.openMic) {   // the voice gate: a frame above gateDb opens it for hangMs
                    double e = 0.0;
                    for (int16_t v : f) e += (double)v * (double)v;
                    const double rms = std::sqrt(e / (double)kVoiceFrameSamples) / 32768.0;
                    const double db = rms > 1e-9 ? 20.0 * std::log10(rms) : -120.0;
                    if (db >= cfg_.gateDb) gateOpenUntil_ = now + cfg_.hangMs;
                    if (now >= gateOpenUntil_) { ++stats_.gated; continue; }
                }
                frames.push_back(f);
            }
        }
        for (const VoiceFrame& f : frames) {
            std::vector<uint8_t> opus;
            if (!enc_.encode(f, opus)) continue;
            std::vector<uint8_t> pkt = {kVoicePacket, (uint8_t)ls_.self(), (uint8_t)seq_, (uint8_t)(seq_ >> 8),
                                        (uint8_t)((cfg_.radio ? kFlagRadio : 0) | (cfg_.proximity ? kFlagProximity : 0))};
            pkt.insert(pkt.end(), opus.begin(), opus.end());
            ls_.sendVoice(pkt.data(), pkt.size());   // to the host (a client) or fanned out (the host)
            ++seq_; ++stats_.sent;
            Heard& me = heard_[ls_.self()]; me.lastMs = now; me.via = pkt[4];
        }
    }
    // ---- play: every remote talker, one frame per 20 ms of wall time ----
    if (lastPlayMs_ <= 0.0) lastPlayMs_ = now;
    int ticks = 0;
    while (now - lastPlayMs_ >= kFrameMs && ticks < 10) { lastPlayMs_ += kFrameMs; ++ticks;
        for (auto& kv : talkers_) playTalker(kv.first, kv.second, seatPos, sameTeam, occluded, room, listener, listenerRight);
    }
    if (ticks == 10) lastPlayMs_ = now;   // a long stall: do not burst-play the backlog
}

void VoiceSession::receive(int seat, const uint8_t* p, size_t n) {
    if (n < 6 || p[0] != kVoicePacket) return;
    Talker& t = talkers_[seat];
    if (!t.dec) t.dec = std::make_unique<VoiceDecoder>();
    const uint16_t seq = (uint16_t)(p[2] | (p[3] << 8));
    ++stats_.received;
    { Heard& h = heard_[seat]; h.lastMs = nowMs(); h.via = p[4]; }   // the indicator shows a muted talker too (as the original)
    if (muted_ && muted_(seat)) { ++stats_.muted; t.buf.clear(); t.playing = false; return; }
    if (t.playing && seqDiff(seq, t.next) < 0) { ++stats_.late; return; }   // its play-out slot has passed
    if (t.buf.size() < 50) t.buf[seq] = {p[4], std::vector<uint8_t>(p + 5, p + n)};
}

void VoiceSession::playTalker(int seat, Talker& t, const SeatPos& seatPos, const SameTeam& sameTeam, const Occluded& occluded,
                              const Room& room, const core::Vec3& listener, const core::Vec3& right) {
    if (!t.playing) {
        if ((int)t.buf.size() < cfg_.jitterFrames) return;   // fill the jitter buffer first
        t.playing = true; t.missesInRow = 0;
        t.next = t.buf.begin()->first;
        for (const auto& kv : t.buf) if (seqDiff(kv.first, t.next) < 0) t.next = kv.first;   // the oldest (wrap-safe)
    }
    VoiceFrame pcm{};
    bool have = false;
    auto it = t.buf.find(t.next);
    const bool concealedFrame = it == t.buf.end();   // (before the erase below)
    if (it != t.buf.end()) {
        t.lastFlags = it->second.first;
        have = t.dec->decode(it->second.second.data(), it->second.second.size(), pcm);
        t.buf.erase(it);
        t.missesInRow = 0;
        if (have) ++stats_.played;
    } else if (t.missesInRow < 3) {   // a lost / late frame: Opus concealment
        have = t.dec->decode(nullptr, 0, pcm);
        ++t.missesInRow;
        if (have) ++stats_.concealed;
    } else {   // the talker stopped (or a long gap): back to buffering
        t.playing = false; ++stats_.underruns;
        return;
    }
    ++t.next;
    if (!have) return;
    if (tap_) tap_(seat, pcm, concealedFrame);
    (void)right;   // the mixer pans positional streams itself
    // Who hears it: radio = the talker's team, 2D at the voice volume; proximity = anyone in earshot, as a 3D voice in the cue mix
    // (falloff, pan, the listener's room reverb, occlusion); radio wins for teammates.
    const bool radio = (t.lastFlags & kFlagRadio) && sameTeam && sameTeam(seat);
    if (!radio) {
        core::Vec3 at;
        if (!(t.lastFlags & kFlagProximity) || !seatPos || !seatPos(seat, at)) { ++stats_.unheard; return; }   // not for this listener
        const double now = nowMs();
        const float dist = core::length(at - listener);
        // TEST ONLY: WFC_VOICE_MAXDIST widens the earshot cut-off so a harness can reach the proximity path when the talkers spawn far
        // apart (the falloff below stays the original's curve).
        static const float maxD = std::getenv("WFC_VOICE_MAXDIST") ? (float)std::atof(std::getenv("WFC_VOICE_MAXDIST")) : kProxMax;
        static const bool vlog0 = std::getenv("WFC_VOICELOG") != nullptr;
        if (dist >= maxD) {
            ++stats_.unheard;
            if (vlog0 && now - t.lastLogMs >= 2000.0) { t.lastLogMs = now; LOG_INFO("VOICE seat %d proximity: %.1f m - out of earshot (%.0f m)", seat, dist, maxD); }
            return;
        }
        if (occluded && now - t.lastOccMs >= 250.0) { t.lastOccMs = now; t.occluded = occluded(listener, at); }   // OcclusionCheckInterval
        const float target = t.occluded ? 0.5f : 1.0f;   // -6 dB
        const float stepG = 0.5f * (float)(kFrameMs / 500.0);   // the full 1 -> 0.5 swing over 0.5 s
        t.occGain = t.occGain < target ? std::min(target, t.occGain + stepG) : std::max(target, t.occGain - stepG);
        ++stats_.proximity; if (t.occluded) ++stats_.occluded;
        static const bool vlog = std::getenv("WFC_VOICELOG") != nullptr;
        if (vlog && now - t.lastLogMs >= 2000.0) {
            t.lastLogMs = now;
            LOG_INFO("VOICE seat %d proximity: %.1f m, %s (gain %.2f), listener room %s", seat, dist, t.occluded ? "occluded" : "clear",
                     t.occGain, room ? room().c_str() : "?");
        }
        if (!audio_) return;
        if (t.pstream < 0) t.pstream = audio_->openPositionalStream(kVoiceRate);
        if (t.pstream < 0) return;
        audio_->setPositionalStream(t.pstream, at, cfg_.volume * t.occGain, kProxMin, maxD, true);
        float mono[kVoiceFrameSamples];
        for (int i = 0; i < kVoiceFrameSamples; ++i) mono[i] = (float)pcm[(size_t)i] / 32768.0f;
        audio_->pushPositionalStream(t.pstream, mono, kVoiceFrameSamples);
        return;
    }
    ++stats_.radio;
    if (!audio_) return;
    const float gainL = cfg_.volume, gainR = cfg_.volume;
    if (t.stream < 0) t.stream = audio_->openStream(kVoiceRate);
    if (t.stream < 0) return;
    float lr[kVoiceFrameSamples * 2];
    for (int i = 0; i < kVoiceFrameSamples; ++i) {
        const float v = (float)pcm[(size_t)i] / 32768.0f;
        lr[2 * i] = v * gainL; lr[2 * i + 1] = v * gainR;
    }
    audio_->pushStream(t.stream, lr, kVoiceFrameSamples);
}

}  // namespace net
