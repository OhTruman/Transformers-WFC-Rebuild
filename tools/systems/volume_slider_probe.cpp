// Real-device check of the profile volume sliders (SetAudioGroupVolume): the frontend music's output peak follows the
// Music slider immediately (80 -> 40 is -6 dB, 0 is silent); the FX slider moves a running movie's level and leaves
// the music alone. Usage: volume_slider_probe.exe
#include "audio/Audio.h"
#include "game/FrontendAudioRuntime.h"
#include "game/SoundMixer.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

int main() {
    std::unique_ptr<audio::IAudio> a(audio::createAudio());
    if (!a->reportsVoices()) { std::printf("no audio device\n"); return 2; }
    std::unique_ptr<game::FrontendAudioRuntime> rt(new game::FrontendAudioRuntime(a.get()));
    // the mean of the per-block peaks over the window (the music is not stationary: compare like with like)
    auto window = [&](double secs, bool stream) {
        double sum = 0; int n = 0; float mx = -96.0f;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
            rt->tick(1.0f / 30.0f);
            Sleep(33);
            audio::MixStats s; a->mixStats(s);
            const float v = stream ? s.streamPeakDb : s.peakDb;
            mx = std::max(mx, v); sum += v; ++n;
        }
        return std::make_pair(n ? sum / n : -96.0, mx);
    };
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    rt->applyProfileVolumes(80, 80, 80);
    rt->uiLevelStarted("UI_FrontEnd_m");
    window(3.0, false);                                         // music fade-in
    // FX / Dialogue at 0 isolate the music (the frontend ambience beds are SFX and louder in peak).
    // Alternate so the music's own loudness drift averages out: 80, 40, 80, 40.
    double at80 = 0, at40 = 0;
    for (int r = 0; r < 2; ++r) {
        rt->applyProfileVolumes(80, 0, 0); at80 += window(2.0, false).first / 2;
        rt->applyProfileVolumes(40, 0, 0); at40 += window(2.0, false).first / 2;
    }
    // The ambience beds are SFX loops (stationary): the FX slider on them, music muted.
    double fx80 = 0, fx40 = 0;
    for (int r = 0; r < 3; ++r) {
        rt->applyProfileVolumes(0, 80, 0); window(0.3, false); fx80 += window(2.0, false).first / 3;
        rt->applyProfileVolumes(0, 40, 0); window(0.3, false); fx40 += window(2.0, false).first / 3;
    }
    std::printf("FX slider on the ambience beds: 80 -> %.1f dB, 40 -> %.1f dB (diff %.2f; expect -6.02)\n", fx80, fx40, fx40 - fx80);
    check(std::fabs((fx40 - fx80) + 6.02) < 1.0, "FX 80 -> 40 lowers the SFX ambience 6 dB");
    rt->applyProfileVolumes(0, 0, 0);
    window(2.0, false);                                         // the reverb return's tail decays
    const auto at0 = window(1.5, false);
    std::printf("music slider: 80 -> %.1f dB, 40 -> %.1f dB (diff %.2f; expect -6.02), 0 -> max %.1f dB\n", at80, at40, at40 - at80, at0.second);
    check(at40 - at80 < -2.5, "Music 80 -> 40 lowers the music (the track is not stationary: a mean-of-peaks estimate)");
    check(at0.second <= -90.0f, "all sliders 0: silent (music, ambience, reverb return)");
    rt->applyProfileVolumes(80, 80, 80);
    const auto back = window(2.0, false);
    check(back.first > at80 - 3.0, "Music back to 80 restores it");

    // A running movie follows the FX slider (GetMovieVolume's SFX class volume); the music group is untouched.
    rt->levelChange();
    rt->setMoviePlaying(true);
    if (!rt->startMovieAudio("F:/Transformers Rebuild/ExtractedAssets/movies/FMV_a1intro.mkv")) { std::printf("no movie\n"); return 2; }
    window(2.0, true);
    double m80 = 0, m40 = 0;
    for (int r = 0; r < 2; ++r) {
        rt->applyProfileVolumes(80, 80, 80); m80 += window(1.5, true).first / 2;
        rt->applyProfileVolumes(80, 40, 80); m40 += window(1.5, true).first / 2;
    }
    std::printf("movie FX slider: 80 -> %.1f dB, 40 -> %.1f dB (diff %.2f; expect -6.02)\n", m80, m40, m40 - m80);
    check(std::fabs((m40 - m80) + 6.02) < 2.0, "FX 80 -> 40 lowers the running movie about 6 dB");
    check(std::fabs(game::SoundMixer::groupVolume("MUSIC") - 0.8f) < 1e-6f, "the FX slider leaves the Music group alone");
    rt->stopMovieAudio(); rt->setMoviePlaying(false);
    rt->levelChange();
    window(0.5, false);
    rt.reset();
    std::printf("end: voices %d streams %d -> %s\n", a->activeVoices(), a->openStreams(), fails ? "FAIL" : "OK");
    return fails ? 1 : 0;
}
