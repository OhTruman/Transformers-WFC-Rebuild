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
    if (event.empty()) return false;                 // authored None (e.g. TnGameTypeMessageCTF has no GameTypeDialog): silent
    auto it = events_.find(event);
    if (it == events_.end()) { if (logOn()) LOG_INFO("ANNOUNCER no cue for %s", event.c_str()); return false; }
    return play(it->second);
}

void MatchAudio::tick(float dt) {
    clock_ += dt;
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
    return false;                                      // a tie: no win line [CONF RE MP sweep S5: forfeits keep it]
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

namespace {
const assets::Json& objective(const char* cls) { return messages()["objective_messages"][cls]; }
} // namespace

bool MatchAudio::flagMessage(int sw) {
    const assets::Json& m = objective("TnFlagMessage");
    static const char* kStinger[4] = {"ReturnedSound", "PickupSound", "DropSound", "ScoredSound"};
    static const char* kDialog[4] = {"ReturnedDialogSound", "PickupDialogSound", "DropDialogSound", "ScoredDialogSound"};
    if (sw < 0 || sw > 3 || !m.isObject()) return false;
    const std::string& st = m[kStinger[sw]].asString();
    if (!st.empty() && cues_.hasCue(st.c_str())) { SoundCues::Emitter e; e.owner = SoundCues::kUI; cues_.play(st.c_str(), e, 0.0f); }
    announcerEvent(m[kDialog[sw]].asString());
    return true;
}

bool MatchAudio::bombMessage(int sw, int team) {
    const assets::Json& m = objective("TnBombMessage");
    std::string ev;
    switch (sw) {
    case 1: ev = m[team == 0 ? "AutobotPickupDialogSound" : "DecepticonPickupDialogSound"].asString(); break;
    case 2: ev = m["DropDialogSound"].asString(); break;
    case 3: ev = m[team == 0 ? "AutobotDetonatedDialogSound" : "DecepticonDetonatedDialogSound"].asString(); break;
    case 4: ev = m["DefusedDialogSound"].asString(); break;
    case 5: ev = m["PlantedDialogSound"].asString(); break;
    default: return false;
    }
    return announcerEvent(ev);
}

bool MatchAudio::dominationMessage(int sw) {
    const assets::Json& m = objective("TnDominationMessage");
    static const char* kLists[4] = {"AutobotsTakePointDialog", "DecepticonsTakePointDialog", "AutobotsCapturingPointDialog",
                                    "DecepticonsCapturingPointDialog"};
    const int type = sw % 10, point = sw / 10;
    if (type < 0 || type > 3) return false;
    return announcerEvent(m[kLists[type]][(size_t)point].asString());
}

bool MatchAudio::ctfMessage(bool localTeamAttacks) {
    const assets::Json& m = objective("TnCTFMessage");
    return announcerEvent(m[localTeamAttacks ? "AttackerDialog" : "DefenderDialog"].asString());
}

bool MatchAudio::roundMessage(int sw) {
    const assets::Json& m = objective("TnRoundBasedGameMessage");
    if (sw < 0 || sw >= (int)m["DialogSound"].size()) return false;
    announcerEvent(m["DialogSound"][(size_t)sw].asString());
    const char* field = sw == 2 ? "TimeUpMusic" : sw == 3 ? "SwitchingSidesMusic" : nullptr;
    if (field && !m[field].asString().empty()) {
        MusicTrack t; t.cue = m[field].asString(); t.fadeIn = 0.0f; t.fadeOut = 0.0f; t.priority = 0;
        if (cues_.hasCue(t.cue.c_str())) music_.playMusic(t);
    }
    return true;
}

void MatchAudio::kothMatchStarting() {
    kothIgnoreUntil_ = clock_ + messages()["countdown"]["koth_announcer_start_hysteresis"].asFloat(3.0f);
}

bool MatchAudio::playUiCue(const std::string& q) {
    if (q.empty() || !cues_.hasCue(q.c_str())) return false;
    SoundCues::Emitter e;
    e.owner = SoundCues::kUI;                          // PlaySound on the GRI (no location): non-positional
    return cues_.play(q.c_str(), e, 0.0f) >= 0;
}

bool MatchAudio::killstreakActivated(const std::string& id, StreakRole role, int activatorTeam) {
    const assets::Json& k = messages()["killstreaks"][id];
    const char* field = role == StreakRole::Self ? "self" : role == StreakRole::Friendly ? "friendly" : "enemy";
    std::string ev = k[field].asString();
    const assets::Json& fac = k["faction"];
    if (ev.empty() && activatorTeam >= 0 && (size_t)activatorTeam < fac.size()) ev = fac[(size_t)activatorTeam].asString();
    return !ev.empty() && announcerEvent(ev);
}

bool MatchAudio::objectiveBroadcast(const std::string& tag, int value) {
    auto sub = [&](const char* cls) -> std::string {
        if (tag.rfind(cls, 0) != 0) return "#";
        const size_t a = tag.find('('), b = tag.find(')');
        return a == std::string::npos || b == std::string::npos ? std::string() : tag.substr(a + 1, b - a - 1);
    };
    std::string s = sub("TnFlagMessage");
    if (s != "#") {
        const int sw = s == "taken" ? 1 : s == "dropped" ? 2 : s == "captured" ? 3 : s == "returned" ? 0 : -1;
        return sw >= 0 && flagMessage(sw);
    }
    s = sub("TnBombMessage");
    if (s != "#") {
        const int sw = s == "taken" ? 1 : s == "dropped" ? 2 : s == "detonated" ? 3 : s == "defused" ? 4 : s == "planted" ? 5 : -1;
        return sw >= 0 && bombMessage(sw, value);
    }
    if (tag == "TnDominationMessage") return dominationMessage(value);
    return false;
}

bool MatchAudio::countdownChanged(int current, bool countingDown) {
    const assets::Json& c = messages()["countdown"];
    if (!countingDown || current < 0 || current > c["low_tick_threshold"].asInt(10)) return false;
    return playUiCue(c["low_tick_sound"].asString());
}

bool MatchAudio::objectiveCountdownChanged(int current) {
    const assets::Json& c = messages()["countdown"];
    if (current == -1 || current > c["objective_tick_threshold"].asInt(5)) return false;
    return playUiCue(c["objective_tick_sound"].asString());
}

bool MatchAudio::kothZoneActivated(bool matchOver) {
    if (matchOver || clock_ < kothIgnoreUntil_) return false;          // PlayAnnouncerDialog: IgnoringAnnouncer
    return announcerEvent(objective("TnKingOfTheHillZone")["ZoneChangeSound"].asString());
}

bool MatchAudio::kothDefenderChanged(int team, bool ignoring, bool matchOver) {
    if (ignoring || matchOver || clock_ < kothIgnoreUntil_) return false;
    const assets::Json& m = objective("TnKingOfTheHillZone");
    const char* f = team == 0 ? "CapturedSoundAutobotSound" : team == 1 ? "CapturedSoundDecepticonSound"
                  : team == 254 ? "ContestedSound" : team == 255 ? "NeutralSound" : nullptr;
    return f && announcerEvent(m[f].asString());
}

} // namespace game
