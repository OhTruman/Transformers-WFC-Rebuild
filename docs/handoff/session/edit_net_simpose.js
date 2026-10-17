// NET.identity follow-up 5 (Experimental / Systems: the two-process tick-1299 split that a replay cannot show):
// 1. Character::drawYawOffset_ (the local pawn's per-frame presentation yaw, set by the LIVE input pass) was part of meshMatrix, which the
//    sim reads through boneWorld (vehicle weapon sockets = shot origins), the gameplay weapon socket and separatePawns. The step prefix
//    zeroes it only after the seat commands have fired, and the background remainder can overlap the next input pass. Sim queries now use
//    simMatrix (the simulated yaw only); drawing keeps meshMatrix.
// 2. WFC_NET_STATEDUMP=<sim tick>: one block of every participant's sim state at the end of that tick (position, velocity, yaw, hp, form,
//    pose hash, presentation yaw offset, weapon / ability timers), the bots' and Smart RNGs, the sim and Match RNGs and the projectiles, so
//    a two-process split names its field.
// 3. CTF ?Rounds=N (RE ONLINE_RULES_CHECK_RE: the Frontend builds it as the original's BuildURL; TnMultiplayerGame.InitGame
//    GetIntOption(Options, "Rounds")); URLs without it keep the PointsToWin path. WFC_SESSIONTEST: a CTF launch with ?Rounds=4.
// Run from a tree root (integration df4d62aa+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Character.h', [
    ['        // Same model transform as draw()/updateWeaponSocket(), including the transform-shift offset.\n        out = meshMatrix(form_) * animScratch_[(size_t)n];\n',
     '        // The simulated model transform (no presentation yaw offset), including the transform-shift offset.\n        out = simMatrix(form_) * animScratch_[(size_t)n];\n'],
    ['    static float meshYawOffset();\n',
     '    static float meshYawOffset();\n' +
     '    // The model transform the simulation uses: meshMatrix without the per-frame presentation yaw offset (drawYawOffset_).\n' +
     '    core::Mat4 simMatrix(Form f) const;\n' +
     '    // Diagnostics (WFC_NET_STATEDUMP): a hash of this step\'s bone pose.\n' +
     '    uint64_t poseHash() const {\n' +
     '        uint64_t h = 1469598103934665603ULL;\n' +
     '        const unsigned char* c = animScratch_.empty() ? nullptr : (const unsigned char*)animScratch_.data();\n' +
     '        for (size_t i = 0; c && i < animScratch_.size() * sizeof(animScratch_[0]); ++i) { h ^= c[i]; h *= 1099511628211ULL; }\n' +
     '        return h;\n' +
     '    }\n'],
]);
ed('src/game/Character.cpp', [
    ['core::Mat4 Character::meshMatrix(Form f) const {\n',
     'core::Mat4 Character::simMatrix(Form f) const {\n' +
     '    core::Mat4 m = core::Mat4::translate(meshOrigin(f)) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset);\n' +
     '    if (moveForm() == Form::Vehicle && (veh_.pitch != 0.0f || veh_.roll != 0.0f))\n' +
     '        m = m * core::Mat4::rotateZ(veh_.pitch) * core::Mat4::rotateX(veh_.roll);\n' +
     '    return m;\n' +
     '}\n\n' +
     'core::Mat4 Character::meshMatrix(Form f) const {\n'],
    ['    weaponWorld_ = meshMatrix(Form::Robot) * weaponBoneCur_ * weaponOffset_;\n',
     '    weaponWorld_ = simMatrix(Form::Robot) * weaponBoneCur_ * weaponOffset_;   // gameplay (muzzle, traces): the simulated yaw\n'],
]);
ed('src/game/World.cpp', [
    ['                const core::Mat4 m = c.meshMatrix(Form::Vehicle);\n', '                const core::Mat4 m = c.simMatrix(Form::Vehicle);\n'],
    ['                const core::Mat4 m = V.meshMatrix(Form::Vehicle);\n', '                const core::Mat4 m = V.simMatrix(Form::Vehicle);\n'],
    // Rounds=N.
    ['    if (out.settings.rounds > 0 && opt.count("PointsToWin")) { out.settings.rounds = std::max(2, std::atoi(opt["PointsToWin"].c_str())); out.settings.goalScore = 1000000; }\n',
     '    if (out.settings.rounds > 0 && opt.count("PointsToWin")) { out.settings.rounds = std::max(2, std::atoi(opt["PointsToWin"].c_str())); out.settings.goalScore = 1000000; }\n' +
     '    // TnMultiplayerGame.InitGame: Rounds = GetIntOption(Options, "Rounds") - the Frontend\'s BuildURL sends it for CTF [CONF RE rules check].\n' +
     '    if (out.settings.rounds > 0 && opt.count("Rounds")) { out.settings.rounds = std::max(2, std::atoi(opt["Rounds"].c_str())); out.settings.goalScore = 1000000; }\n'],
    // The state dump at the end of the tick.
    ['    static const char* simHashEnv = std::getenv("WFC_SIMHASH");\n',
     '    static const long stateDumpAt = std::getenv("WFC_NET_STATEDUMP") ? std::atol(std::getenv("WFC_NET_STATEDUMP")) : -1;\n' +
     '    if (stateDumpAt >= 0 && (long)simTick_ == stateDumpAt) {\n' +
     '        LOG_INFO("STATEDUMP tick %lld local p%d online %d simRand %08x matchRand %08x", (long long)simTick_, localPlayer_, (int)onlineSeats_, core::simRandState(), match_.rngState());\n' +
     '        for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '            const Character* c = participantPawn((int)q);\n' +
     '            if (!c) { LOG_INFO("STATEDUMP p%zu -", q); continue; }\n' +
     '            const core::Vec3 p = c->position(), v = c->velocity(); const Weapon& w = c->weapon();\n' +
     '            LOG_INFO("STATEDUMP p%zu pos %.6f %.6f %.6f vel %.6f %.6f %.6f yaw %.6f hp %.4f form %d tr %d pose %016llx drawYaw %.6f w %s ammo %d since %.4f rel %d ab %.3f/%.3f hover %d",\n' +
     '                     q, p.x, p.y, p.z, v.x, v.y, v.z, c->yaw(), c->health().current, (int)c->moveForm(), (int)c->isTransforming(), (unsigned long long)c->poseHash(),\n' +
     '                     c->drawYawOffset(), w.def ? w.def->id : "-", w.ammo, w.sinceShot, (int)w.reloading(), c->abilities_[0].cooldown, c->abilities_[1].cooldown, c->hoverState_);\n' +
     '        }\n' +
     '        for (const BotBrain& b : bots_) LOG_INFO("STATEDUMP bot p%d rng %08x tgt %d yaw %.6f wp %zu/%zu", b.player, b.rng, b.target, b.yaw, b.wp, b.path.size());\n' +
     '        for (size_t q = 0; q < smart_.size(); ++q) if (smart_[q].active) LOG_INFO("STATEDUMP smart p%zu rng %08x", q, smart_[q].rng);\n' +
     '        for (const auto& pr : projectiles_) LOG_INFO("STATEDUMP proj inst %d pos %.6f %.6f %.6f vel %.6f %.6f %.6f life %.4f", pr.instigator, pr.pos.x, pr.pos.y, pr.pos.z, pr.vel.x, pr.vel.y, pr.vel.z, pr.life);\n' +
     '    }\n' +
     '    static const char* simHashEnv = std::getenv("WFC_SIMHASH");\n'],
]);
ed('src/game/Match.h', [[
    '    void seedRandom(unsigned s) { rng_ = s ? s : 0x1234ABCDu; }\n',
    '    void seedRandom(unsigned s) { rng_ = s ? s : 0x1234ABCDu; }\n' +
    '    unsigned rngState() const { return rng_; }   // diagnostics (WFC_NET_STATEDUMP)\n'
]]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    {   // CTF ?Rounds=N (the Frontend\'s BuildURL; InitGame reads "Rounds"); without it, the PointsToWin path as before\n' +
    '        game::MatchLaunch R4; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=CTF?Rounds=4", R4);\n' +
    '        game::MatchLaunch R0; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=CTF", R0);\n' +
    '        LOG_INFO("SESSIONTEST CTF rounds: ?Rounds=4 -> %d, none -> %d", R4.settings.rounds, R0.settings.rounds);\n' +
    '        check(R4.settings.rounds == 4 && R0.settings.rounds == 2, "CTF ?Rounds=4 plays 4 rounds; no option keeps 2");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
