// Escalation M11 (on the head + M10): the loot pickups (1 m touch, seek 15 m at 25 m/s, LifeSpan 10 s; WorldSurvival.cpp copied from
// argv[2]) and the ability resource replacing the cooldowns for the players [C, RE CAMPAIGN_OMEGA_HITREACT_RE 3.6: skill data index 1 -
// an ability needs Resource >= ResourceRequired 100, triggering sets it to 0 and starts no cooldown; the spam timer still applies; cap
// 100; the HUD shows it normalized]. PROV: Escalation only (the campaign keeps the cooldowns until its pickups drop); the horde and
// team bots keep their cooldowns.
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
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/WorldSurvival.cpp'), 'src/game/WorldSurvival.cpp');
ed('src/game/World.cpp', [
    ['    if (!a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) return false;\n    const int p = s.player;\n',
     '    const int p = s.player;\n' +
     '    // Escalation: a player\'s abilities run on the ability resource (>= 100 to trigger; no cooldown) instead of the cooldowns\n' +
     '    const bool resMode = sv_.phase != SurvivalState::Phase::Off && p >= 0 && (size_t)p < match_.players().size() &&\n' +
     '                         match_.players()[(size_t)p].kind != ParticipantKind::Bot;\n' +
     '    if (resMode) { if (!a.implemented || a.spam > 0.0f || (size_t)p >= sv_.abilityResource.size() || sv_.abilityResource[(size_t)p] < 100.0f) return false; }\n' +
     '    else if (!a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) return false;\n'],
    ['    a.spam = 1.0f; a.pendingCooldown = true;\n    if (participantAbilityHook) presentHook([this, p, id = std::string(a.id), ch = pc.chassis().id, at = pc.actorLocation()] { participantAbilityHook(p, id, ch, at); });\n    return true;\n',
     '    a.spam = 1.0f;\n' +
     '    if (resMode) sv_.abilityResource[(size_t)p] = 0.0f; else a.pendingCooldown = true;\n' +
     '    if (participantAbilityHook) presentHook([this, p, id = std::string(a.id), ch = pc.chassis().id, at = pc.actorLocation()] { participantAbilityHook(p, id, ch, at); });\n    return true;\n'],
    ['            h.svPower = (int)sv_.power[(size_t)localPlayer_];\n',
     '            h.svPower = (int)sv_.power[(size_t)localPlayer_];\n' +
     '            if ((size_t)localPlayer_ < sv_.abilityResource.size()) h.svAbilityResource = sv_.abilityResource[(size_t)localPlayer_] / 100.0f;\n'],
]);
ed('src/game/World.h', [
    ['    int svPower = 0; std::string svGeneratorName; int svGeneratorNeeded = -1;\n',
     '    int svPower = 0; std::string svGeneratorName; int svGeneratorNeeded = -1;\n' +
     '    float svAbilityResource = -1.0f;   // Escalation: the local player\'s ability resource, normalized (1 = an ability ready; -1 outside it)\n'],
]);
console.log('ok');
