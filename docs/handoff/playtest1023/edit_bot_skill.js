// Playtest 2026-10-23 #12 (bots too accurate; TTK itself matches the original): the per-difficulty bot skill becomes data,
// data/frontend/bot_skill.json (retunable without code), with the aim error retuned on an AIDUEL sweep (4bcfde3d, Smart + Classic
// mean hitscan hit % at 25-50 m, seeds 4-6): EASY 7 -> 21 deg (16.5 %), MEDIUM 4 -> 11 (29 %), HARD 2 -> 5.5 (50 %), EXPERT 1.2
// unchanged (74 %). Difficulty is a PC ADAPTATION (the original MP had no bot difficulty). The compiled table keeps the same values
// for a missing / broken file. Also: WFC_AIDUEL joins the direct-boot list (headless with WFC_NULLRENDER). Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
const rows = [
    ['EASY', 0.80, 220, 21.0, 110, 50, 2, 0.45, 0.6, 1.5],
    ['MEDIUM', 0.45, 380, 11.0, 130, 60, 4, 0.75, 0.85, 1.15],
    ['HARD', 0.22, 620, 5.5, 160, 70, 6, 1.0, 1.0, 1.0],
    ['EXPERT', 0.15, 800, 1.2, 175, 80, 8, 1.0, 1.0, 0.85],
];
const keys = ['reaction', 'turnRateDeg', 'aimErrorDeg', 'fovDeg', 'sightM', 'memory', 'strafe', 'burstScale', 'pauseScale'];
let j = '{\n  "_note": "Bot skill per difficulty (PC ADAPTATION: the original MP had no bot difficulty). Read once at start; a missing or broken file falls back to the same compiled values (src/game/BotBrain.cpp). aimErrorDeg tuned 2026-10-23 on AIDUEL to about 15 / 30 / 45 % hitscan hits at 25-50 m (Easy / Medium / Hard).",\n  "difficulties": [\n';
j += rows.map(r => '    {"name": "' + r[0] + '", ' + keys.map((k, i) => '"' + k + '": ' + r[i + 1]).join(', ') + '}').join(',\n');
j += '\n  ]\n}\n';
fs.writeFileSync('data/frontend/bot_skill.json', j);
const lit = (r) => '{' + r.slice(1).map(v => { const s = String(v); return (s.includes('.') ? s : s + '.0') + 'f'; }).join(', ') + '}';
ed('src/game/BotBrain.cpp', [
    ['#include "game/BotBrain.h"\n',
     '#include "game/BotBrain.h"\n#include "assets/Json.h"\n#include "core/Log.h"\n#include "frontend/DataPaths.h"\n#include <fstream>\n#include <sstream>\n'],
    ['    static const BotSkill s[4] = {{0.80f, 220.0f, 7.0f, 110.0f, 50.0f, 2.0f, 0.45f, 0.6f, 1.5f},    // EASY\n' +
     '                                  {0.45f, 380.0f, 4.0f, 130.0f, 60.0f, 4.0f, 0.75f, 0.85f, 1.15f},  // MEDIUM\n' +
     '                                  {0.22f, 620.0f, 2.0f, 160.0f, 70.0f, 6.0f, 1.0f, 1.0f, 1.0f},     // HARD\n' +
     '                                  {0.15f, 800.0f, 1.2f, 175.0f, 80.0f, 8.0f, 1.0f, 1.0f, 0.85f}};   // EXPERT (PC EXTENSION)\n' +
     '    return s[std::clamp(difficulty, 0, kMaxBotDifficulty)];\n',
     '    // Playtest 2026-10-23 #12: aim error x3 / x2.75 / x2.75 / x1 (AIDUEL, about 15 / 30 / 45 % hits at 25-50 m). The live values come\n' +
     '    // from data/frontend/bot_skill.json (read once; per-field fallback to this table).\n' +
     '    static const BotSkill s[4] = {' + lit(rows[0]) + ',    // EASY\n' +
     '                                  ' + lit(rows[1]) + ',  // MEDIUM\n' +
     '                                  ' + lit(rows[2]) + ',     // HARD\n' +
     '                                  ' + lit(rows[3]) + '};   // EXPERT (PC EXTENSION)\n' +
     '    static const BotSkill* table = [] {   // thread-safe once (the parallel steering pass calls this)\n' +
     '        static BotSkill t[4]; for (int i = 0; i < 4; ++i) t[i] = s[i];\n' +
     '        std::ifstream f(frontend::frontendDataFile("bot_skill.json"), std::ios::binary);\n' +
     '        std::stringstream ss; ss << f.rdbuf(); assets::Json j;\n' +
     '        if (!f.good() || !assets::Json::parse(ss.str(), j) || !j["difficulties"].isArray()) { LOG_WARN("bot_skill.json missing or invalid: compiled bot skill"); return (const BotSkill*)t; }\n' +
     '        const assets::Json& d = j["difficulties"];\n' +
     '        for (size_t i = 0; i < 4 && i < d.size(); ++i) {\n' +
     '            BotSkill& b = t[i]; const assets::Json& r = d[i];\n' +
     '            b.reaction = r["reaction"].asFloat(b.reaction); b.turnRateDeg = r["turnRateDeg"].asFloat(b.turnRateDeg);\n' +
     '            b.aimErrorDeg = r["aimErrorDeg"].asFloat(b.aimErrorDeg); b.fovDeg = r["fovDeg"].asFloat(b.fovDeg);\n' +
     '            b.sightM = r["sightM"].asFloat(b.sightM); b.memory = r["memory"].asFloat(b.memory); b.strafe = r["strafe"].asFloat(b.strafe);\n' +
     '            b.burstScale = r["burstScale"].asFloat(b.burstScale); b.pauseScale = r["pauseScale"].asFloat(b.pauseScale);\n' +
     '        }\n' +
     '        LOG_INFO("bot skill: data/frontend/bot_skill.json (aim error %.1f / %.1f / %.1f / %.1f deg)", t[0].aimErrorDeg, t[1].aimErrorDeg, t[2].aimErrorDeg, t[3].aimErrorDeg);\n' +
     '        return (const BotSkill*)t;\n' +
     '    }();\n' +
     '    return table[std::clamp(difficulty, 0, kMaxBotDifficulty)];\n'],
]);
ed('src/core/Application_Frontend.cpp', [
    ['    for (const char* v : {"WFC_SMOKE_FRAMES", "WFC_SHOTLIST",',
     '    for (const char* v : {"WFC_SMOKE_FRAMES", "WFC_AIDUEL", "WFC_SHOTLIST",'],
]);
console.log('ok');
