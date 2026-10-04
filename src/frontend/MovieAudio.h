// Clean-room reconstruction — the audio of the shipped full-screen movies (Bink, extracted by AssetTools as .mkv with
// every Bink audio track as one mono FLAC stream).
//
// Track layout (measured, docs/FRONTEND.md "Movie audio"): 10 mono tracks.
//   0 / 1  front left / right music + effects bed (correlated pair)              [HIGH]
//   2 / 3  surround left / right (quiet bed, silent in dialogue windows)          [PARTIAL: order of 2 / 3]
//   4      LFE (lowest level)                                                     [PARTIAL]
//   5..9   one centre / dialogue track per language (EFIGS): mutually uncorrelated speech, identical on the logos
//          (no speech), each aligned with the INT subtitle timing                 [HIGH]
//   Which language track the native movie player selects is UNKNOWN (RE: the BinkSetSoundTrack call); the rebuild
//   uses the first (5) for INT.                                                   [PARTIAL]
// The stereo mix is a standard 5.1 fold-down (centre and surrounds at -3 dB, LFE at -6 dB), peak-limited.
// Movies with other track counts: 1 = mono, 2 = stereo, otherwise tracks 0 / 1.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace frontend {

struct MovieAudioLayout {
    int tracks = 0;
    int dialogueTrack = -1;   // the chosen language track (-1 none)
    std::string description;
};

// Folds the movie's tracks to interleaved 16-bit stereo at the source rate. `languageIndex` 0 = the first language
// track (INT). Returns the layout used.
MovieAudioLayout downmixMovieAudio(const std::vector<std::vector<int16_t>>& tracks, int languageIndex,
                                   std::vector<int16_t>& stereo);
bool writeWav16Stereo(const std::string& path, const std::vector<int16_t>& stereo, int rate);

} // namespace frontend
