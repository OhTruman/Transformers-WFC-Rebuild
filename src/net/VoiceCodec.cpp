// [Systems] Voice codec - see VoiceCodec.h.
#include "net/VoiceCodec.h"

#ifdef WFC_OPUS
#include <opus.h>
#endif

namespace net {

#ifdef WFC_OPUS
bool voiceCodecAvailable() { return true; }

VoiceEncoder::VoiceEncoder() {
    int err = 0;
    OpusEncoder* e = opus_encoder_create(kVoiceRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !e) return;
    opus_encoder_ctl(e, OPUS_SET_BITRATE(16000));
    opus_encoder_ctl(e, OPUS_SET_VBR(0));                 // constant size: a predictable voice lane
    opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(1));          // some resilience to a lost frame
    opus_encoder_ctl(e, OPUS_SET_PACKET_LOSS_PERC(5));
    enc_ = e;
}
VoiceEncoder::~VoiceEncoder() { if (enc_) opus_encoder_destroy((OpusEncoder*)enc_); }
bool VoiceEncoder::encode(const VoiceFrame& pcm, std::vector<uint8_t>& out) {
    if (!enc_) return false;
    out.resize(256);
    const opus_int32 n = opus_encode((OpusEncoder*)enc_, pcm.data(), kVoiceFrameSamples, out.data(), (opus_int32)out.size());
    if (n < 0) { out.clear(); return false; }
    out.resize((size_t)n);
    return true;
}

VoiceDecoder::VoiceDecoder() {
    int err = 0;
    OpusDecoder* d = opus_decoder_create(kVoiceRate, 1, &err);
    if (err == OPUS_OK) dec_ = d;
}
VoiceDecoder::~VoiceDecoder() { if (dec_) opus_decoder_destroy((OpusDecoder*)dec_); }
bool VoiceDecoder::decode(const uint8_t* data, size_t n, VoiceFrame& pcm) {
    if (!dec_) return false;
    const int got = opus_decode((OpusDecoder*)dec_, data, data ? (opus_int32)n : 0, pcm.data(), kVoiceFrameSamples, 0);
    return got == kVoiceFrameSamples;
}
#else
bool voiceCodecAvailable() { return false; }
VoiceEncoder::VoiceEncoder() {}
VoiceEncoder::~VoiceEncoder() {}
bool VoiceEncoder::encode(const VoiceFrame&, std::vector<uint8_t>& out) { out.clear(); return false; }
VoiceDecoder::VoiceDecoder() {}
VoiceDecoder::~VoiceDecoder() {}
bool VoiceDecoder::decode(const uint8_t*, size_t, VoiceFrame&) { return false; }
#endif

}  // namespace net
