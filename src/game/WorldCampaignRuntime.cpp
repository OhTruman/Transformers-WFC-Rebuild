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

// The AI's body: AssetTools' character export of the AICD (out/escalation_characters/<id>, loaded as "ESC_<id>"); an Autobot / Decepticon
// variant without its own export borrows the other faction's (PROV, logged); none -> "" (the factory is held).
std::string bodyForAicd(const std::string& aicd, std::string& note) {
    std::string id = aicd;
    if (id.size() > 5 && id.compare(id.size() - 5, 5, "_AICD") == 0) id.resize(id.size() - 5);
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
            F.chassis = bodyForAicd(F.aicd, F.note);
            if (F.chassis.empty()) F.note = "no character export for " + F.aicd;
        }
        // The pool: one participant per spawn (never respawned; directorSpawned keeps them out of the match start).
        if (F.ai && !F.chassis.empty())
            for (int c = 0; c < F.count && pool < 64; ++c, ++pool) {
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
                F.pool.push_back(p);
            }
        if (!F.note.empty()) LOG_INFO("campaign: factory %s (%s): %s", afterDot(F.action).c_str(), F.aicd.c_str(), F.note.c_str());
        cp_.factories.push_back(F);
    }
    rebuildOppIndex();
    LOG_INFO("campaign: %zu pawn factories, %d AI participants pooled", cp_.factories.size(), pool);
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

// The in-map pick (CharacterSelect_GFX -> Customize.SelectCharacter): the human takes the picked trio member; the AI squad is the trio
// minus the humans' picks, so a squad member holding the picked character takes the free one (its body swapped in place).
void World::cpSyncPicks() {
    if (!cp_.active || localPlayer_ < 0 || cp_.squad.empty()) return;
    const std::string mine = resolveChassis(match_.players()[(size_t)localPlayer_].selection, match_.faction(localPlayer_));
    if (mine == cp_.humanChassis) return;
    cp_.humanChassis = mine;
    for (size_t k = 0; k < cp_.squad.size(); ++k) {
        if (cp_.squadChassis[k] != mine) continue;
        std::string freeName, freeChassis;
        for (size_t t = 0; t < cpChapter_.trio.size() && t < cp_.trioChassis.size(); ++t) {
            const std::string& ch = cp_.trioChassis[t];
            if (ch == mine || std::find(cp_.squadChassis.begin(), cp_.squadChassis.end(), ch) != cp_.squadChassis.end()) continue;
            freeName = cpChapter_.trio[t]; freeChassis = ch; break;
        }
        if (freeChassis.empty()) return;
        const int p = cp_.squad[k];
        CharacterSelection sel; sel.type = 1; sel.chassisId = freeChassis;
        match_.selectCharacter(p, sel);
        MatchPlayer& mp = match_.playerMutable(p);
        mp.name = freeName;
        cp_.squadChassis[k] = freeChassis;
        chassisAssets(freeChassis);
        for (MatchOpponent* o : opponents_)
            if (o->matchPlayer() == p && o->spawned() && applyChassisToPawn(o->pawn(), freeChassis)) applyCharacterTo(o->pawn(), &mp.selection, &mp);
        LOG_INFO("campaign: the human picked %s; squad p%d is now %s (%s)", mine.c_str(), p, freeName.c_str(), freeChassis.c_str());
    }
}

int World::cpTestFireFactory(int k) {
    if (k < 0 || (size_t)k >= cp_.factories.size() || cp_.factories[(size_t)k].op < 0) return -1;
    cp_.vm.impulse(cp_.factories[(size_t)k].op, 0);
    return (int)cp_.factories[(size_t)k].pool.size();
}

} // namespace game
