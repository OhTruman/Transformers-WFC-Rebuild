// Escalation DLC map mechanisms: the presentation [Rendering]. Gameplay's SvMechState (WorldSurvivalMech.cpp) goes to the renderer
// every frame: the matinees at the sim's positions (Pulse's doors and ramp, Tranquillity's drain / base doors / waterfalls, District's
// laser button, Static's fire jets; their toggle tracks switch the emitters), the laser grids' and sentries' emitters, the power
// outage's post-process grade, the actors unhidden at Drain Completed, and District's sentry turrets (the TnSentry archetypes'
// destructible meshes: LightSentry_TURRETDEF -> WEP_SentryBlaster_SKEL, RepairSentry_TURRETDEF -> WEP_SentryRepair_SKEL, the
// Activate / DeActivate clips; the whole turret turns to the sim's yaw, as the ability sentry). Maps without mechanisms: no-ops.
#include "game/World.h"
#include "render/Renderer.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>

namespace game {

void World::svMechPresent(render::IRenderer& r) const {
    const SvMech& M = svData_.mech;
    if (!M.any()) return;
    const SvMechState& S = sv_.mech;
    const bool live = sv_.phase != SurvivalState::Phase::Off;
    auto at = [&](const std::vector<float>& v, size_t k) { return live && k < v.size() ? v[k] : -1.0f; };
    for (size_t k = 0; k < M.lasers.size(); ++k) {
        const SvMech::Laser& L = M.lasers[k];
        const bool on = live && k < S.lasers.size() && S.lasers[k].active;
        const float sw = live && k < S.lasers.size() ? S.lasers[k].switchPos : L.switchLength;
        // the button and switch matinees start together: forward (disabled) from 0, reversed (bought) from their ends
        const float btn = on ? std::max(0.0f, L.buttonLength - (L.switchLength - sw)) : std::min(L.buttonLength, sw);
        if (!L.switchInterp.empty()) r.setMatineePosition(L.switchInterp, sw);
        if (!L.buttonInterp.empty()) r.setMatineePosition(L.buttonInterp, btn);
        for (const std::string& e : L.emitters) r.setMapEffectActive(e, on);
    }
    for (size_t k = 0; k < M.sentries.size(); ++k) {
        const bool on = live && k < S.sentries.size() && S.sentries[k].active;
        for (const std::string& e : M.sentries[k].emitters) r.setMapEffectActive(e, on);
    }
    if (M.outage.on) {   // PostProcessVolume ppvOn (the dark grade) while the power is out, ppvOff otherwise; the screens' matinee
        const bool out = live && S.outageLeft > 0.0f;
        if (!M.outage.ppvOn.empty()) r.setPostProcessVolumeEnabled(M.outage.ppvOn, out);
        if (!M.outage.ppvOff.empty()) r.setPostProcessVolumeEnabled(M.outage.ppvOff, !out);
        if (!M.outage.interp.empty()) r.setMatineePosition(M.outage.interp, live ? S.outagePos : 0.0f);
    }
    for (size_t k = 0; k < M.floods.size(); ++k) {   // a bought floodgate: its waterfall / splash off, its material matinee
        const float t = at(S.floodT, k);
        if (!M.floods[k].waterfallInterp.empty()) r.setMatineePosition(M.floods[k].waterfallInterp, t);
        if (!M.floods[k].materialInterp.empty()) r.setMatineePosition(M.floods[k].materialInterp, t);
    }
    if (!M.drainInterp.empty()) r.setMatineePosition(M.drainInterp, live ? S.drainT : 0.0f);
    for (const std::string& a : M.unhideAtDrain) r.setActorHidden(a, !(live && S.drained));
    for (const SvMech::BaseDoor& B : M.baseDoors) {   // held at doorHold until OpenBase; the pistons hidden once "Opened"
        const float t = live ? S.doorPos : M.doorHold;
        if (!B.interp.empty()) r.setMatineePosition(B.interp, t);
        for (const std::string& p : B.pistons) r.setActorHidden(p, t >= M.doorOpened);
    }
    for (size_t k = 0; k < M.managers.size(); ++k)   // a fired Zone_Manager's door / ramp matinees
        for (const std::string& i : M.managers[k].interps) r.setMatineePosition(i, at(S.managerT, k));
    for (size_t k = 0; k < M.traps.size(); ++k) {   // a trap's matinee while active, held at its end while cooling, then unplayed
        float t = -1.0f;
        if (live && k < S.traps.size()) t = S.traps[k].active ? S.traps[k].t : S.traps[k].cooling ? M.traps[k].time : -1.0f;
        if (!M.traps[k].interp.empty()) r.setMatineePosition(M.traps[k].interp, t);
    }
}

void World::svMechEnsureModels(const std::string& dir) {   // main thread (textures: GL), once a map with sentries is up
    if (svMechModelsTried_ || svData_.mech.sentries.empty() || !onMainThread()) return;
    svMechModelsTried_ = true;
    const char* skel[2] = {"SentryBlaster/WEP_SentryBlaster_SKEL.gltf", "SentryRepair/WEP_SentryRepair_SKEL.gltf"};
    const char* anim[2] = {"SentryBlaster/WEP_SentryBlaster_ANIM.anim.gltf", "SentryRepair/WEP_SentryRepair_ANIM.anim.gltf"};
    for (int i = 0; i < 2; ++i) {
        assets::SkinnedModel& m = svSentryModel_[i];
        if (assets::loadSkinnedGlb(dir + skel[i], m) && m.valid()) {
            assets::loadAnimationsByName(dir + anim[i], m);
            resolveModelTextures(m);
            prewarmSkin(m);
        } else LOG_ERROR("escalation: sentry turret %s unavailable", skel[i]);
    }
}

void World::svMechDrawSentries(render::IRenderer& r) const {
    const SvMech& M = svData_.mech;
    if (M.sentries.empty()) return;
    const SvMechState& S = sv_.mech;
    if (svSentryPose_.size() != M.sentries.size()) svSentryPose_.assign(M.sentries.size(), {});
    const float now = mapState_.clock();
    for (size_t k = 0; k < M.sentries.size(); ++k) {
        const SvMech::Sentry& D = M.sentries[k];
        const assets::SkinnedModel& mdl = svSentryModel_[D.repair ? 1 : 0];
        if (!mdl.valid()) continue;
        SvSentryPose& P = svSentryPose_[k];
        const bool on = sv_.phase != SurvivalState::Phase::Off && k < S.sentries.size() && S.sentries[k].active;
        if (on != P.on) { P.on = on; P.since = now; P.everOn |= on; }
        const int clip = mdl.clipByName(std::string(D.repair ? "SentryRepair_" : "SentryBlaster_") + (on ? "Activate" : "DeActivate"));
        if (clip >= 0) {   // never woken: the dormant pose (DeActivate's end); the clips clamp at their ends, skinned once more
            const float dur = mdl.clips[(size_t)clip].duration;
            const float t = P.everOn ? std::min(now - P.since, dur) : dur;
            if (clip != P.clip || t != P.t || P.mesh.positions.empty()) {
                assets::LocalPose lp; std::vector<core::Mat4> g;
                assets::samplePose(mdl, clip, t, false, lp);
                assets::skinPose(mdl, lp, g, P.mesh);
                P.clip = clip; P.t = t;
            }
        }
        if (P.mesh.positions.empty()) continue;
        const float yaw = k < S.sentries.size() ? S.sentries[k].yaw : D.yaw;
        r.drawDynamicMesh(P.mesh, core::Mat4::translate(D.pos) * core::Mat4::rotateY(yaw + core::config::kMeshYawOffset), core::Vec3{1, 1, 1});
    }
}

} // namespace game
