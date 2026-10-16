"""DIAGNOSTIC ONLY (never in a patch): WFC_MATSTAGE=1 stages the item-6 weapon / prop materials in front of the camera. Once the
match is InProgress the local pawn respawns as the Scientist preset (the sentry ability's owning class), then, alive and
2 s later, the bots freeze and the pawn places its sentry (requestSentry; spawned ahead of it) and stands still (god mode);
1 s after, three kamikaze mines (the Leader's grenade) are placed 5 m ahead.
Log lines 'MATSTAGE: ...'. argv[1] = tree root (src/game/World.cpp, after fx_.tick)."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);""".replace('\n', nl)
b = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
        if (std::getenv("WFC_MATSTAGE") && match_.state() == Match::State::InProgress) {   // DIAGNOSTIC (item-6 material crop)
            static int st = 0; static float t = 0.0f;
            t += dt;
            if (st == 0 && t >= 1.0f) {
                for (const CharacterSelection& c : qaCharacterChoicesAlways())
                    if (std::string(specialtyName(c.specialty)) == "Scientist") {
                        preloadSelections({c});
                        match_.selectCharacter(localPlayer_, c);
                        if (!localDead_) killLocalPlayer(localPlayer_, true);
                        st = 1; t = 0.0f;
                        LOG_INFO("MATSTAGE: local pawn respawns as the Scientist preset (%s)", c.chassisByFaction[0].c_str());
                        break;
                    }
            } else if (st == 1 && !localDead_ && player_.pawn().specialty() == "Scientist") {
                st = 2; t = 0.0f;
                LOG_INFO("MATSTAGE: Scientist alive (%s)", player_.pawn().chassis().id.c_str());
            } else if (st == 2 && t >= 2.0f) {
                qaBotsFrozen_ = true; qaGod_ = true;
                requestSentry(localPlayer_);
                st = 3;
                const core::Vec3 lp = player_.pawn().position();
                LOG_INFO("MATSTAGE: sentry requested at %.1f %.1f %.1f", lp.x, lp.y, lp.z);
            } else if (st == 3 && t >= 3.0f) {   // three kamikaze mines (the Leader's grenade) in a row ahead of the pawn
                const Character& pc = player_.pawn();
                const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
                for (int i = -1; i <= 1; ++i) {
                    KamikazeMine m; m.pos = pc.actorLocation() + f * 5.0f + r * (1.5f * (float)i); m.vel = {0, 0, 0};
                    m.audioKey = ++projAudioKey_;
                    mines_.push_back(m);
                }
                st = 4;
                LOG_INFO("MATSTAGE: 3 kamikaze mines placed");
            }
        }""".replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
if '#include <cstdlib>' not in s:
    s = s.replace('#include "game/World.h"', '#include "game/World.h"' + nl + '#include <cstdlib>', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
