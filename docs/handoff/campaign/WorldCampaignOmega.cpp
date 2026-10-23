// Omega Supreme, D5's boss (TnOmegaSupremePawn + OmegaSupreme_AICD) [C, RE CAMPAIGN_SETPIECES 1 / CAMPAIGN_BOSSES D5 /
// CAMPAIGN_OMEGA_HITREACT_RE 1]: stationary (no move actions: he turns, locks on and fires), the state machine Intro -> Healthy (4
// sentries) -> Wounded (battery lock-on + heal) -> Healthy again (a clean battery: the sentries heal) or Corrupted (vulnerable) ->
// Critical (<= 1 HP, invulnerable) -> the heart armor -> OmegaDeath, with TnSeqEvent_OmegaStatus 0-9 back to the Kismet.
// The arms: RocketArm (lock 2 s, a homing rocket every 1-5 s, 25 dmg), ShootingFrenzy (every 25 s: tell + lock 2 s, 15 homing rockets
// 0.25 s apart, 55 dmg r 15 m), RocketBarrage (every 15 s, OmegaStatus 5), AoE (Melee_AOE within 32 m), the tractor-beam grab (every 20
// s, 4 s warning, OmegaStatus 8, 1000 dmg at 5.3 s of Melee_ClawCrush_Omega). Targets: round robin over the living enemies, 7 s each.
// PROV: the sentries are 4 hit pools of 400 HP at fixed spots on his body (no TnSentryGroup export; a hit while Healthy goes to the
// sentry nearest the shooter), the heart armor is a 1000 HP pool hit while Critical, the AoE deals 50, the barrage is 8 rockets, the
// healing anims' lengths come from his clips (else 6 s), the grab's LOS from 40 m up his body.
#include "game/World.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {

namespace {
const char* const kStatus[] = {"Healthy", "Wounded", "Critical", "Dying", "GrabbedBattery", "ShootingRockets", "Corrupted",
                               "SentryDestroyed", "LockingGrabOnPlayer", "HeartArmorDestroyed"};
// The sentry spots (PROV, metres in his frame: x right, y up, z forward): shoulders and forearms.
const float kSentry[4][3] = {{-22.0f, 48.0f, 4.0f}, {22.0f, 48.0f, 4.0f}, {-34.0f, 28.0f, 10.0f}, {34.0f, 28.0f, 10.0f}};
}

void World::cpOmegaStatus(int k) {
    for (int op = 0; op < (int)cp_.vm.opCount(); ++op)
        if (cp_.vm.op(op).cls == "TnSeqEvent_OmegaStatus") cp_.vm.fireEvent(op, kStatus[k]);
    LOG_INFO("campaign: Omega status %d %s at t %.1f", k, kStatus[k], match_.matchTime());
}

float World::cpOmegaClip(const char* clip, float fallback) const {
    const Character* c = participantPawn(cp_.omega.p);
    const assets::SkinnedModel* m = c ? c->currentModel() : nullptr;
    const int ci = m ? m->clipByName(clip) : -1;
    return ci >= 0 ? m->clips[(size_t)ci].duration : fallback;
}

void World::cpOmegaPlay(const char* clip) {
    if (Character* c = participantPawnMutable(cp_.omega.p)) c->playAction(clip, false);
}

core::Vec3 World::cpOmegaSentryPos(int k) const {
    const CampaignOmega& O = cp_.omega;
    const float s = std::sin(O.yaw), c = std::cos(O.yaw);   // forward = (-sin, -cos); right = (cos, -sin)
    const float* o = kSentry[k];
    return O.anchor + core::Vec3{o[0] * c - o[2] * s, o[1], -o[0] * s - o[2] * c};
}

void World::cpOmegaEnterHealthy() {
    CampaignOmega& O = cp_.omega;
    O.stage = CampaignOmega::Healthy;
    for (float& h : O.sentryHp) h = 400.0f;   // Sentries.Heal(): vulnerable, healed, the kill count reset
    O.sentryKills = 0;
    cpOmegaStatus(0);
}

// Bind the factory-spawned Omega participant once it is alive: stationary, out of the bot brains, 5000 HP.
void World::cpOmegaBind() {
    CampaignOmega& O = cp_.omega;
    auto pool = cp_.bodyPools.find("CMP_OmegaSupreme");
    if (pool == cp_.bodyPools.end()) return;
    for (int p : pool->second) {
        if (!match_.players()[(size_t)p].alive) continue;
        Character* c = participantPawnMutable(p);
        if (!c) continue;
        O = CampaignOmega{};
        O.p = p; O.anchor = c->position(); O.yaw = c->yaw();
        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) { o->health().max = 5000.0f; o->health().current = 5000.0f; }
        O.stage = CampaignOmega::Intro;
        O.timer = cpOmegaClip("Omega_Supreme_Intro", 15.0f);
        cpOmegaPlay("Omega_Supreme_Intro");
        O.rocketT = 4.0f; O.frenzyT = 25.0f; O.barrageT = 15.0f; O.grabT = 20.0f; O.aoeT = 6.0f;
        LOG_INFO("campaign: Omega Supreme is p%d at (%.1f %.1f %.1f), intro %.1f s", p, O.anchor.x, O.anchor.y, O.anchor.z, O.timer);
        return;
    }
}

// TnSeqAct_SetOmegaStage (OMEGASTAGE_1 default: intro done + the sentries heal; OMEGASTAGE_2: intro done + Critical, the checkpoint
// resume), SeqAct_TnCorruptOmegaBattery (the battery's CorruptedSpikes piece takes its TnCorruption trigger; the battery corrupted).
bool World::cpOmegaActivate(KismetVM& vm, int op, int input) {
    (void)input;
    const KismetVM::Op& o = vm.op(op);
    if (o.cls == "TnSeqAct_SetOmegaStage") {
        if (cp_.omega.p < 0) cpOmegaBind();
        if (cp_.omega.p >= 0) {
            if (o.authored["Stage"].asString() == "OMEGASTAGE_2") { cp_.omega.stage = CampaignOmega::Critical; cp_.omega.heartHp = 1000.0f; cpOmegaStatus(2); }
            else cpOmegaEnterHealthy();
        } else cp_.omega.pendingStage = o.authored["Stage"].asString() == "OMEGASTAGE_2" ? 2 : 1;
        if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
        return true;
    }
    if (o.cls == "SeqAct_TnCorruptOmegaBattery") {
        for (int v : vm.varLink(op, "Target")) {
            const KismetVM::Var* var = vm.var(v);
            if (!var || var->obj.rfind("des:", 0) != 0) continue;
            const int di = std::atoi(var->obj.c_str() + 4);
            if (di < 0 || (size_t)di >= cp_.destructibles.size()) continue;
            cp_.omega.corrupt.push_back(di);
            CampaignDestructible& d = cp_.destructibles[(size_t)di];
            for (size_t k = 0; k < d.pieces.size(); ++k)
                if (const CampaignDestructible::State* s = d.pieces[k].state())
                    for (const auto& L : s->links) if (L.second.find("Corruption") != std::string::npos) { cpDesTransition(di, k, L.first); break; }
            LOG_INFO("campaign: Omega battery %s corrupted", d.name.c_str());
        }
        if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
        return true;
    }
    return false;
}

// TakeDamage on Omega: invulnerable outside Corrupted (Healthy: the hit goes to a sentry; Critical: to the heart armor); clamped at 1 HP
// (-> Critical). True = the damage is consumed here.
bool World::cpOmegaDamage(int victim, int instigator, float& amount, Health& h) {
    CampaignOmega& O = cp_.omega;
    if (victim != O.p || O.p < 0) return false;
    if (O.stage == CampaignOmega::Healthy) {
        const Character* ic = participantPawn(instigator);
        int k = -1; float bd = 1e30f;
        for (int s = 0; s < 4; ++s) {
            if (O.sentryHp[s] <= 0.0f) continue;
            const core::Vec3 sp = cpOmegaSentryPos(s);
            const float d = ic ? core::length(sp - ic->position()) : (float)s;
            if (d < bd) { bd = d; k = s; }
        }
        if (k >= 0 && (O.sentryHp[k] -= amount) <= 0.0f) {
            ++O.sentryKills;
            cpOmegaStatus(7);
            if (O.sentryKills >= 4) {   // the 4th kill: an emote, Wounded
                O.stage = CampaignOmega::Wounded;
                cpOmegaPlay("HitReactions_React_Stand_F");
                cpOmegaStatus(1);
            }
        }
        if (instigator >= 0) O.lastSentryHitter = instigator;   // SentryGroupDamage: draws his attention
        return true;
    }
    if (O.stage == CampaignOmega::Critical) {
        if (O.heartHp > 0.0f && (O.heartHp -= amount) <= 0.0f) {
            O.stage = CampaignOmega::Dying;   // OmegaDeath: needs Critical + the heart armor gone
            cpOmegaStatus(9);
            cpOmegaPlay("Death_Omega");
            O.timer = cpOmegaClip("Death_Omega", 5.0f);
        }
        return true;
    }
    if (O.stage != CampaignOmega::Corrupted) return true;
    if (h.current - amount <= 1.0f) {   // he can't die from damage: invulnerable at 1 HP -> Critical
        amount = std::max(0.0f, h.current - 1.0f);
        O.stage = CampaignOmega::Critical;
        O.heartHp = 1000.0f;
        cpOmegaPlay("HitReactions_React_Stand_F");
        cpOmegaStatus(2);
    }
    return false;
}

// LOS from his head (WeaponSocket_Primary height, PROV 40 m) to the victim's chest: the static world blocks it.
bool World::cpOmegaSees(int target) const {
    const Character* tc = participantPawn(target);
    if (!tc || !collision_.valid()) return tc != nullptr;
    const core::Vec3 from = cp_.omega.anchor + core::Vec3{0.0f, 40.0f, 0.0f}, to = tc->actorLocation();
    float t;
    return !collision_.segmentHit(from, to, t) || t > 0.98f;
}

void World::cpOmegaFire(int target, float dmg, float radiusM, const char* type, float spread) {
    const Character* tc = participantPawn(target);
    if (!tc) return;
    CampaignOmega& O = cp_.omega;
    const core::Vec3 from = O.anchor + core::Vec3{0.0f, 40.0f, 0.0f} + core::Vec3{-std::sin(O.yaw), 0.0f, -std::cos(O.yaw)} * 12.0f;
    core::Vec3 dir = tc->actorLocation() - from;
    const float L = core::length(dir);
    if (L < 1e-3f) return;
    dir = dir * (1.0f / L);
    if (spread > 0.0f) { dir.x += (O.rand01() * 2.0f - 1.0f) * spread; dir.y += (O.rand01() * 2.0f - 1.0f) * spread; }
    Weapon w;
    w.projSpeed = 70.0f; w.projDamage = dmg; w.projRadiusM = radiusM; w.rangeM = 300.0f; w.damageType = type;
    w.projHoming = true; w.homingForce = 40.0f; w.projMaxSpeed = 70.0f; w.lockRobots = true;
    spawnProjectile(from, dir * 70.0f, w, O.p);
    projectiles_.back().target = target;
}

void World::cpTickOmega(float dt) {
    CampaignOmega& O = cp_.omega;
    if (O.p < 0) {
        cpOmegaBind();
        if (O.p >= 0 && O.pendingStage) { if (O.pendingStage == 2) { O.stage = CampaignOmega::Critical; O.heartHp = 1000.0f; cpOmegaStatus(2); } else cpOmegaEnterHealthy(); }
        if (O.p < 0) return;
    }
    Character* c = participantPawnMutable(O.p);
    if (!c || match_.state() != Match::State::InProgress) return;
    if (core::length(c->position() - O.anchor) > 0.5f) { c->setPosition(O.anchor); c->velocity() = core::Vec3{0, 0, 0}; }   // stationary
    if (cp_.omegaHold) return;   // CAMPAIGNTEST: held (the squad / game-over checks run beside him)
    if (O.stage == CampaignOmega::Dead) return;
    if (O.stage == CampaignOmega::Dying) {
        if ((O.timer -= dt) <= 0.0f) { O.stage = CampaignOmega::Dead; cpOmegaStatus(3); }   // RobotDying -> 3 Dying
        return;
    }
    if (O.stage == CampaignOmega::Intro) {
        if ((O.timer -= dt) <= 0.0f) cpOmegaEnterHealthy();
        return;
    }
    // the target: round robin over the living enemies (+3 for 7 s), the last sentry attacker first
    if ((O.targetT -= dt) <= 0.0f || O.target < 0 || !match_.players()[(size_t)O.target].alive) {
        std::vector<int> en;
        for (size_t q = 0; q < match_.players().size(); ++q)
            if ((int)q != O.p && match_.players()[q].alive && !match_.sameTeam((int)q, O.p) && participantPawn((int)q)) en.push_back((int)q);
        O.target = -1;
        if (O.lastSentryHitter >= 0 && std::find(en.begin(), en.end(), O.lastSentryHitter) != en.end()) { O.target = O.lastSentryHitter; O.lastSentryHitter = -1; }
        else if (!en.empty()) O.target = en[(size_t)(O.robin++ % (int)en.size())];
        O.targetT = 7.0f;
    }
    const Character* tc = O.target >= 0 ? participantPawn(O.target) : nullptr;
    if (tc) { const core::Vec3 d = tc->position() - O.anchor; O.yaw = std::atan2(-d.x, -d.z); c->setYaw(O.yaw); }
    // Wounded: LockOnBattery (a FullMesh battery: 2 s) -> the grab (TnTractorBeam trigger, OmegaStatus 4) -> HealWithGrabbedObject
    if (O.stage == CampaignOmega::Wounded && O.battery < 0) {
        for (size_t i = 0; i < cp_.destructibles.size() && O.battery < 0; ++i) {
            CampaignDestructible& d = cp_.destructibles[i];
            if (d.gone || d.name.find("Battery") == std::string::npos) continue;
            for (auto& pc : d.pieces) {   // only a FullMesh battery can be taken
                const CampaignDestructible::State* st = pc.state();
                if (pc.name == "BaseBattery" && st && st->name == "FullMesh") { O.battery = (int)i; O.healT = 2.0f; O.healPhase = 0; }
            }
        }
    }
    if (O.battery >= 0 && (O.stage == CampaignOmega::Wounded || O.stage == CampaignOmega::Healing)) {
        if ((O.healT -= dt) <= 0.0f) {
            if (O.healPhase == 0) {   // grabbed
                CampaignDestructible& d = cp_.destructibles[(size_t)O.battery];
                for (size_t k = 0; k < d.pieces.size(); ++k)
                    if (const CampaignDestructible::State* s = d.pieces[k].state())
                        for (const auto& L : s->links) if (L.second.find("TractorBeam") != std::string::npos) { cpDesTransition(O.battery, k, L.first); break; }
                cpOmegaStatus(4);
                O.healCorrupt = std::find(O.corrupt.begin(), O.corrupt.end(), O.battery) != O.corrupt.end();
                const char* clip = O.healCorrupt ? "HitReactions_React_CorruptBattery_F" : "HitReactions_React_HealingBattery_F";
                cpOmegaPlay(clip);
                O.healT = cpOmegaClip(clip, 6.0f);
                O.healPhase = 1; O.stage = CampaignOmega::Healing;
                if (O.healCorrupt) { O.stage = CampaignOmega::Corrupted; cpOmegaStatus(6); O.healT = std::max(O.healT, 20.0f); }   // Omega.Corrupt(): vulnerable
            } else {   // the heal ends: a clean battery heals the sentries; a corrupted one ends the vulnerable window unless Critical
                ++O.heals;
                O.battery = -1;
                if (O.stage != CampaignOmega::Critical) cpOmegaEnterHealthy();
            }
        }
    }
    if (!tc || O.stage == CampaignOmega::Healing) return;
    // the arms
    if (O.grabVictim >= 0) {   // the tractor-beam grab: the 4 s warning, then Melee_ClawCrush_Omega; 1000 at 5.3 s
        if ((O.grabTimer -= dt) <= 0.0f) {
            if (O.grabPhase == 0) {   // the lock needs LOS at its end: cover breaks it
                if (!cpOmegaSees(O.grabVictim)) { LOG_INFO("campaign: Omega's grab on p%d broken (no LOS)", O.grabVictim); O.grabVictim = -1; return; }
                O.grabPhase = 1; O.grabTimer = 5.3f; cpOmegaPlay("Melee_ClawCrush_Omega");
            }
            else { applyMatchDamage(O.grabVictim, O.p, 1000.0f, false, "TnDamageTypeOmegaTractorBeamGrab"); O.grabVictim = -1; }
        }
        return;
    }
    if ((O.grabT -= dt) <= 0.0f && cpOmegaSees(O.target)) { O.grabT = 20.0f; O.grabVictim = O.target; O.grabPhase = 0; O.grabTimer = 4.0f; cpOmegaStatus(8); return; }
    if (O.frenzyLeft > 0) {
        if ((O.frenzyNext -= dt) <= 0.0f) { cpOmegaFire(O.target, 55.0f, 15.0f, "TnDamageTypeOmegaShootingFrenzyRocket", 0.08f); O.frenzyNext = 0.25f; --O.frenzyLeft; }
        return;
    }
    if ((O.frenzyT -= dt) <= 0.0f) { O.frenzyT = 25.0f; O.frenzyLeft = 15; O.frenzyNext = 2.0f; cpOmegaPlay("Melee_ShootingFrenzy"); return; }
    if ((O.barrageT -= dt) <= 0.0f) {
        O.barrageT = 15.0f;
        cpOmegaPlay("Melee_RocketBarrage");
        cpOmegaStatus(5);
        for (int k = 0; k < 8; ++k) cpOmegaFire(O.target, 40.0f, 0.4f, "TnDamageTypeOmegaRocket", 0.3f);
    }
    if ((O.aoeT -= dt) <= 0.0f) {
        O.aoeT = 6.0f;
        bool any = false;
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const Character* e = (int)q != O.p && match_.players()[q].alive && !match_.sameTeam((int)q, O.p) ? participantPawn((int)q) : nullptr;
            if (!e) continue;
            const core::Vec3 d = e->position() - O.anchor;
            if (std::sqrt(d.x * d.x + d.z * d.z) > 32.0f) continue;
            if (!any) { cpOmegaPlay("Melee_AOE"); any = true; }
            applyMatchDamage((int)q, O.p, 50.0f, true, "TnDamageTypeOmegaAOE");
        }
    }
    if ((O.rocketT -= dt) <= 0.0f) {   // RocketArm: Missile_Shoot every 1-5 s (after the 2 s lock of each new target)
        O.rocketT = 1.0f + O.rand01() * 4.0f;
        cpOmegaFire(O.target, 25.0f, 0.1f, "TnDamageTypeOmegaRocket", 0.1f);
    }
}

} // namespace game
