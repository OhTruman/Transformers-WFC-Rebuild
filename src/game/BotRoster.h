// Clean-room reconstruction — offline multiplayer bot participants: launch settings and identities.
// The shipped versus game had NO bots (RE: no MP bot controller; bBot PRIs hidden from the scoreboard; MaxPlayers 10). Everything
// here is a PC ADAPTATION for offline Private Match: bots are ordinary Match participants (ParticipantKind::Bot) that select a
// character, spawn, score and die through the same rules as players. Names and displayed levels are generated presentation.
#pragma once
#include "game/CharacterRoster.h"
#include <string>
#include <vector>

namespace game {

// Private Match Bot Settings (Frontend contract, StartLevel URL ?BotsFriendly=<n>?BotsEnemy=<n>?BotDifficulty=<0|1|2>).
// Team modes: friendly = on the human's team, enemy = the other team. Non-team modes: enemy = total AI opponents.
struct BotLaunch {
    int friendly = 0, enemy = 0;
    int difficulty = 1;            // 0 EASY, 1 MEDIUM, 2 HARD (labels borrowed from the campaign; PC ADAPTATION)
};

struct BotIdentity {
    std::string name;
    int team = 255;
    CharacterSelection selection;  // custom class preset: specialty, default faction bodies, legal preset weapons
    int level = 1;                 // displayed level (4 specialties x 0-25 summed; presentation only)
};

// Deterministic identities for a match: names unique among `taken`, classes spread per team so each team covers
// Scout / Scientist / Leader / Soldier before repeating. `seed` varies the order between matches.
std::vector<BotIdentity> makeBotIdentities(const BotLaunch& b, bool teamGame, int humanTeam, int maxPerTeam, int maxPlayers,
                                           int humans, const std::vector<std::string>& taken, unsigned seed);

const char* botDifficultyName(int d);

} // namespace game
