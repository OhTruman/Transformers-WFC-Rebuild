// Pad X = TryInteract, else ReloadWeapon (RE; Systems' pad actions map X to both Interact and Reload): a tap next to an
// interactable interacted AND reloaded. The controller now drops the reload tap of an X press (Interact + Reload down on the same
// frame) when an interactable was offered at that press; the World tells it each frame before input (a carried object to pick up,
// a campaign interact prompt, an Escalation generator in reach, a DLC mechanism's button / detpack in reach). Keyboard E and R stay
// independent (not pressed on the same frame). Run from the tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/PlayerController.h', [
    ['    bool wantReload_ = false;     // latched on release of a tap < kReloadTapTime\n',
     '    bool wantReload_ = false;     // latched on release of a tap < kReloadTapTime\n' +
     '    bool interactAvailable_ = false;   // the World: an interactable is offered this frame (TryInteract would succeed)\n' +
     '    bool xInteracted_ = false;         // the current Reload press came with Interact (pad X) while one was offered: no reload\n'],
    ['    bool interactHeld() const { return interactHeld_; }',
     '    void setInteractAvailable(bool a) { interactAvailable_ = a; }   // World, each frame before handleInput (pad X: TryInteract else reload)\n' +
     '    bool interactHeld() const { return interactHeld_; }'],
]);
ed('src/game/PlayerController.cpp', [
    ['    if (!reloadDown && prevReloadDown_ && reloadHeld_ < cfg::kReloadTapTime) wantReload_ = true;\n',
     '    if (in.wasPressed(Button::Reload)) xInteracted_ = in.wasPressed(Button::Interact) && interactAvailable_;   // pad X: TryInteract first\n' +
     '    if (!reloadDown && prevReloadDown_ && reloadHeld_ < cfg::kReloadTapTime && !xInteracted_) wantReload_ = true;\n'],
]);
ed('src/game/World.h', [
    ['    bool svInteractHeld(int player) const;\n',
     '    bool svInteractHeld(int player) const;\n' +
     '    bool localInteractAvailable() const;   // an interactable offered to the local player (pad X: TryInteract else reload)\n'],
]);
ed('src/game/World.cpp', [
    ['void World::handleInput(const platform::InputFrame& in, float dt) {\n',
     '// Pad X is TryInteract, else reload: what TryInteract would find for the local player right now.\n' +
     'bool World::localInteractAvailable() const {\n' +
     '    if (!matchActive_ || localDead_ || localPlayer_ < 0 || (size_t)localPlayer_ >= match_.players().size()) return false;\n' +
     '    const Character& pc = player_.pawn();\n' +
     '    MapState::ObjPawn op{localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true,\n' +
     '                         pc.form() == Form::Robot && !pc.isTransforming() && !pc.isMeleeing()};\n' +
     '    if (mapState_.pickupCandidate(op) >= 0) return true;                  // the Code of Power / bomb\n' +
     '    if (cp_.active && !cp_.interactPrompt.empty()) return true;          // a campaign interact\n' +
     '    if (sv_.phase != SurvivalState::Phase::Off) {\n' +
     '        if ((size_t)localPlayer_ < sv_.nearGenerator.size() && sv_.nearGenerator[(size_t)localPlayer_] >= 0) return true;   // a generator\n' +
     '        const SvMech& M = svData_.mech;\n' +
     '        const core::Vec3 p = pc.position();\n' +
     '        for (size_t k = 0; k < M.lasers.size() && k < sv_.mech.lasers.size(); ++k)\n' +
     '            if (sv_.mech.lasers[k].active && sv_.mech.lasers[k].t >= M.lasers[k].armDelay && core::length(p - M.lasers[k].button) <= M.lasers[k].buttonRange) return true;\n' +
     '        for (size_t k = 0; k < M.detpacks.size() && k < sv_.mech.detpacks.size(); ++k)\n' +
     '            if (!sv_.mech.detpacks[k].used && core::length(p - M.detpacks[k].pos) <= M.detpacks[k].range) return true;\n' +
     '    }\n' +
     '    return false;\n' +
     '}\n\n' +
     'void World::handleInput(const platform::InputFrame& in, float dt) {\n' +
     '    player_.controller().setInteractAvailable(localInteractAvailable());\n'],
]);
console.log('ok');
