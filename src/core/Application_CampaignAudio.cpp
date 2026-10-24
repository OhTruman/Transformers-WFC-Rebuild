// [Systems] WFC_CAMPAIGNAUDIOTEST=<chapter audio JSON> (tools/systems/gen_level_audio.py --campaign): headless check of the
// campaign Kismet audio ops on real chapter data, with no campaign runtime yet (the Kismet triggers are Gameplay's next item).
// Every conversation is played through its own Play input and run to its end at 60 Hz: it must report Finished (all lines
// started and ended, the ducking preset pushed and popped) or Stopped with a logged reason (a line that cannot start). Every
// timeline with sound keys is played to its end: each key must fire once. Run with WFC_NULLRENDER=1 WFC_NULLRENDER_AUDIO=1
// WFC_MUTE=1 (the whole cue mix, silent), paced at real time (the device plays voices in wall time). Exit 0 = PASS.
#include "core/Application.h"
#include "core/Config.h"
#include "core/Log.h"
#include "assets/Json.h"
#include "game/AmbientAudio.h"
#include "game/CampaignSubtitles.h"
#include "game/LevelAudioScript.h"
#include "game/SoundCues.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

namespace core {

void Application::runCampaignAudioTest() {
    // A chapter JSON path, or a compiled-in level manifest's name (e.g. MP_ESC_BrokenHope: the MP / Escalation level audio).
    const char* path = std::getenv("WFC_CAMPAIGNAUDIOTEST");
    std::string text;
    if (const char* compiled = game::AmbientAudio::manifestJson(path)) text = compiled;
    else { std::ifstream f(path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); text = ss.str(); }
    assets::Json doc;
    if (text.empty() || !assets::Json::parse(text.data(), text.size(), doc)) { LOG_ERROR("CAMPAIGNAUDIO: cannot read %s", path); std::exit(2); }
    if (!audio_) { LOG_ERROR("CAMPAIGNAUDIO: no audio device (WFC_NULLRENDER_AUDIO=1)"); std::exit(2); }
    const char* ar = std::getenv("WFC_ASSETS");
    const std::string content = std::string(ar ? ar : config::kAssetRootDefault) + "/../content/";
    game::SoundCues cues;
    cues.load(audio_, content);
    const int nCues = cues.addCues(doc["cues"], content);
    game::LevelAudioScript script;
    const assets::Json& k = doc["kismet"];
    script.load(k);
    game::CampaignSubtitles subtitles;
    const int subCues = subtitles.load(doc);
    bool subtitleTimingBad = false;
    {   // [Systems QC] the HUD's timing (CampaignSubtitles::lineAt) on this chapter's real lines: each shown line at its time, nothing past the end
        int checked = 0, bad = 0;
        for (const std::string& cue : subtitles.cues()) {
            const auto* l = subtitles.lines(cue, "int");
            if (!l || l->empty()) continue;
            const float dur = subtitles.duration(cue);
            for (const auto& ln : *l) {
                const std::string got = game::CampaignSubtitles::lineAt(*l, ln.time, dur);
                bool later = false;   // a later line at the same time wins
                for (const auto& o : *l) later = later || (&o > &ln && o.time <= ln.time);
                if (!later && got != (ln.show ? ln.text : std::string())) ++bad;
                ++checked;
            }
            const float end = dur > 0.0f ? dur : l->back().time + 4.0f;
            if (!game::CampaignSubtitles::lineAt(*l, end + 0.1f, dur).empty()) ++bad;
        }
        LOG_INFO("CAMPAIGNAUDIO subtitle timing: %d lines checked, %d wrong", checked, bad);
        subtitleTimingBad = bad || (subCues > 0 && checked == 0);
    }
    int linesStarted = 0, linesWithText = 0;
    auto listen = [&](const std::string& cue, int) {
        ++linesStarted;
        if (const auto* l = subtitles.lines(cue, "int")) {
            ++linesWithText;
            if (!l->empty()) LOG_INFO("CAMPAIGNAUDIO subtitle %s @%.2f: %s%s", cue.c_str(), (*l)[0].time, (*l)[0].text.c_str(), (*l)[0].show ? "" : " (hidden)");
        }
    };
    LOG_INFO("CAMPAIGNAUDIO %s: %d cues, %d ops, %d links", doc["map"].asString().c_str(), nCues, script.opCount(), script.linkCount());
    const core::Vec3 listener{0, 0, 0};
    // Drive each op through a private trigger: a test link per op ("Test:<id>:<input>") added to a copy of the script.
    int fail = 0, convs = 0, finished = 0, stopped = 0, timelines = 0, keys = 0;
    // WFC_CAMPAIGNAUDIO_FIRE=<trigger>: fire that gameplay trigger (e.g. Game:TnSeqEvent_SurvivalEvents:Round End) 4 times, 12 s apart at
    // real time, with every op live (no per-op isolation): what the level's chains play, logged. Authored silences count (a RandomSwitch
    // with empty links: BrokenHope's Round End has 11 of 20): PASS = the trigger reaches the graph and some fire starts a dialog line,
    // or the trigger has no audio chain at all (reported).
    if (const char* fire = std::getenv("WFC_CAMPAIGNAUDIO_FIRE")) {
        script.setDialogListener(listen);
        int reachedAny = 0;
        for (int round = 0; round < 4; ++round) {
            const int before = linesStarted;
            const int reached = script.fire(fire, cues, nullptr, listener);
            for (float t = 0.0f; t < 12.0f; t += 1.0f / 60.0f) {
                script.tick(1.0f / 60.0f, listener, cues, nullptr);
                cues.setListener(listener); cues.tick(1.0f / 60.0f); audio_->update();
                std::this_thread::sleep_for(std::chrono::microseconds(16667));
            }
            LOG_INFO("CAMPAIGNAUDIO fire %s #%d: %d op inputs reached, %d dialog line(s) started", fire, round + 1, reached, linesStarted - before);
            reachedAny += reached;
        }
        const bool ok = reachedAny == 0 || linesStarted > 0;
        LOG_INFO("CAMPAIGNAUDIO RESULT %s: trigger %s, %d dialog lines over 4 fires%s", ok ? "PASS" : "FAIL", fire, linesStarted,
                 reachedAny == 0 ? " (no audio chain on this trigger)" : "");
        std::exit(ok ? 0 : 1);
    }
    const assets::Json& ops = k["ops"];
    for (size_t i = 0; i < ops.size(); ++i) {
        const std::string type = ops[i]["type"].asString(), id = ops[i]["id"].asString();
        const bool conv = type == "conversation", tl = type == "timeline" && ops[i]["sounds"].size() > 0;
        if (!conv && !tl) continue;
        assets::Json one = k;                                     // this op + a test trigger into its Play input
        assets::Json link; link.type = assets::Json::Type::Object;
        link.obj["from"].type = assets::Json::Type::String; link.obj["from"].strVal = "Test:play";
        link.obj["to"].type = assets::Json::Type::String; link.obj["to"].strVal = id;
        link.obj["input"].type = assets::Json::Type::Number; link.obj["input"].numVal = 0;
        one.obj["links"].arr.push_back(link);
        script.load(one);
        script.setDialogListener(listen);
        script.fire("Test:play", cues, nullptr, listener);
        const int shots0 = script.soundKeysFired();
        bool done = false;
        float t = 0.0f;
        for (; t < 120.0f && !done; t += 1.0f / 60.0f) {
            script.tick(1.0f / 60.0f, listener, cues, nullptr);
            cues.setListener(listener);
            cues.tick(1.0f / 60.0f);
            audio_->update();
            std::this_thread::sleep_for(std::chrono::microseconds(16667));   // the device plays in real time
            done = conv ? script.conversationsPlaying() == 0 : script.timelinesPlaying() == 0;
        }
        if (!done && std::getenv("WFC_CAMPAIGNAUDIODEBUG")) cues.logSpatial("CAMPAIGNAUDIO", listener, true);
        if (conv) {
            ++convs;
            const bool fin = script.hasFired("Out:" + id + ":Finished"), stp = script.hasFired("Out:" + id + ":Stopped");
            finished += fin; stopped += stp;
            LOG_INFO("CAMPAIGNAUDIO conversation %s: %zu lines, %s after %.2f s", id.c_str(), ops[i]["lines"].size(),
                     fin ? "Finished" : stp ? "Stopped" : "STILL RUNNING", t);
            if (!done || (!fin && !stp)) ++fail;
        } else {
            ++timelines;
            const int fired = script.soundKeysFired() - shots0;
            int want = 0;   // keys inside the matinee [0, length]: authored keys before 0 / past the end never play (UE3 position walk)
            for (size_t s = 0; s < ops[i]["sounds"].size(); ++s) {
                const float kt = ops[i]["sounds"][s]["time"].asFloat();
                want += kt >= 0.0f && kt <= ops[i]["length"].asFloat() ? 1 : 0;
            }
            keys += fired;
            LOG_INFO("CAMPAIGNAUDIO timeline %s: %d / %d sound keys, %.2f s", id.c_str(), fired, want, t);
            if (!done || fired != want) ++fail;
        }
        cues.stopAll();
    }
    LOG_INFO("CAMPAIGNAUDIO subtitles: %d cues with text; %d of %d started dialog lines resolved to a subtitle", subCues, linesWithText, linesStarted);
    if (subtitleTimingBad) ++fail;
    LOG_INFO("CAMPAIGNAUDIO RESULT %s: %d conversations (%d finished, %d stopped), %d timelines (%d keys), %d failures",
             fail ? "FAIL" : "PASS", convs, finished, stopped, timelines, keys, fail);
    std::exit(fail ? 1 : 0);
}

} // namespace core
