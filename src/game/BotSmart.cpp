#include "game/BotSmart.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

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

const SmartTune& smartTune() {
    static const SmartTune t = [] {
        SmartTune v;
        const char* e = std::getenv("WFC_SMARTTUNE");
        if (!e) return v;
        struct K { const char* k; float* p; } keys[] = {
            {"coverExposure", &v.coverExposure}, {"coverMaxExposure", &v.coverMaxExposure}, {"coverChance", &v.coverChance},
            {"coverChancePerDiff", &v.coverChancePerDiff}, {"retreatHp", &v.retreatHp}, {"huntConfidence", &v.huntConfidence},
            {"routeExposureW", &v.routeExposureW}, {"focusHurtW", &v.focusHurtW}, {"focusMatesW", &v.focusMatesW},
            {"squadSize", &v.squadSize}, {"squadFollow", &v.squadFollow}, {"squadHelp", &v.squadHelp}, {"squadRegroupM", &v.squadRegroupM}, {"squadWaitM", &v.squadWaitM}, {"outnumberMargin", &v.outnumberMargin}, {"outnumberHp", &v.outnumberHp}, {"preAim", &v.preAim}, {"flank", &v.flank}, {"vehicle", &v.vehicle}, {"segBreak", &v.segBreak}, {"pickupSegments", &v.pickupSegments}};
        std::string s = e;
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find(',', i); if (j == std::string::npos) j = s.size();
            const std::string kv = s.substr(i, j - i); const size_t eq = kv.find('=');
            if (eq != std::string::npos) for (K& k : keys) if (kv.compare(0, eq, k.k) == 0 && std::strlen(k.k) == eq) *k.p = (float)std::atof(kv.c_str() + eq + 1);
            i = j + 1;
        }
        return v;
    }();
    return t;
}

int smartSkillCount() { return (int)(sizeof kSkills / sizeof kSkills[0]); }
const SmartSkill& smartSkill(int d) { return kSkills[d < 0 ? 0 : (d >= smartSkillCount() ? smartSkillCount() - 1 : d)]; }

}  // namespace game
