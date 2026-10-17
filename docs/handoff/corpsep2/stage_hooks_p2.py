"""DIAGNOSTIC ONLY (never in a patch; applied identically to the base and patch trees, after corpsegpu/stage_hooks2.py):
- WFC_CORPSESTAGE: a sixth staged kill, k 5: a robot with Engine.DmgType_Fell (KillZ: ShouldInstaGib).
- WFC_DEATHCAMSTAGE=robot|vehicle: once InProgress + 3 s, bots frozen (no god mode); 'vehicle' puts the local pawn in vehicle form;
  at + 6 s the nearest enemy kills the local player (AssaultRifle). No spectator view, so the death camera shows. Log lines
  'DEATHCAMSTAGE: ...'.
argv[1] = tree root."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)
rep('"TransGame.TnDamageTypeAssaultRifle", "TransGame.TnDamageTypeTankShell"};',
    '"TransGame.TnDamageTypeAssaultRifle", "TransGame.TnDamageTypeTankShell", "Engine.DmgType_Fell"};')
rep('                if (k < 5 && t >= 5.0f + 5.0f * (float)k) {', '                if (k < 6 && t >= 5.0f + 5.0f * (float)k) {')
rep("""        if (std::getenv("WFC_CORPSESTAGE") && match_.state() == Match::State::InProgress && !localDead_) {   // DIAGNOSTIC (corpse crops)""",
    """        if (const char* dc = std::getenv("WFC_DEATHCAMSTAGE"); dc && match_.state() == Match::State::InProgress) {   // DIAGNOSTIC (death camera)
            static float t = 0.0f; static int phase = 0;
            t += dt;
            Character& pc = player_.pawn();
            if (phase == 0 && t >= 3.0f) {
                phase = 1; qaBotsFrozen_ = true;
                if (!std::strcmp(dc, "vehicle")) {
                    const float above = pc.meshToActor(Form::Robot) - pc.meshToActor(Form::Vehicle);
                    pc.setForm(Form::Vehicle); pc.setPosition(pc.position() + core::Vec3{0, above, 0});
                }
                LOG_INFO("DEATHCAMSTAGE: %s at %.1f %.1f %.1f yaw %.2f", dc, pc.position().x, pc.position().y, pc.position().z, pc.yaw());
            }
            if (phase == 1 && t >= 6.0f && !localDead_) {
                phase = 2;
                MatchOpponent* best = nullptr; float bd = 1e9f;
                for (MatchOpponent* o : opponents_) {
                    if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;
                    const float d = core::length(o->position() - pc.position());
                    if (d < bd) { bd = d; best = o; }
                }
                LOG_INFO("DEATHCAMSTAGE: local killed by p%d (%.1f m)", best ? best->matchPlayer() : -1, bd);
                applyMatchDamage(localPlayer_, best ? best->matchPlayer() : -1, 99999.0f, false, "TransGame.TnDamageTypeAssaultRifle");
            }
        }
        if (std::getenv("WFC_CORPSESTAGE") && match_.state() == Match::State::InProgress && !localDead_) {   // DIAGNOSTIC (corpse crops)""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
