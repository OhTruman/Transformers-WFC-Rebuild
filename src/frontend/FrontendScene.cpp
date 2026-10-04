#include "frontend/FrontendScene.h"
#include "assets/Json.h"
#include "frontend/FlowTrace.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace frontend {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kUUToDeg = 360.0 / 65536.0;

// FRotationMatrix(pitch, yaw, roll) in degrees: rows are the rotated X (forward), Y (right), Z (up) axes.
void rotMatrix(const double prDeg[3], double m[9]) {
    double P = prDeg[0] * kPi / 180, Y = prDeg[1] * kPi / 180, R = prDeg[2] * kPi / 180;
    double SP = std::sin(P), CP = std::cos(P), SY = std::sin(Y), CY = std::cos(Y), SR = std::sin(R), CR = std::cos(R);
    m[0] = CP * CY; m[1] = CP * SY; m[2] = SP;
    m[3] = SR * SP * CY - CR * SY; m[4] = SR * SP * SY + CR * CY; m[5] = -SR * CP;
    m[6] = -(CR * SP * CY + SR * SY); m[7] = CY * SR - CR * SP * SY; m[8] = CR * CP;
}

// FMatrix::Rotator.
void matrixRotator(const double m[9], double out[3]) {
    out[0] = std::atan2(m[2], std::sqrt(m[0] * m[0] + m[1] * m[1])) * 180 / kPi;
    out[1] = std::atan2(m[1], m[0]) * 180 / kPi;
    double noRoll[9], pr[3] = {out[0], out[1], 0};
    rotMatrix(pr, noRoll);
    const double* sy = noRoll + 3;
    out[2] = std::atan2(m[6] * sy[0] + m[7] * sy[1] + m[8] * sy[2], m[3] * sy[0] + m[4] * sy[1] + m[5] * sy[2]) * 180 / kPi;
}

// child (rows in parent space) composed with parent -> rows in world space.
void compose(const double child[9], const double parent[9], double out[9]) {
    double r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = child[i * 3] * parent[j] + child[i * 3 + 1] * parent[3 + j] + child[i * 3 + 2] * parent[6 + j];
    std::copy(r, r + 9, out);
}

void transformPoint(const double m[9], const double origin[3], const double p[3], double out[3]) {
    double r[3];
    for (int j = 0; j < 3; ++j) r[j] = p[0] * m[j] + p[1] * m[3 + j] + p[2] * m[6 + j] + origin[j];
    std::copy(r, r + 3, out);
}

std::vector<FrontendScene::Key> keys(const assets::Json& a) {
    std::vector<FrontendScene::Key> out;
    for (size_t i = 0; i < a.size(); ++i) {
        const assets::Json& k = a[i];
        FrontendScene::Key K;
        K.t = k["t"].asDouble();
        for (int c = 0; c < 3; ++c) { K.v[c] = k["v"][c].asDouble(); K.ai[c] = k["ai"][c].asDouble(); K.lo[c] = k["lo"][c].asDouble(); }
        const std::string& mode = k["mode"].asString();
        K.mode = mode == "CIM_Constant" ? 1 : mode.rfind("CIM_Curve", 0) == 0 ? 2 : 0;
        out.push_back(K);
    }
    return out;
}

void vec3(const assets::Json& a, double out[3]) { for (int c = 0; c < 3; ++c) out[c] = a[c].asDouble(); }
} // namespace

// UE3 FInterpCurve::Eval: clamped ends; linear / constant / Hermite (tangents scaled by the segment length).
void FrontendScene::evalCurve(const std::vector<Key>& k, double t, double out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (k.empty()) return;
    if (k.size() == 1 || t <= k.front().t) { std::copy(k.front().v, k.front().v + 3, out); return; }
    if (t >= k.back().t) { std::copy(k.back().v, k.back().v + 3, out); return; }
    size_t i = 1;
    while (i < k.size() && k[i].t < t) ++i;
    const Key& a = k[i - 1];
    const Key& b = k[i];
    double d = b.t - a.t, s = d > 0 ? (t - a.t) / d : 0;
    for (int c = 0; c < 3; ++c) {
        if (a.mode == 1) out[c] = a.v[c];
        else if (a.mode == 2) {
            double s2 = s * s, s3 = s2 * s;
            out[c] = (2 * s3 - 3 * s2 + 1) * a.v[c] + (s3 - 2 * s2 + s) * a.lo[c] * d + (-2 * s3 + 3 * s2) * b.v[c] + (s3 - s2) * b.ai[c] * d;
        } else out[c] = a.v[c] + (b.v[c] - a.v[c]) * s;
    }
}

bool FrontendScene::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) return false;
    for (const auto& [ui, list] : j["scenes"].obj)
        for (size_t i = 0; i < list.size(); ++i) scenes_[ui].push_back(list[i].asString());
    for (const auto& [name, L] : j["levels"].obj) {
        Level& lv = data_[name];
        const assets::Json& acts = L["actors"];
        for (size_t i = 0; i < acts.size(); ++i) {
            const assets::Json& a = acts[i];
            Actor A;
            A.name = a["name"].asString();
            A.cls = a["class"].asString();
            vec3(a["location"], A.loc);
            double ruu[3]; vec3(a["rotation"], ruu);
            for (int c = 0; c < 3; ++c) A.rot[c] = ruu[c] * kUUToDeg;
            A.base = a["base"].asString();
            if (a.has("relativeLocation")) {
                vec3(a["relativeLocation"], A.relLoc);
                vec3(a["relativeRotation"], ruu);
                for (int c = 0; c < 3; ++c) A.relRot[c] = ruu[c] * kUUToDeg;
            }
            A.fov = a["fov"].asDouble(0);
            A.camera = A.cls.find("CameraActor") != std::string::npos;
            lv.actors[A.name] = A;
        }
        const assets::Json& ms = L["matinees"];
        for (size_t i = 0; i < ms.size(); ++i) {
            const assets::Json& m = ms[i];
            Matinee M;
            M.name = m["name"].asString();
            M.comment = m["comment"].asString();
            M.looping = m["looping"].asBool();
            M.length = m["length"].asDouble();
            for (size_t g = 0; g < m["groups"].size(); ++g) {
                const assets::Json& G = m["groups"][g];
                Group gr;
                gr.name = G["name"].asString();
                gr.director = G["class"].asString() == "InterpGroupDirector";
                for (size_t a = 0; a < G["actors"].size(); ++a) gr.actors.push_back(G["actors"][a].asString());
                for (size_t t = 0; t < G["tracks"].size(); ++t) {
                    const assets::Json& T = G["tracks"][t];
                    if (T["class"].asString() == "InterpTrackMove") {
                        MoveTrack mt;
                        mt.relativeToInitial = T["frame"].asString() == "IMF_RelativeToInitial";
                        mt.pos = keys(T["pos"]);
                        mt.euler = keys(T["euler"]);
                        gr.moves.push_back(mt);
                    } else if (T["class"].asString() == "InterpTrackDirector") {
                        for (size_t c = 0; c < T["cuts"].size(); ++c)
                            gr.cuts.push_back({T["cuts"][c]["t"].asDouble(), T["cuts"][c]["group"].asString()});
                    }
                }
                M.groups.push_back(gr);
            }
            for (size_t s = 0; s < m["startedBy"].size(); ++s) {
                const assets::Json& S = m["startedBy"][s];
                if (S["input"].asString() != "Play") continue;
                if (S["class"].asString() == "GFxEvent_FsCommand" && S["fscommand"].isString()) M.fscommands.push_back(S["fscommand"].asString());
                if (S["class"].asString() == "SeqAct_MoviePlayer" && S["output"].asString() == "Stopped") M.onMovieStopped = true;
            }
            lv.matinees.push_back(M);
        }
    }
    loaded_ = !data_.empty();
    return loaded_;
}

void FrontendScene::enterLevel(const std::string& uiLevel) {
    playing_.clear();
    time_ = 0.0;
    auto it = scenes_.find(uiLevel);
    levels_ = it == scenes_.end() ? std::vector<std::string>{} : it->second;
    FlowTrace::emit("scene.enter", {{"uiLevel", uiLevel}, {"levels", std::to_string(levels_.size())}});
}

void FrontendScene::leave() {
    playing_.clear();
    levels_.clear();
}

void FrontendScene::start(const Matinee& m) {
    for (Playing& p : playing_) if (p.m == &m) { p.t = 0; p.order = ++order_; return; }   // Play restarts
    playing_.push_back({&m, 0.0, ++order_});
    FlowTrace::emit("scene.matinee", {{"matinee", m.name}, {"comment", m.comment}, {"looping", FlowTrace::boolean(m.looping)},
                                      {"length", FlowTrace::num(m.length)}});
}

void FrontendScene::trigger(const std::string& trig) {
    bool fs = trig.rfind("FsCommand:", 0) == 0, movie = trig.rfind("MovieStopped:", 0) == 0;
    std::string cmd = fs ? trig.substr(10) : std::string();
    for (const std::string& l : levels_) {
        auto it = data_.find(l);
        if (it == data_.end()) continue;
        for (const Matinee& m : it->second.matinees) {
            bool go = (fs && std::find(m.fscommands.begin(), m.fscommands.end(), cmd) != m.fscommands.end()) || (movie && m.onMovieStopped);
            if (go) start(m);
        }
    }
}

void FrontendScene::tick(double dt) {
    time_ += dt;
    for (Playing& p : playing_) {
        p.t += dt;
        if (p.m->looping && p.m->length > 0) p.t = std::fmod(p.t, p.m->length);
    }
}

std::vector<std::string> FrontendScene::playing() const {
    std::vector<std::string> v;
    for (const Playing& p : playing_) v.push_back(p.m->comment.empty() ? p.m->name : p.m->comment);
    return v;
}

const FrontendScene::Actor* FrontendScene::findActor(const std::string& name) const {
    for (const std::string& l : levels_) {
        auto it = data_.find(l);
        if (it == data_.end()) continue;
        auto a = it->second.actors.find(name);
        if (a != it->second.actors.end()) return &a->second;
    }
    return nullptr;
}

void FrontendScene::actorWorld(const std::string& name, double pos[3], double rotM[9], int depth) const {
    const Actor* a = findActor(name);
    double ident[3] = {0, 0, 0};
    if (!a || depth > 8) { std::copy(ident, ident + 3, pos); rotMatrix(ident, rotM); return; }
    // Local transform: relative to the base when hard-attached, else world.
    bool attached = !a->base.empty();
    double lp[3], lr[3];
    std::copy(attached ? a->relLoc : a->loc, (attached ? a->relLoc : a->loc) + 3, lp);
    std::copy(attached ? a->relRot : a->rot, (attached ? a->relRot : a->rot) + 3, lr);
    double lm[9];
    rotMatrix(lr, lm);
    // Matinee move tracks on this actor (latest started wins). RelativeToInitial: the track transform in the
    // initial frame (InitialTM * track); IMF_World: the track is the transform. Euler track = (roll, pitch, yaw).
    const Playing* best = nullptr;
    const MoveTrack* trk = nullptr;
    for (const Playing& p : playing_)
        for (const Group& g : p.m->groups)
            if (!g.moves.empty() && std::find(g.actors.begin(), g.actors.end(), name) != g.actors.end() && (!best || p.order > best->order)) {
                best = &p; trk = &g.moves.front();
            }
    if (trk) {
        double tp[3], te[3];
        evalCurve(trk->pos, best->t, tp);
        evalCurve(trk->euler, best->t, te);
        double tr[3] = {te[1], te[2], te[0]}, tm[9];
        rotMatrix(tr, tm);
        if (trk->relativeToInitial) {
            double np[3];
            transformPoint(lm, lp, tp, np);
            std::copy(np, np + 3, lp);
            compose(tm, lm, lm);
        } else {
            std::copy(tp, tp + 3, lp);
            std::copy(tm, tm + 9, lm);
        }
    }
    if (attached) {
        double bp[3], bm[9];
        actorWorld(a->base, bp, bm, depth + 1);
        transformPoint(bm, bp, lp, pos);
        compose(lm, bm, rotM);
    } else {
        std::copy(lp, lp + 3, pos);
        std::copy(lm, lm + 9, rotM);
    }
}

SceneView FrontendScene::view() const {
    SceneView v;
    v.levels = levels_;
    v.time = time_;
    if (levels_.empty()) return v;
    // The camera: the latest director cut among the playing matinees, else a camera group's actor, else the scene's
    // authored camera (first CameraActor away from the origin, persistent level first).
    std::string cam, from;
    int bestOrder = -1;
    for (const Playing& p : playing_) {
        for (const Group& g : p.m->groups) {
            if (!g.director || g.cuts.empty() || p.order < bestOrder) continue;
            std::string target;
            for (const auto& c : g.cuts) if (c.first <= p.t + 1e-6) target = c.second;
            if (target.empty()) target = g.cuts.front().second;
            for (const Group& tg : p.m->groups)
                if (tg.name == target && !tg.actors.empty()) { cam = tg.actors.front(); from = p.m->comment; bestOrder = p.order; }
        }
    }
    if (cam.empty())
        for (const std::string& l : levels_) {
            auto it = data_.find(l);
            if (it == data_.end() || !cam.empty()) continue;
            for (const auto& [n, a] : it->second.actors)
                if (a.camera && (a.loc[0] != 0 || a.loc[1] != 0 || !a.base.empty())) { cam = n; break; }
        }
    const Actor* a = cam.empty() ? nullptr : findActor(cam);
    if (!a) return v;
    double m[9];
    actorWorld(cam, v.pos, m);
    matrixRotator(m, v.rot);
    v.fov = a->fov > 0 ? a->fov : 90.0;
    v.camera = cam;
    v.matinee = from;
    v.valid = true;
    return v;
}

} // namespace frontend
