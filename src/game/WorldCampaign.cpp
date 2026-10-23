// Campaign chapters (TnCampaignGameSolo / TnCoopGame): the chapter World (the units' geometry merged), the chapter start, the AI squad,
// the game over and the level Kismet. See Campaign.h.
#include "game/World.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {

namespace {

// TransLevels.ini TnDataProvider_MapInfo (C, RE CAMPAIGN_FRONTEND_FLOW_RE section 3): MapId, the chapter, its faction, the iconic trio.
struct ChapterRow { int mapId; const char* chapter; int faction; const char* trio[3]; };
const ChapterRow kChapters[] = {
    {96, "A1_IAC", 0, {"Optimus", "Bumblebee", "Ratchet"}},
    {97, "A2_KON", 0, {"Optimus", "Sideswipe", "Bumblebee"}},
    {98, "A3_COR", 0, {"Optimus Prime", "Ironhide", "Warpath"}},
    {99, "A4_ORB", 0, {"AirRaid", "Jetfire", "SilverBolt"}},
    {100, "A5_TRY", 0, {"Bumblebee", "Optimus Prime", "Ironhide"}},
    {101, "D1_ORB", 1, {"Brawl", "Barricade", "Megatron"}},
    {102, "D2_COR", 1, {"Starscream", "Skywarp", "Thundercracker"}},
    {103, "D3A_IAC", 1, {"Megatron", "Soundwave", "Breakdown"}},
    {104, "D3B_IAC", 1, {"Megatron", "Soundwave", "Breakdown"}},
    {105, "D5_OME", 1, {"Megatron", "Soundwave", "Breakdown"}},
};

std::string lowerOf(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string alnumLower(const std::string& s) { std::string o; for (char c : s) if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c); return o; }

bool readJson(const std::string& path, assets::Json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), j);
}

core::Vec3 vec3Of(const assets::Json& a) { return a.size() >= 3 ? core::Vec3{a[0].asFloat(), a[1].asFloat(), a[2].asFloat()} : core::Vec3{0, 0, 0}; }

std::string manifestRoot() {
    const char* mr = std::getenv("WFC_MANIFESTS");
    return mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests";
}

// The trio's characters as iconic selections: mp_characters.json iconic_preset.CharacterName -> chassis_id (PROV: the MP iconic
// presets; the campaign's own PlayerData / weapons are a later step).
std::string chassisForCharacter(const std::string& name) {
    static assets::Json chars; static bool loaded = false;
    if (!loaded) { loaded = true; readJson(manifestRoot() + "/mp_content/mp_characters.json", chars); }
    const std::string want = alnumLower(name);
    const assets::Json& cs = chars["characters"];
    for (size_t i = 0; i < cs.size(); ++i) {
        const std::string cn = alnumLower(cs[i]["iconic_preset"]["CharacterName"].asString());
        if (!cn.empty() && (cn == want || (want == "thundercracker" && cn == "thunder"))) return cs[i]["chassis_id"].asString();
    }
    return {};
}

} // namespace

std::string CampaignChapter::root() {
    const char* r = std::getenv("WFC_CAMPAIGN_ROOT");
    return r ? r : "F:/Transformers Rebuild/AssetTools/out/campaign_render";
}

std::string CampaignChapter::chapterOf(const std::string& name) {
    std::string base = name;
    for (const char* suf : {"_BASE_m", "_Base_m", "_base_m", "_m", "_BASE", "_Base"}) {
        const size_t n = std::char_traits<char>::length(suf);
        if (base.size() > n && base.compare(base.size() - n, n, suf) == 0) { base.resize(base.size() - n); break; }
    }
    for (const ChapterRow& r : kChapters) if (lowerOf(base) == lowerOf(r.chapter)) return r.chapter;
    return {};
}

bool CampaignChapter::load(const std::string& chapter, CampaignChapter& out) {
    out = CampaignChapter{};
    const ChapterRow* row = nullptr;
    for (const ChapterRow& r : kChapters) if (chapter == r.chapter) row = &r;
    if (!row) return false;
    out.chapter = chapter; out.mapId = row->mapId; out.faction = row->faction;
    for (const char* t : row->trio) out.trio.push_back(t);
    out.dir = root() + "/" + chapter + "/";
    assets::Json cj;
    if (!readJson(out.dir + "chapter.json", cj)) { LOG_WARN("campaign: no %schapter.json", out.dir.c_str()); return false; }
    out.persistentDir = cj["persistent_dir"].asString();
    const std::string is = cj["initial_streaming_state"].asString();
    out.initialState = is.substr(is.find('.') == std::string::npos ? 0 : is.find('.') + 1);
    const assets::Json& sl = cj["sublevels"];
    for (size_t i = 0; i < sl.size(); ++i)
        if (sl[i]["in_initial_state"].asBool(false) || sl[i]["dir"].asString() == out.persistentDir) out.units.push_back(sl[i]["dir"].asString());
    assets::Json nav;
    if (readJson(out.dir + "navigation.json", nav)) {
        const assets::Json& ns = nav["nodes"];
        for (size_t i = 0; i < ns.size(); ++i) {
            const std::string cls = ns[i]["class"].asString();
            if (cls == "TnCampaignPlayerStart" && out.playerStart.actor.empty()) out.playerStart = {ns[i]["node"].asString(), vec3Of(ns[i]["location_gltf"])};
            if (cls == "Checkpoint") { Checkpoint c; c.actor = ns[i]["node"].asString(); c.pos = vec3Of(ns[i]["location_gltf"]); out.checkpoints.push_back(c); }
        }
    }
    {   // tags / DisplayOrder / streaming state (AssetTools manifests/campaign/<ch>_assets.json from RE's checkpoint export)
        std::string key = lowerOf(chapter.substr(0, chapter.find('_')));
        assets::Json aj;
        if (readJson(manifestRoot() + "/campaign/" + key + "_assets.json", aj)) {
            const assets::Json& cps = aj["checkpoints"];
            for (size_t i = 0; i < cps.size(); ++i) {
                const std::string path = cps[i]["checkpoint"].asString();
                for (Checkpoint& c : out.checkpoints)
                    if (path.size() >= c.actor.size() && path.compare(path.size() - c.actor.size(), c.actor.size(), c.actor) == 0) {
                        c.tag = cps[i]["tag"].asString(); c.displayOrder = cps[i]["display_order"].asInt(0); c.state = cps[i]["streaming_state"].asString();
                    }
            }
        }
        std::stable_sort(out.checkpoints.begin(), out.checkpoints.end(), [](const Checkpoint& a, const Checkpoint& b) { return a.displayOrder < b.displayOrder; });
    }
    out.ok = !out.units.empty() && !out.playerStart.actor.empty();
    return out.ok;
}

const CampaignChapter::Checkpoint* CampaignChapter::checkpoint(const std::string& n) const {
    if (n.empty()) return nullptr;
    for (const Checkpoint& c : checkpoints)
        if (c.actor == n || c.tag == n || (n.size() >= c.actor.size() && n.compare(n.size() - c.actor.size(), c.actor.size(), c.actor) == 0)) return &c;
    return nullptr;
}

// ---- the chapter World ----

bool World::loadCampaignMesh(const std::string& file, render::MeshData& out) {
    out = render::MeshData{};
    int units = 0;
    for (const std::string& u : cpChapter_.units) {
        render::MeshData m;
        if (!std::ifstream(cpChapter_.dir + u + "/" + file, std::ios::binary)) continue;   // a unit without that file (no collision of its own)
        if (!assets::loadGlb(cpChapter_.dir + u + "/" + file, m) || m.empty()) continue;
        ++units;
        const uint32_t base = (uint32_t)out.vertexCount(), ib = (uint32_t)out.indices.size(), mb = (uint32_t)out.mats.size();
        const size_t nv = m.vertexCount();
        auto fill = [&](std::vector<float>& dst, const std::vector<float>& src, size_t per, size_t had) {
            if (src.size() == nv * per) { dst.resize(had * per); dst.insert(dst.end(), src.begin(), src.end()); }
            else if (!dst.empty()) dst.resize((had + nv) * per, 0.0f);
        };
        const size_t had = out.vertexCount();
        fill(out.normals, m.normals, 3, had); fill(out.tangents, m.tangents, 4, had); fill(out.uv, m.uv, 2, had); fill(out.uv1, m.uv1, 2, had);
        if (!m.srcVert.empty() || !out.srcVert.empty()) { out.srcVert.resize(had, UINT32_MAX); if (m.srcVert.size() == nv) out.srcVert.insert(out.srcVert.end(), m.srcVert.begin(), m.srcVert.end()); else out.srcVert.resize(had + nv, UINT32_MAX); }
        out.positions.insert(out.positions.end(), m.positions.begin(), m.positions.end());
        for (uint32_t i : m.indices) out.indices.push_back(i + base);
        for (render::SubMesh s : m.subs) { s.indexOffset += ib; s.material += (int)mb; out.subs.push_back(s); }
        out.mats.insert(out.mats.end(), m.mats.begin(), m.mats.end());
        if (units == 1) { out.boundsMin = m.boundsMin; out.boundsMax = m.boundsMax; }
        else {
            out.boundsMin = {std::min(out.boundsMin.x, m.boundsMin.x), std::min(out.boundsMin.y, m.boundsMin.y), std::min(out.boundsMin.z, m.boundsMin.z)};
            out.boundsMax = {std::max(out.boundsMax.x, m.boundsMax.x), std::max(out.boundsMax.y, m.boundsMax.y), std::max(out.boundsMax.z, m.boundsMax.z)};
        }
    }
    LOG_INFO("campaign: %s %s from %d of %zu units: %zu triangles", cpChapter_.chapter.c_str(), file.c_str(), units, cpChapter_.units.size(), out.triangleCount());
    return !out.empty();
}

// The chapter's starts: the active checkpoint (or the TnCampaignPlayerStart), plus two aux spots 5 m to its sides for the squad
// (PROV: the original's AuxilliarySpawnPoints of the checkpoint are not exported yet; CAMPAIGN_SQUAD_AI_RE section 1).
void World::cpSetStarts(const std::string& checkpointName) {
    const CampaignChapter::Checkpoint* cp = cpChapter_.checkpoint(checkpointName);
    const core::Vec3 at = cp ? cp->pos : cpChapter_.playerStart.pos;
    const std::string actor = cp ? cp->actor : cpChapter_.playerStart.actor;
    std::vector<Match::Start> st;
    for (int k = 0; k < 3; ++k) {
        Match::Start s;
        s.actor = k == 0 ? actor : actor + "_Aux" + std::to_string(k);
        s.cluster = cpChapter_.initialState; s.team = cpChapter_.faction;
        s.pos = at + core::Vec3{k == 1 ? 5.0f : k == 2 ? -5.0f : 0.0f, 0.0f, 0.0f};   // clear of a pawn on the start (Match safe())
        st.push_back(s);
    }
    match_.setCampaignStarts(st);
    LOG_INFO("campaign: start at %s (%.1f %.1f %.1f)%s", actor.c_str(), at.x, at.y, at.z, cp ? " (checkpoint)" : "");
}

// ---- the game (TnCampaignGame) ----

struct CampaignKismetHost : KismetVM::Host {
    World* w = nullptr;
    bool activate(KismetVM& vm, int op, int input) override { return w->cpKismetActivate(vm, op, input); }
    void updateLatent(KismetVM& vm, int op, float dt) override { w->cpKismetLatent(vm, op, dt); }
};

void World::setupCampaign(const MatchLaunch& l) {
    cp_ = CampaignState{};
    if (!match_.settings().campaign) return;
    if (!campaignMap_) { LOG_ERROR("campaign: %s is not a campaign chapter", mapName_.c_str()); return; }
    cp_.active = true;
    cp_.difficulty = std::clamp(l.difficulty, 0, 2);
    cp_.checkpoint = cpChapter_.checkpoint(l.checkpoint) ? cpChapter_.checkpoint(l.checkpoint)->actor : std::string();
    // The human's pick (the in-map CharacterSelect; PROV: ?Character=<name or 0..2>, default the trio's first).
    int pick = 0;
    for (int k = 0; k < (int)cpChapter_.trio.size(); ++k)
        if (alnumLower(cpChapter_.trio[(size_t)k]) == alnumLower(l.character) || l.character == std::to_string(k)) pick = k;
    auto iconic = [](const std::string& chassis) { CharacterSelection s; s.type = 1; s.chassisId = chassis; return s; };
    const std::string mine = chassisForCharacter(cpChapter_.trio[(size_t)pick]);
    for (const std::string& n : cpChapter_.trio) cp_.trioChassis.push_back(chassisForCharacter(n));
    cp_.humanChassis = mine;
    if (localPlayer_ >= 0 && !mine.empty()) match_.selectCharacter(localPlayer_, iconic(mine));
    // SetupAiSquad: the trio minus the humans' picks become AI buddies (IsAiBuddy, DownedEnabled false).
    std::vector<int> others;
    for (int k = 0; k < (int)cpChapter_.trio.size(); ++k) if (k != pick) others.push_back(k);
    BotLaunch b; b.friendly = (int)others.size(); b.enemy = 0; b.difficulty = 1; b.ai = (int)BotAi::Classic;
    const size_t first = match_.players().size();
    const int n = addBots(b);
    for (int k = 0; k < n && k < (int)others.size(); ++k) {
        const int p = (int)(first + (size_t)k);
        const std::string& name = cpChapter_.trio[(size_t)others[(size_t)k]];
        const std::string ch = chassisForCharacter(name);
        MatchPlayer& mp = match_.playerMutable(p);
        mp.name = name;
        if (!ch.empty()) { match_.selectCharacter(p, iconic(ch)); chassisAssets(ch); }
        cp_.squad.push_back(p); cp_.squadChassis.push_back(ch);
        LOG_INFO("campaign: AI squad p%d %s (%s)", p, name.c_str(), ch.c_str());
    }
    {   // the level Kismet (manifests/next_map/<CH>_kismet.json); it begins with InProgress
        assets::Json kj;
        cp_.kismetLoaded = readJson(manifestRoot() + "/next_map/" + cpChapter_.chapter + "_kismet.json", kj) && cp_.vm.load(kj);
        if (!cpHost_) cpHost_ = std::make_unique<CampaignKismetHost>();
        static_cast<CampaignKismetHost*>(cpHost_.get())->w = this;
        cp_.vm.setHost(cpHost_.get());
        cp_.vm.log = std::getenv("WFC_KISMETLOG") != nullptr;
        LOG_INFO("campaign: Kismet %s (%zu ops)", cp_.kismetLoaded ? "loaded" : "MISSING", cp_.vm.opCount());
        if (cp_.kismetLoaded) cpAudioBind();
        cpLoadFactories();
    }
    LOG_INFO("campaign: %s (MapId %d) %s, difficulty %d, the human as %s, squad %zu", cpChapter_.chapter.c_str(), cpChapter_.mapId,
             match_.settings().modeTag.c_str(), cp_.difficulty, cpChapter_.trio[(size_t)pick].c_str(), cp_.squad.size());
}

// Host ops of the level Kismet (PROV for this step: checkpoints, mission complete and streaming are real; the gameplay ops - pawn
// factories, matinee, AI moves, movies, form changes - are held latent until their systems land; conversations take 3 s).
bool World::cpKismetActivate(KismetVM& vm, int op, int input) {
    KismetVM::Op& o = vm.op(op);
    const std::string& c = o.cls;
    if (const int a = cpAudioActivate(vm, op, input)) return a == 2;   // [Systems] the chapter's audio ops (level audio, hosted)
    if (c == "SeqAct_SetActiveCheckpoint") {
        std::string cpPath = vm.objectOf(op, "Checkpoint");
        const CampaignChapter::Checkpoint* cp = cpChapter_.checkpoint(cpPath);
        if (cp) {
            cp_.checkpoint = cp->actor; cp_.checkpointsReached.push_back(cp->actor);
            vm.activeCheckpoint = cp->actor;
            match_.emitCampaignEvent(MatchEvent::Type::CheckpointReached, localPlayer_, cp->actor, cp->displayOrder);
            LOG_INFO("campaign: checkpoint %s (%s, display order %d) reached at t %.1f", cp->actor.c_str(), cp->tag.c_str(), cp->displayOrder, match_.matchTime());
        }
        vm.output(op, "Out");
        return true;
    }
    if (c == "TnSeqAct_MissionComplete") {
        if (!cp_.missionComplete) {
            cp_.missionComplete = true;
            match_.emitCampaignEvent(MatchEvent::Type::MissionComplete, localPlayer_, cpChapter_.chapter, cpChapter_.mapId);
            LOG_INFO("campaign: MissionComplete (%s, MapId %d, difficulty %d) at t %.1f", cpChapter_.chapter.c_str(), cpChapter_.mapId, cp_.difficulty, match_.matchTime());
        }
        o.latent = true;   // the outro movie
        return true;
    }
    if (c == "SeqAct_SetStreamingState") {   // Started at once; Finished when the state is current (every unit of the start is loaded: PROV)
        vm.output(op, "Started"); vm.output(op, "Finished");
        return true;
    }
    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);
    if (c == "SeqAct_Conversation" && input == 0) { o.latent = true; o.timer = 3.0f; vm.output(op, "Played"); return true; }
    if (c == "SeqAct_Interp" || c == "SeqAct_MoviePlayer" || c == "SeqAct_TnAiMoveToActor" ||
        c == "SeqAct_TnForceIntoForm" || c == "SeqAct_ActorFactory" || c == "SeqAct_ActorFactoryEx") { o.latent = true; o.timer = -1.0f; return true; }
    return false;
}

// [Systems] The chapter's audio ops run in the level audio (LevelAudioScript HOSTED, data/campaign/<CH>_audio.json): the VM forwards
// their inputs, their outputs come back through the sink and follow the VM's links. Without an audio device (headless) or an op the
// chapter audio lacks, the stand-ins below stay (a conversation takes 3 s).
void World::cpAudioBind() {
    cp_.audioId.assign(cp_.vm.opCount(), std::string());
    cp_.audioOp.clear(); cp_.audioOps = 0;
    for (size_t i = 0; i < cp_.vm.opCount(); ++i) {
        const std::string& p = cp_.vm.op((int)i).path;
        const size_t k = p.find(".Main_Sequence.");
        if (k == std::string::npos) continue;
        const std::string id = p.substr(0, p.find('.')) + "|" + p.substr(k + 15);
        if (!levelAudio_.hasHostedOp(id)) continue;
        cp_.audioId[i] = id; cp_.audioOp[id] = (int)i; ++cp_.audioOps;
    }
    if (cp_.audioOps == 0) { levelAudio_.clearHosted(); LOG_INFO("campaign: no hosted audio ops (no audio / no chapter audio)"); return; }
    levelAudio_.setHostSink([this](const std::string& id, const std::string& output) {
        auto it = cp_.audioOp.find(id);
        if (it == cp_.audioOp.end()) return;
        const int vop = it->second;
        KismetVM::Op& o = cp_.vm.op(vop);
        if (o.cls.rfind("SeqEvent_", 0) == 0) cp_.vm.fireEvent(vop, output);                         // a touch volume
        else if (o.latent && (output == "Finished" || output == "Stopped")) cp_.vm.finishLatent(vop, output);   // a conversation's end
        else cp_.vm.output(vop, output);                                                            // Played, a line, a scene, a key
    });
    LOG_INFO("campaign: %d Kismet audio ops hosted by the level audio", cp_.audioOps);
}

int World::cpAudioActivate(KismetVM& vm, int op, int input) {
    if ((size_t)op >= cp_.audioId.size() || cp_.audioId[(size_t)op].empty()) return 0;
    KismetVM::Op& o = vm.op(op);
    const auto r = levelAudio_.hostedInput(cp_.audioId[(size_t)op], input, listenerPos_);
    if (r == LevelAudioScript::Hosted::Unknown) return 0;
    if (o.cls == "SeqAct_Interp") return 0;   // its sounds / event keys only: the matinee itself stays Gameplay's (held latent)
    if (r == LevelAudioScript::Hosted::Immediate) return 1;
    if (o.cls == "SeqAct_Conversation" || o.cls == "SeqAct_PlayDialog") o.latent = true;   // until Finished / Stopped
    return 2;
}

void World::cpKismetLatent(KismetVM& vm, int op, float dt) {
    KismetVM::Op& o = vm.op(op);
    if (o.cls == "SeqAct_Conversation" && o.timer > 0.0f && (o.timer -= dt) <= 0.0f) vm.finishLatent(op, "Finished");
}

void World::cpGameOver(int victim) {
    if (cp_.gameOver) return;
    cp_.gameOver = true; cp_.gameOverTime = match_.matchTime(); cp_.gameOverVictim = victim;
    match_.campaignGameOver();   // InProgress.CampaignGameOver: TnCampaignGameOverMessage(WhoDied) -> InProgressOver
    LOG_INFO("campaign: game over (%s died) at t %.1f -> InProgressOver", victim >= 0 ? match_.players()[(size_t)victim].name.c_str() : "?", cp_.gameOverTime);
}

void World::tickCampaign(float dt) {
    if (!cp_.active) return;
    if (!cp_.kismetBegun && cp_.kismetLoaded && match_.state() == Match::State::InProgress) {
        cp_.kismetBegun = true;
        cp_.vm.begin(cp_.checkpoint);
        LOG_INFO("campaign: Kismet begun at %s", cp_.checkpoint.empty() ? "the chapter start" : cp_.checkpoint.c_str());
    }
    if (cp_.kismetBegun && !cp_.gameOver) cp_.vm.tick(dt);
    cpSyncPicks();
    if (!cp_.gameOver) cpTickFactories(dt);
    // TnCampaignGame.Killed: a human killed by anything but DmgType_Suicided -> CampaignGameOver (solo: never downed).
    const auto& kh = match_.killHistory();
    for (; cp_.killsSeen < kh.size(); ++cp_.killsSeen) {
        const KillFeedEntry& k = kh[cp_.killsSeen];
        if (k.victim < 0 || (size_t)k.victim >= match_.players().size()) continue;
        if (match_.players()[(size_t)k.victim].kind == ParticipantKind::Bot || k.damageType.find("Suicided") != std::string::npos) continue;
        if (match_.state() == Match::State::InProgress) cpGameOver(k.victim);
    }
    if (cp_.gameOver && !cp_.gameOverSent && match_.matchTime() - cp_.gameOverTime >= 1.0f) {   // _DelayBeforeShowingGameOverScreen
        cp_.gameOverSent = true;
        match_.emitCampaignEvent(MatchEvent::Type::CampaignGameOver, cp_.gameOverVictim, "", 14);
        LOG_INFO("campaign: SendGameOverEvent (UI event 14) at t %.1f", match_.matchTime());
    }
    // The squad respawns at once (AIRespawnDelay 0) near a player, on the nav (PROV: the nav cell nearest a point 6 m in front of the
    // nearest human; the original takes the closest unoccupied nav point in front of a player).
    if (match_.state() != Match::State::InProgress || cp_.gameOver) return;
    // A squad member stranded 30 s (the versus bots' unstick would suicide): the original squad has no such rule, so it is moved back
    // beside its buddy instead (PROV, PC adaptation; flagged by the strand rules, applied here in the sequential part of the step).
    for (BotBrain& b : bots_) {
        if (!b.cpRecover) continue;
        b.cpRecover = false;
        Character* pc = participantPawnMutable(b.player);
        const Character* lead = cpLeader(b.player);
        if (!pc || !lead) continue;
        core::Vec3 at = lead->position();
        if (botNav_.valid()) { const int c = botNav_.findCell(at, 4.0f); if (c >= 0) at = botNav_.cells()[(size_t)c].centroid; }
        at = at + core::Vec3{std::cos(lead->yaw()) * 3.0f, 0.5f, -std::sin(lead->yaw()) * 3.0f};
        pc->setPosition(at); pc->velocity() = core::Vec3{0, 0, 0};
        b.offMeshRun = 0.0f; b.noWaySince = -1.0f; b.path.clear(); b.wp = 0; b.wantRepath = true;
        LOG_INFO("campaign: squad p%d stranded 30 s: moved back beside its buddy at (%.1f %.1f %.1f)", b.player, at.x, at.y, at.z);
    }
    for (int p : cp_.squad) {
        if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].alive) continue;
        const Character* lead = cpLeader(p);
        if (!lead) continue;
        const float yaw = lead->yaw();
        core::Vec3 at = lead->position() + core::Vec3{-std::sin(yaw) * 6.0f, 0.0f, -std::cos(yaw) * 6.0f};
        if (botNav_.valid()) { const int c = botNav_.findCell(at, 4.0f); if (c >= 0) at = botNav_.cells()[(size_t)c].centroid; else at = lead->position(); }
        match_.restartPlayerAt(p, at, yaw);
        LOG_INFO("campaign: squad p%d respawned at (%.1f %.1f %.1f)", p, at.x, at.y, at.z);
    }
}

const Character* World::cpLeader(int bot) const {
    const Character* me = participantPawn(bot);
    const Character* best = nullptr; float bd = 1e18f;
    for (size_t q = 0; q < match_.players().size(); ++q) {
        if (match_.players()[q].kind == ParticipantKind::Bot) continue;
        const Character* c = participantPawn((int)q);
        if (!c) continue;
        const core::Vec3 d = me ? c->position() - me->position() : core::Vec3{0, 0, 0};
        const float l = d.x * d.x + d.y * d.y + d.z * d.z;
        if (!best || l < bd) { best = c; bd = l; }
    }
    return best;
}

// Move-To-Buddy (the squad's idle goal; Kill outranks it [C]): a spot beside the nearest human, one side per member (PROV formation).
bool World::cpSquadGoal(BotBrain& b, BotGoal& g) {
    if (!cp_.active) return false;
    int idx = -1;
    for (size_t k = 0; k < cp_.squad.size(); ++k) if (cp_.squad[k] == b.player) idx = (int)k;
    if (idx < 0) return false;
    const Character* lead = cpLeader(b.player);
    if (!lead) return false;
    const float yaw = lead->yaw(), side = idx % 2 == 0 ? 3.0f : -3.0f;
    // right = forward x up: forward (-sin, 0, -cos) -> right (cos, 0, -sin) [the Match start yaw convention]
    g = BotGoal{};
    g.kind = BotGoalKind::Defend;
    g.pos = botSnap(lead->position() + core::Vec3{std::cos(yaw) * side + std::sin(yaw) * 3.0f, 0.0f, -std::sin(yaw) * side + std::cos(yaw) * 3.0f});
    g.radius = 3.0f; g.target = -1;
    b.mission = false;
    return true;
}

bool World::cpLeaderMoved(const BotBrain& b) const {
    if (!cp_.active || !b.hasGoal || b.goal.kind != BotGoalKind::Defend) return false;
    if (std::find(cp_.squad.begin(), cp_.squad.end(), b.player) == cp_.squad.end()) return false;
    const Character* lead = cpLeader(b.player);
    if (!lead) return false;
    const core::Vec3 d = lead->position() - b.goal.pos;
    return d.x * d.x + d.z * d.z > 8.0f * 8.0f;
}

} // namespace game
