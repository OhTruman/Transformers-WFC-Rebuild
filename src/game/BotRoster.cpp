// Clean-room reconstruction — offline multiplayer bot identities (PC ADAPTATION; see BotRoster.h).
#include "game/BotRoster.h"
#include "game/ChassisDef.h"
#include <algorithm>

namespace game {

namespace {
// Generated player handles (not character names: the chassis already carries the iconic name).
const char* const kBotNames[] = {
    "Axlegrease", "Boltcutter", "Coldfusion", "Dynamo", "Exhaust", "Flywheel", "Gearjammer", "Hardlight",
    "Ionstorm", "Jumpstart", "Kickback", "Lugnut", "Manifold", "Nitrous", "Overdrive", "Piston",
    "Quickshift", "Redline", "Sparkplug", "Torque", "Ultravolt", "Valvetrain", "Wingnut", "Xenon",
    "Yawrate", "Zerofault", "Afterburn", "Blacktop", "Crankshaft", "Downforce", "Endgame", "Fulcrum",
    "Gasket", "Halfshaft", "Idler", "Jackplate", "Keystone", "Lockring", "Magneto", "Nosecone",
    "Outrider", "Pinion", "Quench", "Ratchetjaw", "Skidplate", "Tailpipe", "Undertow", "Vortexer",
    "Ampere", "Bearing", "Camber", "Detent", "Enginehead", "Ferrous", "Gimbal", "Hotshoe",
    "Inductor", "Joule", "Kilowatt", "Lifter", "Mainspring", "Nacelle", "Overrun", "Pushrod",
};
constexpr int kNameCount = (int)(sizeof(kBotNames) / sizeof(kBotNames[0]));

unsigned mix(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
}

const char* botDifficultyName(int d) { return d <= 0 ? "EASY" : (d >= 2 ? "HARD" : "MEDIUM"); }

std::vector<BotIdentity> makeBotIdentities(const BotLaunch& b, bool teamGame, int humanTeam, int maxPerTeam, int maxBotsPerTeam, int maxPlayers,
                                           int humans, const std::vector<std::string>& taken, unsigned seed) {
    std::vector<BotIdentity> out;
    // Capacity: never exceed the per-team / total slots, whatever the URL asked for.
    int friendly = 0, enemy = 0;
    if (teamGame) {
        const int mine = humanTeam == 1 ? 1 : 0;
        friendly = std::clamp(b.friendly, 0, std::max(0, std::min(maxBotsPerTeam, maxPerTeam - humans)));
        enemy = std::clamp(b.enemy, 0, std::min(maxBotsPerTeam, maxPerTeam));
        friendly = std::min(friendly, std::max(0, maxPlayers - humans));
        enemy = std::min(enemy, std::max(0, maxPlayers - humans - friendly));
        (void)mine;
    } else {
        enemy = std::clamp(b.enemy, 0, std::max(0, maxPlayers - humans));
    }
    std::vector<std::string> used = taken;
    unsigned nameCursor = mix(seed) % (unsigned)kNameCount;
    auto nextName = [&]() {
        for (int k = 0; k < kNameCount; ++k) {
            const std::string n = kBotNames[(nameCursor + (unsigned)k) % (unsigned)kNameCount];
            if (std::find(used.begin(), used.end(), n) == used.end()) { nameCursor += (unsigned)k + 1; used.push_back(n); return n; }
        }
        const std::string n = "Bot" + std::to_string(used.size());
        used.push_back(n); return n;
    };
    // Class spread: a per-team shuffled order of the four specialties, repeated.
    auto classOrder = [&](int salt) {
        std::vector<int> o = {0, 1, 2, 3};
        unsigned h = mix(seed * 2654435761U + (unsigned)salt);
        for (int i = 3; i > 0; --i) { std::swap(o[(size_t)i], o[h % (unsigned)(i + 1)]); h = mix(h); }
        return o;
    };
    auto add = [&](int team, int idx, const std::vector<int>& order, int difficulty) {
        BotIdentity id;
        id.name = nextName();
        id.team = team;
        CharacterSelection& c = id.selection;
        c.type = 0;
        c.specialty = (Specialty)order[(size_t)(idx % 4)];
        c.chassisByFaction[0] = defaultChassis(c.specialty, 0);
        c.chassisByFaction[1] = defaultChassis(c.specialty, 1);
        // The class's legal MP preset: every list of the PCD (the chassis' iconic lists would otherwise fill the gaps).
        c.weapons = classPresetList(specialtyName(c.specialty), "weapons");
        c.vehicleWeapons = classPresetList(specialtyName(c.specialty), "vehicle_weapons");
        c.melee = classPresetList(specialtyName(c.specialty), "melee");
        c.abilities = classPresetList(specialtyName(c.specialty), "abilities");
        // Ability variety (PC ADAPTATION): some Soldiers carry the class-pool SpawnAmmoCrate (Soldier, LevelRestriction 2) in
        // place of one preset ability. Only abilities the bot AI performs are swapped in.
        // Every second Soldier (alternating by team, so a match always has some) swaps Hover for it; Whirlwind (melee) stays.
        if (c.specialty == Specialty::Soldier && c.abilities.size() == 2 && ((idx / 4 + team + (int)(seed & 1)) & 1))
            c.abilities[1] = "SpawnAmmoCrate";
        c.customSlot = specialtyName(c.specialty);
        // Displayed level: sum of four specialty levels (0-25 each) -> 1..100; harder bots read as more experienced.
        const int lo = difficulty <= 0 ? 4 : (difficulty == 1 ? 16 : 36), span = difficulty <= 0 ? 20 : (difficulty == 1 ? 34 : 50);
        id.level = lo + (int)(mix(seed ^ (unsigned)(team * 977 + idx * 131 + 7)) % (unsigned)span);
        out.push_back(id);
    };
    if (teamGame) {
        const int mine = humanTeam == 1 ? 1 : 0, other = 1 - mine;
        const std::vector<int> o0 = classOrder(mine + 1), o1 = classOrder(other + 1);
        // The human's own class is unknown here: friendlies simply follow the shuffled order.
        for (int i = 0; i < friendly; ++i) add(mine, i, o0, b.difficulty);
        for (int i = 0; i < enemy; ++i) add(other, i, o1, b.difficulty);
    } else {
        const std::vector<int> o = classOrder(9);
        for (int i = 0; i < enemy; ++i) add(255, i, o, b.difficulty);
    }
    return out;
}

} // namespace game
