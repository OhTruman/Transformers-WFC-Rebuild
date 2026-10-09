#include "game/BotSmart.h"

#include <cctype>
#include <cstring>

namespace game {

const char* botAiName(BotAi a) { return a == BotAi::Smart ? "Smart" : "Classic"; }

BotAi botAiFromString(const char* s, BotAi fallback) {
    if (!s || !*s) return fallback;
    char low[16] = {0};
    for (size_t i = 0; i + 1 < sizeof low && s[i]; ++i) low[i] = (char)std::tolower((unsigned char)s[i]);
    if (!std::strcmp(low, "smart") || !std::strcmp(low, "1")) return BotAi::Smart;
    if (!std::strcmp(low, "classic") || !std::strcmp(low, "0")) return BotAi::Classic;
    return fallback;
}

namespace {
// Easy / Normal / Hard. Hearing ranges are the same for everyone (what a player could hear); skill shows in memory, call-out delay
// and how much a hit reveals. A new tier (e.g. Expert) is one more row here and in the later stages' tables.
const SmartSkill kSkills[] = {
    {"Easy",   3.0f, 1.5f, 70.0f, 90.0f, 10.0f, 30.0f, 0.2f},
    {"Normal", 6.0f, 0.9f, 70.0f, 90.0f, 15.0f, 35.0f, 0.5f},
    {"Hard",  10.0f, 0.4f, 70.0f, 90.0f, 15.0f, 40.0f, 0.8f},
    {"Expert", 14.0f, 0.25f, 70.0f, 90.0f, 15.0f, 40.0f, 0.95f},   // user decision 2026-10-09 (PC EXTENSION)
};
}  // namespace

int smartSkillCount() { return (int)(sizeof kSkills / sizeof kSkills[0]); }
const SmartSkill& smartSkill(int d) { return kSkills[d < 0 ? 0 : (d >= smartSkillCount() ? smartSkillCount() - 1 : d)]; }

}  // namespace game
