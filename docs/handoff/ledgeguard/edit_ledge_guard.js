// Bot ground-vehicle ledge guard (both AIs, bot steering only): moving faster than 6 m/s, if the point the vehicle reaches in 0.3 / 0.6 s
// is off the nav mesh and has no floor within 2.5 m below the floor under the vehicle (a drop), the bot takes the stick off for that step
// (no boost / nitro, the stick braking against the velocity and back toward the corridor) - unless the corridor goes there (a jump / drop
// link next, or the next waypoint itself below the drop). Streets: bot vehicles carried a corner off a ledge at (123.3, -705.9, -439.1)
// into a dead-end trench. [PC ADAPTATION: the original's vehicle AI navigation is not recovered; a conservative rebuild rule.] NAVQ
// reports the guard's episodes / steps. Run from the tree root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/WorldBots.cpp', `    if (vehicle) in.steer = botDriveSteer(pc.yaw(), in.faceYaw);`,
`    // Ledge guard (bot ground vehicles): a drop ahead that the corridor does not take -> no boost, brake against the velocity and steer
    // back toward the corridor for this step. [PC ADAPTATION: the original's vehicle AI navigation is not recovered; conservative rule.]
    {
        bool guard = false;
        if (vehicle && !jetForm && collision() && botNav_.valid()) {
            core::Vec3 hv{pc.velocity().x, 0.0f, pc.velocity().z};
            const float sp = core::length(hv);
            float gy0; core::Vec3 n0;
            if (sp > 6.0f && collision()->groundHeight(pos.x, pos.z, pos.y, 0.5f, gy0, n0)) {
                const bool linkNext = b.wp < b.path.size() && b.path[b.wp].action != 0;
                const bool wpBelow = b.wp < b.path.size() && b.path[b.wp].pos.y < gy0 - 2.5f;
                if (!linkNext && !wpBelow)
                    for (float t : {0.3f, 0.6f}) {
                        const core::Vec3 q = pos + hv * t;
                        if (botNav_.findCell(q, 0.0f) >= 0) continue;
                        float gq; core::Vec3 nq;
                        const bool floorNear = collision()->groundHeight(q.x, q.z, gy0 + 1.0f, 0.0f, gq, nq) && gq >= gy0 - 2.5f;
                        if (!floorNear) { guard = true; break; }
                    }
            }
            if (guard) {
                const core::Vec3 vd = hv * (1.0f / sp);
                core::Vec3 tw = (b.wp < b.path.size() ? b.path[b.wp].pos : b.goal.pos) - pos; tw.y = 0.0f;
                const float tl = core::length(tw);
                core::Vec3 md = (tl > 1e-3f ? tw * (0.4f / tl) : core::Vec3{0, 0, 0}) - vd * 0.6f;
                const float ml2 = core::length(md);
                if (ml2 > 1e-3f) md = md * (1.0f / ml2);
                in.moveForward = core::dot(md, F); in.moveRight = core::dot(md, R);
                in.wantBoost = false; in.wantDash = false;
                ++ledgeGuardSteps_;
                if (!b.ledgeGuarding) ++ledgeGuardEpisodes_;
            }
        }
        b.ledgeGuarding = guard;
    }
    if (vehicle) in.steer = botDriveSteer(pc.yaw(), in.faceYaw);`);
ed('src/game/World.h', `    std::vector<float> botMsSamples_;   // per-step AI cost (ms) since resetBotTiming (BOTTEST p99)`,
`    std::vector<float> botMsSamples_;   // per-step AI cost (ms) since resetBotTiming (BOTTEST p99)
public:
    std::atomic<long> ledgeGuardSteps_{0}, ledgeGuardEpisodes_{0};   // bot vehicle ledge guard activations (NAVQ report; botSteer runs on workers)
private:`);
ed('src/game/BotBrain.h', `    core::Vec3 offMeshFrom{0, 0, 0}; float offMeshStall = 0.0f;   // off the mesh without 0.5 m of movement (s): forces a rejoin search`,
`    core::Vec3 offMeshFrom{0, 0, 0}; float offMeshStall = 0.0f;   // off the mesh without 0.5 m of movement (s): forces a rejoin search
    bool ledgeGuarding = false;                                   // the vehicle ledge guard acted last step (episode count)`);
ed('src/core/Application.cpp', `    if (farR[0] > 0)
        LOG_INFO("NAVQ squad far`, `    LOG_INFO("NAVQ LEDGEGUARD %s: %ld episodes, %ld steps (bot vehicle ledge guard)", world_.mapName().c_str(), (long)world_.ledgeGuardEpisodes_.load(),
             (long)world_.ledgeGuardSteps_.load());
    if (farR[0] > 0)
        LOG_INFO("NAVQ squad far`);
console.log('ok');
