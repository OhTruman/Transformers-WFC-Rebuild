#pragma once
// [Systems] Voice codec (ONLINE_MP_ARCHITECTURE.md section 7): Opus, mono 16 kHz, 20 ms frames, VOIP application, a fixed 16 kbit/s
// (~40 bytes a frame). One VoiceEncoder per local talker, one VoiceDecoder per remote talker (Opus decoders keep state: packet-loss
// concealment fills a lost frame). Built only with WFC_OPUS (tools/net/fetch_opus.py); without it available() is false and the session
// reports "voice unavailable".
#include "net/VoiceFrames.h"

#include <cstdint>
#include <vector>

namespace net {

bool voiceCodecAvailable();

class VoiceEncoder {
public:
    VoiceEncoder();
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;
    bool encode(const VoiceFrame& pcm, std::vector<uint8_t>& out);   // false = unavailable / error
private:
    void* enc_ = nullptr;
};

class VoiceDecoder {
public:
    VoiceDecoder();
    ~VoiceDecoder();
    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;
    bool decode(const uint8_t* data, size_t n, VoiceFrame& pcm);   // data == nullptr: conceal one lost frame
private:
    void* dec_ = nullptr;
};

}  // namespace net
