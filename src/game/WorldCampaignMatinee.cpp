// Campaign runtime step 5: Matinee (SeqAct_Interp over AssetTools manifests/next_map/<CH>_matinee.json) - the timeline (Play / Reverse /
// Stop / Pause / Change Dir, Completed / Reversed), event keys (the op's outputs), InterpActor movement (the chapter's movers: moving
// collision, the renderer's actor transform, pawns standing on them carried along: the A1 intro lift), and the director (camera cuts to
// the CameraActor groups, FOVAngle, the fade) into the local view + Rendering's CinematicView. See Campaign.h.
#include "game/World.h"
#include "core/DataRoots.h"
#include "assets/Json.h"
#include "assets/SkinnedModel.h"
#include "game/GpuSkin.h"
#include "core/LoadYield.h"
#include "render/Renderer.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {

namespace {

bool readJ(const std::string& path, assets::Json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), j);
}
std::string shortName(const std::string& s) { const size_t k = s.find_last_of('.'); return k == std::string::npos ? s : s.substr(k + 1); }

// UE3 curve keys (FInterpCurve): CIM_Linear, CIM_Constant, CIM_Curve* (Hermite with the authored arrive / leave tangents).
std::vector<CampaignMatinee::Key> keysOf(const assets::Json& a) {
    std::vector<CampaignMatinee::Key> out;
    for (size_t i = 0; i < a.size(); ++i) {
        const assets::Json& k = a[i];
        CampaignMatinee::Key K;
        K.t = k["t"].asFloat();
        auto vec = [](const assets::Json& v, float o[3]) {
            if (v.isNumber()) { o[0] = v.asFloat(); o[1] = o[2] = 0.0f; return; }
            o[0] = v["X"].asFloat(); o[1] = v["Y"].asFloat(); o[2] = v["Z"].asFloat();
        };
        vec(k["v"], K.v); vec(k["arrive"], K.ai); vec(k["leave"], K.lo);
        const std::string m = k["mode"].asString();
        K.mode = m == "CIM_Linear" ? 0 : m == "CIM_Constant" ? 1 : 2;
        out.push_back(K);
    }
    return out;
}

void evalKeys(const std::vector<CampaignMatinee::Key>& k, float t, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (k.empty()) return;
    if (t <= k.front().t) { for (int c = 0; c < 3; ++c) out[c] = k.front().v[c]; return; }
    if (t >= k.back().t) { for (int c = 0; c < 3; ++c) out[c] = k.back().v[c]; return; }
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].t < t) ++i;
    const CampaignMatinee::Key& a = k[i];
    const CampaignMatinee::Key& b = k[i + 1];
    const float dt = std::max(1e-6f, b.t - a.t), s = (t - a.t) / dt;
    for (int c = 0; c < 3; ++c) {
        if (a.mode == 1) out[c] = a.v[c];
        else if (a.mode == 0) out[c] = a.v[c] + (b.v[c] - a.v[c]) * s;
        else {
            const float s2 = s * s, s3 = s2 * s;
            out[c] = (2 * s3 - 3 * s2 + 1) * a.v[c] + (s3 - 2 * s2 + s) * dt * a.lo[c] + (-2 * s3 + 3 * s2) * b.v[c] + (s3 - s2) * dt * b.ai[c];
        }
    }
}

core::Vec3 ueToGltf(const float u[3]) { return core::Vec3{u[0] * 0.01f, u[2] * 0.01f, u[1] * 0.01f}; }

} // namespace

// ---- movers: the chapter's InterpActors (physics.json "movers" per unit) as moving collision ----

void World::cpCollectMovers() {
    cpMovers_.clear();
    for (const std::string& u : cpChapter_.allUnits) {
        assets::Json pj;
        if (!readJ(cpChapter_.dir + u + "/physics.json", pj)) continue;
        const assets::Json& mv = pj["movers"];
        for (size_t i = 0; i < mv.size(); ++i) {
            CampaignMover m;
            m.name = shortName(mv[i]["actor"].asString());
            const assets::Json& L = mv[i]["location_ue"];
            const float lu[3] = {L["X"].asFloat(), L["Y"].asFloat(), L["Z"].asFloat()};
            m.pivot = ueToGltf(lu);
            cpMovers_.push_back(m);
        }
    }
}

// Split the movers' triangles out of the merged static collision (by collision node name), pivot-relative.
void World::cpSplitMovers(const render::MeshData& in, render::MeshData& out) {
    out.positions = in.positions;
    for (const render::SubMesh& sm : in.subs) {
        CampaignMover* mv = nullptr;
        for (CampaignMover& m : cpMovers_) if (m.name == sm.nodeName || sm.nodeName.rfind(m.name, 0) == 0) { mv = &m; break; }
        for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
            if (!mv) { out.indices.push_back(in.indices[i]); continue; }
            const uint32_t v = in.indices[i];
            const core::Vec3 p{in.positions[v * 3], in.positions[v * 3 + 1], in.positions[v * 3 + 2]};
            mv->tris.push_back(p - mv->pivot);
            mv->lo = mv->tris.size() == 1 ? p : core::Vec3{std::min(mv->lo.x, p.x), std::min(mv->lo.y, p.y), std::min(mv->lo.z, p.z)};
            mv->hi = mv->tris.size() == 1 ? p : core::Vec3{std::max(mv->hi.x, p.x), std::max(mv->hi.y, p.y), std::max(mv->hi.z, p.z)};
        }
    }
    int n = 0;
    for (const CampaignMover& m : cpMovers_) n += !m.tris.empty();
    LOG_INFO("campaign: %zu movers, %d with collision split out", cpMovers_.size(), n);
}

void World::cpRegisterMovers() {
    for (CampaignMover& m : cpMovers_)
        if (!m.tris.empty()) m.pawnSet = collision_.addDynamicSet(m.tris, core::Mat4::translate(m.pivot));
}

// Mover-attached nav cells (AssetTools bot_nav flags.mover_attached + mover_base: one per MoverPathNode, e.g. A1's intro lift): bound
// to their mover once the nav is loaded; moved with it when a matinee leaves it at rest (PROV: not while it moves - the squad rides
// it physically; Gameplay's nav grid is xz, so a vertical lift keeps its buckets).
void World::cpBindMoverNav() {
    if (!botNav_.valid()) return;
    int n = 0;
    for (CampaignMover& m : cpMovers_) {
        m.navCells.clear();
        for (size_t c = 0; c < botNav_.cells().size(); ++c)
            if (!botNav_.cells()[c].moverBase.empty() && shortName(botNav_.cells()[c].moverBase) == m.name) m.navCells.push_back((int)c);
        n += (int)m.navCells.size();
        // The walkable floor of a mover with nav cells: its collision footprint (x / z of the split-out triangles) at its cells' height, as
        // a collision set riding with it. A1's lift collision is the rim only (the floor is TnTransformingWorldActor_6275's skeletal pod,
        // no collision row), so the pawns fell to the bottom of the shaft at the start (PROV floor until a collision export of the pod).
        if (!m.navCells.empty() && m.floorSet < 0 && !m.tris.empty()) {
            float y = 0.0f;
            for (int c : m.navCells) y += botNav_.cells()[(size_t)c].centroid.y;
            y /= (float)m.navCells.size();
            const core::Vec3 at = m.pivot + m.offset;
            const core::Vec3 a{m.lo.x, y, m.lo.z}, b{m.hi.x, y, m.lo.z}, c{m.hi.x, y, m.hi.z}, d{m.lo.x, y, m.hi.z};
            const std::vector<core::Vec3> ft{a - at, c - at, b - at, a - at, d - at, c - at};
            m.floorSet = collision_.addDynamicSet(ft, core::Mat4::translate(at));
            LOG_INFO("campaign: %s floor %.1f x %.1f m at y %.1f (its nav cells' height)", m.name.c_str(), m.hi.x - m.lo.x, m.hi.z - m.lo.z, y);
        }
    }
    if (n > 0) LOG_INFO("campaign: %d mover-attached nav cells bound", n);
}

void World::cpSyncMoverNav() {
    if (cp_.transformMoved && botNav_.valid()) { cp_.transformMoved = false; transforming_.applyNavBlocks(botNav_); }   // a moved transforming actor's nav blocks at rest
    for (CampaignMover& m : cpMovers_) {
        const core::Vec3 d = m.offset - m.navOffset;
        if (m.navCells.empty() || (!m.navMoving && std::fabs(d.x) < 0.01f && std::fabs(d.y) < 0.01f && std::fabs(d.z) < 0.01f)) continue;
        m.navMoving = false;
        botNav_.translateCells(m.navCells, d);
        const int links = botNav_.relinkCells(m.navCells);   // the portals where it rests (not the old landing's)
        m.navOffset = m.offset;
        for (BotBrain& b : bots_) { b.path.clear(); b.wp = 0; b.wantRepath = true; }   // every plan replans on the moved nav
        LOG_INFO("campaign: %zu nav cells of %s moved by (%.1f %.1f %.1f), %d portals at rest", m.navCells.size(), m.name.c_str(), d.x, d.y, d.z, links);
    }
}

// ---- the matinee data ----

void World::cpLoadMatinees() {
    cp_.matinees.clear(); cp_.interps.clear();
    const char* mr = std::getenv("WFC_MANIFESTS");
    assets::Json j;
    if (!readJ((mr ? std::string(mr) : core::projectRoot() + "/AssetTools/manifests") + "/next_map/" + cpChapter_.chapter + "_matinee.json", j)) {
        LOG_WARN("campaign: no matinee data for %s", cpChapter_.chapter.c_str());
        return;
    }
    const assets::Json& ms = j["matinees"];
    for (size_t i = 0; i < ms.size(); ++i) {
        const assets::Json& m = ms[i];
        CampaignMatinee M;
        M.length = m["length"].asFloat(0.0f);
        M.interpData = m["interp_data"].asString();
        for (size_t g = 0; g < m["groups"].size(); ++g) {
            const assets::Json& G = m["groups"][g];
            CampaignMatinee::Group grp;
            grp.name = G["name"].asString();
            grp.director = G["class"].asString() == "InterpGroupDirector";
            for (const auto& kv : G["bound"].obj) {
                std::vector<std::string> as;
                for (size_t a = 0; a < kv.second["actors"].size(); ++a) {   // a runtime character binding stays "CHARACTER:<ROBODEF>"
                    const std::string full = kv.second["actors"][a].asString();
                    as.push_back(full.rfind("CHARACTER:", 0) == 0 ? full : shortName(full));
                }
                grp.actors[kv.first] = as;
            }
            for (size_t t = 0; t < G["tracks"].size(); ++t) {
                const assets::Json& T = G["tracks"][t];
                if (T["disabled"].asBool(false)) continue;
                const std::string c = T["class"].asString();
                CampaignMatinee::Track tr;
                if (c == "InterpTrackMove") { tr.kind = 0; tr.relative = T["move_frame"].asString() == "IMF_RelativeToInitial"; tr.pos = keysOf(T["pos"]); tr.euler = keysOf(T["euler"]); }
                else if (c == "InterpTrackEvent") { tr.kind = 1; for (size_t k = 0; k < T["keys"].size(); ++k) tr.events.push_back({T["keys"][k]["t"].asFloat(), T["keys"][k]["event"].asString()}); }
                else if (c == "InterpTrackDirector") { tr.kind = 2; for (size_t k = 0; k < T["cuts"].size(); ++k) tr.events.push_back({T["cuts"][k]["t"].asFloat(), T["cuts"][k]["camera_group"].asString()}); }
                else if (c == "InterpTrackFade") { tr.kind = 3; tr.curve = keysOf(T["curve"]); }
                else if (c == "InterpTrackFloatProp" && T["property"].asString() == "FOVAngle") { tr.kind = 4; tr.curve = keysOf(T["curve"]); }
                else if (c == "InterpTrackAnimControl") {   // kind 5: (t, AnimSeqName, StartOffset, AnimPlayRate, bLooping) keys
                    tr.kind = 5;
                    for (size_t k = 0; k < T["keys"].size(); ++k) {
                        const assets::Json& K = T["keys"][k];
                        tr.events.push_back({K["t"].asFloat(), K["anim"].asString()});
                        tr.animOffset.push_back(K["start_offset"].asFloat(0.0f)); tr.animRate.push_back(K["rate"].asFloat(1.0f));
                        tr.animLoop.push_back(K["looping"].asBool(false) ? 1 : 0);
                    }
                }
                else continue;
                grp.tracks.push_back(tr);
            }
            M.groups.push_back(grp);
        }
        const int mi = (int)cp_.matinees.size();
        for (size_t p = 0; p < m["played_by"].size(); ++p) {
            const assets::Json& P = m["played_by"][p];
            CampaignState::Interp in;
            in.matinee = mi;
            in.action = P["action"].asString();
            in.op = cp_.vm.find(in.action);
            in.rate = P["play_rate"].asFloat(1.0f);
            in.looping = P["looping"].asBool(false);
            in.rewindOnPlay = P["rewind_on_play"].asBool(false);
            if (in.op >= 0) cp_.interps.push_back(in);
        }
        cp_.matinees.push_back(M);
    }
    LOG_INFO("campaign: %zu matinees, %zu SeqAct_Interp ops bound", cp_.matinees.size(), cp_.interps.size());
}

// SeqAct_Interp inputs [H stock]: 0 Play, 1 Reverse, 2 Stop, 3 Pause, 4 Change Dir.
bool World::cpInterpActivate(KismetVM& vm, int op, int input) {
    for (CampaignState::Interp& in : cp_.interps) {
        if (in.op != op) continue;
        const CampaignMatinee& M = cp_.matinees[(size_t)in.matinee];
        KismetVM::Op& o = vm.op(op);
        if (input == 0 || input == 1) {
            const bool rev = input == 1;
            if (!in.playing) {
                if (!rev && (in.rewindOnPlay || in.pos >= M.length)) in.pos = 0.0f;
                if (rev && in.pos <= 0.0f) in.pos = M.length;
            }
            in.playing = true; in.dir = rev ? -1.0f : 1.0f;
            LOG_INFO("campaign: matinee %s %s at %.2f / %.2f s", shortName(in.action).c_str(), rev ? "reverse" : "play", in.pos, M.length);
            o.latent = true; o.timer = -1.0f;
            cpMatineeApply(in, in.pos, in.pos);   // the start pose / camera at once
        } else if (input == 2) { in.playing = false; o.latent = false; cpMatineeEnd(in); }
        else if (input == 3) in.playing = !in.playing;
        else if (input == 4) in.dir = -in.dir;
        return true;
    }
    return false;
}

void World::cpTickMatinees(float dt) {
    for (CampaignState::Interp& in : cp_.interps) {
        if (!in.playing) continue;
        const CampaignMatinee& M = cp_.matinees[(size_t)in.matinee];
        const float from = in.pos;
        in.pos += dt * in.rate * in.dir;
        bool done = false;
        if (in.dir > 0.0f && in.pos >= M.length) { if (in.looping) in.pos = std::fmod(in.pos, std::max(1e-3f, M.length)); else { in.pos = M.length; done = true; } }
        if (in.dir < 0.0f && in.pos <= 0.0f) { if (in.looping) in.pos += M.length; else { in.pos = 0.0f; done = true; } }
        cpMatineeApply(in, from, in.pos);
        if (done) {
            in.playing = false;
            cpMatineeEnd(in);
            cpSyncMoverNav();   // the mover-attached nav cells take the mover's resting pose
            cp_.vm.finishLatent(in.op, in.dir > 0.0f ? "Completed" : "Reversed");
        }
    }
    cpFlushCinematicView();
    // pawns standing on a moving InterpActor ride it (PROV base: inside its footprint, on its top)
    for (CampaignMover& m : cpMovers_) {
        const core::Vec3 d = m.offset - m.lastOffset;
        m.lastOffset = m.offset;
        if ((m.pawnSet < 0 && m.floorSet < 0) || (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f)) continue;
        if (!m.navCells.empty()) {   // its nav cells ride with it (relinked where it comes to rest: cpSyncMoverNav)
            botNav_.translateCells(m.navCells, m.offset - m.navOffset);   // (a matinee end this tick may have synced them already)
            m.navOffset = m.offset;
            m.navMoving = true;
        }
        const core::Vec3 lo = m.lo + m.lastOffset - d, hi = m.hi + m.lastOffset - d;
        for (size_t q = 0; q < match_.players().size(); ++q) {
            Character* c = participantPawnMutable((int)q);
            if (!c) continue;
            const core::Vec3 p = c->position();
            if (p.x < lo.x - 0.5f || p.x > hi.x + 0.5f || p.z < lo.z - 0.5f || p.z > hi.z + 0.5f || p.y < hi.y - 1.5f || p.y > hi.y + 4.0f) continue;
            c->setPosition(p + d);
            c->groundY += d.y;
        }
    }
}

// Evaluate the matinee at `to` (event keys crossed from `from`): movers, the director's camera, FOV and fade.
void World::cpMatineeApply(CampaignState::Interp& in, float from, float to) {
    const CampaignMatinee& M = cp_.matinees[(size_t)in.matinee];
    const float lo = std::min(from, to), hi = std::max(from, to);
    std::string camGroup;
    float fade = 0.0f; bool haveFade = false;
    for (const CampaignMatinee::Group& g : M.groups) {
        auto act = g.actors.find(in.action);
        for (const CampaignMatinee::Track& t : g.tracks) {
            if (t.kind == 1) {   // event keys -> the SeqAct_Interp's output of that name
                for (const auto& e : t.events) if ((e.first > lo && e.first <= hi) || (from == to && e.first == to && to == 0.0f)) cp_.vm.output(in.op, e.second);
            } else if (t.kind == 2) {
                for (const auto& e : t.events) if (e.first <= to) camGroup = e.second;
            } else if (t.kind == 3) { float v[3]; evalKeys(t.curve, to, v); fade = v[0]; haveFade = true; }
            else if (t.kind == 5 && (act == g.actors.end() || act->second.empty() || act->second[0].rfind("CHARACTER:", 0) == 0)) {   // AnimControl on a pawn group (CHARACTER:<ROBODEF>)
                int k = -1;
                for (size_t e = 0; e < t.events.size(); ++e) if (t.events[e].first <= to) k = (int)e;
                auto pg = cpPawnGroups_.find(M.interpData + "|" + g.name);
                if (k >= 0 && pg != cpPawnGroups_.end())
                    cpPlayPawnClip(in, pg->second, t.events[(size_t)k].second,
                                   (to - t.events[(size_t)k].first) * t.animRate[(size_t)k] + t.animOffset[(size_t)k], t.animLoop[(size_t)k] != 0);
            }
            else if (t.kind == 5 && act != g.actors.end()) {   // AnimControl: the clip of the last key at or before `to`
                int k = -1;
                for (size_t e = 0; e < t.events.size(); ++e) if (t.events[e].first <= to) k = (int)e;
                if (k >= 0)
                    for (const std::string& a : act->second) {
                        auto sk = cpSkel_.find(a);
                        if (sk == cpSkel_.end()) continue;
                        sk->second.clip = t.events[(size_t)k].second;
                        sk->second.time = (to - t.events[(size_t)k].first) * t.animRate[(size_t)k] + t.animOffset[(size_t)k];
                        sk->second.loop = t.animLoop[(size_t)k] != 0;
                        sk->second.active = true;
                    }
            }
            else if (t.kind == 0 && act != g.actors.end()) {
                float p[3], r[3];
                evalKeys(t.pos, to, p); evalKeys(t.euler, to, r);
                for (const std::string& a : act->second) {
                    bool mover = false;
                    for (const CampaignMover& m : cpMovers_) if (m.name == a) mover = true;
                    core::Vec3 base;
                    if (!mover && a.rfind("CHARACTER:", 0) != 0 && cpActorPos(a, base)) {   // a non-mover actor: e.g. A1's lift pod (TnTransformingWorldActor_6275)
                        const core::Vec3 w = t.relative ? base + ueToGltf(p) : ueToGltf(p);
                        if (renderer_) renderer_->setMapActorTransform(a, core::Vec3{w.x * 100.0f, w.z * 100.0f, w.y * 100.0f}, core::Vec3{r[1], r[2], r[0]});
                        const int ti = transforming_.find(a);   // a transforming actor: its collision rides along (re-posed next tick)
                        if (ti >= 0) { transforming_.setOffset((size_t)ti, w - base); cp_.transformMoved = true; }
                    }
                    for (CampaignMover& m : cpMovers_) {
                        if (m.name != a) continue;
                        m.offset = t.relative ? ueToGltf(p) : ueToGltf(p) - m.pivot;
                        if (m.pawnSet >= 0) collision_.setDynamicPose(m.pawnSet, core::Mat4::translate(m.pivot + m.offset));
                        if (m.floorSet >= 0) collision_.setDynamicPose(m.floorSet, core::Mat4::translate(m.pivot + m.offset));
                        if (renderer_) {
                            const core::Vec3 w = m.pivot + m.offset;
                            renderer_->setMapActorTransform(m.name, core::Vec3{w.x * 100.0f, w.z * 100.0f, w.y * 100.0f}, core::Vec3{r[1], r[2], r[0]});
                        }
                    }
                }
            }
        }
    }
    // The director: the current camera group's CameraActor pose (an absolute movement track; RelativeToInitial adds the actor's
    // location from actors.json), its FOVAngle (default 90), the fade.
    if (!camGroup.empty() && camGroup != "DirGroup") {
        for (const CampaignMatinee::Group& g : M.groups) {
            if (g.name != camGroup) continue;
            core::Vec3 pos{0, 0, 0}; float rot[3] = {0, 0, 0}, fov = 90.0f; bool have = false;
            for (const CampaignMatinee::Track& t : g.tracks) {
                if (t.kind == 0) {
                    float p[3]; evalKeys(t.pos, to, p); evalKeys(t.euler, to, rot);
                    pos = ueToGltf(p);
                    if (t.relative) { auto act = g.actors.find(in.action); core::Vec3 base; if (act != g.actors.end() && !act->second.empty() && cpActorPos(act->second[0], base)) pos = pos + base; }
                    have = true;
                } else if (t.kind == 4) { float v[3]; evalKeys(t.curve, to, v); fov = v[0]; }
            }
            if (!have) break;
            const float ueYaw = rot[2] * 3.14159265f / 180.0f, uePitch = rot[1] * 3.14159265f / 180.0f;
            const float yaw = std::atan2(-std::cos(ueYaw), -std::sin(ueYaw));
            player_.controller().setSpectatorView(pos, yaw, uePitch, fov);
            if (camGroup != cp_.cineCamGroup && renderer_) renderer_->notifyCameraCut("matinee");
            cp_.cineCamGroup = camGroup; cp_.cineOp = in.op;
            break;
        }
    }
    // the view: this matinee's fade, held while it is active (playing or paused); combined per tick (cpFlushCinematicView)
    in.viewing = haveFade || cp_.cineOp == in.op;
    if (haveFade) in.lastFade = fade;
}

// One CinematicView per tick: the strongest fade of the active matinees (a paused hold-black keeps holding: A1 Intro_Setup).
void World::cpFlushCinematicView() {
    bool any = false; float fade = 0.0f; std::string who;
    for (const CampaignState::Interp& in : cp_.interps)
        if (in.viewing && in.op >= 0 && cp_.vm.op(in.op).latent) { any = true; if (in.lastFade >= fade) { fade = in.lastFade; who = in.action; } }
    if (!renderer_) return;
    if (any) {
        render::IRenderer::CinematicView v;
        v.active = true; v.fade[0] = v.fade[1] = v.fade[2] = 0.0f; v.fade[3] = fade;
        renderer_->setCinematicView(v);
        if (!cp_.viewOn) LOG_INFO("campaign: cinematic view on (%s, fade %.2f)", shortName(who).c_str(), fade);
        cp_.viewOn = true;
    } else if (cp_.viewOn) {
        render::IRenderer::CinematicView v; v.active = false;
        renderer_->setCinematicView(v);
        cp_.viewOn = false;
        LOG_INFO("campaign: cinematic view off");
    }
}

void World::cpMatineeEnd(CampaignState::Interp& in) {
    LOG_INFO("campaign: matinee %s ends at %.2f s", shortName(in.action).c_str(), in.pos);
    in.viewing = false;
    for (int p : in.cinePawns) if (Character* c = participantPawnMutable(p)) c->clearCineClip();   // the pawns go back to their own animation
    in.cinePawns.clear();
    if (cp_.cineOp == in.op) {
        player_.controller().clearSpectatorView();
        if (renderer_) renderer_->notifyCameraCut("matinee end");
        cp_.cineOp = -1; cp_.cineCamGroup.clear();
    }
}

} // namespace game

namespace game {

// The matinee-animated skeletal actors (AssetTools <chapter>/matinee_skeletal.json: actor, mesh_gltf, anim_files, placement).
void World::cpLoadSkeletal() {
    cpSkel_.clear();
    assets::Json j;
    if (!readJ(cpChapter_.dir + "matinee_skeletal.json", j)) return;
    const assets::Json& rows = j["rows"];
    for (size_t i = 0; i < rows.size(); ++i) {
        const assets::Json& r = rows[i];
        CampaignSkel k;
        k.mesh = r["mesh_gltf"].asString();
        for (size_t f = 0; f < r["anim_files"].size(); ++f) k.animFiles.push_back(r["anim_files"][f].asString());
        const assets::Json& L = r["location_gltf"];
        if (L.size() >= 3) k.pos = core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        k.yawDeg = r["rotation_deg"]["yaw"].asFloat(0.0f);
        k.object = r["object"].asString();
        k.unit = r["sublevel"].asString();   // the level package; the unit dir drops its "_m"
        if (k.unit.size() > 2 && k.unit.compare(k.unit.size() - 2, 2, "_m") == 0) k.unit.resize(k.unit.size() - 2);
        if (!k.mesh.empty()) cpSkel_[shortName(r["actor"].asString())] = k;
    }
    // (300 fps: a first-draw load stalled D1 4.5 s in play) every distinct mesh + anim set loads here, under the loading screen
    const std::string root = core::extractedRoot();
    std::map<std::string, std::shared_ptr<assets::SkinnedModel>> models;
    size_t failed = 0;
    for (auto& kv : cpSkel_) {
        CampaignSkel& k = kv.second;
        std::string key = k.mesh;
        for (const std::string& f : k.animFiles) key += "|" + f;
        auto it = models.find(key);
        if (it == models.end()) {
            auto m = std::make_shared<assets::SkinnedModel>();
            if (assets::loadSkinnedGlb(root + k.mesh, *m)) {
                for (const std::string& f : k.animFiles) assets::loadAnimationsByName(root + f, *m);
                if (renderer_ && onMainThread()) prewarmSkin(*m);
            } else { LOG_WARN("campaign: skeletal actor %s: no mesh %s", kv.first.c_str(), k.mesh.c_str()); m.reset(); ++failed; }
            it = models.emplace(key, m).first;
            core::loadYield("campaign skeletal");
        }
        k.model = it->second; k.tried = true; k.ok = k.model != nullptr;
    }
    LOG_INFO("campaign: %zu matinee skeletal actors (%zu distinct models loaded, %zu failed)", cpSkel_.size(), models.size(), failed);
}

// Draw the active ones: the clip sampled at its matinee time. GPU-skinned (D1 Crash runs 244 at once: CPU skinning them every
// frame cost ~45 ms); the palette is rebuilt only when the clip / time changes. The CPU skin draws when the renderer cannot.
void World::cpDrawSkeletal(render::IRenderer& r) const {
    const std::string root = core::extractedRoot();
    for (auto& kv : cpSkel_) {
        CampaignSkel& k = kv.second;
        if (!k.active || k.clip.empty()) continue;
        {   // (300 fps, RE CAMPAIGN_CULLING_LOD_RE.md 5-6) not in the original's world: its unit not Visible, or hidden / destroyed
            auto uv = cp_.unitVisible.find(k.unit);
            if (uv != cp_.unitVisible.end() && !uv->second) continue;
            auto h = cp_.actorHidden.find(k.object.empty() ? kv.first : k.object);
            if (h == cp_.actorHidden.end()) h = cp_.actorHidden.find(kv.first);
            if (h != cp_.actorHidden.end() && h->second) continue;
        }
        if (!k.ok || !k.model) continue;   // (loaded at chapter load: cpLoadSkeletal)
        const assets::SkinnedModel& model = *k.model;
        const int clip = model.clipByName(k.clip);
        if (clip < 0) continue;
        const core::Mat4 world = core::Mat4::translate(k.pos) * core::Mat4::rotateY(-k.yawDeg * 3.14159265f / 180.0f);
        if (!model.joints.empty()) {
            if (clip != k.posedClip || k.time != k.posedT || k.palette.empty()) {
                assets::LocalPose lp;
                assets::samplePose(model, clip, k.time, k.loop, lp);
                assets::poseGlobals(model, lp, k.scratch);
                buildPalette(model, k.scratch, k.palette);
                k.posedClip = clip; k.posedT = k.time; ++k.serial;
            }
            render::MeshData& bind = bindMeshOf(model);
            syncMats(bind.mats, model.mats);
            if (drawSkinnedGpu(r, bind, model.joints, model.weights, k.palette, nullptr, 1.0f, world, core::Vec3{1, 1, 1}, &k, k.serial, 0))
                continue;
        }
        assets::evaluatePose(model, clip, k.time, k.scratch, k.skinned, k.loop);
        r.drawDynamicMesh(k.skinned, world, core::Vec3{1, 1, 1});
    }
}

} // namespace game

namespace game {

// The pawn groups of the chapter's matinees (matinee_skeletal.json pawn_groups: CHARACTER:<ROBODEF> bindings + clip -> anim file) and
// the ROBODEF -> chassis map (roster_package.json chassis[].robot.definition).
void World::cpLoadPawnGroups() {
    cpPawnGroups_.clear();
    assets::Json j;
    if (!readJ(cpChapter_.dir + "matinee_skeletal.json", j)) return;
    static std::map<std::string, std::string> robodefChassis;
    if (robodefChassis.empty()) {
        assets::Json r;
        const char* mr = std::getenv("WFC_MANIFESTS");
        if (readJ((mr ? std::string(mr) : core::projectRoot() + "/AssetTools/manifests") + "/mp_content/roster_package.json", r))
            for (const auto& kv : r["chassis"].obj) robodefChassis[kv.second["robot"]["definition"].asString()] = kv.first;
    }
    const assets::Json& gs = j["pawn_groups"];
    for (size_t i = 0; i < gs.size(); ++i) {
        const assets::Json& g = gs[i];
        CampaignPawnGroup P;
        for (size_t b = 0; b < g["bound"].size(); ++b) {
            const std::string s = g["bound"][b].asString();
            if (s.rfind("CHARACTER:", 0) == 0) { auto it = robodefChassis.find(s.substr(10)); if (it != robodefChassis.end()) P.chassis.push_back(it->second); }
        }
        for (const auto& kv : g["clip_map"].obj) P.clipFile[kv.first] = kv.second.asString();
        if (!P.chassis.empty()) cpPawnGroups_[g["interp_data"].asString() + "|" + g["group"].asString()] = P;
    }
    LOG_INFO("campaign: %zu matinee pawn groups bound to characters", cpPawnGroups_.size());
    // Every participant chassis' clips load now (cpPlayPawnClip loaded each at its first play: a 5-38 ms step). Load scheduling only.
    int preloaded = 0;
    for (size_t q = 0; q < match_.players().size(); ++q) {
        const std::string ch = resolveChassis(match_.players()[q].selection, match_.faction((int)q));
        const ChassisAssets* ca = chassisAssets(ch);
        if (!ca || !ca->ok) continue;
        assets::SkinnedModel& model = const_cast<assets::SkinnedModel&>(ca->robot);
        for (const auto& kv : cpPawnGroups_) {
            if (std::find(kv.second.chassis.begin(), kv.second.chassis.end(), ch) == kv.second.chassis.end()) continue;
            for (const auto& cf : kv.second.clipFile)
                if (model.clipByName(cf.first) < 0) { assets::loadAnimationsByName(core::extractedRoot() + cf.second, model); ++preloaded; }
        }
    }
    if (preloaded) LOG_INFO("campaign: %d matinee pawn clip files preloaded", preloaded);
    cpCharacterTargets("");   // its ROBODEF -> chassis table (roster_package.json) builds now, not in the first Character-targeted op
}

// Play `clip` at `time` on every participant of the group's characters (the human or the squad holding that chassis): the clip is
// appended to the chassis' robot model from its anim file once, then the pawn's forced-clip slot holds it while the matinee drives it.
void World::cpPlayPawnClip(CampaignState::Interp& in, const CampaignPawnGroup& g, const std::string& clip, float time, bool loop) {
    for (size_t q = 0; q < match_.players().size(); ++q) {
        const std::string ch = resolveChassis(match_.players()[q].selection, match_.faction((int)q));
        if (std::find(g.chassis.begin(), g.chassis.end(), ch) == g.chassis.end()) continue;
        Character* c = participantPawnMutable((int)q);
        const ChassisAssets* ca = chassisAssets(ch);
        if (!c || !ca || !ca->ok) continue;
        assets::SkinnedModel& model = const_cast<assets::SkinnedModel&>(ca->robot);
        int ci = model.clipByName(clip);
        if (ci < 0) {
            auto f = g.clipFile.find(clip);
            if (f != g.clipFile.end()) assets::loadAnimationsByName(core::extractedRoot() + f->second, model);
            ci = model.clipByName(clip);
            LOG_INFO("campaign: matinee clip %s on %s (p%zu): %s", clip.c_str(), ch.c_str(), q, ci >= 0 ? "loaded" : "NOT FOUND");
            if (ci < 0) continue;
        }
        c->setCineClip(ci, time, loop);
        if (std::find(in.cinePawns.begin(), in.cinePawns.end(), (int)q) == in.cinePawns.end()) in.cinePawns.push_back((int)q);
    }
}

} // namespace game
