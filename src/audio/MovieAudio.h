// Clean-room reconstruction — full-screen movie AUDIO (Systems). The shipped Bink movies (Game Dump
// TransGame/Movies/*.bik; AssetTools ExtractedAssets/movies/*.mkv = H.264 + one FLAC stream per Bink audio track)
// carry their sound as separate mono Bink tracks. This plays them through the Systems audio backend; the video and
// the movie sequencing stay with the Frontend lane's movie player (platform::IMoviePlayer).
//
// Evidence (tools/systems/bink_tracks.py, movie_lang_*.py; Systems M07):
//   [CONF data] every logo / intro / campaign FMV has 10 mono 48 kHz DCT tracks, Bink track IDs 0..9; the chapter
//       text movies 6, the credits 2; TF_LoadingScreen_* / TF_InitialStartup_* / LoadingScreenAlpha have none.
//   [CONF data] track 4 is the LFE (87-100 % of its energy below 120 Hz); tracks 0/1 a front pair, 2/3 a quieter pair
//       (surrounds); tracks 5..9 are bit-identical in the dialogue-free logos and mutually uncorrelated (r ~ 0) in the
//       dialogue movies: five per-language centre channels. The 6-track text movies: 0..4 the same + one centre.
//   [HIGH] layout FL, FR, SL, SR, LFE, C[language]; the left / right order of the 2/3 pair is the standard one.
//   [UNKNOWN] which of 5..9 is INT (English): neither the subtitle timing nor a voice-spectrum match is conclusive and
//       the native track selection (HmPlayerController.MovieAudioSetup, a native; the Bink library) is not traced.
//       The logos (centre identical on 5..9) are exact for any choice; dialogue movies use `languageSlot` (default 0 =
//       track 5) [PROVISIONAL].
//   [CONF config] Engine.MovieSettings MoviesToAlwaysPlaySound = the three logos (all movies play sound; the logos
//       even when the console's own music would suppress movie sound - console behaviour, not modelled).
//   [CONF config] [HM_Engine.FmodAudioDevice] MovieMixerPreset=CINE_MUTE_FOR_BINK (the GAME mix's Master to 0 while
//       a movie plays) and [HM_Engine.SoundMixerProperties] UnflushableMixerPresets=CINE_MUTE_FOR_BINK. The movie's
//       own sound is not part of that mix: the stream bypasses the category / Master scale (IAudio streams).
// Stereo fold-down [PROVISIONAL: the platform's default 5.1 -> stereo matrix]: L = FL + 0.707 C + 0.707 SL,
// R = FR + 0.707 C + 0.707 SR, LFE dropped.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace audio {

class IAudio;

struct MovieTrackLayout { int fl = -1, fr = -1, sl = -1, sr = -1, lfe = -1, c = -1; };
// Track roles for a movie with `tracks` mono audio tracks (10: shared 0..4 + language centres 5..9; 6: 0..4 + 5;
// 2: front pair; 1: centre). `languageSlot` picks the centre among 5..9 (out of range -> the first).
inline MovieTrackLayout movieTrackLayout(int tracks, int languageSlot) {
    MovieTrackLayout l;
    if (tracks >= 6) {
        l.fl = 0; l.fr = 1; l.sl = 2; l.sr = 3; l.lfe = 4;
        const int slot = languageSlot >= 0 && languageSlot < tracks - 5 ? languageSlot : 0;
        l.c = 5 + slot;
    } else if (tracks >= 2) {
        l.fl = 0; l.fr = 1;
    } else if (tracks == 1) {
        l.c = 0;
    }
    return l;
}
// Fold planar mono tracks (tracks[i][frame]) to interleaved stereo (a mono movie: its track at full level on both).
inline void movieDownmix(const MovieTrackLayout& l, const float* const* t, size_t frames, float* lr) {
    constexpr float k = 0.70710678f;
    const bool monoOnly = l.fl < 0 && l.fr < 0 && l.c >= 0;
    for (size_t f = 0; f < frames; ++f) {
        if (monoOnly) { lr[f * 2] = lr[f * 2 + 1] = t[l.c][f]; continue; }
        const float c = l.c >= 0 ? t[l.c][f] * k : 0.0f;
        float L = c, R = c;
        if (l.fl >= 0) L += t[l.fl][f];
        if (l.fr >= 0) R += t[l.fr][f];
        if (l.sl >= 0) L += t[l.sl][f] * k;
        if (l.sr >= 0) R += t[l.sr][f] * k;
        lr[f * 2] = L;
        lr[f * 2 + 1] = R;
    }
}

class MovieAudioPlayer {
public:
    virtual ~MovieAudioPlayer() = default;
    // Opens the movie file's audio tracks (false: none / unreadable). Decoding starts on its own thread; the sound
    // starts with start().
    virtual bool open(IAudio* a, const std::string& path, int languageSlot = 0) = 0;
    virtual void start() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void stop() = 0;                       // the stream closes at once (skip / movie end)
    virtual int tracks() const = 0;
    virtual double clock() const = 0;              // seconds of movie audio played (the stream clock)
    virtual double duration() const = 0;
    virtual bool finished() const = 0;             // decoded to the end and everything played
};

// Platform factory (Win32: Media Foundation decode thread, src/platform/win32/Win32MovieAudio.cpp).
MovieAudioPlayer* createMovieAudioPlayer();

} // namespace audio
