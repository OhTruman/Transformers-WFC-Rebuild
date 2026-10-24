// Campaign SeqVar_ObjectList (the factories' spawned groups; SeqAct_ModifyObjectList) and tactical volumes
// (SeqAct_TnAssignTacticalVolume: assigned AI keep their goals inside the volume). WorldCampaignRuntime.cpp copied from argv[2].
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
    ['        std::string chassisPath;   // TnSeqVar_Character ChassisTypePathName (a ROBODEF)\n',
     '        std::string chassisPath;   // TnSeqVar_Character ChassisTypePathName (a ROBODEF)\n' +
     '        std::vector<std::string> list;   // SeqVar_ObjectList entries ("pawn:<p>", actor paths)\n'],
]);
ed('src/game/Campaign.h', [
    ['    std::map<std::string, float> actorYaw;     // actors.json rotation_deg yaw (UE degrees): teleport destinations\n',
     '    std::map<std::string, float> actorYaw;     // actors.json rotation_deg yaw (UE degrees): teleport destinations\n' +
     '    struct TacVolume { std::string name; core::Vec3 lo{0, 0, 0}, hi{0, 0, 0}; std::vector<std::pair<core::Vec3, float>> planes;\n' +
     '                       bool cellsBuilt = false; std::vector<core::Vec3> inside; };   // a TnTacticalVolume brush + its nav spots\n' +
     '    std::map<std::string, TacVolume> tacVolumes;\n' +
     '    std::map<int, std::string> tactical;       // AI player -> its assigned tactical volume\n'],
]);
ed('src/game/World.h', [
    ['    bool cpTeleportActivate(KismetVM& vm, int op, int input);\n',
     '    bool cpTeleportActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpWriteSpawned(KismetVM& vm, int op, const std::string& obj);\n' +
     '    void cpObjectListsDrop(const std::string& obj);\n' +
     '    bool cpModifyObjectList(KismetVM& vm, int op, int input);\n' +
     '    void cpLoadTacticalVolumes();\n' +
     '    bool cpInTactical(const CampaignState::TacVolume& T, const core::Vec3& p) const;\n' +
     '    bool cpTacticalActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpTacticalConstrain(BotBrain& b, BotGoal& g);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadMineDefs();\n', '        cpLoadMineDefs();\n        cpLoadTacticalVolumes();\n'],
    ['    if (cpTeleportActivate(vm, op, input)) return true;     // SeqAct_Teleport\n',
     '    if (cpTeleportActivate(vm, op, input)) return true;     // SeqAct_Teleport\n' +
     '    if (cpModifyObjectList(vm, op, input)) return true;     // SeqAct_ModifyObjectList\n' +
     '    if (cpTacticalActivate(vm, op, input)) return true;     // SeqAct_TnAssignTacticalVolume\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['    if (!b.hasGoal || hdist(ng.pos, b.goal.pos) > 4.0f || ng.kind != b.goal.kind) { b.wantRepath = true; b.goalTime = now; }\n',
     '    if (cp_.active) cpTacticalConstrain(b, ng);   // campaign: an AI assigned to a tactical volume keeps its spot inside it\n' +
     '    if (!b.hasGoal || hdist(ng.pos, b.goal.pos) > 4.0f || ng.kind != b.goal.kind) { b.wantRepath = true; b.goalTime = now; }\n'],
]);
console.log('ok');
