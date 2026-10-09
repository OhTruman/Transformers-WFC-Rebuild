// Clean-room reconstruction — XP / stat award producer (TnKillAwardManager + TnXpEventType + ReportGameStat sources).
// Reads the authoritative event record (Match::gameplayEvents) by serial and produces the awards the original raised:
//  - XpAward: one per XP event (Kill, First Blood, Double Kill, ..., GameWin), grouped by transactionId (one popup group per
//    kill, as TnHudDataObserverXpTransactions), with the localized Announcement / Description and "Killstreak,<id>" extra data;
//    xp = GlobalXpMultiplier x (DeathmatchXpAmount when >= 0 and not a team game, else XpAmount) [CONF RE].
//  - StatAward: StatPropertyId increments from the kill-award rules' StatIds and the PRI stat sources [CONF RE data].
// CanGainXp (= !IsPrivateGame in the original) is NOT applied here: Frontend's profile applies the rule (PC / OFFLINE ADAPTATION
// default: offline matches earn; WFC_ORIGINAL_XP_RULE restores the original). Awards are produced for every participant
// (player field); bot-scored awards are a PC ADAPTATION of TrackKills.CheckKills (TnPlayerController killer and victim only),
// switched by MatchSettings::botVictimsScore.
#pragma once
#include "game/GameplayEvents.h"
#include <map>
#include <string>
#include <vector>

namespace game {

class Match;

struct XpAward { int player = -1; int transactionId = 0; long xp = 0; std::string announcement, description, extra; std::string eventId; long baseXp = 0; };   // baseXp: before the bot-match scale

// XP earned in matches with bots (USER DECISION 2026-10-06, PC ADAPTATION: the original gives none for anything involving AI): scaled by
// the bots' difficulty, rounded per award. Challenge progress counts in full (user decision). One table; WFC_ORIGINAL_XP_RULE=1
// (Frontend profile) = the original.
struct BotXpPolicy {
    static constexpr float kScale[4] = {0.25f, 0.50f, 0.75f, 0.85f};   // EASY, MEDIUM, HARD, EXPERT (user decision 2026-10-09)
    static float scale(int difficulty) { return kScale[difficulty < 0 ? 0 : (difficulty > 3 ? 3 : difficulty)]; } };
struct StatAward { int player = -1; int statId = 0; long amount = 0; int updateType = 0; };   // ReportGameStat: 0 add, 1 match max, 2 match add

class AwardProducer {
public:
    void reset();                                       // a new match (Match::begin)
    void setXpScale(float s) { xpScale_ = s; }          // bot matches: BotXpPolicy::scale(difficulty); 1 without bots
    void consume(const Match& m);                       // new event records since the last call
    std::vector<XpAward> drainXp() { std::vector<XpAward> o; o.swap(xp_); return o; }
    std::vector<StatAward> drainStats() { std::vector<StatAward> o; o.swap(stats_); return o; }
    const std::vector<XpAward>& pendingXp() const { return xp_; }
    // Diagnostics: totals per player this match.
    long xpTotal(int player) const { auto it = totals_.find(player); return it == totals_.end() ? 0 : it->second; }
    static int xpAmount(const std::string& eventId, bool teamGame);
    static int challengeStatId(const char* challenge);

private:
    unsigned lastSerial_ = 0;
    float xpScale_ = 1.0f;
    int nextTxn_ = 1;
    bool firstKill_ = false, firstDeath_ = false;
    std::map<int, std::vector<float>> recentKills_;     // MultiKillDetector: kill times per killer (<= 3.0 s apart)
    std::map<std::pair<int, int>, int> dominate_;       // (killer, victim) consecutive kills
    std::map<int, int> lastKiller_;                      // victim -> who last killed them
    std::map<int, long> totals_;
    std::vector<XpAward> xp_;
    std::vector<StatAward> stats_;
    void xp(int player, int txn, const std::string& id, bool teamGame, const std::string& extra = std::string());
    void stat(int player, int statId, long amount = 1, int updateType = 0);
    void onKill(const Match& m, const GameplayEvent& e);
};

} // namespace game
