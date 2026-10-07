// Clean-room reconstruction - multiplayer progression model (see Progression.h).
#include "frontend/Progression.h"

#include "frontend/Catalog.h"

#include <algorithm>
#include <cstdlib>

namespace frontend::progression {
namespace {
// Default__TnXpManager.LevelTable: XP to reach levels 1..25 [CONFIRMED authored].
constexpr long kLevelTable[] = {500, 1500, 3000, 5000, 7500, 11000, 15500, 21000, 27500, 35000, 44000, 54500, 66500,
                                80000, 95000, 112000, 131000, 152000, 175000, 200000, 227000, 256000, 287000, 320000, 355000};
constexpr int kLevels = (int)(sizeof kLevelTable / sizeof kLevelTable[0]);
const char* kNames[4] = {"Scout", "Scientist", "Soldier", "Leader"};

long num(const std::string& v) { return std::strtol(v.c_str(), nullptr, 0); }

struct Challenge {
    int id = 0, stat = 0;
    bool prime = false;
    std::vector<long> goals, xp;
    std::string name, description;
};

std::vector<Challenge> challenges(const Catalog& cat) {
    std::vector<Challenge> out;
    for (const Catalog::Provider& p : cat.providers("Challenge")) {
        Challenge c;
        for (const auto& [k, v] : p.fields) {
            if (k == "ChallengeId") c.id = (int)num(v);
            else if (k == "StatPropertyId") c.stat = (int)num(v);
            else if (k == "GoalValues") c.goals.push_back(num(v));
            else if (k == "XpRewardValues") c.xp.push_back(num(v));
            else if (k == "PrimeChallenge") c.prime = v == "True" || v == "true" || v == "1";
            else if (k == "ChallengeName") c.name = v;
            else if (k == "Description") c.description = v;
        }
        if (c.name.empty()) c.name = cat.localize("TransChallenges", p.name, "ChallengeName");
        if (c.description.empty()) c.description = cat.localize("TransChallenges", p.name, "Description");
        if (c.id && c.stat && !c.goals.empty()) out.push_back(std::move(c));
    }
    return out;
}

std::string withGoal(std::string d, long goal) {
    const std::string g = std::to_string(goal);
    for (size_t at; (at = d.find("`g")) != std::string::npos;) d.replace(at, 2, g);
    return d;
}
}

int challengeStat(const Catalog& cat, int challengeId) {
    static std::vector<Challenge> list;
    if (list.empty()) list = challenges(cat);
    for (const Challenge& c : list) if (c.id == challengeId) return c.stat;
    return 0;
}

int specialtyIndex(const std::string& name) {
    for (int i = 0; i < 4; ++i) if (name == kNames[i]) return i;
    return -1;
}
const char* specialtyName(int i) { return i >= 0 && i < 4 ? kNames[i] : ""; }

int levelForXp(long xp) {
    int l = 0;
    while (l < kLevels && xp >= kLevelTable[l]) ++l;
    return l;
}
long xpNeededForLevel(int level) {
    if (level <= 0) return 0;
    if (level > kLevels) return -1;
    return kLevelTable[level - 1];
}
int playerLevel(const ProgressionState& s) {
    int sum = 0;
    for (long x : s.xp) sum += levelForXp(x);
    return sum;
}
bool levelMaxed(const ProgressionState& s) {
    for (long x : s.xp) if (x < kXpCap) return false;
    return true;
}

LevelUp addXp(ProgressionState& s, int specialty, long amount) {
    LevelUp up;
    if (specialty < 0 || specialty > 3 || amount <= 0) return up;
    const int before = levelForXp(s.xp[(size_t)specialty]);
    const long add = std::min(amount, kXpCap - s.xp[(size_t)specialty]);
    if (add <= 0) return up;
    s.xp[(size_t)specialty] += add;
    s.lastMatchXp[(size_t)specialty] += add;
    const int after = levelForXp(s.xp[(size_t)specialty]);
    if (after > before) { up.specialty = specialty; up.level = after; up.from = before; }
    return up;
}

std::vector<ChallengeUnlock> reportStat(ProgressionState& s, const Catalog& cat, int statId, long amount, int updateType,
                                        int specialty, std::vector<LevelUp>& levelUps) {
    std::vector<ChallengeUnlock> out;
    long& v = s.stats[statId];
    if (updateType == 1) v = std::max(v, amount);
    else v += amount;
    static std::vector<Challenge> list;   // authored, read once
    if (list.empty()) list = challenges(cat);
    for (const Challenge& c : list) {
        if (c.stat != statId || (c.prime && !s.prime)) continue;
        int& tier = s.tiers[c.id];
        while (tier < (int)c.goals.size() && v >= c.goals[(size_t)tier]) {   // every tier whose goal <= value
            ChallengeUnlock u;
            u.challengeId = c.id; u.tier = tier + 1; u.goal = c.goals[(size_t)tier];
            u.xp = (size_t)tier < c.xp.size() ? c.xp[(size_t)tier] : 0;
            u.prime = c.prime; u.name = c.name; u.description = withGoal(c.description, u.goal);
            ++tier;
            if (u.prime) { for (int k = 0; k < 4; ++k) { LevelUp l = addXp(s, k, u.xp); if (l.specialty >= 0) levelUps.push_back(l); } }
            else { LevelUp l = addXp(s, specialty, u.xp); if (l.specialty >= 0) levelUps.push_back(l); }
            out.push_back(u);
        }
    }
    return out;
}

} // namespace frontend::progression
