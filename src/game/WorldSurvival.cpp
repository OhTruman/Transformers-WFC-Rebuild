// Escalation (SV) E1: the director, the wave budget and the spawn finder (see game/Survival.h). RE MP_ESCALATION_RE.md §1-§2 and
// RE's TnSurvivalSpawnFinder answer (2026-10-23, C: TransContent bytecode 989-1160, SpawnDude 1318). PROV until E2-E4: the horde
// bodies / weapons / health are the versus bot presets of the horde faction with the Classic brain (the GOAP sets are not ported),
// no power / generators / downed state; spawn point positions inside the zone box until AssetTools' PossibleSpawnPoints land.
#include "game/World.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {

#include "game/EscalationData.inc"

bool SurvivalData::load(const std::string& map, SurvivalData& out) {
    out = SurvivalData{};
    for (const auto& e : kEscalationData) {
        if (map != e.map) continue;
        std::string txt;
        for (const char* const* p = e.parts; *p; ++p) txt += *p;
        assets::Json j;
        if (!assets::Json::parse(txt, j)) { LOG_ERROR("escalation: data for %s does not parse", map.c_str()); return false; }
        out.map = map;
        out.coopTeam = (int)j["coopTeam"].asFloat(0.0f);
        out.preGameTime = j["preGameTime"].asFloat(30.0f);
        out.timeBetweenWaves = j["timeBetweenWaves"].asFloat(25.0f);
        out.fillWaveReCheckTime = j["fillWaveReCheckTime"].asFloat(0.5f);
        const assets::Json& w = j["waves"];
        for (size_t i = 0; i < w.size(); ++i)
            out.waves.push_back({(int)w[i]["budget"].asFloat(0), (int)w[i]["budgetAlive"].asFloat(0), (int)w[i]["deficit"].asFloat(0)});
        const assets::Json& en = j["enemies"];
        for (size_t i = 0; i < en.size(); ++i) {
            const assets::Json& r = en[i];
            Enemy x;
            x.type = r["type"].isString() ? r["type"].strVal : std::string("?");
            x.minWave = (int)r["minWave"].asFloat(0); x.maxWave = (int)r["maxWave"].asFloat(0);
            x.minCount = (int)r["minCount"].asFloat(0); x.maxCount = (int)r["maxCount"].asFloat(0);
            x.cost = (int)r["cost"].asFloat(0); x.spawnType = std::clamp((int)r["spawnType"].asFloat(0), 0, kSvTypes - 1);
            x.power = r["power"].asFloat(0);
            out.enemies.push_back(x);
        }
        const assets::Json& zs = j["zones"];
        for (size_t i = 0; i < zs.size(); ++i) {
            Zone z; z.id = zs[i]["id"].strVal;
            out.zones.push_back(z);
        }
        for (size_t i = 0; i < zs.size(); ++i) {
            const assets::Json& r = zs[i];
            Zone& z = out.zones[i];
            z.active = r["active"].asBool(true);
            z.points = (int)r["points"].asFloat(0);
            for (int t = 0; t < kSvTypes; ++t) z.self[(size_t)t] = r["self"][(size_t)t].asFloat(0);
            z.mn = {r["min"][0].asFloat(0), r["min"][1].asFloat(0), r["min"][2].asFloat(0)};
            z.mx = {r["max"][0].asFloat(0), r["max"][1].asFloat(0), r["max"][2].asFloat(0)};
            const assets::Json& ch = r["changes"];
            for (size_t c = 0; c < ch.size(); ++c) {
                Zone::Change x;
                for (size_t k = 0; k < out.zones.size(); ++k) if (out.zones[k].id == ch[c]["zone"].strVal) x.zone = (int)k;
                if (x.zone < 0) { LOG_WARN("escalation: zone %s changes unknown zone %s", z.id.c_str(), ch[c]["zone"].strVal.c_str()); continue; }
                for (int t = 0; t < kSvTypes; ++t) { x.mod[(size_t)t] = ch[c]["mod"][(size_t)t].asFloat(0); x.named[(size_t)t] = ch[c]["named"][(size_t)t].asFloat(0) != 0.0f; }
                z.changes.push_back(x);
            }
            const assets::Json& sp = r["spawnPoints"];
            for (size_t k = 0; k < sp.size(); ++k)
                z.spawnPoints.push_back({sp[k][0].asFloat(0), sp[k][1].asFloat(0), sp[k][2].asFloat(0), sp[k][3].asFloat(0)});
        }
        out.ok = !out.waves.empty() && !out.enemies.empty() && !out.zones.empty();
        return out.ok;
    }
    return false;
}

// The horde: one participant per enemy entry x MaxCount (the most that entry can have alive), spawned only by the director.
void World::setupSurvival() {
    sv_ = SurvivalState{};
    if (!match_.settings().survival) return;
    if (!SurvivalData::load(mapName_, svData_)) { LOG_ERROR("escalation: no Escalation data for %s", mapName_.c_str()); return; }
    ensureBotNav();
    const int horde = svData_.coopTeam == 0 ? 1 : 0;
    int n = 0;
    for (const SurvivalData::Enemy& e : svData_.enemies) n += std::max(0, e.maxCount);
    BotLaunch b; b.friendly = 0; b.enemy = n; b.difficulty = 1;   // skill index fixed at 1 (no difficulty setting) [RE §1]
    std::vector<std::string> taken;
    int humans = 0;
    for (const MatchPlayer& p : match_.players()) { taken.push_back(p.name); humans += p.kind != ParticipantKind::Bot; }
    const std::vector<BotIdentity> ids = makeBotIdentities(b, true, svData_.coopTeam, 128, 128, 256, humans, taken, 0x5eed5u);
    size_t k = 0;
    for (size_t e = 0; e < svData_.enemies.size(); ++e)
        for (int c = 0; c < svData_.enemies[e].maxCount && k < ids.size(); ++c, ++k) {
            const BotIdentity& id = ids[k];
            const int p = match_.addPlayerExact(svData_.enemies[e].type + " " + std::to_string(c + 1), horde);
            MatchPlayer& mp = match_.playerMutable(p);
            mp.kind = ParticipantKind::Bot;
            mp.directorSpawned = true;
            match_.selectCharacter(p, id.selection);
            preloadHeldWeaponModels(id.selection.weapons);
            chassisAssets(resolveChassis(id.selection, match_.faction(p)));
            auto o = std::make_unique<MatchOpponent>(p, mp.team, true);
            addBotBrain(p, 1);
            if (smart_.size() <= (size_t)p) smart_.resize((size_t)p + 1);
            smart_[(size_t)p] = SmartBot{};   // Classic (PROV until the GOAP sets: RE §9.5)
            opponents_.push_back(o.get());
            actors_.push_back(std::move(o));
            sv_.slots.push_back({p, (int)e, false});
        }
    rebuildOppIndex();
    const size_t nz = svData_.zones.size();
    sv_.universal.assign(nz, 0.0f);
    sv_.current.assign(nz, {});
    sv_.pointIndex.assign(nz, 0);
    for (int t = 0; t < kSvTypes; ++t) svSort(t);
    sv_.phase = SurvivalState::Phase::PreGame;
    LOG_INFO("escalation: %s: %zu waves, %zu enemy entries, %zu zones, %zu horde slots (team %d vs players' team %d)", mapName_.c_str(),
             svData_.waves.size(), svData_.enemies.size(), nz, sv_.slots.size(), horde, svData_.coopTeam);
}

float World::svZoneScore(int z, int t) const {
    return sv_.current[(size_t)z][(size_t)t] + sv_.universal[(size_t)z] + svData_.zones[(size_t)z].self[(size_t)t];
}

// Sorter for one type: the active zones by GetZoneScore, highest first.
void World::svSort(int t) {
    std::vector<int>& s = sv_.sorted[(size_t)t];
    s.clear();
    for (size_t z = 0; z < svData_.zones.size(); ++z) if (svData_.zones[z].active) s.push_back((int)z);
    std::stable_sort(s.begin(), s.end(), [&](int a, int b) { return svZoneScore(a, t) > svZoneScore(b, t); });
}

// TnSurvivalZoneVolume.Touch (sign +1) / UnTouch (-1) by a player pawn: UniversalScoreAdjustment -= 999 (OccupiedZoneScore),
// ApplyZoneChanges to the listed zones, then re-sort only the types those entries name [RE C].
void World::svTouch(int z, int sign) {
    sv_.universal[(size_t)z] -= 999.0f * (float)sign;
    std::array<bool, kSvTypes> affected{};
    for (const SurvivalData::Zone::Change& c : svData_.zones[(size_t)z].changes)
        for (int t = 0; t < kSvTypes; ++t) {
            sv_.current[(size_t)c.zone][(size_t)t] += c.mod[(size_t)t] * (float)sign;
            affected[(size_t)t] = affected[(size_t)t] || c.named[(size_t)t];
        }
    for (int t = 0; t < kSvTypes; ++t) if (affected[(size_t)t]) svSort(t);
}

// SpawnDude: up to 5 tries, each re-rolling the zone among the type's top 3 (weighted by |score|, no floor) and advancing that zone's
// round-robin point; a zone without points spends the try; the capsule must fit. False: "Failed to find a place to spawn a dude".
bool World::svSpawn(int slot) {
    const SurvivalState::Slot& s = sv_.slots[(size_t)slot];
    const int t = svData_.enemies[(size_t)s.enemy].spawnType;
    const std::vector<int>& sorted = sv_.sorted[(size_t)t];
    const int top = std::min<int>(3, (int)sorted.size());
    if (top <= 0) return false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        float total = 0.0f;
        for (int i = 0; i < top; ++i) total += std::fabs(svZoneScore(sorted[(size_t)i], t));
        float r = match_.drawFloat() * total;
        int pick = 0;   // a fall-through (float edge) reads index 0 [RE H]
        for (int i = 0; i < top; ++i) {
            const float w = std::fabs(svZoneScore(sorted[(size_t)i], t));
            if (r <= w) { pick = i; break; }
            r -= w;
        }
        const int z = sorted[(size_t)pick];
        const SurvivalData::Zone& zone = svData_.zones[(size_t)z];
        if (zone.points <= 0) continue;   // GetSpawnPoint: none (an error log in the original), the try is spent
        const int idx = sv_.pointIndex[(size_t)z] % zone.points;
        sv_.pointIndex[(size_t)z] = (idx + 1) % zone.points;
        core::Vec3 at; float yaw = 0.0f;
        if ((size_t)idx < zone.spawnPoints.size()) {
            const auto& q = zone.spawnPoints[(size_t)idx];
            at = {q[0], q[1], q[2]}; yaw = q[3];
        } else {   // PROV: point idx of n spread around the box centre until the authored TnSurvivalSpawnPoint positions arrive
            const float cx = 0.5f * (zone.mn.x + zone.mx.x), cz = 0.5f * (zone.mn.z + zone.mx.z);
            const float rad = std::min(4.0f, 0.25f * std::min(zone.mx.x - zone.mn.x, zone.mx.z - zone.mn.z));
            const float a = 6.2831853f * (float)idx / (float)std::max(1, zone.points);
            at = {cx + rad * std::cos(a), zone.mx.y, cz + rad * std::sin(a)};
        }
        float gy = 0.0f; core::Vec3 gn;
        if (!collision_.valid() || !collision_.groundHeight(at.x, at.z, at.y + 0.5f, 0.0f, gy, gn)) continue;
        if ((size_t)idx >= zone.spawnPoints.size() && gy < zone.mn.y - 2.0f) continue;   // PROV point fell out of the zone
        at.y = gy;
        bool clear = true;   // IsSafeSpawnLocation (capsule): no live pawn within two radii + 1 m while the cylinders overlap in height
        for (size_t q = 0; q < match_.players().size() && clear; ++q) {
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const core::Vec3 d = c->position() - at;
            if (std::fabs(d.y) < 4.0f && std::sqrt(d.x * d.x + d.z * d.z) < 2.0f + c->cylinderRadius(c->moveForm()) + 1.0f) clear = false;
        }
        if (!clear) continue;
        // Face the nearest player (PROV: the point's own rotation once the authored points land).
        if ((size_t)idx >= zone.spawnPoints.size()) {
            float best = 1e9f;
            for (size_t q = 0; q < match_.players().size(); ++q) {
                if (match_.players()[q].kind == ParticipantKind::Bot) continue;
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                const core::Vec3 d = c->position() - at;
                const float l = d.x * d.x + d.z * d.z;
                if (l < best) { best = l; yaw = std::atan2(-d.x, -d.z); }
            }
        }
        match_.restartPlayerAt(s.player, at, yaw);
        // SeeEnemy for every player's pawn: the horde knows where everyone is from the start.
        for (BotBrain& b : bots_) {
            if (b.player != s.player) continue;
            const float now = match_.matchTime();
            float best = 1e9f;
            for (size_t q = 0; q < match_.players().size(); ++q) {
                if (match_.players()[q].kind == ParticipantKind::Bot) continue;
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                b.seen[(int)q] = BotBrain::Seen{c->actorLocation(), now, true};
                const float l = core::length(c->position() - at);
                if (l < best) { best = l; b.target = (int)q; }
            }
        }
        return true;
    }
    return false;
}

void World::tickSurvival(float dt) {
    if (sv_.phase == SurvivalState::Phase::Off || sv_.phase == SurvivalState::Phase::GameOver) return;
    if (match_.state() == Match::State::MatchOver) { sv_.phase = SurvivalState::Phase::GameOver; return; }
    if (match_.state() != Match::State::InProgress) return;
    const std::vector<MatchPlayer>& P = match_.players();
    // Zone touches by player pawns (TnPlayerPawn only; a dead pawn untouches).
    if (sv_.touching.size() < P.size()) sv_.touching.resize(P.size());
    for (size_t q = 0; q < P.size(); ++q) {
        std::vector<int> now;
        const Character* c = P[q].kind != ParticipantKind::Bot ? participantPawn((int)q) : nullptr;
        if (c) {
            const core::Vec3 p = c->position();
            for (size_t z = 0; z < svData_.zones.size(); ++z) {
                const SurvivalData::Zone& Z = svData_.zones[z];
                if (p.x >= Z.mn.x && p.x <= Z.mx.x && p.y >= Z.mn.y && p.y <= Z.mx.y && p.z >= Z.mn.z && p.z <= Z.mx.z) now.push_back((int)z);
            }
        }
        std::vector<int>& was = sv_.touching[q];
        for (int z : was) if (std::find(now.begin(), now.end(), z) == now.end()) svTouch(z, -1);
        for (int z : now) if (std::find(was.begin(), was.end(), z) == was.end()) svTouch(z, +1);
        was = now;
    }
    // A dead horde pawn frees its slot (its cost leaves the alive cost; the horde never respawns).
    int aliveCost = 0, alive = 0;
    std::vector<int> living(svData_.enemies.size(), 0);
    for (SurvivalState::Slot& s : sv_.slots) {
        if (s.inUse && !P[(size_t)s.player].alive) s.inUse = false;
        if (s.inUse) { aliveCost += svData_.enemies[(size_t)s.enemy].cost; ++alive; ++living[(size_t)s.enemy]; }
    }
    auto humansSpawned = [&] { for (const MatchPlayer& m : P) if (m.kind != ParticipantKind::Bot && m.alive) return true; return false; };
    auto enterWave = [&] {
        sv_.currentWave = std::min(sv_.currentWave + 1, (int)svData_.waves.size() - 1);   // IncrementWave (clamped)
        ++sv_.displayWave;
        sv_.usedThisWave = 0;
        sv_.phase = SurvivalState::Phase::InWave;
        sv_.phaseTime = 0.0f;
        sv_.filling = true; sv_.fillTimer = svData_.fillWaveReCheckTime;   // FillOutWave: the first enemy at once
        LOG_INFO("escalation: wave %d begins (row %d: budget %d, alive %d, deficit %d)", sv_.displayWave + 1, sv_.currentWave,
                 svData_.waves[(size_t)sv_.currentWave].budget, svData_.waves[(size_t)sv_.currentWave].budgetAlive, svData_.waves[(size_t)sv_.currentWave].deficit);
    };
    switch (sv_.phase) {
        case SurvivalState::Phase::PreGame:
            sv_.phaseTime += dt;
            if (humansSpawned() && sv_.phaseTime >= svData_.preGameTime) enterWave();
            break;
        case SurvivalState::Phase::BetweenWaves:
            sv_.phaseTime += dt;
            if (sv_.phaseTime >= svData_.timeBetweenWaves) enterWave();
            break;
        case SurvivalState::Phase::InWave: {
            bool anyHuman = false, anyAlive = false;
            for (const MatchPlayer& m : P) if (m.kind != ParticipantKind::Bot) { anyHuman = true; anyAlive = anyAlive || m.alive; }
            if (anyHuman && !anyAlive) {   // CheckLivingPlayers: every player dead -> EndGame("AllDead")
                LOG_INFO("escalation: game over at wave %d (TnSurvivalGameOverMessage %d)", sv_.displayWave + 1, sv_.displayWave + 1);
                match_.endSurvival("AllDead");
                sv_.phase = SurvivalState::Phase::GameOver;
                break;
            }
            const SurvivalData::Wave& w = svData_.waves[(size_t)sv_.currentWave];
            if (sv_.usedThisWave >= w.budget && alive == 0) {   // the budget is spent and the horde is dead -> BetweenWaves
                sv_.phase = SurvivalState::Phase::BetweenWaves;
                sv_.phaseTime = 0.0f; sv_.filling = false;
                // RoundEnded: the respawn helper respawns every dead player (revive of the living: E4).
                for (size_t q = 0; q < P.size(); ++q)
                    if (P[q].kind != ParticipantKind::Bot && !P[q].alive) match_.respawnPlayer((int)q);
                LOG_INFO("escalation: wave %d cleared (%d used); next in %.0f s", sv_.displayWave + 1, sv_.usedThisWave, svData_.timeBetweenWaves);
                break;
            }
            if (!sv_.filling && aliveCost <= w.deficit && sv_.usedThisWave < w.budget) { sv_.filling = true; sv_.fillTimer = svData_.fillWaveReCheckTime; }
            if (!sv_.filling) break;
            sv_.fillTimer += dt;
            if (sv_.fillTimer < svData_.fillWaveReCheckTime) break;
            sv_.fillTimer = 0.0f;
            if (sv_.usedThisWave >= w.budget || aliveCost >= w.budgetAlive) { sv_.filling = false; break; }
            // GenerateListOfPossibleEnemies: in the wave window (MaxWave 0 = open), below MaxCount, with a free slot; a type below
            // MinCount is chosen exclusively, else a uniform pick.
            std::vector<int> eligible;
            int forced = -1;
            for (size_t e = 0; e < svData_.enemies.size(); ++e) {
                const SurvivalData::Enemy& E = svData_.enemies[e];
                if (sv_.currentWave < E.minWave || (E.maxWave > 0 && sv_.currentWave > E.maxWave) || living[e] >= E.maxCount) continue;
                eligible.push_back((int)e);
                if (forced < 0 && living[e] < E.minCount) forced = (int)e;
            }
            if (eligible.empty()) break;
            const int e = forced >= 0 ? forced : eligible[(size_t)match_.drawInt((int)eligible.size())];
            int slot = -1;
            for (size_t s = 0; s < sv_.slots.size(); ++s) if (sv_.slots[s].enemy == e && !sv_.slots[s].inUse) { slot = (int)s; break; }
            if (slot < 0) break;
            if (!svSpawn(slot)) { ++sv_.failedSpawns; LOG_INFO("escalation: Failed to find a place to spawn a dude (%s)", svData_.enemies[(size_t)e].type.c_str()); break; }
            sv_.slots[(size_t)slot].inUse = true;
            sv_.usedThisWave += svData_.enemies[(size_t)e].cost;
            ++sv_.spawns;
            sv_.spawnOrder.push_back(e);
            break;
        }
        default: break;
    }
}

} // namespace game
