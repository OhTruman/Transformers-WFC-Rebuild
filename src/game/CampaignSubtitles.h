#pragma once
// [Systems] Campaign dialog subtitles (CAMPAIGN_AUDIO_DIALOG.md section 3). The chapter audio JSON (tools/systems/gen_level_audio.py
// --campaign) carries "subtitles": {wave: {duration, int: [{time, text, show}], fra: [...]}} from the chapter's _LOC packages
// (SoundNodeWaveEx Subtitles / Duration), and the cue trees that name each cue's localized waves. This table answers "a dialog cue
// started: which lines, when": the first localized wave of the cue that has lines in the language (else the English "int" text).
// show = the original's UseSubtitles (false: authored not to display - the HUD skips the line). Display is the HUD's (the original's
// subtitle movie clip; the profile's Subtitles option, default off).
#include <string>
#include <unordered_map>
#include <vector>
#include "assets/Json.h"

namespace game {

class CampaignSubtitles {
public:
    struct Line { float time = 0.0f; std::string text; bool show = true; };
    // Loads a chapter audio document (its "cues" and "subtitles"); returns the number of cues with subtitles.
    int load(const assets::Json& doc);
    void clear() { byWave_.clear(); cueWaves_.clear(); }
    // The lines for a dialog cue in a language ("int", "fra"; another / missing language falls back to "int"); null = none.
    const std::vector<Line>* lines(const std::string& cue, const std::string& lang = "int") const;
    float duration(const std::string& cue) const;   // the wave's authored Duration (s), 0 = unknown
    std::vector<std::string> cues() const;           // every cue with localized waves (tests)
    // The text on screen t seconds after a line's cue started: the last line with time <= t that the original shows (show), until
    // the wave's Duration (unknown: the last line's time + 4 s); "" before the first line, after the end, or for a hidden line.
    static std::string lineAt(const std::vector<Line>& lines, float t, float duration);
private:
    struct Wave { float duration = 0.0f; std::unordered_map<std::string, std::vector<Line>> byLang; };
    std::unordered_map<std::string, Wave> byWave_;
    std::unordered_map<std::string, std::vector<std::string>> cueWaves_;   // cue -> its localized waves, tree order
    const Wave* waveFor(const std::string& cue, const std::string& lang) const;
};

}  // namespace game
