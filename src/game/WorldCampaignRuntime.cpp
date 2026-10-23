// Campaign runtime step 3: the level Kismet's pawn factories (SeqAct_TnPawnFactory over AssetTools next_map/<CH>_pawn_factories.json),
// the spawned AI's alert-to-player awareness, and the in-map character pick (the human takes a trio member, the squad is the rest).
// See Campaign.h.
#include "game/World.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
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
        if (input != 0) return true;   // Stop / other inputs: nothing to do yet
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
        LOG_INFO("campaign: factory %s spawned p%d %s at (%.1f %.1f %.1f)", afterDot(F.action).c_str(), p, F.chassis.c_str(), at.x, at.y, at.z);
        if (!F.spawnedVar.empty())
            for (int v = 0; v < cp_.vm.varCount(); ++v)
                if (KismetVM::Var* var = cp_.vm.var(v); var && var->varName == F.spawnedVar) var->obj = "pawn:" + std::to_string(p);
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
        }
    }
    LOG_INFO("campaign: %zu actor locations (navigation.json%s)", cp_.actorPos.size(), actorsFile ? " + actors.json" : "");
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
        if (v->cls == "TnSeqVar_Buddy") {   // FindBuddy [C]: BuddyIndex 1-based (default 1) over the ACTIVE members in squad order
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

// SeqAct_TnAiMoveToActor (one move, In), SeqAct_TnAiSetMoveToObjective (Enable / Disable, Range), SeqAct_TnAiUseInteract (move to the
// interact object; PROV: the interact itself is the generic Out), SeqAct_TnAiShootAtActor / UseTurret (PROV: the generic Out) [H].
bool World::cpAiOpActivate(KismetVM& vm, int op, int input) {
    KismetVM::Op& o = vm.op(op);
    const std::string& c = o.cls;
    const bool move = c == "SeqAct_TnAiMoveToActor", objective = c == "SeqAct_TnAiSetMoveToObjective", interact = c == "SeqAct_TnAiUseInteract";
    if (!move && !objective && !interact) return false;
    const std::vector<int> who = cpTargets(vm, op, "Target");
    std::string dest = vm.objectOf(op, move ? "Destination" : objective ? "Objective Point" : "Interact Object");
    if (dest.empty() && interact) dest = o.authored["Interact"].asString();
    core::Vec3 at{0, 0, 0};
    const bool found = !dest.empty() && cpActorPos(dest, at);
    if (objective && input == 1) { for (int p : who) cp_.objectives.erase(p); }
    else if (found) {
        const std::string range = o.authored["Range"].asString();
        const float r = range == "RANGE_Long" ? 25.0f : range == "RANGE_Medium" ? 12.0f : 5.0f;   // PROV radii
        for (int p : who) cp_.objectives[p] = {at, r, !move && !interact};
    } else if (!dest.empty()) LOG_INFO("campaign: %s: destination %s not located (actors.json pending)", afterDot(o.path).c_str(), afterDot(dest).c_str());
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
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

// The visible units of a LevelStreamingState (chapter.json visible_in_states; the persistent unit always): every unit is loaded at the
// start (Rendering preloads them; the collision of every unit is merged, PROV), so a state change is a visibility change, Started and
// Finished at once [C op semantics, RE CAMPAIGN_STREAMING section 3; PROV: no load time].
void World::cpApplyStreamingState(const std::string& state) {
    cp_.streamingState = state;
    for (const std::string& u : cpChapter_.allUnits) {
        auto it = cpChapter_.unitStates.find(u);
        const bool vis = u == cpChapter_.persistentDir || (it != cpChapter_.unitStates.end() && std::find(it->second.begin(), it->second.end(), state) != it->second.end());
        auto was = cp_.unitVisible.find(u);
        if (was != cp_.unitVisible.end() && was->second == vis) continue;
        cp_.unitVisible[u] = vis;
        if (renderer_) renderer_->setChapterUnitVisible(u, vis);
    }
    LOG_INFO("campaign: streaming state %s", state.c_str());
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

int World::cpTestFireFactory(int k) {
    if (k < 0 || (size_t)k >= cp_.factories.size() || cp_.factories[(size_t)k].op < 0) return -1;
    cp_.vm.impulse(cp_.factories[(size_t)k].op, 0);
    return (int)cp_.factories[(size_t)k].pool.size();
}

} // namespace game
