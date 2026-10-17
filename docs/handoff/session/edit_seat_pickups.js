// NET.identity follow-up 6 (Systems' tick-1723 split: a seat took a health pickup only on the peer where it was the local pawn): the
// placed pickup factories (TnHealth / TnOverShield / TnAmmoCrate) tested only world.player().pawn(). Online every human seat is tested,
// in match player order, with its own touch state (overlap begin, the ValidTouch trace re-check); the first valid toucher takes it.
// Offline: the local pawn only, the same flow as before. [PARTIAL: bots never take placed pickups, offline or online.]
// Run after edit_net_simpose.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/PickupFactory.h', [
    ['    float traceRecheck_ = 0.0f;   // ValidTouch FastTrace failure: retry after 0.5 s while still touching\n',
     '    // Per touching pawn (match player index): Touch = overlap begin; ValidTouch FastTrace failure: retry after 0.5 s while still touching.\n' +
     '    struct Touch { bool overlapping = false, now = false, began = false; float recheck = 0.0f; };\n' +
     '    std::map<int, Touch> touch_;\n'],
    ['    bool overlapping_ = false;      // the player pawn\'s cylinder overlapped last step (Touch = overlap begin)\n', ''],
]);
let h = fs.readFileSync('src/game/PickupFactory.h', 'utf8');
if (!h.includes('#include <map>')) { h = h.replace('#pragma once', '#pragma once\n#include <map>\n#include <utility>\n#include <vector>'); fs.writeFileSync('src/game/PickupFactory.h', h); }
ed('src/game/PickupFactory.cpp', [[
    'void PickupFactory::tick(World& world, float dt) {\n' +
    '    Character& c = world.player().pawn();\n' +
    '    bool overlapNow = overlaps(c);\n' +
    '    bool began = overlapNow && !overlapping_;\n' +
    '    overlapping_ = overlapNow;\n' +
    '    if (!available_) {\n',
    'void PickupFactory::tick(World& world, float dt) {\n' +
    '    // The pawns that can take it (offline the local pawn; online every human seat by match player index).\n' +
    '    static std::vector<std::pair<int, Character*>> pawns;\n' +
    '    world.pickupPawns(pawns);\n' +
    '    for (auto& pr : pawns) { Touch& t = touch_[pr.first]; t.now = overlaps(*pr.second); t.began = t.now && !t.overlapping; t.overlapping = t.now; }\n' +
    '    if (!available_) {\n'
]]);
ed('src/game/PickupFactory.cpp', [
    ['        if (kind_ == Kind::Health && overlapNow && tryGive(c)) take(world, c);   // CheckTouching\n        return;\n    }\n',
     '        if (kind_ == Kind::Health)   // CheckTouching\n' +
     '            for (auto& pr : pawns) if (touch_[pr.first].now && tryGive(*pr.second)) { take(world, *pr.second); break; }\n' +
     '        return;\n    }\n'],
    ['    if (began) traceRecheck_ = 0.0f;\n' +
     '    if (!overlapNow) return;\n' +
     '    if (traceRecheck_ > 0.0f) { traceRecheck_ -= dt; if (traceRecheck_ > 0.0f) return; }\n' +
     '    else if (!began) return;\n' +
     '    const CollisionWorld* lw = world.weaponCollision();\n' +
     '    float th;\n' +
     '    if (lw && lw->segmentHit(c.actorLocation(), pos_ + core::Vec3{0, 1.0f, 0}, th)) { traceRecheck_ = 0.5f; return; }\n' +
     '    traceRecheck_ = 0.0f;\n' +
     '    if (tryGive(c)) take(world, c);\n',
     '    const CollisionWorld* lw = world.weaponCollision();\n' +
     '    for (auto& pr : pawns) {\n' +
     '        Touch& t = touch_[pr.first]; Character& c = *pr.second;\n' +
     '        if (t.began) t.recheck = 0.0f;\n' +
     '        if (!t.now) continue;\n' +
     '        if (t.recheck > 0.0f) { t.recheck -= dt; if (t.recheck > 0.0f) continue; }\n' +
     '        else if (!t.began) continue;\n' +
     '        float th;\n' +
     '        if (lw && lw->segmentHit(c.actorLocation(), pos_ + core::Vec3{0, 1.0f, 0}, th)) { t.recheck = 0.5f; continue; }\n' +
     '        t.recheck = 0.0f;\n' +
     '        if (tryGive(c)) { take(world, c); break; }   // taken: Sleeping\n' +
     '    }\n'],
]);
ed('src/game/World.h', [[
    '    bool guidedMissileAlive() const { return missile_.alive; }\n',
    '    // The pawns the placed pickup factories test: offline the local pawn (as before), online every human seat by match player index.\n' +
    '    void pickupPawns(std::vector<std::pair<int, Character*>>& out) {\n' +
    '        out.clear();\n' +
    '        if (!onlineSeats_) { out.push_back({localPlayer_, &player_.pawn()}); return; }\n' +
    '        for (size_t q = 0; q < match_.players().size(); ++q) if (seatHuman((int)q)) if (Character* c = participantPawnMutable((int)q)) out.push_back({(int)q, c});\n' +
    '    }\n' +
    '    bool guidedMissileAlive() const { return missile_.alive; }\n'
]]);
console.log('ok');
