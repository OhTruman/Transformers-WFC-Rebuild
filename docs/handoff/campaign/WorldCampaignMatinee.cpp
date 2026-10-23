// Campaign runtime step 5: Matinee (SeqAct_Interp over AssetTools manifests/next_map/<CH>_matinee.json) - the timeline (Play / Reverse /
// Stop / Pause / Change Dir, Completed / Reversed), event keys (the op's outputs), InterpActor movement (the chapter's movers: moving
// collision, the renderer's actor transform, pawns standing on them carried along: the A1 intro lift), and the director (camera cuts to
// the CameraActor groups, FOVAngle, the fade) into the local view + Rendering's CinematicView. See Campaign.h.
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
    for (const std::string& u : cpChapter_.units) {
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

// ---- the matinee data ----

void World::cpLoadMatinees() {
    cp_.matinees.clear(); cp_.interps.clear();
    const char* mr = std::getenv("WFC_MANIFESTS");
    assets::Json j;
    if (!readJ(std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/next_map/" + cpChapter_.chapter + "_matinee.json", j)) {
        LOG_WARN("campaign: no matinee data for %s", cpChapter_.chapter.c_str());
        return;
    }
    const assets::Json& ms = j["matinees"];
    for (size_t i = 0; i < ms.size(); ++i) {
        const assets::Json& m = ms[i];
        CampaignMatinee M;
        M.length = m["length"].asFloat(0.0f);
        for (size_t g = 0; g < m["groups"].size(); ++g) {
            const assets::Json& G = m["groups"][g];
            CampaignMatinee::Group grp;
            grp.name = G["name"].asString();
            grp.director = G["class"].asString() == "InterpGroupDirector";
            for (const auto& kv : G["bound"].obj) {
                std::vector<std::string> as;
                for (size_t a = 0; a < kv.second["actors"].size(); ++a) as.push_back(shortName(kv.second["actors"][a].asString()));
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
            cp_.vm.finishLatent(in.op, in.dir > 0.0f ? "Completed" : "Reversed");
        }
    }
    // pawns standing on a moving InterpActor ride it (PROV base: inside its footprint, on its top)
    for (CampaignMover& m : cpMovers_) {
        const core::Vec3 d = m.offset - m.lastOffset;
        m.lastOffset = m.offset;
        if (m.pawnSet < 0 || (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f)) continue;
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
            else if (t.kind == 0 && act != g.actors.end()) {
                float p[3], r[3];
                evalKeys(t.pos, to, p); evalKeys(t.euler, to, r);
                for (const std::string& a : act->second)
                    for (CampaignMover& m : cpMovers_) {
                        if (m.name != a) continue;
                        m.offset = t.relative ? ueToGltf(p) : ueToGltf(p) - m.pivot;
                        if (m.pawnSet >= 0) collision_.setDynamicPose(m.pawnSet, core::Mat4::translate(m.pivot + m.offset));
                        if (renderer_) {
                            const core::Vec3 w = m.pivot + m.offset;
                            renderer_->setMapActorTransform(m.name, core::Vec3{w.x * 100.0f, w.z * 100.0f, w.y * 100.0f}, core::Vec3{r[1], r[2], r[0]});
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
    if ((haveFade || cp_.cineOp == in.op) && renderer_) {
        render::IRenderer::CinematicView v;
        v.active = true;
        v.fade[0] = v.fade[1] = v.fade[2] = 0.0f; v.fade[3] = haveFade ? fade : 0.0f;
        renderer_->setCinematicView(v);
        cp_.cineViewOp = in.op;
    }
}

void World::cpMatineeEnd(CampaignState::Interp& in) {
    if (cp_.cineOp == in.op) {
        player_.controller().clearSpectatorView();
        if (renderer_) renderer_->notifyCameraCut("matinee end");
        cp_.cineOp = -1; cp_.cineCamGroup.clear();
    }
    if (cp_.cineViewOp == in.op && renderer_) {
        render::IRenderer::CinematicView v; v.active = false;
        renderer_->setCinematicView(v);
        cp_.cineViewOp = -1;
    }
}

} // namespace game
