// Clean-room reconstruction — fixed-function OpenGL renderer (graybox milestone).
// Deliberately GL 1.1 immediate mode: no extension loading needed to get pixels on screen.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include "assets/Gltf.h"
#include "assets/SkinnedModel.h"
#include "assets/Json.h"
#include <sstream>
#include <map>
#include <fstream>
#include <random>
#include <thread>
#include <array>
#include <windows.h>
#endif
#include <GL/gl.h>

#include "render/Renderer.h"
#include "render/gl/WfcPipeline.h"
#include "platform/FileCompression.h"
#include "render/gl/RenderWatchdog.h"
#include "render/FrameLimiter.h"
#include "platform/Image.h"
#include "core/Config.h"
#include "core/Log.h"

// GL 1.2 enums the GL 1.1 header may omit (drivers still support them).
#ifndef GL_LIGHT_MODEL_COLOR_CONTROL
#define GL_LIGHT_MODEL_COLOR_CONTROL 0x81F8
#endif
#ifndef GL_SEPARATE_SPECULAR_COLOR
#define GL_SEPARATE_SPECULAR_COLOR 0x81FA
#endif
// GL 1.3 texture-env combine (for applying the lightmap HDR ScaleVector up to 4x).
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_RGB_SCALE 0x8573
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_PRIMARY_COLOR 0x8577
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace render {
bool probeD3D12Interop(std::string& detail);   // D3D12InteropProbe.cpp
void initD3D12PresentIfRequested();            // d3d12/D3D12Presenter.cpp
namespace {

class GLRenderer final : public IRenderer {
public:
    ~GLRenderer() override {
        watchdog::stop();
        wfc::Pipeline::clearProgramCache();   // M54: cached programs belong to this context
    }
    bool init() override {
        watchdog::start();                    // stall diagnostics: a freeze logs its phase and writes a minidump
        if (const char* fl = std::getenv("WFC_FRAMELIMIT")) { limiter_.setLimit((float)std::atof(fl)); limitFromEnv_ = true; }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glClearColor(0.16f, 0.07f, 0.08f, 1.0f);   // warm dark to blend with the recovered fog

        // Lighting approximation. NOTE: the real WFC Streets lighting is BAKED into lightmaps
        // (StaticLightCollectionActor; lightmaps not extracted), so this is a [PROV] stand-in.
        // The previous global+light ambient of 0.35+0.35 floored every surface at ~0.70
        // brightness -> the washed-out, contrast-free look. Lower ambient + a warm key light +
        // a separate specular term restore contrast and metallic highlights.
        GLfloat gAmb[4] = {0.14f, 0.15f, 0.17f, 1.0f};   // cool global ambient
        GLfloat lAmb[4] = {0.10f, 0.10f, 0.12f, 1.0f};
        GLfloat dif[4]  = {1.05f, 1.00f, 0.92f, 1.0f};   // warm directional key
        GLfloat spec[4] = {0.55f, 0.55f, 0.55f, 1.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT, lAmb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gAmb);
        // Add specular AFTER texture modulation so highlights are not darkened by the texture.
        glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        GLfloat matSpec[4] = {0.45f, 0.45f, 0.45f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 22.0f);

        // [CONF] HeightFog recovered from MP_IAC_Streets_ART_m HeightFogComponent_11010:
        // LightColor (234,91,116) warm red-pink, Density 2e-5/UU = 0.002/m (StartDistance 2048 UU
        // not modelled by GL_EXP). This replaces the earlier guessed blue-grey fog.
        GLfloat fogC[4] = {234.0f / 255.0f, 91.0f / 255.0f, 116.0f / 255.0f, 1.0f};
        glFogi(GL_FOG_MODE, GL_EXP);
        glFogfv(GL_FOG_COLOR, fogC);
        glFogf(GL_FOG_DENSITY, 0.0014f);   // [CONF-approx] softened from 0.002/m for the play-area scale
        glHint(GL_FOG_HINT, GL_NICEST);
        glEnable(GL_FOG);
        return true;
    }

    // Diagnostics (WFC_PACINGLOG=N): presentation pacing over N-frame windows - frame interval percentiles and how the
    // camera the caller hands in changes per frame (yaw / position). A fixed-step simulation rendered without
    // interpolation shows as many frames with no change followed by a jump (steps of the tick, not of the frame).
    struct Pacing { std::chrono::steady_clock::time_point last; bool has = false; float yaw = 0; core::Vec3 pos{0, 0, 0};
                    std::vector<double> dt, dyaw, dpos; } pacing_;
    void pacingSample(const Camera& c) {
        static const long every = std::getenv("WFC_PACINGLOG") ? std::atol(std::getenv("WFC_PACINGLOG")) : 0;
        if (every <= 0) return;
        const auto now = std::chrono::steady_clock::now();
        if (pacing_.has) {
            pacing_.dt.push_back(std::chrono::duration<double, std::milli>(now - pacing_.last).count());
            pacing_.dyaw.push_back(std::fabs(c.yaw - pacing_.yaw));
            pacing_.dpos.push_back(core::length(c.pos - pacing_.pos));
        }
        pacing_.last = now; pacing_.yaw = c.yaw; pacing_.pos = c.pos; pacing_.has = true;
        if ((long)pacing_.dt.size() < every) return;
        auto pct = [](std::vector<double> v, double q) { std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(q * v.size()))]; };
        size_t still = 0, moved = 0; double sumYaw = 0, sumDt = 0;
        for (size_t i = 0; i < pacing_.dt.size(); ++i) {
            const bool chg = pacing_.dyaw[i] > 1e-6 || pacing_.dpos[i] > 1e-5;
            (chg ? moved : still)++; sumYaw += pacing_.dyaw[i]; sumDt += pacing_.dt[i];
        }
        std::vector<double> rate;   // camera yaw rate on the frames that changed (rad/s): uneven = stepped
        for (size_t i = 0; i < pacing_.dt.size(); ++i) if (pacing_.dyaw[i] > 1e-6) rate.push_back(pacing_.dyaw[i] / (pacing_.dt[i] / 1000.0));
        LOG_INFO("PACING %zu frames: interval p50 %.2f p95 %.2f p99 %.2f max %.2f ms (%.0f fps); camera unchanged on %zu, changed on "
                 "%zu; yaw rate on change p10 %.2f p50 %.2f p90 %.2f rad/s, mean over time %.2f rad/s",
                 pacing_.dt.size(), pct(pacing_.dt, 0.5), pct(pacing_.dt, 0.95), pct(pacing_.dt, 0.99), pct(pacing_.dt, 1.0),
                 1000.0 * pacing_.dt.size() / std::max(sumDt, 1e-3), still, moved,
                 rate.empty() ? 0.0 : pct(rate, 0.1), rate.empty() ? 0.0 : pct(rate, 0.5), rate.empty() ? 0.0 : pct(rate, 0.9),
                 sumYaw / std::max(sumDt / 1000.0, 1e-6));
        pacing_.dt.clear(); pacing_.dyaw.clear(); pacing_.dpos.clear();
    }
    // ---- WFC_SLOWFRAME=<ms>: one line per frame whose frame-to-frame interval exceeds <ms> (no glFinish, no stall) ----
    // interval = this renderer beginFrame to the next one (everything: game tick, 3D, HUD / 2D, swap). render = the
    // renderer's own span (beginFrame .. GPU-timed end); its CPU split at the GPU pass marks: world, characters + caller
    // draws, map FX, translucency, post. outside = interval - render (game tick, GFx HUD, swap / present, audio).
    // Counters for the frame: draws, program binds, buffer upload KB, new textures, shader compiles, map FX CPU (sim).
    // GPU: the same frame's GPU time and pass split from the non-stalling query ring (arrives ~3 frames later; the
    // line waits for it, at most 8 frames, else "gpu n/a").
    struct SlowRec {
        long idx = -1; int frame = 0; bool open = false, slow = false, done = false, gpuHave = false;
        std::chrono::steady_clock::time_point t0;
        double interval = 0, render = 0, mark[6] = {-1, -1, -1, -1, -1, -1}, gpu = 0, gpuPass[6] = {-1, -1, -1, -1, -1, -1};
        double gpuAfter3d = -1, gpuPeriodPrev = -1;
        int draws = 0, dyn = 0, fxDraws = 0, age = 0;
        unsigned long long binds0 = 0, bytes0 = 0, binds = 0, bytes = 0;
        unsigned long long firstBinds0 = 0, allocs0 = 0, allocBytes0 = 0, firstBinds = 0, allocs = 0; double allocBytes = 0;
        unsigned long tex0 = 0, sh0 = 0, tex = 0, sh = 0;
        double fx0 = 0, fxSim0 = 0, fx = 0, fxSim = 0;
    };
    SlowRec slowRing_[12];
    int slowCur_ = -1;
    double slowThr() const {
        static const double t = std::getenv("WFC_SLOWFRAME") ? std::atof(std::getenv("WFC_SLOWFRAME")) : 0.0;
        return t;
    }
    void slowFramePrint(SlowRec& s, bool gpuMissing) {
        auto seg = [&](int k) {                      // CPU ms between the previous available mark and mark k
            if (s.mark[k] < 0) return -1.0;
            for (int j = k - 1; j >= 0; --j) if (s.mark[j] >= 0) return s.mark[k] - s.mark[j];
            return -1.0;
        };
        char gpu[160];
        if (s.gpuHave) std::snprintf(gpu, sizeof gpu, "gpu %.2f (world %.2f, chars %.2f, fx %.2f, transl %.2f, post %.2f; "
                                     "after 3D %.2f, prev period %.2f)", s.gpu, s.gpuPass[1], s.gpuPass[2], s.gpuPass[3],
                                     s.gpuPass[4], s.gpuPass[5], s.gpuAfter3d, s.gpuPeriodPrev);
        else std::snprintf(gpu, sizeof gpu, "gpu n/a%s", gpuMissing ? "" : "");
        LOG_INFO("SLOWFRAME f%d interval %.2f ms: render %.2f (world %.2f, chars %.2f, fx %.2f, transl %.2f, post %.2f), "
                 "outside %.2f; %s; draws %d (dyn %d, fx %d), program binds %llu, buffer upload %.0f KB, new textures %lu, "
                 "shader compiles %lu, map FX cpu %.2f (sim %.2f), first program binds %llu, buffer allocs %llu (%.0f KB)",
                 s.frame, s.interval, s.render, seg(1), seg(2), seg(3), seg(4), seg(5), s.interval - s.render, gpu, s.draws,
                 s.dyn, s.fxDraws, s.binds, s.bytes / 1024.0, s.tex, s.sh, s.fx, s.fxSim, s.firstBinds, s.allocs,
                 s.allocBytes / 1024.0);
        s.done = true;
    }
    void slowFrameBegin() {
        if (slowThr() <= 0.0) return;
        static const bool hooked = [] { glx::installPrePresentStamp(); return true; }();
        (void)hooked;
        const auto now = std::chrono::steady_clock::now();
        if (slowCur_ >= 0) {
            SlowRec& p = slowRing_[slowCur_];
            if (p.open) {
                p.interval = std::chrono::duration<double, std::milli>(now - p.t0).count();
                p.slow = p.interval > slowThr() && wfc_.active();
                p.open = false;
                p.done = !p.slow;
            }
        }
        for (SlowRec& s : slowRing_)
            if (s.slow && !s.done && ++s.age > 8) slowFramePrint(s, true);
        slowCur_ = (slowCur_ + 1) % 12;
        SlowRec& c = slowRing_[slowCur_];
        if (c.slow && !c.done) slowFramePrint(c, true);
        c = SlowRec{};
        c.idx = glx::gpuFrameIndex();
        c.frame = wfc_.frameNumber() + 1;
        c.open = true;
        c.t0 = now;
        c.binds0 = glx::programBinds(); c.bytes0 = glx::bufferUploadBytes();
        c.firstBinds0 = glx::firstProgramBinds(); c.allocs0 = glx::bufferAllocs(); c.allocBytes0 = glx::bufferAllocBytes();
        c.tex0 = wfc::gTexCreates; c.sh0 = wfc::gShaderCompiles;
        c.fx0 = wfc_.fxMsTotal(); c.fxSim0 = wfc_.fxSimMsTotal();
    }
    void slowFrameGpu() {                            // after gpuTimerBegin: a readback may have arrived
        if (slowThr() <= 0.0) return;
        static long seen = 0;
        const long reads = glx::gpuFrameReads();
        if (reads == seen) return;
        seen = reads;
        const long idx = glx::lastGpuFrameIndex();
        for (SlowRec& s : slowRing_) {
            if (s.idx != idx || s.open) continue;
            s.gpuHave = true;
            s.gpu = glx::lastGpuFrameMs();
            for (int k = 0; k < 6; ++k) s.gpuPass[k] = glx::lastGpuPassMs(k);
            s.gpuAfter3d = glx::lastGpuAfter3dMs(); s.gpuPeriodPrev = glx::lastGpuPeriodMs();
            if (s.slow && !s.done) slowFramePrint(s, false);
        }
    }
    void slowFrameEnd() {
        if (slowThr() <= 0.0 || slowCur_ < 0) return;
        SlowRec& c = slowRing_[slowCur_];
        if (!c.open) return;
        c.render = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c.t0).count();
        for (int k = 0; k < 6; ++k) c.mark[k] = glx::cpuPassMark(k);
        const auto& fc = wfc_.lastFrameCounts();
        c.draws = fc.draws; c.dyn = fc.dynamicDraws; c.fxDraws = fc.fxDraws;
        c.binds = glx::programBinds() - c.binds0; c.bytes = glx::bufferUploadBytes() - c.bytes0;
        c.firstBinds = glx::firstProgramBinds() - c.firstBinds0; c.allocs = glx::bufferAllocs() - c.allocs0;
        c.allocBytes = (double)(glx::bufferAllocBytes() - c.allocBytes0);
        c.tex = wfc::gTexCreates - c.tex0; c.sh = wfc::gShaderCompiles - c.sh0;
        c.fx = wfc_.fxMsTotal() - c.fx0; c.fxSim = wfc_.fxSimMsTotal() - c.fxSim0;
    }

    void beginFrame(const Camera& camIn, int vpW, int vpH) override {
        watchdog::phase("beginFrame");
        glx::uniformCacheForgetCurrent();            // programs bound outside the renderer since the last frame
        glx::textureCacheInvalidate();               // ... and textures
        if (const char* hf = WFC_ENV("WFC_HUDFX")) wfc_.setHudScreenEffect(std::atoi(hf));   // diagnostics: force a HUD chain
        slowFrameBegin();                            // WFC_SLOWFRAME: closes the previous frame's record
        if (WFC_ENV("WFC_GPUFACTS") && !gpuFactsRead_) gpuFacts();   // test switch: log the GPU facts at the first frame
        initD3D12PresentIfRequested();               // optional D3D12 presentation (WFC_D3D12PRESENT; A3a)
        if (WFC_ENV("WFC_D3D12PROBE")) {               // A3a capability probe: GL <-> D3D12 sharing (once)
            static bool probed = false;
            if (!probed) { probed = true; std::string d; const bool ok = probeD3D12Interop(d); LOG_INFO("D3D12 INTEROP PROBE: %s - %s", ok ? "PASS" : "FAIL", d.c_str()); }
        }
        glx::gpuTimerBegin();                        // M43: GPU time of the 3D frame (long frames logged)
        slowFrameGpu();
        {   // a new GPU time read back this frame belongs to the frame 3 renderer frames ago
            static long seen = 0;
            const long reads = glx::gpuFrameReads();
            if (reads != seen) {
                seen = reads;
                static int logged = 0;
                static const double thr = std::getenv("WFC_GPUSPIKE_MS") ? std::atof(std::getenv("WFC_GPUSPIKE_MS")) : 50.0;
                if (glx::lastGpuFrameMs() > thr && wfc_.active() && logged++ < 40)
                    LOG_WARN("GPU frame spike %.1f ms (cpu %.1f ms) at frame %d: passes world %.1f, characters+caller %.1f, "
                             "map FX %.1f, translucency %.1f, post %.1f ms; %s", glx::lastGpuFrameMs(), glx::lastGpuFrameCpuMs(),
                             wfc_.frameNumber() - 2, glx::lastGpuPassMs(glx::kPassWorld), glx::lastGpuPassMs(glx::kPassCaller),
                             glx::lastGpuPassMs(glx::kPassMapFx), glx::lastGpuPassMs(glx::kPassTranslucent),
                             glx::lastGpuPassMs(glx::kPassPost), wfc_.frameRecordText(wfc_.frameNumber() - 2).c_str());
            }
        }
        pacingSample(camIn);
        if (const char* dt = WFC_ENV("WFC_DECALTEST")) {   // diagnostics: death scorch under x,y,z (glTF m)
            static int frames = 0;
            if (++frames == 20) {
                core::Vec3 p{0, 0, 0}; std::sscanf(dt, "%f,%f,%f", &p.x, &p.y, &p.z);
                core::Vec3 hit, n;
                if (!traceDownReceivers(p, 3.0f, hit, n)) LOG_INFO("decal test: nothing within 300 UU below");
                else if (n.y < 0.5f) LOG_INFO("decal test: glancing surface (N.down %.2f)", n.y);
                else spawnDecal("FX_Decals_p.DeathDecal_MAT", hit, {0, -1, 0}, 8.75f, 8.75f, 3.0f, 37.0f, 30.0f);
            }
        }
        if (const char* it = WFC_ENV("WFC_IMPACTTEST")) {   // diagnostics: 7 impacts across the view centre
            static int frames = 0;
            if (++frames == 20) {
                std::string w = it; const bool proj = w.find(",projectile") != std::string::npos;
                w = w.substr(0, w.find(','));
                int made = 0;
                for (int k = -3; k <= 3; ++k) {
                    const core::Vec3 dir = core::forwardFromYawPitch(camIn.yaw + k * 0.06f, camIn.pitch - 0.05f * (k & 1));
                    core::Vec3 hit, n; std::string mat;
                    if (!traceReceiverMaterial(camIn.pos, dir, 60.0f, hit, mat, &n)) continue;
                    const bool ok = spawnImpactDecal(w, hit, n, proj);
                    made += ok ? 1 : 0;
                    LOG_INFO("impact test: %s on %s -> %s", w.c_str(), mat.c_str(), ok ? "decal" : "none");
                }
                LOG_INFO("impact test: %d decals", made);
            }
        }
        if (visualCheckOn()) glEntry_ = captureGlState();   // what the previous user of the context left bound
        // The 3D frame owns its GL state. The draws set blend / depth writes / culling per material, but depth TEST,
        // depth func, scissor, stencil, colour mask and polygon mode were only set once in init(). M11 root cause:
        // the frontend's GFx host leaves GL_DEPTH_TEST disabled, so after the menus every map was drawn without depth
        // testing (human playtest: architecture "missing", effects floating) while a direct boot rendered correctly.
        static const bool inheritState = std::getenv("WFC_M11_INHERITSTATE") != nullptr;   // regression reproduction only
        if (!inheritState) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
            glDisable(GL_BLEND);
        } else {
            // M25: the Frontend now restores its own state too (a96f841), so merely inheriting no longer reproduces
            // anything. Inject the leak the GFx pass used to leave, so the harness keeps proving it FAILs this state.
            glDisable(GL_DEPTH_TEST);
        }
        const Camera& cam0 = camIn;
        glViewport(0, 0, vpW, vpH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Diagnostic camera override for render inspection: WFC_RENDERCAM="x,y,z,yawRad,pitchRad".
        Camera camOv = cam0;
        if (const char* rc = WFC_ENV("WFC_RENDERCAM"))
            std::sscanf(rc, "%f,%f,%f,%f,%f", &camOv.pos.x, &camOv.pos.y, &camOv.pos.z, &camOv.yaw, &camOv.pitch);
        // Measurements: WFC_FIXEDCAM="x,y,z,yawDeg,pitchDeg" holds every MATCH frame at one view (menu scenes keep their
        // own cameras), so performance runs see the same scene regardless of where the local player goes
        if (matchMap_) {
            static const char* fc = std::getenv("WFC_FIXEDCAM");
            float y = 0, pt = 0;
            if (fc && std::sscanf(fc, "%f,%f,%f,%f,%f", &camOv.pos.x, &camOv.pos.y, &camOv.pos.z, &y, &pt) == 5) {
                camOv.yaw = core::radians(y); camOv.pitch = core::radians(pt);
            }
        }
        // M51: the far plane covers the loaded world's geometry (UE3 renders with an infinite far plane). Debris' sky
        // dome (SpaceDome x6, radius ~26.6 km) lay beyond the 20 km default and was clipped mid-screen (black sky with
        // pieces at the view edges). Depth precision is governed by the near plane; one far plane for every 3D pass.
        if (wfc_.active() && wfc_.worldRadius() > 0.0f) {
            float need = wfc_.worldRadius() + core::length(camOv.pos) + 1000.0f;
            if (need > camOv.zfar) camOv.zfar = need;
        }
        const Camera& cam = camOv;
        lastCam_ = cam;
        if (wfc_.active()) wfc_.beginFrame(cam, vpW, vpH);
        vpW_ = vpW; vpH_ = vpH;
        inFrame_ = true;
        glMatrixMode(GL_PROJECTION);
        core::Mat4 p = cam.proj();
        glLoadMatrixf(p.m);

        glMatrixMode(GL_MODELVIEW);
        view_ = cam.view();
        glLoadMatrixf(view_.m);

        // Directional light fixed in world space (set while modelview == view).
        GLfloat lightDir[4] = {0.4f, 1.0f, 0.6f, 0.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
    }

    void endFrame() override {
        struct ForgetOnExit { ~ForgetOnExit() { glx::uniformCacheForgetCurrent(); glx::textureCacheInvalidate(); } } forgetOnExit;   // GFx / frontend draw next
        if (glx::GetGraphicsResetStatus) glx::pollResetStatus();   // M43: a lost context is logged (once)
        glx::gpuMark(glx::kPassWorld);       // no dynamic draw this frame: the world ends here
        glx::gpuMark(glx::kPassCaller);
        watchdog::phase("endFrame: map presentation");
        if (wfc_.active()) wfc_.drawMapPresentation();
        watchdog::phase("endFrame: post / composite");
        if (wfc_.active()) wfc_.endFrame();
        glx::gpuMark(glx::kPassPost);
        slowFrameEnd();
        glx::gpuTimerEnd();
        if (!slotWaited_) { watchdog::phase("frame limiter"); limiter_.wait(); }   // the loop did not call waitFrameSlot
        slotWaited_ = false;
        { static int frames = 0; watchdog::frameDone(++frames);
          if (frames == 60 && WFC_ENV("WFC_HANGTEST")) {   // diagnostics: a 7 s stall to exercise the watchdog
              watchdog::phase("WFC_HANGTEST stall");
              std::this_thread::sleep_for(std::chrono::seconds(7));
          } }
        watchdog::phase("after endFrame (buffer swap / game update)");
        if (WFC_ENV("WFC_FRAMELOG") && wfc_.active()) {   // M50 diagnostics: per-frame GPU time + draw counts
            RenderDiagnostics d = renderDiagnostics();
            LOG_INFO("FRAME %d gpu=%.2fms (cpu %.2fms) draws=%d world=%d bsp=%d dyn=%d fx=%d opaque=%d transl=%d culled=%d cam=%.1f,%.1f,%.1f yaw=%.2f pitch=%.2f",
                     d.frame, glx::lastGpuFrameMs(), glx::lastGpuFrameCpuMs(), d.draws, d.worldDraws, d.bspDraws, d.dynamicDraws, d.fxDraws, d.opaqueDraws,
                     d.translucentDraws, d.culledSubs, d.camPos[0], d.camPos[1], d.camPos[2], d.camYaw, d.camPitch);
        }
        if (visualCheckOn()) {                       // GL errors raised by this frame's 3D work (first ones logged)
            int n = 0;
            for (GLenum e = glGetError(); e != GL_NO_ERROR && n < 64; e = glGetError(), ++n)
                if (glErrorLogs_ < 20) { ++glErrorLogs_; LOG_ERROR("GL error 0x%04X in 3D frame %d", (unsigned)e, wfc_.frameNumber()); }
            glErrors_ = n;
        }
        sampleScene();
        drawLegacyMarker();
        drawReticle();
        for (const ScreenBatch& b : screenQueue_) drawScreenNow(b);   // 2D composition on top, in order
        screenQueue_.clear();
        inFrame_ = false;
        glFlush();
    }

    void drawScreenTriangles(const ScreenBatch& b) override {
        watchdog::phase("drawScreenTriangles");
        if (b.verts.empty()) return;
        if (inFrame_) screenQueue_.push_back(b); else drawScreenNow(b);
    }

    // ---- Canvas fonts (UE3 UFont from render data) ----
    struct CanvasFont {
        bool ok = false;
        std::vector<std::array<int, 6>> chars;        // StartU, StartV, USize, VSize, TextureIndex, VerticalOffset
        std::map<uint32_t, int> remap;
        std::vector<TextureHandle> pages; std::vector<std::pair<int, int>> pageSize;
        float lineHeight = 0;
    };
    std::map<std::string, CanvasFont> fonts_;
    std::map<int, ScreenBatch> canvasTextPages_;           // drawCanvasText scratch (see there)
    CanvasFont& font(const std::string& name) {
        auto it = fonts_.find(name);
        if (it != fonts_.end()) return it->second;
        CanvasFont& f = fonts_[name];
        std::string dir = wfc::Pipeline::renderDataRoot() + "/_ui/fonts/";
        std::ifstream in(dir + name + ".json", std::ios::binary);
        if (!in) return f;
        std::stringstream ss; ss << in.rdbuf();
        assets::Json J;
        if (!assets::Json::parse(ss.str(), J)) return f;
        for (size_t i = 0; i < J["characters"].size(); ++i) {
            const assets::Json& c = J["characters"][i];
            std::array<int, 6> a{};
            for (int k = 0; k < 6; ++k) a[(size_t)k] = (int)c[(size_t)k].asFloat();
            f.chars.push_back(a);
            f.lineHeight = std::max(f.lineHeight, (float)a[3]);
        }
        for (const auto& kv : J["remap"].obj) f.remap[(uint32_t)std::stoul(kv.first)] = (int)kv.second.asFloat();
        for (size_t i = 0; i < J["pages"].size(); ++i) {
            ImageData img;
            if (!platform::decodeImage(dir + J["pages"][i].asString(), img)) continue;
            f.pageSize.push_back({img.w, img.h});
            f.pages.push_back(uploadTexture(img));
            setTexturePersistent(f.pages.back());   // fonts are cached for the session (first use may be in a match)
        }
        f.ok = !f.chars.empty() && !f.pages.empty();
        return f;
    }
    // decoded into a reused buffer (HUD marker labels: a vector per label per frame); callers only iterate it
    static const std::vector<uint32_t>& utf8Decode(const std::string& s) {
        static thread_local std::vector<uint32_t> out;
        out.clear();
        for (size_t i = 0; i < s.size();) {
            unsigned char c = (unsigned char)s[i];
            uint32_t cp = c; int n = 0;
            if (c >= 0xF0) { cp = c & 0x07; n = 3; } else if (c >= 0xE0) { cp = c & 0x0F; n = 2; } else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
            ++i;
            for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            out.push_back(cp);
        }
        return out;
    }
    const std::array<int, 6>* glyph(const CanvasFont& f, uint32_t cp) const {
        auto it = f.remap.find(cp);
        int idx = it != f.remap.end() ? it->second : (f.remap.count('?') ? f.remap.at('?') : -1);
        return idx >= 0 && (size_t)idx < f.chars.size() ? &f.chars[(size_t)idx] : nullptr;
    }
    bool canvasTextSize(const std::string& name, const std::string& utf8, float& w, float& h, float scale) override {
        CanvasFont& f = font(name);
        w = h = 0;
        if (!f.ok) return false;
        for (uint32_t cp : utf8Decode(utf8)) if (const auto* g = glyph(f, cp)) w += (*g)[2] * scale;
        h = f.lineHeight * scale;
        return true;
    }
    bool drawCanvasText(const std::string& name, const std::string& utf8, float x, float y, const uint8_t rgba[4],
                        float scale) override {
        CanvasFont& f = font(name);
        if (!f.ok) return false;
        // per-page batches kept across calls (HUD markers draw text every frame): vertex lists cleared, capacity kept;
        // pages this call leaves empty are skipped below, so the draws and their order match a fresh map
        std::map<int, ScreenBatch>& perPage = canvasTextPages_;
        for (auto& kv : perPage) kv.second.verts.clear();
        float cx = x;
        for (uint32_t cp : utf8Decode(utf8)) {
            const auto* g = glyph(f, cp);
            if (!g) continue;
            const auto& c = *g;
            int page = c[4] < (int)f.pages.size() ? c[4] : 0;
            float tw = (float)f.pageSize[(size_t)page].first, th = (float)f.pageSize[(size_t)page].second;
            float w = c[2] * scale, h = c[3] * scale, top = y + c[5] * scale;
            if (w > 0 && h > 0) {
                float u0 = c[0] / tw, v0 = c[1] / th, u1 = (c[0] + c[2]) / tw, v1 = (c[1] + c[3]) / th;
                ScreenBatch& b = perPage[page];
                b.texture = f.pages[(size_t)page]; b.blend = ScreenBlend::Alpha; b.clampUV = true;
                ScreenVertex q[4] = {{cx, top, u0, v0, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx + w, top, u1, v0, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx + w, top + h, u1, v1, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx, top + h, u0, v1, rgba[0], rgba[1], rgba[2], rgba[3]}};
                for (int k : {0, 1, 2, 0, 2, 3}) b.verts.push_back(q[k]);
            }
            cx += c[2] * scale;
        }
        for (auto& kv : perPage) if (!kv.second.verts.empty()) drawScreenTriangles(kv.second);
        return true;
    }

    bool pickWorld(const core::Vec3& o, const core::Vec3& d, float maxDist, PickHit& out) override {
        float best = maxDist;
        bool hit = false;
        for (const MeshData& m : meshes_) {
            if (m.subs.empty() || m.subs[0].component.empty()) continue;      // authored world meshes only
            for (const SubMesh& sm : m.subs) {
                for (uint32_t k = sm.indexOffset; k + 2 < sm.indexOffset + sm.indexCount; k += 3) {
                    const float* a = &m.positions[(size_t)m.indices[k] * 3];
                    const float* b = &m.positions[(size_t)m.indices[k + 1] * 3];
                    const float* c = &m.positions[(size_t)m.indices[k + 2] * 3];
                    core::Vec3 A{a[0], a[1], a[2]}, B{b[0], b[1], b[2]}, C{c[0], c[1], c[2]};
                    core::Vec3 e1 = B - A, e2 = C - A, pv = core::cross(d, e2);
                    float det = core::dot(e1, pv);
                    if (std::fabs(det) < 1e-9f) continue;
                    float inv = 1.0f / det;
                    core::Vec3 tv = o - A;
                    float u = core::dot(tv, pv) * inv;
                    if (u < 0 || u > 1) continue;
                    core::Vec3 qv = core::cross(tv, e1);
                    float v = core::dot(d, qv) * inv;
                    if (v < 0 || u + v > 1) continue;
                    float t = core::dot(e2, qv) * inv;
                    if (t <= 1e-3f || t >= best) continue;
                    best = t; hit = true;
                    out.component = sm.component; out.mesh = sm.sourceMesh;
                    out.material = sm.material >= 0 && (size_t)sm.material < m.mats.size() ? m.mats[(size_t)sm.material].sourceName : "";
                    out.distance = t; out.point = o + d * t; out.normal = core::normalize(core::cross(e1, e2));
                }
            }
        }
        return hit;
    }

    bool updateTexture(TextureHandle h, const ImageData& img) override {
        watchdog::phase("updateTexture (movie / UI frame)");
        if (h < 0 || (size_t)h >= textures_.size() || !textures_[(size_t)h] || !img.valid()) return false;
        glBindTexture(GL_TEXTURE_2D, textures_[(size_t)h]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }
    int viewportWidth() const override { return vpW_; }
    int viewportHeight() const override { return vpH_; }

    void drawScreenNow(const ScreenBatch& b) {
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        const int W = vp[2] > 0 ? vp[2] : vpW_, H = vp[3] > 0 ? vp[3] : vpH_;
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING); glDisable(GL_FOG);
        if (b.scissor) { glEnable(GL_SCISSOR_TEST); glScissor(b.sx, H - (b.sy + b.sh), b.sw, b.sh); }
        switch (b.blend) {
            case ScreenBlend::Opaque: glDisable(GL_BLEND); break;
            case ScreenBlend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case ScreenBlend::Premultiplied: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case ScreenBlend::Additive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            case ScreenBlend::Multiply: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
        }
        const bool tex = b.texture >= 0 && (size_t)b.texture < textures_.size();
        if (tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures_[(size_t)b.texture]);
            const GLint wrap = b.clampUV ? GL_CLAMP_TO_EDGE : GL_REPEAT, filt = b.linearFilter ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_TRIANGLES);
        for (const ScreenVertex& v : b.verts) {
            glColor4ub(v.r, v.g, v.b, v.a);
            glTexCoord2f(v.u, v.v);
            glVertex2f(v.x, v.y);
        }
        glEnd();
        glColor4ub(255, 255, 255, 255);
        if (tex) { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); }
        if (b.scissor) glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    void setReticle(const ReticleState& s) override { reticle_ = s; }
    bool evaluatesFxMaterials() const override { return wfc_.active(); }

    // mc_crosshairIonBlaster (Hud_GFX.gfx char 404), at crosshairAnchor_mc = stage (560, 360) of the
    // 1120x720 movie: three prong_mc clips (char 402 -> shape 401) under rotations 0/120/240 deg, each
    // the 32x16 bitmap 400 (Hud_GFX_I190.png) stretched by its fill matrix to (-15..15, -6..6) px.
    // Its DoAction: SpreadMultiplier 300; on WeaponSpread change every prong_mc._y eases to
    // -300 * WeaponSpread over 0.2 s ("easeout"). Tint: NotifyTargetTypeChanged 0 -> 0x50B5D5,
    // 1 -> 0xFF3333, else white, eased over 0.2 s. [PROV] stage scale mode (ShowAll assumed) and the
    // HmActionScript easeout curve (quadratic ease-out assumed).
    void drawReticle() {
        if (!reticle_.visible || vpW_ <= 0 || vpH_ <= 0) return;
        if (reticleTex_ == 0 && !reticleTried_) {
            reticleTried_ = true;
            ImageData img;
            std::string path = std::string(core::config::kAssetRootDefault) + "/../content/UI_GFxHud_p/Hud_GFX_I190.png";
            if (platform::decodeImage(path, img)) {
                glGenTextures(1, &reticleTex_);
                glBindTexture(GL_TEXTURE_2D, reticleTex_);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            } else {
                LOG_WARN("reticle: %s not found; crosshair not drawn", path.c_str());
            }
        }
        if (reticleTex_ == 0) return;
        // tweens (prong offset, tint) driven by wall time like the Flash timeline
        static auto t0 = std::chrono::steady_clock::now();
        static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
        static int frames = 0;
        float now = lockstep ? (float)(++frames) / 60.0f : std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        auto tween = [&](Tween& tw, float target, float dur) {
            if (target != tw.to) { tw.from = tw.value(now); tw.to = target; tw.start = now; tw.dur = dur; }
            return tw.value(now);
        };
        // the clip's first WeaponSpread is applied instantly, later changes ease (fineaim_hud.json, AssetTools 7a69756)
        if (!prongSet_) { prongTween_.to = 300.0f * reticle_.weaponSpread; prongTween_.start = -1.0f; prongSet_ = true; }
        float off = tween(prongTween_, 300.0f * reticle_.weaponSpread, 0.2f);
        uint32_t rgb = reticle_.targetType == 0 ? 0x50B5D5u : reticle_.targetType == 1 ? 0xFF3333u : 0xFFFFFFu;
        float tint[3];
        for (int c = 0; c < 3; ++c) tint[c] = tween(tintTween_[c], (float)((rgb >> (16 - 8 * c)) & 0xFF) / 255.0f, 0.2f);

        float scale = std::min((float)vpW_ / 1120.0f, (float)vpH_ / 720.0f);
        // midCenter_mc (560, 360) -> crosshair controller sprite 621 at (0.2, 0.2) -> crosshairAnchor_mc (0, 0)
        float cx = vpW_ * 0.5f + 0.2f * scale, cy = vpH_ * 0.5f + 0.2f * scale;
        glViewport(0, 0, vpW_, vpH_);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, vpW_, vpH_, 0, -1, 1);                       // Flash stage axes: y down
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, reticleTex_);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(tint[0], tint[1], tint[2], 1.0f);
        glBegin(GL_QUADS);
        for (int k = 0; k < 3; ++k) {
            float a = (float)k * 2.0943951f, ca = std::cos(a), sa = std::sin(a);
            const float px[4] = {-15, 15, 15, -15}, py[4] = {-6, -6, 6, 6}, u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
            for (int i = 0; i < 4; ++i) {
                float x = px[i], y = py[i] - off;                  // prong_mc._y = -300 * spread
                glTexCoord2f(u[i], v[i]);
                glVertex2f(cx + scale * (ca * x - sa * y), cy + scale * (sa * x + ca * y));
            }
        }
        glEnd();
        glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    // Handles uploaded before the unload stay in range but become empty (no GPU mesh, CPU copy dropped): stale
    // handles draw nothing; owners re-upload for the next level.
    bool drawMaterialTile(const MaterialTile& t) override {
        watchdog::phase("drawMaterialTile");
        if (!wfc_.active() || !wfc_.hasMaterial(t.material)) return false;
        wfc_.drawMaterialTile(t);
        return true;
    }
    bool hasMaterial(const std::string& m) const override { return wfc_.active() && wfc_.hasMaterial(m); }

    // ---- frontend scenes ----
    MeshHandle sceneMesh_ = kInvalidMesh;
    std::string sceneDir_;
    bool loadFrontendScene(const std::vector<std::string>& levels) override {
        unloadFrontendScene();
        const std::string data = wfc::Pipeline::renderDataRoot(), assets = wfc::Pipeline::assetRoot();
        // One render-data map at a time. The persistent level's family (levels.front()) is preferred: its render data
        // composes its streamed sublevels (map.json) and holds what the persistent level cooks - the lobbies cook every
        // MP chassis material (the preview pawn's) in UI_PartyLobby_m / UI_Lobby_m, not in the streamed
        // UI_CharacterCustomization_m. Otherwise the requested level with the most placed scenery.
        std::string dir;
        size_t bestSize = 0;
        for (size_t li = 0; li < levels.size(); ++li) {
            const std::string& l = levels[li];
            std::string d = l.size() > 2 && l.compare(l.size() - 2, 2, "_m") == 0 ? l.substr(0, l.size() - 2) : l;
            std::ifstream probe(data + "/" + d + "/materials_glsl.json");
            const long long glbSize = platform::dataFileSize(assets + "/Maps/" + d + "/world.glb");   // or its .xpr twin
            if (!probe || glbSize < 0) continue;
            if (li == 0) { dir = d; break; }
            size_t sz = (size_t)glbSize;
            if (dir.empty() || sz > bestSize) { dir = d; bestSize = sz; }
        }
        if (dir.empty()) {
            renderDataRequested_ = true;     // the caller's fallback (fixed-function world.glb) is not the original scene
            wfc::Pipeline::lastLoadError() = "no frontend-scene render data in " + data + " for " + (levels.empty() ? std::string("-") : levels.front());
            LOG_ERROR("frontend scene: NO RENDER DATA (legacy presentation) for any of %zu levels", levels.size());
            return false;
        }
        MeshData world;
        auto tw = std::chrono::steady_clock::now();
        bool okWorld = assets::loadGlb(assets + "/Maps/" + dir + "/world.glb", world);
        LOG_INFO("frontend scene %s: world.glb read %.0f ms", dir.c_str(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tw).count());
        wfc_.yieldLoad();
        if (!okWorld) {
            LOG_WARN("frontend scene %s: world.glb missing", dir.c_str());
            return false;
        }
        loadingFrontendScene_ = true;
        bool okData = loadMapRenderData(dir);
        loadingFrontendScene_ = false;
        if (!okData) return false;
        wfc_.skipMaterialPrewarm();          // M54: a menu backdrop never draws the match's weapon / effect materials
        uploadingFrontendWorld_ = true;      // M75: no warm-up draw (Matinee-posed backdrop; matches only)
        sceneMesh_ = uploadMesh(world);
        uploadingFrontendWorld_ = false;
        wfc_.prewarmPlacedFx();              // M58: its emitters' programs / textures / meshes, under the loading screen
        sceneDir_ = dir;
        LOG_INFO("frontend scene %s loaded (%zu submeshes)", dir.c_str(), world.subs.size());
        return sceneMesh_ != kInvalidMesh;
    }
    void drawFrontendScene(const core::Vec3& p, const core::Vec3& r, float fovDeg, int w, int h, double t) override {
        if (sceneMesh_ == kInvalidMesh) return;
        Camera cam;
        cam.pos = core::Vec3{p.x * 0.01f, p.z * 0.01f, p.y * 0.01f};       // UE (x, y, z) UU -> glTF (x, z, y) m
        const float pr = r.x * 0.0174533f, yr = r.y * 0.0174533f;
        core::Vec3 fUE{std::cos(pr) * std::cos(yr), std::cos(pr) * std::sin(yr), std::sin(pr)};
        core::Vec3 f{fUE.x, fUE.z, fUE.y};
        cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        cam.yaw = std::atan2(-f.x, -f.z);
        cam.fovXDeg = fovDeg;                                                 // UE FOVAngle is horizontal
        cam.aspect = (float)w / (float)std::max(h, 1);
        setMapClock((float)t);
        beginFrame(cam, w, h);
        drawMesh(sceneMesh_, core::Mat4::identity(), core::Vec3{1, 1, 1});
        if (sceneDraw_) { sceneDraw_(*this); setDrawOwner(0); }
        endFrame();
    }
    void setFrontendSceneDraw(std::function<void(IRenderer&)> f) override { sceneDraw_ = std::move(f); }
    std::map<std::string, assets::AnimFile> animFileCache_;   // M69 parsed AnimSets by path (preview bodies)
    bool loadingFrontendScene_ = false;
    bool matchMap_ = false;                  // a match map's render data is loaded (not a menu scene)
    bool uploadingFrontendWorld_ = false;
    const assets::AnimFile* animFile(const std::string& path) {   // M69 parsed AnimSet, cached by path
        auto cached = animFileCache_.find(path);
        if (cached == animFileCache_.end()) {
            assets::AnimFile f;
            if (!assets::loadAnimationFile(path, f)) return nullptr;
            cached = animFileCache_.emplace(path, std::move(f)).first;
        }
        return &cached->second;
    }
    std::string animSetPath(const std::string& s) const {          // "Package.Set" -> content/Package/Set.anim.gltf
        size_t dot = s.find('.');
        return dot == std::string::npos ? std::string() : wfc::Pipeline::contentRoot() + s.substr(0, dot) + "/" + s.substr(dot + 1) + ".anim.gltf";
    }
    void preparePreviewBody(const std::string& gl, const std::vector<std::string>& sets) override {
        const auto t0 = std::chrono::steady_clock::now();
        for (const std::string& s : sets) { std::string p = animSetPath(s); if (!p.empty()) animFile(p); wfc_.yieldLoad(); }
        if (wfc_.active()) {
            std::string rel = gl.rfind("content/", 0) == 0 ? gl.substr(8) : gl;
            assets::SkinnedModel model;
            if (assets::loadSkinnedGlb(wfc::Pipeline::contentRoot() + rel, model)) {
                MeshData md; md.subs = model.subs; md.mats = model.mats;
                wfc_.prewarmDynamic(md);
            }
        }
        LOG_INFO("preview body prepared %s: %.1f ms", gl.c_str(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    int loadPreviewBody(const std::string& gl, const std::vector<std::string>& sets, const std::string& anim) override {
        auto b = std::make_unique<PreviewBody>();
        const std::string content = wfc::Pipeline::contentRoot();
        auto rel = [](const std::string& p) { return p.rfind("content/", 0) == 0 ? p.substr(8) : p; };
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b2) { return std::chrono::duration<double, std::milli>(b2 - a).count(); };
        const auto tStart = Clock::now();
        if (!assets::loadSkinnedGlb(content + rel(gl), b->model)) return -1;
        const auto tMesh = Clock::now();
        for (auto it = sets.rbegin(); it != sets.rend(); ++it) {   // last set first: clipByName returns its clip
            const std::string& s = *it;
            size_t dot = s.find('.');
            if (dot == std::string::npos) continue;
            // M69: AnimSets are parsed once per session and shared by every preview body (the shared robot sets
            // carry ~300 clips each; re-parsing them per body was the CaC class-pick freeze)
            if (const assets::AnimFile* f = animFile(animSetPath(s))) assets::appendAnimations(*f, b->model);
        }
        // UAnimNodeSequence::SetAnim (WFC xex Function_82E3FF48, RE): the requested name is first remapped through the
        // AnimSets' ChooserGroups, last set first; the first set with that group supplies the anim (weighted pick).
        // render data _ui/anim_choosers.json (tools/render/build_anim_choosers.py). Cust_Idle -> NAV_Idle on e.g.
        // Sideswipe / Barricade.
        std::string resolved = anim;
        {
            static assets::Json choosers;
            static bool loaded = false;
            if (!loaded) {
                loaded = true;
                std::ifstream f(wfc::Pipeline::renderDataRoot() + "/_ui/anim_choosers.json");
                std::stringstream ss; ss << f.rdbuf();
                if (!f || !assets::Json::parse(ss.str(), choosers))
                    LOG_WARN("preview body: no _ui/anim_choosers.json (rebuild render data); sequences used as named");
            }
            const assets::Json& S = choosers["sets"];
            for (auto it = sets.rbegin(); it != sets.rend() && S.isObject(); ++it) {
                const assets::Json& G = S[*it][anim];
                if (!G.isArray() || G.size() == 0) continue;
                int total = 0;
                for (size_t k = 0; k < G.size(); ++k) total += std::max(G[k][(size_t)1].asInt(1), 0);
                int pick = total > 0 ? std::rand() % total : 0;   // weighted random pick (HIGH, RE)
                for (size_t k = 0; k < G.size(); ++k) {
                    pick -= std::max(G[k][(size_t)1].asInt(1), 0);
                    if (pick < 0 || k + 1 == G.size()) { resolved = G[k][(size_t)0].asString(); break; }
                }
                break;
            }
        }
        b->clip = b->model.clipByName(resolved);
        // PC ADAPTATION: bodies never selectable in the original (Frenzy / Rumble: no Cust_Idle chooser group, no
        // Cust_Idle clip) preview in their own idle (NAV_Idle, AssetTools) instead of the reference pose
        if (b->clip < 0 && resolved == "Cust_Idle" && b->model.clipByName("NAV_Idle") >= 0) {
            LOG_INFO("preview body %s: no Cust_Idle; NAV_Idle (PC ADAPTATION: never selectable in the original)", gl.c_str());
            resolved = "NAV_Idle";
            b->clip = b->model.clipByName(resolved);
        }
        const auto tAnim = Clock::now();
        if (wfc_.active()) {                 // M54: its materials compile now (load), not on its first drawn frame
            MeshData md; md.subs = b->model.subs; md.mats = b->model.mats;
            wfc_.prewarmDynamic(md);
        }
        LOG_INFO("preview body %s: %.1f ms (mesh %.1f, anim sets %.1f, materials %.1f)", gl.c_str(), ms(tStart, Clock::now()),
                 ms(tStart, tMesh), ms(tMesh, tAnim), ms(tAnim, Clock::now()));
        if (resolved != anim) LOG_INFO("preview body %s: %s -> %s (AnimSet chooser)", gl.c_str(), anim.c_str(), resolved.c_str());
        if (b->clip < 0) LOG_WARN("preview body %s: sequence %s not in its AnimSets; reference pose", gl.c_str(), resolved.c_str());
        else LOG_INFO("preview body %s: %s (%.2f s)", gl.c_str(), resolved.c_str(), b->model.clips[(size_t)b->clip].duration);
        for (size_t i = 0; i < previewBodies_.size(); ++i)
            if (!previewBodies_[i]) { previewBodies_[i] = std::move(b); return (int)i; }
        previewBodies_.push_back(std::move(b));
        return (int)previewBodies_.size() - 1;
    }
    void releasePreviewBody(int h) override {
        if (h >= 0 && (size_t)h < previewBodies_.size()) previewBodies_[(size_t)h].reset();
    }
    int previewBodyCount() const override {
        int n = 0;
        for (const auto& b : previewBodies_) n += b ? 1 : 0;
        return n;
    }
    bool posePreviewBody(int h, float t, MeshData& out) override {
        if (h < 0 || (size_t)h >= previewBodies_.size() || !previewBodies_[(size_t)h]) return false;
        PreviewBody& b = *previewBodies_[(size_t)h];
        assets::evaluatePose(b.model, b.clip, t, b.scratch, out, true);
        // PC ADAPTATION (preview only): a body taller than the customization cameras were framed for (the tallest
        // originally selectable chassis, 4.1 m; the Machine Gunners Car8-10 are 5.3 m and were never selectable) is
        // scaled uniformly about its origin to that height, so the class camera frames it. Matches are unaffected.
        const float height = b.model.boundsMax.y - b.model.boundsMin.y;
        constexpr float kDesignHeight = 4.1f;
        if (height > kDesignHeight + 0.05f) {
            const float k = kDesignHeight / height;
            for (float& v : out.positions) v *= k;
        }
        return !out.empty();
    }
    bool loadContentMesh(const std::string& gl, MeshData& out) override {
        std::string rel = gl.rfind("content/", 0) == 0 ? gl.substr(8) : gl;
        return assets::loadGlb(wfc::Pipeline::contentRoot() + rel, out) && !out.empty();
    }
    void unloadFrontendScene() override {
        if (sceneMesh_ == kInvalidMesh && sceneDir_.empty()) return;
        unloadMapRenderData();
        sceneMesh_ = kInvalidMesh;
        sceneDir_.clear();
    }

    void setLoadYield(std::function<void()> y) override { wfc_.setLoadYield(std::move(y)); }

    void unloadMapRenderData() override {
        matchMap_ = false;
        renderDataRequested_ = false;
        sceneSampled_ = false;
        wfc_.release();
        recv_ = DecalReceivers{};
        impact_ = assets::Json{}; impactLoaded_ = false;
        for (size_t i = 0; i < meshes_.size(); ++i) { meshes_[i] = MeshData{}; gpu_[i] = -1; }
        // M28: textures uploaded since the previous unload are match-owned (Frontend persistent-renderer soak: +60..105
        // live textures per match, never released, when the renderer outlives the match)
        int freed = 0;
        static const bool keepTex = std::getenv("WFC_M28_KEEPTEX") != nullptr;   // A/B: the pre-M28 behaviour
        for (size_t i = texEpoch_; i < textures_.size() && !keepTex; ++i)
            if (textures_[i] && !persistentTex_[i]) { glDeleteTextures(1, &textures_[i]); textures_[i] = 0; ++freed; }
        texEpoch_ = textures_.size();
        LOG_INFO("renderer: unloadMapRenderData released %d match textures (%d live)", freed, liveTextureCount());
        glx::textureTraceDump("after unloadMapRenderData");   // WFC_GLTRACE / WFC_TEXTRACE (leak hunting)
    }

    // ---- validation (M10) ------------------------------------------------------------------------------------
    // Scene image metrics before 2D composition (WFC_VISUALCHECK): near-black fraction, flat-tile fraction, luminance
    // percentiles, quantized colour count. One readback every 120 frames and before each screenshot.
    struct ImageMetrics { float black = 0, flat = 0, p50 = 0, p95 = 0; int colors = 0; };
    static ImageMetrics imageMetrics(const std::vector<uint8_t>& rgb, int w, int h) {
        ImageMetrics m;
        if (w <= 0 || h <= 0) return m;
        std::vector<int> hist(256, 0);
        std::vector<char> seen(1 << 15, 0);
        size_t n = (size_t)w * h, black = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = &rgb[i * 3];
            int l = (p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8;
            ++hist[(size_t)l];
            if (l < 10) ++black;
            int q = ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3);
            if (!seen[(size_t)q]) { seen[(size_t)q] = 1; ++m.colors; }
        }
        m.black = (float)black / (float)n;
        size_t acc = 0; bool h50 = false;
        for (int l = 0; l < 256; ++l) {
            acc += (size_t)hist[(size_t)l];
            if (!h50 && acc >= n / 2) { m.p50 = (float)l; h50 = true; }
            if (acc >= n * 95 / 100) { m.p95 = (float)l; break; }
        }
        const int T = 16;
        int tiles = 0, flat = 0;
        for (int ty = 0; ty + T <= h; ty += T)
            for (int tx = 0; tx + T <= w; tx += T) {
                int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
                for (int y = ty; y < ty + T; ++y)
                    for (int x = tx; x < tx + T; ++x)
                        for (int c = 0; c < 3; ++c) {
                            int v = rgb[((size_t)y * w + x) * 3 + c];
                            lo[c] = std::min(lo[c], v); hi[c] = std::max(hi[c], v);
                        }
                ++tiles;
                if (hi[0] - lo[0] <= 3 && hi[1] - lo[1] <= 3 && hi[2] - lo[2] <= 3) ++flat;
            }
        m.flat = tiles ? (float)flat / (float)tiles : 0.0f;
        return m;
    }
    static bool visualCheckOn() { static const bool on = std::getenv("WFC_VISUALCHECK") != nullptr; return on; }

    void sampleScene() {
        if (!visualCheckOn() || !inFrame_) return;
        ++sceneFrames_;
        if (!sceneSamplePending_ && sceneFrames_ % 120 != 0) return;
        sceneSamplePending_ = false;
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] <= 0 || vp[3] <= 0) return;
        std::vector<uint8_t> rgb((size_t)vp[2] * vp[3] * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(vp[0], vp[1], vp[2], vp[3], GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        ImageMetrics m = imageMetrics(rgb, vp[2], vp[3]);
        scene_ = m; sceneSampled_ = true;
        RenderDiagnostics d = renderDiagnostics();
        std::string why = verdict(d, m);
        LOG_INFO("VISUALCHECK frame %d path=%s black=%.3f flat=%.3f lumaP50=%.0f colors=%d draws=%d world=%d bsp=%d "
                 "materials=%d noProgram=%d noDepth=%d glErr=%d gpu=%.1fms glDebug=%u -> %s", d.frame, d.originalPath ? "original" : "legacy", m.black, m.flat,
                 m.p50, m.colors, d.draws, d.worldDraws, d.bspDraws, d.distinctMaterials, d.noProgramSubs, d.opaqueNoDepthTest, d.glErrors,
                 glx::lastGpuFrameMs(), glx::debugCounts().errors + glx::debugCounts().undefined + glx::debugCounts().high,
                 why.empty() ? "PASS" : ("FAIL: " + why).c_str());
    }

    // Rejects obviously broken 3D scenes (the M06 playtest recording): legacy fallback, mostly black, mostly flat,
    // almost no colours, nothing of the map drawn. Thresholds are deliberately loose: a FAIL is a broken scene.
    static std::string verdict(const RenderDiagnostics& d, const ImageMetrics& m) {
        std::string why;
        auto add = [&](const std::string& s) { why += (why.empty() ? "" : "; ") + s; };
        if (d.renderDataRequested && !d.originalPath && !d.legacyRequested)
            add("legacy fallback (" + (d.lastLoadError.empty() ? std::string("no render data") : d.lastLoadError) + ")");
        if (d.originalPath && d.worldDraws + d.bspDraws == 0) add("no map geometry drawn");
        if (d.opaqueNoDepthTest > 0) add(std::to_string(d.opaqueNoDepthTest) + " opaque draws without depth testing");
        if (d.glErrors > 0) add(std::to_string(d.glErrors) + " GL errors");
        {   // M43: driver debug-output errors / undefined behaviour since start (always-on callback)
            const auto& dc = glx::debugCounts();
            if (dc.errors + dc.undefined > 0) add(std::to_string(dc.errors + dc.undefined) + " GL debug errors");
        }
        // black / flat coverage judges a level (or a fallback), not a sparse menu backdrop: the lobby SpaceDome is ~70 %
        // flat by design (M11 long session: 98 false FAILs at 8 draws)
        const bool judgeImage = !d.originalPath || d.worldDraws + d.bspDraws >= 100;
        // M25: with the authored volume grades (Bloom_Scale 0.1 as on Streets, HighLights 1.5) dark maps are 60-65 %
        // below luma 10 at lit, correct views (Molten lava cave, Gorge shaft); a lost world is > 90 %
        if (judgeImage && m.black > 0.80f) add("scene " + std::to_string((int)(m.black * 100)) + "% black");
        if (judgeImage && m.flat > 0.70f) add("scene " + std::to_string((int)(m.flat * 100)) + "% flat tiles");
        // few colours alone is not a failure (the lobby SpaceDome is dark and smooth: 19 colours); with an almost
        // black image it is
        if (m.colors < 32 && m.p95 < 16.0f) add("near-black (" + std::to_string(m.colors) + " colours, luma p95 " +
                                                 std::to_string((int)m.p95) + ")");
        return why;
    }

    // Compact dump of the GL state a frame inherits (render-state leak audit between frontend scene / loading
    // screen / GFx host / map). Values: depth test/func/mask, blend + func, cull + mode, front face, scissor, colour
    // mask, polygon mode, program, VAO, array / element buffers, framebuffer, active texture + 2D binding,
    // fixed-function lighting / fog / texture 2D.
    static std::string captureGlState() {
        GLint v[4] = {0, 0, 0, 0};
        GLboolean b4[4] = {0, 0, 0, 0}, bm = 0;
        std::string s;
        auto I = [&](const char* n, GLenum e) { GLint x = 0; glGetIntegerv(e, &x); s += std::string(n) + "=" + std::to_string(x) + " "; };
        auto B = [&](const char* n, GLenum e) { s += std::string(n) + "=" + (glIsEnabled(e) ? "1 " : "0 "); };
        B("depth", GL_DEPTH_TEST); I("depthFunc", GL_DEPTH_FUNC);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &bm); s += std::string("depthMask=") + (bm ? "1 " : "0 ");
        B("blend", GL_BLEND); I("blendSrc", GL_BLEND_SRC); I("blendDst", GL_BLEND_DST);
        B("cull", GL_CULL_FACE); I("cullMode", GL_CULL_FACE_MODE); I("frontFace", GL_FRONT_FACE);
        B("scissor", GL_SCISSOR_TEST);
        glGetBooleanv(GL_COLOR_WRITEMASK, b4);
        s += "colorMask=" + std::to_string((int)b4[0]) + std::to_string((int)b4[1]) + std::to_string((int)b4[2]) + std::to_string((int)b4[3]) + " ";
        glGetIntegerv(GL_POLYGON_MODE, v); s += "polygonMode=" + std::to_string(v[0]) + " ";
        I("program", 0x8B8D /* GL_CURRENT_PROGRAM */); I("vao", 0x85B5 /* GL_VERTEX_ARRAY_BINDING */);
        I("arrayBuf", 0x8894 /* GL_ARRAY_BUFFER_BINDING */); I("elemBuf", 0x8895 /* GL_ELEMENT_ARRAY_BUFFER_BINDING */);
        I("fbo", 0x8CA6 /* GL_FRAMEBUFFER_BINDING */); I("activeTex", 0x84E0 /* GL_ACTIVE_TEXTURE */);
        I("tex2D", GL_TEXTURE_BINDING_2D);
        B("lighting", GL_LIGHTING); B("fog", GL_FOG); B("texture2D", GL_TEXTURE_2D);
        if (!s.empty()) s.pop_back();
        return s;
    }

    // A map or frontend scene asked for the original presentation and did not get it: a red frame on screen, so a
    // broken run cannot pass as a visual success. WFC_LEGACYRENDER (intended fallback) draws nothing.
    void drawLegacyMarker() {
        if (!renderDataRequested_ || wfc_.active() || WFC_ENV("WFC_LEGACYRENDER") || !inFrame_) return;
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_FOG);
        glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, vpW_, vpH_, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        const float b = 10.0f, W = (float)vpW_, Hh = (float)vpH_;
        glColor3f(1.0f, 0.0f, 0.0f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(W, 0); glVertex2f(W, b); glVertex2f(0, b);
        glVertex2f(0, Hh - b); glVertex2f(W, Hh - b); glVertex2f(W, Hh); glVertex2f(0, Hh);
        glVertex2f(0, 0); glVertex2f(b, 0); glVertex2f(b, Hh); glVertex2f(0, Hh);
        glVertex2f(W - b, 0); glVertex2f(W, 0); glVertex2f(W, Hh); glVertex2f(W - b, Hh);
        glEnd();
        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
        glPopAttrib();
        glx::textureCacheInvalidate();               // GL_ALL_ATTRIB_BITS restored the texture bindings
    }

    std::string glObjectCensus() const override {
        int tex = 0, buf = 0, prog = 0, fbo = 0, vao = 0;
        for (GLuint n = 1; n <= 131072; ++n) {
            if (glIsTexture(n)) ++tex;
            if (glx::IsBuffer && glx::IsBuffer(n)) ++buf;
            if (glx::IsProgram && glx::IsProgram(n)) ++prog;
            if (glx::IsFramebuffer && glx::IsFramebuffer(n)) ++fbo;
            if (glx::IsVertexArray && glx::IsVertexArray(n)) ++vao;
        }
        return "textures=" + std::to_string(tex) + " buffers=" + std::to_string(buf) + " programs=" + std::to_string(prog) +
               " framebuffers=" + std::to_string(fbo) + " vaos=" + std::to_string(vao) + " cpuMeshes=" + std::to_string(meshes_.size()) +
               " previewBodies=" + std::to_string(previewBodyCount());
    }
    RenderDiagnostics renderDiagnostics() const override {
        RenderDiagnostics d;
        d.originalPath = wfc_.active();
        d.legacyRequested = std::getenv("WFC_LEGACYRENDER") != nullptr;
        d.renderDataRequested = renderDataRequested_;
        d.renderDataRoot = wfc::Pipeline::renderDataRoot();
        d.mapDataDir = wfc_.active() ? wfc_.dataDir() : std::string();
        d.lastLoadError = wfc::Pipeline::lastLoadError();
        const auto& c = wfc_.lastFrameCounts();
        d.frame = wfc_.frameNumber();
        d.draws = c.draws; d.worldDraws = c.worldDraws; d.bspDraws = c.bspDraws; d.dynamicDraws = c.dynamicDraws;
        d.fxDraws = c.fxDraws; d.opaqueDraws = c.opaque; d.translucentDraws = c.translucent;
        d.lightmappedDraws = c.lightmapped; d.culledSubs = c.culled; d.noProgramSubs = c.noProgram;
        d.opaqueNoDepthTest = c.opaqueNoDepthTest; d.glErrors = glErrors_;
        d.distinctMaterials = c.materials; d.distinctPrograms = c.programs; d.noProgramMaterials = c.noProgramMats;
        d.materials = wfc_.materialCount(); d.programs = wfc_.programCount(); d.textures = wfc_.textureCount();
        d.lightmaps = wfc_.lightmapCount(); d.meshes = wfc_.meshCount();
        const core::Vec3& p = wfc_.cameraPos();
        d.camPos[0] = p.x; d.camPos[1] = p.y; d.camPos[2] = p.z;
        d.camYaw = lastCam_.yaw; d.camPitch = lastCam_.pitch; d.camFovX = lastCam_.fovXDeg;
        std::copy(wfc_.viewProjMatrix().m, wfc_.viewProjMatrix().m + 16, d.viewProj);
        glGetIntegerv(GL_VIEWPORT, d.viewport);
        glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &d.framebuffer);
        d.sceneSampled = sceneSampled_;
        d.sceneBlack = scene_.black; d.sceneFlat = scene_.flat; d.sceneLumaP50 = scene_.p50; d.sceneLumaP95 = scene_.p95;
        d.sceneColors = scene_.colors;
        d.scenePosesApplied = wfc_.scenePosesApplied();
        d.scenePosesUnknown = (int)wfc_.scenePosesUnknown().size();
        d.glEntryState = glEntry_;
        return d;
    }

    void writeVisualCheck(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h) {
        RenderDiagnostics d = renderDiagnostics();
        ImageMetrics fin = imageMetrics(rgb, w, h);
        std::string why = sceneSampled_ ? verdict(d, scene_) : verdict(d, fin);
        std::FILE* f = std::fopen((path + ".json").c_str(), "w");
        if (!f) return;
        auto esc = [](const std::string& s) { std::string o; for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; } return o; };
        std::fprintf(f, "{\n  \"verdict\": \"%s\",\n  \"reasons\": \"%s\",\n", why.empty() ? "PASS" : "FAIL", esc(why).c_str());
        std::fprintf(f, "  \"render_path\": \"%s\", \"legacy_requested\": %s, \"render_data_requested\": %s,\n",
                     d.originalPath ? "original" : "legacy", d.legacyRequested ? "true" : "false", d.renderDataRequested ? "true" : "false");
        std::fprintf(f, "  \"render_data_root\": \"%s\", \"map_data_dir\": \"%s\", \"last_load_error\": \"%s\",\n",
                     esc(d.renderDataRoot).c_str(), esc(d.mapDataDir).c_str(), esc(d.lastLoadError).c_str());
        std::fprintf(f, "  \"frame\": %d, \"draws\": %d, \"world_draws\": %d, \"bsp_draws\": %d, \"dynamic_draws\": %d, \"fx_draws\": %d,\n",
                     d.frame, d.draws, d.worldDraws, d.bspDraws, d.dynamicDraws, d.fxDraws);
        std::fprintf(f, "  \"opaque_draws\": %d, \"translucent_draws\": %d, \"lightmapped_draws\": %d, \"culled_subs\": %d, \"no_program_subs\": %d,\n",
                     d.opaqueDraws, d.translucentDraws, d.lightmappedDraws, d.culledSubs, d.noProgramSubs);
        std::fprintf(f, "  \"opaque_no_depth_test\": %d, \"gl_errors\": %d,\n", d.opaqueNoDepthTest, d.glErrors);
        std::fprintf(f, "  \"distinct_materials\": %d, \"distinct_programs\": %d,\n  \"no_program_materials\": [",
                     d.distinctMaterials, d.distinctPrograms);
        for (size_t i = 0; i < d.noProgramMaterials.size(); ++i)
            std::fprintf(f, "%s\"%s\"", i ? ", " : "", esc(d.noProgramMaterials[i]).c_str());
        std::fprintf(f, "],\n  \"loaded\": {\"materials\": %zu, \"programs\": %zu, \"textures\": %zu, \"lightmaps\": %zu, \"meshes\": %zu},\n",
                     d.materials, d.programs, d.textures, d.lightmaps, d.meshes);
        std::fprintf(f, "  \"camera_pos\": [%.3f, %.3f, %.3f], \"camera_yaw\": %.5f, \"camera_pitch\": %.5f, \"camera_fovx\": %.2f,\n  \"view_proj\": [",
                     d.camPos[0], d.camPos[1], d.camPos[2], d.camYaw, d.camPitch, d.camFovX);
        for (int i = 0; i < 16; ++i) std::fprintf(f, "%s%.6g", i ? ", " : "", d.viewProj[i]);
        std::fprintf(f, "],\n  \"viewport\": [%d, %d, %d, %d], \"framebuffer\": %d,\n", d.viewport[0], d.viewport[1],
                     d.viewport[2], d.viewport[3], d.framebuffer);
        std::fprintf(f, "  \"gl_entry_state\": \"%s\",\n", esc(d.glEntryState).c_str());
        std::fprintf(f, "  \"scene_poses\": {\"applied\": %d, \"unknown_actors\": %d},\n", d.scenePosesApplied, d.scenePosesUnknown);
        std::fprintf(f, "  \"scene\": {\"sampled\": %s, \"black\": %.4f, \"flat\": %.4f, \"luma_p50\": %.0f, \"luma_p95\": %.0f, \"colors\": %d},\n",
                     sceneSampled_ ? "true" : "false", scene_.black, scene_.flat, scene_.p50, scene_.p95, scene_.colors);
        std::fprintf(f, "  \"final\": {\"black\": %.4f, \"flat\": %.4f, \"luma_p50\": %.0f, \"luma_p95\": %.0f, \"colors\": %d}\n}\n",
                     fin.black, fin.flat, fin.p50, fin.p95, fin.colors);
        std::fclose(f);
        LOG_INFO("VISUALCHECK %s -> %s%s", path.c_str(), why.empty() ? "PASS" : "FAIL: ", why.c_str());
    }

    bool loadMapRenderData(const std::string& mapName) override {
        watchdog::phase("loadMapRenderData");
        // one map's render data at a time: a new load releases the previous map (level travel, frontend scenes)
        if (wfc_.active() || sceneMesh_ != kInvalidMesh) {
            unloadMapRenderData();
            sceneMesh_ = kInvalidMesh; sceneDir_.clear();
        }
        renderDataRequested_ = true;
        sceneSampled_ = false;
        bool ok = wfc_.load(mapName);
        matchMap_ = ok && !loadingFrontendScene_;
        if (ok) glDisable(GL_FOG);   // fog is evaluated per vertex in the shader path (UE3 height fog)
        if (ok && !loadingFrontendScene_) {
            animFileCache_.clear();          // M69: menu-only data, not held during a match
            wfc_.requestMaterialPrewarm();   // M54: after the world upload, yielding
            for (const MeshData& r : prewarmRequests_) wfc_.prewarmDynamic(r);   // M59: replayed per map load
        }
        return ok;
    }

    void setVisibilityQuery(VisibilityQuery q) override { wfc_.setVisibility(std::move(q)); }
    void setCharacterColors(const CharacterColors& c) override { wfc_.setCharacterColors(c); }
    void setDrawOwner(int o) override { wfc_.setDrawOwner(o); }
    void setDisplayGamma(float g) override { wfc_.setDisplayGamma(g); }
    void setFrontendActorTransform(const std::string& a, const core::Vec3& p, const core::Vec3& r) override {
        wfc_.setActorPose(a, p, r);
    }
    void setFrontendActorScale(const std::string& a, float s) override { wfc_.setActorScale(a, s); }
    bool sceneGroundHeight(float x, float y, float zFrom, float& z) const override {
        return wfc_.active() && wfc_.groundBelowUE(x, y, zFrom, z);
    }
    std::vector<FloatPropTrack> frontendFloatTracks() const override {
        std::vector<FloatPropTrack> out;
        if (sceneDir_.empty()) return out;
        std::ifstream f(wfc::Pipeline::renderDataRoot() + "/" + sceneDir_ + "/matinee_floatprops.json");
        if (!f) { LOG_WARN("frontend scene %s: no matinee_floatprops.json (rebuild render data)", sceneDir_.c_str()); return out; }
        std::stringstream ss; ss << f.rdbuf();
        assets::Json J;
        if (!assets::Json::parse(ss.str(), J)) return out;
        const assets::Json& T = J["tracks"];
        for (size_t i = 0; i < T.size(); ++i) {
            FloatPropTrack t;
            t.level = T[i]["level"].asString(); t.seqActInterp = T[i]["seqact_interp"].asString();
            t.matineeComment = T[i]["matinee_comment"].asString(); t.interpData = T[i]["interp_data"].asString();
            t.group = T[i]["group"].asString(); t.property = T[i]["property"].asString();
            const assets::Json& K = T[i]["keys"];
            for (size_t k = 0; k < K.size(); ++k) {
                InterpKeyF key;
                key.t = K[k]["t"].asFloat(); key.v = K[k]["v"].asFloat();
                key.arrive = K[k]["arrive"].asFloat(); key.leave = K[k]["leave"].asFloat();
                const std::string m = K[k]["mode"].asString();
                key.mode = m == "CIM_Constant" ? 0 : (m == "CIM_Linear" ? 1 : 2);
                t.keys.push_back(key);
            }
            out.push_back(std::move(t));
        }
        return out;
    }
    void setActorHidden(const std::string& actor, bool hidden) override { wfc_.setActorHidden(actor, hidden); }
    void setMapEffectActive(const std::string& what, bool active) override { wfc_.setMapEffectActive(what, active); }
    void setMapEffectState(const std::string& k, bool a, bool h) override { wfc_.setMapEffectState(k, a, h); }
    void setActiveGameRules(const std::vector<std::string>& r) override { wfc_.setActiveGameRules(r); }
    // glTF metres -> UE component rows: X = forward, Z = up (orthogonalised), Y = Z x X; position x100 (y / z swapped)
    static bool fxRows(const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& up, float R[3][3], float T[3]) {
        core::Vec3 x{fwd.x, fwd.z, fwd.y}, z{up.x, up.z, up.y};
        float xl = core::length(x);
        if (xl < 1e-6f) return false;
        x = x * (1.0f / xl);
        z = z - x * core::dot(z, x);
        float zl = core::length(z);
        if (zl < 1e-6f) {                                   // up parallel to forward: any perpendicular
            z = std::fabs(x.z) < 0.9f ? core::Vec3{0, 0, 1} : core::Vec3{1, 0, 0};
            z = z - x * core::dot(z, x); zl = core::length(z);
        }
        z = z * (1.0f / zl);
        core::Vec3 y = core::cross(z, x);
        const core::Vec3 rows[3] = {x, y, z};
        for (int r = 0; r < 3; ++r) { R[r][0] = rows[r].x; R[r][1] = rows[r].y; R[r][2] = rows[r].z; }
        T[0] = pos.x * 100.0f; T[1] = pos.z * 100.0f; T[2] = pos.y * 100.0f;
        return true;
    }
    int spawnParticleEffect(const std::string& tpl, const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& up,
                            const float* color) override {
        float R[3][3], T[3];
        if (!wfc_.active() || !fxRows(pos, fwd, up, R, T)) return -1;
        return wfc_.spawnFx(tpl, R, T, color, nullptr);
    }
    int spawnParticleEffectSegment(const std::string& tpl, const core::Vec3& a, const core::Vec3& b, const float* color) override {
        float R[3][3], T[3];
        if (!wfc_.active() || !fxRows(a, b - a, core::Vec3{0, 1, 0}, R, T)) return -1;
        float tgt[3] = {b.x * 100.0f, b.z * 100.0f, b.y * 100.0f};
        return wfc_.spawnFx(tpl, R, T, color, tgt);
    }
    bool setParticleEffectSegment(int h, const core::Vec3& a, const core::Vec3& b) override {
        float R[3][3], T[3];
        if (!wfc_.active() || !fxRows(a, b - a, core::Vec3{0, 1, 0}, R, T)) return false;
        const float tgt[3] = {b.x * 100.0f, b.z * 100.0f, b.y * 100.0f};
        return wfc_.setFxTransform(h, R, T) && wfc_.setFxTarget(h, tgt);
    }
    bool setParticleEffectTransform(int h, const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& up) override {
        float R[3][3], T[3];
        return wfc_.active() && fxRows(pos, fwd, up, R, T) && wfc_.setFxTransform(h, R, T);
    }
    void stopParticleEffect(int h) override { if (wfc_.active()) wfc_.stopFx(h); }
    bool setParticleEffectParam(int h, const std::string& n, const float v[4]) override {
        return wfc_.active() && wfc_.setFxParam(h, n, v);
    }
    bool setFrontendMaterialParam(const std::string& actor, const std::string& param, float value) override {
        const float v[4] = {value, value, value, value};
        return wfc_.active() && wfc_.setMaterialParam(actor, param, v);
    }
    int liveParticleEffects() const override { return wfc_.liveFx(); }
    void setParticleEffectPooled(int handle) override { wfc_.markFxPooled(handle); }
    void setEmitterPoolCap(bool on) override { wfc_.setEmitterPoolCap(on); }
    bool drawsAuthoredMapFx() const override { return wfc_.active(); }
    void setMapClock(float t) override { wfc_.setMapClock(t); }
    void setDestructibleState(const std::string& a, int s) override { wfc_.setDestructibleState(a, s); }

    FrameLimiter limiter_;
    bool limitFromEnv_ = false, slotWaited_ = false;
    void setFrameLimit(float hz) override {
        if (limitFromEnv_) return;                 // a test override wins
        if (hz != limiter_.limit()) LOG_INFO("renderer: frame limit %s", hz > 0 ? std::to_string((int)hz).c_str() : "off");
        limiter_.setLimit(hz);
    }
    float frameLimit() const override { return limiter_.limit(); }
    void setHudScreenEffect(int chain) override { wfc_.setHudScreenEffect(chain); }
    float drawOwnerRenderAge(int owner) const override { return wfc_.drawOwnerRenderAge(owner); }
    void notePresentedFrame() override { watchdog::phase("presented outside the renderer (movie / frontend)"); }
    void waitFrameSlot() override { watchdog::phase("frame limiter"); limiter_.wait(); slotWaited_ = true; }
    // M73 decal receivers: compact copy (positions + triangle indices) of the authored world geometry - the full CPU
    // world mesh is dropped after upload - with a ground-plane (x, z) grid of triangles for the box query
    struct DecalReceivers {
        std::vector<float> pos;          // glTF metres
        std::vector<uint32_t> tri;       // 3 indices per triangle
        std::vector<uint16_t> triMat;    // M76: per triangle, index into mats (the hit surface's material)
        std::vector<std::string> mats;
        std::map<std::string, uint16_t> matIndex;
        float cell = 8.0f, minX = 0, minZ = 0; int nx = 0, nz = 0;
        std::vector<uint32_t> start, list;   // CSR grid
        std::vector<uint32_t> big;           // triangles over more than 64 cells (terrain, domes): every query
        std::vector<uint32_t> stamp; uint32_t epoch = 0;
        bool dirty = false;
    } recv_;
    void addDecalReceivers(const MeshData& m) {
        if (m.subs.empty() || m.subs[0].component.empty()) return;            // authored world meshes only
        const uint32_t base = (uint32_t)(recv_.pos.size() / 3);
        recv_.pos.insert(recv_.pos.end(), m.positions.begin(), m.positions.end());
        for (const SubMesh& sm : m.subs) {
            const std::string mat = sm.material >= 0 && (size_t)sm.material < m.mats.size() ? m.mats[(size_t)sm.material].wfcName : "";
            if (mat.find("Invisible") != std::string::npos) continue;      // collision-only brushes are not drawn
            if (wfc_.isTranslucentMaterial(mat)) continue;                  // fog sheets / effects: no collision, no decals
            auto mi = recv_.matIndex.find(mat);
            if (mi == recv_.matIndex.end()) {
                mi = recv_.matIndex.emplace(mat, (uint16_t)std::min<size_t>(recv_.mats.size(), 65535)).first;
                recv_.mats.push_back(mat);
            }
            for (uint32_t k = sm.indexOffset; k + 2 < sm.indexOffset + sm.indexCount && k + 2 < m.indices.size(); k += 3) {
                for (int j = 0; j < 3; ++j) recv_.tri.push_back(base + m.indices[k + j]);
                recv_.triMat.push_back(mi->second);
            }
        }
        recv_.dirty = true;
    }
    void buildDecalGrid() {
        recv_.dirty = false;
        const auto tg0 = std::chrono::steady_clock::now();
        const size_t nt = recv_.tri.size() / 3;
        float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
        for (size_t i = 0; i + 2 < recv_.pos.size(); i += 3) {
            mnx = std::min(mnx, recv_.pos[i]); mxx = std::max(mxx, recv_.pos[i]);
            mnz = std::min(mnz, recv_.pos[i + 2]); mxz = std::max(mxz, recv_.pos[i + 2]);
        }
        if (nt == 0 || mnx > mxx) { recv_.nx = recv_.nz = 0; return; }
        recv_.minX = mnx; recv_.minZ = mnz;
        recv_.nx = std::max(1, (int)std::ceil((mxx - mnx) / recv_.cell) + 1);
        recv_.nz = std::max(1, (int)std::ceil((mxz - mnz) / recv_.cell) + 1);
        auto range = [&](size_t t, int& x0, int& x1, int& z0, int& z1) {
            float a = 1e30f, b = -1e30f, c = 1e30f, d = -1e30f;
            for (int j = 0; j < 3; ++j) {
                const float* p = &recv_.pos[(size_t)recv_.tri[t * 3 + j] * 3];
                a = std::min(a, p[0]); b = std::max(b, p[0]); c = std::min(c, p[2]); d = std::max(d, p[2]);
            }
            x0 = std::clamp((int)((a - recv_.minX) / recv_.cell), 0, recv_.nx - 1); x1 = std::clamp((int)((b - recv_.minX) / recv_.cell), 0, recv_.nx - 1);
            z0 = std::clamp((int)((c - recv_.minZ) / recv_.cell), 0, recv_.nz - 1); z1 = std::clamp((int)((d - recv_.minZ) / recv_.cell), 0, recv_.nz - 1);
        };
        std::vector<uint32_t> count((size_t)recv_.nx * recv_.nz + 1, 0);
        recv_.big.clear();
        auto isBig = [&](int x0, int x1, int z0, int z1) { return (int64_t)(x1 - x0 + 1) * (z1 - z0 + 1) > 64; };
        for (size_t t = 0; t < nt; ++t) {
            int x0, x1, z0, z1; range(t, x0, x1, z0, z1);
            if (isBig(x0, x1, z0, z1)) { recv_.big.push_back((uint32_t)t); continue; }
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) ++count[(size_t)z * recv_.nx + x + 1];
        }
        for (size_t i = 1; i < count.size(); ++i) count[i] += count[i - 1];
        recv_.start = count;
        recv_.list.assign(count.back(), 0);
        std::vector<uint32_t> fill(count.begin(), count.end() - 1);
        for (size_t t = 0; t < nt; ++t) {
            int x0, x1, z0, z1; range(t, x0, x1, z0, z1);
            if (isBig(x0, x1, z0, z1)) continue;
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) recv_.list[fill[(size_t)z * recv_.nx + x]++] = (uint32_t)t;
        }
        recv_.stamp.assign(nt, 0); recv_.epoch = 0;
        LOG_INFO("decal receivers: %zu triangles (%zu large), %dx%d cells (%.1f MB, %.0f ms)", nt, recv_.big.size(), recv_.nx, recv_.nz,
                 (recv_.pos.size() * 4 + recv_.tri.size() * 4 + recv_.list.size() * 4 + recv_.start.size() * 4) / 1048576.0,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tg0).count());
    }
    // the receiver triangle hit first by the segment o + d * t, t in (0, maxDist]: its material
    bool traceReceiverMaterial(const core::Vec3& o, const core::Vec3& d, float maxDist, core::Vec3& hit, std::string& mat,
                               core::Vec3* nOut = nullptr) {
        float best = maxDist; bool ok = false;
        const core::Vec3 e = o + d * maxDist;
        forDecalTrianglesIdx(std::min(o.x, e.x) - 0.01f, std::max(o.x, e.x) + 0.01f, std::min(o.z, e.z) - 0.01f,
                             std::max(o.z, e.z) + 0.01f, [&](uint32_t t, core::Vec3 A, core::Vec3 B, core::Vec3 C) {
            core::Vec3 e1 = B - A, e2 = C - A, pv = core::cross(d, e2);
            float det = core::dot(e1, pv);
            if (std::fabs(det) < 1e-9f) return;
            float inv = 1.0f / det; core::Vec3 tv = o - A;
            float u = core::dot(tv, pv) * inv; if (u < 0 || u > 1) return;
            core::Vec3 qv = core::cross(tv, e1);
            float v = core::dot(d, qv) * inv; if (v < 0 || u + v > 1) return;
            float tt = core::dot(e2, qv) * inv; if (tt <= 0 || tt >= best) return;
            best = tt; ok = true; hit = o + d * tt;
            mat = t < recv_.triMat.size() ? recv_.mats[recv_.triMat[t]] : std::string();
            if (nOut) { *nOut = core::normalize(core::cross(e1, e2)); if (core::dot(*nOut, d) > 0) *nOut = *nOut * -1.0f; }
        });
        return ok;
    }
    template <class F> void forDecalTriangles(float x0, float x1, float z0, float z1, F&& f) {
        forDecalTrianglesIdx(x0, x1, z0, z1, [&](uint32_t, core::Vec3 a, core::Vec3 b, core::Vec3 c) { f(a, b, c); });
    }
    template <class F> void forDecalTrianglesIdx(float x0, float x1, float z0, float z1, F&& f) {
        if (recv_.dirty) buildDecalGrid();
        if (recv_.nx == 0) return;
        if (++recv_.epoch == 0) { std::fill(recv_.stamp.begin(), recv_.stamp.end(), 0); recv_.epoch = 1; }
        const int cx0 = std::clamp((int)((x0 - recv_.minX) / recv_.cell), 0, recv_.nx - 1), cx1 = std::clamp((int)((x1 - recv_.minX) / recv_.cell), 0, recv_.nx - 1);
        const int cz0 = std::clamp((int)((z0 - recv_.minZ) / recv_.cell), 0, recv_.nz - 1), cz1 = std::clamp((int)((z1 - recv_.minZ) / recv_.cell), 0, recv_.nz - 1);
        auto visit = [&](uint32_t t) {
            if (recv_.stamp[t] == recv_.epoch) return;
            recv_.stamp[t] = recv_.epoch;
            const float* a = &recv_.pos[(size_t)recv_.tri[t * 3] * 3];
            const float* b = &recv_.pos[(size_t)recv_.tri[t * 3 + 1] * 3];
            const float* c2 = &recv_.pos[(size_t)recv_.tri[t * 3 + 2] * 3];
            f(t, core::Vec3{a[0], a[1], a[2]}, core::Vec3{b[0], b[1], b[2]}, core::Vec3{c2[0], c2[1], c2[2]});
        };
        for (uint32_t t : recv_.big) visit(t);
        for (int z = cz0; z <= cz1; ++z)
            for (int x = cx0; x <= cx1; ++x) {
                const size_t c = (size_t)z * recv_.nx + x;
                for (uint32_t i = recv_.start[c]; i < recv_.start[c + 1]; ++i) visit(recv_.list[i]);
            }
    }
    // Diagnostics: first receiver hit straight down from p (WFC_DECALTEST)
    bool traceDownReceivers(const core::Vec3& p, float maxDist, core::Vec3& hit, core::Vec3& n) {
        float best = maxDist; bool ok = false;
        const core::Vec3 d{0, -1, 0};
        forDecalTriangles(p.x - 0.01f, p.x + 0.01f, p.z - 0.01f, p.z + 0.01f, [&](core::Vec3 A, core::Vec3 B, core::Vec3 C) {
            core::Vec3 e1 = B - A, e2 = C - A, pv = core::cross(d, e2);
            float det = core::dot(e1, pv);
            if (std::fabs(det) < 1e-9f) return;
            float inv = 1.0f / det; core::Vec3 tv = p - A;
            float u = core::dot(tv, pv) * inv; if (u < 0 || u > 1) return;
            core::Vec3 qv = core::cross(tv, e1);
            float v = core::dot(d, qv) * inv; if (v < 0 || u + v > 1) return;
            float t = core::dot(e2, qv) * inv; if (t <= 0 || t >= best) return;
            best = t; ok = true; hit = p + d * t;
            n = core::normalize(core::cross(e1, e2)); if (n.y < 0) n = n * -1.0f;
        });
        return ok;
    }
    int spawnDecal(const std::string& material, const core::Vec3& L, const core::Vec3& dirIn, float w, float h,
                   float thick, float rollDeg, float lifetime) override {
        if (!wfc_.active() || w <= 0 || h <= 0 || thick <= 0) return -1;
        const core::Vec3 D = core::normalize(dirIn);
        const core::Vec3 ref = std::fabs(D.y) < 0.99f ? core::Vec3{0, 1, 0} : core::Vec3{1, 0, 0};
        const core::Vec3 T0 = core::normalize(core::cross(ref, D)), B0 = core::cross(D, T0);
        const float r = rollDeg * 3.14159265f / 180.0f, cr = std::cos(r), sr = std::sin(r);
        const core::Vec3 T = T0 * cr + B0 * sr, B = B0 * cr - T0 * sr;
        const float hw = w * 0.5f, hh = h * 0.5f, ht = thick * 0.5f;
        const float rad = std::sqrt(hw * hw + hh * hh + ht * ht);
        MeshData m;
        struct V { float s, t, d; };
        forDecalTriangles(L.x - rad, L.x + rad, L.z - rad, L.z + rad, [&](core::Vec3 A, core::Vec3 Bv, core::Vec3 C) {
            core::Vec3 n = core::cross(Bv - A, C - A);
            if (core::dot(n, D) >= 0) return;                 // faces away from the projector (no back faces)
            std::vector<V> poly;
            for (const core::Vec3& P : {A, Bv, C}) { core::Vec3 q = P - L; poly.push_back({core::dot(q, T), core::dot(q, B), core::dot(q, D)}); }
            // clip to the decal box (bNoClip false): s in [-hw, hw], t in [-hh, hh], d in [-ht, ht]
            for (int plane = 0; plane < 6 && !poly.empty(); ++plane) {
                const int ax = plane / 2; const float sg = (plane & 1) ? -1.0f : 1.0f;
                const float lim = ax == 0 ? hw : ax == 1 ? hh : ht;
                auto dist = [&](const V& v) { return lim - sg * (ax == 0 ? v.s : ax == 1 ? v.t : v.d); };
                std::vector<V> out;
                for (size_t i = 0; i < poly.size(); ++i) {
                    const V& a = poly[i]; const V& b = poly[(i + 1) % poly.size()];
                    const float da = dist(a), db = dist(b);
                    if (da >= 0) out.push_back(a);
                    if ((da >= 0) != (db >= 0)) {
                        const float k = da / (da - db);
                        out.push_back({a.s + (b.s - a.s) * k, a.t + (b.t - a.t) * k, a.d + (b.d - a.d) * k});
                    }
                }
                poly.swap(out);
            }
            if (poly.size() < 3) return;
            const core::Vec3 nn = core::normalize(n);
            const uint32_t base = (uint32_t)m.vertexCount();
            for (const V& v : poly) {
                const core::Vec3 P = L + T * v.s + B * v.t + D * v.d;
                m.positions.insert(m.positions.end(), {P.x, P.y, P.z});
                m.normals.insert(m.normals.end(), {nn.x, nn.y, nn.z});
                m.uv.insert(m.uv.end(), {0.5f - v.s / w, 0.5f - v.t / h});   // original decal VS (TileX / Y 1, offset 0)
            }
            for (uint32_t i = 1; i + 1 < poly.size(); ++i) m.indices.insert(m.indices.end(), {base, base + i, base + i + 1});
        });
        if (m.empty()) return -1;
        Material mat; mat.wfcName = material; mat.sourceName = material.substr(material.rfind('.') + 1);
        m.mats.push_back(mat);
        SubMesh s; s.indexOffset = 0; s.indexCount = (uint32_t)m.indices.size(); s.material = 0; m.subs.push_back(s);
        const size_t tris = m.indices.size() / 3;
        const int id = wfc_.addRuntimeDecal(std::move(m), lifetime);
        LOG_INFO("decal %s at (%.2f %.2f %.2f) %.2fx%.2f m: %zu triangles, %zu live", material.c_str(), L.x, L.y, L.z, w, h,
                 tris, wfc_.runtimeDecalCount());
        return id;
    }

    assets::Json impact_;                 // M76 impact_decals.json of the loaded map
    bool impactLoaded_ = false;
    std::mt19937 decalRng_{0x5eedu};
    float frand() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(decalRng_); }
    bool spawnImpactDecal(const std::string& weaponClass, const core::Vec3& hitIn, const core::Vec3& normalIn,
                          bool projectile) override {
        if (!wfc_.active()) return false;
        if (!impactLoaded_) {
            impactLoaded_ = true;
            std::ifstream f(wfc_.dataDir() + "/impact_decals.json", std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            if (!assets::Json::parse(ss.str(), impact_)) LOG_WARN("impact decals: impact_decals.json missing");
        }
        const assets::Json& W = impact_["weapons"][weaponClass];
        if (!W.isObject()) return false;
        const core::Vec3 n = core::normalize(normalIn);
        // the hit surface: instant hits at the hit point; projectiles trace 200 UU along -HitNormal
        core::Vec3 hit; std::string mat;
        const float reach = projectile ? 2.0f : 0.1f;
        if (!traceReceiverMaterial(hitIn + n * 0.02f, n * -1.0f, reach + 0.02f, hit, mat)) return false;
        std::string pm = impact_["materials"][mat].asString();
        if (pm.empty()) pm = impact_["default_surface"].asString();   // GEngine.DefaultPhysMaterial (Metal)
        const assets::Json& S = impact_["surfaces"][pm];
        if (pm.empty() || !S.isObject()) return false;              // no property object: no impact decal
        if (!projectile && S["no_decal"].asBool()) return false;
        const std::string type = projectile ? std::string("TnWeaponEffectsTypeExplosive") : W["effects_type"].asString();
        const assets::Json* group = nullptr;
        for (size_t g = 0; g < S["groups"].size() && !group; ++g)
            for (size_t t = 0; t < S["groups"][g]["types"].size(); ++t)
                if (S["groups"][g]["types"][t].asString() == type) { group = &S["groups"][g]; break; }   // None == None
        const assets::Json* d = nullptr;
        if (group && (*group)["decals"].size() > 0)
            d = &(*group)["decals"][std::min<size_t>((size_t)(frand() * (*group)["decals"].size()), (*group)["decals"].size() - 1)];
        else if (!projectile && W["default_decal"].isObject()) d = &W["default_decal"];
        if (!d) return false;
        const float r1 = frand(), r2 = (*d)["uniform"].asBool() ? r1 : frand();
        const float w = (*d)["min_w"].asFloat() + r1 * (*d)["range_w"].asFloat();
        const float h = (*d)["min_h"].asFloat() + r2 * (*d)["range_h"].asFloat();
        // instant hits: the surface's ApplyRandomRotationToDecal; projectiles: the entry's ApplyRandomRotation (add. 34)
        const bool spin = projectile ? (*d)["random_rotation"].asBool(true) : S["random_rotation"].asBool(true);
        const float roll = spin ? frand() * 360.0f : 0.0f;
        return spawnDecal((*d)["material"].asString(), hit, n * -1.0f, w * 0.01f, h * 0.01f,
                          (*d)["thickness"].asFloat(10.0f) * 0.01f, roll, (*d)["lifetime"].asFloat(30.0f)) >= 0;
    }

    MeshHandle uploadMesh(const MeshData& mesh) override {
        watchdog::phase("uploadMesh");
        if (mesh.empty()) return kInvalidMesh;
        addDecalReceivers(mesh);
        if (recv_.dirty && mesh.vertexCount() > 100000) buildDecalGrid();   // the world: under the loading screen
        const int gpu = wfc_.active() ? wfc_.upload(mesh) : -1;
        if (gpu >= 0 && mesh.vertexCount() > 100000 && !loadingFrontendScene_ && !uploadingFrontendWorld_)
            wfc_.warmupWorld(gpu, vpW_, vpH_);   // M75: the driver's first-draw work, under the loading screen
        // CPU copy for the GL 1.1 client-array fallback and WFC_PICK. A large world mesh owned by the shader path is
        // never drawn from it: dropping it saves ~150 MB per Streets load (M11 memory high-water). Small meshes keep
        // theirs (drawMeshFx falls back to it when an effect material is missing).
        static const bool picking = std::getenv("WFC_PICK") != nullptr;
        if (gpu >= 0 && mesh.vertexCount() > 100000 && !picking) meshes_.push_back(MeshData{});
        else meshes_.push_back(mesh);
        gpu_.push_back(gpu);
        return (MeshHandle)(meshes_.size() - 1);
    }

    TextureHandle uploadTexture(const ImageData& img) override {
        if (!img.valid()) return kInvalidTexture;
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        textures_.push_back(id);
        persistentTex_.push_back(false);
        return (TextureHandle)(textures_.size() - 1);
    }
    void setTexturePersistent(TextureHandle h) override {
        if (h >= 0 && (size_t)h < persistentTex_.size()) persistentTex_[(size_t)h] = true;
    }
    void releaseTexture(TextureHandle h) override {
        if (h < 0 || (size_t)h >= textures_.size() || !textures_[(size_t)h]) return;
        glDeleteTextures(1, &textures_[(size_t)h]);
        textures_[(size_t)h] = 0;
    }
    int liveTextureCount() const override {
        int n = 0;
        for (GLuint t : textures_) n += t ? 1 : 0;
        return n;
    }

    void drawMesh(MeshHandle h, const core::Mat4& model, const core::Vec3& color) override {
        watchdog::phase("drawMesh");
        if (h < 0 || (size_t)h >= meshes_.size()) return;
        if (wfc_.active() && gpu_[(size_t)h] >= 0) { wfc_.draw(gpu_[(size_t)h], model); glLoadMatrixf(view_.m); return; }
        drawMeshArrays(meshes_[(size_t)h], model, color);
    }

    // M59: requests are remembered and replayed after every map's render data loads. Callers prewarm once per
    // model (Gameplay: once per chassis, when its assets are cached), possibly before the first map's render data is
    // loaded, and every map load resets the pipeline's programs / textures.
    void setDrawMaterialParam(const std::string& n, const float v[4]) override { wfc_.setDrawMaterialParam(n, v); }
    void clearDrawMaterialParam(const std::string& n) override { wfc_.clearDrawMaterialParam(n); }
    void setDrawEnergyDeath(float d) override { wfc_.setDrawEnergyDeath(d); }
    void releaseMeshCaches(const MeshData& m) override { wfc_.releaseMeshCaches(&m); }
    void setUpscaling(float renderScale, float sharpness) override { wfc_.setUpscaling(renderScale, sharpness); }
    void setUpscaling(int mode) override {
        static const float kScale[4] = {1.0f, 0.667f, 0.588f, 0.5f};   // AMD FSR 1 presets (Quality / Balanced / Perf.)
        if (mode <= 0 || mode > 3) wfc_.setUpscaling(1.0f, -1.0f);       // Off: the original presentation
        else wfc_.setUpscaling(kScale[mode], 0.2f);
        LOG_INFO("renderer: upscaling mode %d (%s)", mode, mode <= 0 || mode > 3 ? "off" : mode == 1 ? "FSR 1 Quality" :
                 mode == 2 ? "FSR 1 Balanced" : "FSR 1 Performance");
    }
    void setAnisotropy(int level) override { wfc_.setAnisotropy(level); }
    bool hdTexturesAvailable() override { return wfc::Pipeline::hdTexturesAvailable(); }
    GpuFacts gpuFacts() override {
        if (!gpuFactsRead_) { gpuFacts_ = readGpuFacts(); gpuFactsRead_ = true; }
        return gpuFacts_;
    }
    GpuFacts gpuFacts_;
    bool gpuFactsRead_ = false;
    void setHdTextures(bool on) override { wfc_.setHdTextures(on); }
    void prewarmDynamicMesh(const MeshData& m) override {
        std::string key;
        for (const Material& mt : m.mats) key += mt.wfcName + "|" + mt.sourceName + ";";
        if (!prewarmKeys_.insert(key).second) return;
        MeshData lite;
        lite.subs = m.subs; lite.mats = m.mats;
        prewarmRequests_.push_back(std::move(lite));
        if (wfc_.active() && !loadingFrontendScene_) wfc_.prewarmDynamic(prewarmRequests_.back());
    }
    std::vector<MeshData> prewarmRequests_;
    std::set<std::string> prewarmKeys_;

    void drawDynamicMesh(const MeshData& m, const core::Mat4& model, const core::Vec3& color) override {
        watchdog::phase("drawDynamicMesh");
        glx::gpuMark(glx::kPassWorld);       // first character / dynamic draw: the world before it
        if (m.empty()) return;
        if (wfc_.active()) { wfc_.drawDynamic(m, model); glLoadMatrixf(view_.m); return; }
        drawMeshArrays(m, model, color);
    }

    void drawDynamicMeshPosed(const MeshData& m, const core::Mat4& model, const core::Vec3& color, uint64_t serial) override {
        watchdog::phase("drawDynamicMesh");
        glx::gpuMark(glx::kPassWorld);
        if (m.empty()) return;
        if (wfc_.active()) { wfc_.drawDynamic(m, model, &m, serial); glLoadMatrixf(view_.m); return; }
        drawMeshArrays(m, model, color);
    }

    void prewarmSkinnedMesh(const MeshData& bind, const std::vector<uint16_t>& joints, const std::vector<float>& weights) override {
        if (wfc_.active()) wfc_.prewarmSkinned(bind, joints, weights);
    }
    bool drawSkinnedMesh(const MeshData& bind, const std::vector<uint16_t>& joints, const std::vector<float>& weights,
                         const std::vector<core::Mat4>& palette, const std::vector<core::Mat4>* prevPalette, float alpha,
                         const core::Mat4& model, const core::Vec3& color, const void* key, uint64_t serial) override {
        static const bool off = std::getenv("WFC_NOGPUSKIN") != nullptr;   // A/B: the caller CPU-skins
        if (off || !wfc_.active() || bind.empty()) return false;
        (void)color;
        watchdog::phase("drawDynamicMesh");
        glx::gpuMark(glx::kPassWorld);
        const bool ok = wfc_.drawSkinned(bind, joints, weights, palette, prevPalette, alpha, model, key, serial);
        glLoadMatrixf(view_.m);
        return ok;
    }

    void drawDynamicMeshBlended(const MeshData& m, const std::vector<float>& prevP, const std::vector<float>& prevN, float alpha,
                                const core::Mat4& model, const core::Vec3& color, uint64_t serial) override {
        if (!wfc_.active() || alpha >= 1.0f || prevP.size() != m.positions.size()) {
            IRenderer::drawDynamicMeshBlended(m, prevP, prevN, alpha, model, color, serial);   // posed / CPU blend
            return;
        }
        watchdog::phase("drawDynamicMesh");
        glx::gpuMark(glx::kPassWorld);
        if (m.empty()) return;
        wfc_.drawDynamic(m, model, &m, serial, &prevP, &prevN, alpha);
        glLoadMatrixf(view_.m);
    }

    void drawBox(const core::Vec3& center, const core::Vec3& size,
                 const core::Vec3& color, float yaw) override {
        core::Mat4 model = core::Mat4::translate(center) * core::Mat4::rotateY(yaw) *
                           core::Mat4::scale(size);
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        drawUnitCube(color);
        glLoadMatrixf(view_.m);
    }

    void drawGroundGrid(float half, float spacing, const core::Vec3& c) override {
        glLoadMatrixf(view_.m);
        // Solid dark quad first for depth, then grid lines on top.
        glBegin(GL_QUADS);
        glColor3f(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f);
        glVertex3f(-half, 0, -half); glVertex3f(-half, 0, half);
        glVertex3f(half, 0, half);   glVertex3f(half, 0, -half);
        glEnd();

        glColor3f(c.x, c.y, c.z);
        glBegin(GL_LINES);
        for (float x = -half; x <= half + 0.001f; x += spacing) {
            glVertex3f(x, 0.01f, -half); glVertex3f(x, 0.01f, half);
        }
        for (float z = -half; z <= half + 0.001f; z += spacing) {
            glVertex3f(-half, 0.01f, z); glVertex3f(half, 0.01f, z);
        }
        glEnd();
    }

    bool captureScreenshot(const char* path) override {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        int w = vp[2], h = vp[3];
        if (w <= 0 || h <= 0) return false;
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        if (visualCheckOn()) writeVisualCheck(path, rgb, w, h);

        std::FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        int rowPad = (4 - (w * 3) % 4) % 4;
        uint32_t imgSize = (uint32_t)((w * 3 + rowPad) * h);
        uint32_t fileSize = 54 + imgSize;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        std::memcpy(hdr + 2, &fileSize, 4);
        uint32_t dataOff = 54; std::memcpy(hdr + 10, &dataOff, 4);
        uint32_t dibSize = 40; std::memcpy(hdr + 14, &dibSize, 4);
        std::memcpy(hdr + 18, &w, 4);
        std::memcpy(hdr + 22, &h, 4);            // positive = bottom-up (matches glReadPixels)
        uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
        uint16_t bpp = 24; std::memcpy(hdr + 28, &bpp, 2);
        std::memcpy(hdr + 34, &imgSize, 4);
        std::fwrite(hdr, 1, 54, f);
        std::vector<uint8_t> row((size_t)w * 3 + rowPad, 0);
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = rgb.data() + (size_t)y * w * 3;
            for (int x = 0; x < w; ++x) {       // RGB -> BGR
                row[(size_t)x * 3 + 0] = src[(size_t)x * 3 + 2];
                row[(size_t)x * 3 + 1] = src[(size_t)x * 3 + 1];
                row[(size_t)x * 3 + 2] = src[(size_t)x * 3 + 0];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        LOG_INFO("screenshot: %s (%dx%d)", path, w, h);
        return true;
    }

    void drawLine(const core::Vec3& a, const core::Vec3& b, const core::Vec3& c) override {
        glLoadMatrixf(view_.m);
        glColor3f(c.x, c.y, c.z);
        glBegin(GL_LINES);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
        glEnd();
    }

    void drawParticles(const ParticleBatch& b) override {
        watchdog::phase("drawParticles");
        if (!b.p || b.n == 0) return;
        glLoadMatrixf(view_.m);
        // Camera basis from the view matrix (rows of the rotation part).
        core::Vec3 camR{view_.m[0], view_.m[4], view_.m[8]};
        core::Vec3 camU{view_.m[1], view_.m[5], view_.m[9]};
        core::Vec3 camF{-view_.m[2], -view_.m[6], -view_.m[10]};
        if (wfc_.active() && b.material) {
            // Original emitter material: same quad construction as below, shaded by the compiled graph.
            std::vector<wfc::Pipeline::Sprite> sp(b.n);
            for (size_t i = 0; i < b.n; ++i) {
                const Particle& p = b.p[i];
                core::Vec3 ax, ay;
                particleAxes(p, camR, camU, camF, ax, ay);
                core::Vec3 hx = ax * (p.w * 0.5f), hy = ay * (p.h * 0.5f);
                wfc::Pipeline::Sprite& s = sp[i];
                s.c[0] = p.pos - hx - hy; s.c[1] = p.pos + hx - hy; s.c[2] = p.pos + hx + hy; s.c[3] = p.pos - hx + hy;
                particleUVs(p, s.uv);
                float k = b.colorScale;
                s.color[0] = p.r * k; s.color[1] = p.g * k; s.color[2] = p.b * k; s.color[3] = p.a;
            }
            if (WFC_ENV("WFC_FXLOG")) {
                static int logged = 0;
                if (logged++ < 8) LOG_INFO("fx sprites %s: n=%zu color0=(%.2f,%.2f,%.2f,%.2f)", b.material, sp.size(),
                                           sp[0].color[0], sp[0].color[1], sp[0].color[2], sp[0].color[3]);
            }
            if (wfc_.drawSprites(b.material, sp.data(), sp.size(), camF * -1.0f)) { glLoadMatrixf(view_.m); return; }
        }
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        // Restore the caller's fog state afterwards: the WFC shader path keeps fixed-function fog off.
        const GLboolean fogWas = glIsEnabled(GL_FOG);
        bool add = b.blend == ParticleBlend::Additive;
        if (add) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDisable(GL_FOG); }
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        bool tex = b.tex >= 0 && (size_t)b.tex < textures_.size();
        if (tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures_[(size_t)b.tex]);
            if (b.colorScale > 1.0f) {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, b.colorScale >= 4.0f ? 4.0f : (b.colorScale >= 2.0f ? 2.0f : 1.0f));
                // Alpha: texture alpha x vertex alpha (smoke opacity is baked into alpha at load).
                glTexEnvi(GL_TEXTURE_ENV, 0x8572 /*GL_COMBINE_ALPHA*/, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, 0x8588 /*GL_SOURCE0_ALPHA*/, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, 0x8598 /*GL_OPERAND0_ALPHA*/, GL_SRC_ALPHA);
                glTexEnvi(GL_TEXTURE_ENV, 0x8589 /*GL_SOURCE1_ALPHA*/, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, 0x8599 /*GL_OPERAND1_ALPHA*/, GL_SRC_ALPHA);
            }
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_QUADS);
        for (size_t i = 0; i < b.n; ++i) {
            const Particle& p = b.p[i];
            core::Vec3 ax, ay;   // ax: across (width), ay: up/along (height/length)
            particleAxes(p, camR, camU, camF, ax, ay);
            core::Vec3 hx = ax * (p.w * 0.5f), hy = ay * (p.h * 0.5f);
            glColor4f(p.r, p.g, p.b, p.a);
            core::Vec3 q[4] = {p.pos - hx - hy, p.pos + hx - hy, p.pos + hx + hy, p.pos - hx + hy};
            float uv[4][2];
            particleUVs(p, uv);
            for (int k = 0; k < 4; ++k) {
                glTexCoord2f(uv[k][0], uv[k][1]);
                glVertex3f(q[k].x, q[k].y, q[k].z);
            }
        }
        glEnd();
        if (tex && b.colorScale > 1.0f) {
            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        if (fogWas) glEnable(GL_FOG); else glDisable(GL_FOG);
    }

    static void particleAxes(const Particle& p, const core::Vec3& camR, const core::Vec3& camU, const core::Vec3& camF,
                             core::Vec3& ax, core::Vec3& ay) {
        float al = core::length(p.axis);
        if (al > 1e-5f) {
            ay = p.axis * (1.0f / al);
            ax = core::normalize(core::cross(camF, ay));
            if (core::length(ax) < 1e-4f) ax = camR;
        } else {
            float c = std::cos(p.rot), s = std::sin(p.rot);
            ax = camR * c + camU * s;
            ay = camU * c - camR * s;
        }
    }
    // UVs: v0 (texture top) at the +axis end; uAlongAxis maps U along the axis instead.
    static void particleUVs(const Particle& p, float uv[4][2]) {
        if (p.uAlongAxis) {
            uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u0; uv[1][1] = p.v0;
            uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u1; uv[3][1] = p.v1;
        } else {
            uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u1; uv[1][1] = p.v1;
            uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u0; uv[3][1] = p.v0;
        }
    }

    void drawMeshFx(MeshHandle h, const core::Mat4& model, float r, float g, float b, float a,
                    float colorScale, float fresnelExp, float fresnelScale, float fresnelPower) override {
        if (h < 0 || (size_t)h >= meshes_.size()) return;
        if (wfc_.active() && (size_t)h < gpu_.size() && gpu_[(size_t)h] >= 0) {
            // Mesh emitter with its original material: the graph supplies fresnel/panning/depth fade;
            // the particle colour (HDR) is the mesh-emitter vertex colour.
            float sc = colorScale, col[4] = {r * sc, g * sc, b * sc, a};
            if (wfc_.drawFx(gpu_[(size_t)h], model, col)) { glLoadMatrixf(view_.m); return; }
        }
        const MeshData& m = meshes_[(size_t)h];
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FOG);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, m.positions.data());
        bool haveUV = m.hasUV();
        if (haveUV) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data()); }
        float sc = colorScale >= 4.0f ? 4.0f : (colorScale >= 2.0f ? 2.0f : 1.0f);
        glColor4f(r, g, b, a);
        // Fresnel rim: per-vertex colour from the view direction (camera position from the view).
        std::vector<float> fcol;
        bool fres = fresnelExp > 0.0f && m.normals.size() == m.positions.size();
        if (fres) {
            core::Mat4 inv = view_;   // eye = -R^T t
            core::Vec3 eye{-(inv.m[0] * inv.m[12] + inv.m[1] * inv.m[13] + inv.m[2] * inv.m[14]),
                           -(inv.m[4] * inv.m[12] + inv.m[5] * inv.m[13] + inv.m[6] * inv.m[14]),
                           -(inv.m[8] * inv.m[12] + inv.m[9] * inv.m[13] + inv.m[10] * inv.m[14])};
            size_t vc = m.vertexCount();
            fcol.resize(vc * 4);
            for (size_t i = 0; i < vc; ++i) {
                core::Vec3 p = core::transformPoint(model, {m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]});
                core::Vec3 n = core::normalize(core::transformDir(model, {m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]}));
                core::Vec3 v = core::normalize(eye - p);
                float f = std::pow(std::max(0.0f, 1.0f - std::fabs(core::dot(n, v))), fresnelExp) * fresnelScale;
                f = std::pow(core::clampf(f, 0.0f, 1.0f), fresnelPower);
                fcol[i * 4] = r * f; fcol[i * 4 + 1] = g * f; fcol[i * 4 + 2] = b * f; fcol[i * 4 + 3] = a;
            }
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_FLOAT, 0, fcol.data());
        }
        std::vector<SubMesh> all;
        if (m.subs.empty()) { SubMesh whole; whole.indexCount = (uint32_t)m.indices.size(); all.push_back(whole); }
        for (const SubMesh& s : m.subs.empty() ? all : m.subs) {
            const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
            bool tex = haveUV && mat && mat->tex >= 0 && (size_t)mat->tex < textures_.size();
            if (tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, textures_[(size_t)mat->tex]);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, sc);
            } else {
                glDisable(GL_TEXTURE_2D);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT, m.indices.data() + s.indexOffset);
            if (tex) { glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); }
        }
        if (haveUV) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if (fres) glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FOG);
        glLoadMatrixf(view_.m);
    }

private:
    void drawMeshArrays(const MeshData& m, const core::Mat4& model, const core::Vec3& color) {
        // client-side vertex arrays: no buffer object may be bound (another renderer / UI pass may leave one)
        // [integration M06] the extension entry points are loaded with the WFC pipeline; on the legacy path
        // (WFC_LEGACYRENDER, no render data) they are null and nothing else binds buffers (was a crash: probe
        // still_vehicle_legacy, exit 0xC0000005)
        if (glx::BindBuffer) { glx::BindBuffer(GL_ARRAY_BUFFER, 0); glx::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
        if (glx::BindVertexArray) glx::BindVertexArray(0);
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glColor3f(color.x, color.y, color.z);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, m.positions.data());
        bool haveN = m.normals.size() == m.positions.size();
        if (haveN) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, m.normals.data()); }
        bool haveUV = m.hasUV();
        if (haveUV) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data()); }

        static const bool lmOff = std::getenv("WFC_NOLIGHTMAP") != nullptr;   // A/B debug toggle
        bool haveUV1 = m.hasUV1() && !lmOff;
        if (m.subs.empty()) {
            glDrawElements(GL_TRIANGLES, (GLsizei)m.indices.size(), GL_UNSIGNED_INT, m.indices.data());
        } else {
            // --- base pass: lightmapped submeshes drawn UNLIT (baked lighting replaces the
            // stand-in directional); everything else lit as before. ---
            for (const SubMesh& s : m.subs) {
                const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                          ? &m.mats[(size_t)s.material] : nullptr;
                bool lm = haveUV1 && s.lightmapTex >= 0 && (size_t)s.lightmapTex < textures_.size();
                if (lm) glDisable(GL_LIGHTING); else glEnable(GL_LIGHTING);
                TextureHandle th = mat ? mat->tex : kInvalidTexture;
                if (haveUV && th >= 0 && (size_t)th < textures_.size()) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, textures_[(size_t)th]);
                    glColor3f(1, 1, 1);
                } else {
                    glDisable(GL_TEXTURE_2D);
                    core::Vec3 c = mat ? mat->color : color;
                    glColor3f(c.x, c.y, c.z);
                }
                glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                               m.indices.data() + s.indexOffset);
            }
            glEnable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D);

            // --- baked-lightmap modulate pass: framebuffer *= atlas(uv1*scale + bias) ---
            if (haveUV1) {
                bool anyLm = false;
                for (const SubMesh& s : m.subs)
                    if (s.lightmapTex >= 0 && (size_t)s.lightmapTex < textures_.size()) { anyLm = true; break; }
                if (anyLm) {
                    glDisable(GL_LIGHTING);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_DST_COLOR, GL_ZERO);        // multiply
                    glDepthMask(GL_FALSE);
                    glEnable(GL_TEXTURE_2D);
                    glTexCoordPointer(2, GL_FLOAT, 0, m.uv1.data());   // lightmap UVs
                    // Reconstruct HDR lightmap brightness: fragment = atlas * (scaleVec/4) * 4.
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 4.0f);
                    for (const SubMesh& s : m.subs) {
                        if (s.lightmapTex < 0 || (size_t)s.lightmapTex >= textures_.size()) continue;
                        float r = s.lmScaleVec[0] * 0.25f, g = s.lmScaleVec[1] * 0.25f, b = s.lmScaleVec[2] * 0.25f;
                        glColor3f(r > 1 ? 1 : r, g > 1 ? 1 : g, b > 1 ? 1 : b);
                        glMatrixMode(GL_TEXTURE);
                        glLoadIdentity();
                        glTranslatef(s.lmBias[0], s.lmBias[1], 0.0f);
                        glScalef(s.lmScale[0], s.lmScale[1], 1.0f);     // atlasUV = uv1*scale + bias
                        glMatrixMode(GL_MODELVIEW);
                        glBindTexture(GL_TEXTURE_2D, textures_[(size_t)s.lightmapTex]);
                        glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                                       m.indices.data() + s.indexOffset);
                    }
                    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
                    glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data());     // restore UV0
                    glDisable(GL_TEXTURE_2D);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    glEnable(GL_LIGHTING);
                }
            }

            // Emissive additive pass: self-illumination (Autobot optics, energon glow),
            // unlit and added on top so it reads as glow regardless of scene lighting.
            bool anyEm = false;
            if (haveUV)
                for (const SubMesh& s : m.subs) {
                    const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                              ? &m.mats[(size_t)s.material] : nullptr;
                    if (mat && mat->emissiveTexHandle >= 0 &&
                        (size_t)mat->emissiveTexHandle < textures_.size()) { anyEm = true; break; }
                }
            if (anyEm) {
                glDisable(GL_LIGHTING);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glDepthMask(GL_FALSE);
                glEnable(GL_TEXTURE_2D);
                glColor3f(1, 1, 1);
                for (const SubMesh& s : m.subs) {
                    const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                              ? &m.mats[(size_t)s.material] : nullptr;
                    TextureHandle eh = mat ? mat->emissiveTexHandle : kInvalidTexture;
                    if (eh < 0 || (size_t)eh >= textures_.size()) continue;
                    glBindTexture(GL_TEXTURE_2D, textures_[(size_t)eh]);
                    glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                                   m.indices.data() + s.indexOffset);
                }
                glDisable(GL_TEXTURE_2D);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glEnable(GL_LIGHTING);
            }
        }

        if (haveUV) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        if (haveN) glDisableClientState(GL_NORMAL_ARRAY);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_LIGHTING);
        glLoadMatrixf(view_.m);
    }

    static void face(float r, float g, float b,
                     core::Vec3 a, core::Vec3 bb, core::Vec3 c, core::Vec3 d) {
        glColor3f(r, g, b);
        glVertex3f(a.x, a.y, a.z); glVertex3f(bb.x, bb.y, bb.z);
        glVertex3f(c.x, c.y, c.z); glVertex3f(d.x, d.y, d.z);
    }

    static void drawUnitCube(const core::Vec3& col) {
        // Unit cube of half-size 0.5, faces shaded for readability (fake directional light).
        const float h = 0.5f;
        core::Vec3 p000{-h, -h, -h}, p001{-h, -h, h}, p010{-h, h, -h}, p011{-h, h, h};
        core::Vec3 p100{h, -h, -h}, p101{h, -h, h}, p110{h, h, -h}, p111{h, h, h};
        auto shade = [&](float k) { return core::Vec3{col.x * k, col.y * k, col.z * k}; };
        glBegin(GL_QUADS);
        core::Vec3 s;
        s = shade(1.00f); face(s.x, s.y, s.z, p011, p111, p110, p010); // top (+Y)
        s = shade(0.45f); face(s.x, s.y, s.z, p000, p100, p101, p001); // bottom (-Y)
        s = shade(0.85f); face(s.x, s.y, s.z, p001, p101, p111, p011); // front (+Z)
        s = shade(0.60f); face(s.x, s.y, s.z, p100, p000, p010, p110); // back (-Z)
        s = shade(0.75f); face(s.x, s.y, s.z, p101, p100, p110, p111); // right (+X)
        s = shade(0.55f); face(s.x, s.y, s.z, p000, p001, p011, p010); // left (-X)
        glEnd();
    }

    core::Mat4 view_;
    int vpW_ = 0, vpH_ = 0;
    std::string glEntry_;
    struct PreviewBody { assets::SkinnedModel model; int clip = -1; std::vector<core::Mat4> scratch; };
    std::vector<std::unique_ptr<PreviewBody>> previewBodies_;
    std::function<void(IRenderer&)> sceneDraw_;
    Camera lastCam_;
    bool renderDataRequested_ = false;   // a map / scene asked for the original presentation (M10 legacy-fallback marker)
    bool sceneSampled_ = false, sceneSamplePending_ = false;
    long sceneFrames_ = 0;
    ImageMetrics scene_;
    int glErrors_ = 0, glErrorLogs_ = 0;
    ReticleState reticle_;
    GLuint reticleTex_ = 0;
    bool reticleTried_ = false;
    struct Tween {
        float from = 0, to = 0, start = -1, dur = 0.2f;
        float value(float now) const {                          // quadratic ease-out
            if (start < 0 || dur <= 0) return to;
            float t = std::min(std::max((now - start) / dur, 0.0f), 1.0f);
            return from + (to - from) * (1.0f - (1.0f - t) * (1.0f - t));
        }
    };
    Tween prongTween_, tintTween_[3] = {{1, 1}, {1, 1}, {1, 1}};
    bool prongSet_ = false;
    wfc::Pipeline wfc_;
    std::vector<int> gpu_;
    std::vector<ScreenBatch> screenQueue_;   // 2D batches submitted inside a 3D frame
    bool inFrame_ = false;
    std::vector<MeshData> meshes_;
    std::vector<GLuint> textures_;
    std::vector<bool> persistentTex_;     // parallel to textures_: survives unloadMapRenderData
    size_t texEpoch_ = 0;                 // first texture uploaded since the previous unloadMapRenderData
};

} // namespace

IRenderer* createGLRenderer() {
    auto* r = new GLRenderer();
    if (!r->init()) { delete r; return nullptr; }
    return r;
}

} // namespace render

namespace render {
std::string wfcRenderDataRoot() { return wfc::Pipeline::renderDataRoot(); }
}
