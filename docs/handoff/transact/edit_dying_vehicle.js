// Dying-vehicle collision [RE VEHICLE_DEATH_PRESENTATION_RE.md 2babd61, CONFIRMED]: TnVehicle VehicleDying keeps the live vehicle's rigid
// body and full collision until the gib, blocking pawns, projectiles and traces for TotalTime (car / tank: moving U(0.3, 0.75) s,
// stationary U(0.2, 0.5) s; jets U(4, 5) s). Ours despawned the pawn at death (nothing blocked).
// - A pool of 16 unit-box dynamic sets per world (pawn + weapon), made at map load (no sets added mid-match, as the barriers' pool).
// - A vehicle-form death (local or participant, at the sim death sites, once per death) queues a body: the vehicle's collision cylinder
//   as a box (radius x half height x radius, the pawn's yaw) at its location, TotalTime drawn from the sim RNG then. "Moving" = horizontal
//   speed > 1 m/s [H: the threshold].
// - The main-thread map tick applies queued bodies and expires them (deterministic: one step after the death, every peer alike).
// - Not done: the hull gib's hitscan-only body (H; it follows the presentation-side wreck) [PARTIAL, noted to Integration].
// Run from a tree root after edit_transforming_actors.js.
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
    '    TransformingActors transforming_;   // the map\'s TnTransformingWorldActors (doors / bridges): state, collision, nav blocks\n',
    '    TransformingActors transforming_;   // the map\'s TnTransformingWorldActors (doors / bridges): state, collision, nav blocks\n' +
    '    // VehicleDying bodies (see noteVehicleDeath): a pool of unit-box dynamic sets; queued at the death, applied in the map tick.\n' +
    '    struct DyingBody { int pawnSet = -1, weaponSet = -1; float remain = 0.0f; };\n' +
    '    struct DyingRequest { core::Vec3 pos{0, 0, 0}; float yaw = 0.0f, radius = 1.0f, halfHeight = 1.0f, time = 0.5f; };\n' +
    '    std::vector<DyingBody> dyingBodies_;\n' +
    '    std::vector<DyingRequest> dyingQueue_;\n' +
    '    void noteVehicleDeath(const Character& pawn);\n' +
    '    void tickDyingBodies(float dt);\n'
]]);
ed('src/game/World.cpp', [
    ['            transforming_.load(mapName_, mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n        }\n',
     '            transforming_.load(mapName_, mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n        }\n' +
     '        {   // the VehicleDying body pool: unit boxes (+-1), scaled by the pose; disabled until used\n' +
     '            std::vector<core::Vec3> tris;\n' +
     '            static const int f[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1}, {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};\n' +
     '            for (const auto& t : f) for (int k = 0; k < 3; ++k) { const int i = t[k]; tris.push_back(core::Vec3{(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f}); }\n' +
     '            dyingBodies_.clear(); dyingQueue_.clear();\n' +
     '            for (int i = 0; i < 16; ++i) {\n' +
     '                DyingBody b;\n' +
     '                b.pawnSet = collision_.addDynamicSet(tris, core::Mat4::identity()); collision_.setDynamicEnabled(b.pawnSet, false);\n' +
     '                if (weaponCollision_.valid()) { b.weaponSet = weaponCollision_.addDynamicSet(tris, core::Mat4::identity()); weaponCollision_.setDynamicEnabled(b.weaponSet, false); }\n' +
     '                dyingBodies_.push_back(b);\n' +
     '            }\n' +
     '        }\n'],
    ['                              transforming_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr); }\n',
     '                              transforming_.tick(dt, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n' +
     '                              tickDyingBodies(dt); }\n'],
    ['    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);   // a fresh level\n',
     '    if (collision_.valid()) transforming_.resetForNewMatch(collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);   // a fresh level\n' +
     '    dyingQueue_.clear();\n' +
     '    for (DyingBody& b : dyingBodies_) { b.remain = 0.0f; if (b.pawnSet >= 0) collision_.setDynamicEnabled(b.pawnSet, false); if (b.weaponSet >= 0 && weaponCollision_.valid()) weaponCollision_.setDynamicEnabled(b.weaponSet, false); }\n'],
    // The sim death sites: participants (applyMatchDamage, tickMatch PlayerKilled) and the local player.
    ['                    onPawnDeath(opp->pawn().chassis().id, opp->pawn().moveForm() == Form::Vehicle, kdt, opp->pawn().actorLocation());\n                }\n                opp->despawn();\n',
     '                    onPawnDeath(opp->pawn().chassis().id, opp->pawn().moveForm() == Form::Vehicle, kdt, opp->pawn().actorLocation());\n                }\n' +
     '                if (opp->spawned()) noteVehicleDeath(opp->pawn());   // VehicleDying: the body keeps blocking until the gib\n' +
     '                opp->despawn();\n'],
    ['                if (e.player == localPlayer_) localDead_ = true;\n                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == e.player) o->despawn();\n',
     '                if (e.player == localPlayer_ && !localDead_) noteVehicleDeath(player_.pawn());\n' +
     '                if (e.player == localPlayer_) localDead_ = true;\n' +
     '                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == e.player) { if (o->spawned()) noteVehicleDeath(o->pawn()); o->despawn(); }\n'],
    ['    match_.killed(killer, localPlayer_, suicide, damageType, &kc);\n    localDead_ = true;\n',
     '    match_.killed(killer, localPlayer_, suicide, damageType, &kc);\n    noteVehicleDeath(player_.pawn());   // VehicleDying body\n    localDead_ = true;\n'],
    ['void World::requestAmmoBeacon(int owner) {\n',
     '// ---- VehicleDying collision [CONF RE VEHICLE_DEATH_PRESENTATION_RE.md: the live rigid body and collision stay until the gib] ----\n' +
     'void World::noteVehicleDeath(const Character& pawn) {\n' +
     '    if (pawn.moveForm() != Form::Vehicle || dyingBodies_.empty()) return;\n' +
     '    const bool jet = pawn.vehicleParams().form == VehicleFormType::Jet;\n' +
     '    const core::Vec3 v = pawn.velocity();\n' +
     '    const bool moving = std::sqrt(v.x * v.x + v.z * v.z) > 1.0f;   // [H] the moving / stationary threshold\n' +
     '    DyingRequest r;\n' +
     '    r.pos = pawn.actorLocation(); r.yaw = pawn.yaw();\n' +
     '    r.radius = pawn.cylinderRadius(Form::Vehicle); r.halfHeight = pawn.cylinderHalfHeight(Form::Vehicle);\n' +
     '    const float u = core::simRand01();\n' +
     '    r.time = jet ? 4.0f + u : (moving ? 0.3f + 0.45f * u : 0.2f + 0.3f * u);   // TotalTime: jets U(4, 5); moving U(0.3, 0.75); still U(0.2, 0.5)\n' +
     '    dyingQueue_.push_back(r);\n' +
     '}\n\n' +
     'void World::tickDyingBodies(float dt) {\n' +
     '    CollisionWorld* weapon = weaponCollision_.valid() ? &weaponCollision_ : nullptr;\n' +
     '    for (DyingBody& b : dyingBodies_) {\n' +
     '        if (b.remain <= 0.0f) continue;\n' +
     '        b.remain -= dt;\n' +
     '        if (b.remain > 0.0f) continue;\n' +
     '        b.remain = 0.0f;   // the gib: the dying actor stops blocking\n' +
     '        if (b.pawnSet >= 0) collision_.setDynamicEnabled(b.pawnSet, false);\n' +
     '        if (weapon && b.weaponSet >= 0) weapon->setDynamicEnabled(b.weaponSet, false);\n' +
     '    }\n' +
     '    for (const DyingRequest& r : dyingQueue_) {\n' +
     '        DyingBody* slot = nullptr;\n' +
     '        for (DyingBody& b : dyingBodies_) if (b.remain <= 0.0f) { slot = &b; break; }\n' +
     '        if (!slot) { LOG_WARN("vehicle dying: body pool full (16)"); break; }\n' +
     '        const core::Mat4 pose = core::Mat4::translate(r.pos) * core::Mat4::rotateY(r.yaw) * core::Mat4::scale(core::Vec3{r.radius, r.halfHeight, r.radius});\n' +
     '        slot->remain = r.time;\n' +
     '        if (slot->pawnSet >= 0) { collision_.setDynamicPose(slot->pawnSet, pose); collision_.setDynamicEnabled(slot->pawnSet, true); }\n' +
     '        if (weapon && slot->weaponSet >= 0) { weapon->setDynamicPose(slot->weaponSet, pose); weapon->setDynamicEnabled(slot->weaponSet, true); }\n' +
     '    }\n' +
     '    dyingQueue_.clear();\n' +
     '}\n\n' +
     'void World::requestAmmoBeacon(int owner) {\n'],
]);
ed('src/core/Application.cpp', [[
    '    world_.launchMatch(L);\n    const game::TransformingActors& T = world_.transformingActors();\n',
    '    world_.launchMatch(L);\n    game::MatchOpponent* dvOpponent = world_.addMatchOpponent("DV", false);   // the VehicleDying check below\n' +
    '    const game::TransformingActors& T = world_.transformingActors();\n'
], [
    '    if (openTotal) check(opened == openTotal, "level-start doors open and stop blocking their closed pose (" + std::to_string(opened) + "/" + std::to_string(openTotal) + ")");\n',
    '    if (openTotal) check(opened == openTotal, "level-start doors open and stop blocking their closed pose (" + std::to_string(opened) + "/" + std::to_string(openTotal) + ")");\n' +
    '    // VehicleDying: a vehicle-form death leaves a blocking body until the gib (car / tank <= 0.75 s).\n' +
    '    {\n' +
    '        game::MatchOpponent* v = dvOpponent;   // added at launch (a harness opponent added mid-match may get no start)\n' +
    '        for (int w = 0; w < 40 && v && !v->spawned(); ++w) run(1.0f);\n' +
    '        if (v && v->spawned()) {\n' +
    '            v->pawn().beginTransform();\n' +
    '            run(3.0f);\n' +
    '            const bool veh = v->pawn().moveForm() == game::Form::Vehicle;\n' +
    '            const core::Vec3 c = v->pawn().actorLocation();\n' +
    '            const float r = v->pawn().cylinderRadius(game::Form::Vehicle) + 1.0f;\n' +
    '            const core::Vec3 p0 = c + core::Vec3{-r, 0, 0}, p1 = c + core::Vec3{r, 0, 0};\n' +
    '            float tt; const bool before = world_.pawnCollisionWorld().dynamicSetHit(p0, p1, tt) >= 0;\n' +
    '            world_.applyMatchDamage(v->matchPlayer(), world_.localMatchPlayer(), 99999.0f, false, "TransGame.TnDamageTypeAssaultRifle");\n' +
    '            run(0.1f);\n' +
    '            const bool dying = world_.pawnCollisionWorld().dynamicSetHit(p0, p1, tt) >= 0 && world_.weaponCollisionWorld().dynamicSetHit(p0, p1, tt) >= 0;\n' +
    '            run(1.5f);\n' +
    '            const bool after = world_.pawnCollisionWorld().dynamicSetHit(p0, p1, tt) >= 0;\n' +
    '            check(veh && !before && dying && !after, "a dying vehicle blocks pawns and shots until the gib, then nothing");\n' +
    '        } else {\n' +
    '            const auto& mp = world_.match().players()[(size_t)(v ? v->matchPlayer() : 0)];\n' +
    '            LOG_INFO("TRANSACTTEST DV: state %d alive %d respawn %.1f selected %d spawnError \'%s\' chassis %s", (int)world_.match().state(), (int)mp.alive,\n' +
    '                     mp.timeToRespawn, (int)mp.hasSelectedCharacter, mp.spawnError.c_str(), mp.chassis.c_str());\n' +
    '            LOG_INFO("TRANSACTTEST SKIP the dying-vehicle check: the harness opponent got no pawn on this map (no start)");\n' +
    '        }\n' +
    '    }\n'
]]);
console.log('ok');
