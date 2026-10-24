// User playtest 3 (2026-10-24), multiplayer batch (Gameplay). Run from the tree root.
//  1. P0 aim (shots above / off the crosshair, sniper obvious): fireRobotShot applied the weapon's permanent camera pitch step
//     (TnRecoilCameraBehavior PitchAdjustmentDeg, onLocalShot) BEFORE it built the shot from the camera rotation, so every shot
//     carried its own recoil (Shotgun / EMP 4 deg, Bazooka / Heavy Pistol 1.5, Sniper 1.0 at a 20 deg fine-aim FOV). The shot is now
//     built from the rotation the crosshair showed, then the recoil steps the camera (the vehicle path already did this).
//     WFC_AIMLOG=1 (diagnostic): per local robot shot, the camera pitch / yaw used for the shot minus the pitch / yaw at the trigger.
//  2. Orbital Beacon (and Hard Lock) without a visible effect: the versus pawn markers asked for the setup
//     "TransformerEnemyMarkerSetup", but TnObjectiveMarkerTypeTransformerVersus authors "EnemyMarkerSetup" (render/_ui/hud_markers.json;
//     HudMarkers skips an unknown setup), so enemy markers never drew. The sim side (seeEnemiesRemain_ 30 s -> drawn enemy tags,
//     WFC_PARTICIPANTTEST) was right; the original's effect = enemy objective markers for 30 s + the MP_OrbitalReconActivated*Dialog
//     announcer lines (TnBuffSeeEnemyObjectiveMarkers, ABT_Team; AssetTools assets.json).
//  3. XP on kills: GameFlow::canGainXp applied the ORIGINAL online rule (private games earn nothing) whenever the session role
//     was not Offline - and a private match is now a hosted session even with nobody else in it. A host alone (no session
//     members) keeps the PC offline rule (private bot matches earn progression); a host with members or a member: the original.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/PlayerController.cpp', [
    ['            onLocalShot(w.def ? w.def->id : nullptr);   // TnPlayerController.OnWeaponFired: camera recoil + shake' + NL,
     '            const float trigPitch = camPitch_, trigYaw = camYaw_;   // the rotation the crosshair showed at the trigger (WFC_AIMLOG)' + NL],
    ['            else { for (int k = 0; k < std::max(1, pawn_->weapon().shots); ++k) { world.setPelletIndex(k); world.fireHitscan(start, camDir); } world.setPelletIndex(0); }' + NL +
     '    };' + NL,
     '            else { for (int k = 0; k < std::max(1, pawn_->weapon().shots); ++k) { world.setPelletIndex(k); world.fireHitscan(start, camDir); } world.setPelletIndex(0); }' + NL +
     '            static const bool aimLog = std::getenv("WFC_AIMLOG") != nullptr;   // diagnostic: the shot rotation vs the trigger\'s' + NL +
     '            if (aimLog) {' + NL +
     '                const float sp = std::asin(core::clampf(camDir.y, -1.0f, 1.0f)), sy = std::atan2(-camDir.x, -camDir.z);' + NL +
     '                LOG_INFO("AIMLOG %s shot-vs-trigger pitch %+.3f deg yaw %+.3f deg", w.def ? w.def->id : "IonBlaster",' + NL +
     '                         (sp - trigPitch) * 57.29578f, std::remainder(sy - trigYaw, 6.2831853f) * 57.29578f);' + NL +
     '            }' + NL +
     '            // TnPlayerController.OnWeaponFired after the shot: camera recoil (the permanent pitch step) + shake, as the vehicle path' + NL +
     '            onLocalShot(w.def ? w.def->id : nullptr);' + NL +
     '    };' + NL],
]);
ed('src/game/World.cpp', [
    ['            m.setup = t.ally ? "AllyMarkerSetup" : "TransformerEnemyMarkerSetup"; m.relation = t.ally ? 0 : 1; m.player = t.player;',
     '            m.setup = t.ally ? "AllyMarkerSetup" : "EnemyMarkerSetup"; m.relation = t.ally ? 0 : 1; m.player = t.player;   // the type\'s authored setups'],
]);
{   // the World.h comment
    const f = 'src/game/World.h'; let s = fs.readFileSync(f, 'utf8');
    s = s.replace('"AllyMarkerSetup" / "TransformerEnemyMarkerSetup" / "MarkerSetup"', '"AllyMarkerSetup" / "EnemyMarkerSetup" / "MarkerSetup"');
    fs.writeFileSync(f, s);
}
ed('src/frontend/GameFlow.cpp', [
    ['    if (role_ != SessionRole::Offline) return publicGame;' + NL,
     '    // a private host with nobody else in the session plays as offline (playtest 3: XP stopped once private matches became hosted sessions)' + NL +
     '    if (role_ == SessionRole::Member || (role_ == SessionRole::Host && !onlineMembers_.empty())) return publicGame;' + NL],
]);
ed('tests/FrontendTests.cpp', [
    ['static void testOnlineHostJoin() {' + NL,
     'static void testSoloHostXp() {' + NL +
     '    // playtest 3: a private match hosted alone earns XP (the PC offline rule), as before private matches became hosted sessions' + NL +
     '    using T = SessionEvent::Type;' + NL +
     '    SessionEvent o = ev(T::HostOpened); o.joinCode = "WFC-7QK2-M9TD"; o.address = "192.168.1.20:7777";' + NL +
     '    OnlineRig r({after("host", o, 0.5)});' + NL +
     '    r.f().call("Online.EditGameMode", {"TDM"});' + NL +
     '    r.f().call("Online.PlayPrivateGame", {"TDM"});' + NL +
     '    const bool inLobby = runUntil(*r.rt, [&] { return r.f().level() == LevelKind::GameLobby && !r.f().loading().active; });' + NL +
     '    const bool host = r.f().sessionRole() == GameFlow::SessionRole::Host;' + NL +
     '    check(r.ok && inLobby && host && r.f().canGainXp() && r.f().buildLaunchUrl().find("WfcCanGainXp=0") == std::string::npos,' + NL +
     '          "playtest3.solo_host_xp", std::to_string(inLobby) + std::to_string(host) + std::to_string(r.f().canGainXp()));' + NL +
     '}' + NL + NL +
     'static void testOnlineHostJoin() {' + NL],
    ['    testOnlineHostJoin();' + NL, '    testOnlineHostJoin();' + NL + '    testSoloHostXp();' + NL],
]);
console.log('ok');
