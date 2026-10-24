// Escalation DLC map mechanisms (SurvivalMech.h): District's laser defense grid, sentries, power outage and detpacks; Tranquillity's
// floodgate drain and base doors. Behaviour: RE work/handoff/DLC_ESC_MECHANISMS_RE.md (1104a86, C unless noted); data: the
// escalation_<map>.json "mechanisms" block (tools/gameplay/gen_escalation.py). Maps without the block: every function is a no-op.
#include "game/World.h"
#include "game/Destructible.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {
core::Vec3 v3(const assets::Json& a) { return core::Vec3{a[0].asFloat(0), a[1].asFloat(0), a[2].asFloat(0)}; }
void strings(const assets::Json& a, std::vector<std::string>& out) { for (size_t k = 0; k < a.size(); ++k) out.push_back(a[k].asString()); }
// TnWeaponSentryLight (AssetTools weapon.json, as the campaign sentry table): hitscan 5 every 0.16 s; LightSentry_TURRETDEF
// MaxAttackRange 16000 / SightRadius 20000 UU, LagDegreesPerSecond 60, AimedAtTargetThreshold 3. RepairSentry_TURRETDEF: range
// 8000 / sight 10000 UU, lag 80; TnWeaponSentryRepair HealthPerSecond 15 on its team (TnHealTypeRepairTeam).
constexpr float kLightDamage = 5.0f, kLightInterval = 0.16f, kLightRange = 160.0f, kLightSight = 200.0f, kLightLag = 60.0f;
constexpr float kRepairRange = 80.0f, kRepairLag = 80.0f, kRepairPerSec = 15.0f, kAimDeg = 3.0f;
}

void SvMech::parse(const assets::Json& j, SvMech& out) {
    out = SvMech{};
    if (!j.isObject()) return;
    for (size_t k = 0; k < j["hazardsOff"].size(); ++k) out.hazardsOff.push_back(j["hazardsOff"][k].asString());
    for (size_t k = 0; k < j["lasers"].size(); ++k) {
        const assets::Json& r = j["lasers"][k];
        Laser L; L.gen = r["gen"].asString(); L.hazard = r["hazard"].asString(); L.button = v3(r["button"]);
        L.time = r["time"].asFloat(45.0f); L.armDelay = r["armDelay"].asFloat(3.0f); L.buttonRange = r["buttonRange"].asFloat(3.0f);
        L.seq = r["seq"].asString(); L.buttonActor = r["buttonActor"].asString(); L.switchActor = r["switchActor"].asString();
        L.buttonInterp = r["interps"]["button"].asString(); L.switchInterp = r["interps"]["switch"].asString();
        L.buttonLength = r["interps"]["buttonLength"].asFloat(0.06f); L.switchLength = r["interps"]["switchLength"].asFloat(1.84f);
        strings(r["emitters"], L.emitters);
        out.lasers.push_back(L);
    }
    for (size_t k = 0; k < j["sentries"].size(); ++k) {
        const assets::Json& r = j["sentries"][k];
        Sentry S; S.seq = r["seq"].asString(); S.gen = r["gen"].asString(); S.pos = v3(r["pos"]); S.yaw = r["yaw"].asFloat(0);
        S.repair = r["repair"].asBool(); S.time = r["time"].asFloat(45.0f); S.uses = r["uses"].asInt(0);
        S.body = r["body"].asString(); S.templ = r["template"].asString(); strings(r["emitters"], S.emitters);
        out.sentries.push_back(S);
    }
    for (size_t k = 0; k < j["detpacks"].size(); ++k) {
        const assets::Json& r = j["detpacks"][k];
        Detpack D; D.pos = v3(r["pos"]); D.range = r["range"].asFloat(5.0f); D.countdown = r["countdown"].asFloat(3.0f);
        for (size_t h = 0; h < r["hits"].size(); ++h) D.hits.push_back({r["hits"][h][0].asString(), r["hits"][h][1].asFloat(0), r["hits"][h][2].asFloat(1000.0f)});
        out.detpacks.push_back(D);
    }
    if (j["outage"].isObject()) {
        const assets::Json& o = j["outage"];
        Outage& O = out.outage;
        O.on = true; O.fromRound = o["fromRound"].asInt(4); O.lateRound = o["lateRound"].asInt(10); O.max = o["max"].asInt(3);
        O.chance = o["chance"].asFloat(0.25f); O.lateChance = o["lateChance"].asFloat(0.5f); O.delay = o["delay"].asFloat(5.0f);
        O.duration = o["duration"].asFloat(60.0f); O.lockout = o["lockout"].asFloat(180.0f); O.lateLockout = o["lateLockout"].asFloat(300.0f);
        O.ppvOn = o["ppvOn"].asString(); O.ppvOff = o["ppvOff"].asString(); O.interp = o["interp"].asString(); O.interpLength = o["interpLength"].asFloat(1.51f);
    }
    if (j["floodgates"].isObject()) {
        const assets::Json& f = j["floodgates"];
        for (size_t k = 0; k < f["gens"].size(); ++k) {
            const assets::Json& r = f["gens"][k];
            Flood F; F.gen = r["gen"].asString(); F.trigger = r["trigger"].asString(); F.hazard = r["hazard"].asString();
            F.waterfallInterp = r["waterfallInterp"].asString(); F.materialInterp = r["materialInterp"].asString();
            out.floods.push_back(F);
        }
        for (size_t k = 0; k < f["drainTimes"].size(); ++k) out.drainTimes.push_back(f["drainTimes"][k].asFloat(0));
        for (size_t k = 0; k < f["zonesOn"].size(); ++k) out.zonesOn.push_back(f["zonesOn"][k].asString());
        for (size_t k = 0; k < f["blockers"].size(); ++k) out.blockers.push_back(f["blockers"][k].asString());
        out.energon = f["energon"].asString();
        out.gateOpenDelay = f["gateOpenDelay"].asFloat(3.1f);
        out.drainInterp = f["drainInterp"].asString(); out.drainLength = f["drainLength"].asFloat(10.03f);
        strings(f["unhideAtDrain"], out.unhideAtDrain); out.fogOnAtDrain = f["fogOnAtDrain"].asBool(false);
        for (size_t k = 0; k < f["baseDoors"].size(); ++k) {
            const assets::Json& r = f["baseDoors"][k];
            BaseDoor B; B.interp = r["interp"].asString(); B.blocker = r["blocker"].asString(); strings(r["pistons"], B.pistons);
            out.baseDoors.push_back(B);
        }
        out.doorHold = f["doorHold"].asFloat(1.25f); out.doorOpened = f["doorOpened"].asFloat(4.35f); out.doorLength = f["doorLength"].asFloat(6.67f);
        out.floodBlockers = out.blockers;
    }
    for (size_t k = 0; k < j["managers"].size(); ++k) {
        const assets::Json& r = j["managers"][k];
        Manager G; G.seq = r["seq"].asString(); G.delay = r["delay"].asFloat(0.0f);
        strings(r["gens"], G.gens); strings(r["interps"], G.interps); strings(r["blockers"], G.blockers); strings(r["actors"], G.actors);
        for (const std::string& b : G.blockers) if (std::find(out.blockers.begin(), out.blockers.end(), b) == out.blockers.end()) out.blockers.push_back(b);
        out.managers.push_back(G);
    }
}

std::vector<std::string> World::svMechBlockerNames(const std::string& map) {
    if (map.rfind("MP_ESC_", 0) != 0) return {};
    SurvivalData d;
    return SurvivalData::load(map, d) ? d.mech.blockers : std::vector<std::string>{};
}

void World::svMechAddBlockers() {
    escBlockerSets_.clear();
    for (const auto& b : escBlockerTris_) escBlockerSets_.push_back(collision_.addDynamicSet(b.second, core::Mat4::identity()));
    if (!escBlockerSets_.empty()) LOG_INFO("escalation: %zu base-door blocking volumes as switchable collision", escBlockerSets_.size());
}

void World::svMechSetBlockers(bool on) {
    for (int id : escBlockerSets_) collision_.setDynamicEnabled(id, on);
    escBlockersOn_ = on;
    escBlockerOn_.assign(escBlockerSets_.size(), on ? 1 : 0);
}

bool World::svMechSetBlocker(const std::string& name, bool on) {   // one blocking volume / door by actor name
    bool found = false;
    for (size_t i = 0; i < escBlockerTris_.size() && i < escBlockerSets_.size(); ++i)
        if (escBlockerTris_[i].first == name) {
            collision_.setDynamicEnabled(escBlockerSets_[i], on); found = true;
            if (escBlockerOn_.size() < escBlockerSets_.size()) escBlockerOn_.resize(escBlockerSets_.size(), 1);
            escBlockerOn_[i] = on ? 1 : 0;
        }
    return found;
}

bool World::svMechBlockerOn(const std::string& name) const {
    for (size_t i = 0; i < escBlockerTris_.size() && i < escBlockerOn_.size(); ++i) if (escBlockerTris_[i].first == name) return escBlockerOn_[i] != 0;
    return true;
}

bool World::svMechHazard(const std::string& actor, bool on) {
    for (HazardVolume& h : hazards_) if (h.actor == actor) { h.on = on; return true; }
    return false;
}

void World::svMechResetGenerator(int g) {   // ModifySurvivalGeneratorPower -cost: back at 0, its activation volume on (re-buyable)
    if (g < 0 || (size_t)g >= svData_.generators.size()) return;
    sv_.pool[(size_t)g] = 0.0f; sv_.generatorDone[(size_t)g] = 0; sv_.generatorOff[(size_t)g] = 0;
}

void World::svMechSetup() {
    SvMech& M = svData_.mech;
    SvMechState& S = sv_.mech;
    S = SvMechState{};
    for (HazardVolume& h : hazards_) h.on = true;
    svMechSetBlockers(true);
    if (!M.any()) return;
    auto gen = [&](const std::string& id) { for (size_t g = 0; g < svData_.generators.size(); ++g) if (svData_.generators[g].id == id) return (int)g; return -1; };
    int missing = 0;
    for (const std::string& h : M.hazardsOff) missing += !svMechHazard(h, false);
    for (SvMech::Laser& L : M.lasers) missing += (L.g = gen(L.gen)) < 0;
    for (SvMech::Sentry& T : M.sentries) missing += (T.g = gen(T.gen)) < 0;
    for (SvMech::Flood& F : M.floods) missing += (F.g = gen(F.gen)) < 0;
    S.lasers.assign(M.lasers.size(), {});
    S.sentries.assign(M.sentries.size(), {});
    for (size_t k = 0; k < M.sentries.size(); ++k) S.sentries[k].yaw = M.sentries[k].yaw;
    for (size_t k = 0; k < M.lasers.size(); ++k) S.lasers[k].switchPos = M.lasers[k].switchLength;   // GameplayStarted: forward = disabled
    S.floodT.assign(M.floods.size(), -1.0f);
    S.doorPos = M.doorHold;
    S.managerT.assign(M.managers.size(), -1.0f); S.managerDone.assign(M.managers.size(), 0);
    for (SvMech::Manager& G : M.managers) { G.g.clear(); for (const std::string& id : G.gens) { const int gi = gen(id); if (gi >= 0) G.g.push_back(gi); else ++missing; } }
    S.detpacks.assign(M.detpacks.size(), {});
    S.prevInteract.assign(match_.players().size(), 0);
    LOG_INFO("escalation: map mechanisms: %zu lasers (%zu pain volumes off), %zu sentries, %zu detpacks, outage %s, %zu floodgates, %zu blockers%s",
             M.lasers.size(), M.hazardsOff.size(), M.sentries.size(), M.detpacks.size(), M.outage.on ? "on" : "off", M.floods.size(),
             escBlockerSets_.size(), missing ? " - SOME TARGETS NOT FOUND" : "");
}

bool World::svMechBuyBlocked() const { return sv_.mech.outageLeft > 0.0f; }

// A generator's New Power Level (svFireGenerator, after its flattened actions): the mechanism it starts.
void World::svMechGenerator(int g, bool levelDrop) {
    if (levelDrop || g < 0) return;
    SvMech& M = svData_.mech;
    SvMechState& S = sv_.mech;
    for (size_t k = 0; k < M.lasers.size(); ++k)
        if (M.lasers[k].g == g) {   // LaserGeneratorUsed: lasers on, the 45 s timer, its activation volume off while active
            S.lasers[k].active = true; S.lasers[k].t = 0.0f;
            svMechHazard(M.lasers[k].hazard, true);
            sv_.generatorOff[(size_t)g] = 1;
            ++S.laserRuns;
            LOG_INFO("escalation: laser grid %s on (%s, %.0f s)", M.lasers[k].gen.c_str(), M.lasers[k].hazard.c_str(), M.lasers[k].time);
        }
    for (size_t k = 0; k < M.sentries.size(); ++k)
        if (M.sentries[k].g == g) {   // SetDormant off for its time
            S.sentries[k].active = true; S.sentries[k].t = 0.0f; S.sentries[k].target = -1;
            sv_.generatorOff[(size_t)g] = 1;
            ++S.sentryRuns;
            LOG_INFO("escalation: sentry %s (%s) awake for %.0f s", M.sentries[k].seq.c_str(), M.sentries[k].repair ? "repair" : "ion", M.sentries[k].time);
        }
    for (size_t k = 0; k < M.managers.size(); ++k)   // a Zone_Manager: its door matinee plays (the zones / volumes: the flattened actions)
        if (S.managerT[k] < 0.0f && std::find(M.managers[k].g.begin(), M.managers[k].g.end(), g) != M.managers[k].g.end()) {
            S.managerT[k] = 0.0f;
            LOG_INFO("escalation: %s fired by generator %s (doors open in %.2f s)", M.managers[k].seq.c_str(), svData_.generators[(size_t)g].id.c_str(), M.managers[k].delay);
        }
    for (size_t k = 0; k < M.floods.size(); ++k)
        if (M.floods[k].g == g && S.floodBought < (int)M.floods.size()) {   // resume the drain to its next pause; its pain volume off
            ++S.floodBought;
            svMechHazard(M.floods[k].hazard, false);
            if (k < S.floodT.size()) S.floodT[k] = 0.0f;
            LOG_INFO("escalation: floodgate %s bought (%d / %zu)", M.floods[k].gen.c_str(), S.floodBought, M.floods.size());
        }
}

void World::svMechTick(float dt) {
    const SvMech& M = svData_.mech;
    SvMechState& S = sv_.mech;
    if (!M.any() || sv_.phase == SurvivalState::Phase::Off) return;
    const std::vector<MatchPlayer>& P = match_.players();
    if (S.prevInteract.size() < P.size()) S.prevInteract.resize(P.size(), 0);
    // the presentation's matinee positions (rate 1)
    for (size_t k = 0; k < M.lasers.size(); ++k)   // bought: the button matinee reversed (usable); else forward (disabled)
        S.lasers[k].switchPos = std::clamp(S.lasers[k].switchPos + (S.lasers[k].active ? -dt : dt), 0.0f, M.lasers[k].switchLength);
    if (M.outage.on) S.outagePos = std::clamp(S.outagePos + (S.outageLeft > 0.0f ? dt : -dt), 0.0f, M.outage.interpLength);
    for (float& t : S.floodT) if (t >= 0.0f) t += dt;
    for (size_t k = 0; k < M.managers.size(); ++k) {   // a fired manager's doors: their collision off once the matinee has played
        if (S.managerT[k] < 0.0f) continue;
        S.managerT[k] += dt;
        if (S.managerDone[k] || S.managerT[k] < M.managers[k].delay) continue;
        S.managerDone[k] = 1;
        for (const std::string& b : M.managers[k].blockers) svMechSetBlocker(b, false);
        LOG_INFO("escalation: %s doors open (%zu blocking actors off)", M.managers[k].seq.c_str(), M.managers[k].blockers.size());
    }
    if (S.gateT >= 0.0f) { S.gateT += dt; S.doorPos = std::min(M.doorLength, M.doorHold + S.gateT); }   // OpenBase: the doors play on
    std::vector<uint8_t> press(P.size(), 0);   // a player's Interact press this step (rising edge)
    for (size_t q = 0; q < P.size(); ++q) {
        const bool held = P[q].kind != ParticipantKind::Bot && svInteractHeld((int)q);
        press[q] = held && !S.prevInteract[q];
        S.prevInteract[q] = held ? 1 : 0;
    }
    auto pressNear = [&](const core::Vec3& at, float range) {
        for (size_t q = 0; q < P.size(); ++q) {
            if (!press[q]) continue;
            const Character* c = participantPawn((int)q);
            if (c && core::length(c->position() - at) <= range) return (int)q;
        }
        return -1;
    };
    // Laser grid: the timer, or the button's End Use (usable from armDelay), ends it: lasers off, the generator back at 0.
    for (size_t k = 0; k < M.lasers.size(); ++k) {
        SvMechState::Laser& L = S.lasers[k];
        if (!L.active) continue;
        L.t += dt;
        const bool button = L.t >= M.lasers[k].armDelay && pressNear(M.lasers[k].button, M.lasers[k].buttonRange) >= 0;
        if (L.t < M.lasers[k].time && !button) continue;
        L.active = false;
        svMechHazard(M.lasers[k].hazard, false);
        svMechResetGenerator(M.lasers[k].g);
        LOG_INFO("escalation: laser grid %s off (%s at %.1f s)", M.lasers[k].gen.c_str(), button ? "the button" : "time", L.t);
    }
    // Sentries: awake ones shoot (ion) or repair (heal); at their time dormant again, the generator reset (limited: off for good).
    const int coop = svData_.coopTeam;
    for (size_t k = 0; k < M.sentries.size(); ++k) {
        const SvMech::Sentry& D = M.sentries[k];
        SvMechState::Sentry& T = S.sentries[k];
        if (!T.active) continue;
        if ((T.t += dt) >= D.time) {
            T.active = false; ++T.expiries;
            const bool spent = D.uses > 0 && T.expiries >= D.uses;
            if (spent) sv_.generatorOff[(size_t)D.g] = 1;   // the Increment B 3: volume and emitter off for good
            else svMechResetGenerator(D.g);
            LOG_INFO("escalation: sentry %s dormant (%d uses%s)", D.seq.c_str(), T.expiries, spent ? ", spent" : "");
            continue;
        }
        if (match_.state() != Match::State::InProgress) continue;
        const core::Vec3 eye = D.pos + core::Vec3{0.0f, 0.5f, 0.0f};
        int tgt = -1; float best = D.repair ? kRepairRange : kLightSight;
        for (size_t q = 0; q < P.size(); ++q) {
            const MatchPlayer& mp = P[q];
            if (!mp.alive || (D.repair ? mp.team != coop : mp.team == coop)) continue;
            const Character* c = participantPawn((int)q);
            if (!c || (D.repair && c->health().current >= c->health().max)) continue;
            const float L = core::length(c->actorLocation() - eye);
            if (L > best || !botLineOfSight(eye, c->actorLocation())) continue;
            best = L; tgt = (int)q;
        }
        T.target = tgt;
        if (tgt < 0) continue;
        Character* tc = participantPawnMutable(tgt);
        const core::Vec3 aim = tc->actorLocation() - eye;
        const float want = std::atan2(-aim.x, -aim.z);
        float dy = std::remainder(want - T.yaw, 6.2831853f);
        const float step = (D.repair ? kRepairLag : kLightLag) * 0.0174533f * dt;
        T.yaw += std::clamp(dy, -step, step);
        dy = std::remainder(want - T.yaw, 6.2831853f);
        if (std::fabs(dy) > kAimDeg * 0.0174533f) continue;
        if (D.repair) { tc->health().heal(Health::HealType::AddHealthToAll, kRepairPerSec * dt); continue; }
        if ((T.cool -= dt) > 0.0f || best > kLightRange) continue;
        T.cool = kLightInterval;
        applyMatchDamage(tgt, -1, kLightDamage, false, "TnDamageTypeSentryLight");
    }
    // Detpacks: a press within range plants it (single use); after the countdown each destructible takes its damage.
    for (size_t k = 0; k < M.detpacks.size(); ++k) {
        const SvMech::Detpack& D = M.detpacks[k];
        SvMechState::Detpack& T = S.detpacks[k];
        if (!T.used) {
            const int q = pressNear(D.pos, D.range);
            if (q < 0) continue;
            T.used = true; T.t = 0.0f; T.by = q;
            LOG_INFO("escalation: detpack %zu planted by p%d", k, q);
            continue;
        }
        if (T.next >= D.hits.size()) continue;
        T.t += dt;
        while (T.next < D.hits.size() && T.t >= D.countdown + D.hits[T.next].delay) {
            const SvMech::Hit& h = D.hits[T.next++];
            bool found = false;
            for (Destructible* d : destructibles_) {
                const std::string& n = d->name();
                if (n.size() >= h.wall.size() && n.compare(n.size() - h.wall.size(), h.wall.size(), h.wall) == 0) { d->applyDamage(*this, h.damage); found = true; break; }
            }
            ++S.detonations;
            LOG_INFO("escalation: detpack %zu BaBoom: %.0f on %s%s", k, h.damage, h.wall.c_str(), found ? "" : " (NOT FOUND)");
        }
    }
    // Power outage: armed from the fromRound-th Round Begin; each later Round Begin rolls after the delay (within the match max and
    // outside the lockout); while out no generator can be bought (active traps / sentries keep running).
    const SvMech::Outage& O = M.outage;
    if (O.on) {
        if (S.outageLeft > 0.0f && (S.outageLeft -= dt) <= 0.0f) { S.outageLeft = 0.0f; LOG_INFO("escalation: power restored"); }
        if (S.lockLeft > 0.0f) S.lockLeft = std::max(0.0f, S.lockLeft - dt);
        for (; S.roundsSeen < sv_.roundBegins; ++S.roundsSeen)
            if (S.roundsSeen + 1 >= O.fromRound && S.outages < O.max && S.lockLeft <= 0.0f && S.rollIn < 0.0f) S.rollIn = O.delay;
        if (S.rollIn >= 0.0f && (S.rollIn -= dt) < 0.0f) {
            S.rollIn = -1.0f;
            // RandomSwitch_195 (4 links, the Match RNG): Link 1 -> PowerOutage; Link 4 -> Gate_15340 (open from the lateRound-th
            // Round Begin) -> PowerOutage; Links 2 / 3 nothing. So chance 1 / 4, late 2 / 4 (the data's chance / lateChance).
            const bool late = sv_.roundBegins >= O.lateRound;
            const int link = match_.drawInt(4);
            if (link == 0 || (late && link == 3)) {
                S.outageLeft = O.duration; ++S.outages; S.lockLeft = late ? O.lateLockout : O.lockout;
                LOG_INFO("escalation: POWER OUTAGE %d (round %d, %.0f s; lockout %.0f s)", S.outages, sv_.roundBegins, O.duration, S.lockLeft);
            }
        }
    }
    // Floodgates: the drain plays to the pause of the purchases so far; the last one plays to the end: Drain Completed.
    if (!M.floods.empty() && !S.drained && S.floodBought > 0 && (size_t)S.floodBought <= M.drainTimes.size()) {
        const float target = M.drainTimes[(size_t)S.floodBought - 1];
        S.drainT = std::min(target, S.drainT + dt);
        if (S.floodBought == (int)M.floods.size() && S.drainT >= target) {
            S.drained = true; S.gateT = 0.0f;
            for (const std::string& z : M.zonesOn)
                for (size_t i = 0; i < svData_.zones.size(); ++i) if (svData_.zones[i].id == z) svSetZoneActive((int)i, true);
            if (!M.energon.empty()) svMechHazard(M.energon, false);
            LOG_INFO("escalation: Drain Completed (zones %zu on, the base doors opening)", M.zonesOn.size());
        }
    }
    if (S.gateT >= 0.0f && !S.gatesOpen && S.gateT >= M.gateOpenDelay) {   // "Opened"
        S.gatesOpen = true;
        for (const std::string& b : M.floodBlockers) svMechSetBlocker(b, false);
        LOG_INFO("escalation: base doors Opened (%zu blocking volumes off)", M.floodBlockers.size());
    }
}

} // namespace game
