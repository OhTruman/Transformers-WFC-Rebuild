"""DIAGNOSTIC ONLY (never in a patch): WFC_EXPSTAGE=<template> spawns that effect template in front of the local pawn every 2 s
from 3 s after InProgress (bots frozen, god mode), through World::fxStepSpawn(..., explosion = true) - the projectile /
mine explosion path - with forward = up and the mine code's up vector. Log 'EXPSTAGE: ...' per spawn with the returned id.
argv[1] = tree root (src/game/World.cpp, after fx_.tick)."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);""".replace('\n', nl)
b = """        fx_.tick(dt, have ? &ms : nullptr, collision_.valid() ? &collision_ : nullptr);
        if (const char* xs = std::getenv("WFC_EXPSTAGE"); xs && *xs && match_.state() == Match::State::InProgress) {   // DIAGNOSTIC
            static float t = 0.0f, next = 3.0f;
            t += dt;
            if (t >= 2.0f) { qaBotsFrozen_ = true; qaGod_ = true; }
            if (t >= next) {
                next += 2.0f;
                const Character& pc = player_.pawn();
                const core::Vec3 fw = core::forwardFromYawPitch(player_.controller().viewYaw(), 0.0f);
                const core::Vec3 at = pc.position() + fw * 8.0f + core::Vec3{0, 1.0f, 0};
                const core::Vec3 f{0, 1, 0}, u{0, 0, -1};
                const int id = fxStepSpawn(xs, at, f, u, 0, true);
                LOG_INFO("EXPSTAGE: %s spawned at %.1f %.1f %.1f (id %d, t %.2f s)", xs, at.x, at.y, at.z, id, t);
            }
        }""".replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
if '#include <cstdlib>' not in s:
    s = s.replace('#include "game/World.h"', '#include "game/World.h"' + nl + '#include <cstdlib>', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
