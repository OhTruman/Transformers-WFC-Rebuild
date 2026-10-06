// Clean-room reconstruction - multiplayer progression (TnXpManager / challenge unlocks), the frontend's model.
// Each specialty (Scout, Scientist, Soldier, Leader) has its own XP and level 0-25 (Default__TnXpManager.LevelTable:
// level = thresholds <= XP; XP capped at 355000) [CONFIRMED, RE MP_PROGRESSION 2026-10-06 s4]. The displayed player level
// is the sum of the four. Challenges (TransChallenges.ini: ChallengeId, StatPropertyId, GoalValues, XpRewardValues,
// PrimeChallenge) unlock tier by tier when their stat reaches a goal; a tier's XP goes to the played specialty, a Prime
// challenge's to all four [CONFIRMED s6]. Prime: offered when all four are maxed; enables the Prime challenges.
// Award sources are Gameplay's (XP events, game stats); this module applies them to the local profile.
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

namespace frontend {

class Catalog;

struct ProgressionState {
    std::array<long, 4> xp{};             // Scout, Scientist, Soldier, Leader
    std::array<long, 4> lastMatchXp{};    // earned in the last (or current) match: the results screen
    bool prime = false;                   // UnlockPrimeMode taken
    std::map<int, int> tiers;             // ChallengeId -> tiers unlocked (0..3)
    std::map<int, long> stats;            // StatPropertyId -> lifetime value (challenge counters)
};

namespace progression {

constexpr long kXpCap = 355000;
int specialtyIndex(const std::string& name);   // "Scout" / "Scientist" / "Soldier" / "Leader" -> 0..3, else -1
const char* specialtyName(int i);
int levelForXp(long xp);                       // 0..25
long xpNeededForLevel(int level);              // XP to reach level (0 -> 0); -1 above 25
int playerLevel(const ProgressionState& s);    // sum of the four
bool levelMaxed(const ProgressionState& s);    // all four at the cap (Prime offered)

struct ChallengeUnlock {
    int challengeId = 0, tier = 0;             // tier: 1-based, the one just unlocked
    long goal = 0, xp = 0;
    bool prime = false;
    std::string name, description;             // description with `g = goal
};
struct LevelUp { int specialty = -1, level = 0; };
int challengeStat(const Catalog& cat, int challengeId);   // its StatPropertyId (0 if unknown)

// XP for the played specialty (capped). Returns the level-up, if any.
LevelUp addXp(ProgressionState& s, int specialty, long amount);
// A game stat (ReportGameStat): updateType 0 add (lifetime), 1 max within the match, 2 add within the match. Returns
// the tiers unlocked; their XP is applied (played specialty / all four for Prime), level-ups appended to levelUps.
std::vector<ChallengeUnlock> reportStat(ProgressionState& s, const Catalog& cat, int statId, long amount, int updateType,
                                        int specialty, std::vector<LevelUp>& levelUps);

} // namespace progression
} // namespace frontend
