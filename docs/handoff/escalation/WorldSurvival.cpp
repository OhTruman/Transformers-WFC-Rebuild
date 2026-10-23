// Escalation (SV) E1: the director, the wave budget and the spawn finder (see game/Survival.h). RE MP_ESCALATION_RE.md §1-§2 and
// RE's TnSurvivalSpawnFinder answer (2026-10-23, C: TransContent bytecode 989-1160, SpawnDude 1318). PROV until E2-E4: the horde
// bodies / weapons / health are the versus bot presets of the horde faction with the Classic brain (the GOAP sets are not ported),
// no power / generators / downed state; spawn point positions inside the zone box until AssetTools' PossibleSpawnPoints land.
#include "game/World.h"
#include "game/Destructible.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <map>

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
        {
            Wave wv{(int)w[i]["budget"].asFloat(0), (int)w[i]["budgetAlive"].asFloat(0), (int)w[i]["deficit"].asFloat(0)};
            for (size_t b = 0; b < w[i]["buffs"].size(); ++b) {
                const std::string cls = w[i]["buffs"][b][0].asString(); const int lv = w[i]["buffs"][b][1].asInt(0);
                if (cls == "TnBuffSurvivalDecreaseDamageTaken") wv.decreaseTaken = lv;
                else if (cls == "TnBuffSurvivalIncreaseDamage") wv.increaseDealt = lv;
            }
            out.waves.push_back(wv);
        }
        const assets::Json& en = j["enemies"];
        for (size_t i = 0; i < en.size(); ++i) {
            const assets::Json& r = en[i];
            Enemy x;
            x.type = r["type"].isString() ? r["type"].strVal : std::string("?");
            x.minWave = (int)r["minWave"].asFloat(0); x.maxWave = (int)r["maxWave"].asFloat(0);
            x.minCount = (int)r["minCount"].asFloat(0); x.maxCount = (int)r["maxCount"].asFloat(0);
            x.cost = (int)r["cost"].asFloat(0); x.spawnType = std::clamp((int)r["spawnType"].asFloat(0), 0, kSvTypes - 1);
            x.power = r["power"].asFloat(0);
            x.loot = r["loot"].asString();
            x.chassis = r["chassis"].asString(); x.health = r["health"].asFloat(150.0f);
            for (size_t k = 0; k < r["robotWeapons"].size(); ++k) x.robotWeapons.push_back(r["robotWeapons"][k].asString());
            for (size_t k = 0; k < r["vehicleWeapons"].size(); ++k) x.vehicleWeapons.push_back(r["vehicleWeapons"][k].asString());
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
        const assets::Json& gs = j["generators"];
        for (size_t i = 0; i < gs.size(); ++i) {
            const assets::Json& r = gs[i];
            Generator g;
            g.id = r["id"].asString(); g.nameKey = r["nameKey"].asString(); g.volume = r["volume"].asString();
            g.cost = r["cost"].asInt(0); g.drainRate = r["drainRate"].asFloat(0.0f);
            g.location = {r["location"][0].asFloat(0), r["location"][1].asFloat(0), r["location"][2].asFloat(0)};
            g.mn = {r["min"][0].asFloat(0), r["min"][1].asFloat(0), r["min"][2].asFloat(0)};
            g.mx = {r["max"][0].asFloat(0), r["max"][1].asFloat(0), r["max"][2].asFloat(0)};
            for (size_t k = 0; k < r["planes"].size(); ++k) {
                const assets::Json& q = r["planes"][k];
                g.planes.push_back({q[0].asFloat(0), q[1].asFloat(0), q[2].asFloat(0), q[3].asFloat(0)});
            }
            for (size_t k = 0; k < r["actions"].size(); ++k) {
                const assets::Json& a = r["actions"][k];
                g.actions.push_back({a["op"].asString(), a["class"].asString(), a["target"].asString()});
            }
            out.generators.push_back(g);
        }
        const assets::Json& km = j["kismet"];
        auto vol = [&](const std::string& name) {
            for (size_t v = 0; v < out.volumes.size(); ++v) if (out.volumes[v].first == name) return (int)v;
            const assets::Json& r = km["volumes"][name];
            if (!r.isObject()) return -1;
            Shape s;
            s.mn = {r["min"][0].asFloat(0), r["min"][1].asFloat(0), r["min"][2].asFloat(0)};
            s.mx = {r["max"][0].asFloat(0), r["max"][1].asFloat(0), r["max"][2].asFloat(0)};
            for (size_t k = 0; k < r["planes"].size(); ++k) { const assets::Json& q = r["planes"][k]; s.planes.push_back({q[0].asFloat(0), q[1].asFloat(0), q[2].asFloat(0), q[3].asFloat(0)}); }
            out.volumes.push_back({name, s});
            return (int)out.volumes.size() - 1;
        };
        for (size_t k = 0; k < km["crashWalls"].size(); ++k) out.crashWalls.push_back({vol(km["crashWalls"][k][0].asString()), km["crashWalls"][k][1].asString()});
        for (size_t k = 0; k < km["roundGates"].size(); ++k) {
            RoundGate g; g.volume = vol(km["roundGates"][k][0].asString()); g.round = km["roundGates"][k][2].asInt(0);
            for (size_t z = 0; z < out.zones.size(); ++z) if (out.zones[z].id == km["roundGates"][k][1].asString()) g.zone = (int)z;
            out.roundGates.push_back(g);
        }
        if (km["window"].isObject()) {
            const assets::Json& w = km["window"];
            out.window = {vol(w["enable"].asString()), vol(w["volume"].asString()), w["wall"].asString(), w["damage"].asFloat(0)};
        }
        const assets::Json& wp = j["weaponPickups"];
        for (size_t k = 0; k < wp.size(); ++k) {
            const assets::Json& r = wp[k];
            WeaponPickup w; w.actor = r["actor"].asString(); w.weapon = r["weapon"].asString(); w.respawn = r["respawn"].asFloat(2.0f);
            w.pos = {r["pos"][0].asFloat(0), r["pos"][1].asFloat(0), r["pos"][2].asFloat(0)}; w.yaw = r["yaw"].asFloat(0);
            out.weaponPickups.push_back(w);
        }
        for (int t = 0; t < kSvTypes; ++t)
            for (size_t k = 0; k < j["initialSorted"][(size_t)t].size(); ++k) out.initialSorted[(size_t)t].push_back(j["initialSorted"][(size_t)t][k].asString());
        out.ok = !out.waves.empty() && !out.enemies.empty() && !out.zones.empty();
        return out.ok;
    }
    return false;
}

// The horde: one participant per enemy entry x MaxCount (the most that entry can have alive), spawned only by the director.
// The Escalation nav file / blockers path (AssetTools manifests; WFC_MANIFESTS overrides, as the other manifest readers).
static std::string svManifest(const std::string& map, const char* file) {
    const char* mr = std::getenv("WFC_MANIFESTS");
    return std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/maps/" + map + "/" + file;
}

void World::setupSurvival() {
    const bool hadEscNav = sv_.escNav;
    sv_ = SurvivalState{};
    if (!match_.settings().survival) {
        if (hadEscNav) { botNavTried_ = false; ensureBotNav(); LOG_INFO("escalation: the versus nav is back for %s", mapName_.c_str()); }
        return;
    }
    if (!SurvivalData::load(mapName_, svData_)) { LOG_ERROR("escalation: no Escalation data for %s", mapName_.c_str()); return; }
    ensureBotNav();
    // E2b part 3: the original NavigationCell mesh and its gating [AssetTools from escalation_navmesh.json + the generators' Kismet].
    // On by default since nav v3 (floor-lowered cells, typed links) + overlay h passed both maps' soaks (Integration 2026-10-23);
    // WFC_ESCNAV=0 (DEV / TEST) = the versus nav + the door-box blocking + the PROV spawn stand-in.
    static const bool escNavOn = !(std::getenv("WFC_ESCNAV") && std::atoi(std::getenv("WFC_ESCNAV")) == 0);
    if (escNavOn) {
        BotNav esc;
        if (esc.load(svManifest(mapName_, "bot_nav_escalation.json"))) {
            botNav_ = std::move(esc);
            sv_.escNav = true;
            std::ifstream f(svManifest(mapName_, "escalation_nav_blockers.json"), std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            assets::Json b;
            if (f.good() && assets::Json::parse(ss.str(), b)) {
                for (size_t k = 0; k < b["blocked_at_start"].size(); ++k) { botNav_.setBlocked(b["blocked_at_start"][k].asInt(-1), true); ++sv_.navBlockedAtStart; }
                for (const auto& g : b["opened_by_generator"].obj) {
                    std::vector<int> cells;
                    for (size_t k = 0; k < g.second.size(); ++k) cells.push_back(g.second[k].asInt(-1));
                    sv_.generatorCells.push_back({g.first, cells});
                }
            }
            botNav_.recomputePieces();
            LOG_INFO("escalation: nav %s: %zu cells, %d blocked at start, %zu generators open cells", "bot_nav_escalation.json", botNav_.cells().size(),
                     sv_.navBlockedAtStart, sv_.generatorCells.size());
        } else LOG_WARN("escalation: no bot_nav_escalation.json for %s - the versus nav (PROV)", mapName_.c_str());
    }
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
            // The entry's body (E3a): an iconic selection of its chassis; without one, the versus preset of the identity (PROV).
            CharacterSelection sel = id.selection;
            if (!svData_.enemies[e].chassis.empty()) { sel = CharacterSelection{}; sel.type = 1; sel.chassisId = svData_.enemies[e].chassis; }
            match_.selectCharacter(p, sel);
            preloadHeldWeaponModels(svData_.enemies[e].robotWeapons);
            if (const ChassisAssets* ca = chassisAssets(resolveChassis(sel, match_.faction(p))); ca && !ca->error.empty())
                LOG_WARN("escalation: horde body %s: %s", svData_.enemies[e].chassis.c_str(), ca->error.c_str());
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
    for (size_t z = 0; z < nz; ++z) if (svData_.zones[z].active) sv_.zonesList.push_back((int)z);   // the data is in actor order
    for (int t = 0; t < kSvTypes; ++t) svSort(t);
    // Economy: power per match player, a pool per generator. The pickup factories a generator unhides start hidden (bought, not placed).
    sv_.power.assign(match_.players().size(), 0.0f);
    sv_.pool.assign(svData_.generators.size(), 0.0f);
    sv_.generatorDone.assign(svData_.generators.size(), 0);
    sv_.generatorOff.assign(svData_.generators.size(), 0);
    sv_.nearGenerator.assign(match_.players().size(), -1);
    sv_.drainFailed.assign(match_.players().size(), 0);
    sv_.killsSeen = match_.killHistory().size();
    sv_.allowDrain.assign(match_.players().size(), 0);
    sv_.prevInteract.assign(match_.players().size(), 0);
    sv_.drainClock.assign(match_.players().size(), 0.0f);
    sv_.itemFactory.assign(svData_.generators.size(), -1);
    sv_.reofferTimer.assign(svData_.generators.size(), 0.0f);
    sv_.factoryWasAvailable.assign(svData_.generators.size(), 1);
    sv_.crashDone.assign(svData_.crashWalls.size(), 0);
    sv_.windowInside.assign(match_.players().size(), 0);
    if (svData_.window.volume >= 0)   // PreGameplaySetup: SeqAct_TnToggleDestructible 1 -> _Invulnerable
        for (Destructible* d : destructibles_) {
            const std::string& n = d->name(); const std::string& w = svData_.window.wall;
            if (n.size() >= w.size() && n.compare(n.size() - w.size(), w.size(), w) == 0) d->setInvulnerable(true);
        }
    sv_.weaponPicks.assign(svData_.weaponPickups.size(), SurvivalState::WeaponPick{});
    sv_.pickupPress.assign(match_.players().size(), 0);
    sv_.gateOn.assign(svData_.roundGates.size(), 0);
    sv_.gateInside.assign(svData_.roundGates.size(), std::vector<uint8_t>(match_.players().size(), 0));
    for (size_t g = 0; g < svData_.generators.size(); ++g)
        for (const SurvivalData::Generator::Action& a : svData_.generators[g].actions)
            if (a.op == "unhide")
                for (size_t f = 0; f < pickupFactories_.size(); ++f) if (pickupFactories_[f]->name() == a.target) sv_.itemFactory[g] = (int)f;
    int hidden = 0;
    for (const SurvivalData::Generator& g : svData_.generators)
        for (const SurvivalData::Generator::Action& a : g.actions)
            if (a.op == "unhide")
                for (PickupFactory* f : pickupFactories_) if (f->name() == a.target) { f->setHidden(true); ++hidden; }
    LOG_INFO("escalation: %zu generators, %d pickup factories hidden until bought", svData_.generators.size(), hidden);
    {   // DEV (WFC_ESCLOG): the authored spawn points our bot nav does not reach (AssetTools ESC nav coverage), log only
        static const bool nlog = std::getenv("WFC_ESCLOG") != nullptr;
        if (nlog && botNav_.valid()) {
            int off = 0, all = 0;
            for (const SurvivalData::Zone& z : svData_.zones)
                for (size_t k = 0; k < z.spawnPoints.size(); ++k) {
                    const core::Vec3 at{z.spawnPoints[k][0], z.spawnPoints[k][1], z.spawnPoints[k][2]};
                    ++all;
                    if (botNav_.findCell(at, 2.0f) < 0) { ++off; LOG_INFO("ESCLOG spawn point zone %s #%zu (%.1f %.1f %.1f) off the bot nav%s", z.id.c_str(), k, at.x, at.y, at.z, z.active ? " (active at start)" : ""); }
                }
            LOG_INFO("ESCLOG spawn points off the bot nav: %d of %d", off, all);
        }
    }
    sv_.phase = SurvivalState::Phase::PreGame;
    LOG_INFO("escalation: %s: %zu waves, %zu enemy entries, %zu zones, %zu horde slots (team %d vs players' team %d)", mapName_.c_str(),
             svData_.waves.size(), svData_.enemies.size(), nz, sv_.slots.size(), horde, svData_.coopTeam);
}

// Wall_Crashers (RE E2b §4, C): see SurvivalData::crashWalls / roundGates / window.
void World::svTickKismet() {
    const std::vector<MatchPlayer>& P = match_.players();
    auto damageWall = [&](const std::string& name, float amount) {
        for (Destructible* d : destructibles_) {   // physics.json names are object paths: match the actor name at the end
            const std::string& n = d->name();
            if (n.size() >= name.size() && n.compare(n.size() - name.size(), name.size(), name) == 0) { d->applyDamage(*this, amount); return true; }
        }
        return false;
    };
    auto anyInside = [&](int v, int who) {   // who: 0 any pawn, 1 players only, 2 horde only
        if (v < 0) return false;
        for (size_t q = 0; q < P.size(); ++q) {
            const bool bot = P[q].kind == ParticipantKind::Bot;
            if ((who == 1 && bot) || (who == 2 && !P[q].directorSpawned)) continue;
            const Character* c = participantPawn((int)q);
            if (c && svData_.volumes[(size_t)v].second.contains(c->position())) return true;
        }
        return false;
    };
    for (size_t k = 0; k < svData_.crashWalls.size(); ++k)   // SeqEvent_Touch bPlayerOnly false, once: CauseDamage 200
        if (!sv_.crashDone[k] && anyInside(svData_.crashWalls[k].volume, 0)) {
            sv_.crashDone[k] = 1;
            const bool hit = damageWall(svData_.crashWalls[k].wall, 200.0f);
            LOG_INFO("escalation: crash wall %s %s", svData_.crashWalls[k].wall.c_str(), hit ? "broken" : "NOT FOUND");
        }
    for (size_t k = 0; k < svData_.roundGates.size(); ++k) {   // SeqCond_Increment A == B at Round Begin -> the touch turns on
        const SurvivalData::RoundGate& g = svData_.roundGates[k];
        if (!sv_.gateOn[k] && sv_.roundBegins >= g.round) sv_.gateOn[k] = 1;
        if (sv_.gateInside[k].size() < P.size()) sv_.gateInside[k].resize(P.size(), 0);
        for (size_t q = 0; q < P.size(); ++q) {   // per pawn events: any Touched -> on, any UnTouched -> off
            const Character* c = P[q].kind != ParticipantKind::Bot ? participantPawn((int)q) : nullptr;
            const uint8_t in = c && g.volume >= 0 && svData_.volumes[(size_t)g.volume].second.contains(c->position()) ? 1 : 0;
            if (sv_.gateOn[k] && in != sv_.gateInside[k][q]) svSetZoneActive(g.zone, in != 0);
            sv_.gateInside[k][q] = in;
        }
    }
    const SurvivalData::Window& w = svData_.window;
    if (w.volume >= 0 && !sv_.windowDone) {
        // TV 8879 per player pawn event: Touched -> the AI touch on + vulnerable; UnTouched -> off + invulnerable [RE review W1].
        if (sv_.windowInside.size() < P.size()) sv_.windowInside.resize(P.size(), 0);
        for (size_t q = 0; q < P.size(); ++q) {
            const Character* c = P[q].kind != ParticipantKind::Bot ? participantPawn((int)q) : nullptr;
            const uint8_t in = c && w.enable >= 0 && svData_.volumes[(size_t)w.enable].second.contains(c->position()) ? 1 : 0;
            if (in != sv_.windowInside[q]) {
                sv_.windowArmed = in != 0;
                for (Destructible* d : destructibles_) {
                    const std::string& n = d->name();
                    if (n.size() >= w.wall.size() && n.compare(n.size() - w.wall.size(), w.wall.size(), w.wall) == 0) d->setInvulnerable(!sv_.windowArmed);
                }
            }
            sv_.windowInside[q] = in;
        }
        if (sv_.windowArmed && anyInside(w.volume, 2)) { sv_.windowDone = true; damageWall(w.wall, w.damage); LOG_INFO("escalation: window %s smashed by the horde", w.wall.c_str()); }
    }
}

Character* World::svMutablePawn(int p) {
    if (p == localPlayer_) return localDead_ ? nullptr : &player_.pawn();
    if ((size_t)p < oppByPlayer_.size()) { MatchOpponent* o = oppByPlayer_[(size_t)p]; return o && o->spawned() ? &o->pawn() : nullptr; }
    return nullptr;
}

// SetPlayerDefaults (TnPlayerPawnSurvival) [RE §1]: the primary and secondary weapons removed, a TnWeaponAssaultRifle given; the
// grenade bag and the abilities stay [RE E2b §5]. Every spawn.
void World::svApplySpawnLoadout(Character& c) {
    const WeaponDef* ar = findWeaponDef("AssaultRifle");
    if (!ar) return;
    std::vector<Weapon> robot{Weapon::fromDef(*ar)};
    for (int i = 0; i < c.weaponCount(); ++i) if (c.weaponAt(i).fireType == WeaponFire::Grenade) robot.push_back(c.weaponAt(i));
    c.setLoadout(robot, c.vehicleWeapons());
}

// A pickup's weapon: PostInstantiateFromPickupFactory -> SetupAmmoForDefaultPlayerPickup [C, RE E2b §1]: clip = min(DefaultPickupAmount,
// MaxAmmoClipCount), reserve = clamp(DefaultPickupAmount - clip, 0, MaxAmmoCount), from the weapon's PlayerData (Escalation's
// DesiredWeaponDataType 4 has none: the GivenTo fallback) - AssetTools manifests/escalation/escalation_weapons.json. The reserve cap
// and the clip are PlayerData's too; the other PlayerData values (damage, spread) stay the MP ones until E3b (PROV).
struct SvPickupAmmo { int amount = -1, clip = -1, maxAmmo = -1; };
static const SvPickupAmmo& svPickupAmmo(const std::string& id) {
    static std::map<std::string, SvPickupAmmo> table; static bool loaded = false;
    if (!loaded) {
        loaded = true;
        const char* mr = std::getenv("WFC_MANIFESTS");
        std::ifstream f(std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/escalation/escalation_weapons.json", std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        assets::Json j;
        if (f.good() && assets::Json::parse(ss.str(), j))
            for (const auto& kv : j["weapons"].obj) {
                const std::string pre = "TransContent.TnWeapon";
                if (kv.first.compare(0, pre.size(), pre) != 0) continue;
                const assets::Json& v = kv.second["escalation_data"]["values"];
                SvPickupAmmo a;
                a.amount = kv.second["DefaultPickupAmount"].asInt(-1);
                a.clip = v["MaxAmmoClipCount"].asInt(-1); a.maxAmmo = v["MaxAmmoCount"].asInt(-1);
                table[kv.first.substr(pre.size())] = a;
            }
        LOG_INFO("escalation: %zu pickup ammo entries (escalation_weapons.json)", table.size());
    }
    static const SvPickupAmmo none;
    auto it = table.find(id);
    return it == table.end() ? none : it->second;
}
static Weapon svPickupWeapon(const WeaponDef& d) {
    Weapon w = Weapon::fromDef(d);
    const SvPickupAmmo& a = svPickupAmmo(d.id ? d.id : "");
    if (a.amount < 0) { w.ammo = w.magSize; w.reserve = std::min(w.reserveMax, d.initialReserve); return w; }   // not in the table (PROV)
    if (a.maxAmmo >= 0) w.reserveMax = a.maxAmmo;
    if (a.clip > 0) w.magSize = a.clip;   // PlayerData MaxAmmoClipCount
    // a clip-less class (grenade bags: MaxAmmoClipCount 0) keeps its count in our clip, as Weapon::fromDef models it
    const int clipMax = a.clip > 0 ? a.clip : w.magSize;
    w.ammo = std::min(a.amount, clipMax);
    w.reserve = std::clamp(a.amount - w.ammo, 0, a.maxAmmo >= 0 ? a.maxAmmo : w.reserveMax);
    return w;
}

// Taking a weapon pickup (factory or dropped) [RE E2b §1-§3]: returns whether it was taken.
bool World::svTakeWeapon(Character& c, int player, const Weapon& got) {
    if (!got.def) return false;
    const std::string id = got.def->id;
    const int type = got.def->typeCode;   // 0 robot weapon, 1 heavy, 4 grenade bag
    for (int i = 0; i < c.weaponCount(); ++i) {   // owned: AddInventory merges -> AddAmmo(clip + reserve), reload if empty
        Weapon& w = c.weaponAt(i);
        if (!w.def || id != w.def->id) continue;
        if (w.reserve >= w.reserveMax) return false;   // DenyPickupQuery: the reserve is full
        w.reserve = std::min(w.reserveMax, w.reserve + got.ammo + got.reserve);
        if (w.ammo == 0) { const int take = std::min(w.magSize, w.reserve); w.ammo = take; w.reserve -= take; }
        ++sv_.ammoTakes;
        return true;
    }
    auto toss = [&](const Weapon& w) {   // TnDroppedPickupWeapon with the tossed weapon's clip / reserve
        if (!w.def || w.ammo + w.reserve <= 0) return;   // ShouldDestroyOnDrop: an empty weapon is destroyed, not dropped
        SurvivalState::Drop d; d.w = w; d.pos = c.position(); d.life = 120.0f;   // CampaignLifeSpan 120 s (SV skill index 1) [RE E2b §5]
        sv_.drops.push_back(d); ++sv_.tosses;
    };
    if (type == 4) {   // a grenade bag: holding a different bag -> toss it, take the new one
        for (int i = 0; i < c.weaponCount(); ++i)
            if (c.weaponAt(i).fireType == WeaponFire::Grenade) { toss(c.weaponAt(i)); c.weaponAt(i) = got; ++sv_.weaponTakes; return true; }
        c.addWeapon(got); ++sv_.weaponTakes; return true;
    }
    // DropRobotWeapon: (2) the current heavy goes; (3) a free primary / secondary slot or a heavy taken -> nothing more;
    // (4) else the current robot weapon, else the primary, else the secondary.
    if (c.tempWeapon_ == 2) { toss(c.weapon()); c.removeTempWeapon(); }
    if (type == 1) { c.grantTempWeapon(*got.def, 2, 1.0e9f); c.weapon() = got; ++sv_.weaponTakes; return true; }
    std::vector<int> robot;
    for (int i = 0; i < c.weaponCount(); ++i) if (c.weaponAt(i).fireType != WeaponFire::Grenade && c.weaponAt(i).def) robot.push_back(i);
    if (robot.size() < 2) { c.addWeapon(got); ++sv_.weaponTakes; return true; }
    int slot = robot[0];
    for (int i : robot) if (i == c.activeWeaponIndex()) slot = i;
    toss(c.weaponAt(slot));
    c.weaponAt(slot) = got;
    ++sv_.weaponTakes;
    (void)player;
    return true;
}

static void svTakeLog(const char* weapon, const char* from, size_t player) {   // DEV (WFC_ESCLOG): soak counts, log only
    static const bool tl = std::getenv("WFC_ESCLOG") != nullptr;
    if (tl) LOG_INFO("ESCLOG take %s from %s by p%zu", weapon, from, player);
}

void World::svTickWeaponPickups(float dt) {
    const std::vector<MatchPlayer>& P = match_.players();
    for (SurvivalState::WeaponPick& w : sv_.weaponPicks)   // RespawnTime 2 s
        if (!w.available && (w.respawn -= dt) <= 0.0f) w.available = true;
    for (size_t k = 0; k < sv_.drops.size();)
        if ((sv_.drops[k].life -= dt) <= 0.0f) sv_.drops.erase(sv_.drops.begin() + (long)k); else ++k;
    auto near = [](const core::Vec3& a, const core::Vec3& b) {   // the pickup cylinder (200 UU radius, 100 UU half height) vs the pawn
        const core::Vec3 d = a - b; return std::sqrt(d.x * d.x + d.z * d.z) < 2.0f && std::fabs(d.y) < 2.5f;
    };
    for (size_t q = 0; q < P.size(); ++q) {
        if (P[q].kind == ParticipantKind::Bot) continue;   // AiInventoryType None: the horde never takes one
        Character* c = svMutablePawn((int)q);
        if (!c || c->moveForm() != Form::Robot || c->isTransforming() || c->health().isDead()) continue;   // CanPickupInventory
        // Carried heavies are tossed on a swap / transform (WT_Heavy).
        if (c->tempWeapon_ == 2 && c->tempDropRequested_) {
            if (c->weapon().ammo + c->weapon().reserve > 0) { SurvivalState::Drop d; d.w = c->weapon(); d.pos = c->position(); d.life = 120.0f; sv_.drops.push_back(d); ++sv_.tosses; }
            c->removeTempWeapon(); c->tempDropRequested_ = false;
        }
        const bool press = q < sv_.pickupPress.size() && sv_.pickupPress[q];
        int bestF = -1, bestD = -1; float bestDist = 1e9f;
        for (size_t k = 0; k < svData_.weaponPickups.size(); ++k) {
            SurvivalState::WeaponPick& w = sv_.weaponPicks[k];
            if (w.hidden || !w.available || !near(svData_.weaponPickups[k].pos, c->position())) continue;
            const WeaponDef* def = findWeaponDef(svData_.weaponPickups[k].weapon.c_str());
            if (!def) continue;
            bool owned = false;
            for (int i = 0; i < c->weaponCount(); ++i) owned = owned || (c->weaponAt(i).def && std::string(c->weaponAt(i).def->id) == def->id);
            if (owned) {   // PickupWhenTouched: an owned weapon is ammo
                if (svTakeWeapon(*c, (int)q, svPickupWeapon(*def))) { w.available = false; w.respawn = svData_.weaponPickups[k].respawn; svTakeLog(def->id, "factory (owned: ammo)", q); }
                continue;
            }
            const float d = core::length(svData_.weaponPickups[k].pos - c->position());
            if (d < bestDist) { bestDist = d; bestF = (int)k; bestD = -1; }
        }
        for (size_t k = 0; k < sv_.drops.size(); ++k) {
            const SurvivalState::Drop& d = sv_.drops[k];
            if (!near(d.pos, c->position())) continue;
            // TnDroppedPickup: bPickupWhenTouched false - press only, owned or not [RE E2b §5]
            const float dist = core::length(d.pos - c->position());
            if (dist < bestDist) { bestDist = dist; bestD = (int)k; bestF = -1; }
        }
        if (!press) continue;   // TnPickupManager: the press takes the closest valid touching pickup
        if (bestF >= 0) {
            const WeaponDef* def = findWeaponDef(svData_.weaponPickups[(size_t)bestF].weapon.c_str());
            if (def && svTakeWeapon(*c, (int)q, svPickupWeapon(*def))) { sv_.weaponPicks[(size_t)bestF].available = false; sv_.weaponPicks[(size_t)bestF].respawn = svData_.weaponPickups[(size_t)bestF].respawn; svTakeLog(def->id, "factory", q); }
        } else if (bestD >= 0) {
            const Weapon w = sv_.drops[(size_t)bestD].w;
            sv_.drops.erase(sv_.drops.begin() + bestD);
            if (svTakeWeapon(*c, (int)q, w)) svTakeLog(w.def ? w.def->id : "?", "drop", q);
        }
    }
}

// A horde pawn's loadout and health at its spawn (E3a): the entry's robot / vehicle weapons and its ROBODEF health.
void World::svApplyHordeStats(Character& c, int player) {
    for (const SurvivalState::Slot& s : sv_.slots) {
        if (s.player != player) continue;
        const SurvivalData::Enemy& E = svData_.enemies[(size_t)s.enemy];
        std::vector<Weapon> robot;
        for (const std::string& w : E.robotWeapons) if (const WeaponDef* d = findWeaponDef(w)) robot.push_back(Weapon::fromDef(*d));
        if (!robot.empty() || !E.vehicleWeapons.empty()) c.setLoadout(robot, E.vehicleWeapons.empty() ? c.vehicleWeapons() : E.vehicleWeapons);
        c.health().max = E.health; c.health().current = E.health;
        // The Brute chassis' Health 100,000: body damage is irrelevant, the backpack is the kill path [C, RE section 2]
        if (s.bpStage >= 0) { c.health().max = 100000.0f; c.health().current = 100000.0f; }
        return;
    }
}

// E3b: a Brute carries the backpack (its only kill path) [C, RE ESCALATION_HORDE_AI section 2]; a Soldier Jet its boosters (section 4);
// set at each spawn.
static void svArmBackpack(SurvivalState::Slot& sl, const SurvivalData& d) {
    const bool brute = d.enemies[(size_t)sl.enemy].type.rfind("Brute_", 0) == 0;
    sl.bpStage = brute ? 0 : -1; sl.bpHealth = brute ? 25.0f : 0.0f; sl.bpCritical = -1.0f; sl.bpLastHitter = -1;
    sl.jet = d.enemies[(size_t)sl.enemy].type.rfind("Soldier_Jet", 0) == 0;
    sl.boosterHealth = sl.jet ? 125.0f : 0.0f; sl.hoverCooldown = 1.0f;
}

// WFC_ESCTEST: one horde entry of that type spawned now (outside the wave budget).
int World::svTestSpawn(const std::string& prefix) {
    for (size_t k = 0; k < sv_.slots.size(); ++k) {
        SurvivalState::Slot& sl = sv_.slots[k];
        if (sl.inUse || svData_.enemies[(size_t)sl.enemy].type.rfind(prefix, 0) != 0) continue;
        if (!svSpawn((int)k)) return -1;
        sl.inUse = true;
        svArmBackpack(sl, svData_);
        return sl.player;
    }
    return -1;
}

// E3b: a hit on a Brute (applyMatchDamage, before the pawn's own multipliers): the backpack is an attached TnDestructibleActor, so a
// hit from behind lands on it and nothing reaches the pawn [C, RE section 6]. PROV hit test: our traces stop at the pawn cylinder, so
// the direction gate stands in for the attach actor: an attacker within DamageAngle 100 deg of the backpack's facing (the Brute's back)
// hits the backpack, any other hit the body (100,000). Modifiers: AI x0 (still the LastHitInstigator), melee x0.5, radius x0.25.
// Each stage's overflow does not carry (H stock). Returns true when the backpack took the hit (the pawn takes nothing).
bool World::svBackpackHit(int victim, int instigator, float amount, const std::string& damageType) {
    for (SurvivalState::Slot& s : sv_.slots) {
        if (s.player == victim && s.jet) {   // the Jet's boosters [C, RE section 4]: powered down while walking (no damage link: the body),
                                             // powered up while hovering: 125, AI x0.5, destroyed -> TnKillAttachedPawn (RobotBooster).
            const Character* jc = participantPawn(victim);   // PROV hit test: every hit on a hovering jet lands on a booster
            if (!jc || jc->hoverState_ != 2 || s.boosterHealth <= 0.0f || instigator < 0 || instigator == victim) return false;
            float dmg = amount;
            if ((size_t)instigator < match_.players().size() && match_.players()[(size_t)instigator].kind == ParticipantKind::Bot) dmg *= 0.5f;
            s.bpLastHitter = instigator;
            if ((s.boosterHealth -= dmg) > 0.0f) return true;
            ++sv_.boosterKills;
            const int killer = !match_.sameTeam(instigator, victim) ? instigator : -1;
            svKillingBrute_ = true;
            noteHitDir(core::Vec3{0, 1, 0});
            applyMatchDamage(victim, killer, 1.0e9f, false, "TransGame.TnDamageTypeRobotBooster");
            svKillingBrute_ = false;
            return true;
        }
        if (s.player != victim || s.bpStage < 0) continue;
        if (s.bpStage == 1 || s.bpStage == 4 || s.bpStage == 2) return false;   // critical / exploding / gone: no health to take
        const Character* bc = participantPawn(victim);
        if (!bc || instigator < 0 || instigator == victim) return false;
        const core::Vec3 from = match_.playerLocation(instigator);
        core::Vec3 d = from - bc->position(); d.y = 0.0f;
        const core::Vec3 back = core::forwardFromYawPitch(bc->yaw(), 0.0f) * -1.0f;
        const float l = core::length(d);
        if (l < 1e-3f || core::dot(d * (1.0f / l), back) < std::cos(100.0f * 3.14159265f / 180.0f)) { ++sv_.bpRefused; return false; }   // the body
        s.bpLastHitter = instigator;
        float dmg = amount;
        if ((size_t)instigator < match_.players().size() && match_.players()[(size_t)instigator].kind == ParticipantKind::Bot) dmg = 0.0f;   // AI x 0
        if (damageType.find("Melee") != std::string::npos) dmg *= 0.5f;
        if (svRadiusHit_) dmg *= 0.25f;
        ++sv_.bpHits;
        if (dmg <= 0.0f) return true;
        if ((s.bpHealth -= dmg) > 0.0f) return true;
        static const int next[6] = {5, -1, -1, 1, -1, 3};   // 0 -> 5 -> 3 -> 1
        const int ns = next[s.bpStage];
        s.bpStage = ns;
        if (ns == 5) s.bpHealth = 50.0f;            // first damage (smoke)
        else if (ns == 3) s.bpHealth = 75.0f;       // overloading: DeathImminent L1 (React_To_Backpack_Damage)
        else if (ns == 1) s.bpCritical = 3.0f;      // critical: DeathImminent L2 (ExtinguishBackpack); auto explode after 3.0 s
        static const bool slog = std::getenv("WFC_ESCLOG") != nullptr;
        if (slog) LOG_INFO("ESCLOG backpack p%d stage %d (hit by p%d)", victim, ns, instigator);
        return true;
    }
    return false;
}

// The backpack explosion: HmRadialDamageDestructionEffect -> HurtRadius(500, 4000 UU, TnDamageTypeBruteBackpack, momentum 600,000,
// InstigatedBy = LastHitInstigator), falloff (1 - clamp(d - r, 0, R) / R)^2, an unobstructed trace; then TnKillAttachedPawn [C, RE 2 / 6].
// The usual team rule applies (BruteBackpack is not TnDamageTypeAOE): the horde and the instigator are hurt, other players are not.
void World::svBackpackExplode(SurvivalState::Slot& s) {
    const Character* bc = participantPawn(s.player);
    s.bpStage = 4;
    if (!bc) { s.bpStage = 2; return; }
    const core::Vec3 at = bc->actorLocation();
    const std::string type = "TransGame.TnDamageTypeBruteBackpack";
    const float R = 40.0f;
    for (size_t q = 0; q < match_.players().size(); ++q) {
        if ((int)q == s.player) continue;
        const Character* c = participantPawn((int)q);
        if (!c) continue;
        const float dd = std::clamp(core::length(c->actorLocation() - at) - c->cylinderRadius(c->moveForm()), 0.0f, R);
        const float k = (1.0f - dd / R) * (1.0f - dd / R);
        if (k > 0.0f && radiusVisible(at, c->actorLocation())) { noteHitDir(c->actorLocation() - at); applyMatchDamage((int)q, s.bpLastHitter, 500.0f * k, false, type); }
    }
    ++sv_.bpExplosions;
    static const bool slog = std::getenv("WFC_ESCLOG") != nullptr;
    if (slog) LOG_INFO("ESCLOG backpack p%d explodes (instigator p%d)", s.player, s.bpLastHitter);
    // TnKillAttachedPawn: the Brute dies by the backpack, credited to the last hitter (a horde hitter: no killer)
    const int killer = s.bpLastHitter >= 0 && !match_.sameTeam(s.bpLastHitter, s.player) ? s.bpLastHitter : -1;
    s.bpStage = 2;
    svKillingBrute_ = true;
    noteHitDir(core::Vec3{0, 1, 0});
    applyMatchDamage(s.player, killer, 1.0e9f, false, type);
    svKillingBrute_ = false;
}

// E3b part 2: loot on a horde death [C, RE ESCALATION_HORDE_AI section 5 + the ESCALATION_WAVES correction]: the spawn-list row's
// LootClassification - Soldier / CQC rows 4 TnAbilityResourcePickups, Brute / Titan 7, the boss Tank 12 plus a 50 % rare GatlingGun /
// HeavyRocketTurret (1 : 1); GENERIC (the Jet row), SpiderDec, HoloBrute nothing; every kill +1, +1 for melee, +1 for an explosion
// (+5 headshot: no headshot state yet, PROV). The pickups scatter around the body (PROV ring) and live 60 s (PROV).
void World::svDropLoot(const KillFeedEntry& e) {
    for (const SurvivalState::Slot& s : sv_.slots) {
        if (s.player != e.victim) continue;
        const std::string& lc = svData_.enemies[(size_t)s.enemy].loot;
        int n = 0;
        if (lc == "SoldierDec" || lc == "SoldierAut") n = 4;
        else if (lc == "BruteDec" || lc == "BruteAut" || lc == "TitanDec" || lc == "TitanAut") n = 7;
        else if (lc == "SoldierTankDec") n = 12;
        n += 1;
        if (e.damageType.find("Melee") != std::string::npos) n += 1;
        if (e.damageType.find("Rocket") != std::string::npos || e.damageType.find("Grenade") != std::string::npos ||
            e.damageType.find("Explo") != std::string::npos || e.damageType.find("Plasma") != std::string::npos) n += 1;
        const core::Vec3 at = match_.playerLocation(e.victim);
        for (int k = 0; k < n; ++k) {
            const float a = 6.2831853f * (float)k / (float)n;
            SurvivalState::Loot l; l.pos = at + core::Vec3{std::cos(a) * 1.5f, 0.0f, std::sin(a) * 1.5f}; l.life = 60.0f;
            sv_.loot.push_back(l);
        }
        sv_.lootDropped += n;
        if (lc == "SoldierTankDec" && match_.drawFloat() < 0.5f)
            if (const WeaponDef* d = findWeaponDef(match_.drawInt(2) == 0 ? "GatlingGun" : "HeavyRocketTurret")) {
                SurvivalState::Drop dr; dr.w = svPickupWeapon(*d); dr.pos = at; dr.life = 120.0f;
                sv_.drops.push_back(dr); ++sv_.rareDrops;
            }
        return;
    }
}

// The wave buffs on damage: a horde victim's DecreaseDamageTaken, a horde instigator's IncreaseDamage.
float World::svDamageMul(int victim, int instigator) const {
    float m = 1.0f;
    for (const SurvivalState::Slot& s : sv_.slots) {
        if (s.player == victim) m *= s.takenMul;
        if (s.player == instigator && instigator != victim) m *= s.dealtMul;
    }
    return m;
}

float World::svZoneScore(int z, int t) const {
    return sv_.current[(size_t)z][(size_t)t] + sv_.universal[(size_t)z] + svData_.zones[(size_t)z].self[(size_t)t];
}

// The original binary's CRT qsort (MSVC qsort.c: CUTOFF 8 shortsort moving the max to the end, median of three, equal-skipping
// partition, smaller side first). Unstable but deterministic: equal scores end in the original's order. Port of RE tools/msvc_qsort.py.
template <class Cmp> static void msvcQsort(std::vector<int>& a, Cmp comp) {
    const int n = (int)a.size();
    if (n < 2) return;
    auto sw = [&](int i, int j) { std::swap(a[(size_t)i], a[(size_t)j]); };
    auto A = [&](int i) { return a[(size_t)i]; };
    std::vector<int> lostk, histk;
    int lo = 0, hi = n - 1;
    for (;;) {
        const int size = hi - lo + 1;
        bool recursed = false;
        if (size <= 8) {
            for (int h = hi; h > lo; --h) {
                int m = lo;
                for (int p = lo + 1; p <= h; ++p) if (comp(A(p), A(m)) > 0) m = p;
                sw(m, h);
            }
        } else {
            int mid = lo + size / 2;
            if (comp(A(lo), A(mid)) > 0) sw(lo, mid);
            if (comp(A(lo), A(hi)) > 0) sw(lo, hi);
            if (comp(A(mid), A(hi)) > 0) sw(mid, hi);
            int loguy = lo, higuy = hi;
            for (;;) {
                if (mid > loguy) { ++loguy; while (loguy < mid && comp(A(loguy), A(mid)) <= 0) ++loguy; }
                if (mid <= loguy) { ++loguy; while (loguy <= hi && comp(A(loguy), A(mid)) <= 0) ++loguy; }
                --higuy;
                while (higuy > mid && comp(A(higuy), A(mid)) > 0) --higuy;
                if (higuy < loguy) break;
                sw(loguy, higuy);
                if (mid == higuy) mid = loguy;
            }
            ++higuy;
            if (mid < higuy) { --higuy; while (higuy > mid && comp(A(higuy), A(mid)) == 0) --higuy; }
            if (mid >= higuy) { --higuy; while (higuy > lo && comp(A(higuy), A(mid)) == 0) --higuy; }
            if (higuy - lo >= hi - loguy) {
                if (lo < higuy) { lostk.push_back(lo); histk.push_back(higuy); }
                if (loguy < hi) { lo = loguy; recursed = true; }
            } else {
                if (loguy < hi) { lostk.push_back(loguy); histk.push_back(hi); }
                if (lo < higuy) { hi = higuy; recursed = true; }
            }
        }
        if (recursed) continue;
        if (lostk.empty()) return;
        lo = lostk.back(); lostk.pop_back(); hi = histk.back(); histk.pop_back();
    }
}

// HmArraySorter.SortArray for one type: SortedIndices rebuilt from the Zones order, then qsort with CompareZoneScores (descending).
void World::svSort(int t) {
    std::vector<int>& s = sv_.sorted[(size_t)t];
    s = sv_.zonesList;
    msvcQsort(s, [&](int a, int b) { const float x = svZoneScore(a, t), y = svZoneScore(b, t); return x < y ? 1 : (x > y ? -1 : 0); });
}

// NotifyZoneActivated (AddItem at the end) / NotifyZoneDeactivated (RemoveItem), then SortAllArrays [RE E1 review F2, C].
void World::svSetZoneActive(int z, bool on) {
    if (z < 0 || (size_t)z >= svData_.zones.size() || svData_.zones[(size_t)z].active == on) return;
    svData_.zones[(size_t)z].active = on;
    if (on) sv_.zonesList.push_back(z);
    else sv_.zonesList.erase(std::remove(sv_.zonesList.begin(), sv_.zonesList.end(), z), sv_.zonesList.end());
    for (int k = 0; k < kSvTypes; ++k) svSort(k);
}

// Interact held this step by match player p (the seat command's held bit online and for remote seats; the local controller offline).
bool World::svInteractHeld(int p) const {
    if (p == localPlayer_ && !onlineSeats_) return player_.controller().interactHeld();
    return (size_t)p < seatInteract_.size() && seatInteract_[(size_t)p] != 0;
}

// TnSeqEvent_SurvivalGenerator "New Power Level" -> the actions AssetTools resolved. Kinds not built yet (nav cells, dynamic blocking
// volumes, weapon pickup factories, path nodes: E2b) are counted per purchase.
void World::svFireGenerator(int gi, bool levelDrop) {
    const SurvivalData::Generator& g = svData_.generators[(size_t)gi];
    int done = 0, pending = 0;
    bool doors = false;
    for (const SurvivalData::Generator::Action& a : g.actions) {
        bool ok = false;
        if (a.cls.find("PickupFactory") != std::string::npos && a.cls != "TnWeaponPickupFactory" && (a.op == "unhide" || a.op == "hide")) {
            for (PickupFactory* f : pickupFactories_) if (f->name() == a.target) { f->setHidden(a.op == "hide"); ok = true; }
        } else if (a.cls == "TnWeaponPickupFactory" || a.cls == "TnPickupFactory") {
            for (size_t k = 0; k < svData_.weaponPickups.size(); ++k)
                if (svData_.weaponPickups[k].actor == a.target) { sv_.weaponPicks[k].hidden = a.op == "hide"; ok = true; }
        } else if (a.cls == "TnTransformingWorldActor") {
            const int i = transforming_.find(a.target);
            if (i >= 0) {
                if (a.op == "transform" || a.op == "untransform") { transforming_.setDesired((size_t)i, a.op == "transform" ? 1 : 0); doors = true; ok = true; }
                else if (a.op == "unhide" || a.op == "hide") { transforming_.setShown((size_t)i, a.op == "unhide"); ok = true; }
                else if (a.op == "collide") { transforming_.setCollide((size_t)i, true); doors = true; ok = true; }
            }
        } else if (a.cls == "TnSurvivalZoneVolume") {
            for (size_t z = 0; z < svData_.zones.size(); ++z)
                if ("TnSurvivalZoneVolume_" + svData_.zones[z].id == a.target) { svSetZoneActive((int)z, a.op == "on" || (a.op == "toggle" && !svData_.zones[z].active)); ok = true; }
        } else if (a.cls == "TriggerVolume" && a.op == "off") {   // a generator's own ActivationVolume turned off: no longer usable
            for (size_t k = 0; k < svData_.generators.size(); ++k) {
                const std::string& v = svData_.generators[k].volume;
                if (v.size() >= a.target.size() && v.compare(v.size() - a.target.size(), a.target.size(), a.target) == 0) sv_.generatorOff[k] = 1;
            }
            ok = true;
        }
        ok ? ++done : ++pending;
    }
    if (sv_.escNav) {   // the original's gating: this generator's SeqAct_Toggle Turn On NavigationCells
        bool opened = false;
        for (const auto& gc : sv_.generatorCells)
            if (gc.first == "TnSurvivalGenerator_" + g.id) { for (int c : gc.second) botNav_.setBlocked(c, false); opened = true; }
        if (opened) botNav_.recomputePieces();
    } else if (doors && botNav_.valid()) { transforming_.applyNavBlocks(botNav_); botNav_.recomputePieces(); }   // versus nav: door boxes
    if (!levelDrop) ++sv_.purchases;
    LOG_INFO("escalation: generator %s (%s, %d) %s: %d actions applied, %d not built yet (E2b)", g.id.c_str(), g.nameKey.c_str(), g.cost,
             levelDrop ? "level drop (Taken)" : "bought", done, pending);
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
        static const bool slog = std::getenv("WFC_ESCLOG") != nullptr;   // DEV: every spawn try
        if (slog) LOG_INFO("ESCLOG try %d type %d zone %s score %.0f points %d (top: %s %.0f / %s %.0f / %s %.0f)", attempt, t, zone.id.c_str(), svZoneScore(z, t), zone.points,
                           svData_.zones[(size_t)sorted[0]].id.c_str(), svZoneScore(sorted[0], t), top > 1 ? svData_.zones[(size_t)sorted[1]].id.c_str() : "-",
                           top > 1 ? svZoneScore(sorted[1], t) : 0.0f, top > 2 ? svData_.zones[(size_t)sorted[2]].id.c_str() : "-", top > 2 ? svZoneScore(sorted[2], t) : 0.0f);
        if (zone.points <= 0) continue;   // GetSpawnPoint: none (an error log in the original), the try is spent
        int idx = sv_.pointIndex[(size_t)z] % zone.points;
        sv_.pointIndex[(size_t)z] = (idx + 1) % zone.points;
        // PROV (Integration 2026-10-23, removed with E2b part 3's Escalation nav): about half the authored points are off our bot nav
        // (the horde would stand stranded there); until the nav covers them the next on-nav point of this zone stands in (round
        // robin), logged; no on-nav point in the zone -> the try is spent.
        const SurvivalData::Zone* pz = &zone;   // the zone whose point is used (PROV stand-in below)
        if (botNav_.valid() && (size_t)idx < zone.spawnPoints.size()) {
            // reachable = on a cell of the main connected piece (the Escalation nav still loads as several pieces: AssetTools asked for
            // its drop links; a perch outside the main piece would strand the horde)
            auto onNav = [&](const SurvivalData::Zone& zz, int k) {
                const auto& q = zz.spawnPoints[(size_t)k];
                const int c = botNav_.findCell(core::Vec3{q[0], q[1], q[2]}, 2.0f);
                return c >= 0 && botNav_.pieceOf(c) == botNav_.mainPieceId();
            };
            if (!onNav(zone, idx)) {
                int sub = -1;
                for (int k = 1; k < (int)zone.spawnPoints.size() && sub < 0; ++k) if (onNav(zone, (idx + k) % (int)zone.spawnPoints.size())) sub = (idx + k) % (int)zone.spawnPoints.size();
                std::string how;
                if (sub >= 0) { idx = sub; how = "#" + std::to_string(sub) + " of the zone stands in"; }
                else {   // no on-nav point in the zone: the nearest on-nav authored point of any active zone
                    const core::Vec3 c = (zone.mn + zone.mx) * 0.5f; float best = 1e18f; int bz = -1, bk = -1;
                    for (int az : sv_.zonesList) {
                        const SurvivalData::Zone& zz = svData_.zones[(size_t)az];
                        for (int k = 0; k < (int)zz.spawnPoints.size(); ++k) {
                            if (!onNav(zz, k)) continue;
                            const auto& q = zz.spawnPoints[(size_t)k];
                            const float d = (q[0] - c.x) * (q[0] - c.x) + (q[1] - c.y) * (q[1] - c.y) + (q[2] - c.z) * (q[2] - c.z);
                            if (d < best) { best = d; bz = az; bk = k; }
                        }
                    }
                    if (bz < 0) { LOG_INFO("escalation: PROV spawn point zone %s #%d is off the bot nav and no active zone has an on-nav point: the try is spent", zone.id.c_str(), idx); continue; }
                    pz = &svData_.zones[(size_t)bz]; idx = bk; how = "zone " + pz->id + " #" + std::to_string(bk) + " (nearest on-nav point) stands in";
                }
                LOG_INFO("escalation: PROV spawn point zone %s is not nav-reachable: %s", zone.id.c_str(), how.c_str());
            }
        }
        core::Vec3 at; float yaw = 0.0f;
        if ((size_t)idx < pz->spawnPoints.size()) {
            const auto& q = pz->spawnPoints[(size_t)idx];
            at = {q[0], q[1], q[2]}; yaw = q[3];
        } else {   // PROV: point idx of n spread around the box centre until the authored TnSurvivalSpawnPoint positions arrive
            const float cx = 0.5f * (zone.mn.x + zone.mx.x), cz = 0.5f * (zone.mn.z + zone.mx.z);
            const float rad = std::min(4.0f, 0.25f * std::min(zone.mx.x - zone.mn.x, zone.mx.z - zone.mn.z));
            const float a = 6.2831853f * (float)idx / (float)std::max(1, zone.points);
            at = {cx + rad * std::cos(a), zone.mx.y, cz + rad * std::sin(a)};
        }
        float gy = 0.0f; core::Vec3 gn;
        if (!collision_.valid() || !collision_.groundHeight(at.x, at.z, at.y + 0.5f, 0.0f, gy, gn)) {
            if (slog) LOG_INFO("ESCLOG   point %d (%.2f %.2f %.2f): no ground below", idx, at.x, at.y, at.z);
            continue;
        }
        if ((size_t)idx >= pz->spawnPoints.size() && gy < zone.mn.y - 2.0f) continue;   // PROV point fell out of the zone
        at.y = gy;
        bool clear = true;   // IsSafeSpawnLocation (capsule): no live pawn within two radii + 1 m while the cylinders overlap in height
        for (size_t q = 0; q < match_.players().size() && clear; ++q) {
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const core::Vec3 d = c->position() - at;
            if (std::fabs(d.y) < 4.0f && std::sqrt(d.x * d.x + d.z * d.z) < 2.0f + c->cylinderRadius(c->moveForm()) + 1.0f) clear = false;
        }
        if (!clear) { if (slog) LOG_INFO("ESCLOG   point %d (%.2f %.2f %.2f): occupied", idx, at.x, at.y, at.z); continue; }
        // Face the nearest player (PROV: the point's own rotation once the authored points land).
        if ((size_t)idx >= pz->spawnPoints.size()) {
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
    // TnSurvivalGame.ElapsedTime counts from level start (the global Tick, PendingMatch included) [RE E1 review F1].
    if (sv_.phase == SurvivalState::Phase::PreGame) sv_.phaseTime += dt;
    if (match_.state() != Match::State::InProgress) return;
    const std::vector<MatchPlayer>& P = match_.players();
    if (sv_.power.size() < P.size()) {
        sv_.power.resize(P.size(), 0.0f); sv_.nearGenerator.resize(P.size(), -1); sv_.drainFailed.resize(P.size(), 0);
        sv_.allowDrain.resize(P.size(), 0); sv_.prevInteract.resize(P.size(), 0); sv_.drainClock.resize(P.size(), 0.0f);
    }
    svTickKismet();
    svTickWeaponPickups(dt);
    // Item generators (map Kismet per generator, PickupStatusChange Taken): Hide; ModifySurvivalGeneratorPower(-cost); a level drop
    // (1 -> 0) fires "New Power Level" again - the event fires on negative changes too - so its actions re-apply (UnHide + the volume
    // off); Delay 2 s -> the volume Turn On. After the first purchase the item thus stays out as a normal respawning factory [RE E2a
    // review G1, C]; a take at level 0 only hides it until the generator is bought again.
    for (size_t g = 0; g < svData_.generators.size(); ++g) {
        const int f = sv_.itemFactory[g];
        if (f < 0) continue;
        PickupFactory& F = *pickupFactories_[(size_t)f];
        const bool avail = F.available() && !F.hidden();
        if (sv_.factoryWasAvailable[g] && !avail && !F.hidden()) {   // Taken
            F.setHidden(true);
            const bool drop = sv_.generatorDone[g] != 0;
            sv_.pool[g] = std::max(0.0f, sv_.pool[g] - (float)svData_.generators[g].cost);
            sv_.generatorDone[g] = 0;
            if (drop) svFireGenerator((int)g, true);
            sv_.reofferTimer[g] = 2.0f;
        }
        sv_.factoryWasAvailable[g] = (F.available() && !F.hidden()) ? 1 : 0;
        if (sv_.reofferTimer[g] > 0.0f && (sv_.reofferTimer[g] -= dt) <= 0.0f) { sv_.reofferTimer[g] = 0.0f; sv_.generatorOff[g] = 0; }
    }
    // Killed: the killer's PRI gains the enemy's PowerReward (players only; clamped 0..9999).
    for (const auto& k = match_.killHistory(); sv_.killsSeen < k.size(); ++sv_.killsSeen) {
        const KillFeedEntry& e = k[sv_.killsSeen];
        static const bool klog = std::getenv("WFC_ESCLOG") != nullptr;   // DEV: every kill (soak counts), log only
        if (klog) {
            std::string victim = e.victim >= 0 && (size_t)e.victim < P.size() ? P[(size_t)e.victim].name : std::string("?");
            for (const SurvivalState::Slot& s : sv_.slots) if (s.player == e.victim) victim = svData_.enemies[(size_t)s.enemy].type;
            LOG_INFO("ESCLOG kill %s by p%d %s t %.1f", victim.c_str(), e.killer, e.damageType.c_str(), match_.matchTime());
        }
        svDropLoot(e);
        if (e.killer < 0 || (size_t)e.killer >= P.size() || P[(size_t)e.killer].kind == ParticipantKind::Bot) continue;
        for (const SurvivalState::Slot& s : sv_.slots)
            if (s.player == e.victim) sv_.power[(size_t)e.killer] = std::min(9999.0f, sv_.power[(size_t)e.killer] + svData_.enemies[(size_t)s.enemy].power);
    }
    // Generators (per occupying player pawn, so two players add up, each paying from their own power): _AllowDraining is reset on
    // entering the ActivationVolume, set by an Interact press inside, cleared on release; while set, each 30 Hz tick drains
    // int(max(1, tick x DrainRate)) capped at the remaining cost and the player's power into the shared pool. 0 drained: the fail
    // sound. At the cost the generator fires its one level [RE E2 §3-§4].
    for (size_t q = 0; q < P.size(); ++q) {
        const int was = sv_.nearGenerator[q];
        sv_.nearGenerator[q] = -1; sv_.drainFailed[q] = 0;
        const bool held = P[q].kind != ParticipantKind::Bot && svInteractHeld((int)q);
        const bool press = held && !sv_.prevInteract[q];
        if (sv_.pickupPress.size() <= q) sv_.pickupPress.resize(q + 1, 0);
        sv_.pickupPress[q] = press ? 1 : 0;
        sv_.prevInteract[q] = held ? 1 : 0;
        if (P[q].kind == ParticipantKind::Bot) continue;
        const Character* c = participantPawn((int)q);
        if (!c) { sv_.allowDrain[q] = 0; continue; }
        for (size_t g = 0; g < svData_.generators.size(); ++g)
            if (!sv_.generatorDone[g] && !sv_.generatorOff[g] && svData_.generators[g].contains(c->position())) { sv_.nearGenerator[q] = (int)g; break; }
        const int g = sv_.nearGenerator[q];
        if (g != was) { sv_.allowDrain[q] = 0; sv_.drainClock[q] = 0.0f; }   // entering (or leaving): _AllowDraining = false
        if (g < 0) continue;
        if (press) sv_.allowDrain[q] = 1;
        if (!held) sv_.allowDrain[q] = 0;
        if (!sv_.allowDrain[q]) continue;
        const SurvivalData::Generator& G = svData_.generators[(size_t)g];
        const float rate = G.drainRate > 0.0f ? G.drainRate : kSvDefaultDrainRate;
        for (sv_.drainClock[q] += dt; sv_.drainClock[q] >= kSvDrainTick - 1e-6f && !sv_.generatorDone[(size_t)g]; sv_.drainClock[q] -= kSvDrainTick) {
            const float need = (float)G.cost - sv_.pool[(size_t)g];
            const float amt = std::min({std::floor(std::max(1.0f, kSvDrainTick * rate)), need, std::floor(sv_.power[q])});
            if (amt <= 0.0f) { sv_.drainFailed[q] = 1; break; }   // DrainingFailedSound, beam off
            sv_.power[q] -= amt; sv_.pool[(size_t)g] += amt;
            if (sv_.pool[(size_t)g] >= (float)G.cost) {
                sv_.generatorDone[(size_t)g] = 1;
                static const bool blog = std::getenv("WFC_ESCLOG") != nullptr;
                if (blog) LOG_INFO("ESCLOG buy %s (%s) by p%zu cost %d t %.1f", G.id.c_str(), G.nameKey.c_str(), q, G.cost, match_.matchTime());
                svFireGenerator(g);
            }
        }
    }
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
    // E3b part 2: the Jet's HoverFromTactical (<= 6 s; ShootingStrafeFromHover every 2-3 s) [C actions, PROV trigger]: a horde jet with a
    // target jumps into a hover when its timer runs out; the hover itself is the chassis' (HoverJumpHeight / HoverDuration).
    for (SurvivalState::Slot& s : sv_.slots) {
        if (!s.inUse || !s.jet || (s.hoverCooldown -= dt) > 0.0f) continue;
        const Character* jc = participantPawn(s.player);
        for (BotBrain& br : bots_)
            if (br.player == s.player && jc && jc->onGround() && br.target >= 0) {
                br.pendingHover = true; ++sv_.hovers;
                s.hoverCooldown = 6.0f + 2.0f + match_.drawFloat();
            }
    }
    // Loot: a player pawn touching a pickup (1.5 m) takes +3 ability resource; unclaimed pickups expire.
    for (size_t k = 0; k < sv_.loot.size();) {
        SurvivalState::Loot& l = sv_.loot[k];
        bool taken = false;
        for (size_t q = 0; q < P.size() && !taken; ++q) {
            if (P[q].kind == ParticipantKind::Bot) continue;
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const core::Vec3 d = c->position() - l.pos;
            if (d.x * d.x + d.z * d.z < 1.5f * 1.5f && std::fabs(d.y) < 3.0f) {
                if (sv_.abilityResource.size() < P.size()) sv_.abilityResource.resize(P.size(), 0.0f);
                sv_.abilityResource[q] += 3.0f; ++sv_.lootTaken; taken = true;
            }
        }
        if (taken || (l.life -= dt) <= 0.0f) { l = sv_.loot.back(); sv_.loot.pop_back(); } else ++k;
    }
    for (SurvivalState::Slot& s : sv_.slots)   // E3b: the backpack's critical state explodes after 3.0 s (or with the extinguish anim's end: PROV 3 s)
        if (s.inUse && s.bpStage == 1 && (s.bpCritical -= dt) <= 0.0f) svBackpackExplode(s);
    // PROV (E3, until the GOAP sets): the original's threat sensor keeps every player known (SeeEnemy at spawn, RE §9.1); the Classic
    // brain forgets an unseen target after its memory, so once a second each horde bot is told where the nearest player is.
    if (simTick_ % 60 == 0)
        for (const SurvivalState::Slot& s : sv_.slots) {
            if (!s.inUse) continue;
            const Character* hc = participantPawn(s.player);
            if (!hc) continue;
            int bq = -1; float best = 1e18f;
            for (size_t q = 0; q < P.size(); ++q) {
                if (match_.sameTeam((int)q, s.player)) continue;   // every player-team participant (our team bots too)
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                const core::Vec3 d = c->position() - hc->position();
                const float l = d.x * d.x + d.y * d.y + d.z * d.z;
                if (l < best) { best = l; bq = (int)q; }
            }
            if (bq < 0) continue;
            for (BotBrain& br : bots_)
                if (br.player == s.player) {
                    BotBrain::Seen& sn = br.seen[bq];
                    if (!sn.visible) { sn.pos = participantPawn(bq)->actorLocation(); sn.time = match_.matchTime(); }
                    if (br.target < 0) br.target = bq;
                }
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
        ++sv_.roundBegins;   // TnSeqEvent_SurvivalEvents "Round Begin" (ActivateSequenceEvent 1) [RE E1 review F3]
        levelAudioEvent("Game:TnSeqEvent_SurvivalEvents:Round Begin");   // the map sequence (announcer / dialog / mixer: LevelAudioScript)
        LOG_INFO("escalation: wave %d begins (row %d: budget %d, alive %d, deficit %d)", sv_.displayWave + 1, sv_.currentWave,
                 svData_.waves[(size_t)sv_.currentWave].budget, svData_.waves[(size_t)sv_.currentWave].budgetAlive, svData_.waves[(size_t)sv_.currentWave].deficit);
    };
    switch (sv_.phase) {
        case SurvivalState::Phase::PreGame:
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
                std::string last = "none";   // the last player death (kills are not logged elsewhere)
                for (auto it = match_.killHistory().rbegin(); it != match_.killHistory().rend(); ++it)
                    if (it->victim >= 0 && (size_t)it->victim < P.size() && P[(size_t)it->victim].kind != ParticipantKind::Bot) {
                        last = P[(size_t)it->victim].name + " by " + (it->killer >= 0 && (size_t)it->killer < P.size() ? P[(size_t)it->killer].name : std::string("world")) +
                               " (" + it->damageType + ") at " + std::to_string((int)it->time) + " s";
                        break;
                    }
                LOG_INFO("escalation: game over at wave %d (TnSurvivalGameOverMessage %d); last player death: %s", sv_.displayWave + 1, sv_.displayWave + 1, last.c_str());
                match_.endSurvival("AllDead");
                sv_.phase = SurvivalState::Phase::GameOver;
                break;
            }
            const SurvivalData::Wave& w = svData_.waves[(size_t)sv_.currentWave];
            if (sv_.usedThisWave >= w.budget && alive == 0) {   // the budget is spent and the horde is dead -> BetweenWaves
                sv_.phase = SurvivalState::Phase::BetweenWaves;
                ++sv_.roundEnds;   // "Round End" (ActivateSequenceEvent 0)
                levelAudioEvent("Game:TnSeqEvent_SurvivalEvents:Round End");
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
            svArmBackpack(sv_.slots[(size_t)slot], svData_);
            {   // the wave's buffs, applied at spawn: DecreaseDamageTaken L0-4 x0.9..0.5, IncreaseDamage L0-4 x1.25..2.25 [RE §2]
                const SurvivalData::Wave& wv = svData_.waves[(size_t)sv_.currentWave];
                sv_.slots[(size_t)slot].takenMul = wv.decreaseTaken >= 0 ? 0.9f - 0.1f * (float)wv.decreaseTaken : 1.0f;
                sv_.slots[(size_t)slot].dealtMul = wv.increaseDealt >= 0 ? 1.25f + 0.25f * (float)wv.increaseDealt : 1.0f;
            }
            sv_.usedThisWave += svData_.enemies[(size_t)e].cost;
            ++sv_.spawns;
            sv_.spawnOrder.push_back(e);
            break;
        }
        default: break;
    }
}

} // namespace game
