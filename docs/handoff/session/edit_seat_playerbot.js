// NET.identity follow-up 2 (Systems' record / replay): online, the harness player bot (WFC_PLAYERBOT) is input-only, like a human. Its
// brain ran between steps on the live peer only and wrote sim state directly: abilities (Hover -> the 0.6 m/s rise at the first
// InProgress step), melee, reloads, weapon switches, grenades (the bag reserve), repair-ray heals, the aim pitch, the shared path-search
// queue / budget, the stranded suicide. Online (pbSandbox_), each becomes the matching button press (the seat path applies it on every
// peer) or is skipped; its path searches are private synchronous ones. Offline (no seats): unchanged.
// Run after edit_seat_launch.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', [[
    '    std::vector<char> seatFineAim_;',
    '    bool pbSandbox_ = false;            // online: the player bot\'s brain is running (input only: its actions become presses)\n' +
    '    uint32_t pbPress_ = 0;              // the presses it asked for (platform::Button bits)\n' +
    '    std::vector<char> seatFineAim_;'
]]);
const P = (b) => "pbPress_ |= 1u << (int)platform::Button::" + b;
ed('src/game/WorldBots.cpp', [
    ['                pc.requestWeaponSwitch(dir); ++b.switches; b.switchHold = 0.0f;\n',
     '                if (pbSandbox_) pbPress_ |= 1u << (int)(dir > 0 ? platform::Button::NextWeapon : platform::Button::PrevWeapon);   // online player bot\n' +
     '                else pc.requestWeaponSwitch(dir);\n' +
     '                ++b.switches; b.switchHold = 0.0f;\n'],
    ['                bag->reserve -= 1;\n                b.grenadeCooldown = std::max(6.0f, bag->fireInterval);\n',
     '                if (pbSandbox_) { ' + P('Grenade') + '; b.grenadeCooldown = std::max(6.0f, bag->fireInterval); ++b.grenades; }   // online player bot\n' +
     '                else {\n' +
     '                bag->reserve -= 1;\n                b.grenadeCooldown = std::max(6.0f, bag->fireInterval);\n'],
    ['                pc.exposeSelf();\n                ++b.grenades;\n',
     '                pc.exposeSelf();\n                ++b.grenades;\n                }\n'],
    ['        if (a.id != id || !a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) continue;\n',
     '        if (a.id != id || !a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) continue;\n' +
     '        if (pbSandbox_) {   // online player bot: the ability button (the seat path triggers it on every peer)\n' +
     '            const int slot = (int)(&a - &pc.abilities_[0]);\n' +
     '            pbPress_ |= 1u << (int)(slot == 0 ? platform::Button::Dash : platform::Button::Ability1);\n' +
     '            ++b.abilities;\n' +
     '            return true;\n' +
     '        }\n'],
    ['                w->onFired();\n                const float tick = w->fireInterval > 0.0f ? w->fireInterval : 0.1f;\n',
     '                if (pbSandbox_) { b.fireWish = true; return; }   // online player bot: Fire (the seat path runs the beam)\n' +
     '                w->onFired();\n                const float tick = w->fireInterval > 0.0f ? w->fireInterval : 0.1f;\n'],
    ['            pc.setAimPitch(b.pitch);\n            const float range = w->rangeM > 0.0f ? w->rangeM : 35.0f;\n' +
     '            if (pc.weaponUsable() && w->ammo == 0 && w->canReload()) { w->beginReload(); ++b.reloads; }\n',
     '            if (!pbSandbox_) pc.setAimPitch(b.pitch);   // online player bot: the camera pitch reaches the pawn through the command\n' +
     '            const float range = w->rangeM > 0.0f ? w->rangeM : 35.0f;\n' +
     '            if (pc.weaponUsable() && w->ammo == 0 && w->canReload()) { if (pbSandbox_) ' + P('Reload') + '; else w->beginReload(); ++b.reloads; }\n'],
    ['    pc.setAimPitch(b.pitch);\n    // Weapon state.\n',
     '    if (!pbSandbox_) pc.setAimPitch(b.pitch);\n    // Weapon state.\n'],
    ['    if (usable && w->ammo == 0 && w->canReload()) { w->beginReload(); ++b.reloads; }\n',
     '    if (usable && w->ammo == 0 && w->canReload()) { if (pbSandbox_) ' + P('Reload') + '; else w->beginReload(); ++b.reloads; }\n'],
    ['            startMeleeFor(pc, o.matchPlayer(), false, b.yaw);\n',
     '            if (pbSandbox_) { ' + P('Melee') + '; ++b.melees; b.meleeCooldown = b.frange(1.2f, 2.0f) + (2 - b.difficulty) * 0.5f; return; }   // online player bot\n' +
     '            startMeleeFor(pc, o.matchPlayer(), false, b.yaw);\n'],
    // Path upkeep: a private synchronous search (no shared queue / owner / budget).
    ['    if (wants && std::find(botSearchQueue_.begin(), botSearchQueue_.end(), b.player) == botSearchQueue_.end()) botSearchQueue_.push_back(b.player);\n',
     '    if (pbSandbox_) {   // online player bot: a private synchronous search when no time-sliced one is in flight (the bots\' state untouched)\n' +
     '        if (!wants || botNav_.searchActive()) return;\n' +
     '        b.wantRepath = false; b.repathTimer = chasing ? 1.5f : 6.0f; b.vehiclePath = vehicle; ++b.repaths;\n' +
     '        if (match_.matchTime() > b.avoidUntil) b.avoidCells.clear();\n' +
     '        ag.avoid = b.avoidCells.empty() ? nullptr : &b.avoidCells;\n' +
     '        std::vector<BotNav::Waypoint> path;\n' +
     '        if (botNav_.findPath(pos, b.goal.pos, ag, path) && !path.empty()) { b.path.swap(path); b.wp = 0; b.bestDist = 1e9f; b.progressTimer = 0.0f; }\n' +
     '        else botPathFailed(b, vehicle);\n' +
     '        return;\n' +
     '    }\n' +
     '    if (wants && std::find(botSearchQueue_.begin(), botSearchQueue_.end(), b.player) == botSearchQueue_.end()) botSearchQueue_.push_back(b.player);\n'],
    // playerBotInput: the sandbox scope, the presses into the input frame, no suicide online.
    ['    Character& pc = player_.pawn();\n    BotBrain& b = playerBot_;\n',
     '    Character& pc = player_.pawn();\n    BotBrain& b = playerBot_;\n' +
     '    struct PbScope { bool& f; ~PbScope() { f = false; } } pbScope{pbSandbox_};\n' +
     '    pbSandbox_ = onlineSeats_; pbPress_ = 0;   // online: input only\n'],
    ['    { const int keep = botPathBudget_; if (botSearchOwner_ < 0) botPathBudget_ = std::max(botPathBudget_, 1); botPathUpkeep(body, b, dt); if (botPathBudget_ > 0) botPathBudget_ = keep; }\n',
     '    if (pbSandbox_) botPathUpkeep(body, b, dt);   // online: a private search\n' +
     '    else { const int keep = botPathBudget_; if (botSearchOwner_ < 0) botPathBudget_ = std::max(botPathBudget_, 1); botPathUpkeep(body, b, dt); if (botPathBudget_ > 0) botPathBudget_ = keep; }\n'],
    ['    if (b.suicideWish) {   // stranded (botSteer / the off-mesh watch)',
     '    if (b.suicideWish && pbSandbox_) { b.suicideWish = false; b.offMeshRun = 0.0f; }   // online: no direct kill (input only)\n' +
     '    if (b.suicideWish) {   // stranded (botSteer / the off-mesh watch)'],
    ['    in.mouseDX = 0; in.mouseDY = 0;\n',
     '    for (size_t k = 0; k < std::size(in.pressed) && k < 32; ++k) if ((pbPress_ >> k) & 1u) in.pressed[k] = true;   // online: the brain\'s actions\n' +
     '    pbPress_ = 0;\n' +
     '    in.mouseDX = 0; in.mouseDY = 0;\n'],
]);
// The controller online (a wire seat): the tank quick turn's pawn writes (dash cooldown, sound serial) stay off the local pawn.
ed('src/game/PlayerController.h', [[
    '    bool guiding_ = false, detonate_ = false;\n',
    '    bool guiding_ = false, detonate_ = false;\n' +
    '    bool wireSeat_ = false;   // online: the pawn moves only through the seat commands (no direct pawn writes from input)\n' +
    'public:\n' +
    '    void setWireSeat(bool w) { wireSeat_ = w; }\n' +
    'private:\n'
]]);
// The Transform press: online it only travels in the command (the controller transformed the local pawn directly on the press: the
// NET.identity tick-602 split); every seat uses the direct path's StartTransform checks (robot fit + safe-spot shift).
ed('src/game/PlayerController.h', [[
    '    bool tryBeginTransform();\n',
    '    bool tryBeginTransform();\n' +
    '    // StartTransform for any pawn: TransformDisruptor refusal, the robot fit + safe-spot shift out of vehicle form (counters optional).\n' +
    '    static bool tryBeginTransformOn(Character& pawn, const CollisionWorld* col, int* cantTransform, int* transformFailed);\n'
]]);
ed('src/game/PlayerController.cpp', [
    ['        pawn_->vehicleState().dashCooldown = 1.2f;\n        ++pawn_->vehicleState().quickTurnSerial;   // Systems: Tank 180 sound event\n',
     '        if (!wireSeat_) {   // online the seat path owns the pawn (the camera turn stays)\n' +
     '        pawn_->vehicleState().dashCooldown = 1.2f;\n        ++pawn_->vehicleState().quickTurnSerial;   // Systems: Tank 180 sound event\n' +
     '        }\n'],
    ['        if (pawn_->moveForm() == Form::Robot) fineAimWanted_ = false;\n        tryBeginTransform();\n',
     '        if (pawn_->moveForm() == Form::Robot) fineAimWanted_ = false;\n' +
     '        if (!wireSeat_) tryBeginTransform();   // online: the press goes in the seat command (applied on every peer)\n'],
    ['bool PlayerController::tryBeginTransform() {\n' +
     '    if (!pawn_) return false;\n' +
     '    if (pawn_->transformDisruptRemain_ > 0.0f) { ++transformFailedCount_; return false; }   // TnBuffTransformDisruptor: transforming disabled\n' +
     '    if (pawn_->moveForm() == Form::Vehicle && col_) {\n',
     'bool PlayerController::tryBeginTransform() {\n' +
     '    return pawn_ && tryBeginTransformOn(*pawn_, col_, &cantTransformCount_, &transformFailedCount_);\n' +
     '}\n\n' +
     'bool PlayerController::tryBeginTransformOn(Character& pawnRef, const CollisionWorld* col_, int* cantTransform, int* transformFailed) {\n' +
     '    Character* pawn_ = &pawnRef;\n' +
     '    int dummyCant = 0, dummyFailed = 0;\n' +
     '    int& cantTransformCount_ = cantTransform ? *cantTransform : dummyCant;\n' +
     '    int& transformFailedCount_ = transformFailed ? *transformFailed : dummyFailed;\n' +
     '    if (pawn_->transformDisruptRemain_ > 0.0f) { ++transformFailedCount_; return false; }   // TnBuffTransformDisruptor: transforming disabled\n' +
     '    if (pawn_->moveForm() == Form::Vehicle && col_) {\n'],
]);
ed('src/game/World.cpp', [[
    '        if (down(platform::Button::Transform) && !pc.isTransforming()) pc.beginTransform();\n',
    '        if (down(platform::Button::Transform) && !pc.isTransforming()) {   // StartTransform with the direct path\'s checks (every peer)\n' +
    '            if (p == localPlayer_) player_.controller().tryBeginTransformSeat(pc, collision());\n' +
    '            else PlayerController::tryBeginTransformOn(pc, collision(), nullptr, nullptr);\n' +
    '        }\n'
]]);
ed('src/game/PlayerController.h', [[
    '    void setWireSeat(bool w) { wireSeat_ = w; }\n',
    '    void setWireSeat(bool w) { wireSeat_ = w; }\n' +
    '    bool tryBeginTransformSeat(Character& pawn, const CollisionWorld* col) { return tryBeginTransformOn(pawn, col, &cantTransformCount_, &transformFailedCount_); }   // + the HUD pulses\n'
]]);
ed('src/game/World.cpp', [[
    '        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.online;   // online (a session launch; never from the seed)\n',
    '        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.online;   // online (a session launch; never from the seed)\n' +
    '        player_.controller().setWireSeat(onlineSeats_);\n'
]]);
console.log('ok');
