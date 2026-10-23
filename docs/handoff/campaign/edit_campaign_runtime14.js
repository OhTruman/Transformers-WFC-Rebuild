// Campaign runtime step 14 (anchor edits on the head): player interacts (WorldCampaignInteract.cpp, RE CAMPAIGN_CONTENT_DELTAS 2.2a),
// Omega Supreme's boss AI (WorldCampaignOmega.cpp), the destructible trigger rules + SeqAct_TnToggleDestructible + factory Kill All
// (H5; WorldCampaignDestructibles.cpp), the battery factory's Spawned variable (WorldCampaignRuntime.cpp). Sources copied from argv[2].
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
for (const f of ['WorldCampaignRuntime.cpp', 'WorldCampaignDestructibles.cpp', 'WorldCampaignInteract.cpp', 'WorldCampaignOmega.cpp'])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), path.join('src/game', f));
ed('src/game/Campaign.h', [
    ['                   bool blocks = false; int colSet = -1; };   // a static-mesh state blocks; its collision set (built once)',
     '                   bool blocks = false; int colSet = -1;     // a static-mesh state blocks; its collision set (built once)\n' +
     '                   float damageDeps = 0.0f; };              // HmDamageDependentsDestructionEffectFactory DamageAmount (0 = none)'],
    ['    std::vector<int> events;   // its Kismet notification events (op indices)\n};',
     '    std::vector<int> events;   // its Kismet notification events (op indices)\n' +
     '    bool gone = false;         // removed (its factory\'s Kill All)\n' +
     '    bool off = false;          // SeqAct_TnToggleDestructible Turn Off: not damageable\n};'],
    ['    std::vector<std::string> outputs;\n',
     '    std::vector<std::string> outputs;\n    std::vector<int> spawnedDes;   // a destructible factory\'s spawned actors (Kill All)\n'],
    ['struct CampaignState {',
     '// A player interact (TnInteract*) and its TnSeqEvent_Interact events.\n' +
     'struct CampaignInteract {\n' +
     '    std::string name, cls;\n' +
     '    core::Vec3 pos{0, 0, 0};\n' +
     '    float touch = 10.0f, useRange = 10.0f, hold = 2.0f, t = 0.0f;   // TouchDistance, the use range, the Begin Use sequence length\n' +
     '    bool singleUse = false, enabled = true;\n' +
     '    int user = -1, toucher = -1;\n' +
     '    std::vector<int> events;\n' +
     '};\n' +
     '// Omega Supreme (D5): the boss state machine and his arms (WorldCampaignOmega.cpp).\n' +
     'struct CampaignOmega {\n' +
     '    enum Stage { Intro, Healthy, Wounded, Healing, Corrupted, Critical, Dying, Dead };\n' +
     '    int p = -1, stage = Intro, pendingStage = 0;\n' +
     '    core::Vec3 anchor{0, 0, 0}; float yaw = 0.0f, timer = 0.0f;\n' +
     '    float sentryHp[4] = {0, 0, 0, 0}; int sentryKills = 0, lastSentryHitter = -1; float heartHp = 0.0f;\n' +
     '    int battery = -1, healPhase = 0, heals = 0; float healT = 0.0f; bool healCorrupt = false; std::vector<int> corrupt;\n' +
     '    int target = -1, robin = 0; float targetT = 0.0f;\n' +
     '    int grabVictim = -1, grabPhase = 0; float grabTimer = 0.0f, grabT = 0.0f;\n' +
     '    int frenzyLeft = 0; float frenzyNext = 0.0f, frenzyT = 0.0f, barrageT = 0.0f, aoeT = 0.0f, rocketT = 0.0f;\n' +
     '    uint32_t rng = 0x0E6A5u;\n' +
     '    float rand01() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) * (1.0f / 16777216.0f); }\n' +
     '};\n\n' +
     'struct CampaignState {'],
    ['    bool cinematicMode = false;',
     '    std::vector<CampaignInteract> interacts;   // step 14\n' +
     '    std::string interactPrompt;                // the local player\'s interact in reach (its class; empty = none)\n' +
     '    std::vector<uint8_t> interactHeldPrev;\n' +
     '    CampaignOmega omega;\n' +
     '    bool cinematicMode = false;'],
]);
ed('src/game/World.h', [
    ['    void cpTickDestructibles(float dt);\n',
     '    void cpTickDestructibles(float dt);\n' +
     '    bool cpToggleDestructible(KismetVM& vm, int op, int input);\n' +
     '    void cpRemoveDestructible(int i);\n' +
     '    void cpLoadInteracts();                      // WorldCampaignInteract.cpp\n' +
     '    void cpInteractFire(CampaignInteract& I, const char* out, int user);\n' +
     '    bool cpInteractActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpTickInteracts(float dt);\n' +
     '    bool cpInteractUserInvincible(int player) const;\n' +
     '    void cpOmegaStatus(int k);                   // WorldCampaignOmega.cpp\n' +
     '    float cpOmegaClip(const char* clip, float fallback) const;\n' +
     '    void cpOmegaPlay(const char* clip);\n' +
     '    core::Vec3 cpOmegaSentryPos(int k) const;\n' +
     '    void cpOmegaEnterHealthy();\n' +
     '    void cpOmegaBind();\n' +
     '    bool cpOmegaActivate(KismetVM& vm, int op, int input);\n' +
     '    bool cpOmegaDamage(int victim, int instigator, float& amount, Health& h);\n' +
     '    bool cpOmegaSees(int target) const;\n' +
     '    void cpOmegaFire(int target, float dmg, float radiusM, const char* type, float spread);\n' +
     '    void cpTickOmega(float dt);\n'],
    ['    bool cpCinematic = false;   // SeqAct_ToggleCinematicMode on: the HUD hides\n',
     '    bool cpCinematic = false;   // SeqAct_ToggleCinematicMode on: the HUD hides\n' +
     '    std::string cpInteract;     // the local player\'s interact prompt: the TnInteract class in reach (empty = none)\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadPawnGroups();\n', '        cpLoadPawnGroups();\n        cpLoadInteracts();\n'],
    ['    cpTickDestructibles(dt);\n', '    cpTickDestructibles(dt);\n    cpTickInteracts(dt);\n    if (!cp_.gameOver) cpTickOmega(dt);\n'],
    ['    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);\n',
     '    if (cpInteractActivate(vm, op, input)) return true;   // SeqAct_Toggle on interacts\n' +
     '    if (cpToggleDestructible(vm, op, input)) return true;\n' +
     '    if (cpOmegaActivate(vm, op, input)) return true;      // SetOmegaStage / CorruptOmegaBattery\n' +
     '    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);\n'],
]);
ed('src/game/World.cpp', [
    ['    if (!h) return false;\n',
     '    if (!h) return false;\n' +
     '    if (cp_.active) {   // campaign: an interact\'s user is invincible; Omega\'s states route / clamp the damage\n' +
     '        if (cpInteractUserInvincible(victim)) return false;\n' +
     '        if (cpOmegaDamage(victim, instigator, amount, *h)) return false;\n' +
     '    }\n'],
    ['    h.cpCinematic = cp_.active && cp_.cinematicMode;\n',
     '    h.cpCinematic = cp_.active && cp_.cinematicMode;\n    h.cpInteract = cp_.active ? cp_.interactPrompt : std::string();\n'],
]);
console.log('ok');
