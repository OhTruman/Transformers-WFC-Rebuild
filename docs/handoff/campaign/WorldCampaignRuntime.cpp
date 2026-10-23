// Campaign runtime step 3: the level Kismet's pawn factories (SeqAct_TnPawnFactory over AssetTools next_map/<CH>_pawn_factories.json),
// the spawned AI's alert-to-player awareness, and the in-map character pick (the human takes a trio member, the squad is the rest).
// See Campaign.h.
#include "game/World.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {

namespace {

bool readJsonFile(const std::string& path, assets::Json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), j);
}
std::string manifestsRoot() { const char* mr = std::getenv("WFC_MANIFESTS"); return mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests"; }
std::string afterDot(const std::string& s) { const size_t k = s.find_last_of('.'); return k == std::string::npos ? s : s.substr(k + 1); }
bool fileExists(const std::string& p) { return (bool)std::ifstream(p, std::ios::binary); }

// The AI's body: AssetTools' campaign export of the AICD (out/campaign_characters/<id>, "CMP_<id>"), else the Escalation one
// (out/escalation_characters/<id>, "ESC_<id>", PROV); an Autobot / Decepticon
// variant without its own export borrows the other faction's (PROV, logged); none -> "" (the factory is held).
std::string bodyForAicd(const std::string& archetype, std::string& note) {
    std::string id = afterDot(archetype);
    const std::string pkg = archetype.substr(0, archetype.find('.'));
    if (id.size() > 5 && id.compare(id.size() - 5, 5, "_AICD") == 0) id.resize(id.size() - 5);
    const std::string cmp = "F:/Transformers Rebuild/AssetTools/out/campaign_characters/";
    if (fileExists(cmp + id + "/character.json")) return "CMP_" + id;   // the campaign template
    if (fileExists(cmp + id + "__" + pkg + "/character.json")) return "CMP_" + id + "__" + pkg;   // a name in two packages: id__<package>
    const std::string root = "F:/Transformers Rebuild/AssetTools/out/escalation_characters/";
    if (fileExists(root + id + "/character.json")) return "ESC_" + id;
    for (const auto& sw : {std::make_pair(std::string("_Autobot"), std::string("_Decepticon")), std::make_pair(std::string("_Decepticon"), std::string("_Autobot"))}) {
        const size_t k = id.find(sw.first);
        if (k == std::string::npos) continue;
        std::string alt = id; alt.replace(k, sw.first.size(), sw.second);
        if (fileExists(root + alt + "/character.json")) { note = "PROV: " + alt + "'s body"; return "ESC_" + alt; }
    }
    return {};
}

} // namespace

void World::cpLoadFactories() {
    cp_.factories.clear();
    assets::Json j;
    if (!readJsonFile(manifestsRoot() + "/next_map/" + cpChapter_.chapter + "_pawn_factories.json", j)) {
        LOG_WARN("campaign: no pawn factories for %s", cpChapter_.chapter.c_str());
        return;
    }
    const int enemyTeam = cpChapter_.faction == 0 ? 1 : 0;
    int pool = 0;
    const assets::Json& fs = j["pawn_factories"];
    for (size_t i = 0; i < fs.size(); ++i) {
        const assets::Json& f = fs[i];
        CampaignFactory F;
        F.action = f["action"].asString();
        F.op = cp_.vm.find(F.action);
        F.cls = f["factory"]["class"].asString();
        F.aicd = afterDot(f["factory"]["archetype"].asString());
        F.ai = F.cls == "TnActorFactoryAI";
        const std::string ft = f["factory"]["FactionTeam"].asString();
        F.team = ft.find("Autobots") != std::string::npos ? 0 : ft.find("Decepticons") != std::string::npos ? 1 : enemyTeam;
        F.vehicle = f["factory"]["StartingFormOverride"].asString() == "TF_Vehicle";
        F.alert = f["factory"]["AlertToPlayer"].asBool(true);
        F.count = std::max(1, f["spawn_count"].asInt(1));
        F.delay = f["spawn_delay"].asFloat(0.5f);
        F.spawnedVar = f["spawned_variable"].asString();
        for (size_t k = 0; k < f["outputs"].size(); ++k) F.outputs.push_back(f["outputs"][k]["output"].asString());
        const assets::Json& sp = f["spawn_points"];
        for (size_t k = 0; k < sp.size(); ++k) {   // glTF metres Y-up = 0.01 x (x, z, y)_UE
            const assets::Json& L = sp[k]["location_ue"];
            if (L.size() >= 3) F.points.push_back(core::Vec3{L[0].asFloat() * 0.01f, L[2].asFloat() * 0.01f, L[1].asFloat() * 0.01f});
        }
        if (F.ai) {
            F.chassis = bodyForAicd(f["factory"]["archetype"].asString(), F.note);
            if (F.chassis.empty()) F.note = "no character export for " + F.aicd;
        }
        // The pool: participants per body, shared by every factory of that body (never respawned; directorSpawned keeps them out of
        // the match start): up to 8 alive per body, 96 in all (PROV: the original spawns pawns on demand).
        std::vector<int>& shared = cp_.bodyPools[F.chassis];
        const int target = std::min(8, (int)shared.size() + F.count);
        if (F.ai && !F.chassis.empty())
            for (int c = (int)shared.size(); c < target && pool < 96; ++c, ++pool) {
                const int p = match_.addPlayerExact(F.aicd + " " + std::to_string(cp_.factories.size() + 1) + "." + std::to_string(c + 1), F.team);
                MatchPlayer& mp = match_.playerMutable(p);
                mp.kind = ParticipantKind::Bot;
                mp.directorSpawned = true;
                CharacterSelection sel; sel.type = 1; sel.chassisId = F.chassis;
                match_.selectCharacter(p, sel);
                chassisAssets(F.chassis);
                auto o = std::make_unique<MatchOpponent>(p, mp.team, true);
                addBotBrain(p, std::clamp(cp_.difficulty, 0, 2));
                if (smart_.size() <= (size_t)p) smart_.resize((size_t)p + 1);
                smart_[(size_t)p] = SmartBot{};   // Classic (the campaign GOAP sets are a later step)
                opponents_.push_back(o.get());
                actors_.push_back(std::move(o));
                shared.push_back(p);
            }
        if (F.ai && !F.chassis.empty()) F.pool = shared;   // (refreshed below once every factory has grown its body's pool)
        if (!F.note.empty()) LOG_INFO("campaign: factory %s (%s): %s", afterDot(F.action).c_str(), F.aicd.c_str(), F.note.c_str());
        cp_.factories.push_back(F);
    }
    for (CampaignFactory& F : cp_.factories) if (F.ai && !F.chassis.empty()) F.pool = cp_.bodyPools[F.chassis];
    rebuildOppIndex();
    LOG_INFO("campaign: %zu pawn factories, %d AI participants pooled in %zu bodies", cp_.factories.size(), pool, cp_.bodyPools.size());
}

// SeqAct_TnPawnFactory [C data, RE CAMPAIGN_KISMET_HANDLERS]: Spawn Actor -> spawn_count pawns at the spawn points (in order, cycling),
// spawn_delay apart; the spawned variable names the last pawn; "All Pawns Spawned" after the last. A factory without a body (no export,
// or the Omega batteries: destructibles) is held latent (PROV, logged once).
bool World::cpPawnFactoryActivate(KismetVM& vm, int op, int input) {
    for (CampaignFactory& F : cp_.factories) {
        if (F.op != op) continue;
        KismetVM::Op& o = vm.op(op);
        const bool desFactory = F.cls == "TnActorFactoryOmegaBatteryDestructible";
        if (desFactory && input == 4) {   // Kill All: the factory's destructibles leave the world
            for (int di : F.spawnedDes) cpRemoveDestructible(di);
            F.spawnedDes.clear();
            return true;
        }
        if (F.cls == "TnActorFactorySentryAI") return cpSentryFactoryActivate(vm, op, input, F);
        if (F.cls == "TnActorFactoryRollerMine") return cpMineFactoryActivate(vm, op, input, F);
        if (input != 0) return true;   // Stop / other inputs: nothing to do yet
        if (desFactory && !F.points.empty()) {   // a destructible factory: the blueprint at its spawn point
            // named after its sequence + op (the four D5 battery sequences share the op name), the Spawned variable = "des:<i>"
            std::string seq = F.action.substr(0, F.action.find_last_of('.'));
            seq = seq.substr(seq.find_last_of('.') + 1);
            for (int c = 0; c < F.count; ++c) {
                const int di = cpSpawnDestructible(F.aicd, seq + "_" + afterDot(F.action) + "_" + std::to_string(c), F.points[(size_t)c % F.points.size()], 0.0f);
                if (di < 0) continue;
                cpBindDesEvents(di);
                F.spawnedDes.push_back(di);
                for (int v : vm.varLink(op, "Spawned")) if (KismetVM::Var* var = vm.var(v)) var->obj = "des:" + std::to_string(di);
            }
            vm.output(op, std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end() ? "All Pawns Spawned" : "Out");
            return true;
        }
        if (F.pool.empty() || F.points.empty()) {
            if (!F.heldLogged) { F.heldLogged = true; LOG_INFO("campaign: factory %s held (%s)", afterDot(F.action).c_str(), F.note.empty() ? "no spawn points" : F.note.c_str()); }
            o.latent = true; o.timer = -1.0f;
            return true;
        }
        F.pending = F.count; F.timer = 0.0f;
        o.latent = true; o.timer = -1.0f;
        return true;
    }
    return false;
}

void World::cpTickFactories(float dt) {
    for (CampaignFactory& F : cp_.factories) {
        if (F.pending <= 0 || (F.timer -= dt) > 0.0f) continue;
        int p = -1;
        for (int q : F.pool) if (!match_.players()[(size_t)q].alive) { p = q; break; }
        if (p < 0) { F.pending = 0; continue; }   // every pooled pawn alive (MaxSpawnedInWorld)
        const core::Vec3 at = F.points[(size_t)(F.spawned % (int)F.points.size())];
        float yaw = 0.0f;
        if (const Character* lead = cpLeader(p)) { const core::Vec3 d = lead->position() - at; yaw = std::atan2(-d.x, -d.z); }
        match_.restartPlayerAt(p, at, yaw);
        if (F.vehicle) cp_.formPending.push_back(p);
        ++F.spawned; ++cp_.enemiesSpawned;
        F.live.push_back(p);
        LOG_INFO("campaign: factory %s spawned p%d %s at (%.1f %.1f %.1f)", afterDot(F.action).c_str(), p, F.chassis.c_str(), at.x, at.y, at.z);
        if (!F.spawnedVar.empty())
            for (int v = 0; v < cp_.vm.varCount(); ++v)
                if (KismetVM::Var* var = cp_.vm.var(v); var && var->varName == F.spawnedVar) var->obj = "pawn:" + std::to_string(p);
        if (F.op >= 0) {   // the factory's Spawned variable + its "Spawned" output for each pawn
            for (int v : cp_.vm.varLink(F.op, "Spawned")) if (KismetVM::Var* var = cp_.vm.var(v)) var->obj = "pawn:" + std::to_string(p);
            if (std::find(F.outputs.begin(), F.outputs.end(), "Spawned") != F.outputs.end()) cp_.vm.output(F.op, "Spawned");
        }
        if (--F.pending > 0) { F.timer = F.delay; continue; }
        if (F.op >= 0) {
            const bool all = std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end();
            cp_.vm.finishLatent(F.op, all ? "All Pawns Spawned" : (F.outputs.empty() ? "Out" : F.outputs.front()));
        }
    }
    // StartingFormOverride TF_Vehicle: the pawn changes form once it stands (the step after its spawn)
    for (size_t k = 0; k < cp_.formPending.size();) {
        Character* c = participantPawnMutable(cp_.formPending[k]);
        if (c) { c->setForm(Form::Vehicle); cp_.formPending[k] = cp_.formPending.back(); cp_.formPending.pop_back(); }
        else ++k;
    }
    // AlertToPlayer: the spawned AI knows the nearest participant of the other team (as the Escalation horde's threat sensor, PROV)
    if (simTick_ % 60 != 0) return;
    for (const CampaignFactory& F : cp_.factories) {
        if (!F.alert) continue;
        for (int p : F.pool) {
            const Character* me = participantPawn(p);
            if (!me) continue;
            int bq = -1; float best = 1e18f;
            for (size_t q = 0; q < match_.players().size(); ++q) {
                if (match_.sameTeam((int)q, p)) continue;
                const Character* c = participantPawn((int)q);
                if (!c) continue;
                const core::Vec3 d = c->position() - me->position();
                const float l = d.x * d.x + d.y * d.y + d.z * d.z;
                if (l < best) { best = l; bq = (int)q; }
            }
            if (bq < 0) continue;
            for (BotBrain& br : bots_)
                if (br.player == p) {
                    BotBrain::Seen& sn = br.seen[bq];
                    if (!sn.visible) { sn.pos = participantPawn(bq)->actorLocation(); sn.time = match_.matchTime(); }
                    if (br.target < 0) br.target = bq;
                }
        }
    }
}

// The in-map picks (CharacterSelect_GFX -> Customize.SelectCharacter), for 1-3 humans in seat order: each human keeps its pick unless an
// earlier human holds it, then it takes the next free trio member (Integration's rule for co-op duplicates; the original greys taken
// members out); the AI squad is the trio minus every human's character (SetupAiSquad again on each change): a member holding a human's
// character swaps to a free one in place, surplus members leave (AiSquad.RemoveMember) [C rule, RE CAMPAIGN_SQUAD_AI section 1].
void World::cpSyncPicks() {
    if (!cp_.active || cp_.trioChassis.empty()) return;
    std::vector<int> humans;
    for (size_t q = 0; q < match_.players().size(); ++q)
        if (match_.players()[q].kind != ParticipantKind::Bot && !match_.players()[q].dormant) humans.push_back((int)q);
    std::string key;
    for (int h : humans) key += std::to_string(h) + "=" + resolveChassis(match_.players()[(size_t)h].selection, match_.faction(h)) + ";";
    if (key == cp_.humanChassis) return;
    cp_.humanChassis = key;
    auto iconic = [](const std::string& ch) { CharacterSelection sel; sel.type = 1; sel.chassisId = ch; return sel; };
    std::vector<std::string> taken;
    for (int h : humans) {
        std::string ch = resolveChassis(match_.players()[(size_t)h].selection, match_.faction(h));
        const bool inTrio = std::find(cp_.trioChassis.begin(), cp_.trioChassis.end(), ch) != cp_.trioChassis.end();
        if (!inTrio || std::find(taken.begin(), taken.end(), ch) != taken.end()) {
            for (const std::string& t : cp_.trioChassis) if (std::find(taken.begin(), taken.end(), t) == taken.end()) { ch = t; break; }
            match_.selectCharacter(h, iconic(ch));
            LOG_INFO("campaign: human p%d takes %s (the next free trio member)", h, ch.c_str());
        }
        taken.push_back(ch);
    }
    std::vector<size_t> freeIdx;   // the trio members no human holds, trio order
    for (size_t t = 0; t < cp_.trioChassis.size(); ++t)
        if (std::find(taken.begin(), taken.end(), cp_.trioChassis[t]) == taken.end()) freeIdx.push_back(t);
    std::vector<uint8_t> used(cp_.trioChassis.size(), 0);
    std::vector<int> keep; std::vector<std::string> keepCh;
    std::vector<size_t> redo;
    for (size_t k = 0; k < cp_.squad.size(); ++k) {   // members already holding a free member keep it
        size_t t = cp_.trioChassis.size();
        for (size_t f : freeIdx) if (!used[f] && cp_.trioChassis[f] == cp_.squadChassis[k]) { t = f; break; }
        if (t < cp_.trioChassis.size()) { used[t] = 1; keep.push_back(cp_.squad[k]); keepCh.push_back(cp_.squadChassis[k]); }
        else redo.push_back(k);
    }
    for (size_t k : redo) {
        const int p = cp_.squad[k];
        size_t t = cp_.trioChassis.size();
        for (size_t f : freeIdx) if (!used[f]) { t = f; break; }
        MatchPlayer& mp = match_.playerMutable(p);
        if (t >= cp_.trioChassis.size()) {   // more AI than free members: RemoveMember
            for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p && o->spawned()) o->despawn();
            mp.alive = false;
            LOG_INFO("campaign: squad p%d leaves (every trio member is taken)", p);
            continue;
        }
        used[t] = 1;
        const std::string& ch = cp_.trioChassis[t];
        match_.selectCharacter(p, iconic(ch));
        mp.name = cpChapter_.trio[t];
        chassisAssets(ch);
        for (MatchOpponent* o : opponents_)
            if (o->matchPlayer() == p && o->spawned() && applyChassisToPawn(o->pawn(), ch)) applyCharacterTo(o->pawn(), &mp.selection, &mp);
        keep.push_back(p); keepCh.push_back(ch);
        LOG_INFO("campaign: squad p%d is now %s (%s)", p, mp.name.c_str(), ch.c_str());
    }
    cp_.squad = keep; cp_.squadChassis = keepCh;
}

// ---- the AI ops (SeqAct_TnAi*): targets, destinations, objectives ----

// An actor's location: a spawned pawn ("pawn:<p>"), AssetTools' actors.json (when delivered), else navigation.json's NavigationPoints.
bool World::cpActorPos(const std::string& path, core::Vec3& out) const {
    if (path.rfind("pawn:", 0) == 0) { if (const Character* c = participantPawn(std::atoi(path.c_str() + 5))) { out = c->position(); return true; } return false; }
    const std::string key = afterDot(path);
    auto it = cp_.actorPos.find(key);
    if (it == cp_.actorPos.end()) return false;
    out = it->second;
    return true;
}

void World::cpLoadActors() {
    cp_.actorPos.clear();
    std::map<std::string, std::array<int, 2>> hiddenRows;   // the actor's object path (the renderer's key: extras.actor_path) -> rows visible / hidden
    cp_.actorHidden.clear(); cp_.actorDestroyed.clear();
    bool actorsFile = false;
    for (const char* file : {"navigation.json", "actors.json"}) {
        assets::Json j;
        if (!readJsonFile(cpChapter_.dir + file, j)) continue;
        actorsFile |= std::string(file) == "actors.json";
        const assets::Json& ns = j[std::string(file) == "actors.json" ? "actors" : "nodes"];
        for (size_t i = 0; i < ns.size(); ++i) {
            const assets::Json& n = ns[i];
            const std::string name = afterDot(n.has("object") ? n["object"].asString() : n["node"].asString());
            const assets::Json& L = n["location_gltf"];
            if (!name.empty() && L.size() >= 3) cp_.actorPos[name] = core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
            if (!name.empty() && n["rotation_deg"].has("yaw")) cp_.actorYaw[name] = n["rotation_deg"]["yaw"].asFloat(0.0f);
            if (!name.empty() && std::string(file) == "actors.json") {   // the authored start visibility (Toggle's base)
                // drawable classes only: lights, volumes, sounds, brushes, nav points and notes carry bHidden by class default (an editor
                // sprite), not as "not drawn" - a hide on a light's name would switch it off in the renderer
                const std::string cls = n["class"].asString();
                const bool drawable = cls.find("MeshActor") != std::string::npos || cls.find("InterpActor") != std::string::npos ||
                                      cls.find("KActor") != std::string::npos || cls.find("TransformingWorldActor") != std::string::npos;
                const bool hid = drawable && n["props"]["bHidden"].asBool(false);
                ++hiddenRows[n.has("object") ? n["object"].asString() : name][hid ? 1 : 0];
            }
        }
    }
    LOG_INFO("campaign: %zu actor locations (navigation.json%s)", cp_.actorPos.size(), actorsFile ? " + actors.json" : "");
    // the authored bHidden: the renderer's actor nodes start visible, so the hidden ones are hidden here, keyed by the full object path
    // (Rendering fd7ca625 matches it against the node's extras.actor_path; short names repeat across a chapter's levels). The flag
    // survives streaming; ToggleHidden / Destroy flip the same flag. A row without an object path keys by its short name.
    int conflicts = 0;
    for (const auto& kv : hiddenRows) {
        if (kv.second[1] > 0 && kv.second[0] == 0) cp_.actorHidden[kv.first] = true;
        else if (kv.second[1] > 0) ++conflicts;
    }
    if (conflicts > 0) LOG_INFO("campaign: %d actor names hidden in one level and visible in another: left visible", conflicts);
    int hid = 0;
    if (renderer_) for (const auto& kv : cp_.actorHidden) if (kv.second) { renderer_->setActorHidden(kv.first, true); ++hid; }
    LOG_INFO("campaign: %zu actors authored hidden (%d sent)", cp_.actorHidden.size(), hid);
}

// The match players an op's link names: TnSeqVar_Buddy (BuddyIndex k = the k-th squad member, 1-based; none = every member), a pawn
// object ("pawn:<p>", a factory's spawned variable), SeqVar_Player (every human).
std::vector<int> World::cpTargets(KismetVM& vm, int op, const std::string& link) {
    std::vector<int> out;
    for (int i : vm.varLink(op, link)) {
        const KismetVM::Var* v = vm.var(i);
        if (!v) continue;
        if (v->named >= 0) v = vm.var(v->named);
        if (!v) continue;
        if (v->cls == "TnSeqVar_Character") {   // the participants playing that ROBODEF
            for (int p : cpCharacterTargets(v->chassisPath)) out.push_back(p);
        } else if (v->cls == "TnSeqVar_Buddy") {   // FindBuddy [C]: BuddyIndex 1-based (default 1) over the ACTIVE members in squad order
            const int bi = std::max(1, v->buddyIndex);
            std::vector<int> active;
            for (int p : cp_.squad) if (match_.players()[(size_t)p].alive) active.push_back(p);
            if ((size_t)bi <= active.size()) out.push_back(active[(size_t)bi - 1]);
        } else if (v->cls == "SeqVar_Player") {
            for (size_t q = 0; q < match_.players().size(); ++q) if (match_.players()[q].kind != ParticipantKind::Bot) out.push_back((int)q);
        } else if (v->obj.rfind("pawn:", 0) == 0) out.push_back(std::atoi(v->obj.c_str() + 5));
    }
    return out;
}

// TnAiController.RangeSet [C, RE batch 3.5] -> the arrival radius (the band's outer edge, metres): Touch 0-175, Striking -600, Close
// -1500, Medium -3000, Far -5000, Retreated -7000 UU; SetMoveToObjective's default is RANGE_Touch.
static float rangeRadius(const std::string& r) {
    if (r == "RANGE_Striking") return 6.0f;
    if (r == "RANGE_Close") return 15.0f;
    if (r == "RANGE_Medium") return 30.0f;
    if (r == "RANGE_Far" || r == "RANGE_Long") return 50.0f;
    if (r == "RANGE_Retreated") return 70.0f;
    return 1.75f;
}

// SeqAct_TnAiMoveToActor (one move, In), SeqAct_TnAiSetMoveToObjective (Enable / Disable, Range), SeqAct_TnAiUseInteract (Use / Abort /
// End: the AI paths to the interact, uses it in reach - Begin Use on its bPlayerOnly-false events - End Use when its sequence ends or on
// Abort / End; "Out" at once, "AI Command Receive" when AI-driven), SeqAct_TnAiShootAtActor (Enable / Disable / Toggle: a persistent
// forced fire target until Disable), SeqAct_TnAiUseTurret (Stay In Turret / Can Leave Turret; PROV: no mounting yet - the AI holds at
// the turret, fighting from there, until Can Leave) [C, RE batch 3.1-3.3].
bool World::cpAiOpActivate(KismetVM& vm, int op, int input) {
    KismetVM::Op& o = vm.op(op);
    const std::string& c = o.cls;
    const std::vector<int> who = cpTargets(vm, op, "Target");
    if (c == "SeqAct_TnAiShootAtActor") {
        const std::string at = vm.objectOf(op, "Shoot At Actor");
        for (int p : who) {
            const bool on = input == 0 ? true : input == 1 ? false : cp_.shootAt.find(p) == cp_.shootAt.end();
            if (on && !at.empty()) cp_.shootAt[p] = at; else cp_.shootAt.erase(p);
        }
        LOG_INFO("campaign: %s: %zu AI %s %s", afterDot(o.path).c_str(), who.size(), input == 1 ? "stop shooting" : "shoot at", afterDot(at).c_str());
        if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
        return true;
    }
    if (c == "SeqAct_TnAiUseTurret") {
        const std::string tur = vm.objectOf(op, "Turret Object");
        core::Vec3 at;
        for (int p : who) {
            if (input == 0 && !tur.empty() && cpActorPos(tur, at)) cp_.objectives[p] = {at, 1.75f, true};
            else cp_.objectives.erase(p);
        }
        if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
        return true;
    }
    const bool move = c == "SeqAct_TnAiMoveToActor", objective = c == "SeqAct_TnAiSetMoveToObjective", interact = c == "SeqAct_TnAiUseInteract";
    if (!move && !objective && !interact) return false;
    std::string dest = vm.objectOf(op, move ? "Destination" : objective ? "Objective Point" : "Interact Object");
    if (dest.empty() && interact) dest = o.authored["Interact"].asString();
    if (interact) {
        const std::string nm = afterDot(dest);
        for (int p : who) {
            if (input == 0) cp_.aiUse[p] = nm;
            else {   // Abort / End Interact: the release
                cp_.aiUse.erase(p);
                for (CampaignInteract& I : cp_.interacts) if (I.user == p) { cpInteractFire(I, "End Use", p); I.user = -1; }
            }
        }
        vm.output(op, "Out");
        bool ai = false;
        for (int p : who) ai |= match_.players()[(size_t)p].kind == ParticipantKind::Bot;
        if (ai) vm.output(op, "AI Command Receive");
        if (input != 0) return true;
    }
    core::Vec3 at{0, 0, 0};
    const bool found = !dest.empty() && cpActorPos(dest, at);
    if (objective && input == 1) { for (int p : who) cp_.objectives.erase(p); }
    else if (found) {
        // (at least 3 m: our nav arrival is not exact to the Touch band's 1.75 m - a squad member short of it would wait there for good)
        const float r = std::max(3.0f, interact ? 1.75f : rangeRadius(o.authored["Range"].asString()));
        for (int p : who) cp_.objectives[p] = {at, r, objective};
    } else if (!dest.empty()) LOG_INFO("campaign: %s: destination %s not located (actors.json pending)", afterDot(o.path).c_str(), afterDot(dest).c_str());
    if (!interact && !o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

// The AI interact uses in progress: in the interact's use range (and it enabled, free) -> Begin Use; the interact tick ends it.
void World::cpTickAiUse() {
    for (auto it = cp_.aiUse.begin(); it != cp_.aiUse.end();) {
        const Character* c = participantPawn(it->first);
        CampaignInteract* I = nullptr;
        for (CampaignInteract& x : cp_.interacts) if (x.name == it->second) I = &x;
        if (!c || !I || !match_.players()[(size_t)it->first].alive) { it = cp_.aiUse.erase(it); continue; }
        const float dx = I->pos.x - c->position().x, dz = I->pos.z - c->position().z;
        const float reach = std::max(4.0f, I->useRange);
        if (I->enabled && I->user < 0 && dx * dx + dz * dz <= reach * reach) {
            I->user = it->first; I->t = 0.0f;
            cpInteractFire(*I, "Begin Use", it->first);
            cp_.objectives.erase(it->first);
            it = cp_.aiUse.erase(it);
            continue;
        }
        ++it;
    }
}

// SeqAct_TnAiShootAtActor's forced target for this AI: the actor's location (a destructible: its bounds' centre; a pawn).
bool World::cpShootAtPoint(int player, core::Vec3& out) const {
    auto it = cp_.shootAt.find(player);
    if (it == cp_.shootAt.end()) return false;
    const std::string nm = afterDot(it->second);
    for (const CampaignDestructible& d : cp_.destructibles) if (!d.gone && d.name == nm) { out = (d.lo + d.hi) * 0.5f; return true; }
    if (it->second.rfind("des:", 0) == 0) {
        const int i = std::atoi(it->second.c_str() + 4);
        if (i >= 0 && (size_t)i < cp_.destructibles.size() && !cp_.destructibles[(size_t)i].gone) { out = (cp_.destructibles[(size_t)i].lo + cp_.destructibles[(size_t)i].hi) * 0.5f; return true; }
        return false;
    }
    return cpActorPos(it->second, out);
}

// A checkpoint's authored AuxilliarySpawnPoints (AssetTools <CH>/checkpoint_spawns.json) for the squad: the k-th (0-based).
bool World::cpAuxSpawn(const std::string& actor, int k, core::Vec3& out) {
    if (!cp_.auxLoaded) {
        cp_.auxLoaded = true;
        assets::Json j;
        if (readJsonFile(cpChapter_.dir + "checkpoint_spawns.json", j)) {
            const assets::Json& rows = j["rows"];
            for (size_t r = 0; r < rows.size(); ++r) {
                std::vector<core::Vec3>& pts = cp_.auxSpawns[rows[r]["actor"].asString()];
                const assets::Json& ap = rows[r]["auxilliary_spawn_points"];
                for (size_t a = 0; a < ap.size(); ++a) {
                    const assets::Json& L = ap[a]["location_gltf"];
                    if (L.size() >= 3) pts.push_back(core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()});
                }
            }
        }
    }
    auto it = cp_.auxSpawns.find(actor);
    if (it == cp_.auxSpawns.end() || k < 0 || (size_t)k >= it->second.size()) return false;
    out = it->second[(size_t)k];
    return true;
}

// A Kismet objective outranks following (below Kill): the bot goes to it; a one-shot move ends on arrival.
bool World::cpObjectiveGoal(BotBrain& b, BotGoal& g) {
    auto it = cp_.objectives.find(b.player);
    if (it == cp_.objectives.end()) return false;
    const Character* me = participantPawn(b.player);
    if (me && !it->second.persistent) {
        const core::Vec3 d = me->position() - it->second.pos;
        if (d.x * d.x + d.z * d.z < it->second.radius * it->second.radius) { cp_.objectives.erase(it); return false; }
    }
    g = BotGoal{};
    g.kind = BotGoalKind::Defend; g.pos = botSnap(it->second.pos); g.radius = it->second.radius; g.target = -1;
    b.mission = true;
    return true;
}

// ---- streaming (SeqAct_SetStreamingState) ----

// The visible units of a LevelStreamingState (chapter.json visible_in_states; the persistent unit always). Streaming for real
// (Integration + Rendering 2026-10-23): a unit becoming visible is loaded then (loadChapterUnits({unit}): a hitch, accepted) unless it
// is already, one becoming hidden is hidden and unloaded (unloadChapterUnit). Collision stays preloaded for every unit (PROV).
// Started and Finished fire at once [C op semantics; PROV: the load time is the hitch, not a latent wait].
void World::cpApplyStreamingState(const std::string& state) {
    cp_.streamingState = state;
    int loaded = 0, unloaded = 0;
    for (const std::string& u : cpChapter_.allUnits) {
        if (u == cpChapter_.persistentDir) continue;
        auto it = cpChapter_.unitStates.find(u);
        const bool vis = it != cpChapter_.unitStates.end() && std::find(it->second.begin(), it->second.end(), state) != it->second.end();
        const bool isLoaded = cpUnitLoaded_.count(u) != 0;
        if (vis && !isLoaded) {
            if (renderer_) renderer_->loadChapterUnits(cpChapter_.dir, std::vector<std::string>{u});
            cpUnitLoaded_.insert(u); ++loaded;
        }
        auto was = cp_.unitVisible.find(u);
        if (was == cp_.unitVisible.end() || was->second != vis) {
            cp_.unitVisible[u] = vis;
            if (renderer_ && (vis || isLoaded)) renderer_->setChapterUnitVisible(u, vis);
        }
        if (!vis && isLoaded) {
            if (renderer_) renderer_->unloadChapterUnit(u);
            cpUnitLoaded_.erase(u); ++unloaded;
        }
    }
    LOG_INFO("campaign: streaming state %s (%d units loaded, %d unloaded, %zu resident)", state.c_str(), loaded, unloaded, cpUnitLoaded_.size());
}

bool World::cpSetStreamingState(KismetVM& vm, int op) {
    cpApplyStreamingState(afterDot(vm.op(op).authored["StreamingState"].asString()));
    vm.output(op, "Started"); vm.output(op, "Finished");
    return true;
}

// TnSeqAct_ForceIntoForm [C, RE 2026-10-23]: ForcedForm (TF_Robot default / TF_Vehicle); each target not in it transforms the regular way
// (PC.ForceToForm / AiController.Transform, with the animation); latent until every target is in the form ("Finished"), a target neither
// in it nor transforming is dropped (AbortFor). PROV: MoveToSafeTransformationLocation is not modelled.
bool World::cpForceIntoForm(KismetVM& vm, int op) {
    KismetVM::Op& o = vm.op(op);
    CampaignState::FormOp F;
    F.op = op; F.vehicle = o.authored["ForcedForm"].asString() == "TF_Vehicle";
    for (int p : cpTargets(vm, op, "Target")) {
        Character* c = participantPawnMutable(p);
        if (!c) continue;
        const Form want = F.vehicle ? Form::Vehicle : Form::Robot;
        if (c->moveForm() != want && !c->isTransforming()) c->beginTransform();
        F.targets.push_back(p);
    }
    o.latent = true; o.timer = -1.0f;
    cp_.formOps.push_back(F);
    return true;
}

void World::cpTickForms() {
    for (size_t k = 0; k < cp_.formOps.size();) {
        CampaignState::FormOp& F = cp_.formOps[k];
        const Form want = F.vehicle ? Form::Vehicle : Form::Robot;
        bool all = true;
        for (size_t t = 0; t < F.targets.size();) {
            const Character* c = participantPawn(F.targets[t]);
            if (c && c->moveForm() == want && !c->isTransforming()) { ++t; continue; }
            if (!c || !c->isTransforming()) { F.targets.erase(F.targets.begin() + (long)t); continue; }   // AbortFor
            all = false; ++t;
        }
        if (all) { cp_.vm.finishLatent(F.op, "Finished"); F = cp_.formOps.back(); cp_.formOps.pop_back(); } else ++k;
    }
}

// Restart From Last Checkpoint (CheckpointManager.RestartFromActiveCheckpoint, the same map): the launch again at the active checkpoint -
// AI destroyed, every participant respawned at the checkpoint, the level Kismet begun again with that CP (PROV: the map is not reloaded,
// world state set by Kismet is rebuilt by the relaunch). Host only.
bool World::cpRestartFromCheckpoint() {
    if (!cp_.active && cpLaunch_.map.empty()) return false;
    MatchLaunch l = cpLaunch_;
    l.checkpoint = cp_.checkpoint;
    LOG_INFO("campaign: restart from checkpoint %s", l.checkpoint.empty() ? "(the chapter start)" : l.checkpoint.c_str());
    return launchMatch(l);
}

// ---- touches (SeqEvent_Touch on TriggerVolumes / Triggers) ----

// Every SeqEvent_Touch the level audio does not host (Systems fires those): its Originator's shape - a TriggerVolume brush (the units'
// physics.json polygons, convex) or a Trigger (CylinderComponent 40 x 40 UU x DrawScale, actors.json) [H stock defaults]. bPlayerOnly
// (CDO true) -> humans only; Touched on entry, UnTouched on exit, Empty when the last toucher leaves.
void World::cpLoadTouches() {
    cp_.touches.clear();
    std::map<std::string, std::vector<std::vector<core::Vec3>>> brushes;
    std::vector<std::string> files;   // every unit's physics.json + the chapter root's (it also covers levels without a render unit:
    for (const std::string& u : cpChapter_.allUnits) files.push_back(cpChapter_.dir + u + "/physics.json");   // the _Audio_m zones)
    files.push_back(cpChapter_.dir + "physics.json");
    for (const std::string& file : files) {
        assets::Json pj;
        if (!readJsonFile(file, pj)) continue;
        const assets::Json& vs = pj["volumes"];
        for (size_t i = 0; i < vs.size(); ++i) {
            const assets::Json& P = vs[i]["polygons_gltf"];
            if (P.size() == 0) continue;
            std::vector<std::vector<core::Vec3>> polys;
            for (size_t k = 0; k < P.size(); ++k) {
                std::vector<core::Vec3> poly;
                for (size_t q = 0; q < P[k].size(); ++q) poly.push_back(core::Vec3{P[k][q][0].asFloat(), P[k][q][1].asFloat(), P[k][q][2].asFloat()});
                if (poly.size() >= 3) polys.push_back(poly);
            }
            brushes[afterDot(vs[i]["actor"].asString())] = polys;
        }
    }
    std::map<std::string, std::pair<core::Vec3, float>> triggers;   // Trigger: location, DrawScale
    {
        assets::Json aj;
        if (readJsonFile(cpChapter_.dir + "actors.json", aj))
            for (size_t i = 0; i < aj["actors"].size(); ++i) {
                const assets::Json& r = aj["actors"][i];
                if (r["class"].asString() != "Trigger" || r["location_gltf"].size() < 3) continue;
                const assets::Json& L = r["location_gltf"];
                triggers[r["actor"].asString()] = {core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()}, r["DrawScale"].asFloat(1.0f)};
            }
    }
    int missing = 0;
    for (int i = 0; i < (int)cp_.vm.opCount(); ++i) {
        const KismetVM::Op& o = cp_.vm.op(i);
        if (o.cls != "SeqEvent_Touch" || ((size_t)i < cp_.audioId.size() && !cp_.audioId[(size_t)i].empty())) continue;   // the level audio hosts it
        CampaignState::Touch T;
        T.op = i;
        T.playerOnly = o.authored["bPlayerOnly"].asBool(true);
        const std::string org = afterDot(o.authored["Originator"].asString());
        auto b = brushes.find(org);
        if (b != brushes.end()) {
            T.polys = b->second;
            T.lo = T.hi = T.polys[0][0];
            for (const auto& poly : T.polys) for (const core::Vec3& p : poly) {
                T.lo = core::Vec3{std::min(T.lo.x, p.x), std::min(T.lo.y, p.y), std::min(T.lo.z, p.z)};
                T.hi = core::Vec3{std::max(T.hi.x, p.x), std::max(T.hi.y, p.y), std::max(T.hi.z, p.z)};
            }
        } else if (auto t = triggers.find(org); t != triggers.end()) {
            T.cylinder = true; T.center = t->second.first; T.radius = 0.4f * t->second.second; T.halfHeight = 0.4f * t->second.second;
        } else { ++missing; continue; }
        cp_.touches.push_back(T);
    }
    LOG_INFO("campaign: %zu touch events armed (%d originators without a shape)", cp_.touches.size(), missing);
}

static bool insideConvex(const std::vector<std::vector<core::Vec3>>& polys, const core::Vec3& lo, const core::Vec3& hi, const core::Vec3& x) {
    if (x.x < lo.x || x.y < lo.y || x.z < lo.z || x.x > hi.x || x.y > hi.y || x.z > hi.z) return false;
    const core::Vec3 c = (lo + hi) * 0.5f;
    for (const auto& p : polys) {
        core::Vec3 n = core::cross(p[1] - p[0], p[2] - p[0]);
        if (core::dot(n, c - p[0]) > 0.0f) n = n * -1.0f;   // outward
        if (core::dot(n, x - p[0]) > 1e-4f) return false;
    }
    return true;
}

void World::cpTickTouches() {
    if (cp_.touches.empty() || simTick_ % 6 != 0) return;   // 10 Hz
    for (CampaignState::Touch& T : cp_.touches) {
        if (!cp_.vm.op(T.op).enabled) continue;
        for (size_t q = 0; q < match_.players().size(); ++q) {
            if (T.playerOnly && match_.players()[q].kind == ParticipantKind::Bot) continue;
            const Character* c = participantPawn((int)q);
            bool in = false;
            if (c) {
                const core::Vec3 x = c->actorLocation();
                if (T.cylinder) { const core::Vec3 d = x - T.center; in = d.x * d.x + d.z * d.z <= T.radius * T.radius && std::fabs(d.y) <= T.halfHeight + 1.0f; }
                else in = insideConvex(T.polys, T.lo, T.hi, x);
            }
            const bool was = T.inside.count((int)q) != 0;
            if (in && !was) { T.inside.insert((int)q); cp_.vm.fireEvent(T.op, "Touched"); }
            else if (!in && was) {
                T.inside.erase((int)q);
                cp_.vm.output(T.op, "UnTouched");
                if (T.inside.empty()) cp_.vm.output(T.op, "Empty");
            }
        }
    }
}

int World::cpTestFireFactory(int k) {
    if (k < 0 || (size_t)k >= cp_.factories.size() || cp_.factories[(size_t)k].op < 0) return -1;
    cp_.vm.impulse(cp_.factories[(size_t)k].op, 0);
    return (int)cp_.factories[(size_t)k].pool.size();
}

} // namespace game

namespace game {

// ---- TnSeqAct_TransformWorldProp: the campaign's TnTransformingWorldActors (doors, tube doors, the lift pods) ----

// Untransform (0) / Transform (1) on the Target actors -> DesiredTransformState; the actors play their transform sequence at rate 1 and
// pose their collision (TransformingActors, loaded from AssetTools' <CH>/transforming_actors_gameplay.json); the nav cells under the
// closed ones are blocked at once (the rest pose). "Finished" (15 of A1's 41 ops) when every target reached it; an op without
// targets in the data finishes at once (logged).
bool World::cpTransformWorldProp(KismetVM& vm, int op, int input) {
    KismetVM::Op& o = vm.op(op);
    if (o.cls != "TnSeqAct_TransformWorldProp") return false;
    std::vector<int> idx;
    int named = 0;
    for (int v : vm.varLink(op, "Target")) {
        const KismetVM::Var* var = vm.var(v);
        if (var && var->named >= 0) var = vm.var(var->named);
        if (!var || var->obj.empty()) continue;
        ++named;
        const int i = transforming_.find(afterDot(var->obj));
        if (i >= 0) { transforming_.setDesired((size_t)i, input == 1 ? 1 : 0); idx.push_back(i); }
    }
    if (!idx.empty() && botNav_.valid()) transforming_.applyNavBlocks(botNav_);
    LOG_INFO("campaign: %s %s: %zu of %d actors", afterDot(o.path).c_str(), input == 1 ? "Transform" : "Untransform", idx.size(), named);
    const bool finished = std::find_if(o.outputs.begin(), o.outputs.end(), [](const auto& x) { return x.first == "Finished"; }) != o.outputs.end();
    if (finished && !idx.empty()) { o.latent = true; o.timer = -1.0f; cp_.transformOps.push_back({op, idx}); }
    else if (finished) vm.output(op, "Finished");
    return true;
}

void World::cpTickTransformOps() {
    for (size_t k = 0; k < cp_.transformOps.size();) {
        bool rest = true;
        for (int i : cp_.transformOps[k].second) {
            const TransformingActors::Actor& a = transforming_.actors()[(size_t)i];
            if (a.t != (a.desired ? a.length : 0.0f)) rest = false;
        }
        if (!rest) { ++k; continue; }
        cp_.vm.finishLatent(cp_.transformOps[k].first, "Finished");
        cp_.transformOps.erase(cp_.transformOps.begin() + (long)k);
    }
}

} // namespace game

namespace game {

// ---- SeqAct_TnPawnFactory death outputs: "Single Pawn Killed" per spawned pawn's death, "All Pawns Killed" once none of its spawns
// lives (and none is pending) [C op outputs; the A1 / A2 / A3 encounters wait on them] ----
void World::cpTickFactoryKills() {
    const auto& kh = match_.killHistory();
    if (cp_.factoryKillsSeen > kh.size()) cp_.factoryKillsSeen = 0;
    for (; cp_.factoryKillsSeen < kh.size(); ++cp_.factoryKillsSeen) {
        const int v = kh[cp_.factoryKillsSeen].victim;
        for (CampaignFactory& F : cp_.factories) cpFactoryPawnDied(F, v);
    }
}

void World::cpFactoryPawnDied(CampaignFactory& F, int victim) {
    auto it = std::find(F.live.begin(), F.live.end(), victim);
    if (it == F.live.end()) return;
    F.live.erase(it);
    if (F.op < 0) return;
    if (std::find(F.outputs.begin(), F.outputs.end(), "Single Pawn Killed") != F.outputs.end()) cp_.vm.output(F.op, "Single Pawn Killed");
    if (F.live.empty() && F.pending <= 0 && std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Killed") != F.outputs.end()) {
        cp_.vm.output(F.op, "All Pawns Killed");
        LOG_INFO("campaign: factory %s: All Pawns Killed", afterDot(F.action).c_str());
    }
}

// ---- sentries (TnActorFactorySentryAI + a TURRETDEF: AssetTools out/campaign_characters/spawnables.json) ----
// A TURRETDEF turns the placed turret body at the spawn point into a stationary AI sentry: it turns toward the nearest living enemy
// in SightRadius at LagDegreesPerSecond and fires its weapon within MaxAttackRange when aimed (AimedAtTargetThreshold) with a line
// of sight; it is a destructible (its hit box at the body) and dies at 0 HP -> the factory's Single / All Pawns Killed; the body hides
// after LifeSpanAfterDeath. HP and fire from AssetTools' sentry DES and weapon fire blocks; PROV: the repair sentry does not repair.
namespace {
struct SentryFire { const char* weapon; bool hitscan; float damage, interval, speed, radius; bool homing; const char* type; };
const SentryFire kSentryFire[] = {   // AssetTools weapon.json fire blocks (AI cadence = FireIntervalModifier.IntervalRange; metres)
    {"TnWeaponSentryLight", true, 5.0f, 0.16f, 0, 0, false, "TnDamageTypeSentryLight"},
    {"TnWeaponSentryRocket", false, 85.0f, 1.5f, 60.0f, 5.0f, false, "TnDamageTypeSentryRocket"},
    {"TnWeaponSentryHomingRocket", false, 75.0f, 2.75f, 50.0f, 5.0f, true, "TnDamageTypeSentryRocket"},
    {"TnWeaponWarshipHomingRocketSentry", false, 15.0f, 3.0f, 42.5f, 3.5f, true, "TnDamageTypeSentryRocket"},
    {"TnWeaponSentryDeathBeam", true, 24.0f, 0.03f, 0, 0, false, "TnDamageTypeSentryDeathBeam"},   // DamagePerSecond 800
    {"TnWeaponOmegaShipMG", false, 10.0f, 0.1f, 80.0f, 1.0f, false, "TnDamageTypeOmegaShipMG"},
    {"TnWeaponOmegaShipRocket", false, 40.0f, 3.5f, 40.0f, 10.0f, false, "TnDamageTypeOmegaRocket"},
    {"TnWeaponSentryRepair", true, 0.0f, 1.0f, 0, 0, false, "TnDamageTypeRepairEnemy"},   // repairs the AI's side: no attack (PROV)
};
std::string tailS(const std::string& s) { const size_t k = s.find_last_of('.'); return k == std::string::npos ? s : s.substr(k + 1); }
}

void World::cpLoadSentryDefs() {
    cp_.sentryFactories.clear(); cp_.sentryDefs.clear(); cp_.sentries.clear();
    assets::Json j;
    if (!readJsonFile("F:/Transformers Rebuild/AssetTools/out/campaign_characters/spawnables.json", j)) return;
    const assets::Json& ss = j["sentries"];
    for (size_t i = 0; i < ss.size(); ++i) {
        const assets::Json& v = ss[i]["values"];
        CampaignSentryDef D;
        D.turretdef = tailS(ss[i]["turretdef"].asString());
        D.weapon = tailS(ss[i]["weapon"]["class"].asString());
        D.range = v["MaxAttackRange"].asFloat(8000.0f) * 0.01f;
        D.sight = v["SightRadius"].asFloat(10000.0f) * 0.01f;
        D.lagDeg = v["LagDegreesPerSecond"].asFloat(90.0f);
        D.aimDeg = v["AimedAtTargetThreshold"].asFloat(3.0f);
        D.deathLife = v["LifeSpanAfterDeath"].asFloat(1.0f);
        // the Destructible's first state's InitialHealth (AssetTools destructibles/<name>.json): Blaster / Rocket / Repair 125,
        // SentryBall 135, the invisible death-beam ball 1000, the warship ball 10000 (no death state authored)
        const std::string des = v["Destructible"].asString();
        D.hp = des.find("Invis") != std::string::npos ? 1000.0f : des.find("Warship") != std::string::npos ? 10000.0f :
               des.find("SentryBall") != std::string::npos ? 135.0f : 125.0f;
        const int di = (int)cp_.sentryDefs.size();
        cp_.sentryDefs.push_back(D);
        const assets::Json& fs = ss[i]["factories"];
        for (size_t f = 0; f < fs.size(); ++f) {
            if (fs[f]["chapter"].asString() != cpChapter_.chapter) continue;
            CampaignState::SentryFactory SF;
            SF.def = di;
            SF.count = std::max(1, fs[f]["spawn_count"].asInt(1));
            for (size_t k = 0; k < fs[f]["spawn_points"].size(); ++k) {
                const assets::Json& sp = fs[f]["spawn_points"][k];
                const assets::Json& L = sp["location_gltf"];
                if (L.size() < 3) continue;
                SF.points.push_back(core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()});
                SF.bodies.push_back(tailS(sp["object"].asString()));
            }
            cp_.sentryFactories[fs[f]["action"].asString()] = SF;
        }
    }
    LOG_INFO("campaign: %zu sentry factories (%zu TURRETDEFs)", cp_.sentryFactories.size(), cp_.sentryDefs.size());
}

bool World::cpSentryFactoryActivate(KismetVM& vm, int op, int input, CampaignFactory& F) {
    const int fi = (int)(&F - &cp_.factories[0]);
    auto it = cp_.sentryFactories.find(F.action);
    if (input == 4) {   // Kill All
        for (CampaignSentry& s : cp_.sentries) if (s.factory == fi && !s.dead) { s.dead = true; s.deadT = 0.0f; cpRemoveDestructible(s.des); }
        F.liveSentries.clear();
        return true;
    }
    if (input != 0) return true;
    if (it == cp_.sentryFactories.end() || it->second.points.empty()) {
        LOG_INFO("campaign: sentry factory %s: no data", afterDot(F.action).c_str());
        if (std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end()) vm.output(op, "All Pawns Spawned");
        return true;
    }
    const CampaignState::SentryFactory SF = it->second;
    const CampaignSentryDef D = cp_.sentryDefs[(size_t)SF.def];
    for (int c = 0; c < SF.count; ++c) {
        const size_t k = (size_t)c % SF.points.size();
        CampaignSentry S;
        S.def = SF.def; S.factory = fi; S.body = SF.bodies[k]; S.pos = SF.points[k];
        // its hit box: a destructible with one 'alive' state (HP) that GenericDamage takes to 'dead'
        CampaignDestructible d;
        d.name = "sentry_" + S.body; d.pos = S.pos; d.block = false;
        d.posUE = core::Vec3{S.pos.x * 100.0f, S.pos.z * 100.0f, S.pos.y * 100.0f};
        CampaignDestructible::Piece P; P.name = "Turret";
        CampaignDestructible::State alive; alive.handle = 0; alive.name = "Alive"; alive.health = D.hp;
        alive.links.push_back({1, std::string("HmGenericDamageDestructionTrigger")});
        CampaignDestructible::State dead; dead.handle = 1; dead.name = "Dead";
        P.states.push_back(alive); P.states.push_back(dead); P.cur = 0; P.health = D.hp;
        d.pieces.push_back(P);
        d.fixedBounds = true; d.lo = S.pos - core::Vec3{1.0f, 1.0f, 1.0f}; d.hi = S.pos + core::Vec3{1.0f, 1.0f, 1.0f};
        cp_.destructibles.push_back(d);
        S.des = (int)cp_.destructibles.size() - 1;
        cp_.sentries.push_back(S);
        const int si = (int)cp_.sentries.size() - 1;
        F.liveSentries.push_back(si);
        for (int v : vm.varLink(op, "Spawned")) if (KismetVM::Var* var = vm.var(v)) var->obj = "sentry:" + std::to_string(si);
        if (std::find(F.outputs.begin(), F.outputs.end(), "Spawned") != F.outputs.end()) vm.output(op, "Spawned");
        if (renderer_) renderer_->setActorHidden(S.body, false);
    }
    LOG_INFO("campaign: sentry factory %s spawned %d %s (%s)", afterDot(F.action).c_str(), SF.count, D.turretdef.c_str(), D.weapon.c_str());
    if (std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end()) vm.output(op, "All Pawns Spawned");
    return true;
}

void World::cpTickSentries(float dt) {
    for (size_t si = 0; si < cp_.sentries.size(); ++si) {
        CampaignSentry& S = cp_.sentries[si];
        const CampaignSentryDef& D = cp_.sentryDefs[(size_t)S.def];
        if (S.dead) {
            if (S.deadT >= 0.0f && (S.deadT += dt) >= D.deathLife) { S.deadT = -1.0f; if (renderer_) renderer_->setActorHidden(S.body, true); }
            continue;
        }
        CampaignDestructible& d = cp_.destructibles[(size_t)S.des];
        if (d.gone || d.pieces.empty() || d.pieces[0].cur != 0) {   // destroyed
            S.dead = true; S.deadT = 0.0f;
            cpRemoveDestructible(S.des);
            LOG_INFO("campaign: sentry %s destroyed", S.body.c_str());
            CampaignFactory& F = cp_.factories[(size_t)S.factory];
            F.liveSentries.erase(std::remove(F.liveSentries.begin(), F.liveSentries.end(), (int)si), F.liveSentries.end());
            if (F.op >= 0) {
                if (std::find(F.outputs.begin(), F.outputs.end(), "Single Pawn Killed") != F.outputs.end()) cp_.vm.output(F.op, "Single Pawn Killed");
                if (F.liveSentries.empty() && std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Killed") != F.outputs.end()) cp_.vm.output(F.op, "All Pawns Killed");
            }
            continue;
        }
        if (S.dormant || match_.state() != Match::State::InProgress) continue;
        // the target: SeqAct_TnAiSentryForceTarget's, else the nearest living enemy (the AI's other side) in SightRadius with LOS
        const int team = cp_.factories[(size_t)S.factory].team;
        const core::Vec3 eye = S.pos + core::Vec3{0.0f, 0.5f, 0.0f};
        int tgt = -1; float best = D.sight;
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const MatchPlayer& mp = match_.players()[q];
            if (!mp.alive || mp.team == team) continue;
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const float L = core::length(c->actorLocation() - eye);
            if ((S.forced >= 0 && (int)q != S.forced) || L > best) continue;
            if (!botLineOfSight(eye, c->actorLocation())) continue;
            best = L; tgt = (int)q;
        }
        if (tgt < 0) continue;
        const core::Vec3 aim = participantPawn(tgt)->actorLocation() - eye;
        const float want = std::atan2(-aim.x, -aim.z);
        float dy = std::remainder(want - S.yaw, 6.2831853f);
        const float step = D.lagDeg * 0.0174533f * dt;
        S.yaw += std::clamp(dy, -step, step);
        dy = std::remainder(want - S.yaw, 6.2831853f);
        if ((S.cool -= dt) > 0.0f || best > D.range || std::fabs(dy) > D.aimDeg * 0.0174533f) continue;
        const SentryFire* sf = nullptr;
        for (const SentryFire& x : kSentryFire) if (D.weapon == x.weapon) sf = &x;
        if (!sf || sf->damage <= 0.0f) { S.cool = 1.0f; continue; }
        S.cool = sf->interval;
        if (sf->hitscan) applyMatchDamage(tgt, -1, sf->damage, false, sf->type);
        else {
            Weapon w;
            w.projSpeed = sf->speed; w.projDamage = sf->damage; w.projRadiusM = sf->radius; w.rangeM = D.range; w.damageType = sf->type;
            w.projHoming = sf->homing; w.homingForce = sf->homing ? 200.0f : 0.0f;   // HomingForce 20000 UU/s^2 w.projMaxSpeed = sf->speed; w.lockRobots = true;
            spawnProjectile(eye, core::normalize(aim) * sf->speed, w, -1);
            if (sf->homing) projectiles_.back().target = tgt;
        }
    }
}

// SeqAct_TnAiSentryForceTarget (the sentries attack this target only) / SeqAct_TnAiSentrySetDormant (On: dormant).
bool World::cpSentryOpActivate(KismetVM& vm, int op, int input) {
    const KismetVM::Op& o = vm.op(op);
    if (o.cls != "SeqAct_TnAiSentryForceTarget" && o.cls != "SeqAct_TnAiSentrySetDormant") return false;
    std::vector<int> ss;
    for (int v : vm.varLink(op, "Target")) {
        const KismetVM::Var* var = vm.var(v);
        if (var && var->named >= 0) var = vm.var(var->named);
        if (var && var->obj.rfind("sentry:", 0) == 0) ss.push_back(std::atoi(var->obj.c_str() + 7));
    }
    if (o.cls == "SeqAct_TnAiSentryForceTarget") {
        const std::vector<int> t = cpTargets(vm, op, "Force Target");
        for (int i : ss) if (i >= 0 && (size_t)i < cp_.sentries.size()) cp_.sentries[(size_t)i].forced = t.empty() ? -1 : t.front();
    } else for (int i : ss) if (i >= 0 && (size_t)i < cp_.sentries.size()) cp_.sentries[(size_t)i].dormant = input == 0;
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

} // namespace game

namespace game {

// ---- roller mines (TnActorFactoryRollerMine -> TransGame.TnRollerMine: AssetTools spawnables.json roller_mine) ----
// The factory's authored DamageRadius / DamageAmount / Health / Fuse / LocalLinearVelocity [C data]: the mine rolls out at the
// launch speed, explodes when its Fuse runs out, when an enemy pawn touches it, or when shot to 0 Health (a destructible hit box);
// the explosion is HurtRadius(DamageAmount, DamageRadius); each one gone -> the factory's Single / All Pawns Killed.
// PROV: no rigid-body physics - it rolls on the ground toward the nearest enemy (the launch direction is not exported: the spawn
// points are Emitters without a rotation in the data), steering at 10 m/s^2.
void World::cpLoadMineDefs() {
    cp_.mineFactories.clear(); cp_.mines.clear();
    assets::Json j;
    if (!readJsonFile("F:/Transformers Rebuild/AssetTools/out/campaign_characters/spawnables.json", j)) return;
    const assets::Json& r = j["roller_mine"];
    const assets::Json& cd = r["class_defaults"];
    const assets::Json& fs = r["factories"];
    for (size_t f = 0; f < fs.size(); ++f) {
        if (fs[f]["chapter"].asString() != cpChapter_.chapter) continue;
        const assets::Json& v = fs[f]["factory_values"];
        CampaignState::MineFactory M;
        M.damage = v["DamageAmount"].asFloat(cd["_DamageAmount"].asFloat(25.0f));
        M.radius = v["DamageRadius"].asFloat(cd["_DamageRadius"].asFloat(1000.0f)) * 0.01f;
        M.health = v["Health"].asFloat(cd["_Health"].asFloat(30.0f));
        M.fuse = v["Fuse"].asFloat(cd["_Fuse"].asFloat(10.0f));
        const assets::Json& lv = v["LocalLinearVelocity"];
        M.speed = std::max(5.0f, std::sqrt(lv["X"].asFloat(0) * lv["X"].asFloat(0) + lv["Y"].asFloat(0) * lv["Y"].asFloat(0)) * 0.01f);
        for (size_t k = 0; k < fs[f]["spawn_points"].size(); ++k) {
            const assets::Json& L = fs[f]["spawn_points"][k]["location_ue"];
            if (L.size() >= 3) M.points.push_back(core::Vec3{L[0].asFloat() * 0.01f, L[2].asFloat() * 0.01f, L[1].asFloat() * 0.01f});
        }
        cp_.mineFactories[fs[f]["action"].asString()] = M;
    }
    LOG_INFO("campaign: %zu roller mine factories", cp_.mineFactories.size());
}

bool World::cpMineFactoryActivate(KismetVM& vm, int op, int input, CampaignFactory& F) {
    const int fi = (int)(&F - &cp_.factories[0]);
    auto it = cp_.mineFactories.find(F.action);
    if (input == 4) { for (CampaignMine& m : cp_.mines) if (m.factory == fi && !m.done) { m.fuse = 0.0f; } return true; }   // Kill All: they go off
    if (input != 0) return true;
    if (it == cp_.mineFactories.end() || it->second.points.empty()) {
        LOG_INFO("campaign: roller mine factory %s: no data", afterDot(F.action).c_str());
        if (std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end()) vm.output(op, "All Pawns Spawned");
        return true;
    }
    const CampaignState::MineFactory M = it->second;
    for (int c = 0; c < std::max(1, F.count); ++c) {
        CampaignMine m;
        m.factory = fi; m.pos = M.points[(size_t)c % M.points.size()]; m.fuse = M.fuse;
        CampaignDestructible d;
        d.name = "rollermine_" + std::to_string(cp_.mines.size()); d.pos = m.pos; d.block = false;
        d.posUE = core::Vec3{m.pos.x * 100.0f, m.pos.z * 100.0f, m.pos.y * 100.0f};
        CampaignDestructible::Piece P; P.name = "Mine";
        CampaignDestructible::State alive; alive.handle = 0; alive.name = "Rolling"; alive.health = M.health;
        alive.mesh = "content/FX_RollerMine_p/Mesh/RollerMine_STAT.gltf";
        alive.links.push_back({1, std::string("HmGenericDamageDestructionTrigger")});
        CampaignDestructible::State dead; dead.handle = 1; dead.name = "Exploded";
        P.states.push_back(alive); P.states.push_back(dead); P.cur = 0; P.health = M.health;
        d.pieces.push_back(P);
        d.fixedBounds = true; d.lo = m.pos - core::Vec3{0.7f, 0.7f, 0.7f}; d.hi = m.pos + core::Vec3{0.7f, 0.7f, 0.7f};
        cp_.destructibles.push_back(d);
        m.des = (int)cp_.destructibles.size() - 1;
        m.damage = M.damage; m.radius = M.radius; m.speed = M.speed;
        cp_.mines.push_back(m);
        F.liveSentries.push_back(-1 - (int)(cp_.mines.size() - 1));   // (mines share the live list: -1 - index)
        if (std::find(F.outputs.begin(), F.outputs.end(), "Spawned") != F.outputs.end()) vm.output(op, "Spawned");
    }
    LOG_INFO("campaign: roller mine factory %s launched %d", afterDot(F.action).c_str(), std::max(1, F.count));
    if (std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Spawned") != F.outputs.end()) vm.output(op, "All Pawns Spawned");
    return true;
}

void World::cpTickMines(float dt) {
    for (size_t mi = 0; mi < cp_.mines.size(); ++mi) {
        CampaignMine& m = cp_.mines[mi];
        if (m.done) continue;
        CampaignDestructible& d = cp_.destructibles[(size_t)m.des];
        const int team = cp_.factories[(size_t)m.factory].team;
        // roll toward the nearest living enemy
        int tgt = -1; float best = 1e30f;
        for (size_t q = 0; q < match_.players().size(); ++q) {
            const MatchPlayer& mp = match_.players()[q];
            if (!mp.alive || mp.team == team) continue;
            const Character* c = participantPawn((int)q);
            if (!c) continue;
            const float L = core::length(c->position() - m.pos);
            if (L < best) { best = L; tgt = (int)q; }
        }
        if (tgt >= 0) {
            core::Vec3 to = participantPawn(tgt)->position() - m.pos; to.y = 0.0f;
            const float L = core::length(to);
            if (L > 1e-3f) {
                const core::Vec3 want = to * (m.speed / L);
                const core::Vec3 dv = want - core::Vec3{m.vel.x, 0.0f, m.vel.z};
                const float dl = core::length(dv), a = 10.0f * dt;
                m.vel = m.vel + (dl > a ? dv * (a / dl) : dv);
            }
        }
        m.pos = m.pos + m.vel * dt;
        float gy; core::Vec3 n;
        if (collision_.valid() && collision_.groundHeight(m.pos.x, m.pos.z, m.pos.y + 1.0f, 2.0f, gy, n)) m.pos.y = gy + 0.6f;
        d.pos = m.pos; d.posUE = core::Vec3{m.pos.x * 100.0f, m.pos.z * 100.0f, m.pos.y * 100.0f};
        d.lo = m.pos - core::Vec3{0.7f, 0.7f, 0.7f}; d.hi = m.pos + core::Vec3{0.7f, 0.7f, 0.7f};
        const bool shot = d.gone || d.pieces.empty() || d.pieces[0].cur != 0;
        const bool touched = tgt >= 0 && best < 1.5f;
        if (!shot && !touched && (m.fuse -= dt) > 0.0f) continue;
        m.done = true;
        cpRemoveDestructible(m.des);
        radiusDamage(m.pos, m.damage, m.radius, -1, "TnDamageTypeRollerMine");
        LOG_INFO("campaign: roller mine %zu explodes (%s)", mi, shot ? "shot" : touched ? "touched" : "fuse");
        CampaignFactory& F = cp_.factories[(size_t)m.factory];
        F.liveSentries.erase(std::remove(F.liveSentries.begin(), F.liveSentries.end(), -1 - (int)mi), F.liveSentries.end());
        if (F.op >= 0) {
            if (std::find(F.outputs.begin(), F.outputs.end(), "Single Pawn Killed") != F.outputs.end()) cp_.vm.output(F.op, "Single Pawn Killed");
            if (F.liveSentries.empty() && std::find(F.outputs.begin(), F.outputs.end(), "All Pawns Killed") != F.outputs.end()) cp_.vm.output(F.op, "All Pawns Killed");
        }
    }
}

} // namespace game

namespace game {

// ---- SeqAct_ToggleHidden (Hide 0 / UnHide 1 / Toggle 2) and SeqAct_Destroy on world actors (A1: 272 / 169 ops) ----
// The target actors' visibility goes to the renderer (setActorHidden; the authored bHidden from actors.json is the start state, which
// the renderer already applies); a transforming actor also stops drawing through TransformingActors. Destroy = hidden for good, plus
// collision off for a mover / a transforming actor, a destructible removed, a sentry ("sentry:<i>") killed. Pawn targets stay
// untouched (PROV). Render-side only except those collision changes.
bool World::cpHideOpActivate(KismetVM& vm, int op, int input) {
    const KismetVM::Op& o = vm.op(op);
    const bool destroy = o.cls == "SeqAct_Destroy";
    if (!destroy && o.cls != "SeqAct_ToggleHidden") return false;
    int n = 0;
    for (int v : vm.varLink(op, "Target")) {
        const KismetVM::Var* var = vm.var(v);
        if (var && var->named >= 0) var = vm.var(var->named);
        if (!var || var->obj.empty() || var->obj.rfind("pawn:", 0) == 0) continue;
        if (var->obj.rfind("sentry:", 0) == 0) {   // a spawned sentry: destroyed (its factory outputs fire on its next tick)
            const int si = std::atoi(var->obj.c_str() + 7);
            if (destroy && si >= 0 && (size_t)si < cp_.sentries.size()) {
                CampaignSentry& s = cp_.sentries[(size_t)si];
                if (!s.dead && s.des >= 0) cpRemoveDestructible(s.des);
                ++n;
            }
            continue;
        }
        if (var->obj.rfind("des:", 0) == 0) { if (destroy) cpRemoveDestructible(std::atoi(var->obj.c_str() + 4)); ++n; continue; }
        const std::string name = afterDot(var->obj);
        const std::string key = var->obj.find('.') != std::string::npos ? var->obj : name;   // the object path when the Kismet names one
        auto hs = cp_.actorHidden.find(key);
        const bool was = hs != cp_.actorHidden.end() && hs->second;
        const bool hidden = destroy ? true : input == 0 ? true : input == 1 ? false : !was;
        if (destroy && hs != cp_.actorHidden.end() && hs->second && cp_.actorDestroyed.count(key)) continue;
        cp_.actorHidden[key] = hidden;
        if (destroy) cp_.actorDestroyed.insert(key);
        else if (cp_.actorDestroyed.count(key)) continue;   // a destroyed actor stays gone
        if (renderer_) renderer_->setActorHidden(key, hidden);
        const int ti = transforming_.find(name);
        if (ti >= 0) { transforming_.setShown((size_t)ti, !hidden); if (destroy) transforming_.setCollide((size_t)ti, false); }
        if (destroy) {
            for (CampaignMover& m : cpMovers_) if (m.name == name) {
                if (m.pawnSet >= 0) collision_.setDynamicEnabled(m.pawnSet, false);
                if (m.floorSet >= 0) collision_.setDynamicEnabled(m.floorSet, false);
            }
            for (size_t i = 0; i < cp_.destructibles.size(); ++i) if (cp_.destructibles[i].name == name) cpRemoveDestructible((int)i);
        }
        ++n;
    }
    static const bool klog = std::getenv("WFC_KISMETLOG") != nullptr;
    if (klog) LOG_INFO("campaign: %s %s: %d actors", afterDot(o.path).c_str(), destroy ? "Destroy" : input == 0 ? "Hide" : input == 1 ? "UnHide" : "Toggle", n);
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

} // namespace game

namespace game {

// ---- SeqAct_Teleport + TnSeqVar_Character (every chapter: A1 60 refs ... A3 253) ----
// TnSeqVar_Character (ChassisTypePathName = a ROBODEF) names the participants playing that character: the chassis whose roster
// robot definition is that ROBODEF (several chassis can share one: Truck / Truck7). SeqAct_Teleport moves its Target pawns to the
// Destination actor's location and rotation (stopped, re-grounded within 2 m); non-pawn targets are left (PROV).
std::vector<int> World::cpCharacterTargets(const std::string& robodef) {
    static std::map<std::string, std::set<std::string>> chassisOf;
    if (chassisOf.empty()) {
        assets::Json r;
        if (readJsonFile(manifestsRoot() + "/mp_content/roster_package.json", r))
            for (const auto& kv : r["chassis"].obj) chassisOf[kv.second["robot"]["definition"].asString()].insert(kv.first);
    }
    std::vector<int> out;
    auto it = chassisOf.find(robodef);
    if (it == chassisOf.end()) return out;
    for (size_t q = 0; q < match_.players().size(); ++q) {
        const MatchPlayer& mp = match_.players()[q];
        if (mp.directorSpawned) continue;   // the factories' AI are not the named characters
        if (it->second.count(resolveChassis(mp.selection, match_.faction((int)q)))) out.push_back((int)q);
    }
    return out;
}

bool World::cpTeleportActivate(KismetVM& vm, int op, int input) {
    (void)input;
    const KismetVM::Op& o = vm.op(op);
    if (o.cls != "SeqAct_Teleport") return false;
    std::string dest = vm.objectOf(op, "Destination");
    if (dest.empty()) dest = o.authored["DestinationActor"].asString();
    core::Vec3 at;
    int moved = 0;
    if (!dest.empty() && cpActorPos(dest, at)) {
        auto yi = cp_.actorYaw.find(afterDot(dest));
        for (int p : cpTargets(vm, op, "Target")) {
            Character* c = participantPawnMutable(p);
            if (!c) continue;
            core::Vec3 q = at;
            float gy; core::Vec3 n;
            if (collision_.valid() && collision_.groundHeight(q.x, q.z, q.y + 0.5f, 2.0f, gy, n) && gy > q.y - 2.0f) q.y = gy;
            c->setPosition(q); c->velocity() = core::Vec3{0, 0, 0}; c->groundY = q.y;
            if (yi != cp_.actorYaw.end()) {   // UE yaw (degrees) -> the pawn's yaw (forwardFromYawPitch convention)
                const float ue = yi->second * 0.0174533f;
                c->setYaw(std::atan2(-std::cos(ue), -std::sin(ue)));
            }
            ++moved;
        }
    }
    LOG_INFO("campaign: %s -> %s: %d pawns", afterDot(o.path).c_str(), afterDot(dest).c_str(), moved);
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

} // namespace game
