// Package 7 hover item (RE GAMEPLAY_HOVER_RE.md; Integration decisions 2026-10-17 in GAMEPLAY_RIDE_HEIGHT_DESIGN.md):
// 1. TnHoverTankSimulation's suspension force has NO normal factor: ApplyForce(-dir * max(0, F)) (car / truck: max(0, (-dir.N) F)).
// 2. AI-driven hover vehicles do not run the player's UpdateTurn pitch / roll correction (car / truck: ShouldUpright = no contact or
//    upside down; tank: !(IsStable && OnTheGround)). Their righting is HoverPhysics.RotateToTargetOrientation alone, gated by
//    IsUnstable / !IsStable = world |pitch| or |roll| > 30 deg (the rotator; never the contact normal), rate-limited by
//    AiMaxAngularSpeed / Accel (the existing aiTipped block). The yaw part of UpdateTurn stays (the rebuild's heading control).
// 3. The gate is the absolute one for every bot: the contact-relative variant (WFC_TIPREL) becomes an opt-in measurement switch.
// 4. Classic bots run the AI righting path too (Integration, user-delegated: Classic is the original's AI; a proven gap). This is a
//    deliberate Classic change: the SIMHASH re-baseline is shared with the ride-height and squad edits of this item.
// 5. VEHTEST: the truck / Optimus rest row next to car and tank, with the RE equilibrium origin heights (car ~125 UU, truck ~109).
// Run from a tree root (af4d0731 or later).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/CharacterMovement.cpp', [
    ['            float Fa = std::max(0.0f, core::dot(B.z, n) * F);   // ApplyForce(up * FMax(0, Dot(up,N)*Force), mount)\n',
     '            // ApplyForce(up * FMax(0, Dot(up,N)*Force), mount); TnHoverTankSimulation: FMax(0, Force), no normal factor [CONF RE hover 2.2]\n' +
     '            float Fa = tank ? std::max(0.0f, F) : std::max(0.0f, core::dot(B.z, n) * F);\n'],
    ['            if (!(!unstable && vs.onTheGround)) { vs.angVel.x = k30 * vs.roll / dt; vs.angVel.y = k30 * vs.pitch / dt; }\n',
     '            // (player path only: an AI tank rights itself through RotateToTargetOrientation below [CONF RE hover 2.4])\n' +
     '            if (tipMode < 0 && !(!unstable && vs.onTheGround)) { vs.angVel.x = k30 * vs.roll / dt; vs.angVel.y = k30 * vs.pitch / dt; }\n'],
    ['            if (contacts == 0 || B.z.y < 0.01f) {          // ShouldUpright\n',
     '            if (tipMode < 0 && (contacts == 0 || B.z.y < 0.01f)) {   // ShouldUpright (player path only [CONF RE hover 2.4])\n'],
]);
ed('src/game/WorldBots.cpp', [[
    '            if (bt) { in.aiTip = std::atoi(bt); in.aiTipRel = tr && std::atoi(tr) != 0; }\n' +
    '            else if (botIsSmart(b.player)) { in.aiTip = 0; in.aiTipRel = !tr || std::atoi(tr) != 0; }\n',
    '            // Every bot (Smart and Classic: the original AI path; Integration 2026-10-17) rights itself with the absolute IsUnstable gate\n' +
    '            // [CONF RE hover 3]; WFC_TIPREL=1 opts into the 2026-10-14 contact-relative variant (measurement only).\n' +
    '            if (bt) { in.aiTip = std::atoi(bt); in.aiTipRel = tr && std::atoi(tr) != 0; }\n' +
    '            else { in.aiTip = 0; in.aiTipRel = tr && std::atoi(tr) != 0; }\n'
]]);
ed('src/game/VehicleTests.cpp', [
    ['        static ChassisDef car, tank, jet;\n        bool ok = loadChassisDef(vs, "Car2", car) && loadChassisDef(vs, "Tank3", tank) && loadChassisDef(vs, "Jet", jet);\n',
     '        static ChassisDef car, tank, jet, truck;\n        bool ok = loadChassisDef(vs, "Car2", car) && loadChassisDef(vs, "Tank3", tank) && loadChassisDef(vs, "Jet", jet) &&\n' +
     '                  loadChassisDef(vs, "Truck", truck);\n'],
    ['            auto rest = [&](const ChassisDef& d, const char* name) {\n',
     '            auto rest = [&](const ChassisDef& d, const char* name, float reOriginUU) {   // reOriginUU: RE hover 1.4 (0: not derived)\n'],
    ['                LOG_INFO("VEHTEST %s rest: root %.3f m above the floor, COM %.3f (spring L_eq %.3f m), contacts %d, pitch %.1f roll %.1f deg",\n' +
     '                         name, s.c.position().y, s.c.position().y + V.comUp, leq, s.c.vehicleState().contacts,\n' +
     '                         s.c.vehicleState().pitch * 57.2958f, s.c.vehicleState().roll * 57.2958f);\n',
     '                LOG_INFO("VEHTEST %s rest: root %.3f m above the floor, COM %.3f (spring L_eq %.3f m), contacts %d, pitch %.1f roll %.1f deg; RE origin %.2f m",\n' +
     '                         name, s.c.position().y, s.c.position().y + V.comUp, leq, s.c.vehicleState().contacts,\n' +
     '                         s.c.vehicleState().pitch * 57.2958f, s.c.vehicleState().roll * 57.2958f, reOriginUU * 0.01f);\n'],
    ['            rest(car, "car  ");\n            rest(tank, "tank ");\n',
     '            rest(car, "car  ", 125.0f);\n            rest(truck, "truck", 109.0f);\n            rest(tank, "tank ", 0.0f);\n'],
]);
console.log('ok');
