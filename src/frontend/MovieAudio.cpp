#include "frontend/MovieAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace frontend {

MovieAudioLayout downmixMovieAudio(const std::vector<std::vector<int16_t>>& tracks, int languageIndex,
                                   std::vector<int16_t>& stereo) {
    MovieAudioLayout L;
    L.tracks = (int)tracks.size();
    stereo.clear();
    if (tracks.empty()) { L.description = "no audio"; return L; }
    size_t n = 0;
    for (const auto& t : tracks) n = std::max(n, t.size());
    auto at = [&](int t, size_t i) -> float { return t >= 0 && t < (int)tracks.size() && i < tracks[(size_t)t].size() ? tracks[(size_t)t][i] : 0.0f; };
    const float c3 = 0.7071f, lfe = 0.5f;
    std::vector<float> l(n), r(n);
    if (tracks.size() >= 10) {
        L.dialogueTrack = 5 + std::max(0, std::min(4, languageIndex));
        L.description = "5.1 + 5 language centres; fold-down FL/FR + C(" + std::to_string(L.dialogueTrack) + ") + SL/SR + LFE";
        for (size_t i = 0; i < n; ++i) {
            float c = at(L.dialogueTrack, i) * c3, sub = at(4, i) * lfe;
            l[i] = at(0, i) + c + at(2, i) * c3 + sub;
            r[i] = at(1, i) + c + at(3, i) * c3 + sub;
        }
    } else if (tracks.size() == 1) {
        L.description = "mono";
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = at(0, i);
    } else {
        L.description = "tracks 0/1 as stereo";
        for (size_t i = 0; i < n; ++i) { l[i] = at(0, i); r[i] = at(1, i); }
    }
    // Peak limit: scale the whole mix down if the fold-down exceeds full scale (no per-sample clipping).
    float peak = 1.0f;
    for (size_t i = 0; i < n; ++i) peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
    float g = peak > 32767.0f ? 32767.0f / peak : 1.0f;
    stereo.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        stereo[i * 2] = (int16_t)std::lround(l[i] * g);
        stereo[i * 2 + 1] = (int16_t)std::lround(r[i] * g);
    }
    return L;
}

bool writeWav16Stereo(const std::string& path, const std::vector<int16_t>& stereo, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    uint32_t data = (uint32_t)(stereo.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32((uint32_t)rate); u32((uint32_t)rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    std::fwrite(stereo.data(), 2, stereo.size(), f);
    return std::fclose(f) == 0;
}

} // namespace frontend
