"""DIAGNOSTIC ONLY (never in a patch): WFC_FXSTAGE=1 stages the three user cases (2026-10-14) around a NON-local pawn:
2 s after the match is InProgress the bots freeze (QA freeze, direct), the local pawn goes 6 m from the nearest bot
facing it (god mode), then: 3-9 s tracer-smoke trails across the bot 1 m in front of and behind it; 9-15 s the local
WeaponFx muzzle flash 2 m in front of the bot (between camera and bot); 15 s+ the local pawn moves to 14 m and raises its barrier (spawned 10 m ahead) ~4 m in front of the bot. argv[1] = tree root."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);""".replace('\n', nl)
b = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
        if (std::getenv("WFC_FXSTAGE") && match_.state() == Match::State::InProgress) {   // DIAGNOSTIC (FX order A/B)
            static float t = 0.0f, acc = 0.0f;
            static int bot = -1;
            static core::Vec3 d{0, 0, -1}, bp{0, 0, 0};
            static bool barrier = false;
            t += dt; acc += dt;
            if (t >= 2.0f && bot < 0) {
                qaBotsFrozen_ = true; qaGod_ = true;
                float best = 1e30f;
                const core::Vec3 lp = player_.pawn().position();
                for (size_t i = 0; i < opponents_.size(); ++i) {
                    MatchOpponent* o = opponents_[i];
                    core::Vec3& v = o->pawn().velocity(); v.x = 0.0f; v.z = 0.0f;
                    if (!o->spawned()) continue;
                    const float dd = core::length(o->pawn().position() - lp);
                    if (dd < best) { best = dd; bot = (int)i; }
                }
                if (bot >= 0) {
                    bp = opponents_[(size_t)bot]->pawn().position();
                    core::Vec3 h = bp - lp; h.y = 0.0f;
                    d = core::length(h) > 0.1f ? core::normalize(h) : core::Vec3{0, 0, -1};
                    const float yaw = std::atan2(-d.x, -d.z);
                    player_.pawn().setPosition(bp - d * 6.0f);
                    player_.pawn().setYaw(yaw);
                    player_.controller().setCameraYaw(yaw);
                    if (renderer_) renderer_->notifyCameraCut("fxstage");
                    LOG_INFO("FXSTAGE: bot %d at %.1f %.1f %.1f, local pawn 6 m from it", bot, bp.x, bp.y, bp.z);
                }
            }
            if (bot >= 0) {
                const core::Vec3 up{0, 1, 0}, rt = core::normalize(core::cross(d, up));
                if (t >= 3.0f && t < 9.0f && acc >= 0.25f) {
                    acc = 0.0f;
                    for (float side : {-1.0f, 1.0f}) {
                        const core::Vec3 c = bp + d * side + core::Vec3{0, 1.4f, 0};
                        fx_.spawnTracer(c - rt * 4.0f, c + rt * 4.0f);
                    }
                } else if (t >= 9.0f && t < 15.0f && acc >= 0.1f) {
                    acc = 0.0f;
                    core::Mat4 m;
                    const core::Vec3 f = d * -1.0f, at = bp - d * 2.0f + core::Vec3{0, 1.5f, 0};
                    m.m[0] = f.x; m.m[1] = f.y; m.m[2] = f.z; m.m[4] = up.x; m.m[5] = up.y; m.m[6] = up.z;
                    m.m[8] = rt.x; m.m[9] = rt.y; m.m[10] = rt.z; m.m[12] = at.x; m.m[13] = at.y; m.m[14] = at.z;
                    fx_.spawnMuzzleFlash(m);
                } else if (t >= 15.0f && !barrier) {
                    barrier = true;
                    player_.pawn().setPosition(bp - d * 14.0f);   // (the barrier spawns 10 m ahead: 4 m in front of the bot)
                    if (renderer_) renderer_->notifyCameraCut(\"fxstage\");
                    requestBarrier(localPlayer_);
                    LOG_INFO("FXSTAGE: local barrier requested");
                }
            }
        }""".replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
if '#include <cstdlib>' not in s:
    s = s.replace('#include "game/World.h"', '#include "game/World.h"' + nl + '#include <cstdlib>', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
