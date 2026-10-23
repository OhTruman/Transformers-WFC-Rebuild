// Campaign SeqAct_Teleport + TnSeqVar_Character + hazard volumes by streamed unit (Integration gate item 2026-10-23; RE: FindMatchingPawn = the pawn playing that
// ROBODEF, human or AI) and, headless only (WFC_NULLRENDER: no movie playback, the campaign's SeqAct_MoviePlayer never reported
// Stopped - D3B's intro chain waits for it), a movie request finishes at once. WorldCampaignRuntime.cpp copied from argv[2].
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignRuntime.cpp'), 'src/game/WorldCampaignRuntime.cpp');
ed('src/game/Kismet.h', [
    ['        int buddyIndex = 1;   // TnSeqVar_Buddy BuddyIndex (1-based, CDO default 1)\n',
     '        int buddyIndex = 1;   // TnSeqVar_Buddy BuddyIndex (1-based, CDO default 1)\n' +
     '        std::string chassisPath;   // TnSeqVar_Character ChassisTypePathName (a ROBODEF)\n'],
]);
ed('src/game/Kismet.cpp', [
    ['v.buddyIndex = a["BuddyIndex"].asInt(1);\n', 'v.buddyIndex = a["BuddyIndex"].asInt(1);\n                v.chassisPath = a["ChassisTypePathName"].asString();\n'],
]);
ed('src/game/Campaign.h', [
    ['    std::set<std::string> actorDestroyed;      // SeqAct_Destroy\'d world actors (they stay hidden)\n',
     '    std::set<std::string> actorDestroyed;      // SeqAct_Destroy\'d world actors (they stay hidden)\n' +
     '    std::map<std::string, float> actorYaw;     // actors.json rotation_deg yaw (UE degrees): teleport destinations\n'],
]);
ed('src/game/World.h', [
    ['    bool cpHideOpActivate(KismetVM& vm, int op, int input);\n',
     '    bool cpHideOpActivate(KismetVM& vm, int op, int input);\n' +
     '    std::vector<int> cpCharacterTargets(const std::string& robodef);\n' +
     '    bool cpTeleportActivate(KismetVM& vm, int op, int input);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    if (cpHideOpActivate(vm, op, input)) return true;       // SeqAct_ToggleHidden / SeqAct_Destroy on world actors\n',
     '    if (cpHideOpActivate(vm, op, input)) return true;       // SeqAct_ToggleHidden / SeqAct_Destroy on world actors\n' +
     '    if (cpTeleportActivate(vm, op, input)) return true;     // SeqAct_Teleport\n'],
    ['    LOG_INFO("campaign: movie %s (request %d)", m.c_str(), cp_.movieSerial);\n',
     '    LOG_INFO("campaign: movie %s (request %d)", m.c_str(), cp_.movieSerial);\n' +
     '    // headless (no movie playback): the movie ends at once, so Kismet waiting on its Stopped (D3B\'s intro switch) goes on\n' +
     '    if (std::getenv("WFC_NULLRENDER")) campaignMovieEnded(cp_.movieSerial);\n'],
]);
// Hazard volumes follow streaming: a campaign hazard (kill / pain volume) is active only while its level's unit is loaded (D3B's
// "08 - Ledge Driving" start stands inside Lift1B_DSN's KillZ PhysicsVolume_2395, a unit LedgeDrive does not load)
ed('src/game/World.h', [
    ['    struct HazardVolume { std::string actor, damageType; float damagePerSec = 0, painInterval = 1.0f; bool entryPain = true;\n',
     '    struct HazardVolume { std::string actor, damageType; float damagePerSec = 0, painInterval = 1.0f; bool entryPain = true;\n' +
     '                          std::string unit;   // campaign: its level\'s unit (object path prefix without _m); active only while loaded\n'],
]);
ed('src/game/World.cpp', [
    ['        h.actor = v["actor"].asString();\n',
     '        h.actor = v["actor"].asString();\n' +
     '        {   // campaign: the unit the volume belongs to\n' +
     '            const std::string o = v["object"].asString();\n' +
     '            const size_t k = o.find(".TheWorld");\n' +
     '            h.unit = k == std::string::npos ? std::string() : o.substr(0, k);\n' +
     '            if (h.unit.size() > 2 && h.unit.compare(h.unit.size() - 2, 2, "_m") == 0) h.unit.resize(h.unit.size() - 2);\n' +
     '        }\n'],
    ['        bool in = !hazards_[i].planes.empty();\n',
     '        const std::string& u = hazards_[i].unit;   // campaign: only while its unit is loaded (streaming)\n' +
     '        if (campaignMap_ && !u.empty() && u != cpChapter_.persistentDir && !cpUnitLoaded_.count(u)) continue;\n' +
     '        bool in = !hazards_[i].planes.empty();\n'],
]);
console.log('ok');
