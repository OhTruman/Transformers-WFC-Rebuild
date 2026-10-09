// Clean-room reconstruction — offline multiplayer bot participants: launch settings and identities.
// The shipped versus game had NO bots (RE: no MP bot controller; bBot PRIs hidden from the scoreboard; MaxPlayers 10). Everything
// here is a PC ADAPTATION for offline Private Match: bots are ordinary Match participants (ParticipantKind::Bot) that select a
// character, spawn, score and die through the same rules as players. Names and displayed levels are generated presentation.
#pragma once
#include "game/CharacterRoster.h"
#include <string>
#include <vector>

namespace game {

// Private Match Bot Settings (Frontend contract, StartLevel URL). Team modes: ?BotsAutobot=<n>?BotsDecepticon=<n> (per faction; win over
// the older ?BotsFriendly / ?BotsEnemy relative to the human's team). Non-team modes: ?BotsEnemy = total AI opponents.
// ?BotDifficulty=<0|1|2>, ?ExtendedPlayers=<0|1> (alias ?BotsExtended).
struct BotLaunch {
    int friendly = 0, enemy = 0;
    int autobot = -1, decepticon = -1;   // per-faction counts (-1 = not given); converted to friendly / enemy by the human's team
    int difficultyAutobot = -1, difficultyDecepticon = -1;   // optional per-faction difficulty (tests / custom games; -1 = difficulty)
    int difficulty = 1;            // 0 EASY, 1 MEDIUM, 2 HARD (labels borrowed from the campaign; PC ADAPTATION), 3 EXPERT (PC EXTENSION)
    bool extended = false;         // ?ExtendedPlayers=1: CUSTOM-GAME EXTENSION slots (16 bots per team), see MatchSettings
    int ai = -1;                   // ?BotAI=Smart|Classic (lobby "Bot AI"; -1 = the default, Classic until Smart passes its gates)
    int aiAutobot = -1, aiDecepticon = -1;   // ?BotAIAutobot / ?BotAIDecepticon: per faction (Smart-vs-Classic duels)
};

struct BotIdentity {
    std::string name;
    int team = 255;
    CharacterSelection selection;  // custom class preset: specialty, default faction bodies, legal preset weapons
    int level = 1;                 // displayed level (4 specialties x 0-25 summed; presentation only)
};

// Deterministic identities for a match: names unique among `taken`, classes spread per team so each team covers
// Scout / Scientist / Leader / Soldier before repeating. `seed` varies the order between matches.
std::vector<BotIdentity> makeBotIdentities(const BotLaunch& b, bool teamGame, int humanTeam, int maxPerTeam, int maxBotsPerTeam, int maxPlayers,
                                           int humans, const std::vector<std::string>& taken, unsigned seed);

const char* botDifficultyName(int d);

} // namespace game
