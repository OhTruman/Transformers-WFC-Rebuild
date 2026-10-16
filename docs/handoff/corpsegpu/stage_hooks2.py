"""DIAGNOSTIC ONLY (never in a patch; applied identically to base and new trees so it cancels out of the diff):
WFC_EYECAM=<player>: a chase camera on that participant every step (12 m behind, 5 m up, looking at it): eye-pass crops of bot behaviour.
WFC_CORPSESTAGE=1: once InProgress + 3 s, bots frozen, god mode; a fixed camera on a spot 12 m ahead of the local pawn; every 5 s one enemy is
placed on the spot and killed with the next damage type (Melee, TankShell, SniperRifle, AssaultRifle; then the first enemy in vehicle form
with TankShell). Log lines 'CORPSESTAGE: ...'. argv[1] = tree root (src/game/World.cpp, after fx_.tick)."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);""".replace('\n', nl)
b = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
        if (const char* ec = std::getenv("WFC_EYECAM")) {   // DIAGNOSTIC (eye-pass crops): chase camera on one participant
            const int who = std::atoi(ec);
            if (const Character* t = participantPawn(who)) {
                const core::Vec3 f = core::forwardFromYawPitch(t->yaw(), 0.0f);
                const core::Vec3 eye = t->position() - f * 12.0f + core::Vec3{0, 5.0f, 0};
                player_.controller().setSpectatorView(eye, t->yaw(), -0.35f, 0.0f);
            }
        }
        if (std::getenv("WFC_CORPSESTAGE") && match_.state() == Match::State::InProgress && !localDead_) {   // DIAGNOSTIC (corpse crops)
            static float t = 0.0f; static int k = 0; static core::Vec3 spot{}; static float yaw0 = 0.0f; static bool set = false;
            t += dt;
            if (!set && t >= 3.0f) {
                set = true; qaBotsFrozen_ = true; qaGod_ = true; yaw0 = player_.pawn().yaw();
                spot = player_.pawn().position() + core::forwardFromYawPitch(yaw0, 0.0f) * 12.0f;
                LOG_INFO("CORPSESTAGE: spot %.1f %.1f %.1f", spot.x, spot.y, spot.z);
            }
            if (set) {
                const core::Vec3 f = core::forwardFromYawPitch(yaw0, 0.0f);
                static const float camD = std::getenv("WFC_CORPSESTAGE_DIST") ? (float)std::atof(std::getenv("WFC_CORPSESTAGE_DIST")) : 9.0f;
                player_.controller().setSpectatorView(spot - f * camD + core::Vec3{0, 4.0f, 0}, yaw0, -0.3f * 9.0f / camD, 0.0f);
                static const char* kinds[] = {"TransGame.TnDamageTypeMelee", "TransGame.TnDamageTypeTankShell", "TransGame.TnDamageTypeSniperRifle",
                                              "TransGame.TnDamageTypeAssaultRifle", "TransGame.TnDamageTypeTankShell"};
                if (k < 5 && t >= 5.0f + 5.0f * (float)k) {
                    for (MatchOpponent* o : opponents_) {
                        if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                        if ((k == 4) != (o->pawn().moveForm() == Form::Vehicle)) continue;
                        o->setPosition(spot);
                        LOG_INFO("CORPSESTAGE: kill %d p%d (%s, %s) with %s", k, o->matchPlayer(), o->pawn().chassis().id.c_str(),
                                 o->pawn().moveForm() == Form::Vehicle ? "vehicle" : "robot", kinds[k]);
                        applyMatchDamage(o->matchPlayer(), localPlayer_, 99999.0f, false, kinds[k]);
                        break;
                    }
                    ++k;
                }
            }
        }""".replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
if '#include <cstdlib>' not in s:
    s = s.replace('#include "game/World.h"', '#include "game/World.h"' + nl + '#include <cstdlib>', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
