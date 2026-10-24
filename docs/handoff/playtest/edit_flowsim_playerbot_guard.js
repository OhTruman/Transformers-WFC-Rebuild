// WFC_FLOWSIMTEST: the bot-driven human needs WFC_PLAYERBOT; without it the human stands still and the move / shoot / transform
// rows fail as if the game were broken (Integration's 7 / 10 on 7f35fb82). The harness now turns it on itself when unset (before
// the first playerBotDifficulty() read caches it) and states it. Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/core/Application_Frontend.cpp', [
    ['    const std::string mode = std::getenv("WFC_FLOWSIMTEST");' + NL,
     '    const std::string mode = std::getenv("WFC_FLOWSIMTEST");' + NL +
     '    // the human is driven by the player bot through its normal input: WFC_PLAYERBOT (default difficulty 1 when unset)' + NL +
     '    if (!std::getenv("WFC_PLAYERBOT")) { _putenv("WFC_PLAYERBOT=1"); LOG_INFO("FLOWSIMTEST: WFC_PLAYERBOT unset - set to 1 (the bot-driven human)"); }' + NL],
    ['    check(!L.online && L.remotes.empty() && L.humans.empty(), "the solo launch is offline (local loop, no wire seat)");' + NL,
     '    check(game::World::playerBotDifficulty() >= 0, "the player bot drives the human (WFC_PLAYERBOT)");' + NL +
     '    check(!L.online && L.remotes.empty() && L.humans.empty(), "the solo launch is offline (local loop, no wire seat)");' + NL],
]);
console.log('ok');
