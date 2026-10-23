// [Systems] Campaign dialog subtitles - see CampaignSubtitles.h.
#include "game/CampaignSubtitles.h"

namespace game {
namespace {

void collectWaves(const assets::Json& n, std::vector<std::string>& out) {
    if (n["class"].asString() == "SoundNodeWaveEx" && !n["loc"].asString().empty()) out.push_back(n["node"].asString());
    const assets::Json& ch = n["children"];
    for (size_t i = 0; i < ch.size(); ++i) collectWaves(ch[i], out);
}

}  // namespace

int CampaignSubtitles::load(const assets::Json& doc) {
    clear();
    const assets::Json& subs = doc["subtitles"];
    for (const auto& kv : subs.obj) {
        Wave w;
        w.duration = kv.second["duration"].asFloat(0.0f);
        for (const auto& lang : kv.second.obj) {
            if (lang.first == "duration") continue;
            std::vector<Line>& lines = w.byLang[lang.first];
            for (size_t i = 0; i < lang.second.size(); ++i) {
                const assets::Json& l = lang.second[i];
                lines.push_back({l["time"].asFloat(0.0f), l["text"].asString(), l["show"].asBool(true)});
            }
        }
        byWave_[kv.first] = std::move(w);
    }
    int n = 0;
    for (const auto& kv : doc["cues"].obj) {
        std::vector<std::string> waves;
        collectWaves(kv.second["tree"], waves);
        bool any = false;
        for (const std::string& w : waves) any = any || byWave_.count(w) != 0;
        if (!waves.empty()) cueWaves_[kv.first] = std::move(waves);
        n += any ? 1 : 0;
    }
    return n;
}

const CampaignSubtitles::Wave* CampaignSubtitles::waveFor(const std::string& cue, const std::string& lang) const {
    auto it = cueWaves_.find(cue);
    if (it == cueWaves_.end()) return nullptr;
    for (const std::string& name : {lang, std::string("int")})
        for (const std::string& w : it->second) {
            auto wi = byWave_.find(w);
            if (wi != byWave_.end() && wi->second.byLang.count(name)) return &wi->second;
        }
    return nullptr;
}

const std::vector<CampaignSubtitles::Line>* CampaignSubtitles::lines(const std::string& cue, const std::string& lang) const {
    const Wave* w = waveFor(cue, lang);
    if (!w) return nullptr;
    auto l = w->byLang.find(lang);
    if (l == w->byLang.end()) l = w->byLang.find("int");
    return l == w->byLang.end() ? nullptr : &l->second;
}

float CampaignSubtitles::duration(const std::string& cue) const {
    const Wave* w = waveFor(cue, "int");
    return w ? w->duration : 0.0f;
}

}  // namespace game
