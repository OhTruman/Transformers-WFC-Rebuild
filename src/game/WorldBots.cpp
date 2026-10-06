// Clean-room reconstruction — offline multiplayer bots on the World (PC ADAPTATION; see BotBrain.h / BotNav.h).
// Bots are ordinary Match participants (MatchOpponent pawns): they move with CharacterMovement from a MoveIntent, fire their
// real Weapon state (clip, reserve, refire, reload, spread) through the shared damage / kill path, transform with the pawn's
// own transformation, and die / respawn / score through Match exactly like players.
#include "game/World.h"
#include "game/PlayerController.h"
#include "core/Log.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>

namespace game {

bool rayAabb(const core::Vec3& o, const core::Vec3& d, float len, const core::Vec3& bmin, const core::Vec3& bmax, float& tHit);   // World.cpp

namespace {
float wrapPi(float a) { return std::remainder(a, 6.2831853f); }
float yawOf(const core::Vec3& d) { return std::atan2(-d.x, -d.z); }   // forwardFromYawPitch convention
float pitchOf(const core::Vec3& d) { return std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)); }
float hdist(const core::Vec3& a, const core::Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
}

bool World::ensureBotNav() {
    if (botNavTried_) return botNav_.valid();
    botNavTried_ = true;
    const std::string path = mapDir() + "bot_nav.json";
    if (!botNav_.load(path)) LOG_WARN("bots: no nav data at %s (AssetTools bot_nav.py); bots will hold position and fight", path.c_str());
    return botNav_.valid();
}

const BotBrain* World::botBrain(int player) const {
    for (const BotBrain& b : bots_) if (b.player == player) return &b;
    return nullptr;
}

// TargetableLocation: the pawn centre [HIGH, as the Repair Ray picker].
static core::Vec3 targetable(const Character& c) { return c.actorLocation(); }

core::Vec3 World::botEye(const Character& c) const { return c.actorLocation() + core::Vec3{0, c.moveForm() == Form::Robot ? c.robotParams().eyeHeight : 1.0f, 0}; }

bool World::botLineOfSight(const core::Vec3& from, const core::Vec3& to) const {
    const CollisionWorld& lw = weaponCollision_.valid() ? weaponCollision_ : collision_;
    if (!lw.valid()) return true;
    float t; core::Vec3 n;
    return !lw.segmentHit(from, to, t, n);
}

// One instant-hit trace for a participant shooter: the same targets, falloff, damage rules and destructibles as the local
// player's trace (fireHitscanWith), with the shooter as instigator and the local pawn as a target.
void World::fireHitscanAs(int instigator, const Character& shooter, const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn) {
    const float range = w.rangeM;
    core::Vec3 dir = core::normalize(dirIn);
    {
        const core::Vec3 rt = core::normalize(core::cross(dir, core::Vec3{0, 1, 0}));
        const core::Vec3 u2 = core::normalize(core::cross(rt, dir));
        auto rf = [] { return (float)std::rand() / (float)RAND_MAX * 2.0f - 1.0f; };
        const float spread = shooter.effectiveSpread();
        dir = core::normalize(dir + rt * (rf() * spread) + u2 * (rf() * spread));
    }
    float bestDist = range;
    if (collision_.valid()) {
        float t; core::Vec3 n;
        const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_;
        if (lineWorld.segmentHit(origin, origin + dir * range, t, n)) bestDist = range * t;
    }
    float targetDist = bestDist;
    int hitPlayer = -1;
    for (MatchOpponent* o : opponents_) {
        if (o->matchPlayer() == instigator) continue;
        float th; if (o->rayHit(origin, dir, range, th) && th < targetDist) { targetDist = th; hitPlayer = o->matchPlayer(); }
    }
    if (matchActive_ && !localDead_ && instigator != localPlayer_) {
        float th; if (MatchOpponent::pawnRayHit(player_.pawn(), origin, dir, range, th) && th < targetDist) { targetDist = th; hitPlayer = localPlayer_; }
    }
    Destructible* hitDes = nullptr;
    for (Destructible* d : destructibles_) {
        float th;
        if (d->state() == 0 && rayAabb(origin, dir, range, d->boxMin(), d->boxMax(), th) && th < targetDist) { targetDist = th; hitDes = d; hitPlayer = -1; }
    }
    { float ts; if (sentryRayHit(origin, dir, range, ts) && ts < targetDist) { targetDist = ts; hitDes = nullptr; hitPlayer = -1; damageSentry(w.damageAt(ts), instigator, w.damageType ? w.damageType : ""); } }
    bool hitBarrier = false;
    { float th; if (barrierRayHit(origin, dir, range, th) && th <= targetDist + 0.05f) { targetDist = th; hitBarrier = true; hitDes = nullptr; hitPlayer = -1; } }
    const float dist = (hitDes || hitPlayer >= 0 || hitBarrier) ? targetDist : bestDist;
    if (hitBarrier) damageBarrier(w.damageAt(dist), w.damageType ? w.damageType : "");
    if (hitPlayer >= 0) {
        if (hitPlayer == localPlayer_) { ++damageTakenCount_; lastDamageFrom_ = origin; }
        applyMatchDamage(hitPlayer, instigator, w.damageAt(dist), false, w.damageType ? w.damageType : "");
        for (BotBrain& b : bots_) {
            if (b.player == hitPlayer) { b.lastDamageTime = match_.matchTime(); b.lastAttacker = instigator; }
            if (b.player == instigator) ++b.hits;
        }
    }
    if (hitDes) hitDes->applyDamage(*this, w.damageAt(dist));
    const core::Vec3 hitPoint = origin + dir * dist;
    participantShots_.push_back({instigator, w.def ? w.def->id : "", origin, hitPoint, dist < range - 0.01f, hitPlayer});
}

void World::botFire(MatchOpponent& o, BotBrain& b, Weapon& w, const core::Vec3& aimPoint) {
    Character& pc = o.pawn();
    pc.exposeSelf();
    w.onFired();
    pc.notifyFired();
    ++b.shots;
    const core::Vec3 eye = botEye(pc);
    const core::Vec3 d = core::normalize(aimPoint - eye);
    if (w.projectile()) {
        // Weapon.ProjectileFire at the muzzle; without a posed held weapon the eye + 1.5 m stands in (the local fallback).
        const core::Vec3 muzzle = eye + d * 1.5f;
        spawnProjectile(muzzle, d * w.projSpeed, w, o.matchPlayer());
        if (w.projHoming && b.target >= 0) projectiles_.back().target = b.target;   // AI fires homing weapons at its enemy
        participantShots_.push_back({o.matchPlayer(), w.def ? w.def->id : "", muzzle, aimPoint, false, -1});
    } else if (w.simulated() && !w.beam()) {
        for (int k = 0; k < std::max(1, w.shots); ++k) fireHitscanAs(o.matchPlayer(), pc, w, eye, d);
    }
}

void World::addBotBrain(int player, int difficulty) {
    BotBrain b;
    b.player = player; b.difficulty = difficulty;
    b.rng = 0x9e3779b9U * (unsigned)(player + 1);
    b.thinkTimer = 0.05f * (float)(bots_.size() % 5);   // staggered decisions
    bots_.push_back(b);
}

// The goal of a bot without a visible enemy: shared objective layer. TDM / DM: attack the most recent enemy sighting by the
// bot's team (callouts; PC ADAPTATION), else roam between the map's anchors (pickups and starts) on the nav mesh.
core::Vec3 World::botSnap(const core::Vec3& p) const {
    // An authored actor (objective, pickup, start) goes to its approach cell (AssetTools: two-way reachable floor near it).
    const int ap = botNav_.approachCellNear(p);
    if (ap >= 0) {
        const BotNav::Cell& cell = botNav_.cells()[(size_t)ap];
        const int under = botNav_.findCell(p, 0.0f);
        return under == ap ? core::Vec3{p.x, cell.centroid.y, p.z} : cell.centroid;
    }
    const int c = botNav_.findCell(p, 15.0f);
    if (c < 0) return p;
    const BotNav::Cell& cell = botNav_.cells()[(size_t)c];
    // The point itself when it is on that cell's floor, else the cell centre.
    return botNav_.findCell(p, 0.0f) == c ? core::Vec3{p.x, cell.centroid.y, p.z} : cell.centroid;
}

// Objective goals of the four objective modes on the shared goal layer. Roles: each bot is an objective player or a hunter by a
// fixed share per mode (PC ADAPTATION); carriers, defusers and returners are on a mission that combat does not interrupt.
bool World::botModeGoal(BotBrain& b, const Character& pc, BotGoal& g) {
    const MatchPlayer& me = match_.players()[(size_t)b.player];
    const int team = me.team;
    const core::Vec3 pos = pc.position();
    const unsigned role = (unsigned)b.player * 2654435761U >> 28;   // 0..15, fixed per participant
    auto at = [&](BotGoalKind k, const core::Vec3& p, float r, bool mission) { g.kind = k; g.pos = botSnap(p); g.radius = r; b.mission = mission; g.hasTouch = true; g.touch = p; return true; };
    const auto& objs = mapState_.objectives();
    switch (matchMode_) {
        case MatchMode::KOTH: {
            // Power Struggle: hold the active zone (most of the team), hunt with the rest.
            const int z = mapState_.activeKothZone();
            if (z < 0 || (size_t)z >= objs.size() || role >= 11) return false;
            const ObjectiveObject& o = objs[(size_t)z];
            return at(BotGoalKind::Hold, o.pos, 4.0f, o.contains(pos));
        }
        case MatchMode::DOM: {
            // Conquest: contest a node the enemy is taking, else capture the nearest node not held by us, else defend one.
            int best = -1; float bestD = 1e9f; BotGoalKind kind = BotGoalKind::Capture;
            for (size_t i = 0; i < objs.size(); ++i) {
                const ObjectiveObject& o = objs[i];
                if (!o.activeInMode || o.cls != "TnDominationPoint") continue;
                const float d = hdist(o.pos, pos) + (float)((role + i) % 4) * 15.0f;   // spread the team over nodes
                BotGoalKind k = o.defenderTeam == team ? (o.claimingTeam != 255 && o.claimingTeam != team ? BotGoalKind::Contest : BotGoalKind::Defend)
                                                       : BotGoalKind::Capture;
                const float pri = k == BotGoalKind::Contest ? -60.0f : (k == BotGoalKind::Capture ? 0.0f : 80.0f);
                if (d + pri < bestD) { bestD = d + pri; best = (int)i; kind = k; }
            }
            if (best < 0 || role >= 13) return false;
            return at(kind, objs[(size_t)best].pos, 3.0f, objs[(size_t)best].contains(pos));
        }
        case MatchMode::CTF:
        case MatchMode::EXT: {
            // The live carried objective: CTF authors one flag factory per team and only the defenders' is in play this round.
            const MapState::Carried* live = nullptr;
            for (const MapState::Carried& q : mapState_.carried()) if (q.holder >= 0 || q.dropped || q.active) { live = &q; break; }
            if (!live) return false;
            const MapState::Carried& c = *live;
            const bool ctf = matchMode_ == MatchMode::CTF;
            const int mine = mapState_.carriedBy(b.player);
            if (ctf) {
                const bool attacking = match_.attackingTeam() == team;
                if (attacking) {
                    if (mine >= 0) {       // carry the Code of Power to our active capture point
                        for (const ObjectiveObject& o : objs)
                            if (o.activeInMode && o.cls == "TnFlagCapturePoint" && o.state == ObjectiveObject::State::Active) return at(BotGoalKind::Capture, o.pos, 2.0f, true);
                        return false;
                    }
                    if (c.holder >= 0) return role < 8 ? at(BotGoalKind::Support, c.pos, 8.0f, false) : false;   // escort the carrier
                    {   // the last 40 m to the Code of Power are an errand: push through and fight on the move
                        const core::Vec3 fp = c.dropped ? c.pos : objs[(size_t)c.home].pos;
                        return at(BotGoalKind::Retrieve, fp, 1.5f, hdist(fp, pos) < 40.0f);
                    }
                }
                if (c.holder >= 0) { g.target = c.holder; return at(BotGoalKind::Attack, c.pos, 4.0f, false); }   // stop the carrier
                if (c.dropped) return at(BotGoalKind::Return, c.pos, 2.0f, hdist(c.pos, pos) < 6.0f);           // stand on it to return
                return role < 6 ? at(BotGoalKind::Defend, objs[(size_t)c.home].pos + core::Vec3{b.frange(-10, 10), 0, b.frange(-10, 10)}, 6.0f, false) : false;
            }
            // Countdown to Extinction: the bomb.
            const MapState::Planted& pl = mapState_.planted();
            if (pl.active && pl.point >= 0 && (size_t)pl.point < objs.size()) {
                if (pl.team == team) return at(BotGoalKind::Defend, objs[(size_t)pl.point].pos, 6.0f, false);     // guard our plant
                return at(BotGoalKind::Contest, objs[(size_t)pl.point].pos, 2.0f, true);                           // defuse theirs
            }
            if (mine >= 0) {       // carry the bomb to the nearest enemy plant point and plant it
                int best = -1; float bestD = 1e9f;
                for (size_t i = 0; i < objs.size(); ++i)
                    if (objs[i].activeInMode && objs[i].cls == "TnBombPlantPoint" && objs[i].defenderTeam != team && hdist(objs[i].pos, pos) < bestD) { bestD = hdist(objs[i].pos, pos); best = (int)i; }
                if (best >= 0) return at(BotGoalKind::Attack, objs[(size_t)best].pos, 2.0f, true);
                return false;
            }
            if (c.holder >= 0) {
                if (c.holderTeam == team) return role < 8 ? at(BotGoalKind::Support, c.pos, 8.0f, false) : false;
                g.target = c.holder; return at(BotGoalKind::Attack, c.pos, 4.0f, false);
            }
            if (c.active || c.dropped) return role < 12 ? at(BotGoalKind::Retrieve, c.dropped ? c.pos : objs[(size_t)c.home].pos, 1.5f, false) : false;
            return false;
        }
        default: return false;
    }
}

BotGoal World::botObjectiveGoal(BotBrain& b, const Character& pc) {
    BotGoal g;
    b.mission = false;
    if (match_.matchTime() >= b.objectiveBlockedUntil && botModeGoal(b, pc, g)) return g;
    b.mission = false;
    g = BotGoal{};
    const MatchPlayer& me = match_.players()[(size_t)b.player];
    float bestT = -1e9f;
    for (const BotBrain& m : bots_) {
        if (match_.settings().teamGame ? match_.players()[(size_t)m.player].team != me.team : m.player != b.player) continue;
        for (const auto& kv : m.seen) {
            if (kv.first == b.player || match_.sameTeam(kv.first, b.player)) continue;
            if (!match_.players()[(size_t)kv.first].alive) continue;
            if (kv.second.time > bestT && match_.matchTime() - kv.second.time < 10.0f) { bestT = kv.second.time; g.pos = kv.second.pos; g.target = kv.first; }
        }
    }
    if (g.target >= 0 && match_.matchTime() >= b.ignoreSightingsUntil) {
        // The sighting may be a jump apex / ledge off the mesh: aim for the nav cell under or near it.
        const int c = botNav_.findCell(g.pos, 15.0f);
        if (c >= 0) { g.pos = botNav_.cells()[(size_t)c].centroid; g.kind = BotGoalKind::Attack; g.radius = 6.0f; return g; }
    }
    g.target = -1;
    g.kind = BotGoalKind::Roam; g.radius = 4.0f;
    // Hunt (PC ADAPTATION, TDM / DM): most roams head for a random live enemy's approximate area (25 m fuzz), as players
    // drift toward the fight; the rest wander between the map's anchors.
    if (b.frand() < 0.7f) {
        std::vector<int> en;
        for (size_t i = 0; i < match_.players().size(); ++i)
            if ((int)i != b.player && match_.players()[i].alive && !(match_.settings().teamGame && match_.sameTeam((int)i, b.player)) && participantPawn((int)i))
                en.push_back((int)i);
        if (!en.empty()) {
            const core::Vec3 p = participantPawn(en[(size_t)(b.frand() * (float)en.size()) % en.size()])->position();
            g.pos = p + core::Vec3{b.frange(-25.0f, 25.0f), 0.0f, b.frange(-25.0f, 25.0f)};
            const int c = botNav_.findCell(g.pos, 15.0f);
            if (c >= 0) { g.pos = botNav_.cells()[(size_t)c].centroid; return g; }
        }
    }
    const auto& as = botNav_.anchors();
    if (!as.empty()) {
        for (int k = 0; k < 8; ++k) {
            const BotNav::Anchor& a = as[(size_t)((unsigned)(b.frand() * 1e6f) % as.size())];
            if (hdist(a.pos, pc.position()) > 15.0f) { g.pos = a.pos; return g; }
        }
    }
    BotNav::Agent ag; ag.radius = pc.cylinderRadius(Form::Robot);
    g.pos = botNav_.randomPoint(b.rng++, ag);
    return g;
}

void World::botThink(MatchOpponent& o, BotBrain& b) {
    Character& pc = o.pawn();
    const BotSkill& sk = botSkill(b.difficulty);
    const float now = match_.matchTime();
    const core::Vec3 eye = botEye(pc);
    const core::Vec3 fwd = core::forwardFromYawPitch(b.yaw, 0.0f);
    // Perception: enemies in range; line of sight for at most three (closest first), the current target always.
    struct Cand { int p; float d; const Character* c; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < match_.players().size(); ++i) {
        const int p = (int)i;
        if (p == b.player || !match_.players()[i].alive || (match_.settings().teamGame && match_.sameTeam(p, b.player))) continue;
        const Character* c = participantPawn(p);
        if (!c) continue;
        const float d = core::length(targetable(*c) - eye);
        if (d > sk.sightM) continue;
        cands.push_back({p, d, c});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& c) { return a.d < c.d; });
    int checks = 0;
    for (auto& kv : b.seen) kv.second.visible = false;
    for (const Cand& c : cands) {
        const bool current = c.p == b.target;
        if (!current && checks >= 3) continue;
        const core::Vec3 to = targetable(*c.c) - eye;
        const float cosAng = core::dot(core::normalize(core::Vec3{to.x, 0, to.z}), fwd);
        const bool inFov = cosAng >= std::cos(sk.fovDeg * 0.5f * 0.0174533f) || c.d < 6.0f ||
                           (b.lastAttacker == c.p && now - b.lastDamageTime < 1.5f);   // hit from behind: turn to the attacker
        if (!inFov && !current) continue;
        // TnBuffCloak: cloaked enemies are only noticed close up (PC ADAPTATION of the player-only cloak).
        if (c.c->cloakRemain_ > 0.0f && c.d > 8.0f) continue;
        ++checks;
        if (!botLineOfSight(eye, targetable(*c.c))) continue;
        BotBrain::Seen& s = b.seen[c.p];
        s.pos = c.c->position(); s.time = now; s.visible = true;
    }
    // Target choice: the closest visible enemy, preferring the current target and whoever is shooting us.
    int best = -1; float bestScore = 1e9f;
    for (const Cand& c : cands) {
        auto it = b.seen.find(c.p);
        if (it == b.seen.end() || !it->second.visible) continue;
        float score = c.d;
        if (c.p == b.target) score -= 8.0f;
        if (c.p == b.lastAttacker && now - b.lastDamageTime < 3.0f) score -= 10.0f;
        if (c.c->health().current < c.c->health().max * 0.35f) score -= 4.0f;
        if (mapState_.carriedBy(c.p) >= 0) score -= 15.0f;   // the enemy flag / bomb carrier first
        if (score < bestScore) { bestScore = score; best = c.p; }
    }
    if (best != b.target) {
        if (best >= 0) { b.reactionLeft = sk.reaction * b.frange(0.8f, 1.25f); b.targetVisibleFor = 0.0f; b.burstLeft = 0; b.burstPause = 0.0f; }
        if (best >= 0 || b.target < 0 || now - b.seen[b.target].time > sk.memory) b.target = best;
    }
    // Repair (Scientist with the Repair Ray, PC ADAPTATION): a wounded teammate (< 65 %) within 30 m in sight, no enemy closer
    // than 20 m.
    b.healTarget = -1;
    {
        int ray = -1;
        for (size_t i = 0; i < pc.inventory().size(); ++i) if (pc.inventory()[i].beam() && (pc.inventory()[i].ammo > 0 || pc.inventory()[i].reserve > 0)) ray = (int)i;
        bool enemyClose = false;
        for (const Cand& c : cands) if (c.d < 20.0f && b.seen.count(c.p) && b.seen[c.p].visible) enemyClose = true;
        if (ray >= 0 && !enemyClose && match_.settings().teamGame && pc.moveForm() == Form::Robot) {
            float bestH = 0.65f;
            for (size_t i = 0; i < match_.players().size(); ++i) {
                const int p = (int)i;
                if (p == b.player || !match_.players()[i].alive || !match_.sameTeam(p, b.player)) continue;
                const Character* c = participantPawn(p);
                if (!c || c->health().max <= 0.0f) continue;
                const float frac = c->health().current / c->health().max;
                if (frac >= bestH || core::length(c->position() - pc.position()) > 30.0f || !botLineOfSight(eye, targetable(*c))) continue;
                bestH = frac; b.healTarget = p;
            }
        }
    }
    // Goal: chase / hold against a target, else the objective layer.
    const bool visible = b.target >= 0 && b.seen[b.target].visible;
    BotGoal ng;
    // Objective modes re-evaluate every think (carriers, flags and zones move); an objective errand (carrying, defusing,
    // returning, standing on a point) keeps its goal through combat - the bot fights on the move.
    const bool objectiveMode = matchMode_ != MatchMode::TDM && matchMode_ != MatchMode::DM;
    BotGoal og; bool haveOg = false;
    if (objectiveMode) { og = botObjectiveGoal(b, pc); haveOg = og.kind != BotGoalKind::Roam; }
    if (haveOg && b.mission) ng = og;
    else if (b.healTarget >= 0 && participantPawn(b.healTarget)) { ng.kind = BotGoalKind::Support; ng.pos = participantPawn(b.healTarget)->position(); ng.radius = 6.0f; ng.target = b.healTarget; }
    else if (b.target >= 0) { ng.kind = BotGoalKind::Attack; ng.pos = b.seen[b.target].pos; ng.target = b.target; ng.radius = 4.0f; b.mission = false; }
    else if (haveOg) ng = og;
    else if (!b.hasGoal || b.goal.kind == BotGoalKind::Attack || (b.goal.kind == BotGoalKind::Support && b.healTarget < 0) ||
             hdist(b.goal.pos, pc.position()) < b.goal.radius || now - b.goalTime > 40.0f)
        ng = botObjectiveGoal(b, pc);
    else ng = b.goal;
    if (!b.hasGoal || hdist(ng.pos, b.goal.pos) > 4.0f || ng.kind != b.goal.kind) { b.wantRepath = true; b.goalTime = now; }
    b.goal = ng; b.hasGoal = true;
    // Purposeful transform (cars, trucks, tanks; jets stay robots: flight steering is not modelled for bots [PARTIAL]): travel
    // to a far goal in vehicle form, fight in robot form.
    const bool canVehicle = pc.vehicleParams().form != VehicleFormType::Jet;
    const float goalDist = hdist(b.goal.pos, pc.position());
    // Vehicle form only where the nav's vehicle layer allows it (clearance 4.4 m / headroom 4 m) and not shortly after a failed
    // vehicle route; an existing vehicle stays one until combat or arrival.
    const int cell = botNav_.findCell(pc.position());
    const bool vehicleRoom = cell >= 0 && botNav_.cells()[(size_t)cell].vehicle && now >= b.noVehicleUntil;
    const bool travel = !visible && (b.goal.kind != BotGoalKind::Attack ? goalDist > 45.0f : goalDist > 60.0f);
    b.wantVehicle = canVehicle && travel && (pc.moveForm() == Form::Vehicle ? now >= b.noVehicleUntil : vehicleRoom);
    if (mapState_.carriedBy(b.player) >= 0) b.wantVehicle = false;   // the flag / bomb is held as the (WT_Heavy) weapon: robot form only
    // Weapon choice: the inventory weapon whose DesiredFiringRange band is nearest the target's band (switch held 0.5 s, as the
    // AI weapon picker's close / far switch delay); an empty weapon with no reserve is swapped out.
    if (pc.moveForm() == Form::Robot && !pc.isTransforming() && !pc.switchingWeapon()) {
        const auto& inv = pc.inventory();
        int want = pc.activeWeaponIndex();
        if (b.healTarget >= 0) {
            for (size_t i = 0; i < inv.size(); ++i) if (inv[i].beam()) want = (int)i;
        } else if (visible) {
            const AiRange band = aiRangeBand(core::length(targetable(*participantPawn(b.target)) - eye));
            int bestW = -1, bestD = 99;
            for (size_t i = 0; i < inv.size(); ++i) {
                const Weapon& w = inv[i];
                if (!w.simulated() || w.beam() || (w.ammo == 0 && w.reserve == 0)) continue;
                const int d = std::abs((int)aiWeaponData(w.def ? w.def->id : "", w.magSize).desired - (int)band);
                if (d < bestD || (d == bestD && (int)i == pc.activeWeaponIndex())) { bestD = d; bestW = (int)i; }
            }
            if (bestW >= 0) want = bestW;
        } else {
            const Weapon& cur = pc.weapon();
            if (!cur.simulated() || cur.beam() || (cur.ammo == 0 && cur.reserve == 0))
                for (size_t i = 0; i < inv.size(); ++i) if (inv[i].simulated() && !inv[i].beam() && (inv[i].ammo > 0 || inv[i].reserve > 0)) { want = (int)i; break; }
        }
        if (want != pc.activeWeaponIndex()) {
            b.switchHold += 0.25f;
            if (b.switchHold >= 0.5f) {
                const int n = (int)inv.size();
                const int dir = ((want - pc.activeWeaponIndex()) % n + n) % n <= n / 2 ? 1 : -1;
                pc.requestWeaponSwitch(dir); ++b.switches; b.switchHold = 0.0f;
            }
        } else b.switchHold = 0.0f;
        // Grenade toss (TnGrenadeThrower through the shared release): at a visible enemy 8-30 m away now and then, from the bag's
        // reserve, at most every 6 s (the bag's FireInterval if longer). Heal grenades are not thrown at enemies [PARTIAL: bots do
        // not heal]. Decision rate by skill (PC ADAPTATION).
        if (visible && b.grenadeCooldown <= 0.0f && b.grenadeDelay < 0.0f && !pc.isMeleeing() && pc.carryingHeavy_ == 0) {
            const Character* tp = participantPawn(b.target);
            Weapon* bag = nullptr;
            for (Weapon& w : pc.inventoryMutable()) if (w.grenade()) { bag = &w; break; }
            const float d = tp ? core::length(tp->position() - pc.position()) : 0.0f;
            static const float kChance[3] = {0.05f, 0.09f, 0.14f};
            if (tp && bag && bag->reserve > 0 && std::string(bag->def->id) != "HealGrenades" && d >= 8.0f && d <= 30.0f &&
                b.frand() < kChance[std::clamp(b.difficulty, 0, 2)]) {
                bag->reserve -= 1;
                b.grenadeCooldown = std::max(6.0f, bag->fireInterval);
                b.grenadeDelay = 0.4f;
                b.grenadeTarget = tp->position() + tp->velocity() * 1.0f;
                pc.playAction("GrenadeThrow", true);
                pc.exposeSelf();
                ++b.grenades;
            }
        }
        // Abilities (PC ADAPTATION: when to use them; the effects are the original ones):
        //  Dodge - just hit while fighting; Cloaking - closing on a far enemy unseen, or escaping at low health;
        //  Hover - an enemy at Medium / Far range (TnBuffIncreaseDamageDuringHover); Whirlwind - an enemy within Striking range.
        {
            const Character* tp = visible ? participantPawn(b.target) : nullptr;
            const float d = tp ? core::length(tp->position() - pc.position()) : 1e9f;
            const float hpFrac = pc.health().max > 0.0f ? pc.health().current / pc.health().max : 1.0f;
            static const float kUse[3] = {0.25f, 0.45f, 0.7f};
            const float use = kUse[std::clamp(b.difficulty, 0, 2)];
            if (visible && now - b.lastDamageTime < 0.5f && b.frand() < use) botTryAbility(o, b, "Dodge");
            if (((b.goal.kind == BotGoalKind::Attack && !visible && hdist(b.goal.pos, pc.position()) > 25.0f) || (visible && hpFrac < 0.3f)) && b.frand() < use * 0.5f)
                botTryAbility(o, b, "Cloaking");
            if (visible && d > 15.0f && d < 50.0f && pc.onGround() && b.frand() < use * 0.3f) botTryAbility(o, b, "Hover");
            if (visible && d <= aiRangeMaxM(AiRange::Striking) && b.frand() < use) botTryAbility(o, b, "Whirlwind");
        }
        // Melee rush (PC ADAPTATION): an enemy within 20 m (the melee-assist pick range), now and then by skill or when out of ammo
        // (melee cannot start mid-reload).
        if (visible && std::getenv("WFC_BOTDIST")) if (const Character* tp = participantPawn(b.target)) { static int h[8] = {}; static int n = 0; h[(int)aiRangeBand(core::length(tp->position() - pc.position()))]++; if (++n % 400 == 0) LOG_INFO("BOTDIST bands touch %d striking %d close %d medium %d far %d retreated %d out %d", h[1], h[2], h[3], h[4], h[5], h[6], h[7]); }
        if (visible && now >= b.rushUntil && b.meleeCooldown <= 0.0f && pc.carryingHeavy_ == 0)
            if (const Character* tp = participantPawn(b.target)) {
                const float d = core::length(tp->position() - pc.position());
                static const float kRush[3] = {0.08f, 0.15f, 0.22f};
                if (d <= 20.0f && !pc.weapon().reloading() && ((pc.weapon().ammo == 0 && pc.weapon().reserve == 0) || b.frand() < kRush[std::clamp(b.difficulty, 0, 2)]))
                    { b.rushUntil = now + 3.0f; ++b.rushes; }
            }
        // Opportunistic reload out of combat.
        Weapon& cw = pc.weapon();
        if (!visible && cw.ammo < cw.magSize / 2 && cw.canReload()) { cw.beginReload(); ++b.reloads; }
    }
}

// TnAbilityManager.TriggerAbility for a bot (the PlayerController rules): robot form, not transforming / reloading / dodging,
// not ability-jammed, past the slot's spam guard and cooldown. Pawn-level abilities only: Dodge, Cloaking, Hover (intent / pawn
// state) and Whirlwind (the shared melee path). Warcry / Barrier / Shockwave / SpawnSentry / ... run in World's local-player effect
// code and are not used by bots yet [PARTIAL].
bool World::botTryAbility(MatchOpponent& o, BotBrain& b, const char* id) {
    Character& pc = o.pawn();
    if (pc.moveForm() != Form::Robot || pc.isTransforming() || pc.weapon().reloading() || pc.isDodging() || pc.jammedRemain_ > 0.0f) return false;
    for (Character::AbilitySlot& a : pc.abilities_) {
        if (a.id != id || !a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) continue;
        if (a.id == "Whirlwind") {
            if (pc.isMeleeing()) return false;
            startMeleeFor(pc, o.matchPlayer(), true, b.yaw);
            if (!pc.isMeleeing()) return false;
        } else if (a.id == "Dodge") b.pendingDodge = b.frand() < 0.5f ? 1 : 2;
        else if (a.id == "Cloaking") pc.cloakRemain_ = 20.0f;                     // AddBuff(TnBuffCloak)
        else if (a.id == "Hover") { b.pendingHover = true; pc.hoverRequested_ = true; }
        else return false;
        a.spam = 1.0f; a.pendingCooldown = true; ++b.abilities;
        return true;
    }
    return false;
}

void World::botPathFailed(BotBrain& b, bool vehicle) {
    ++b.noPaths;
    b.path.clear(); b.wp = 0;
    if (vehicle) { b.noVehicleUntil = match_.matchTime() + 15.0f; b.wantVehicle = false; return; }   // no vehicle corridor: walk it
    // Unreachable goal: another.
    if (b.goal.kind == BotGoalKind::Attack && b.target < 0) b.ignoreSightingsUntil = match_.matchTime() + 8.0f;
    if (b.goal.kind != BotGoalKind::Attack && b.goal.kind != BotGoalKind::Roam) { b.objectiveBlockedUntil = match_.matchTime() + 10.0f; b.mission = false; }
    if (b.target < 0) b.hasGoal = false;
}

void World::botSteer(MatchOpponent& o, BotBrain& b, float dt, MoveIntent& in) {
    Character& pc = o.pawn();
    const BotSkill& sk = botSkill(b.difficulty);
    const core::Vec3 pos = pc.position();
    const bool vehicle = pc.moveForm() == Form::Vehicle;
    BotNav::Agent ag; ag.radius = pc.cylinderRadius(Form::Robot); ag.vehicle = vehicle;
    // Path upkeep: one time-sliced search at a time across all bots (tickBots steps it); the bot keeps its old corridor meanwhile.
    b.repathTimer -= dt;
    const bool chasing = b.goal.kind == BotGoalKind::Attack;
    if ((b.wantRepath || (chasing && b.repathTimer <= 0.0f) || b.vehiclePath != vehicle) && botNav_.valid() && botSearchOwner_ < 0 && botPathBudget_ > 0) {
        --botPathBudget_;
        b.wantRepath = false; b.repathTimer = chasing ? 1.5f : 6.0f; b.vehiclePath = vehicle; ++b.repaths;
        if (match_.matchTime() > b.avoidUntil) b.avoidCells.clear();
        ag.avoid = b.avoidCells.empty() ? nullptr : &b.avoidCells;   // the search keeps the pointer: the brain outlives it
        if (botNav_.beginSearch(pos, b.goal.pos, ag)) { botSearchOwner_ = b.player; botSearchVehicle_ = vehicle; }
        else botPathFailed(b, vehicle);
    }
    core::Vec3 moveDir{0, 0, 0};
    bool jump = false;
    // Combat spacing: hold the weapon's desired band while the target is visible; otherwise follow the path.
    const bool visible = b.target >= 0 && b.seen.count(b.target) && b.seen[b.target].visible;
    float tdist = 0.0f; core::Vec3 toT{0, 0, 0};
    if (visible) if (const Character* t = participantPawn(b.target)) { toT = targetable(*t) - botEye(pc); tdist = core::length(toT); }
    const AiWeaponData& ad = aiWeaponData(pc.weapon().def ? pc.weapon().def->id : "", pc.weapon().magSize);
    const bool inBand = visible && tdist <= aiRangeMaxM(ad.desired) && tdist >= aiRangeMinM(ad.desired) * 0.7f;
    const bool tooClose = visible && tdist < aiRangeMinM(ad.desired) * 0.7f;
    const bool rushing = visible && !b.mission && match_.matchTime() < b.rushUntil && tdist > 1e-3f;
    if (rushing) {
        moveDir = core::normalize(core::Vec3{toT.x, 0, toT.z});   // melee rush: straight at the target
    } else if (b.mission || (!inBand && !tooClose)) {
        // Path following.
        while (b.wp < b.path.size() && hdist(b.path[b.wp].pos, pos) < (vehicle ? 2.5f : 1.2f) && b.path[b.wp].action != 1) { ++b.wp; b.bestDist = 1e9f; }
        // Look-ahead: skip a corner when the one after it is directly walkable (one check per step); vehicles carry momentum
        // past close corners and would otherwise turn back for them.
        if (b.wp + 1 < b.path.size() && b.path[b.wp].action == 0 && b.path[b.wp + 1].action == 0 &&
            botNav_.directWalkable(pos, b.path[b.wp + 1].pos, ag)) { ++b.wp; b.bestDist = 1e9f; }
        if (b.wp < b.path.size()) {
            const BotNav::Waypoint& w = b.path[b.wp];
            core::Vec3 d = w.pos - pos; d.y = 0;
            const float dl = core::length(d);
            if (w.action == 1) {        // jump link: jump when at its foot, then keep pushing toward the top
                if (dl < 6.0f && pc.onGround()) { jump = true; ++b.jumps; }
                if (dl < 1.2f || (pos.y > w.pos.y - 0.5f && dl < 3.0f)) { ++b.wp; b.bestDist = 1e9f; }
            }
            if (dl > 1e-3f) moveDir = d * (1.0f / dl);
            // Stuck: moving less than 0.75 m in 1.5 s, or no 0.5 m of progress toward the current corner in 3 s (jittering against a
            // prop the nav does not know) -> jump, then repath, then avoid that spot and pick a new goal (and leave vehicle form).
            bool noProgress = false;
            if (b.progressWp != b.wp) { b.progressWp = b.wp; b.progressBest = dl; b.progressT = 0.0f; }
            else if (dl < b.progressBest - 0.5f) { b.progressBest = dl; b.progressT = 0.0f; }
            else if ((b.progressT += dt) >= 3.0f) { noProgress = true; b.progressT = 0.0f; b.progressBest = dl; }
            if ((b.stuckT += dt) >= 1.5f || noProgress) {
                if ((noProgress || hdist(pos, b.stuckPos) < 0.75f) && !pc.isTransforming()) {
                    if (b.stuckLevel >= 2) {   // the wedge spot: the current corner's cell and the bot's own
                        const int c0 = botNav_.findCell(pos, 2.0f), c1 = w.cell;
                        for (int c : {c0, c1}) if (c >= 0 && std::find(b.avoidCells.begin(), b.avoidCells.end(), c) == b.avoidCells.end()) b.avoidCells.push_back(c);
                        b.avoidUntil = match_.matchTime() + 30.0f;
                    }
                    ++b.stuckLevel; ++b.stucks;
                    if (b.stuckLevel == 1) jump = true;
                    else if (b.stuckLevel == 2) { b.wantRepath = true; jump = true; }
                    else { b.hasGoal = false; b.wantRepath = true; b.stuckLevel = 0; b.ignoreSightingsUntil = match_.matchTime() + 8.0f;
                           if (vehicle) { b.wantVehicle = false; b.noVehicleUntil = match_.matchTime() + 15.0f; } }
                } else b.stuckLevel = 0;
                b.stuckT = 0.0f; b.stuckPos = pos;
            }
        } else if (b.goal.kind != BotGoalKind::Attack) {
            b.hasGoal = b.hasGoal && hdist(b.goal.pos, pos) > b.goal.radius;   // arrived: the next think picks a new goal
        }
        // Corridor done next to an objective (the nav approach cell): walk the last metres onto the flag / bomb / point itself.
        if ((b.path.empty() || b.wp >= b.path.size()) && b.goal.hasTouch && hdist(b.goal.touch, pos) > 0.6f && hdist(b.goal.touch, pos) < 15.0f) {
            core::Vec3 d = b.goal.touch - pos; d.y = 0; moveDir = core::normalize(d);
        }
        // Off the mesh with no corridor (on a prop / ledge): head for the nearest cell and drop back onto it.
        if (b.path.empty() || b.wp >= b.path.size()) {
            if (botNav_.findCell(pos, 0.0f) < 0) {
                int c = botNav_.findCell(pos, 12.0f);
                if (c < 0) c = botNav_.findCell(pos, 25.0f, 34.0f);   // high on a prop / ledge: walk off toward the floor below
                if (c >= 0) { core::Vec3 d = botNav_.cells()[(size_t)c].centroid - pos; d.y = 0; const float l = core::length(d); if (l > 0.3f) moveDir = d * (1.0f / l); }
                else { core::Vec3 d = b.goal.pos - pos; d.y = 0; const float l = core::length(d); if (l > 0.3f) moveDir = d * (1.0f / l); }   // no floor in reach: head for the goal
                if ((b.offMesh += dt) > 1.5f) { b.offMesh = 0.0f; b.wantRepath = true; }
            } else b.offMesh = 0.0f;
        }
    } else if (tooClose && tdist > 1e-3f) {
        moveDir = core::normalize(core::Vec3{-toT.x, 0, -toT.z});
        if (botNav_.valid() && !botNav_.directWalkable(pos, pos + moveDir * 3.0f, ag)) moveDir = {0, 0, 0};
    }
    // Strafe in combat (robot form), checking the side is walkable.
    if (visible && !vehicle) {
        b.strafeTimer -= dt;
        if (b.strafeTimer <= 0.0f) { b.strafeTimer = b.frange(0.7f, 1.8f); if (b.frand() < 0.6f) b.strafeDir = -b.strafeDir; }
        const core::Vec3 side = core::normalize(core::Vec3{-toT.z, 0, toT.x}) * b.strafeDir;
        if (botNav_.valid() && !botNav_.directWalkable(pos, pos + side * 2.0f, ag)) { b.strafeDir = -b.strafeDir; }
        else moveDir = moveDir + side * sk.strafe;
    }
    // Express the world-space move in the facing frame (MoveIntent is relative to faceYaw).
    in.faceYaw = vehicle && !visible && core::length(moveDir) > 1e-3f ? yawOf(moveDir) : b.yaw;
    const core::Vec3 F = core::forwardFromYawPitch(in.faceYaw, 0.0f);
    const core::Vec3 R = core::normalize(core::cross(F, core::Vec3{0, 1, 0}));
    const float ml = core::length(moveDir);
    if (ml > 1.0f) moveDir = moveDir * (1.0f / ml);
    in.moveForward = core::dot(moveDir, F);
    in.moveRight = core::dot(moveDir, R);
    in.viewPitch = b.pitch;
    in.wantJump = jump && !vehicle;
    if (b.pendingDodge) { in.dodgeDir = b.pendingDodge; b.pendingDodge = 0; }
    if (b.pendingHover) { in.hoverRequest = true; b.pendingHover = false; }
    if (vehicle) { in.steer = core::clampf(wrapPi(in.faceYaw - pc.yaw()) * 1.5f, -1.0f, 1.0f); }
}

void World::botAimAndFire(MatchOpponent& o, BotBrain& b, float dt) {
    Character& pc = o.pawn();
    const BotSkill& sk = botSkill(b.difficulty);
    const core::Vec3 eye = botEye(pc);
    const bool visible = b.target >= 0 && b.seen.count(b.target) && b.seen[b.target].visible;
    const Character* t = visible ? participantPawn(b.target) : nullptr;
    // Desired aim: the target's TargetableLocation (projectiles lead it) plus the skill's tracking error; else along the path.
    float wantYaw = b.yaw, wantPitch = 0.0f;
    core::Vec3 aimPoint = eye + core::forwardFromYawPitch(b.yaw, b.pitch) * 50.0f;
    Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();
    // Repairing a teammate: aim at it and run the Repair Ray's beam ticks (TnWeaponRepair: RepairAmount 60 / s as TnHealTypeRepairTeam,
    // the weapon's own fire interval and ammo) [CONF values; bot use PC ADAPTATION].
    if (b.healTarget >= 0 && w && w->beam() && pc.moveForm() == Form::Robot)
        if (const Character* mate = participantPawn(b.healTarget)) {
            const core::Vec3 d = targetable(*mate) - eye;
            const float maxStep = sk.turnRateDeg * 0.0174533f * dt;
            b.yaw = wrapPi(b.yaw + core::clampf(wrapPi(yawOf(d) - b.yaw), -maxStep, maxStep));
            b.pitch = core::clampf(b.pitch + core::clampf(pitchOf(d) - b.pitch, -maxStep, maxStep), -1.2f, 1.2f);
            pc.setAimPitch(b.pitch);
            const float range = w->rangeM > 0.0f ? w->rangeM : 35.0f;
            if (pc.weaponUsable() && w->ammo == 0 && w->canReload()) { w->beginReload(); ++b.reloads; }
            if (pc.weaponUsable() && core::length(d) <= range && std::fabs(wrapPi(yawOf(d) - b.yaw)) < 0.15f && w->canFire()) {
                w->onFired();
                const float tick = w->fireInterval > 0.0f ? w->fireInterval : 0.1f;
                for (MatchOpponent* q : opponents_) if (q->matchPlayer() == b.healTarget && q->spawned()) q->pawn().health().heal(Health::HealType::AddHealthToAll, 60.0f * tick);
                if (b.healTarget == localPlayer_ && !localDead_) player_.pawn().health().heal(Health::HealType::AddHealthToAll, 60.0f * tick);
                participantShots_.push_back({o.matchPlayer(), w->def ? w->def->id : "RepairRay", eye, targetable(*mate), true, b.healTarget});
                ++b.heals;
            }
            return;
        }
    if (t && w) {
        core::Vec3 tp = targetable(*t);
        const float dist = core::length(tp - eye);
        if (w->projectile() && w->projSpeed > 1.0f) tp = tp + t->velocity() * (dist / w->projSpeed);
        b.targetVisibleFor += dt;
        if ((b.aimErrTimer -= dt) <= 0.0f) {
            b.aimErrTimer = b.frange(0.35f, 0.8f);
            const float settle = std::max(0.4f, 1.0f - b.targetVisibleFor * 0.25f);
            const float r = dist * std::tan(sk.aimErrorDeg * 0.0174533f) * settle;
            b.aimErr = core::Vec3{b.frange(-1, 1), b.frange(-0.6f, 0.6f), b.frange(-1, 1)} * r;
        }
        aimPoint = tp + b.aimErr;
        const core::Vec3 d = aimPoint - eye;
        wantYaw = yawOf(d); wantPitch = pitchOf(d);
    } else if (b.wp < b.path.size()) {
        const core::Vec3 d = b.path[b.wp].pos - pc.position();
        if (hdist(b.path[b.wp].pos, pc.position()) > 0.5f) wantYaw = yawOf(d);
    }
    // Ease the aim at the skill's turn rate (per-step, cheap).
    const float maxStep = sk.turnRateDeg * 0.0174533f * dt;
    const float dy = wrapPi(wantYaw - b.yaw), dp = wantPitch - b.pitch;
    b.yaw = wrapPi(b.yaw + core::clampf(dy, -maxStep, maxStep));
    b.pitch = core::clampf(b.pitch + core::clampf(dp, -maxStep, maxStep), -1.2f, 1.2f);
    pc.setAimPitch(b.pitch);
    // Weapon state.
    if (!w) return;
    if (pc.moveForm() == Form::Vehicle) w->tick(dt);   // the robot weapon ticks in tickBots
    const bool usable = pc.moveForm() == Form::Vehicle ? !pc.isTransforming() : (pc.weaponUsable() && !pc.isMeleeing() && pc.carryingHeavy_ == 0);
    if (usable && w->ammo == 0 && w->canReload()) { w->beginReload(); ++b.reloads; }
    if (!t || pc.isMeleeing()) return;
    // Melee (the shared TnMeleeAttack path, assist lunge included): an enemy within Striking range in front of the robot.
    if (pc.moveForm() == Form::Robot && b.reactionLeft <= 0.0f && b.meleeCooldown <= 0.0f && pc.carryingHeavy_ == 0) {
        const core::Vec3 to = targetable(*t) - pc.actorLocation();
        const float d = std::sqrt(to.x * to.x + to.z * to.z);
        if (d <= 7.0f && std::fabs(wrapPi(yawOf(to) - b.yaw)) < 0.6f) {   // the assist lunge closes the rest
            startMeleeFor(pc, o.matchPlayer(), false, b.yaw);
            if (pc.isMeleeing()) { ++b.melees; b.meleeCooldown = b.frange(1.2f, 2.0f) + (2 - b.difficulty) * 0.5f; return; }
        }
    }
    if (!usable || match_.matchTime() < b.rushUntil) return;   // a melee rush holds fire (a reload would block the strike)
    if (b.reactionLeft > 0.0f) { b.reactionLeft -= dt; return; }
    // Burst pacing from the AI weapon data for the target's CenterPointRange band [CONF RE]; the skill scales it.
    const float dist = core::length(targetable(*t) - eye);
    const AiRange band = aiRangeBand(dist);
    const AiWeaponData& ad = aiWeaponData(w->def ? w->def->id : "", w->magSize);
    const AiWeaponData::Burst& br = (band == AiRange::Striking || band == AiRange::Close) ? ad.shortR : (band == AiRange::Medium ? ad.mediumR : ad.longR);
    if (b.burstLeft <= 0) {
        if (b.burstPause > 0.0f) { b.burstPause -= dt; return; }
        b.burstLeft = std::max(1, (int)std::lround(b.frange((float)br.minShots, (float)br.maxShots + 0.99f) * sk.burstScale));
    }
    // Only shoot roughly on target: the aim must be within the error cone plus the target's size.
    const core::Vec3 aimDir = core::forwardFromYawPitch(b.yaw, b.pitch);
    const core::Vec3 toT = core::normalize(aimPoint - eye);
    const float tol = std::atan2(1.5f, std::max(1.0f, dist)) + sk.aimErrorDeg * 0.0174533f;
    if (std::acos(core::clampf(core::dot(aimDir, toT), -1.0f, 1.0f)) > tol) return;
    if (!w->canFire()) return;
    // Fire along the eased aim (the error is in where the bot looks, not in a second random draw; spread applies on top).
    botFire(o, b, *w, eye + aimDir * std::max(1.0f, dist));
    if (--b.burstLeft <= 0) b.burstPause = b.frange(br.minPause, br.maxPause) * sk.pauseScale;
}

void World::tickBots(float dt) {
    if (bots_.empty() || !matchActive_) return;
    const auto t0 = std::chrono::steady_clock::now();
    ensureBotNav();
    botPathBudget_ = 1;   // at most one new search per simulation step
    // The active search runs at most 1500 cell expansions per step (~1 ms); a long route completes over a few steps.
    if (botSearchOwner_ >= 0 && botNav_.stepSearch(1500) == 1) {
        std::vector<BotNav::Waypoint> path;
        const bool ok = botNav_.finishSearch(path);
        for (BotBrain& sb : bots_)
            if (sb.player == botSearchOwner_) {
                if (ok) { sb.path.swap(path); sb.wp = 0; sb.bestDist = 1e9f; sb.progressTimer = 0.0f; }
                else botPathFailed(sb, botSearchVehicle_);
            }
        botSearchOwner_ = -1;
    }
    for (BotBrain& b : bots_) {
        MatchOpponent* o = nullptr;
        for (MatchOpponent* q : opponents_) if (q->matchPlayer() == b.player) o = q;
        if (!o) continue;
        Character& pc = o->pawn();
        if (!o->spawned()) { b.wasSpawned = false; continue; }
        if (!b.wasSpawned) {   // a fresh spawn: reset the brain (the pawn faces the start's yaw)
            const int keepPlayer = b.player, keepDiff = b.difficulty; const unsigned keepRng = b.rng;
            int s = b.shots, rp = b.repaths, st = b.stucks, j = b.jumps, tr = b.transforms, sw = b.switches, rl = b.reloads;
            const int ml = b.melees, gr = b.grenades, hi = b.hits, np = b.noPaths, ru = b.rushes, he = b.heals, ab = b.abilities;
            b = BotBrain{};
            b.player = keepPlayer; b.difficulty = keepDiff; b.rng = keepRng + 17U;
            b.shots = s; b.repaths = rp; b.stucks = st; b.jumps = j; b.transforms = tr; b.switches = sw; b.reloads = rl;
            b.melees = ml; b.grenades = gr; b.hits = hi; b.noPaths = np; b.rushes = ru; b.heals = he; b.abilities = ab;
            b.wasSpawned = true; b.yaw = pc.yaw();
        }
        b.life += dt;
        // KillZ (FellOutOfWorld) for participants, as the local pawn.
        if (pc.position().y < killZ_) {
            const Match::KillContext kc = killContext(-1, b.player, "Engine.DmgType_Fell");
            match_.killed(-1, b.player, false, "Engine.DmgType_Fell", &kc);
            o->despawn();
            continue;
        }
        if ((b.thinkTimer -= dt) <= 0.0f) { b.thinkTimer = 0.25f; botThink(*o, b); }
        MoveIntent in;
        botSteer(*o, b, dt, in);
        // Transform toward the wanted form (cooldown 2 s; robot spot check for vehicle -> robot as the player's).
        b.transformCooldown -= dt;
        const bool isVeh = pc.moveForm() == Form::Vehicle;
        if (b.transformCooldown <= 0.0f && !pc.isTransforming() && pc.transformDisruptRemain_ <= 0.0f && pc.carryingHeavy_ == 0 && (mapState_.carriedBy(b.player) < 0 || isVeh) && b.wantVehicle != isVeh) {
            bool ok = true;
            if (isVeh) {
                const core::Vec3 a = pc.actorLocation();
                float gy; core::Vec3 gn; core::Vec3 feet = a, spot;
                if (collision_.groundHeight(a.x, a.z, a.y, 4.0f, gy, gn)) feet.y = gy; else feet.y = a.y - pc.meshToActor(Form::Vehicle);
                ok = PlayerController::findRobotSpot(collision(), feet, spot, &pc);
                if (ok) { const core::Vec3 shift{spot.x - feet.x, 0.0f, spot.z - feet.z}; if (core::length(shift) > 1e-3f) { pc.setPosition(pc.position() + shift); pc.addTransformShift(shift * -1.0f); } }
            }
            if (ok) { pc.beginTransform(); ++b.transforms; b.wantRepath = true; }
            b.transformCooldown = 2.0f;
        }
        b.meleeCooldown -= dt; b.grenadeCooldown -= dt;
        pc.tickAbilities(dt);
        tickMeleeFor(pc, b.player, dt);
        if (b.grenadeDelay >= 0.0f && (b.grenadeDelay -= dt) < 0.0f && pc.moveForm() == Form::Robot)
            for (Weapon& w : pc.inventoryMutable()) if (w.grenade()) { releaseGrenade(pc, b.player, w, b.grenadeTarget); break; }
        // Robot weapon state (the local pawn's ticks in PlayerController::applyToPawn).
        if (pc.moveForm() == Form::Robot) {
            pc.tickSpreadModifier(dt);
            pc.tickWeaponSwitch(dt);
            pc.weapon().tick(dt);
        }
        botAimAndFire(*o, b, dt);
        o->setIntent(in);
        static const char* botlog = std::getenv("WFC_BOTLOG");   // diagnostics: each bot (or =<player>) once a second
        if (botlog && (botlog[0] < '0' || botlog[0] > '9' || std::atoi(botlog) == b.player) && ((int)(b.life * 60.0f + 0.5f)) % 60 == 0) {
            const core::Vec3 p = pc.position();
            LOG_INFO("BOTLOG %s p%d (%.1f %.1f %.1f) cell %d %s%s goal %s d%.0f wp %zu/%zu tgt %d vis %d stuck %d in %.2f/%.2f jump %d hp %.0f ammo %d/%d shots %d hits %d nopath %d wpn %s heal %d/%d touch %.1f (%.1f %.1f %.1f)",
                     match_.players()[(size_t)b.player].name.c_str(), b.player, p.x, p.y, p.z, botNav_.findCell(p), pc.moveForm() == Form::Vehicle ? "VEH" : "ROB",
                     pc.isTransforming() ? "*" : "", botGoalName(b.goal.kind), hdist(b.goal.pos, p), b.wp, b.path.size(), b.target,
                     (int)(b.target >= 0 && b.seen[b.target].visible), b.stuckLevel, in.moveForward, in.moveRight, (int)in.wantJump, pc.health().current,
                     pc.weapon().ammo, pc.weapon().reserve, b.shots, b.hits, b.noPaths, pc.weapon().def ? pc.weapon().def->id : "-", b.healTarget, b.heals,
                     b.goal.hasTouch ? core::length(b.goal.touch - p) : -1.0f, b.goal.touch.x, b.goal.touch.y, b.goal.touch.z);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    botMsAccum_ += ms; botMsMax_ = std::max(botMsMax_, ms); ++botTicks_;
}

} // namespace game
