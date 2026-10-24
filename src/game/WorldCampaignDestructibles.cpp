// Campaign runtime step 9: destructibles - the HmDestructible blueprint state graphs (AssetTools' decoded blueprints: pieces -> states
// with InitialHealth / AutomaticTransitionTime / effects / links with a TransitionTrigger), for the factory-spawned ones (D5's Omega
// batteries, out/campaign_characters/destructibles/<blueprint>.json) and the placed ones (<chapter>/destructibles.json when delivered).
// Damage (hitscan + radius), timers, notification links, Kismet SeqEvent_HmDestructibleStateNotification, the state meshes drawn.
#include "game/World.h"
#include "core/DataRoots.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "core/Log.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {

bool rayAabb(const core::Vec3& o, const core::Vec3& d, float len, const core::Vec3& bmin, const core::Vec3& bmax, float& tHit);   // World.cpp

namespace {

bool readJ2(const std::string& path, assets::Json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), j);
}
std::string tail(const std::string& s) { const size_t k = s.find_last_of('.'); return k == std::string::npos ? s : s.substr(k + 1); }
// AssetTools' provenance wrappers: {"value": v, "confidence": ...} -> v
const assets::Json& val(const assets::Json& j) { return j.isObject() && j.has("value") ? j["value"] : j; }

// The triggers damage drives [C, RE CAMPAIGN_OMEGA_HITREACT_RE 3.4]: GenericDamage (normal damage against the state's InitialHealth);
// TnCorruption only by a damage type carrying it (TnDamageTypeCorruption); TractorBeam (Omega's lock), Notification (script / a
// sibling's DamageDependents), TimerExpired and PawnBaseDied never by damage.
bool damageTrigger(const std::string& t, const std::string& type) {
    if (t.find("Corruption") != std::string::npos) return type.find("Corruption") != std::string::npos;
    if (t.find("TractorBeam") != std::string::npos || t.find("Notification") != std::string::npos || t.find("TimerExpired") != std::string::npos ||
        t.find("PawnBaseDied") != std::string::npos) return false;
    return t.find("Damage") != std::string::npos;
}

} // namespace

// A blueprint's pieces (the decoded schema) into a fresh instance.
static bool buildDes(const assets::Json& bp, const std::string& contentRoot, CampaignDestructible& d) {
    const assets::Json& ps = bp["pieces"];
    std::map<std::string, std::string> meshGltf;
    for (const auto& kv : bp["meshes"].obj) meshGltf[kv.first] = kv.second["gltf"].asString();
    for (size_t i = 0; i < ps.size(); ++i) {
        CampaignDestructible::Piece P;
        P.name = val(ps[i]["PieceName"]).asString();
        const assets::Json& ss = ps[i]["states"];
        for (size_t k = 0; k < ss.size(); ++k) {
            CampaignDestructible::State S;
            S.handle = ss[k]["handle"].asInt(0);
            S.name = val(ss[k]["StateName"]).asString();
            S.health = val(ss[k]["InitialHealth"]).asFloat(0.0f);
            S.autoTime = val(ss[k]["AutomaticTransitionTime"]).asFloat(0.0f);
            for (size_t e = 0; e < ss[k]["effects"].size(); ++e) {
                const assets::Json& eff = ss[k]["effects"][e]["effective"];
                const std::string m = eff["Mesh"].asString().empty() ? eff["MeshTemplate"].asString() : eff["Mesh"].asString();
                if (!m.empty() && S.mesh.empty()) {
                    auto it = meshGltf.find(m); S.mesh = it != meshGltf.end() ? it->second : std::string();
                    // a static-mesh state blocks (the factory's default mesh-component template: Collide / BlockActors); physical
                    // debris (HmPhysicalMeshDestructionEffectFactory) does not (PROV)
                    S.blocks = ss[k]["effects"][e]["class"].asString() == "HmStaticMeshDestructionEffectFactory";
                }
                if (ss[k]["effects"][e]["class"].asString() == "HmDamageDependentsDestructionEffectFactory") S.damageDeps = eff["DamageAmount"].asFloat(0.0f);
            }
            for (size_t l = 0; l < ss[k]["links"].size(); ++l) {
                const assets::Json& L = ss[k]["links"][l];
                if (!val(L["Enabled"]).asBool(true)) continue;
                S.links.push_back({L["NextStateHandle"].asInt(-1), L["TransitionTrigger"].asString()});
            }
            P.states.push_back(S);
        }
        if (!P.states.empty()) d.pieces.push_back(P);
    }
    (void)contentRoot;
    return !d.pieces.empty();
}

CampaignDestructible::State* CampaignDestructible::Piece::state() {
    for (State& s : states) if (s.handle == cur) return &s;
    return nullptr;
}

int World::cpSpawnDestructible(const std::string& blueprint, const std::string& name, const core::Vec3& at, float yawDeg) {
    assets::Json bp;
    const std::string root = core::assetToolsOut() + "campaign_characters/destructibles/";
    if (!readJ2(root + tail(blueprint) + ".json", bp)) { LOG_WARN("campaign: no destructible blueprint %s", blueprint.c_str()); return -1; }
    CampaignDestructible d;
    d.name = name; d.pos = at; d.yawDeg = yawDeg;
    d.posUE = core::Vec3{at.x * 100.0f, at.z * 100.0f, at.y * 100.0f}; d.rotDeg = core::Vec3{0.0f, yawDeg, 0.0f};
    if (!buildDes(bp, "", d)) return -1;
    for (CampaignDestructible::Piece& p : d.pieces) { p.cur = p.states.front().handle; if (auto* s = p.state()) { p.health = s->health; p.timer = s->autoTime; } }
    cp_.destructibles.push_back(d);
    cpDesBounds((int)cp_.destructibles.size() - 1);
    LOG_INFO("campaign: destructible %s (%s) spawned at (%.1f %.1f %.1f), %zu pieces", name.c_str(), tail(blueprint).c_str(), at.x, at.y, at.z, d.pieces.size());
    return (int)cp_.destructibles.size() - 1;
}

// The current states' mesh bounds (the hit box), world space (PROV: the AABB of the rotated mesh box).
void World::cpDesBounds(int i) {
    CampaignDestructible& d = cp_.destructibles[(size_t)i];
    if (d.fixedBounds) return;   // a sentry's hit box (set at its spawn)
    bool any = false;
    core::Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    for (CampaignDestructible::Piece& p : d.pieces) {
        CampaignDestructible::State* s = p.state();
        if (!s || s->mesh.empty()) continue;
        auto it = cpDesMeshes_.find(s->mesh);
        if (it == cpDesMeshes_.end()) {
            CampaignMeshAsset a;
            const std::string contentRoot = core::extractedRoot();
            a.ok = assets::loadGlb(contentRoot + s->mesh, a.mesh) || assets::loadGlb(contentRoot + "VerticalSlice/../" + s->mesh, a.mesh);
            if (a.ok && renderer_) a.handle = renderer_->uploadMesh(a.mesh);
            it = cpDesMeshes_.emplace(s->mesh, std::move(a)).first;
        }
        if (!it->second.ok) continue;
        const render::MeshData& m = it->second.mesh;
        const core::Mat4 M = render::ueActorMatrix(d.posUE, d.rotDeg);
        for (int c = 0; c < 8; ++c) {
            const core::Vec3 q{(c & 1) ? m.boundsMax.x : m.boundsMin.x, (c & 2) ? m.boundsMax.y : m.boundsMin.y, (c & 4) ? m.boundsMax.z : m.boundsMin.z};
            const core::Vec3 w = core::transformPoint(M, q);
            if (!any) { lo = hi = w; any = true; }
            lo = core::Vec3{std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = core::Vec3{std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
        }
    }
    if (!any) { lo = d.pos - core::Vec3{1, 0, 1}; hi = d.pos + core::Vec3{1, 2, 1}; }
    d.lo = lo; d.hi = hi;
    cpDesCollide(i);
}

// The current states' blocking meshes as moving-collision sets (one per piece state, built once; the others disabled).
void World::cpDesCollide(int i) {
    CampaignDestructible& d = cp_.destructibles[(size_t)i];
    if (!d.block || !collision_.valid()) return;
    const core::Mat4 M = render::ueActorMatrix(d.posUE, d.rotDeg);
    for (size_t k = 0; k < d.pieces.size(); ++k) {
        CampaignDestructible::Piece& p = d.pieces[k];
        for (CampaignDestructible::State& s : p.states) {
            const bool want = !d.gone && s.handle == p.cur && s.blocks && !s.mesh.empty();
            if (want && s.colSet < 0) {
                auto it = cpDesMeshes_.find(s.mesh);
                if (it == cpDesMeshes_.end() || !it->second.ok) continue;
                const render::MeshData& m = it->second.mesh;
                std::vector<core::Vec3> tris;
                tris.reserve(m.indices.size());
                for (uint32_t v : m.indices) tris.push_back(core::Vec3{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]});
                if (!tris.empty()) s.colSet = collision_.addDynamicSet(tris, M);
            }
            if (s.colSet >= 0) collision_.setDynamicEnabled(s.colSet, want);
        }
    }
}

// A piece leaves its state through `link`: the new state's health / timer, the other pieces' notification links (DamageDependents,
// PROV), the Kismet notification events of the actor.
void World::cpDesTransition(int i, size_t piece, int to) {
    CampaignDestructible& d = cp_.destructibles[(size_t)i];
    CampaignDestructible::Piece& p = d.pieces[piece];
    p.cur = to;
    if (CampaignDestructible::State* s = p.state()) { p.health = s->health; p.timer = s->autoTime; }
    cpDesBounds(i);
    static const bool dlog = std::getenv("WFC_KISMETLOG") != nullptr;
    if (dlog) LOG_INFO("campaign: destructible %s piece %s -> state %d", d.name.c_str(), p.name.c_str(), to);
    for (int op : d.events) cp_.vm.fireEvent(op, "Out");
    // HmDamageDependentsDestructionEffectFactory on the new state: its DamageAmount to the dependent pieces with its trigger
    // (Notification) [C]; PROV: every other piece of the actor is a dependent (the piece dependency lists are not decoded)
    const CampaignDestructible::State* ns = p.state();
    if (!ns || ns->damageDeps <= 0.0f) return;
    for (size_t k = 0; k < d.pieces.size(); ++k) {
        if (k == piece) continue;
        CampaignDestructible::State* s = d.pieces[k].state();
        if (!s) continue;
        for (const auto& L : s->links)
            if (L.second.find("Notification") != std::string::npos) { cpDesTransition(i, k, L.first); break; }
    }
}

void World::cpDamageDes(int i, float amount, int instigator, const std::string& type) {
    if (i < 0 || (size_t)i >= cp_.destructibles.size() || amount <= 0.0f) return;
    CampaignDestructible& d = cp_.destructibles[(size_t)i];
    if (d.gone || d.off) return;   // SeqAct_TnToggleDestructible Turn Off: not damageable
    d.lastHitter = instigator;
    for (size_t k = 0; k < d.pieces.size(); ++k) {
        CampaignDestructible::Piece& p = d.pieces[k];
        CampaignDestructible::State* s = p.state();
        if (!s || s->health <= 0.0f) continue;
        for (const auto& L : s->links) {
            if (!damageTrigger(L.second, type)) continue;
            if ((p.health -= amount) <= 0.0f) cpDesTransition(i, k, L.first);
            break;
        }
    }
}

int World::cpDesRayHit(const core::Vec3& o, const core::Vec3& dir, float range, float& t) const {
    int best = -1; t = range;
    for (size_t i = 0; i < cp_.destructibles.size(); ++i) {
        const CampaignDestructible& d = cp_.destructibles[i];
        if (d.gone) continue;
        float th;
        if (rayAabb(o, dir, range, d.lo, d.hi, th) && th < t) { t = th; best = (int)i; }
    }
    return best;
}

void World::cpRadiusDes(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type) {
    for (size_t i = 0; i < cp_.destructibles.size(); ++i) {
        const CampaignDestructible& d = cp_.destructibles[i];
        if (d.gone) continue;
        const core::Vec3 c = (d.lo + d.hi) * 0.5f;
        const float dist = std::max(0.0f, core::length(c - at) - core::length(d.hi - d.lo) * 0.5f);
        if (dist < radius) cpDamageDes((int)i, damage * (1.0f - dist / std::max(radius, 1e-3f)), instigator, type);
    }
}

void World::cpTickDestructibles(float dt) {
    for (size_t i = 0; i < cp_.destructibles.size(); ++i)
        for (size_t k = 0; k < cp_.destructibles[i].pieces.size(); ++k) {
            CampaignDestructible::Piece& p = cp_.destructibles[i].pieces[k];
            CampaignDestructible::State* s = p.state();
            if (!s || s->autoTime <= 0.0f || cp_.destructibles[i].gone) continue;
            if ((p.timer -= dt) > 0.0f) continue;
            for (const auto& L : s->links)
                if (L.second.find("TimerExpired") != std::string::npos) { cpDesTransition((int)i, k, L.first); break; }
        }
}

void World::cpDrawDestructibles(render::IRenderer& r) const {
    for (const CampaignDestructible& d : cp_.destructibles)
        for (const CampaignDestructible::Piece& p : d.pieces) {
            if (d.gone) break;
            const CampaignDestructible::State* s = nullptr;
            for (const auto& st : p.states) if (st.handle == p.cur) s = &st;
            if (!s || s->mesh.empty()) continue;
            auto it = cpDesMeshes_.find(s->mesh);
            if (it == cpDesMeshes_.end() || !it->second.ok || it->second.handle < 0) continue;
            r.drawMesh(it->second.handle, render::ueActorMatrix(d.posUE, d.rotDeg), core::Vec3{1, 1, 1});
        }
}

// The Kismet notification events of a destructible: SeqEvent_HmDestructibleStateNotification whose Originator (or ObjName) names it.
void World::cpBindDesEvents(int i) {
    CampaignDestructible& d = cp_.destructibles[(size_t)i];
    d.events.clear();
    for (int op = 0; op < (int)cp_.vm.opCount(); ++op) {
        const KismetVM::Op& o = cp_.vm.op(op);
        if (o.cls != "SeqEvent_HmDestructibleStateNotification" && o.cls != "SeqEvent_Destroyed") continue;
        const std::string org = tail(o.authored["Originator"].asString());
        const std::string objName = o.authored["ObjName"].asString();
        if (org == d.name || (org.empty() && objName.rfind(d.name + " ", 0) == 0)) d.events.push_back(op);
    }
}

// The placed destructibles (AssetTools <chapter>/destructibles.json: one row per actor, its blueprint decoded or by name).
void World::cpLoadPlacedDestructibles() {
    assets::Json j;
    if (!readJ2(cpChapter_.dir + "destructibles.json", j)) return;
    const assets::Json& rows = j["destructibles"];
    std::map<std::string, assets::Json> blueprints;
    for (const auto& kv : j["blueprints"].obj) blueprints[kv.first] = kv.second;
    int n = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        const assets::Json& row = rows[r];
        const assets::Json* bp = nullptr;
        if (row.has("pieces")) bp = &row;
        else if (auto it = blueprints.find(row["blueprint"].asString()); it != blueprints.end()) bp = &it->second;
        if (!bp) continue;
        CampaignDestructible d;
        d.name = row["actor"].asString();
        const assets::Json& L = row["location_gltf"];
        if (L.size() >= 3) d.pos = core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        d.yawDeg = row["rotation_deg"]["yaw"].asFloat(0.0f);
        d.rotDeg = core::Vec3{row["rotation_deg"]["pitch"].asFloat(0.0f), d.yawDeg, row["rotation_deg"]["roll"].asFloat(0.0f)};
        {
            const assets::Json& U = row["location_ue"];
            d.posUE = U.isObject() ? core::Vec3{U["X"].asFloat(), U["Y"].asFloat(), U["Z"].asFloat()}
                                   : U.size() >= 3 ? core::Vec3{U[0].asFloat(), U[1].asFloat(), U[2].asFloat()} : core::Vec3{d.pos.x * 100.0f, d.pos.z * 100.0f, d.pos.y * 100.0f};
        }
        d.block = row["collision_flags"]["bBlockActors"].asBool(true);
        if (!buildDes(*bp, "", d)) continue;
        for (CampaignDestructible::Piece& p : d.pieces) { p.cur = p.states.front().handle; if (auto* s = p.state()) { p.health = s->health; p.timer = s->autoTime; } }
        cp_.destructibles.push_back(d);
        cpDesBounds((int)cp_.destructibles.size() - 1);
        cpBindDesEvents((int)cp_.destructibles.size() - 1);
        ++n;
    }
    LOG_INFO("campaign: %d placed destructibles", n);
}

} // namespace game

namespace game {

// SeqAct_TnToggleDestructible (Turn On / Turn Off / Toggle) [H: Off = not damageable, e.g. A1's "can't shoot the targets anymore"].
bool World::cpToggleDestructible(KismetVM& vm, int op, int input) {
    const KismetVM::Op& o = vm.op(op);
    if (o.cls != "SeqAct_TnToggleDestructible") return false;
    for (int v : vm.varLink(op, "Target")) {
        const KismetVM::Var* var = vm.var(v);
        if (var && var->named >= 0) var = vm.var(var->named);
        if (!var) continue;
        const std::string nm = var->obj.rfind("des:", 0) == 0 ? std::string() : tail(var->obj);
        for (size_t i = 0; i < cp_.destructibles.size(); ++i) {
            CampaignDestructible& d = cp_.destructibles[i];
            if (!(nm.empty() ? var->obj == "des:" + std::to_string(i) : d.name == nm)) continue;
            d.off = input == 1 ? true : input == 0 ? false : !d.off;
        }
    }
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

// A destructible factory's Kill All: its spawned actors leave the world (not drawn, no collision, no damage).
void World::cpRemoveDestructible(int i) {
    if (i < 0 || (size_t)i >= cp_.destructibles.size()) return;
    cp_.destructibles[(size_t)i].gone = true;
    cpDesCollide(i);
}

} // namespace game
