// Clean-room reconstruction — application side of the frontend boot (docs/FRONTEND.md).
// The flow itself (levels, lobbies, URLs, UI controller) is src/frontend/GameFlow; this file only owns the
// process loop: frontend frames, loading the match world the flow launches, and releasing it on return.
#include "core/Application.h"
#include "core/FrameProfile.h"
#include "core/FrontendSceneGL.h"
#include "core/LoadYield.h"
#include "core/Log.h"
#include "core/Time.h"
#include "frontend/FlowTrace.h"
#include "frontend/FrontendRuntime.h"
#include "game/MapState.h"
#include "game/MatchOpponent.h"
#include "platform/Image.h"
#include "platform/Movie.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/GfxPresenter.h"
#include "ui/UiGL.h"
#include "ui/gl/GlCensus.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

// Systems audio seam (agents/systems game::FrontendAudioRuntime + the World level-audio contract). Compiled in once
// the Systems lifecycle is integrated; without it the frontend runs silent (calls are traced only).
#if __has_include("game/FrontendAudioRuntime.h")
#include "game/FrontendAudioRuntime.h"
#define WFC_SYSTEMS_FRONTEND_AUDIO 1
namespace {
// Gameplay's look settings (PlayerController::setLookSettings(CameraSensitivity, InvertY_Robot, InvertY_Car,
// InvertY_Plane, InvertY_Tank); the 5-argument form, detected). Car covers trucks, as UpdateInvertMouseBy*Form.
// Rendering M28: unloadMapRenderData releases every texture the match uploaded (setTexturePersistent marks exceptions).
template <class R, class = void> struct HasMatchTextureRelease : std::false_type {};
template <class R>
struct HasMatchTextureRelease<R, std::void_t<decltype(std::declval<R&>().setTexturePersistent(std::declval<render::TextureHandle>()))>>
    : std::true_type {};
constexpr bool kRendererReleasesMatchTextures = HasMatchTextureRelease<render::IRenderer>::value;

// Rendering M09 (agents/rendering 73fd427): IRenderer::setFrameLimit(hz) paces presentation (0 = unlimited); the main
// loop calls waitFrameSlot. Detected; without it the window's own limiter (Win32Window::setFrameLimit) is used.
template <class R, class = void> struct HasRendererFrameLimit : std::false_type {};
template <class R>
struct HasRendererFrameLimit<R, std::void_t<decltype(std::declval<R&>().setFrameLimit(1.0f)), decltype(std::declval<R&>().waitFrameSlot())>>
    : std::true_type {};
template <class R> const char* applyFrameLimit(R* r, platform::IWindow* w, int hz) {
    if constexpr (HasRendererFrameLimit<R>::value) {
        if (r) { r->setFrameLimit((float)hz); if (w) w->setFrameLimit(0); return "renderer"; }
    }
    if (w) w->setFrameLimit(hz);
    return "window";
}

// Rendering 50f0742: preparePreviewBody parses a body's AnimSets into the shared cache and prewarms its materials without
// creating a body; loadContentMesh + prewarmDynamicMesh compile a mesh's materials. Detected.
template <class R, class = void> struct HasPreparePreviewBody : std::false_type {};
template <class R>
struct HasPreparePreviewBody<R, std::void_t<decltype(std::declval<R&>().preparePreviewBody(std::string(), std::vector<std::string>())),
                                            decltype(std::declval<R&>().prewarmDynamicMesh(std::declval<const render::MeshData&>())),
                                            decltype(std::declval<R&>().loadContentMesh(std::string(), std::declval<render::MeshData&>()))>>
    : std::true_type {};
// [integration 08n] fail the build, not the CaC preview prewarm, if the integrated renderer lacks this API.
static_assert(HasPreparePreviewBody<render::IRenderer>::value, "IRenderer::preparePreviewBody / prewarmDynamicMesh / loadContentMesh");
// The party lobby's Create a Character shows the selected character's Autobot and Decepticon (robot and vehicle):
// the bodies of the characters in the class list (defaults + custom) are prepared during the PartyLobby load, under its
// loading screen, so the first class pick and the first vehicle toggle do not parse / compile on a visible frame
// [PC ADAPTATION]. Only those chassis, not the roster's 33.
template <class R> int preparePreviewBodies(R* r, const frontend::CharacterRoster& roster) {
    if constexpr (HasPreparePreviewBody<R>::value) {
        if (!r) return 0;
        std::set<std::string> done;
        int n = 0;
        for (const auto& c : roster.customCharacters())
            for (const std::string& id : c.chassis) {
                const frontend::ChassisInfo* ci = roster.chassis(id);
                if (!ci || !done.insert(id).second) continue;
                if (!ci->robotGltf.empty()) { r->preparePreviewBody(ci->robotGltf, ci->robotAnimSets); ++n; }
                render::MeshData m;
                if (!ci->vehicleGltf.empty() && r->loadContentMesh(ci->vehicleGltf, m)) r->prewarmDynamicMesh(m);
                core::loadYield("Frontend: preview body prepared");
            }
        return n;
    } else { (void)r; (void)roster; return 0; }
}

// Gameplay's GRI objective fields (agents/gameplay bec41cd: HudGameState attackingTeamIndex / currentObjectiveCountdown /
// competitiveScoreEnabled), detected.
template <class H, class = void> struct HasGriObjective : std::false_type {};
template <class H>
struct HasGriObjective<H, std::void_t<decltype(std::declval<H&>().attackingTeamIndex), decltype(std::declval<H&>().currentObjectiveCountdown),
                                      decltype(std::declval<H&>().competitiveScoreEnabled)>> : std::true_type {};
template <class H> void fillGriObjective(const H& h, frontend::MatchValues& v) {
    if constexpr (HasGriObjective<H>::value) {
        v.attackingTeamIndex = h.attackingTeamIndex;
        v.currentObjectiveCountdown = h.currentObjectiveCountdown;
        v.competitiveScoreEnabled = h.competitiveScoreEnabled;
    } else { (void)h; (void)v; }
}

template <class PC, class = void> struct HasLookSettings : std::false_type {};
template <class PC>
struct HasLookSettings<PC, std::void_t<decltype(std::declval<PC&>().setLookSettings(0, false, false, false, false))>> : std::true_type {};
template <class PC> bool applyLookSettings(PC& pc, const frontend::LocalProfile& p) {
    if constexpr (HasLookSettings<PC>::value) {
        pc.setLookSettings(p.getInt("CameraSensitivity"), p.getBool("InvertY_Robot"), p.getBool("InvertY_Car"),
                           p.getBool("InvertY_Plane"), p.getBool("InvertY_Tank"));
        return true;
    } else { (void)pc; (void)p; return false; }
}
}

namespace {
struct SystemsFrontendAudio final : frontend::IFrontendAudio {
    game::FrontendAudioRuntime rt;
    explicit SystemsFrontendAudio(audio::IAudio* a) : rt(a) {}
    // Movie audio: the movie's own Bink tracks, decoded and streamed by Systems (FrontendAudioRuntime, M07), outside
    // the categories CINE_MUTE_FOR_BINK ducks [integration M06: replaces the frontend WAV cache].
    bool startMovieAudio(const std::string& p) override { return rt.startMovieAudio(p); }
    void stopMovieAudio() override {
        // Soak / sync evidence: the movie sound's clock when it is stopped (compare with movie.finished position) and
        // the Systems state right after (no movie stream may remain).
        const double clock = rt.movieAudioClock();
        rt.stopMovieAudio();
        const auto st = rt.state();
        frontend::FlowTrace::emit("movie.audioStop", {{"audioClock", frontend::FlowTrace::num(clock)},
                                                      {"movieStreamAfter", frontend::FlowTrace::boolean(st.movieAudio)},
                                                      {"voices", std::to_string(st.voices)}});
    }
    int playUiSound(const std::string& n) override { return rt.playUiSound(n); }
    bool stopUiSound(const std::string& n, float f) override { return rt.stopUiSound(n, f); }
    void uiLevelStarted(const std::string& l) override { rt.uiLevelStarted(l); }
    void levelChange() override { rt.levelChange(); }
    void tick(float dt) override { rt.tick(dt); }
    void levelEvent(const std::string& t) override { rt.levelEvent(t); }
    void setMoviePlaying(bool p) override { rt.setMoviePlaying(p); }
    void prefetchLevel(const std::string& l) override { rt.prefetchLevel(l); }
};
} // namespace
#endif

namespace core {

namespace {
std::unique_ptr<frontend::IFrontendAudio> g_frontendAudio;
std::unique_ptr<FrontendSceneGL> g_scene;   // the live level under the menus (interim IRenderer presentation)

// The original Brightness setting -> DisplayGamma: HmProfileSettings.GetGammaSetting [CONFIRMED decompile, via Rendering]
// DisplayGamma = 2.2 + Lerp(-0.95, 0.95, Clamp(GammaSetting / 100, 0, 1)) (1.25 .. 3.15, default 50 -> 2.2), published
// to the renderer's IRenderer::setDisplayGamma (agents/rendering 0653bb6) when this tree has it.
template <class R> void applyGamma(R* r, int gammaSetting) {
    float g = std::max(0.0f, std::min(1.0f, gammaSetting / 100.0f));
    float display = 2.2f + (-0.95f + 1.9f * g);
    if constexpr (HasDisplayGamma<R>::value) r->setDisplayGamma(display);
    else (void)r;
    frontend::FlowTrace::emit("settings.gamma", {{"GammaSetting", std::to_string(gammaSetting)}, {"DisplayGamma", frontend::FlowTrace::num(display)},
                                                 {"owner", HasDisplayGamma<R>::value ? "IRenderer::setDisplayGamma" : "none in this tree"}});
}

// Rendering's own bounded load-step callback (agents/rendering IRenderer::setLoadYield) when this tree's IRenderer has
// it: the loading frames then also come from inside loadMapRenderData's steps.
template <class R> void setRendererYield(R* r, bool on) {
    if constexpr (HasLoadYield<R>::value) {
        if (on) r->setLoadYield([] { core::loadYield("Render: load step"); });
        else r->setLoadYield(std::function<void()>());
    } else { (void)r; (void)on; }
}
// Process memory for the cycle soak (Experimental: leaks across frontend <-> match).
ui::GlCensus g_census;   // GL objects created by a match (released on travel away; stopgap, see GlCensus.h)

std::string processMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc))
        return frontend::FlowTrace::num(pmc.PrivateUsage / (1024.0 * 1024.0));
#endif
    return "0";
}
} // namespace

Application::Application() = default;
Application::~Application() = default;

bool Application::wantsFrontendBoot() {
    if (const char* b = std::getenv("WFC_BOOT")) return std::strcmp(b, "match") != 0;
    if (std::getenv("WFC_FRONTEND_SCRIPT") || std::getenv("WFC_FRONTEND_AUTOPLAY")) return true;
    // Existing automation and playtest variables keep the direct-to-match boot they were written for.
    for (const char* v : {"WFC_SMOKE_FRAMES", "WFC_SHOTLIST", "WFC_GAMEMODE", "WFC_MAP", "WFC_PICKUPTEST", "WFC_TRAVERSE",
                          "WFC_MAPTRAVERSE", "WFC_LOCKSTEP", "WFC_DEBUGCAM", "WFC_STARTVEHICLE",
                          // [integration M05] Gameplay / Rendering harnesses written against the direct boot
                          "WFC_XFORMTEST", "WFC_MATCHTEST", "WFC_CAMTEST", "WFC_CHAOS", "WFC_TDMTEST", "WFC_MATCH",
                          "WFC_MATCH_URL", "WFC_RELOADTEST", "WFC_CAMSYNC", "WFC_MODEPLAYTEST", "WFC_FRONTENDSCENE",
                          // [integration M08] Gameplay Pass 22 harnesses (direct boot; they exit when done)
                          "WFC_WEAPONTEST", "WFC_PARTICIPANTTEST", "WFC_CTFTEST", "WFC_MAPSUITE", "WFC_MARKERTEST",
                          "WFC_VEHTEST", "WFC_FOVTEST", "WFC_SCREENTEST", "WFC_TILETEST", "WFC_MODETEST", "WFC_CHASSISTEST",
                          "WFC_CHASSIS", "WFC_SWITCHTEST", "WFC_SCORETEST", "WFC_HEIGHTTEST", "WFC_VEHPHYS", "WFC_POINTPROBE", "WFC_PROJFXTEST", "WFC_MUZZLETEST", "WFC_RMUZZLETEST", "WFC_CHARGETEST", "WFC_DROPTEST", "WFC_PRELOADTEST", "WFC_RISERTEST", "WFC_EVENTTEST", "WFC_CLASSCHANGETEST", "WFC_PACINGTEST", "WFC_FINEAIMTEST", "WFC_XFORMVIS", "WFC_QATEST", "WFC_HEADJIT", "WFC_FXTEST"})
        if (std::getenv(v)) return false;
    return true;
}

void Application::attachPresenter() {
    // The shipped GFx movies (WFC_FRONTEND_NOGFX=1: flow only, no presentation).
    if (std::getenv("WFC_FRONTEND_NOGFX")) return;
    auto p = std::make_unique<ui::GfxPresenter>(*frontend_);
    if (!p->init()) { LOG_WARN("frontend: GFx movies unavailable (no frontend_gfx.json); flow only"); return; }
    presenter_ = p.get();
    frontend_->setPresenter(std::move(p));
    frontend_->setMoviePlayerFactory([] { return platform::createMoviePlayer(); });
    // PCSettings -> the platform window; the saved display settings apply at boot.
    frontend::FrontendRuntime::DisplayHooks dh;
    dh.modes = [this] {
        std::vector<std::pair<int, int>> v;
        for (const auto& m : window_->displayModes()) v.push_back({m.width, m.height});
        return v;
    };
    dh.apply = [this](int w, int h, bool fs) { window_->setDisplayMode(w, h, fs); };
    dh.vsync = [this](bool on) { window_->setVSync(on); };
    frontend_->setDisplayHooks(dh);
    {
        const auto& d = frontend_->flow().profile().display;
        if (d.fullscreen || d.width != window_->width() || d.height != window_->height()) window_->setDisplayMode(d.width, d.height, d.fullscreen);
        window_->setVSync(d.vsync);
        // PC ADAPTATION frame-rate limit: [PCSettings] FrameLimit (Hz, 0 = unlimited, the default; the PC graphics menu's
        // Frame Rate Limit entry), or WFC_FPS_LIMIT for tests. Presentation only: the simulation's fixed step is unaffected.
        int cap = d.frameLimit;
        if (const char* e = std::getenv("WFC_FPS_LIMIT")) cap = std::max(0, std::atoi(e));
        const char* by = applyFrameLimit(renderer_, window_, cap);
        frontend::FlowTrace::emit("display.frameLimit", {{"hz", std::to_string(cap)}, {"by", by}, {"when", "boot"}});
    }
    // Profile settings -> their runtime owners. No owner API exists yet for the volumes (Systems), the camera
    // sensitivity / invert-Y (Gameplay), vibration, subtitles or gamma (Rendering): the values are stored, persisted
    // and reported here so the owners can consume LocalProfile when they add the entry points.
    applyGamma(renderer_, frontend_->flow().profile().getInt("GammaSetting"));
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    // [Systems M08g] HmPlayerController.UpdateLocalCacheOfProfileSettings: SetAudioGroupVolume('Dialog' | 'SFX' |
    // 'MUSIC', slider / 100). Device-global and immediate: frontend, match and movie audio all follow.
    {
        const frontend::LocalProfile& p = frontend_->flow().profile();
        game::LevelAudioHost::applyProfileVolumes(p.getInt("Music Volume"), p.getInt("FX Volume"), p.getInt("Dialogue Volume"));
    }
#endif
    frontend_->flow().profile().onApplied = [this](const frontend::LocalProfile& p) {
        applyGamma(renderer_, p.getInt("GammaSetting"));
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
        game::LevelAudioHost::applyProfileVolumes(p.getInt("Music Volume"), p.getInt("FX Volume"), p.getInt("Dialogue Volume"));
#endif
        if (p.display.frameLimit != appliedFrameLimit_) {   // the Frame Rate Limit selector applies on every step
            appliedFrameLimit_ = p.display.frameLimit;
            const char* by = applyFrameLimit(renderer_, window_, appliedFrameLimit_);
            frontend::FlowTrace::emit("display.frameLimit", {{"hz", std::to_string(appliedFrameLimit_)}, {"by", by}, {"when", "apply"}});
        }
        if (applyLookSettings(world_.player().controller(), p))
            frontend::FlowTrace::emit("profile.lookSettings", {{"CameraSensitivity", p.get("CameraSensitivity")}, {"owner", "gameplay"}});
        frontend::FlowTrace::emit("profile.apply", {{"FXVolume", p.get("FX Volume")}, {"DialogueVolume", p.get("Dialogue Volume")},
                                                    {"MusicVolume", p.get("Music Volume")}, {"CameraSensitivity", p.get("CameraSensitivity")},
                                                    {"InvertY_Robot", p.get("InvertY_Robot")}, {"Vibration", p.get("Controller Vibration")},
                                                    {"owners", "volumes -> Systems; pending: Gameplay camera; gamma -> renderer"}});
    };
    if (!std::getenv("WFC_NO_FRONTEND_SCENE")) {
        g_scene = std::make_unique<FrontendSceneGL>(renderer_);
        frontend_->setSceneRenderer(g_scene.get());
    }
    // Logical UI bindings: defaults (the console presentation also binds Space to Start) + wfc_input.ini overrides.
    platform::UiBindings b = platform::UiBindings::defaults(!frontend_->isPC());
    bool ini = b.loadIni("wfc_input.ini");
    window_->setUiBindings(b);
    frontend::FlowTrace::emit("input.bindings", {{"sku", frontend_->platform()}, {"ini", frontend::FlowTrace::boolean(ini)}});
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    if (audio_) { g_frontendAudio = std::make_unique<SystemsFrontendAudio>(audio_); frontend_->setAudio(g_frontendAudio.get()); }
#endif
    frontend_->script().keyHook = [this](int code, bool down) { if (presenter_) presenter_->injectKey(code, down); };
    frontend_->script().shotHook = [this](const std::string& f) { pendingShot_ = f; };
    frontend_->script().clipHook = [this](const std::string& path, int& x, int& y) {
        return presenter_ && presenter_->clipWindowCenter(path, x, y);
    };
    // Create a Character palette swatches (GetPixelColor): pixel of the palette PNG, images cached for the session.
    frontend_->externalTexturePath = [this](const std::string& res) { return presenter_ ? presenter_->externalTexturePath(res) : std::string(); };
    frontend_->sampleImage = [](const std::string& png, int x, int y, int& r, int& g, int& b) {
        static std::map<std::string, render::ImageData> cache;
        auto it = cache.find(png);
        if (it == cache.end()) {
            render::ImageData img;
            platform::decodeImage(png, img);
            it = cache.emplace(png, std::move(img)).first;
        }
        const render::ImageData& im = it->second;
        if (!im.valid()) return false;
        // x, y are in the picker's 256-unit gradient space (CustomTransformers_GFX gradWidth / gradHeight 256, the
        // swatch selector's _x / _y); the palette textures are 128 x 128 [HIGH: the native GetPixelColor scaling].
        x = std::max(0, std::min(im.w - 1, x * im.w / 256));
        y = std::max(0, std::min(im.h - 1, y * im.h / 256));
        const uint8_t* px = &im.rgba[((size_t)y * (size_t)im.w + (size_t)x) * 4];
        r = px[0]; g = px[1]; b = px[2];
        return true;
    };
    frontend_->script().navCheckHook = [this](const std::string& label) {
        // Navigation stress harness: UI state + presenter report + process memory in one trace line.
        std::vector<std::pair<std::string, std::string>> kv = {{"label", label},
                                                                {"uiState", frontend::uiStateName(frontend_->flow().ui().state())},
                                                                {"level", frontend::levelKindName(frontend_->flow().level())},
                                                                {"openMovie", frontend_->flow().ui().openMovie()}};
        if (presenter_) for (auto& p : presenter_->navReport()) kv.push_back(p);
        kv.push_back({"privateMB", processMemoryMB()});
        if (g_scene) {   // Create a Character preview state (slots / visible / vehicle form / cached meshes / posed bodies)
            FrontendSceneGL::PreviewStats ps = g_scene->previewStats();
            kv.push_back({"preview", std::to_string(ps.slots) + "/" + std::to_string(ps.visible) + "/" + std::to_string(ps.vehicles) + "/" +
                                     std::to_string(ps.meshes) + "/" + std::to_string(ps.bodies)});
            kv.push_back({"rendererBodies", std::to_string(ps.rendererBodies)});   // -1: the renderer cannot report it
        }
        kv.push_back({"matinees", std::to_string(frontend_->scene().playing().size())});
        {   // live GL names (textures first): scene / map load hygiene across menu visits
            std::string live = ui::GlCensus::snapshot();
            kv.push_back({"glTextures", live.substr(9, live.find(' ') - 9)});
        }
        kv.push_back({"camera", frontend_->scene().view().camera});
        std::string line;
        for (const auto& [k, v] : kv) line += " " + k + "=" + (v.empty() ? std::string("-") : v);
        LOG_INFO("FLOW nav.check%s", line.c_str());
    };
    // Create a Character preview pawns -> the scene adapter (drawn by the renderer when it has the preview entry points).
    frontend_->previewHook = [](const frontend::FrontendRuntime::PreviewRequest& pr) {
        if (!g_scene) return;
        if (pr.call == "TransformPreviewCharacter" || pr.call == "TransformPreviewCharacterToRobot") {
            g_scene->transformPreview(pr.call == "TransformPreviewCharacterToRobot");
            return;
        }
        if (pr.call != "UpdatePreviewCharacter") return;
        std::vector<FrontendSceneGL::PreviewSlot> slots;
        for (const auto& s : pr.slots) {
            if (s.robotGltf.empty()) continue;
            FrontendSceneGL::PreviewSlot ps;
            ps.gltf = s.robotGltf;
            ps.vehicleGltf = s.vehicleGltf;
            ps.animSets = s.robotAnimSets;
            for (int k = 0; k < 3; ++k) { ps.pos[k] = s.posUE[k]; ps.primary[k] = s.primaryLinear[k]; ps.secondary[k] = s.secondaryLinear[k]; }
            ps.yawDeg = s.rotUEdeg[1];
            slots.push_back(ps);
        }
        g_scene->setPreview(std::move(slots));
    };
    // Title / lobby scene loads present frames while they block (the boot startup movie keeps playing) - the same
    // cooperative yield as the match load, also through the renderer's own load steps.
    frontend_->sceneLoadWrapper = [this](const std::function<void()>& load) {
        core::setLoadYield([this](double dt) {
            platform::InputFrame in;
            { core::prof::Scope prof("yield.pump"); window_->pump(in); }
            { core::prof::Scope prof("+yield.update"); frontend_->updateLoading((float)std::min(dt, 0.1)); }
            drawFrontendFrame();
        });
        setRendererYield(renderer_, true);
        load();
        setRendererYield(renderer_, false);
        core::setLoadYield(nullptr);
        { core::prof::Scope prof("movie.prewarm"); frontend_->prewarmLoadingUnderlay(); }   // still under the loading screen
        if (frontend_->flow().level() == frontend::LevelKind::PartyLobby) {
            core::setLoadYield([this](double dt) {
                platform::InputFrame in;
                window_->pump(in);
                frontend_->updateLoading((float)std::min(dt, 0.1));
                drawFrontendFrame();
            });
            core::prof::Scope prof("preview.prepare");
            const int n = preparePreviewBodies(renderer_, frontend_->roster());
            core::setLoadYield(nullptr);
            if (n > 0) frontend::FlowTrace::emit("preview.prepared", {{"bodies", std::to_string(n)}, {"owner", "renderer"}});
        }
        if (frontend_->sceneDrawable()) {   // the new scene's first (costly) draw happens under the loading screen
            core::prof::Scope prof("scene.prewarm");
            platform::InputFrame in;
            window_->pump(in);
            frontend_->prewarmSceneOnce();
            drawFrontendFrame();
        }
    };
    frontend_->script().displayHook = [this](int w, int h, bool full) { window_->setDisplayMode(w, h, full); };
    frontend_->script().dumpHook = [this](const std::string& m) {
        for (const std::string& o : presenter_->openMovieObjects())
            if (o.find(m) != std::string::npos) LOG_INFO("GFX DUMP %s\n%s", o.c_str(), presenter_->dumpMovie(o).c_str());
    };
}

namespace {
// Gameplay's QA API (agents/gameplay Pass 24c: World::qaWeaponIds / qaSetLoadout / qaRespawn / qaTeleportToStart /
// qaSetNoclip / qaSetGodMode / qaStatus; DEV / QA TOOLING, no-ops without WFC_QA), detected.
template <class W, class = void> struct HasQaApi : std::false_type {};
template <class W>
struct HasQaApi<W, std::void_t<decltype(std::declval<const W&>().qaWeaponIds(false)), decltype(std::declval<W&>().qaSetLoadout({})),
                               decltype(std::declval<W&>().qaRespawn()), decltype(std::declval<W&>().qaTeleportToStart(0)),
                               decltype(std::declval<W&>().qaSetNoclip(true)), decltype(std::declval<W&>().qaSetGodMode(true)),
                               decltype(std::declval<const W&>().qaStatus())>> : std::true_type {};
template <class W> std::vector<std::string> qaWeapons(const W& w) {
    if constexpr (HasQaApi<W>::value) return w.qaWeaponIds(false); else { (void)w; return {}; }
}
template <class W> std::string qaTool(W& w, platform::QaRequest::Kind k, const std::string& weapon, int& startIndex) {
    if constexpr (HasQaApi<W>::value) {
        using K = platform::QaRequest::Kind;
        if (k == K::Respawn) w.qaRespawn();
        else if (k == K::NextStart) w.qaTeleportToStart(++startIndex);
        else if (k == K::Noclip) w.qaSetNoclip(!w.qaNoclip());
        else if (k == K::God) w.qaSetGodMode(!w.qaGodMode());
        else if (k == K::Launch && !weapon.empty()) {   // the chosen weapon, once in game
            std::vector<std::string> refused = w.qaSetLoadout({weapon});
            if (!refused.empty()) return "weapon refused by the chassis restrictions: " + weapon;
        }
        return w.qaStatus();
    } else { (void)w; (void)k; (void)weapon; (void)startIndex; return "Gameplay QA API not in this build"; }
}
}

void Application::qaTick(const platform::InputFrame& in) {
    // DEBUG-ONLY QA panel (NOT ORIGINAL): development builds only (WFC_DEV_TOOLS; compiled out of shipping-style
    // builds). A separate tool window, never part of the frontend menus: F10 opens / hides it; WFC_QA=1 also opens it at
    // startup (and enables the WFC_QA_LAUNCH / WFC_QA_RESTART_AFTER shortcuts). Its requests run the normal frontend
    // flow (party lobby -> private game -> map -> countdown) through the script runner.
#if !WFC_DEV_TOOLS
    (void)in;
    return;
#else
    if (!frontend_) return;
    static const bool atStartup = std::getenv("WFC_QA") != nullptr;
    bool f10 = false;
    for (uint16_t k : in.keyPresses) f10 = f10 || k == 0x79;   // VK_F10 (Win32Window takes it from WM_SYSKEYDOWN)
    if (!qa_ && !atStartup && !f10) return;   // created on the first F10
    frontend::GameFlow& flow = frontend_->flow();
    if (!qa_) {
        qa_ = platform::createQaPanel();
        if (!qa_) return;
        std::vector<platform::QaPanel::Option> maps, modes, chars, weapons;
        for (const auto& m : frontend_->catalog().maps())
            if (m.mapId > 0) maps.push_back({(m.friendlyName.empty() ? m.mapFilename : m.friendlyName) + " (" + std::to_string(m.mapId) + ")",
                                             std::to_string(m.mapId)});
        for (const char* t : {"TDM", "DM", "CTF", "CP", "KOTH", "DOM", "EXT"}) modes.push_back({t, t});
        for (const auto& c : frontend_->roster().customCharacters()) chars.push_back({c.name, c.name});
        weapons.push_back({"(class default)", ""});
        for (const std::string& w : qaWeapons(world_)) weapons.push_back({w, w});
        qa_->setOptions(maps, modes, chars, weapons);
        qa_->setStatus("Debug QA panel (not original). F10 toggles.");
        LOG_INFO("QA panel created (developer build, %s)", atStartup ? "WFC_QA" : "F10");
        if (atStartup) qa_->show(true);
    }
    if (f10) {
        qa_->show(!qa_->visible());
        frontend::FlowTrace::emit("qa.panel", {{"visible", frontend::FlowTrace::boolean(qa_->visible())}, {"provenance", "DEBUG ONLY"}});
    }
    platform::QaRequest r = qa_->poll();
    {   // command-line equivalents (debug only): WFC_QA_LAUNCH=MODE,MAPID,CLASS once from the title;
        // WFC_QA_RESTART_AFTER=<s>: one Restart after that long in a match
        static bool launched = false, restarted = false;
        static double matchSince = 0;
        if (!launched && r.kind == platform::QaRequest::Kind::None && flow.level() == frontend::LevelKind::FrontEnd && flow.frontEndStarted())
            if (const char* e = std::getenv("WFC_QA_LAUNCH")) {
                std::string v = e;
                size_t a = v.find(','), b = v.find(',', a + 1);
                if (a != std::string::npos && b != std::string::npos) {
                    r.kind = platform::QaRequest::Kind::Launch;
                    r.mode = v.substr(0, a); r.mapId = std::atoi(v.substr(a + 1, b - a - 1).c_str()); r.character = v.substr(b + 1);
                }
                launched = true;
            }
        if (flow.level() == frontend::LevelKind::Match && flow.ui().state() == frontend::UIState::InGame) {
            if (matchSince == 0) matchSince = nowSeconds();
            if (const char* e = std::getenv("WFC_QA_RESTART_AFTER"))
                if (!restarted && r.kind == platform::QaRequest::Kind::None && nowSeconds() - matchSince > std::atof(e)) {
                    r.kind = platform::QaRequest::Kind::Restart; restarted = true;
                }
        } else matchSince = 0;
    }
    {   // the launched weapon, once the local player is in game; in-match tools
        static bool weaponPending = false;
        static int startIndex = 0;
        if (r.kind == platform::QaRequest::Kind::Launch || r.kind == platform::QaRequest::Kind::Restart) weaponPending = true;
        const bool inGame = flow.level() == frontend::LevelKind::Match && flow.ui().state() == frontend::UIState::InGame;
        if (weaponPending && inGame && qaLast_.kind == platform::QaRequest::Kind::Launch) {
            qa_->setStatus(qaTool(world_, platform::QaRequest::Kind::Launch, qaLast_.weapon, startIndex));
            weaponPending = false;
        }
        using K = platform::QaRequest::Kind;
        if (r.kind == K::Respawn || r.kind == K::NextStart || r.kind == K::Noclip || r.kind == K::God || r.kind == K::Dummy) {
            if (!inGame) { qa_->setStatus("In-match tools need a running match."); return; }
            if (r.kind == K::Dummy) { world_.addMatchOpponent("QA Dummy", true /* drawn: visible */); qa_->setStatus("Spawned a dummy opponent."); }
            else qa_->setStatus(qaTool(world_, r.kind, std::string(), startIndex));
            frontend::FlowTrace::emit("qa.tool", {{"kind", std::to_string((int)r.kind)}, {"provenance", "DEBUG ONLY"}});
            return;
        }
    }
    if (r.kind == platform::QaRequest::Kind::None) return;
    if (r.kind == platform::QaRequest::Kind::Restart) {
        if (qaLast_.kind == platform::QaRequest::Kind::None) { qa_->setStatus("Nothing launched yet."); return; }
        r = qaLast_;
    }
    const frontend::LevelKind level = flow.level();
    std::string s;
    if (level == frontend::LevelKind::Match) {   // leave the match through its own quit route (-> party lobby)
        flow.call("Game.QuitToMainMenu", {});
        flow.popupButton('A');
        s = "wait:level=PartyLobby;wait:t=1;";
    } else if (level == frontend::LevelKind::FrontEnd) {
        s = "call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:t=1;";
    } else if (level != frontend::LevelKind::PartyLobby) {
        qa_->setStatus("Use it from the title, the party lobby or a match.");
        return;
    }
    if (r.kind == platform::QaRequest::Kind::Title) {
        s += "ui:Back;wait:t=1;ui:Accept";
        qaCharacter_.clear();
    } else {
        qaLast_ = r;
        qaLast_.kind = platform::QaRequest::Kind::Launch;
        qaCharacter_ = r.character;
        s += "call:Online.PlayPrivateGame," + r.mode + ";wait:level=GameLobby;wait:t=1;call:Online.SetSelectedMapID," + std::to_string(r.mapId) +
             ";wait:t=0.3;call:Online.BeginLobbyExitCountdown";
    }
    frontend_->script().load(s);
    qa_->setStatus(std::string(r.kind == platform::QaRequest::Kind::Title ? "Back to title" : "Launching ") +
                   (r.kind == platform::QaRequest::Kind::Title ? "" : r.mode + " map " + std::to_string(r.mapId) + " as " + r.character));
    frontend::FlowTrace::emit("qa.request", {{"kind", r.kind == platform::QaRequest::Kind::Title ? "title" : "launch"}, {"mode", r.mode},
                                             {"map", std::to_string(r.mapId)}, {"character", r.character}, {"provenance", "DEBUG ONLY"}});
#endif   // WFC_DEV_TOOLS
}

void Application::shutdownFrontend() {
    if (frontend_) { frontend_->setAudio(nullptr); frontend_->setSceneRenderer(nullptr); }
    g_scene.reset();
    g_frontendAudio.reset();
    presenter_ = nullptr;
    frontend_.reset();
}

void Application::drawFrontendFrame() {
    window_->setOsCursorHidden(presenter_ && presenter_->drawsCursor());
    ui::beginScreenFrame(window_->width(), window_->height());
    frontend_->draw(window_->width(), window_->height());
    if (!pendingShot_.empty()) { renderer_->captureScreenshot(pendingShot_.c_str()); pendingShot_.clear(); }
    {   core::prof::Scope prof("present"); window_->present(); }
    {   // WFC_FRAMEPROF: the gap between two presented frames (main loop and load yields alike), with what ran in it
        static double lastPresent = 0;
        double t = core::prof::now();
        if (lastPresent > 0) {
            std::string hitch = core::prof::frameEnd(t - lastPresent);
            if (!hitch.empty())
                LOG_INFO("FLOW frame.hitch %s level=%s movie=%s", hitch.c_str(), frontend::levelKindName(frontend_->flow().level()),
                         frontend_->flow().ui().openMovie().c_str());
        } else core::prof::frameEnd(0);
        lastPresent = t;
    }
}

void Application::runFrontend() {
    frontend::GameFlow& flow = frontend_->flow();
    double last = nowSeconds();
    double started = last;
    double titleTimer = 0.0;
    const double timeout = std::getenv("WFC_FLOW_TIMEOUT") ? std::atof(std::getenv("WFC_FLOW_TIMEOUT")) : 0.0;
    const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
    platform::InputFrame input;
    for (;;) {
        // ---- frontend levels (and the loading screen up to the match load) ----
        bool quit = false;
        for (;;) {
            bool pumped;
            { core::prof::Scope prof("pump"); pumped = window_->pump(input); }
            if (!pumped) { quit = true; break; }
            qaTick(input);
            double now = nowSeconds();
            double dt = now - last;
            last = now;
            if (dt > 0.25) dt = 0.25;
            if (lockstep) dt = 1.0 / 60.0;
            {
                core::prof::Scope prof("+frontend.update");
                frontend_->update(input, (float)dt);
            }
            if (flow.quitRequested()) { quit = true; break; }
            drawFrontendFrame();
            titleTimer += dt;
            if (titleTimer > 0.25) { titleTimer = 0.0; window_->setTitle(frontend_->titleText().c_str()); }
            // The loading movie plays its intro (LoadScreen_GFX "spinIn" -> "loopStart": 34 frames at 30 fps) before the
            // blocking world load starts (the load is not threaded yet: PARTIAL).
            if (flow.hasPendingMatch() && (!presenter_ || !presenter_->hasLoadingMovie() || presenter_->loadingSeconds() >= 34.0f / 30.0f)) break;
            if (timeout > 0.0 && now - started > timeout) {
                frontend::FlowTrace::emit("timeout", {{"seconds", frontend::FlowTrace::num(now - started)}});
                flow.traceSnapshot("timeout");
                quit = true;
                break;
            }
        }
        if (quit) return;

        // ---- match: ServerTravelToMap(match URL). The loading movie stays up (last frame) during the blocking load.
        if (!loadMatch(flow.pendingMatch())) {
            flow.matchLoadFailed("world load failed");
            unloadMatch();
            flow.worldUnloaded();
            continue;
        }
        flow.matchLoaded();
        applyLookSettings(world_.player().controller(), flow.profile());   // the profile's look settings for this match
        // [integration] Frontend's PROVISIONAL adapter (immediate BeginGame) is replaced by Gameplay's match lifecycle.
        // World::launchMatch put the match in PendingMatch (10 s). Client order (RE M05 blockers D5): WaitingOnGameStart ->
        // character select -> PreGameCountdown during PendingMatch -> UI event 3 at InProgress. No character select screen
        // exists yet, so the default character is selected now (OnCharacterSelected, bMatchHasBegun false -> GameStartUI);
        // routeMatchToFrontend() sends UI event 3 when Gameplay's MatchStarted arrives.
        // WaitingOnGameStart opens CustomTransformers_GFX ("Choose Character"); the player's Customize.SelectCharacter
        // continues to the pre-game screen [RE MILESTONE05_PLAYTEST_RE section 7, CONFIRMED]. Automation (scripted
        // frontend runs, the lifecycle driver) selects the first default character instead unless WFC_CHARSELECT=1.
        bool automated = std::getenv("WFC_FRONTEND_SCRIPT") || std::getenv("WFC_FRONTEND_AUTOPLAY") || std::getenv("WFC_LIFECYCLE");
        if (!qaCharacter_.empty()) {   // DEBUG QA launch: the chosen class, through the normal selection contract
            flow.selectCharacter(frontend_->selectionFor(qaCharacter_));
        } else if (automated && !std::getenv("WFC_CHARSELECT")) {
            // Same contract as Customize.SelectCharacter (the first custom slot), not a second derivation.
            std::string first = frontend_->roster().customCharacters().empty() ? std::string() : frontend_->roster().customCharacters().front().name;
            flow.selectCharacter(frontend_->selectionFor(first));
        }
        localDeadForUi_ = spectatingUi_ = false;
        localDeadTime_ = 0.0f;
        window_->setMouseCaptured(true);
        mouseCaptured_ = true;

        MatchExit e = runMatch();
        if (e == MatchExit::Quit) return;
        unloadMatch();
        flow.worldUnloaded();
        last = nowSeconds();
    }
}

// [Gameplay 24r] defined after fillFullSelection below.
namespace {
template <class W, class FE>
auto preloadSavedCustomCharacters(W& world, const FE& fe, int)
    -> decltype(world.preloadSelections(std::vector<game::CharacterSelection>{}), void());
template <class W, class FE> void preloadSavedCustomCharacters(W&, const FE&, long);
}

bool Application::loadMatch(const frontend::MatchLaunch& m) {
    if (!m.map) return false;
    world_.setMapName(m.map->runtimeDir);
    bool modeOk = false;
    for (game::MatchMode mm : {game::MatchMode::DM, game::MatchMode::TDM, game::MatchMode::CTF, game::MatchMode::KOTH,
                               game::MatchMode::EXT, game::MatchMode::DOM})
        if (m.modeTag == game::gameModeName(mm)) { world_.setMatchMode(mm); modeOk = true; }
    if (!modeOk) { LOG_WARN("FLOW match mode %s is not supported by Gameplay", m.modeTag.c_str()); return false; }
    LOG_INFO("FLOW loading match world: map dir %s, mode %s", m.map->runtimeDir.c_str(), m.modeTag.c_str());
    double t0 = nowSeconds();
    if (g_scene) {   // the frontend scene's render data and GL objects go before the match map loads
        ui::GlCensus::Owned keep;
        if (presenter_) presenter_->ownedGl(keep);
        frontend::FlowTrace::emit("scene.release", {{"released", g_scene->release(keep)}});
    }
    g_census.begin();
    // The loading screen keeps presenting during the synchronous load (core::loadYield): window messages, the
    // LoadScreen_GFX animation and the TF_LoadingScreen underlay, one frame per yield.
    core::resetLoadYieldStats();
    core::setLoadYield([this](double dt) {
        platform::InputFrame in;
        window_->pump(in);   // a close request stays pending and ends the frontend loop after the load
        frontend_->updateLoading((float)std::min(dt, 0.1));
        // Validation: WFC_LOADSHOTS=<prefix> captures loading frames 10 / 60 / 120 from inside the load.
        static const char* shots = std::getenv("WFC_LOADSHOTS");
        static int n = 0;
        if (shots && (++n == 10 || n == 60 || n == 120)) pendingShot_ = std::string(shots) + std::to_string(n) + ".bmp";
        drawFrontendFrame();
    });
    struct YieldGuard {
        render::IRenderer* r;
        ~YieldGuard() { core::setLoadYield(nullptr); setRendererYield(r, false); }
    } yieldGuard{renderer_};   // also on the failure returns
    setRendererYield(renderer_, true);
    world_.load(*renderer_);
    core::loadYield("Application.loadMatch: world loaded");
    if (!world_.usingSlice()) { LOG_WARN("FLOW match world failed to load (graybox fallback)"); return false; }
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    // Systems level-audio contract: no hard-wired slice audio; the selected map's audio.
    world_.setAudio(audio_, false);
    {   // [integration] soak evidence: the frontend audio baseline before the map audio loads
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.baseline", {{"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                      {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
    world_.loadMapAudio(m.map->runtimeDir);
    core::loadYield("Application.loadMatch: map audio loaded");
    {   // [integration] soak evidence: Systems audio state with the selected map loaded
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.loaded", {{"level", as.level}, {"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                    {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
#else
    world_.setAudio(audio_);
#endif
    // [integration] Gameplay owns the match: the frontend's StartLevel URL is the launch contract (Gameplay PASS 20b,
    // RE M05 blockers D1-D3: GameModeTag / PointsToWin / TimeLimit passed explicitly). The map is the catalog
    // selection's runtime directory (World loaded it above); teams are Gameplay's PickTeam (RE E4, offline).
    game::MatchLaunch gl;
    game::MatchLaunch::fromURL(m.url.toString(), gl);
    gl.map = m.map->runtimeDir;
    // [integration M08b] The team the lobby assigned (random PickTeam or the player's Switch Team) is the team the player
    // spawns on; Gameplay's own PickTeam used to override it (always Autobots offline). Experimental audit P1-1.
    gl.localTeam = m.teamIndex;
    // TEST / VALIDATION ONLY (explicit opt-in): WFC_LIFECYCLE=<goal score> shortens the match to that score and adds
    // one Gameplay diagnostic opponent (MatchOpponent); driveLifecycleTest() then produces kills and deaths through
    // World::applyMatchDamage, so the real rules run scoring, death, respawn wave, score-limit end and the return.
    lifecycleGoal_ = 0;
    if (const char* lc = std::getenv("WFC_LIFECYCLE")) {
        lifecycleGoal_ = std::max(1, std::atoi(lc));
        gl.settings.goalScore = lifecycleGoal_;
        frontend::FlowTrace::emit("test.lifecycle", {{"goalScore", std::to_string(lifecycleGoal_)}, {"provenance", "TEST ONLY"}});
    }
    if (!world_.launchMatch(gl)) { LOG_WARN("FLOW Gameplay refused the match (%s %s)", gl.map.c_str(), gl.modeTag.c_str()); return false; }
    if (lifecycleGoal_ > 0) world_.addMatchOpponent("LifecycleOpponent", true);
    // [integration M06] Character selection -> Gameplay (GAMEPLAY_FRONTEND_HUD_CONTRACT.md 3): the local player's body
    // comes from the frontend's CustomTransformers selection; CheckReadySpawn waits for it [CONF].
    world_.match().requireCharacterSelection(world_.localMatchPlayer());
    preloadSavedCustomCharacters(world_, *frontend_, 0);   // [Gameplay 24r] saved CaC slots, under the match load
    selectionSentSerial_ = 0;
    lifecycleT_ = 0.0f; lifecycleStep_ = 0;
    frontend::FlowTrace::emit("match.gameplay", {{"map", gl.map}, {"mode", gl.modeTag}, {"goalScore", std::to_string(gl.settings.goalScore)},
                                                 {"timeLimit", std::to_string(gl.settings.timeLimit)}});
    {   // Experimental RUNTIME-EVENTS protocol (MATCH lines), from Gameplay's launch settings and the authored rule list
        std::string rules;
        if (m.settings) for (const std::string& r : m.settings->rules) rules += (rules.empty() ? "" : ",") + r;
        if (matchesLaunched_++ > 0) LOG_INFO("MATCH restart");
        LOG_INFO("MATCH init mode=%s rules=%s teams=%d time_limit_s=%d score_limit=%d", gl.modeTag.c_str(), rules.c_str(),
                 gl.settings.teamGame ? 2 : 0, gl.settings.timeLimit, gl.settings.goalScore);
        matchClock_ = 0.0f; lastLoggedRemaining_ = -1; deathAt_.clear();
    }
    gameMode_.begin(world_);
    core::setLoadYield(nullptr);
    const core::LoadYieldStats ys = core::loadYieldStats();
    frontend::FlowTrace::emit("match.loaded", {{"map", m.map->runtimeDir}, {"mode", m.modeTag},
                                               {"seconds", frontend::FlowTrace::num(nowSeconds() - t0)}, {"privateMB", processMemoryMB()},
                                               {"loadingFrames", std::to_string(ys.frames)},
                                               {"maxFrameGapMs", frontend::FlowTrace::num(ys.maxGapMs)}, {"maxGapAt", ys.maxGapAt}});
    return true;
}

namespace {
// Gameplay's full CharacterSelection (agents/gameplay 1216e80: chassisByFaction, colours, loadout lists) is filled from
// the Frontend contract when this tree has it; older Gameplay keeps the chassisId-by-team mapping only.
template <class CS, class = void> struct HasFullSelection : std::false_type {};
template <class CS> struct HasFullSelection<CS, std::void_t<decltype(std::declval<CS&>().chassisByFaction[0]),
                                                            decltype(std::declval<CS&>().primary[0].palette),
                                                            decltype(std::declval<CS&>().weapons)>> : std::true_type {};
template <class CS> void fillFullSelection(CS& cs, const frontend::GameFlow::SelectedCharacter& fc) {
    if constexpr (HasFullSelection<CS>::value) {
        for (int f = 0; f < 2; ++f) {
            cs.chassisByFaction[f] = fc.chassis[f];
            auto col = [](const frontend::GameFlow::CharacterColorSel& in, auto& out) {
                out.r = in.r; out.g = in.g; out.b = in.b; out.a = in.a; out.palette = in.palette; out.x = in.x; out.y = in.y;
            };
            col(fc.primary[f], cs.primary[f]);
            col(fc.secondary[f], cs.secondary[f]);
        }
        cs.weapons = fc.weapons; cs.vehicleWeapons = fc.vehicleWeapons; cs.melee = fc.melee;
        cs.abilities = fc.abilities; cs.skills = fc.skills;
    } else { (void)cs; (void)fc; }
}

// [Gameplay 24r] World::preloadSelections (agents/gameplay 6a5c213): cache the player's saved custom characters' bodies and
// held-weapon models under the match load, so picking one in the lobby does not load them on that frame (~0.5 s, Integration
// 08k RENDERSTATS). The local faction's class presets are preloaded by Gameplay itself (24o / 24q). Detected at compile time:
// a no-op against a World without it.
template <class W, class FE>
auto preloadSavedCustomCharacters(W& world, const FE& fe, int)
    -> decltype(world.preloadSelections(std::vector<game::CharacterSelection>{}), void()) {
    std::vector<game::CharacterSelection> saved;
    for (const auto& c : fe.roster().customCharacters()) {
        const frontend::GameFlow::SelectedCharacter fc = fe.selectionFor(c.name);
        if (!fc.valid) continue;
        game::CharacterSelection cs;
        cs.type = fc.type;
        const std::string sp = fc.specialty;
        cs.specialty = sp == "Scientist" ? game::Specialty::Scientist : sp == "Scout" ? game::Specialty::Scout
                     : sp == "Soldier" ? game::Specialty::Soldier : game::Specialty::Leader;
        cs.chassisId = fc.chassis[0];
        cs.customSlot = fc.name;
        fillFullSelection(cs, fc);
        saved.push_back(cs);
    }
    world.preloadSelections(saved);
    frontend::FlowTrace::emit("match.preloadCustom", {{"count", std::to_string(saved.size())}});
}
template <class W, class FE> void preloadSavedCustomCharacters(W&, const FE&, long) {}
// [integration 08n] the integrated World must provide it (else the saved-CaC preload would silently drop out).
static_assert(sizeof(decltype(std::declval<game::World&>().preloadSelections(std::vector<game::CharacterSelection>{}), 0)) > 0, "World::preloadSelections");
}

void Application::routeMatchToFrontend(float dt) {
    frontend::GameFlow& flow = frontend_->flow();
    const int me = world_.localMatchPlayer();
    // [integration M06] Customize.SelectCharacter -> TnPlayerController.SelectCharacter -> PRI._SelectedCharacter:
    // the frontend's selection becomes Gameplay's CharacterSelection (type, specialty, iconic chassis UniqueId).
    // Every pick is forwarded, also mid-match (Change Character): the original uses the new selection on the next
    // respawn ("Selected character used on respawn", UIText; SelectCharacter never suicides) - Gameplay applies it.
    if (flow.selectedCharacter().valid && me >= 0 && flow.selectionSerial() != selectionSentSerial_) {
        const frontend::GameFlow::SelectedCharacter& fc = flow.selectedCharacter();
        game::CharacterSelection cs;
        cs.type = fc.type;
        const std::string sp = fc.specialty;
        cs.specialty = sp == "Scientist" ? game::Specialty::Scientist : sp == "Scout" ? game::Specialty::Scout
                     : sp == "Soldier" ? game::Specialty::Soldier : game::Specialty::Leader;
        const int team = world_.match().players()[(size_t)me].team;
        cs.chassisId = fc.chassis[team == 1 ? 1 : 0].empty() ? fc.chassis[0] : fc.chassis[team == 1 ? 1 : 0];
        cs.customSlot = fc.name;
        fillFullSelection(cs, fc);
        world_.match().selectCharacter(me, cs);
        const bool repick = selectionSentSerial_ != 0;
        selectionSentSerial_ = flow.selectionSerial();
        const int f = world_.match().faction(me) == 1 ? 1 : 0;   // [integration M08] resolved faction (FFA: Decepticon)
        frontend::FlowTrace::emit("match.characterSelected", {{"name", fc.name}, {"type", std::to_string(cs.type)}, {"specialty", sp},
                                                            {"repick", frontend::FlowTrace::boolean(repick)},
                                                            {"faction", f == 1 ? "Decepticon" : "Autobot"},
                                                            {"chassis", game::resolveChassis(cs, f)},
                                                            {"body", fc.bodyAvailable[f] ? "available" : "MISSING"}});
        if (!fc.bodyAvailable[f])
            LOG_WARN("FRONTEND selection handoff: %s chassis %s has no body; the spawn will not be this character", fc.name.c_str(),
                     fc.chassis[f].c_str());
    }
    const game::Match& match = world_.match();
    matchClock_ += dt;
    auto teamOf = [&](int p) { return (p >= 0 && (size_t)p < match.players().size()) ? (match.players()[(size_t)p].team == 255 ? -1 : match.players()[(size_t)p].team) : -1; };
    auto posOf = [&](int p) {
        if (p == me) return world_.player().pawn().position();
        for (const game::MatchOpponent* o : world_.matchOpponents()) if (o->matchPlayer() == p) return o->position();
        return core::Vec3{0, 0, 0};
    };
    for (const game::MatchEvent& e : world_.matchEvents()) {
        switch (e.type) {
        case game::MatchEvent::Type::MatchStarted:
            // InProgress.BeginState -> SendUIEventToControllers(3). The match controller's UseInGameLobby keeps "Choose
            // Character" up until a character is chosen (Frontend 6fb19f8, CONFIRMED script); the first spawn sends 5.
            flow.onUIEvent((int)frontend::UIEvent::BeginGame);
            // TnGameTypeMessage switch 0: HUD GameAnnouncement with the mode name [RE A5, CONFIRMED].
            frontend_->hud().announce(frontend_->catalog().modeFriendlyName(match.settings().modeTag));
            frontend::FlowTrace::emit("match.started", {});
            break;
        case game::MatchEvent::Type::PlayerKilled:
            frontend::FlowTrace::emit("match.kill", {{"victim", std::to_string(e.player)}, {"killer", std::to_string(e.other)}, {"how", e.text}});
            {   // RUNTIME-EVENTS: kill (score already applied by Gameplay), team / player score, death
                const core::Vec3 dp = posOf(e.player);
                LOG_INFO("MATCH kill killer=%d victim=%d killer_team=%d victim_team=%d weapon=%s", e.other, e.player, teamOf(e.other),
                         teamOf(e.player), e.text.empty() ? "unknown" : e.text.c_str());
                if (e.other >= 0 && e.other != e.player) {
                    if (teamOf(e.other) >= 0) LOG_INFO("MATCH score team=%d score=%d", teamOf(e.other), match.teamScore(teamOf(e.other)));
                    else LOG_INFO("MATCH score team=-1 player=%d score=%d", e.other, match.players()[(size_t)e.other].score);
                }
                LOG_INFO("MATCH death player=%d pos=%.1f,%.1f,%.1f", e.player, dp.x, dp.y, dp.z);
                deathAt_[e.player] = matchClock_;
            }
            {   // HUD kill feed (TnDeathMessage -> _global.GameMessage). The damage type is not in Gameplay's event yet:
                // the base [TnDamageType] template is used [PARTIAL, Gameplay handoff].
                frontend::HudKill k;
                auto nameOf = [&](int p) {
                    if (p == me) return frontend_->flow().profile().playerName();   // the local identity
                    return p >= 0 && (size_t)p < match.players().size() ? match.players()[(size_t)p].name : std::string();
                };
                k.victim = nameOf(e.player); k.killer = nameOf(e.other);
                k.victimTeam = teamOf(e.player); k.killerTeam = teamOf(e.other);
                k.victimLocal = e.player == me; k.killerLocal = e.other == me;
                k.suicide = e.text == "suicide" || e.other == e.player || e.other < 0;
                k.environment = e.text == "environment";
                frontend_->hud().addKill(k, teamOf(me));
            }
            if (e.player == me) { localDeadForUi_ = true; spectatingUi_ = false; localDeadTime_ = 0.0f; }
            break;
        case game::MatchEvent::Type::PlayerSpawned:
            frontend::FlowTrace::emit("match.spawn", {{"player", std::to_string(e.player)}, {"start", e.text}});
            {   // RUNTIME-EVENTS: spawn (start = the PlayerStart Gameplay chose), respawn with the delay since death
                const core::Vec3 sp = (e.value >= 0 && (size_t)e.value < match.starts().size()) ? match.starts()[(size_t)e.value].pos : posOf(e.player);
                LOG_INFO("MATCH spawn player=%d team=%d start=%s pos=%.1f,%.1f,%.1f chassis=%s", e.player, teamOf(e.player), e.text.c_str(),
                         sp.x, sp.y, sp.z, match.players()[(size_t)e.player].chassis.c_str());
                auto d = deathAt_.find(e.player);
                if (d != deathAt_.end()) { LOG_INFO("MATCH respawn player=%d start=%s delay_s=%.2f", e.player, e.text.c_str(), matchClock_ - d->second); deathAt_.erase(d); }
            }
            if (e.player == me && renderer_) {
                // [integration M07] TnCharacterApplier on the spawned pawn: the selection's colours for the faction it
                // spawned as (Cust_Color_A / Cust_COLOR_B; black = the material's own paint, as in the preview). The
                // match pawn is draw owner 0. [integration 09a] EnergonColor is the CHARACTER's own, never the team's (RE b0d9b22
                // CONFIRMED: TnCharacterApplier.ExtractColors = CD.EnergonColor if set, else the robot mesh material default). The
                // selection carries no energon, so it is left unset (RGB 0): the renderer draws the chassis material's authored
                // EnergonColor (AssetTools energon_default 0.843 / 0.302 / 0.029 on every MP chassis). Replaces the M08 team tint.
                const game::MatchPlayer& mp = match.players()[(size_t)me];
                // The faction the body resolved for (TnGame.GetResolvedCharacterFaction: the team; FFA forces 1 = Decepticon) -
                // not the team, which FFA does not have (a Deathmatch Leader spawns Soundwave and takes the Decepticon paint).
                const int f = match.faction(me) == 1 ? 1 : 0;
                auto lin = [](int c) { float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
                render::CharacterColors cc;
                const game::CharacterColor* src[2] = {&mp.selection.primary[f], &mp.selection.secondary[f]};
                float* dst[2] = {cc.primary, cc.secondary};
                for (int k = 0; k < 2; ++k) { dst[k][0] = lin(src[k]->r); dst[k][1] = lin(src[k]->g); dst[k][2] = lin(src[k]->b); dst[k][3] = 1.0f; }
                renderer_->setDrawOwner(0);
                renderer_->setCharacterColors(cc);
                frontend::FlowTrace::emit("match.pawnBody", {{"chassis", mp.chassis}, {"faction", std::to_string(f)},
                                                             {"primary", std::to_string(src[0]->r) + "," + std::to_string(src[0]->g) + "," + std::to_string(src[0]->b)},
                                                             {"secondary", std::to_string(src[1]->r) + "," + std::to_string(src[1]->g) + "," + std::to_string(src[1]->b)},
                                                             {"energon", frontend::FlowTrace::num(cc.energon[0]) + "," + frontend::FlowTrace::num(cc.energon[1]) + "," + frontend::FlowTrace::num(cc.energon[2])},
                                                             {"weapon", world_.player().pawn().weapon().def ? world_.player().pawn().weapon().def->id : ""},
                                                             {"drawn", world_.localChassis()}});
            }
            if (e.player == me && localDeadForUi_) {
                // RestartPlayer leaves spectating -> UI event 5 (RE E7.4).
                flow.onUIEvent((int)frontend::UIEvent::Respawn);
                localDeadForUi_ = spectatingUi_ = false;
            } else if (e.player == me && flow.ui().state() == frontend::UIState::WaitingOnGameStart) {
                // First spawn after the character was chosen: the player leaves the waiting state -> OnRespawn (5)
                // -> InGame; WaitingOnGameStart.EndState closes the pre-game screen [CONFIRMED TnUIController;
                // the waiting-state sender is HIGH: PlayerWaitingSpectating / PlayerWaitingWatchingMatinee EndState].
                flow.onUIEvent((int)frontend::UIEvent::Respawn);
            }
            break;
        case game::MatchEvent::Type::MatchEnded:
            frontend::FlowTrace::emit("match.ended", {{"winner", std::to_string(e.value)}, {"reason", e.text}});
            {   // RUNTIME-EVENTS: end (Gameplay's EndGame reason / winner; -1 = tie)
                const char* reason = e.text == "Score" ? "score_limit" : (e.text.find("ime") != std::string::npos ? "time_limit" : "other");
                const std::string winner = e.value >= 0 ? std::to_string(e.value) : std::string("draw");
                LOG_INFO("MATCH end reason=%s winner=%s t=%d (gameplay reason %s)", reason, winner.c_str(), match.elapsedTime(), e.text.c_str());
            }
            flow.onUIEvent((int)frontend::UIEvent::EndGame);   // MatchOver: HUD hidden, EndGameStats (RE F4)
            break;
        case game::MatchEvent::Type::ReturnToLobby:
            frontend::FlowTrace::emit("match.return", {});
            flow.returnToGameLobby();                           // MatchOver + 15 s -> ReturnToGameLobby (RE F4 / F6)
            break;
        default: break;
        }
    }
    // RUNTIME-EVENTS: timer, once per change of GRI.RemainingTime while the match runs
    if (match.state() == game::Match::State::InProgress && match.remainingTime() != lastLoggedRemaining_) {
        lastLoggedRemaining_ = match.remainingTime();
        LOG_INFO("MATCH timer remaining_s=%d", lastLoggedRemaining_);
    }
    // Dead: after MinRespawnDelay 3.0 s the controller enters PlayerSpectating -> UI event 4 (RE E7.3, CONFIRMED).
    if (localDeadForUi_ && !spectatingUi_) {
        localDeadTime_ += dt;
        if (localDeadTime_ >= 3.0f) { flow.onUIEvent((int)frontend::UIEvent::Spectating); spectatingUi_ = true; }
    }
    // <CurrentGame:*> / <PlayerOwner:*> match values for the in-match movies (Gameplay authoritative).
    const game::HudGameState h = world_.hudState();
    frontend::MatchValues v;
    v.valid = h.matchActive;
    v.pending = h.matchState == (int)game::Match::State::PendingMatch;
    v.countingDown = v.pending || (h.matchState == (int)game::Match::State::InProgress && h.remainingTime > 0);
    v.countdown = v.pending ? h.countdown : h.remainingTime;
    v.goalScore = h.goalScore;
    v.teamScore[0] = h.teamScore[0]; v.teamScore[1] = h.teamScore[1];
    v.myTeam = h.myTeam;
    v.score = h.score; v.kills = h.kills; v.deaths = h.deaths;
    v.dead = !h.alive;
    v.timeToRespawn = h.timeToRespawn;
    v.gameOverMessage = h.result;
    fillGriObjective(h, v);
    for (size_t i = 0; i < match.players().size(); ++i) {
        const auto& mp = match.players()[i];
        frontend::MatchValues::Player p;
        p.name = mp.name; p.team = mp.team == 255 ? -1 : mp.team; p.score = mp.score; p.kills = mp.kills; p.deaths = mp.deaths;
        p.dead = !mp.alive; p.local = (int)i == me;
        v.players.push_back(p);
    }
    flow.setMatchValues(v);
    // HUD movie values (TnHUD data observers), Gameplay authoritative.
    frontend::HudFrame hf;
    hf.valid = h.matchActive;
    hf.alive = h.alive;
    hf.totalSegments = h.segmentCount;
    if (h.activeSegment >= h.segmentCount) { hf.fullSegments = h.segmentCount; hf.currentSegment = 1.0; }
    else {
        const auto& hp = world_.player().pawn().health();
        float bottom = h.activeSegment > 0 ? hp.segmentTop(h.activeSegment - 1) : 0.0f, top = hp.segmentTop(h.activeSegment);
        hf.fullSegments = h.activeSegment;
        hf.currentSegment = top > bottom ? std::max(0.0f, std::min(1.0f, (h.health - bottom) / (top - bottom))) : 0.0;
    }
    hf.overshield = h.normalizedOverShield;
    const auto& wpn = world_.player().pawn().weapon();
    hf.clip = h.clipAmmo; hf.clipCapacity = wpn.magSize; hf.reserve = h.reserveAmmo; hf.reserveCapacity = wpn.reserveMax;
    {   // Gameplay's TnHUD observer state: fine aim (EHudAimType) -> NotifyFineAimChanged. The weapon class name goes to
        // NotifyCurrentWeaponChanged and Hud_GFX itself picks the icon / crosshair / reticule / scope (RE 934ecde, CONFIRMED).
        // [integration M08c] Gameplay's accessor is the single source again (aa0dfd1 / bec41cd: "TnWeapon" + the active
        // WeaponDef::id, TnWeaponFlag1Hand / TnWeaponBomb while carrying). No Ion default.
        const auto& aim = world_.player().controller().hudAimState();
        hf.weapon = aim.weaponClass ? aim.weaponClass : "";
        hf.aimType = aim.aimType;
    }
    hf.vehicleForm = h.vehicleForm;
    hf.spectating = spectatingUi_;
    frontend_->hud().setFrame(hf);
}

void Application::driveLifecycleTest(float dt) {
    // TEST ONLY (WFC_LIFECYCLE): every 2.5 s of InProgress, alternately the local player kills the opponent (+1 player
    // and team score) and the opponent kills the local player (death -> spectating -> 5 s wave respawn). Waits while
    // either side is dead. Uses only Gameplay's match API; no rule is reimplemented here.
    if (lifecycleGoal_ <= 0 || world_.match().state() != game::Match::State::InProgress || world_.matchOpponents().empty()) return;
    if ((lifecycleT_ += dt) < 2.5f) return;
    game::MatchOpponent* opp = world_.matchOpponents()[0];
    const int me = world_.localMatchPlayer();
    if (world_.localPlayerDead() || !opp->spawned()) return;
    lifecycleT_ = 0.0f;
    const bool killOpponent = (lifecycleStep_++ % 2) == 0;
    if (killOpponent) world_.applyMatchDamage(opp->matchPlayer(), me, 100000.0f, false);
    else world_.applyMatchDamage(me, opp->matchPlayer(), 100000.0f, false);
    frontend::FlowTrace::emit("test.lifecycle.damage", {{"victim", killOpponent ? "opponent" : "local"}});
}

void Application::unloadMatch() {
    // Travel replaces the world. The match world and its audio voices are released by recreating them; the map's
    // render resources by Rendering's IRenderer::unloadMapRenderData (docs/RENDERER_CONTRACT.md) [integration].
    // GlCensus then releases what remains outside the map data (World uploadMesh meshes, effect textures); names the
    // renderer already freed are ignored by glDelete* and nothing is created in between.
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    world_.unloadMapAudio();   // no voice / instance / map cue / sample remains (Systems guarantee)
    {   // [integration] soak evidence: Systems audio state after the map audio is released (expect the frontend baseline)
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.unloaded", {{"level", as.level}, {"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                      {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
#endif
    world_.~World();
    new (&world_) game::World();
    frontend_->flow().setMatchValues(frontend::MatchValues{});   // no stale match values in the lobby / frontend
    LOG_INFO("MATCH cleanup");                                   // RUNTIME-EVENTS: the match world is gone
    renderer_->unloadMapRenderData();
    // One renderer for the whole session: unloadMapRenderData releases everything map-owned (map meshes / BSP /
    // decals, lightmaps, CLUT, map FX and movers, material programs, post / scene-copy targets, map textures and every
    // uploadMesh slot - Rendering M22, census-verified). Names the renderer created during the match and keeps on
    // purpose (lazy programs, preview / dynamic buffers, helper textures) must not be swept, so the census only
    // measures then. Default when the renderer releases the match's uploadTexture textures in unloadMapRenderData
    // (Rendering M28: setTexturePersistent / liveTextureCount, detected); older renderers leaked ~60-105 textures per
    // match that way, so they keep the M06 hard reset (census sweep + a new renderer). WFC_RECREATE_RENDERER=1 forces
    // the hard reset; WFC_PERSISTENT_RENDERER=1 forces the persistent path.
    static const bool recreate = std::getenv("WFC_RECREATE_RENDERER") != nullptr ||
                                 (!kRendererReleasesMatchTextures && std::getenv("WFC_PERSISTENT_RENDERER") == nullptr);
    if (recreate) {
        ui::GlCensus::Owned keep;
        if (presenter_) presenter_->ownedGl(keep);
        if (!std::getenv("WFC_NO_GL_RELEASE")) frontend::FlowTrace::emit("match.glRelease", {{"released", g_census.release(keep)}});
    }
    gameMode_ = game::GameMode();
#ifndef WFC_SYSTEMS_FRONTEND_AUDIO
    // Without the Systems lifecycle the device is recreated to drop the match's voices.
    delete audio_;
    audio_ = audio::createAudio();
#endif
    if (!recreate) {
        // Per-match presentation state a new renderer would start without: the HUD reticle (setReticle) is the match's.
        renderer_->setReticle(render::IRenderer::ReticleState{});
    }
    if (recreate) {
        delete renderer_;
        renderer_ = render::createGLRenderer();
        if (g_scene) g_scene->setRenderer(renderer_);
        applyGamma(renderer_, frontend_->flow().profile().getInt("GammaSetting"));   // the new renderer starts at its default
    }
    {   // owner split for the census: GL the UI presenter (GFx movies, HUD, fonts) holds right now
        ui::GlCensus::Owned ui;
        if (presenter_) presenter_->ownedGl(ui);
        frontend::FlowTrace::emit("match.glCensus", {{"live", ui::GlCensus::snapshot()}, {"renderer", recreate ? "recreated" : "persistent"},
                                                     {"uiTextures", std::to_string(ui.textures.size())}, {"uiBuffers", std::to_string(ui.buffers.size())}});
    }
    camera_ = render::Camera();
    clock_ = FixedStepClock(60.0);
    escWasDown_ = false;
    window_->setMouseCaptured(false);
    mouseCaptured_ = false;
    frontend::FlowTrace::emit("match.unloaded", {{"privateMB", processMemoryMB()}});
}

} // namespace core
