#include "game/MatchAudio.h"
#include "game/AmbientAudio.h"
#include "game/MusicPlayer.h"
#include "game/SoundCues.h"
#include "core/Log.h"

#include <cstdlib>

namespace game {
namespace {

bool logOn() { static const bool on = std::getenv("WFC_MATCHAUDIOLOG") != nullptr; return on; }

// The compiled-in message class defaults (LevelAudio.inc "__match_messages__").
const assets::Json& messages() {
    static assets::Json j;
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* s = AmbientAudio::manifestJson("__match_messages__")) assets::Json::parse(std::string(s), j);
    }
    return j;
}

std::string teamCharacter(int team) {
    const assets::Json& a = messages()["announcer"];
    return team == 1 ? a["team1_dialog_character"].asString() : a["team0_dialog_character"].asString();
}

} // namespace

bool MatchAudio::hasMessageClass(const std::string& c) { return messages()["game_type_messages"].has(c); }

void MatchAudio::reset() {
    modeClass_.clear();
    current_.clear();
    queued_.clear();
    instance_ = -1;
    dialogChar_ = teamCharacter(0);                    // Initialize: Team0DialogCharacter
}

void MatchAudio::setLocalTeam(int team) {
    if (team == 0 || team == 1) dialogChar_ = teamCharacter(team);
}

bool MatchAudio::speaking() const { return instance_ >= 0 && cues_.playing(instance_); }

bool MatchAudio::higherPriority(const std::string& cue) const {
    if (queued_.empty()) return true;
    const cuedata::CueDef* q = cues_.cueDef(queued_.c_str());
    const cuedata::CueDef* n = cues_.cueDef(cue.c_str());
    if (!q || !n) return false;
    return !(q->priority > n->priority);
}

bool MatchAudio::play(const std::string& cue) {
    if (cue.empty() || !cues_.hasCue(cue.c_str())) return false;
    if (speaking()) {                                  // PlayInternal while the component plays
        if (cue == current_) queued_.clear();
        else if (higherPriority(cue)) queued_ = cue;
        if (logOn()) LOG_INFO("ANNOUNCER busy (%s): %s -> queued '%s'", current_.c_str(), cue.c_str(), queued_.c_str());
        return true;
    }
    SoundCues::Emitter e; e.owner = SoundCues::kUI;    // the WorldInfo's dialog component (k2D cues)
    instance_ = cues_.playDialog(cue.c_str(), e, dialogChar_);
    current_ = cue;
    ++lines_;
    if (logOn()) LOG_INFO("ANNOUNCER play %s as %s (instance %d)", cue.c_str(), dialogChar_.c_str(), instance_);
    return instance_ >= 0;
}

bool MatchAudio::announcerEvent(const std::string& event) {
    auto it = events_.find(event);
    if (it == events_.end()) { if (logOn()) LOG_INFO("ANNOUNCER no cue for %s", event.c_str()); return false; }
    return play(it->second);
}

void MatchAudio::tick(float) {
    if (!queued_.empty() && !speaking()) {             // TnAnnouncer.Tick
        const std::string q = queued_;
        queued_.clear();
        play(q);
    }
}

bool MatchAudio::gameTypeMessage(const std::string& cls, int sw, int winnerTeam, bool localPlayerWon) {
    const assets::Json& m = messages()["game_type_messages"][cls];
    if (!m.isObject()) return false;
    MusicTrack t;
    t.fadeIn = 0.0f; t.fadeOut = 0.0f; t.priority = 0;
    switch (sw) {
    case 0:
        announcerEvent(m["GameTypeDialog"].asString());
        announcerEvent(m["GameDescriptionDialog"].asString());
        t.cue = m["GameTypeMusic"].asString();
        break;
    case 1:
        t.cue = m["GameNearlyCompleteMusic"].asString();
        break;
    case 2: {
        t.priority = 1;
        const bool ffa = cls == "TnGameTypeMessageDM";  // TnGameTypeMessageDM.GetEndGameMusic compares the PRI
        if (ffa) t.cue = winnerTeam < 0 ? m["TieMusic"].asString()
                                        : (localPlayerWon ? m["AutobotsWinMusic"].asString() : m["DecepticonsWinMusic"].asString());
        else t.cue = winnerTeam < 0 ? m["TieMusic"].asString()
                                    : (winnerTeam == 0 ? m["AutobotsWinMusic"].asString() : m["DecepticonsWinMusic"].asString());
        break;
    }
    default: return false;
    }
    if (!t.cue.empty() && cues_.hasCue(t.cue.c_str())) music_.playMusic(t);
    if (logOn()) LOG_INFO("MATCHAUDIO %s switch %d -> music %s", cls.c_str(), sw, t.cue.c_str());
    return true;
}

bool MatchAudio::progressAnnouncement(int sw) {
    const assets::Json& s = messages()["progress_announcement_sounds"];
    if (sw < 0 || sw >= (int)s.size()) return false;
    return announcerEvent(s[(size_t)sw].asString());
}

bool MatchAudio::versusGameOver(int winnerTeam) {
    const assets::Json& g = messages()["versus_game_over"];
    if (winnerTeam == 0) return announcerEvent(g["AutobotWinSound"].asString());
    if (winnerTeam == 1) return announcerEvent(g["DecepticonWinSound"].asString());
    return false;                                      // a tie: no win line
}

std::string MatchAudio::messageClassForMode(const std::string& tag) {
    return messages()["mode_messages"][tag].asString();
}

bool MatchAudio::onMatchStarted(const std::string& modeTag, int localTeam) {
    modeClass_ = messageClassForMode(modeTag);
    setLocalTeam(localTeam);
    return !modeClass_.empty() && gameTypeMessage(modeClass_, 0);
}

bool MatchAudio::onGameNearlyComplete() { return !modeClass_.empty() && gameTypeMessage(modeClass_, 1); }

bool MatchAudio::onMatchEnded(int winnerTeam, bool localPlayerWon) {
    if (modeClass_.empty()) return false;
    gameTypeMessage(modeClass_, 2, winnerTeam, localPlayerWon);
    if (modeClass_ != "TnGameTypeMessageDM") versusGameOver(winnerTeam);   // TnVersusGame only (team games)
    return true;
}

} // namespace game
