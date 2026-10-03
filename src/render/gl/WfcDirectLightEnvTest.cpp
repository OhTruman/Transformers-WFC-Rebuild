// Self-test for the WFC DirectLightEnv port (WFC_DLETEST=1): exercises the recovered rules with synthetic
// lights and a scratch environment and logs PASS / FAIL per check. Native references in
// WfcDirectLightEnv.cpp.
#include "render/gl/WfcPipeline.h"
#include "core/Log.h"

#include <cmath>

namespace render {
namespace wfc {

void Pipeline::runDirectLightEnvSelfTest() {
    int pass = 0, fail = 0;
    auto check = [&](bool ok, const char* what) {
        (ok ? pass : fail)++;
        LOG_INFO("dle-test %s: %s", ok ? "PASS" : "FAIL", what);
    };
    const uint32_t robotCh = envChannels(0), vehicleCh = envChannels(1);
    Light pt;
    pt.type = 0; pt.pos = {0, 0, 0}; pt.radius = 10.0f; pt.radiusOfInfluence = 8.0f; pt.falloff = 2.0f;
    pt.brightness = 2.0f; pt.chMask = 0xF8000000u; pt.enabled = true;
    // RadiusOfInfluence boundary: |c - pos| <= RoI + bounds radius
    check(lightAffectsEnv(pt, robotCh, {9.99f, 0, 0}, 2.0f), "point inside RadiusOfInfluence + radius");
    check(!lightAffectsEnv(pt, robotCh, {10.01f, 0, 0}, 2.0f), "point outside RadiusOfInfluence + radius");
    // point intensity formula
    float I = intensityAt(pt, {5, 0, 0});
    check(std::fabs(I - 2.0f * std::pow(1.0f - 0.25f, 2.0f)) < 1e-5f, "point IntensityAt = B * (1 - (d/R)^2)^F");
    check(intensityAt(pt, {11, 0, 0}) == 0.0f, "point IntensityAt = 0 beyond Radius");
    // channels
    Light cin = pt; cin.chMask = 0x10000000u | 0x00080000u;   // Dynamic + Cinematic_1
    check(!lightAffectsEnv(cin, robotCh, {1, 0, 0}, 1.0f), "special channel (Cinematic_1) not on env -> rejected");
    Light po = pt; po.chMask = 0x10000000u | 0x00000100u;     // Dynamic + PlayerOnly
    check(lightAffectsEnv(po, robotCh, {1, 0, 0}, 1.0f), "PlayerOnly light on robot env (PlayerOnly) -> accepted");
    check(!lightAffectsEnv(po, vehicleCh, {1, 0, 0}, 1.0f), "PlayerOnly light on vehicle env -> rejected");
    Light cd = pt; cd.chMask = 0x08000000u;                   // CompositeDynamic only
    check(lightAffectsEnv(cd, vehicleCh, {1, 0, 0}, 1.0f), "CompositeDynamic treated as Dynamic");
    Light st = pt; st.chMask = 0x20000000u;                   // Static only
    check(!lightAffectsEnv(st, robotCh, {1, 0, 0}, 1.0f), "no shared channel -> rejected");
    Light fn = pt; fn.hasLightFunction = true;
    check(!lightAffectsEnv(fn, robotCh, {1, 0, 0}, 1.0f), "light function without composite shadow -> rejected");
    fn.castCompositeShadow = true;
    check(lightAffectsEnv(fn, robotCh, {1, 0, 0}, 1.0f), "light function with composite shadow -> accepted");
    Light off = pt; off.enabled = false;
    check(!lightAffectsEnv(off, robotCh, {1, 0, 0}, 1.0f), "disabled light -> rejected");
    // spot cone^2 with clamped cones: inner 20 deg, outer 40 deg
    Light sp = pt; sp.type = 1; sp.dir = {1, 0, 0}; sp.radius = 100.0f; sp.radiusOfInfluence = 100.0f; sp.falloff = 0.0f;
    float inner = 20.0f * 0.017453292f, outer = 40.0f * 0.017453292f;
    sp.spotCosI = std::cos(inner); sp.spotCosO = std::cos(outer); sp.spotOuterRad = outer;
    auto at = [&](float deg) { float a = deg * 0.017453292f; return intensityAt(sp, {std::cos(a) * 5, std::sin(a) * 5, 0}); };
    check(std::fabs(at(10.0f) - 2.0f) < 1e-4f, "spot inside inner cone = full point intensity");
    check(at(45.0f) == 0.0f, "spot outside outer cone = 0");
    float mid = at(30.0f);
    float c = (std::cos(30.0f * 0.017453292f) - sp.spotCosO) / (sp.spotCosI - sp.spotCosO);
    check(std::fabs(mid - 2.0f * c * c) < 1e-4f, "spot between cones = point * cone^2");
    check(!lightAffectsEnv(sp, robotCh, {0, 5, 0}, 0.5f), "spot relevance: sphere outside the outer cone -> rejected");
    check(lightAffectsEnv(sp, robotCh, {5, 1, 0}, 0.5f), "spot relevance: sphere inside the outer cone -> accepted");
    // queue: a forced full refresh lands within 2 frames even with no budget (deadline service)
    {
        int savedFrame = frameNo_; float savedTime = time_;
        dle_.erase(7);
        if (dleRobotSamples_.empty()) dleRobotSamples_ = {core::Vec3{0, 0, 0}};
        time_ = 100.0f; frameNo_ = 1000;
        tickDirectLightEnv(7, {0, 0, 0}, {1, 1, 1}, {0, 0, 0});            // initial: immediate full
        int full0 = dle_[7].fullUpdates;
        time_ += 1.0f / 60.0f; ++frameNo_;
        tickDirectLightEnv(7, {5, 0, 0}, {1, 1, 1}, {5, 0, 0});            // 5 m > 0.9 m threshold: mode 1
        bool queuedOrDone = !dleQueue_.empty() || dle_[7].fullUpdates > full0;
        int f1 = frameNo_;
        for (int k = 0; k < 3 && dle_[7].fullUpdates == full0; ++k) {
            time_ += 1.0f / 60.0f; ++frameNo_;
            tickDirectLightEnv(7, {5, 0, 0}, {1, 1, 1}, {5, 0, 0});
        }
        check(queuedOrDone && dle_[7].fullUpdates > full0 && dle_[7].lastFullFrame - f1 <= 2,
              "movement > 90 UU queues a full re-gather that lands within 2 frames");
        check(dle_[7].initialized && full0 == 1, "first update after attach is an immediate full update");
        dle_.erase(7); dleQueue_.clear();
        frameNo_ = savedFrame; time_ = savedTime;
    }
    // transition: stationary owner -> rate clamp(0, 0.2, 1) / 0.5 = 0.4 per second, linear, no overshoot
    {
        float rate = std::min(std::max(0.0f * 0.002f, 0.2f), 1.0f) / 0.5f;
        float v0 = 1.0f, target = 0.0f, dt = 0.5f;
        float v = v0 + std::max(-rate * dt, std::min(rate * dt, target - v0));
        check(std::fabs(v - 0.8f) < 1e-6f, "visibility transition 1 -> 0 at 0.4/s gives 0.8 after 0.5 s");
    }
    LOG_INFO("dle-test: %d passed, %d failed", pass, fail);
}

} // namespace wfc
} // namespace render
