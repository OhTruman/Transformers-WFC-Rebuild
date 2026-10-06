// Systems M08 resource-lifecycle probe (not part of the CMake build). One real Win32 audio device shared, as in the
// integration build, by the frontend's game::FrontendAudioRuntime and a match's level audio (LevelAudioHost + its
// SoundCues). Repeats:  logo movie (skipped) -> title -> party lobby -> game lobby -> [frontend level change]
//   -> map N (rotating through every processed MP map) with a rotating character profile: match start / progress /
//      nearly complete / objective / end messages, zone walk, death + round reset -> leave the match -> frontend ...
// and checks, at every boundary, voices / streams / decoded PCM / queued events / level cues / presets / music /
// announcer state; prints per-cycle maxima so growth shows. Usage: lifecycle_probe.exe [cycles]
// Build from the repo root (see audio_native_suite.cpp for the source list) + tools/systems/lifecycle_probe.cpp.
#include "audio/Audio.h"
#include "game/CharacterAudio.h"
#include "game/FrontendAudioRuntime.h"
#include "game/LevelAudioHost.h"
#include "assets/Json.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("  FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

int main(int argc, char** argv) {
    const int cycles = argc > 1 ? std::atoi(argv[1]) : 20;
    const std::string root = "F:/Transformers Rebuild/ExtractedAssets/";
    const std::string vs = root + "VerticalSlice";
    std::unique_ptr<audio::IAudio> a(audio::createAudio());
    if (!a->reportsVoices()) { std::printf("no audio device\n"); return 2; }
    std::unique_ptr<game::FrontendAudioRuntime> fe(new game::FrontendAudioRuntime(a.get()));
    game::SoundCues mcues;                                    // the match World's cue table (same device)
    mcues.load(a.get(), root + "content/");
    game::LevelAudioHost match(mcues);
    match.attach(a.get(), vs);
    const size_t feCues = fe->cues().cueCount(), mCues = mcues.cueCount();
    const int fePresets = fe->cues().mixer().presetCount(), mPresets = mcues.mixer().presetCount();
    size_t pcmFirst = 0;                                      // decoded PCM between frontend and match, cycle 0
    const char* maps[] = {"MP_IAC_Streets", "MP_ESC_BrokenHope", "MP_ESC_Remnant", "MP_IAC_Berth", "MP_IAC_Rust", "MP_IAC_Seed",
                          "MP_KON_Molten", "MP_ORB_Debris", "MP_UND_Complex", "MP_UND_Gorge"};
    const std::vector<std::string> chars = game::CharacterAudio::keys();
    const float dt = 1.0f / 30.0f;
    auto tickFe = [&](float secs) { for (float t = 0; t < secs; t += dt) { fe->tick(dt); Sleep(1); } };
    auto tickMatch = [&](float secs, const core::Vec3& p, bool alive = true) {
        for (float t = 0; t < secs; t += dt) { mcues.setListener(p); match.tick(dt, p, p, alive); mcues.tick(dt); fe->tick(dt); Sleep(1); }
    };
    std::printf("%-5s %-18s %-6s %8s %8s %8s %8s %9s %9s %6s\n", "cycle", "map", "char", "maxVoice", "maxInst", "maxPend", "streams", "PCMmax", "PCMafter",
                "lines");
    for (int cy = 0; cy < cycles; ++cy) {
        // --- frontend: a logo (skipped after 0.5 s), title, party lobby, game lobby
        fe->setMoviePlaying(true);
        fe->startMovieAudio(root + "movies/Logo_HighMoon.mkv");
        tickFe(0.5f);
        fe->stopMovieAudio();
        fe->setMoviePlaying(false);
        fe->uiLevelStarted("UI_FrontEnd_m"); tickFe(0.6f);
        fe->playUiSound("BUTTON_ACCEPT");
        fe->levelChange(); fe->uiLevelStarted("UI_PartyLobby_m"); tickFe(0.4f);
        fe->levelChange(); fe->setMoviePlaying(true); fe->prefetchLevel("UI_Lobby_m"); fe->setMoviePlaying(false);
        fe->uiLevelStarted("UI_Lobby_m"); tickFe(0.4f);
        fe->levelChange();                                    // travel to the match
        CHECK(fe->cues().liveInstances() == 0 && fe->cues().mapCueCount() == 0 && fe->state().music.empty() && a->openStreams() == 0,
              "cycle %d: frontend fully released before the match", cy);
        // --- the match
        // Orphaned worker decodes (a level unloaded mid-decode, M08q freeze fix) are released when they finish - let them settle.
        for (int k = 0; k < 600 && (mcues.orphanDecodes() > 0 || fe->cues().orphanDecodes() > 0); ++k) { mcues.tick(dt); Sleep(10); }
        const size_t pcmBefore = a->residentBytes();
        if (cy == 0) pcmFirst = pcmBefore;
        const int linesBefore = match.match().linesPlayed();
        const char* map = maps[cy % 10];
        const std::string ch = chars.empty() ? "Truck" : chars[(size_t)cy % chars.size()];
        CHECK(match.load(map), "cycle %d: %s loads", cy, map);
        const game::CharacterAudioProfile* prof = game::CharacterAudio::find(ch);
        if (prof) game::CharacterAudio::loadCues(mcues, *prof);
        game::MatchAudio& ma = match.match();
        ma.onMatchStarted(cy % 3 == 0 ? "DM" : "TDM", cy % 2);
        size_t maxInst = 0, maxPend = 0; int maxVoice = 0; size_t pcmMax = 0;
        auto sample = [&] { maxInst = std::max(maxInst, mcues.liveInstances()); maxPend = std::max(maxPend, mcues.pendingEvents());
                            maxVoice = std::max(maxVoice, a->activeVoices()); pcmMax = std::max(pcmMax, a->residentBytes()); };
        // walk: the spawn zone area and a few authored points (the bed + zones + pools run)
        std::ifstream f(vs + "/Maps/" + map + "/audio.json", std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        assets::Json aj; assets::Json::parse(ss.str(), aj);
        std::vector<core::Vec3> pts;
        for (size_t z = 0; z < aj["zones"].size() && pts.size() < 6; ++z) {
            const assets::Json& poly = aj["zones"][z]["trigger_polygons_gltf"][0];
            if (poly.size() < 3) continue;
            core::Vec3 c{0, 0, 0};
            for (size_t i = 0; i < poly.size(); ++i) c = c + core::Vec3{poly[i][0].asFloat(), poly[i][1].asFloat(), poly[i][2].asFloat()};
            pts.push_back(c * (1.0f / (float)poly.size()));
        }
        if (pts.empty()) pts.push_back({0, 0, 0});
        for (size_t i = 0; i < pts.size(); ++i) {
            tickMatch(0.4f, pts[i]); sample();
            if (i == 1) { ma.onProgressAnnouncement(1); ma.onGameNearlyComplete(); ma.flagMessage(1); }
            if (i == 2) { tickMatch(0.3f, pts[i], false); mcues.stopNonMapInstances(); match.resetMatch(); }   // death + round reset
            if (prof && i == 3) { const auto* c = prof->clip("Transform_ToVehicle_ROBO"); if (c && !c->notifies.empty()) mcues.play(prof->notifyCue(c->notifies[0]).c_str(), pts[i], 0.0f); }
        }
        ma.onMatchEnded(cy % 3 - 1, cy % 2 == 0);
        tickMatch(0.6f, pts[0]); sample();
        const int lines = ma.linesPlayed() - linesBefore;
        // --- leave the match
        mcues.stopNonMapInstances();
        match.unload();
        mcues.tick(dt);
        Sleep(40);
        const auto st = match.state();
        CHECK(mcues.liveInstances() == 0 && mcues.pendingEvents() == 0 && mcues.mapCueCount() == 0 && mcues.cueCount() == mCues &&
              mcues.mixer().presetCount() == mPresets && st.music.empty() && st.musicState == 0 && ma.currentCue().empty() &&
              ma.queuedCue().empty() && st.reverb.empty() && match.ambient().script().opCount() == 0,
              "cycle %d %s: match level fully released (live %zu pend %zu levelCues %d mixer [%s])", cy, map, mcues.liveInstances(),
              mcues.pendingEvents(), mcues.mapCueCount(), st.mixer.c_str());
        const size_t pcmAfter = a->residentBytes();
        CHECK(a->activeVoices() == 0 && a->openStreams() == 0 && pcmAfter <= pcmBefore,
              "cycle %d: device back to its pre-match state (voices %d streams %d PCM %.1f / %.1f MB)", cy, a->activeVoices(),
              a->openStreams(), pcmAfter / 1048576.0, pcmBefore / 1048576.0);
        CHECK(pcmBefore <= pcmFirst, "cycle %d: no PCM growth across cycles (%.1f MB, cycle 0 %.1f MB)", cy, pcmBefore / 1048576.0,
              pcmFirst / 1048576.0);
        std::printf("%-5d %-18s %-6s %8d %8zu %8zu %8d %8.1fM %8.1fM %6d\n", cy, map, ch.c_str(), maxVoice, maxInst, maxPend, a->openStreams(),
                    pcmMax / 1048576.0, pcmAfter / 1048576.0, lines);
    }
    fe.reset();
    Sleep(50);
    CHECK(a->activeVoices() == 0 && a->openStreams() == 0, "end: no voice / stream left");
    std::printf("match cue table %zu (base %zu), presets %d (base %d); frontend base %zu cues / %d presets -> %s\n", mcues.cueCount(), mCues,
                mcues.mixer().presetCount(), mPresets, feCues, fePresets, g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}
