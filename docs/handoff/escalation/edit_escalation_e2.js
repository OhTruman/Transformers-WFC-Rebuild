// Escalation E2a (GAMEPLAY_ESCALATION_SPEC.md §5 + RE's E1 review F1-F3): power, generators (drain, shared pool, the resolved New
// Power Level actions: bought pickup factories, doors / bridges, zone activation, own trigger volume off), Interact held per seat,
// TnSurvivalGame's 15 s auto-start countdown, the Escalation start state of the transforming actors, HUD svPower / svGenerator*, and
// the original's zone order (actor order + the binary's qsort; the Remnant wave-1 fix). WorldSurvival.cpp / Survival.h /
// EscalationData.inc / Application_EscTest.cpp are copied in from the Gameplay worktree (argv[2]). Run from a tree root (9ee3f174+).
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
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);

ed('src/game/PickupFactory.h', [
    ['    bool meshVisible() const { return available_; }\n',
     '    bool meshVisible() const { return available_ && !hidden_; }\n' +
     '    // Escalation: a bought factory is hidden (no mesh, no effects, no touch, no timer) until its generator\'s SeqAct_ToggleHidden UnHide.\n' +
     '    void setHidden(bool h) { hidden_ = h; }\n' +
     '    bool hidden() const { return hidden_; }\n'],
    ['    bool customEffectActive() const { return available_ && kind_ != Kind::AmmoCrate; }\n',
     '    bool customEffectActive() const { return available_ && !hidden_ && kind_ != Kind::AmmoCrate; }\n'],
    ['    bool beamActive() const { return available_ && shouldDisplayHighlightFx(kind_); }\n',
     '    bool beamActive() const { return available_ && !hidden_ && shouldDisplayHighlightFx(kind_); }\n'],
    ['    bool available_ = true;\n', '    bool available_ = true;\n    bool hidden_ = false;\n'],
]);
ed('src/game/PickupFactory.cpp', [
    ['void PickupFactory::tick(World& world, float dt) {\n', 'void PickupFactory::tick(World& world, float dt) {\n    if (hidden_) return;   // Escalation: not bought yet\n'],
]);
ed('src/game/TransformingActors.h', [
    ['    void setDesired(size_t i, int desired) { if (i < actors_.size()) actors_[i].desired = desired ? 1 : 0; }\n',
     '    void setDesired(size_t i, int desired) { if (i < actors_.size()) actors_[i].desired = desired ? 1 : 0; }\n' +
     '    // Escalation generators: SeqAct_ToggleHidden (UnHide / Hide) and SeqAct_ChangeCollision on an actor (the bridges); re-posed next tick.\n' +
     '    void setShown(size_t i, bool shown) { if (i < actors_.size()) { actors_[i].hidden = !shown; actors_[i].posedT = -1.0f; } }\n' +
     '    void setCollide(size_t i, bool on) { if (i < actors_.size()) { actors_[i].collide = on; actors_[i].posedT = -1.0f; } }\n' +
     '    int find(const std::string& name) const { for (size_t i = 0; i < actors_.size(); ++i) if (actors_[i].name == name) return (int)i; return -1; }\n'],
]);
ed('src/game/PlayerController.h', [
    ['    bool consumePickupRequest() { bool b = wantPickup_; wantPickup_ = false; return b; }\n',
     '    bool consumePickupRequest() { bool b = wantPickup_; wantPickup_ = false; return b; }\n' +
     '    bool interactHeld() const { return interactHeld_; }   // Interact held this frame (Escalation generators: _AllowDraining)\n'],
    ['wantPickup_ = false;\n', 'wantPickup_ = false;\n    bool interactHeld_ = false;\n'],
]);
ed('src/game/PlayerController.cpp', [
    ['    if (in.wasPressed(Button::Interact)) wantPickup_ = true;\n',
     '    if (in.wasPressed(Button::Interact)) wantPickup_ = true;\n    interactHeld_ = in.isDown(Button::Interact);\n'],
]);
ed('src/game/Match.cpp', [
    ['s.survival = true;', 's.survival = true; s.matchAutoStartCountdown = 15;'],   // TnSurvivalGame MatchAutoStartCountdown 15 [CONF RE class defaults]
]);
ed('src/game/World.h', [
    ['    std::vector<char> seatFineAim_;',
     '    std::vector<uint8_t> seatInteract_;   // each seat\'s last command Interact held bit (Escalation generators)\n    std::vector<char> seatFineAim_;'],
    ['    bool svSpawn(int slot);\n',
     '    bool svSpawn(int slot);\n' +
     '    void svSetZoneActive(int zone, bool on);\n' +
     '    bool svInteractHeld(int player) const;\n' +
     '    void svFireGenerator(int generator);\n' +
     '    bool svLaunching_ = false;   // the next resetForNewLevel is an Escalation level (transforming actors: the SV start state)\n'],
]);
ed('src/game/World.cpp', [
    ['    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);   // a fresh level\n',
     '    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr, svLaunching_);   // a fresh level\n'],
    ['\n    resetForNewLevel();\n', '\n    svLaunching_ = l.settings.survival;\n    resetForNewLevel();\n'],
    ['    s.pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);   // TryPickup\n',
     '    s.pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);   // TryPickup\n' +
     '    if (seatInteract_.size() <= (size_t)p) seatInteract_.resize((size_t)p + 1, 0);\n' +
     '    seatInteract_[(size_t)p] = held(platform::Button::Interact) ? 1 : 0;\n'],
    ['        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;\n',
     '        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;\n' +
     '        if (localPlayer_ >= 0 && (size_t)localPlayer_ < sv_.power.size()) {\n' +
     '            h.svPower = (int)sv_.power[(size_t)localPlayer_];\n' +
     '            const int g = sv_.nearGenerator[(size_t)localPlayer_];\n' +
     '            if (g >= 0) { h.svGeneratorName = svData_.generators[(size_t)g].nameKey; h.svGeneratorNeeded = (int)std::ceil(svData_.generators[(size_t)g].cost - sv_.pool[(size_t)g]); }\n' +
     '        }\n'],
]);
console.log('ok');
