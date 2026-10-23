// Campaign runtime step 4 (anchor edits; on RUNTIME3): co-op humans (every human on the chapter faction; picks for 1-3 humans, a
// duplicate takes the next free trio member; the squad = the trio minus every human, surplus members leave) and the AI ops
// (SeqAct_TnAiMoveToActor / SetMoveToObjective / UseInteract -> Kismet objectives that outrank following; destinations from
// navigation.json, actors.json when AssetTools delivers it). WorldCampaignRuntime.cpp copied from argv[2].
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
ed('src/game/Campaign.h', [
    ['#include <string>\n', '#include <map>\n#include <string>\n'],
    ['    std::string humanChassis;',
     '    struct Objective { core::Vec3 pos{0, 0, 0}; float radius = 5.0f; bool persistent = false; };\n' +
     '    std::map<int, Objective> objectives;      // step 4: Kismet AI objectives per match player (MoveToActor / SetMoveToObjective)\n' +
     '    std::map<std::string, core::Vec3> actorPos;   // actor name -> location (navigation.json, actors.json)\n' +
     '    std::string humanChassis;'],
]);
ed('src/game/World.h', [
    ['    void cpSyncPicks();\n',
     '    void cpSyncPicks();\n' +
     '    void cpLoadActors();\n' +
     '    bool cpActorPos(const std::string& path, core::Vec3& out) const;\n' +
     '    std::vector<int> cpTargets(KismetVM& vm, int op, const std::string& link);\n' +
     '    bool cpAiOpActivate(KismetVM& vm, int op, int input);\n' +
     '    bool cpObjectiveGoal(BotBrain& b, BotGoal& g);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadFactories();\n', '        cpLoadFactories();\n        cpLoadActors();\n'],
    ['    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);\n',
     '    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);\n' +
     '    if (cpAiOpActivate(vm, op, input)) return true;   // MoveToActor / SetMoveToObjective / UseInteract\n'],
    [' || c == "SeqAct_TnAiMoveToActor" ||', ' ||'],
]);
ed('src/game/WorldBots.cpp', [
    ['    if (cpSquadGoal(b, g)) return true;',
     '    if (cp_.active && cpObjectiveGoal(b, g)) return true;   // campaign: a Kismet AI objective outranks following\n    if (cpSquadGoal(b, g)) return true;'],
]);
ed('src/game/World.cpp', [
    ['    if (l.settings.survival) { SurvivalData d; if (SurvivalData::load(canonicalMapName(l.map), d)) svTeam = d.coopTeam; }\n',
     '    if (l.settings.survival) { SurvivalData d; if (SurvivalData::load(canonicalMapName(l.map), d)) svTeam = d.coopTeam; }\n' +
     '    if (l.settings.campaign && campaignMap_) svTeam = cpChapter_.faction;   // campaign co-op: every human on the chapter\'s faction\n'],
]);
ed('src/game/ChassisDef.cpp', [
    ['    const std::string dir = id.rfind("ESC_", 0) == 0 ? vsRoot + "/../../AssetTools/out/escalation_characters/" + id.substr(4) + "/"\n',
     '    // "CMP_<id>": a campaign AI body from AssetTools out/campaign_characters/<id> (the TR_AiCharacterDefinition_p templates).\n' +
     '    const std::string dir = id.rfind("CMP_", 0) == 0 ? vsRoot + "/../../AssetTools/out/campaign_characters/" + id.substr(4) + "/"\n' +
     '                          : id.rfind("ESC_", 0) == 0 ? vsRoot + "/../../AssetTools/out/escalation_characters/" + id.substr(4) + "/"\n'],
    ['    if (id.rfind("ESC_", 0) == 0 && d.vehicleGlb.empty()) d.flyerNoTransform = true;\n',
     '    if ((id.rfind("ESC_", 0) == 0 || id.rfind("CMP_", 0) == 0) && d.vehicleGlb.empty()) d.flyerNoTransform = true;\n'],
    ['    const bool esc = id.rfind("ESC_", 0) == 0;\n',
     '    const bool esc = id.rfind("ESC_", 0) == 0 || id.rfind("CMP_", 0) == 0;   // (campaign bodies: the same rule)\n'],
]);
ed('src/game/World.cpp', [
    ['        const std::string cj = chassis.rfind("ESC_", 0) == 0 ? ',
     '        const std::string cj = chassis.rfind("CMP_", 0) == 0 ? assetRoot() + "/../../AssetTools/out/campaign_characters/" + chassis.substr(4) + "/character.json"\n' +
     '                             : chassis.rfind("ESC_", 0) == 0 ? '],
]);
console.log('ok');
