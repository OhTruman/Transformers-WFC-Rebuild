// DIAGNOSTIC (not for hand-off): WFC_EYEBOTAUDIT=1 - visible-behaviour detectors per sim step (finishRemainder's step count = the
// SIMHASH step). One "EYEBOT <kind> step <n> t <match s> p<player> ..." line per episode. Run from the archive root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', `    void onPawnDeath(const std::string& chassisId, bool vehicleForm, const std::string& damageType, const core::Vec3& pos);`,
`    void onPawnDeath(const std::string& chassisId, bool vehicleForm, const std::string& damageType, const core::Vec3& pos);
    void eyeBotAudit(float dt);   // DIAGNOSTIC (WFC_EYEBOTAUDIT)`);
ed('src/game/World.cpp', `    asyncJitter();   // WFC_ASYNCJITTER: the background part starts late by a seeded 0-8 ms (determinism self-test)
    const double t0 = profNowMs();
    stepRemainder(pendingDt_);`, `    asyncJitter();   // WFC_ASYNCJITTER: the background part starts late by a seeded 0-8 ms (determinism self-test)
    const double t0 = profNowMs();
    stepRemainder(pendingDt_);
    eyeBotAudit(pendingDt_);   // DIAGNOSTIC (WFC_EYEBOTAUDIT): read-only`);
ed('src/game/World.cpp', `void World::tickVehicleAudio(float dt, const VehicleFormSignals& s) {`, `// DIAGNOSTIC (WFC_EYEBOTAUDIT): read-only visible-behaviour detectors. Steps are counted like WFC_SIMHASH's (one per finishRemainder).
void World::eyeBotAudit(float dt) {
    static const bool on = std::getenv("WFC_EYEBOTAUDIT") != nullptr;
    static long step = 0; ++step;
    if (!on || !matchActive_ || match_.state() != Match::State::InProgress) return;
    struct S { bool have = false; core::Vec3 pos{}; float yaw = 0.0f; float push = 0.0f, veh = 0.0f, air = 0.0f, spin = 0.0f; bool pushLog = false, vehLog = false, airLog = false, spinLog = false;
               core::Vec3 pushAt{}; float lastSnapT = -1e9f; int snaps = 0; bool spawned = false; };
    static std::map<int, S> st;
    static std::map<long, float> overlapT; static std::map<long, bool> overlapLog;
    static std::map<int, float> sentryIdle; static std::map<int, bool> sentryLog;
    static size_t killsSeen = 0;
    const float now = match_.matchTime();
    const CollisionWorld* col = collision_.valid() ? &collision_ : nullptr;
    auto dir = [](float yaw) { return core::forwardFromYawPitch(yaw, 0.0f); };
    // Deaths: every kill despawns the victim's pawn the same step (no body). Logged per kill (killHistory).
    const auto& kh = match_.killHistory();
    for (; killsSeen < kh.size(); ++killsSeen)
        LOG_INFO("EYEBOT death step %ld t %.2f p%d killed by p%d (%s): pawn removed this step, no death body", step, now, kh[killsSeen].victim, kh[killsSeen].killer, kh[killsSeen].damageType.c_str());
    std::vector<std::pair<int, const Character*>> pawns;
    for (const MatchOpponent* o : opponents_) {
        S& s = st[o->matchPlayer()];
        if (!o->spawned()) { s.have = false; s.spawned = false; continue; }
        pawns.push_back({o->matchPlayer(), &o->pawn()});
        const Character& c = o->pawn();
        const MoveIntent& in = o->intent();
        const core::Vec3 p = c.position();
        core::Vec3 v = c.velocity(); v.y = 0.0f;
        const float spd = core::length(v);
        const bool veh = c.moveForm() == Form::Vehicle;
        const bool jet = veh && c.vehicleParams().form == VehicleFormType::Jet;
        if (s.have) {
            const core::Vec3 dp = p - s.pos;
            if (std::hypot(dp.x, dp.z) > 3.0f && !c.isTransforming())
                LOG_INFO("EYEBOT pop step %ld t %.2f p%d jumped %.1f m in one step from (%.1f %.1f %.1f) to (%.1f %.1f %.1f) form %s", step, now, o->matchPlayer(),
                         std::hypot(dp.x, dp.z), s.pos.x, s.pos.y, s.pos.z, p.x, p.y, p.z, formName(c.moveForm()));
            const float dy = std::fabs(std::remainder(c.yaw() - s.yaw, 6.2831853f));
            if (dy > 25.0f * 0.0174533f && !c.isTransforming()) {
                ++s.snaps;
                if (now - s.lastSnapT > 1.0f) {
                    const BotBrain* b = botBrain(o->matchPlayer());
                    LOG_INFO("EYEBOT aimsnap step %ld t %.2f p%d yaw jumped %.0f deg in one step (%s, target %d visible %d, speed %.1f) at (%.1f %.1f %.1f)", step, now, o->matchPlayer(),
                             dy * 57.2958f, formName(c.moveForm()), b ? b->target : -1, b && b->target >= 0 && b->seen.count(b->target) ? (int)b->seen.at(b->target).visible : 0, spd, p.x, p.y, p.z);
                    s.lastSnapT = now;
                }
            }
            // Spinning: > 270 deg/s sustained 1 s.
            if (dy / std::max(dt, 1e-4f) > 270.0f * 0.0174533f) { s.spin += dt; if (s.spin >= 1.0f && !s.spinLog) { s.spinLog = true; LOG_INFO("EYEBOT spin step %ld t %.2f p%d turning > 270 deg/s for 1 s at (%.1f %.1f %.1f) form %s", step, now, o->matchPlayer(), p.x, p.y, p.z, formName(c.moveForm())); } }
            else { s.spin = 0.0f; s.spinLog = false; }
        }
        // Wall push: robot on the ground, stick over 0.5, under 1 m/s, a wall 1.2 m along the stick direction (knee or chest).
        const float stick = std::hypot(in.moveForward, in.moveRight);
        core::Vec3 wd{0, 0, 0};
        if (stick > 1e-3f) { const core::Vec3 F = dir(in.faceYaw), R = core::normalize(core::cross(F, core::Vec3{0, 1, 0})); wd = core::normalize(F * in.moveForward + R * in.moveRight); }
        bool wall = false;
        if (!veh && c.onGround() && stick > 0.5f && spd < 1.0f && col) {
            float tt;
            for (float hgt : {0.5f, 1.2f}) { const core::Vec3 a = p + core::Vec3{0, hgt, 0}; if (col->segmentHit(a, a + wd * (c.cylinderRadius(Form::Robot) + 1.2f), tt)) { wall = true; break; } }
        }
        if (wall) { if (s.push == 0.0f) s.pushAt = p; s.push += dt; if (s.push >= 1.0f && !s.pushLog) { s.pushLog = true; LOG_INFO("EYEBOT wallpush step %ld t %.2f p%d pushing into a wall for 1 s at (%.1f %.1f %.1f)", step, now, o->matchPlayer(), p.x, p.y, p.z); } }
        else { if (s.pushLog) LOG_INFO("EYEBOT wallpush-end step %ld t %.2f p%d after %.1f s", step, now, o->matchPlayer(), s.push); s.push = 0.0f; s.pushLog = false; }
        // Stuck vehicle: hover / drive (not jet) with input, under 2 m/s for 2 s.
        if (veh && !jet && (std::fabs(in.moveForward) > 0.5f || std::fabs(in.steer) > 0.5f || in.wantBoost) && spd < 2.0f) {
            s.veh += dt;
            if (s.veh >= 2.0f && !s.vehLog) {
                s.vehLog = true;
                const auto& vs = c.vehicleState();
                LOG_INFO("EYEBOT vehstuck step %ld t %.2f p%d vehicle (%s) under 2 m/s for 2 s with input at (%.1f %.1f %.1f): tilt %.0f deg, contacts %d, ground normal y %.2f, driving %d",
                         step, now, o->matchPlayer(), c.chassis().id.c_str(), p.x, p.y, p.z, std::max(std::fabs(vs.pitch), std::fabs(vs.roll)) * 57.2958f, vs.contacts,
                         vs.contacts ? core::normalize(vs.contactN).y : 0.0f, (int)vs.driving);
            }
        } else { if (s.vehLog) LOG_INFO("EYEBOT vehstuck-end step %ld t %.2f p%d after %.1f s", step, now, o->matchPlayer(), s.veh); s.veh = 0.0f; s.vehLog = false; }
        // Airborne robot over 4 s (stuck on a ledge / falling).
        if (!veh && !c.onGround()) { s.air += dt; if (s.air >= 4.0f && !s.airLog) { s.airLog = true; LOG_INFO("EYEBOT air step %ld t %.2f p%d robot off the ground for 4 s at (%.1f %.1f %.1f) vy %.1f", step, now, o->matchPlayer(), p.x, p.y, p.z, c.velocity().y); } }
        else { s.air = 0.0f; s.airLog = false; }
        s.have = true; s.pos = p; s.yaw = c.yaw();
    }
    if (!localPlayerDead()) pawns.push_back({localPlayer_, &player_.pawn()});
    // Overlap: two pawns' cylinders more than half interpenetrating for 1 s.
    for (size_t i = 0; i < pawns.size(); ++i)
        for (size_t j = i + 1; j < pawns.size(); ++j) {
            const Character& a = *pawns[i].second; const Character& b = *pawns[j].second;
            const core::Vec3 d = a.position() - b.position();
            const long key = (long)pawns[i].first * 1000 + pawns[j].first;
            const float rr = a.cylinderRadius(a.moveForm()) + b.cylinderRadius(b.moveForm());
            if (std::hypot(d.x, d.z) < 0.5f * rr && std::fabs(d.y) < 2.0f) {
                if ((overlapT[key] += dt) >= 1.0f && !overlapLog[key]) { overlapLog[key] = true; LOG_INFO("EYEBOT overlap step %ld t %.2f p%d and p%d inside each other for 1 s at (%.1f %.1f %.1f)", step, now, pawns[i].first, pawns[j].first, a.position().x, a.position().y, a.position().z); }
            } else { overlapT[key] = 0.0f; overlapLog[key] = false; }
        }
    // Idle sentry: alive, an enemy within 30 m in plain sight (eye line), no target for 3 s.
    for (size_t si = 0; si < sentries_.size(); ++si) {
        const Sentry& se = sentries_[si];
        const int key = (int)si;
        bool enemyNear = false;
        if (se.alive)
            for (const auto& pp : pawns) {
                if (pp.first == se.owner || (match_.settings().teamGame && match_.sameTeam(pp.first, se.owner))) continue;
                const core::Vec3 d = pp.second->actorLocation() - (se.pos + core::Vec3{0, 1.5f, 0});
                float tt;
                if (core::length(d) < 30.0f && !(col && col->segmentHit(se.pos + core::Vec3{0, 1.5f, 0}, pp.second->actorLocation(), tt))) { enemyNear = true; break; }
            }
        if (se.alive && enemyNear && se.target < 0) {
            if ((sentryIdle[key] += dt) >= 3.0f && !sentryLog[key]) { sentryLog[key] = true; LOG_INFO("EYEBOT sentryidle step %ld t %.2f sentry of p%d at (%.1f %.1f %.1f): an enemy within 30 m in sight, no target for 3 s", step, now, se.owner, se.pos.x, se.pos.y, se.pos.z); }
        } else { sentryIdle[key] = 0.0f; sentryLog[key] = false; }
    }
}

void World::tickVehicleAudio(float dt, const VehicleFormSignals& s) {`);
console.log('ok');
