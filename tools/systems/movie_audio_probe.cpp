// Systems M07 boot / movie-audio lifecycle probe (not part of the CMake build). Drives game::FrontendAudioRuntime - the
// object the frontend seam forwards to - through the shipped boot flow on the REAL Win32 audio device:
//   cold boot -> Logo_Activision -> Logo_Hasbro -> Logo_HighMoon -> FMV_intro (skipped after a few seconds) ->
//   [FRONTEND START] title (music, ambience) -> menu sounds -> party lobby -> game lobby -> loading Bink (no sound)
//   -> match level change -> back to the frontend (logo chain skipped: intro watched) -> second boot of the chain.
// Per step: movie stream peak, game-mix peak, music state, stream / voice / PCM counts; checks the lifecycle rules.
// Build from the repo root:
//   .toolchain/llvm-mingw-*/bin/clang++.exe -std=c++17 -O2 -Isrc tools/systems/movie_audio_probe.cpp \
//     src/platform/win32/Win32MovieAudio.cpp src/platform/win32/Win32Audio.cpp src/game/SoundCues.cpp \
//     src/game/SoundMixer.cpp src/game/AmbientAudio.cpp src/game/LevelAudioScript.cpp src/game/LevelAudioHost.cpp \
//     src/game/FrontendAudioRuntime.cpp src/game/MusicPlayer.cpp src/game/FrontendAudio.cpp src/core/Log.cpp \
//     -lwinmm -lole32 -static -o work/m7/movie_probe.exe
// Usage: movie_probe.exe [seconds per logo (0 = whole)]
#include "audio/Audio.h"
#include "game/FrontendAudioRuntime.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

static int g_fail = 0;
#define CHECK(c, ...) do { if (c) std::printf("  ok   "); else { std::printf("  FAIL "); ++g_fail; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

struct Peaks { float movie = -96.0f, game = -96.0f; long long underrun = 0; };

int main(int argc, char** argv) {
    const std::string root = "F:/Transformers Rebuild/ExtractedAssets/";
    const double cap = argc > 1 ? std::atof(argv[1]) : 3.0;
    std::unique_ptr<audio::IAudio> a(audio::createAudio());
    if (!a->reportsVoices()) { std::printf("no audio device\n"); return 2; }
    const size_t pcm0 = a->residentBytes();
    std::unique_ptr<game::FrontendAudioRuntime> rt(new game::FrontendAudioRuntime(a.get()));
    const size_t pcmBase = a->residentBytes();
    auto run = [&](double secs, Peaks* pk = nullptr) {
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
            rt->tick(1.0f / 30.0f);
            Sleep(33);
            if (pk) {
                audio::MixStats s; a->mixStats(s);
                pk->movie = std::max(pk->movie, s.streamPeakDb);
                pk->game = std::max(pk->game, s.peakDb);
            }
        }
    };
    auto underrun = [&] { audio::MixStats s; a->mixStats(s); return s.streamUnderrunFrames; };
    auto movie = [&](const char* name, double secs, bool skip) {
        Peaks pk;
        const long long u0 = underrun();
        const bool has = rt->startMovieAudio(root + "movies/" + name + ".mkv");
        const auto t0 = std::chrono::steady_clock::now();
        if (has) {
            const auto end = t0 + std::chrono::duration<double>(secs);
            while (std::chrono::steady_clock::now() < end && !rt->movieAudioFinished()) run(0.1, &pk);
        } else run(std::min(secs, 1.5), &pk);
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double clock = rt->movieAudioClock();
        const bool fin = rt->movieAudioFinished();
        rt->stopMovieAudio();                                  // movie end / skip
        pk.underrun = underrun() - u0;
        auto st = rt->state();
        std::printf("  %-20s audio %d  movie peak %6.1f dB  game mix %6.1f dB  clock %6.2f / wall %6.2f s  %s  streams after %d\n",
                    name, has, pk.movie, pk.game, clock, wall, skip ? "SKIPPED" : (fin ? "ended" : "cut"), st.streams);
        return std::make_pair(has, pk);
    };

    std::printf("[cold boot -> intro chain]\n");
    rt->setMoviePlaying(true);                                 // the chain is up (MovieLoader -> enterMovieSequence)
    for (const char* m : {"Logo_Activision", "Logo_Hasbro", "Logo_HighMoon"}) {
        auto r = movie(m, cap > 0 ? cap : 60.0, false);
        CHECK(r.first && r.second.movie > -30.0f && r.second.game <= -90.0f,
              "%s: its own sound plays (%.1f dB); nothing else under it (%.1f dB)", m, r.second.movie, r.second.game);
        CHECK(rt->state().streams == 0, "%s: its sound stops with it", m);
    }
    auto intro = movie("FMV_intro", 4.0, true);                 // the player skips the intro
    CHECK(intro.first && intro.second.movie > -30.0f && rt->state().streams == 0, "FMV_intro plays, the skip stops its sound at once");
    CHECK(rt->state().music.empty() && rt->cues().liveInstances() == 0, "no frontend music during the chain");
    // [FRONTEND START] (MovieStopped:FMV_intro path) - the chain ends: the game mix comes back, the title music starts.
    rt->setMoviePlaying(false);
    rt->uiLevelStarted("UI_FrontEnd_m");
    Peaks title; run(3.0, &title);
    auto st = rt->state();
    CHECK(st.music == "BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01" && st.musicState == 2 && st.reverb == "REVERB_TRANS_FRONT_END" &&
          title.game > -40.0f && title.movie <= -90.0f && st.masterScale > 0.99f,
          "title: music %s, reverb %s, game mix back (%.1f dB, master %.2f), no movie sound", st.music.c_str(), st.reverb.c_str(), title.game, st.masterScale);
    CHECK(rt->playUiSound("BUTTON_START") >= 0 && rt->playUiSound("BUTTON_ACCEPT") >= 0 && rt->playUiSound("BUTTON_DOWN") >= 0 &&
          rt->playUiSound("BUTTON_BACK") >= 0, "menu sounds: start / accept / selection / back");
    run(1.0);
    std::printf("[menus -> lobbies -> loading -> match]\n");
    rt->levelChange(); rt->uiLevelStarted("UI_PartyLobby_m"); run(2.0);
    CHECK(rt->state().music == "BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX" && rt->cues().activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01") == 0,
          "party lobby music replaces the title music (no double)");
    rt->levelChange();
    // Loading into the game lobby: the loading underlay Bink (no sound) is up; prefetch the lobby music.
    rt->setMoviePlaying(true);
    auto load = movie("TF_LoadingScreen_INT", 1.5, false);
    CHECK(!load.first && load.second.game <= -90.0f, "loading Bink: no movie sound (authored none), game mix muted");
    rt->prefetchLevel("UI_Lobby_m");
    rt->setMoviePlaying(false);
    rt->uiLevelStarted("UI_Lobby_m"); run(2.0);
    CHECK(rt->state().music == "BL_LVL_HUD_INTERFACE.MP_LOBBY_MX", "game lobby music");
    // Loading into the match: the frontend level goes, the movie preset is up across the level change.
    rt->levelChange();
    rt->setMoviePlaying(true);
    rt->levelChange();                                         // a level change (Flush) while the loading movie is up
    st = rt->state();
    CHECK(st.movie && st.masterScale == 0.0f && st.mixer.find("CINE_MUTE_FOR_BINK") != std::string::npos,
          "UnflushableMixerPresets: the movie preset survives the Flush [%s]", st.mixer.c_str());
    run(1.0);
    rt->setMoviePlaying(false);
    run(1.2);
    st = rt->state();
    CHECK(st.instances == 0 && st.level.empty() && st.music.empty() && a->activeVoices() == 0 && st.streams == 0 &&
          st.masterScale > 0.99f && a->residentBytes() == pcmBase,
          "in the match: no frontend audio left (instances %d, voices %d, streams %d, PCM %.1f MB = base %.1f)",
          st.instances, a->activeVoices(), st.streams, a->residentBytes() / 1048576.0, pcmBase / 1048576.0);
    std::printf("[return to the frontend (intro watched: enterFrontEnd) -> second chain]\n");
    rt->uiLevelStarted("UI_FrontEnd_m"); run(2.0);
    CHECK(rt->state().music == "BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01" && rt->cues().activeInstances("BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01") == 1,
          "return: title music once");
    rt->levelChange();
    rt->setMoviePlaying(true);
    for (int i = 0; i < 3; ++i) {                              // repeated boots: no stream / voice / memory growth
        for (const char* m : {"Logo_Activision", "Logo_HighMoon"}) movie(m, 1.0, true);
    }
    rt->setMoviePlaying(false);
    run(1.2);
    CHECK(a->openStreams() == 0 && a->activeVoices() == 0 && a->residentBytes() == pcmBase, "repeated chains: streams 0, voices 0, PCM %.1f MB",
          a->residentBytes() / 1048576.0);
    rt.reset();
    std::printf("end: streams %d, voices %d, PCM %.1f MB (device base %.1f MB) -> %s\n", a->openStreams(), a->activeVoices(),
                a->residentBytes() / 1048576.0, pcm0 / 1048576.0, g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}
