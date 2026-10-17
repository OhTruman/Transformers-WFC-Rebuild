// Death-body patch 2, part 1: the DeathDecal_MAT scorch under a gibbed robot (TnPawn.Dying.Gib; RE pass 5 s12 addendum 23, the recipe
// in Renderer.h spawnDecal): a line trace 300 UU straight down from the pawn origin; skipped when nothing is hit or |N . down| < 0.5;
// then a square 800-950 UU, thickness 300 UU, random roll, 30 s. Robots only (vehicles and jets: no decal).
// Presentation only: the random size and roll come from a hash of the body's seed, so the body's own frand() sequence (the animation
// pick, the chunks, the hull) is unchanged and every existing frame stays identical up to the decal. Null render: no renderer_, no decal.
// Run from a tree root (af4d0731 or later).
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        c.chunks.push_back(k);\n    }\n    // TnSkeletalGibSpawner (vehicles):';
if (s.split(a).length !== 2) throw new Error('anchor');
const add = [
    '        c.chunks.push_back(k);',
    '    }',
    '    // The death scorch (robots): DeathDecal_MAT on the floor below the pawn origin (actorLocation: the capsule centre, as UE3).',
    '    if (!c.vehicle && renderer_ && collision_.valid()) {',
    '        const float y0 = c.pos.y;',
    '        float gy; core::Vec3 n;',
    '        if (collision_.groundHeight(c.pos.x, c.pos.z, y0, 0.0f, gy, n) && gy >= y0 - 3.0f && std::fabs(n.y) >= 0.5f) {',
    '            unsigned h = c.rng ^ 0x5bd1e995u;',
    '            auto hrand = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (float)(h >> 8) / 16777216.0f; };',
    '            const float sz = (800.0f + 150.0f * hrand()) * 0.01f;',
    '            renderer_->spawnDecal("FX_Decals_p.DeathDecal_MAT", core::Vec3{c.pos.x, gy, c.pos.z}, core::Vec3{0, -1, 0}, sz, sz, 3.0f, 360.0f * hrand(), 30.0f);',
    '        }',
    '    }',
    '    // TnSkeletalGibSpawner (vehicles):'].join('\n');
s = s.replace(a, add);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
