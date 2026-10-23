#include "ui/GfxPresenter.h"
#include "ui/MapLabel.h"
#include "platform/Clipboard.h"
#include "core/FrameProfile.h"
#include "core/Log.h"
#include "frontend/FlowTrace.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdlib>

namespace ui {

using gfx::avm1::Args;
using gfx::avm1::Value;

namespace {
Value toValue(gfx::avm1::VM& vm, const frontend::BridgeValue& b) {
    switch (b.kind) {
    case frontend::BridgeValue::Kind::Bool: return Value(b.b);
    case frontend::BridgeValue::Kind::Number: return Value(b.n);
    case frontend::BridgeValue::Kind::String: return Value(b.s);
    case frontend::BridgeValue::Kind::Array: {   // e.g. Account.GetAccountNames
        std::vector<Value> items;
        for (const frontend::BridgeValue& i : b.items) items.push_back(toValue(vm, i));
        return Value(vm.newArray(items));
    }
    default: return Value();
    }
}

// UiKey -> Flash key code, per the movies' KeyListener.getFriendlyKey (XBOX360 branch) [CONFIRMED AS2]:
// arrows navigate, 13 A, 27 B, 112 X, 113 Y, 114 Start, 115 Back, 33 LB, 34 RB, 36 LT, 35 RT, 116/117 stick clicks.
// That the engine sends exactly these codes for the pad buttons is HIGH (native GFx key mapping not traced).
const int kFlashCode[(int)platform::UiKey::Count] = {38, 40, 37, 39, 13, 27, 112, 113, 114, 115, 33, 34, 36, 35, 116, 117};
const char* const kPopupMovie = "UI_GFxShared_p.MessagePrompt_GFX_1";   // Default__TnUIController.MessageBoxUI
} // namespace

bool GfxPresenter::init() {
    if (!lib_.load(frontend::Catalog::defaultManifestRoot(), frontend::Catalog::defaultExtractedRoot())) return false;
    installGlyphLabels();
    // $version prefix = the SKU the movies branch on (HmUtility.Platform). The version digits are UNKNOWN.
    gfx::avm1::VM::defaultVersionString = rt_.platform() + " 8,0,0,0";
    return true;
}

gfx::Player* GfxPresenter::focusPlayer() {
    if (loading_) return &loading_->player();
    if (scoreboard_ && scoreboardShown_) return &scoreboard_->player();
    // a focused extra (the MessagePrompt / TextPrompt popups) takes the mouse as it takes the keys (deliverKeys): its
    // panel buttons are mouse-clickable on PC (mc_buttonMenuPC)
    for (Extra& e : extras_) if (e.focus && e.movie) return &e.movie->player();
    return movies_.empty() ? nullptr : &movies_.back().movie->player();
}

void GfxPresenter::setHud(bool open, bool visible) {
    if (!open) {
        if (hud_) { rt_.dataStores().forgetMovie(hud_->object()); shapesStale_ = true; }
        hud_.reset(); hudVisible_ = false;
        // TnHUD.Destroyed -> ScoreboardMovie.Close(false): the kept scoreboard instance is unloaded with the HUD.
        if (scoreboard_) { rt_.dataStores().forgetMovie(scoreboard_->object()); shapesStale_ = true; }
        scoreboard_.reset(); scoreboardShown_ = false;
        return;
    }
    if (!hud_) {
        {   // Match start: the respawn screen (opened on every death), the scoreboard and the results screen (a 58 ms open
            // at 32 v 32) and their imports enter the movie cache now, not on the frame they open.
            core::prof::Scope prof("gfx.preload");
            GfxMovie warm;
            for (const char* m : {"UI_GFxRespawn_p.MultiplayerRespawn_GFX_1", "UI_GFxInGameStats_p.InGameStats_GFX_1",
                                  "UI_GFxEndGameStats_p.EndGameStats_GFX_1"})
                warm.open(lib_, &rt_.catalog(), m, nullptr, nullptr);
        }
        hud_ = std::make_unique<GfxMovie>();
        bool ok = hud_->open(lib_, &rt_.catalog(), frontend::HudController::kMovie,
                             [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                             [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", frontend::HudController::kMovie}, {"opened", frontend::FlowTrace::boolean(ok)}});
        if (!ok) { hud_.reset(); return; }
    }
    hudVisible_ = visible;
}

// TnHUD.UpdatePlayerListDataCallback (CDO default, CONFIRMED), invoked with no arguments on every show. On the first
// show it finds nothing yet: InGameStats loads PlayerList.swf asynchronously (loadClip), and the list's own setInterval
// (1 s) refreshes it while the movie advances.
static const char* const kScoreboardRefresh = "_global.UpdatePlayerListData";

// The scoreboard as TnHUD drives it [RE: TnHUD.SetShowScores, CONFIRMED script; native Start / Close HIGH]: one
// InGameStats_GFX instance (TnHUD.ScoreboardMovie) for the HUD's lifetime, never started before the first show
// (SetupNewHud only Close(true)s it, so the first press of a match pays the load). Show = Start (the first one loads it)
// + SetFocus + SetVisibility(true) + Invoke(UpdatePlayerListDataCallback) to refresh the list; hide =
// Close(KeepLoaded = true): kept loaded but not advanced, drawn, focused or fed data-store updates until the next show.
void GfxPresenter::setScoreboard(bool open) {
    if (!open) {
        if (scoreboardShown_) frontend::FlowTrace::emit("gfx.scoreboard", {{"shown", "false"}});
        scoreboardShown_ = false;
        return;
    }
    if (scoreboardShown_) return;
    if (!scoreboard_) {
        scoreboard_ = std::make_unique<GfxMovie>();
        bool ok = scoreboard_->open(lib_, &rt_.catalog(), "UI_GFxInGameStats_p.InGameStats_GFX_1",
                                    [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                                    [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", "UI_GFxInGameStats_p.InGameStats_GFX_1"}, {"opened", frontend::FlowTrace::boolean(ok)}});
        if (!ok) { scoreboard_.reset(); return; }
    }
    scoreboardShown_ = true;
    scoreboard_->resetClock();   // resumes where it was closed (no catch-up of the closed time)
    scoreboard_->invoke(kScoreboardRefresh, {});
    frontend::FlowTrace::emit("gfx.scoreboard", {{"shown", "true"}});
}

// Keyboard prompts in the movies' Gamepad* glyph slots (PC ADAPTATION: the PC SKU's own movies are not in the dump; the
// text uses the original PC prompt style of TransGame_PC.int). Menus: the rebuild's UI keys (Enter = buttonA and
// Escape = buttonB are the shipped GFxUI key codes); the HUD: the gameplay keys, in the PC mapper's texts.
void GfxPresenter::setPadPrompts(bool pad) {
    padPrompts_ = pad;
    installGlyphLabels();
    ++lib_.promptGen;
}

void GfxPresenter::installGlyphLabels() {
    if (!lib_.glyphLabel)
        lib_.glyphLabel = [this](const std::string& object, const std::string& image) -> std::string {
            if (padPrompts_) return {};
            static const std::map<std::string, std::string> menu = {
                {"GamepadFaceButtonA", "ENTER"}, {"GamepadFaceButtonB", "ESC"}, {"GamepadFaceButtonX", "F1"},
                {"GamepadFaceButtonY", "F2"}, {"GamepadButtonStart", "F3"}, {"GamepadButtonBack", "TAB"},
                {"GamepadButtonLB", "PAGE UP"}, {"GamepadButtonRB", "PAGE DOWN"}, {"GamepadButtonLT", "HOME"},
                {"GamepadButtonRT", "END"}, {"GamepadButtonL3", "F5"}, {"GamepadButtonR3", "F6"},
                {"GamepadDpadUp", "UP"}, {"GamepadDpadDown", "DOWN"}, {"GamepadDpadLeft", "LEFT"}, {"GamepadDpadRight", "RIGHT"},
                {"GamepadLeftStick", "ARROW KEYS"}, {"GamepadRightStick", "MOUSE"}};
            static const std::map<std::string, std::string> hud = {
                {"GamepadFaceButtonA", "SPACE"}, {"GamepadFaceButtonB", "G"}, {"GamepadFaceButtonX", "R"},
                {"GamepadFaceButtonY", "MOUSE WHEEL"}, {"GamepadButtonStart", "ESC"}, {"GamepadButtonBack", "TAB"},
                {"GamepadButtonLB", "CTRL"}, {"GamepadButtonRB", "SHIFT"}, {"GamepadButtonLT", "RIGHT MOUSE BUTTON"},
                {"GamepadButtonRT", "LEFT MOUSE BUTTON"}, {"GamepadButtonL3", "F"}, {"GamepadButtonR3", "Q"},
                {"GamepadDpadUp", "MOUSE WHEEL"}, {"GamepadDpadDown", "B"}, {"GamepadDpadLeft", "B"}, {"GamepadDpadRight", "B"},
                {"GamepadLeftStick", "W, S, A, D"}, {"GamepadRightStick", "MOUSE"}};
            const bool isHud = object.find("Hud_GFX") != std::string::npos;
            const auto& t = isHud ? hud : menu;
            for (const auto& [k, v] : t) if (image.find(k) != std::string::npos) return v;
            return {};
        };
}

bool GfxPresenter::hudStageSize(double& w, double& h) const {
    if (!hud_) return false;
    w = hud_->player().stageViewW();
    h = hud_->player().stageViewH();
    return w > 0 && h > 0;
}

void GfxPresenter::hudCall(const std::string& fn, const std::vector<frontend::BridgeValue>& args) {
    if (!hud_) return;
    static const bool trace = std::getenv("WFC_HUDTRACE") != nullptr;   // DEV TOOL: log every HUD call with its args
    if (trace) {
        std::string line = fn + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            const frontend::BridgeValue& b = args[i];
            if (i) line += ", ";
            if (b.kind == frontend::BridgeValue::Kind::String) line += "'" + b.s + "'";
            else if (b.kind == frontend::BridgeValue::Kind::Bool) line += b.b ? "true" : "false";
            else { char n[32]; std::snprintf(n, sizeof n, "%g", b.n); line += n; }
        }
        LOG_INFO("HUDCALL %s)", line.c_str());
    }
    Args a;
    for (const frontend::BridgeValue& b : args) a.push_back(toValue(hud_->player().vm(), b));
    if (fn == "_global.GameMessage" && extendedMatch_) { extendedKillFeed(a); return; }
    hud_->invoke(fn, a);
}

namespace {
// WFC_UIPROF=1 (diagnostics): average CPU ms of the presenter's update and draw, logged every 300 frames.
struct UiProf {
    const char* name;
    double sum = 0; int n = 0;
    std::chrono::steady_clock::time_point t0;
    static bool on() { static const bool v = std::getenv("WFC_UIPROF") != nullptr; return v; }
    std::map<std::string, double> cats0;
    void begin() {
        if (!on()) return;
        t0 = std::chrono::steady_clock::now();
        if (core::prof::enabled()) cats0 = core::prof::frame();
    }
    void end() {
        if (!on()) return;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > 4.0) {   // one frame over budget, with what ran in it (WFC_FRAMEPROF categories)
            std::string cats;
            if (core::prof::enabled())
                for (const auto& [k, v] : core::prof::frame()) {
                    const double d = (v - (cats0.count(k) ? cats0[k] : 0.0)) * 1000.0;
                    if (d >= 0.5) { char b[64]; std::snprintf(b, sizeof b, " %s=%.1f", k.c_str(), d); cats += b; }
                }
            LOG_INFO("uiprof slow %s %.2f ms%s", name, ms, cats.c_str());
        }
        sum += ms;
        if (++n == 300) { LOG_INFO("uiprof %s avg %.3f ms (300 frames)", name, sum / n); sum = 0; n = 0; }
    }
};
UiProf g_uiUpdate{"update"}, g_uiDraw{"draw"};
// ... and the whole frame (draw to draw, every screen): average ms / fps, the worst frame and the 1 % low fps, per UI
// state, so menus and matches are measured the same way.
struct UiFrameProf {
    std::chrono::steady_clock::time_point last{};
    std::vector<double> ms;
    void tick(const char* state) {
        if (!UiProf::on()) return;
        const auto now = std::chrono::steady_clock::now();
        if (last.time_since_epoch().count() != 0) ms.push_back(std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
        if (ms.size() < 300) return;
        double sum = 0; for (double v : ms) sum += v;
        std::vector<double> s = ms; std::sort(s.begin(), s.end());
        const double p99 = s[s.size() * 99 / 100], worst = s.back(), avg = sum / (double)s.size();
        LOG_INFO("uiprof frame %s avg %.3f ms (%.0f fps) 1%%low %.0f fps worst %.2f ms", state, avg, 1000.0 / avg, 1000.0 / p99, worst);
        ms.clear();
    }
};
UiFrameProf g_uiFrame;
struct UiProfScope { UiProf& p; explicit UiProfScope(UiProf& q) : p(q) { p.begin(); } ~UiProfScope() { p.end(); } };
// Kill feed lines in extended matches (PC EXTENSION, user decision): one constant to tune.
constexpr int kExtendedFeedLines = 6;
gfx::MovieClip* findFeedManager(gfx::avm1::VM& vm, gfx::MovieClip* c, int depth = 0) {
    if (!c || depth > 10) return nullptr;
    if (c->script && vm.findOwner(c->script, "messageQueue") == c->script && vm.get(c->script, "messageQueue").isObject()) return c;
    for (auto& [d, ch] : c->children)
        if (ch->kind == gfx::DisplayObject::Kind::Clip && !ch->removed)
            if (gfx::MovieClip* r = findFeedManager(vm, static_cast<gfx::MovieClip*>(ch.get()), depth + 1)) return r;
    return nullptr;
}
}

// Hud_GFX mc_gameMessageManager (_global.GameMessage) [CONFIRMED script]: each line fades itself after 5 s; a new line
// is unshifted onto messageQueue, every line moves to y = i * -22 and lines past index 4 fadeOut() over 1 s - 5 lines
// plus the fading overflow. In extended matches (user decision, PC EXTENSION) up to kExtendedFeedLines stay, each with
// the same 5 s fade, and a line pushed past the last is removed at once: before the original function runs, the lines
// from index 4 on leave its queue (so it starts no overflow fade) and are kept in __wfcHeld, placed below it afterwards.
void GfxPresenter::extendedKillFeed(const Args& a) {
    gfx::Player& p = hud_->player();
    gfx::avm1::VM& vm = p.vm();
    // The manager clip by its last path (the HUD keeps it for the match); the tree search only when that fails.
    gfx::MovieClip* mgrClip = nullptr;
    if (!feedManagerPath_.empty())
        if (gfx::DisplayObject* d = p.resolveTarget(feedManagerPath_, p.root()); d && d->kind == gfx::DisplayObject::Kind::Clip && !d->removed && d->script)
            mgrClip = static_cast<gfx::MovieClip*>(d);
    if (!mgrClip) mgrClip = findFeedManager(vm, p.root());
    if (!mgrClip) { hud_->invoke("_global.GameMessage", a); return; }
    gfx::avm1::Object* mgr = mgrClip->script;
    feedManagerPath_ = mgrClip->targetPath();
    const auto alive = [&](const gfx::avm1::Value& v) { return v.isObject() && !vm.getV(v, "_name").isUndef(); };
    gfx::avm1::Value q = vm.get(mgr, "messageQueue");
    std::vector<gfx::avm1::Value> held;
    const int qn = (int)vm.toNumber(vm.getV(q, "length"));
    if (qn > 4) {
        gfx::avm1::Value moved = vm.callMethod(q, "splice", {gfx::avm1::Value(4)});
        const int mn = (int)vm.toNumber(vm.getV(moved, "length"));
        for (int i = 0; i < mn; ++i) { gfx::avm1::Value v = vm.getV(moved, std::to_string(i)); if (alive(v)) held.push_back(v); }
    }
    gfx::avm1::Value old = vm.get(mgr, "__wfcHeld");
    if (old.isObject()) {
        const int on = (int)vm.toNumber(vm.getV(old, "length"));
        for (int i = 0; i < on; ++i) { gfx::avm1::Value v = vm.getV(old, std::to_string(i)); if (alive(v)) held.push_back(v); }
    }
    // Every line onto its slot before the shift (a shift still running when the next kill arrives would start the
    // lines from uneven positions), and the line that entered last becomes visible.
    const auto snap = [&](const gfx::avm1::Value& line, int slot) {
        gfx::avm1::Object* to = vm.newPlain();
        vm.set(to, "_y", gfx::avm1::Value((double)(slot * -22)));
        vm.callMethod(line, "interp", {gfx::avm1::Value(0.001), gfx::avm1::Value("easeout"), gfx::avm1::Value(4), gfx::avm1::Value(to)});
        vm.setV(line, "_y", gfx::avm1::Value((double)(slot * -22)));
        vm.setV(line, "_visible", gfx::avm1::Value(true));
    };
    {
        const int qn2 = (int)vm.toNumber(vm.getV(q, "length"));
        for (int i = 0; i < qn2; ++i) { gfx::avm1::Value v = vm.getV(q, std::to_string(i)); if (alive(v)) snap(v, i); }
        for (size_t k = 0; k < held.size(); ++k) snap(held[k], 4 + (int)k);
    }
    hud_->invoke("_global.GameMessage", a);
    // The new line enters at slot 0 while the others move up over 0.2 s: it stays hidden until they have left its slot.
    {
        gfx::avm1::Value nl = vm.getV(vm.get(mgr, "messageQueue"), "0");
        if (alive(nl)) { vm.setV(nl, "_visible", gfx::avm1::Value(false)); vm.set(mgr, "__wfcPending", nl); feedRevealIn_ = 0.2f; }
        feedCheckFor_ = 1.5f;
    }
    // After the call the queue holds the new line and up to 4 older ones (indices 0..4); the held lines follow at 5...
    std::vector<gfx::avm1::Value> keep;
    for (size_t k = 0; k < held.size(); ++k) {
        const int idx = 5 + (int)k;
        if (idx < kExtendedFeedLines) {
            gfx::avm1::Object* to = vm.newPlain();
            vm.set(to, "_y", gfx::avm1::Value((double)(idx * -22)));
            vm.callMethod(held[k], "interp", {gfx::avm1::Value(0.2), gfx::avm1::Value("easeout"), gfx::avm1::Value(4), gfx::avm1::Value(to)});
            keep.push_back(held[k]);
        } else {
            vm.callMethod(held[k], "removeMovieClip", {});
        }
    }
    vm.set(mgr, "__wfcHeld", gfx::avm1::Value(vm.newArray(keep)));
    frontend::FlowTrace::emit("hud.killFeedExtended", {{"queue", std::to_string((int)vm.toNumber(vm.getV(vm.get(mgr, "messageQueue"), "length")))},
                                                    {"held", std::to_string(keep.size())}, {"removed", std::to_string(held.size() - keep.size())},
                                                    {"minGapSoFar", feedMinGapSeen_ > 1e8f ? std::string("-") : std::to_string(feedMinGapSeen_)}});
}

bool GfxPresenter::isUiControllerMovie(const std::string& movie) {
    // the UI controller's screens (UIController: spectating / pause / end game / in-game lobby / game start), any class
    for (const char* k : {"MultiplayerRespawn_GFX", "PauseMenu_GFX", "EndGameStats_GFX", "PreGameCountdown_GFX", "CustomTransformers_GFX",
                          "CharacterSelect_GFX", "GameLobby_GFX", "PartyLobby_GFX", "FrontEnd_GFX"})
        if (movie.find(k) != std::string::npos) return true;
    return false;
}

GfxMovie* GfxPresenter::openMovieNamed(const std::string& movie) {
    for (Open& o : movies_) if (o.object == movie) return o.movie.get();
    for (Extra& e : extras_) if (e.object == movie) return e.movie.get();
    return nullptr;
}

void GfxPresenter::movieCall(const std::string& movie, const std::string& fn, const std::vector<frontend::BridgeValue>& args) {
    // An open movie (the flow's or an extra) takes the call where it is. A flow / UI-controller movie is never opened here:
    // the flow may have opened it this frame (the respawn screen's SetSpectateTarget comes on the first Spectating frame,
    // before syncMovies presents it), so the call waits a few frames for it and is dropped if it never comes. Opening a
    // copy instead was the stuck respawn timer (user playtest 2026-10-23 #2): nothing ever closed the copy.
    if (openMovieNamed(movie)) { pendingCalls_.push_back({movie, fn, args, true}); return; }
    if (flowMovieNames_.count(movie) || isUiControllerMovie(movie)) { pendingCalls_.push_back({movie, fn, args, false, 4}); return; }
    bool open = false;
    if (!open) {
        Extra e;
        e.object = movie;
        e.movie = std::make_unique<GfxMovie>();
        bool ok = e.movie->open(lib_, &rt_.catalog(), movie,
                                [this](GfxMovie& mv, const std::string& f, Args& aa) { return bridge(mv, f, aa); },
                                [this](GfxMovie& mv, const std::string& c, const std::string& aa) { fsCommand(mv, c, aa); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", movie}, {"opened", frontend::FlowTrace::boolean(ok)}, {"by", "movieCall"}});
        if (!ok) return;
        extras_.push_back(std::move(e));
    }
    pendingCalls_.push_back({movie, fn, args, open});
}

bool GfxPresenter::clipWindowCenter(const std::string& path, int& x, int& y) {
    gfx::Player* p = focusPlayer();
    if (!p) return false;
    gfx::DisplayObject* d = p->resolveTarget(path, p->root());
    if (!d || d->removed) return false;
    gfx::Rect b = d->boundsIn(d->worldMatrix());   // stage twips
    gfx::Point c = GfxRendererGL::movieMatrix(*p, viewW_, viewH_)
                       .apply({(b.xmin + b.xmax) * 0.5f, (b.ymin + b.ymax) * 0.5f});
    x = (int)c.x; y = (int)c.y;
    return true;
}

void GfxPresenter::deliverMouse(const platform::InputFrame& in) {
    // Window pixels -> each movie's stage pixels (the inverse of the draw mapping; stages differ per movie).
    auto toStage = [&](gfx::Player& p, float& sx, float& sy) {
        gfx::Matrix inv = GfxRendererGL::movieMatrix(p, viewW_, viewH_).inverse();
        gfx::Point s = inv.apply({(float)in.mouseX, (float)in.mouseY});
        sx = s.x / 20.0f; sy = s.y / 20.0f;
    };
    bool inside = in.mouseX >= 0 && in.mouseY >= 0;
    gfx::Player* target = focusPlayer();
    if (mouseTarget_ && mouseTarget_ != target) {
        // Focus moved to another movie: the old one loses the pointer (roll-outs), never a stray release.
        for (Open& o : movies_) if (&o.movie->player() == mouseTarget_) o.movie->player().mouseLeave();
        mouseTarget_ = nullptr;
    }
    if (cursor_ && inside) { float x, y; toStage(cursor_->player(), x, y); cursor_->player().mouseMove(x, y); }
    if (!target) return;
    if (!inside) { target->mouseLeave(); prevMouseLeft_ = in.mouseLeft; return; }
    float x, y;
    toStage(*target, x, y);
    target->mouseMove(x, y);
    mouseTarget_ = target;
    if (in.mouseLeft != prevMouseLeft_) {
        gfx::MovieClip* b = target->hoverButton();
        frontend::FlowTrace::emit("gfx.mouse", {{"down", frontend::FlowTrace::boolean(in.mouseLeft)},
                                               {"x", frontend::FlowTrace::num(x)}, {"y", frontend::FlowTrace::num(y)},
                                               {"button", b ? b->targetPath() : std::string()}});
        target->mouseButton(in.mouseLeft);
    }
    prevMouseLeft_ = in.mouseLeft;
    if (in.mouseWheel != 0.0f) target->mouseWheel((int)(in.mouseWheel * 3.0f));
}

bool GfxPresenter::runsMovie(const std::string& movie) const { return !lib_.movieFileForObject(movie).empty(); }

GfxMovie* GfxPresenter::openMovie(const std::string& object) {
    auto m = std::make_unique<GfxMovie>();
    bool ok = m->open(lib_, &rt_.catalog(), object,
                      [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                      [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
    frontend::FlowTrace::emit("gfx.movie", {{"movie", object}, {"opened", frontend::FlowTrace::boolean(ok)}});
    if (!ok) return nullptr;
    movies_.push_back({object, std::move(m)});
    return movies_.back().movie.get();
}

Value GfxPresenter::bridge(GfxMovie& m, const std::string& fn, Args& a) {
    core::prof::Scope prof("gfx.bridge");
    // Cell reads (PlayerList: ~1000 per refresh) straight to the data stores: the arguments converted as below (undefined /
    // null -> ""), the same result as the generic path (FrontendRuntime::bridge -> DataStores::call).
    if (fn.size() > 25 && fn.compare(0, 25, "DataStores.ReadCollection") == 0 &&
        (fn == "DataStores.ReadCollectionValue" || fn == "DataStores.ReadCollectionBoolValue")) {
        gfx::avm1::VM& vm = m.player().vm();
        auto s = [&](size_t i) { return i < a.size() && !a[i].isNullish() ? vm.toString(a[i]) : std::string(); };
        return toValue(vm, rt_.dataStores().readCell(s(0), s(1), s(2), fn.size() == 34));
    }
    std::vector<std::string> sa;
    // An undefined / null argument reaches an UnrealScript string parameter as "" (not "undefined"): e.g. the
    // Customize.CommitCharacter weapon list of an unset slot.
    for (const Value& v : a) sa.push_back(v.isNullish() ? std::string() : m.player().vm().toString(v));
    if (fn == "HmObjectInterpolator.addInterp") return hudInterpAdd(m, a);
    if (fn == "Input.RegisterLeftStickCallback" && !sa.empty()) {
        for (auto& c : stickCallbacks_) if (c.movie == m.object() && c.path == sa[0]) return Value();
        stickCallbacks_.push_back({m.object(), sa[0]});
        frontend::FlowTrace::emit("gfx.stickCallback", {{"movie", m.object()}, {"path", sa[0]}, {"registered", "true"}});
        return Value();
    }
    if (fn == "Input.UnregisterLeftStickCallback") {
        // No argument: the movie's callback goes (HmPickablePalette.disableInput).
        for (size_t i = stickCallbacks_.size(); i-- > 0;)
            if (stickCallbacks_[i].movie == m.object()) stickCallbacks_.erase(stickCallbacks_.begin() + (long)i);
        frontend::FlowTrace::emit("gfx.stickCallback", {{"movie", m.object()}, {"registered", "false"}});
        return Value();
    }
    if (fn == "HmActionScript.setColor") { if (a.size() >= 2) setColorHex(m.player().vm(), a[0], a[1]); return Value(); }
    if (fn == "Self.Close" && m.object() == kPopupMovie) {
        // The message box movie closed itself (its own CloseMessagePrompt path).
        deferredErase_.push_back(m.object());
        rt_.flow().popupClosedByMovie();
        return Value();
    }
    if (fn == "Self.Close") {
        // TnUIController: the open UI closing itself (pause "Resume" etc.).
        m.closeRequested = true;
        if (rt_.flow().ui().openMovie() == m.object()) rt_.flow().uiClosedItself();
        return Value();
    }
    if (fn == "Self.SetExternalTextureWithPath" && sa.size() >= 2) {
        // ImagePath "UI_LevelThumbnails_p.MP_Streets" -> the extracted texture.
        std::string obj = sa[1];
        size_t dot = obj.find('.');
        std::string png = dot == std::string::npos ? "" : lib_.extractedRoot() + "/content/" + obj.substr(0, dot) + "/" + obj.substr(dot + 1) + ".png";
        m.setExternalTexture(sa[0], png);
        frontend::FlowTrace::emit("gfx.externalTexture", {{"movie", m.object()}, {"resource", sa[0]}, {"texture", obj}});
        return Value();
    }
    if (fn == "Self.OpenMovieWithPath" && !sa.empty()) {
        for (const Extra& e : extras_) if (e.object == sa[0]) return Value();
        Extra e;
        e.object = sa[0];
        e.focus = sa.size() > 2 && (sa[2] == "true" || sa[2] == "1");
        e.movie = std::make_unique<GfxMovie>();
        bool ok = e.movie->open(lib_, &rt_.catalog(), sa[0],
                                [this](GfxMovie& mv, const std::string& f, Args& aa) { return bridge(mv, f, aa); },
                                [this](GfxMovie& mv, const std::string& c, const std::string& aa) { fsCommand(mv, c, aa); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", sa[0]}, {"opened", frontend::FlowTrace::boolean(ok)}, {"by", m.object()}});
        if (ok) extras_.push_back(std::move(e));
        return Value();
    }
    if (fn == "Self.CloseMovieWithPath" && !sa.empty()) {
        for (size_t i = extras_.size(); i-- > 0;)
            if (extras_[i].object == sa[0]) { rt_.dataStores().forgetMovie(sa[0]); deferredErase_.push_back(sa[0]); }
        return Value();
    }
    if (fn.find("PostProcessChain") != std::string::npos) { rt_.bridge(m.object(), fn, sa); return Value(); }   // HUD screen effect state
    if (fn.rfind("Self.", 0) == 0) { frontend::FlowTrace::emit("bridge.unhandled", {{"fn", fn}, {"movie", m.object()}}); return Value(); }
    if (fn == "Online.CheckIsProfileReady") {
        // TnOnlineActionScriptBinding.CheckIsProfileReady [CONFIRMED script]: a ready profile -> OwnerMovie.Invoke(
        // ProfileIsReadyCallback = "ProfileIsReady", CheckId); otherwise the TnLoadProfileStatusMessageBox popup. The
        // offline rebuild's local profile is always ready. The menu opens Campaign (1) / Escalation (2) / Settings (3)
        // from that callback.
        rt_.bridge(m.object(), fn, sa);
        deferred_.push_back({m.object(), "_global.ProfileIsReady", {Value(sa.empty() ? 0.0 : std::atof(sa[0].c_str()))}});
        return Value();
    }
    return toValue(m.player().vm(), rt_.bridge(m.object(), fn, sa));
}

void GfxPresenter::syncPopup(frontend::GameFlow& flow) {
    const frontend::GameFlow::Popup& p = flow.popup();
    Extra* box = nullptr;
    for (Extra& e : extras_) if (e.object == kPopupMovie) box = &e;
    if (!p.open) {
        if (box && std::find(deferredErase_.begin(), deferredErase_.end(), box->object) == deferredErase_.end()) {
            rt_.dataStores().forgetMovie(box->object);
            deferredErase_.push_back(box->object);
        }
        return;
    }
    if (!box) {
        Extra e;
        e.object = kPopupMovie;
        e.focus = true;
        e.movie = std::make_unique<GfxMovie>();
        bool ok = e.movie->open(lib_, &rt_.catalog(), kPopupMovie,
                                [this](GfxMovie& mv, const std::string& f, Args& aa) { return bridge(mv, f, aa); },
                                [this](GfxMovie& mv, const std::string& c, const std::string& aa) { fsCommand(mv, c, aa); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", kPopupMovie}, {"opened", frontend::FlowTrace::boolean(ok)}, {"by", "popup"}});
        if (!ok) { flow.popupClosedByMovie(); return; }   // never leave a modal without a movie
        extras_.push_back(std::move(e));
        box = &extras_.back();
        popupSerial_ = 0;
    }
    if (popupSerial_ != p.serial) {
        popupSerial_ = p.serial;
        box->movie->invoke("_global.DisplayMessage", {Value(p.title), Value(p.message), Value(p.buttonString()), Value((double)p.icon)});
        frontend::FlowTrace::emit("gfx.invoke", {{"movie", kPopupMovie}, {"fn", "_global.DisplayMessage"}, {"buttons", p.buttonString()}});
    }
}

namespace {
size_t countDisplay(const gfx::DisplayObject* d) {
    if (!d || d->removed) return 0;
    size_t n = 1;
    if (d->kind == gfx::DisplayObject::Kind::Clip)
        for (const auto& [depth, ch] : static_cast<const gfx::MovieClip*>(d)->children) n += countDisplay(ch.get());
    return n;
}
}

std::vector<std::pair<std::string, std::string>> GfxPresenter::navReport() {
    std::vector<std::pair<std::string, std::string>> r;
    std::string movies, extras;
    size_t heap = 0, nodes = 0, grave = 0, timers = 0;
    auto add = [&](GfxMovie& m) {
        heap += m.player().vm().heapSize();
        timers += m.player().intervalCount();
        nodes += countDisplay(m.player().root());
        grave += m.player().graveyard.size();
    };
    for (Open& o : movies_) { movies += (movies.empty() ? "" : "+") + o.object; add(*o.movie); }
    int focusExtras = 0;
    for (Extra& e : extras_) { extras += (extras.empty() ? "" : "+") + e.object + (e.focus ? "*" : ""); add(*e.movie); focusExtras += e.focus; }
    GfxMovie* focus = scoreboard_ && scoreboardShown_ ? scoreboard_.get() : (movies_.empty() ? nullptr : movies_.back().movie.get());
    for (Extra& e : extras_) if (e.focus) focus = e.movie.get();
    std::string owner = "-", ownerVisible = "-";
    if (focus) {
        gfx::avm1::VM& vm = focus->player().vm();
        gfx::avm1::Value cm = vm.get(vm.global, "currentMenu");
        if (cm.isObject() && cm.o->display) {
            owner = cm.o->display->targetPath();
            ownerVisible = cm.o->display->removed ? "removed" : (cm.o->display->worldVisible() ? "1" : "0");
        } else owner = cm.isObject() ? "object" : "none";
    }
    r.push_back({"movies", movies.empty() ? "-" : movies});
    r.push_back({"asTimers", std::to_string(timers)});
    r.push_back({"stickCb", std::to_string(stickCallbacks_.size())});
    r.push_back({"extras", extras.empty() ? "-" : extras});
    r.push_back({"focus", focus ? focus->object() : "-"});
    r.push_back({"focusExtras", std::to_string(focusExtras)});
    r.push_back({"inputOwner", owner});
    r.push_back({"inputOwnerVisible", ownerVisible});
    r.push_back({"popup", rt_.flow().popup().open ? "open" : "closed"});
    r.push_back({"scoreboard", scoreboard_ && scoreboardShown_ ? "1" : "0"});
    r.push_back({"asHeap", std::to_string(heap)});
    r.push_back({"displayNodes", std::to_string(nodes)});
    r.push_back({"graveyard", std::to_string(grave)});
    r.push_back({"glShapes", std::to_string(gl_.cachedShapes())});
    return r;
}

namespace {
// The menus' _global.findInterpValue (AS2, every menu movie): linear, easein, easeout (curve < 2: quadratic-style; even
// / odd curves: the power form) and easeinout, with an overshoot term [CONFIRMED structure; exponent forms HIGH].
double interpValue(double b, double dest, double t0, double now, double t1, const std::string& anim, double curve, double s) {
    double t = now - t0, c = dest - b, d = t1 - t0;
    if (d <= 0) return dest;
    std::string a = anim;
    for (char& ch : a) ch = (char)std::tolower((unsigned char)ch);
    if (curve == 0 || a == "linear" || a.empty()) return c * t / d + b;
    if (a == "easein") { double u = t / d; return c * u * std::pow(u, curve - 1) * ((s + 1) * u - s) + b; }
    if (a == "easeout") {
        if (curve < 2) { double u = t / d; return -c * u * ((s + 1) * (u - 2) + s) + b; }
        double u = t / d - 1;
        if ((int)curve % 2 == 0) return c * (u * std::pow(u, curve - 1) * ((s + 1) * u + s) + 1) + b;
        return -c * (u * std::pow(u, curve - 1) * ((s + 1) * u + s) - 1) + b;
    }
    if (a == "easeinout") {
        if (t < d / 2) return interpValue(0, c, 0, t * 2, d, "easein", curve, s) * 0.5 + b;
        return interpValue(0, c, 0, t * 2 - d, d, "easeout", curve, s) * 0.5 + c * 0.5 + b;
    }
    return dest;
}
bool parseHexColor(gfx::avm1::VM& vm, const gfx::avm1::Value& v, float rgb[3]) {
    unsigned x = 0;
    if (v.isNumber()) x = (unsigned)vm.toNumber(v);
    else {
        std::string h = vm.toString(v);
        if (h.rfind("0x", 0) == 0) h = h.substr(2);
        if (h.empty()) return false;
        x = (unsigned)std::strtoul(h.c_str(), nullptr, 16);
    }
    rgb[0] = ((x >> 16) & 0xFF) / 255.0f; rgb[1] = ((x >> 8) & 0xFF) / 255.0f; rgb[2] = (x & 0xFF) / 255.0f;
    return true;
}
}

void GfxPresenter::setColorHex(gfx::avm1::VM& vm, const Value& target, const Value& color) {
    // MovieClip / TextField setColor: a ColorTransform with the colour as the multipliers, alpha = _alpha / 100.
    if (!target.isObject() || !target.o->display) return;
    float rgb[3];
    if (!parseHexColor(vm, color, rgb)) return;
    gfx::DisplayObject* d = target.o->display;
    d->cx.mr = rgb[0]; d->cx.mg = rgb[1]; d->cx.mb = rgb[2];
    d->cx.ar = d->cx.ag = d->cx.ab = 0;
}

Value GfxPresenter::hudInterpAdd(GfxMovie& m, Args& a) {
    core::prof::Scope prof("gfx.interpAdd");
    // HmObjectInterpolator.addInterp(target, interpTime, animType, interpCurve, overShoot, interpParams): one entry
    // per parameter; an entry for the same target + property replaces the running one (_global.addInterp).
    gfx::avm1::VM& vm = m.player().vm();
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Value::undef(); };
    Value target = arg(0), params = arg(5);
    if (!target.isObject() || !params.isObject()) return Value();
    double time = arg(1).isUndef() ? 1.0 : vm.toNumber(arg(1));
    std::string anim = arg(2).isNullish() ? std::string("linear") : vm.toString(arg(2));
    double curve = arg(3).isNullish() ? 0.0 : vm.toNumber(arg(3)), over = arg(4).isNullish() ? 0.0 : vm.toNumber(arg(4));
    Value listV = vm.get(vm.global, "__hmInterps");
    if (!listV.isObject()) { listV = Value(vm.newArray()); vm.global->setRaw("__hmInterps", listV, gfx::avm1::DontEnum); }
    std::vector<Value>& list = listV.o->elems;
    double now = m.player().timeMs();
    for (const std::string& prop : vm.enumerate(params.o)) {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& e) {
            return e.isObject() && vm.get(e.o, "targ").o == target.o && vm.toString(vm.get(e.o, "prop")) == prop;
        }), list.end());
        gfx::avm1::Object* e = vm.newPlain();
        e->setRaw("targ", target);
        e->setRaw("prop", Value(prop));
        e->setRaw("dest", vm.get(params.o, prop));
        e->setRaw("t0", Value(now));
        e->setRaw("t1", Value(now + time * 1000.0));
        e->setRaw("anim", Value(anim));
        e->setRaw("curve", Value(curve));
        e->setRaw("over", Value(over));
        if (prop == "color") {
            gfx::DisplayObject* d = target.o->display;
            gfx::avm1::Object* init = vm.newPlain();
            init->setRaw("r", Value(d ? (double)d->cx.mr : 1.0)); init->setRaw("g", Value(d ? (double)d->cx.mg : 1.0));
            init->setRaw("b", Value(d ? (double)d->cx.mb : 1.0));
            e->setRaw("init", Value(init));
        } else if (prop != "callback") {
            e->setRaw("init", vm.get(target.o, prop));
        }
        list.push_back(Value(e));
    }
    return Value();
}

void GfxPresenter::hudInterpUpdate(GfxMovie& m) {
    gfx::avm1::VM& vm = m.player().vm();
    Value listV = vm.get(vm.global, "__hmInterps");
    if (!listV.isObject()) return;
    double now = m.player().timeMs();
    std::vector<Value> list = listV.o->elems;   // copy: callbacks may add entries
    std::vector<Value> keep;
    std::vector<Value> callbacks;
    for (const Value& ev : list) {
        if (!ev.isObject()) continue;
        gfx::avm1::Object* e = ev.o;
        Value targ = vm.get(e, "targ");
        if (!targ.isObject() || (targ.o->display && targ.o->display->removed)) continue;   // the target went away
        std::string prop = vm.toString(vm.get(e, "prop"));
        double t1 = vm.toNumber(vm.get(e, "t1"));
        bool done = now >= t1;
        Value dest = vm.get(e, "dest");
        if (prop == "callback") {
            if (done) { if (dest.isObject()) callbacks.push_back(dest); }
            else keep.push_back(ev);
            continue;
        }
        double t0 = vm.toNumber(vm.get(e, "t0")), curve = vm.toNumber(vm.get(e, "curve")), over = vm.toNumber(vm.get(e, "over"));
        std::string anim = vm.toString(vm.get(e, "anim"));
        if (prop == "color") {
            float to[3];
            Value init = vm.get(e, "init");
            if (targ.o->display && init.isObject() && parseHexColor(vm, dest, to)) {
                const char* ch[3] = {"r", "g", "b"};
                float v[3];
                for (int i = 0; i < 3; ++i) {
                    double b = vm.toNumber(vm.get(init.o, ch[i]));
                    v[i] = (float)(done ? to[i] : interpValue(b, to[i], t0, now, t1, anim, curve, over));
                }
                targ.o->display->cx.mr = v[0]; targ.o->display->cx.mg = v[1]; targ.o->display->cx.mb = v[2];
            }
        } else {
            double b = vm.toNumber(vm.get(e, "init")), d = vm.toNumber(dest);
            vm.set(targ.o, prop, done ? dest : Value(interpValue(b, d, t0, now, t1, anim, curve, over)));
        }
        if (!done) keep.push_back(ev);
    }
    // Entries added by callbacks during this update are kept too.
    std::vector<Value>& live = listV.o->elems;
    for (size_t i = list.size(); i < live.size(); ++i) keep.push_back(live[i]);
    live = keep;
    for (const Value& cb : callbacks) {
        Args none;
        try { vm.call(cb, Value::undef(), none); } catch (const gfx::avm1::ScriptThrow&) {}
    }
    if (!callbacks.empty()) m.player().drainActions();
}

void GfxPresenter::fsCommand(GfxMovie& m, const std::string& cmd, const std::string& arg) {
    rt_.flow().fsCommand(m.object(), cmd, arg);
}

void GfxPresenter::syncMovies(frontend::GameFlow& flow) {
    // Copy: opening a movie runs its first frame, which may open / close movies through the flow (fscommands).
    const std::vector<std::string> want = flow.openMovies();
    for (const std::string& m : want) flowMovieNames_.insert(m);
    if (flow.level() != lastLevel_) {   // a new level: no extra copy of a flow / UI movie survives it
        lastLevel_ = flow.level();
        for (size_t i = extras_.size(); i-- > 0;)
            if (flowMovieNames_.count(extras_[i].object) || isUiControllerMovie(extras_[i].object)) {
                frontend::FlowTrace::emit("gfx.extraClosedByLevel", {{"movie", extras_[i].object}});
                rt_.dataStores().forgetMovie(extras_[i].object);
                extras_.erase(extras_.begin() + (long)i);
                shapesStale_ = true;
            }
    }
    // Close movies the flow no longer has open.
    for (size_t i = movies_.size(); i-- > 0;) {
        if (std::find(want.begin(), want.end(), movies_[i].object) == want.end()) {
            rt_.dataStores().forgetMovie(movies_[i].object);
            if (&movies_[i].movie->player() == graphicsPagePlayer_) { graphicsPagePlayer_ = nullptr; volumetricGateShown_ = -1; }
            if (&movies_[i].movie->player() == audioPagePlayer_) { audioPagePlayer_ = nullptr; micMeterShown_.clear(); }
            if (&movies_[i].movie->player() == firstRunAudioPending_) firstRunAudioPending_ = nullptr;
            if (&movies_[i].movie->player() == firstRunFooterHidden_) firstRunFooterHidden_ = nullptr;
            if (&movies_[i].movie->player() == gsPlayer_) { gsPlayer_ = nullptr; gsState_ = 0; gsMenu_ = nullptr; }
            frontend::FlowTrace::emit("gfx.movieClosed", {{"movie", movies_[i].object}});
            movies_.erase(movies_.begin() + (long)i);
            shapesStale_ = true;
        }
    }
    for (const std::string& o : want) {
        bool have = false;
        for (const Open& op : movies_) if (op.object == o) have = true;
        if (!have) openMovie(o);
    }
    // Loading movie (TnMoviePlayer: Bink + the GFx overlay named by SetLoadingMovieFilename).
    const frontend::LoadingScreen& L = flow.loading();
    if (L.active && !L.gfxMovie.empty()) {
        if (!loading_ || loadingUrl_ != L.url) {
            if (loading_) shapesStale_ = true;
            loading_ = std::make_unique<GfxMovie>();
            loadingUrl_ = L.url;
            loadingTime_ = 0.0f;
            bool ok = loading_->open(lib_, &rt_.catalog(), L.gfxMovie,
                                     [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                                     [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
            if (!ok) { loading_.reset(); return; }
            // EnqueueLoadingMapMovie / Enqueue*LobbyLoadingMovie: HmMoviePlayer deferred methods [CONFIRMED script].
            loading_->invoke("_global.SetLevelText", {Value(L.title), Value(L.message)});
            loadingMessageSent_ = L.message;
            for (const std::string& t : L.engageTexts) loading_->invoke("_global.AddEngageText", {Value(t)});
            frontend::FlowTrace::emit("gfx.loadingMovie", {{"movie", L.gfxMovie}, {"title", L.title}, {"message", L.message}});
        } else if (L.message != loadingMessageSent_) {   // online load gate: "Waiting for `n players"
            loading_->invoke("_global.SetLevelText", {Value(L.title), Value(L.message)});
            loadingMessageSent_ = L.message;
        }
    } else if (loading_) {
        loading_.reset();
        shapesStale_ = true;
        loadingUrl_.clear();
    }
}

namespace {
gfx::MovieClip* findClip(gfx::MovieClip* c, const std::string& name, int depth = 0) {
    if (!c || depth > 8) return nullptr;
    for (auto& [d, ch] : c->children) {
        if (ch->kind != gfx::DisplayObject::Kind::Clip || ch->removed) continue;
        auto* mc = static_cast<gfx::MovieClip*>(ch.get());
        if (mc->name == name) {   // the PlayerList's inner list: playerList_mc holding the team headers / entries
            for (auto& [d2, ch2] : mc->children) if (ch2->name.rfind("teamHeader", 0) == 0) return mc;
        }
        if (gfx::MovieClip* r = findClip(mc, name, depth + 1)) return r;
    }
    return nullptr;
}
}

namespace {
gfx::MovieClip* findNamed(gfx::MovieClip* c, const std::string& name, int depth = 0) {
    if (!c || depth > 8) return nullptr;
    for (auto& [d, ch] : c->children) {
        if (ch->kind != gfx::DisplayObject::Kind::Clip || ch->removed) continue;
        auto* mc = static_cast<gfx::MovieClip*>(ch.get());
        if (mc->name == name) return mc;
        if (gfx::MovieClip* r = findNamed(mc, name, depth + 1)) return r;
    }
    return nullptr;
}
}

// PC EXTENSION (extended matches): InGameStats_GFX (and EndGameStats_GFX's View Scores) lays the PlayerList out at a fixed step (team header 54, entry 25;
// PlayerList.swf) for the original 5 per team, with no scrolling (the list takes input only on consoles, for gamer
// cards). With up to 32 per team the list runs off the screen, so when it is taller than the space above the button
// hints, Up / Down and the mouse wheel scroll it and the rows outside that space are hidden. At the original counts it
// fits and nothing changes.
void GfxPresenter::scrollPlayerList(gfx::Player& p, float& scroll, const platform::InputFrame& in, float dt) {
    gfx::MovieClip* list = findClip(p.root(), "playerList_mc");
    if (!list || !list->script) return;
    gfx::avm1::VM& vm = p.vm();
    // The list's y as its timeline / script last set it (this function re-applies its offset every frame).
    const double cur = vm.toNumber(vm.get(list->script, "_y"));
    gfx::avm1::Value last = vm.get(list->script, "__wfcScrollSet");
    double base = cur;
    if (!last.isUndef() && std::fabs(vm.toNumber(last) - cur) < 0.01) base = vm.toNumber(vm.get(list->script, "__wfcScrollY"));
    vm.set(list->script, "__wfcScrollY", gfx::avm1::Value(base));
    // Stage space: the list top (unscrolled) and the bottom limit (the button hints, else the stage bottom).
    const gfx::Matrix pm = list->parent ? list->parent->worldMatrix() : gfx::Matrix{};
    const float sy = std::fabs(pm.d) > 1e-4f ? pm.d : 1.0f;
    const gfx::Matrix lm = list->worldMatrix();
    const float listScale = std::fabs(lm.d) > 1e-4f ? lm.d : 1.0f;
    const float top = pm.ty / 20.0f + (float)base * sy;   // world matrices are in twips
    float bottom = 720.0f - 70.0f;
    if (gfx::MovieClip* hints = findNamed(p.root(), "buttonHints_mc")) bottom = hints->worldMatrix().ty / 20.0f - 12.0f;
    float content = 0.0f;
    for (auto& [d, ch] : list->children) {
        if (ch->removed || !ch->script) continue;
        const bool header = ch->name.rfind("teamHeader", 0) == 0;
        if (!header && ch->name.rfind("playerEntry", 0) != 0 && ch->name.rfind("iconicEntry", 0) != 0) continue;
        const float h = header ? 54.0f : ch->name.rfind("iconicEntry", 0) == 0 ? 104.0f : 25.0f;
        content = std::max(content, ((float)vm.toNumber(vm.get(ch->script, "_y")) + h) * listScale);
    }
    const float room = bottom - top;
    const float maxScroll = std::max(0.0f, content - room);
    float step = 0.0f;
    if (in.uiIsDown(platform::UiKey::Down)) step += 1.0f;
    if (in.uiIsDown(platform::UiKey::Up)) step -= 1.0f;
    scroll += step * 420.0f * dt - in.mouseWheel * 25.0f * 3.0f * listScale;
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    const double y = base - scroll / sy;
    vm.set(list->script, "_y", gfx::avm1::Value(y));
    vm.set(list->script, "__wfcScrollSet", gfx::avm1::Value(vm.toNumber(vm.get(list->script, "_y"))));
    // Rows outside [top, bottom] are hidden (no mask clip in the original layout).
    for (auto& [d, ch] : list->children) {
        if (ch->removed || !ch->script) continue;
        const bool header = ch->name.rfind("teamHeader", 0) == 0;
        if (!header && ch->name.rfind("playerEntry", 0) != 0 && ch->name.rfind("iconicEntry", 0) != 0) continue;
        const float h = (header ? 54.0f : 25.0f) * listScale;
        const float wy = ch->worldMatrix().ty / 20.0f;
        const bool show = maxScroll <= 0.0f || (wy >= top - 1.0f && wy + h <= bottom + 1.0f);
        if (ch->visible != show) vm.set(ch->script, "_visible", gfx::avm1::Value(show));
    }
}

// PC ADAPTATION (user playtest 2026-10-23 #17): the PC pause menu (PauseMenu_GFX buildPauseMenu, WIN branch) builds its
// rows from escalationArray whenever GRI._bIconicMode is set - the same rows as multiplayerArray without "Choose
// Transformer" (and without Switch Teams) [CONFIRMED AS]. In the original only Escalation had iconic characters, claimed
// for the whole game; the PC's Iconic Characters option for versus inherited it, so a player stayed on the first pick
// for the whole match. In a versus match (free-for-all / team) the menu gets the original multiplayerArray instead, with
// Switch Teams by the original rule (not auto-balanced, not free-for-all); Choose Transformer's DisplayCharacterSelect
// lists the iconics with their claims (CharacterSelect.swf) and the pick applies at the next respawn as any other.
// Escalation / campaign keep the original menu. mc_subMenu rebuilds on a new buildArray (watch('buildArray')).
namespace {
gfx::avm1::Object* findScriptWith(gfx::avm1::VM& vm, gfx::MovieClip* c, const char* prop, int depth = 0) {
    if (!c || depth > 6) return nullptr;
    if (c->script && vm.get(c->script, prop).isObject()) return c->script;
    for (auto& [d, ch] : c->children) {
        if (ch->removed || ch->kind != gfx::DisplayObject::Kind::Clip) continue;
        if (gfx::avm1::Object* o = findScriptWith(vm, static_cast<gfx::MovieClip*>(ch.get()), prop, depth + 1)) return o;
    }
    return nullptr;
}
}
// PC ADAPTATION (user playtest 2026-10-23 #7): PlayerList.swf (the countdown roster, the Tab scoreboard, the end-of-match
// list) spaces player rows 25 px, but 104 px in iconic mode unless DisableIconicDisplay [CONFIRMED AS: BuildPlayerList] -
// the original's Escalation layout, a portrait per player for its 4 co-op players. With the PC Iconic Characters option in
// a versus match every row took 104 px and the list ran far down the screen. In versus the in-match lists get the
// original's own switch (PartyLobby_GFX sets playerList_mc.DisableIconicDisplay = true): the 25 px rows; the next 1 s
// UpdatePlayerListData relays them out. Escalation keeps the portrait rows.
void GfxPresenter::syncIconicPlayerList(gfx::Player& p, frontend::GameFlow& flow) {
    const frontend::MatchLaunch& m = flow.currentMatch();
    if (flow.level() != frontend::LevelKind::Match || !flow.iconicMode() || (m.gameTeamStatus != 1 && m.gameTeamStatus != 3)) return;
    gfx::avm1::VM& vm = p.vm();
    gfx::avm1::Object* list = findScriptWith(vm, p.root(), "BuildPlayerList");
    if (!list || vm.toBool(vm.get(list, "DisableIconicDisplay"))) return;
    vm.set(list, "DisableIconicDisplay", gfx::avm1::Value(true));
    frontend::FlowTrace::emit("playerList.iconicDisplay", {{"disabled", "true"}, {"why", "iconic versus: 25 px rows (PC ADAPTATION)"}});
}

void GfxPresenter::syncPauseChooseCharacter(gfx::Player& p, frontend::GameFlow& flow) {
    const frontend::MatchLaunch& m = flow.currentMatch();
    if (flow.level() != frontend::LevelKind::Match || !flow.iconicMode() || (m.gameTeamStatus != 1 && m.gameTeamStatus != 3)) return;
    gfx::MovieClip* menu = findNamed(p.root(), "mainMenu_mc");
    if (!menu || !menu->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    if (!vm.get(menu->script, "__wfcVersusRows").isUndef()) return;
    vm.set(menu->script, "__wfcVersusRows", V(true));
    const V cur = vm.get(menu->script, "buildArray");
    if (!cur.isObject() || cur.o->kind != gfx::avm1::ObjKind::Array) return;
    for (const V& e : cur.o->elems)
        if (e.isObject() && vm.toString(vm.get(e.o, "ID")) == "ChooseChar") return;   // already the multiplayer rows
    gfx::avm1::Object* holder = findScriptWith(vm, p.root(), "multiplayerArray");
    const V mp = holder ? vm.get(holder, "multiplayerArray") : V();
    if (!mp.isObject() || mp.o->kind != gfx::avm1::ObjKind::Array) return;
    std::vector<V> rows = mp.o->elems;
    const bool swap = !vm.toBool(vm.get(holder, "AutoBalance")) && vm.toString(vm.get(holder, "GameTeamStatus")) != "GTS_FreeForAllGame";
    if (swap && rows.size() > 1) {
        gfx::avm1::Object* it = vm.newPlain();
        vm.set(it, "panelHeight", V(29.0));
        vm.set(it, "panelWidth", V(413.0));
        vm.set(it, "clickFunction", vm.get(holder, "swapTeamsClickFunction"));
        vm.set(it, "ID", V(std::string("Swap")));
        vm.set(it, "text", V(std::string("$UIText.Lobby.SwitchTeams")));
        rows.insert(rows.begin() + 1, V(it));
    }
    vm.set(menu->script, "buildArray", V(vm.newArray(rows)));
    frontend::FlowTrace::emit("pause.versusRows", {{"rows", std::to_string(rows.size())}, {"swapTeams", frontend::FlowTrace::boolean(swap)},
                                                   {"why", "iconic versus: multiplayerArray (PC ADAPTATION)"}, {"mode", m.modeTag}});
}

// PC ADAPTATION (user playtest 2026-10-23 #6): SettingsMenu.swf's sub-menu stacks its rows upward from Commit Changes
// at a fixed 31 px step with no scrolling (the original Graphics page had 6 rows). With the PC rows the Graphics page has
// 15 and reached the top of the screen. A sub-menu with more than kSettingsRows rows now shows a window of that many: the
// window follows the focused row (keys / pad) and the mouse wheel, the rows outside it are hidden, and the window sits
// where the bottom rows always were. Pages with kSettingsRows rows or fewer are untouched.
namespace { constexpr int kSettingsRows = 10; constexpr double kSettingsStep = 31.0; }
void GfxPresenter::scrollSettingsSubMenu(gfx::Player& p, const platform::InputFrame& in) {
    gfx::MovieClip* menu = findNamed(p.root(), "subMenu_mc");
    if (!menu || !menu->script) return;
    std::vector<gfx::MovieClip*> rows;
    for (int i = 0;; ++i) {
        gfx::MovieClip* r = nullptr;
        const std::string want = "listItem" + std::to_string(i) + "_mc";
        for (auto& [d, ch] : menu->children)
            if (!ch->removed && ch->kind == gfx::DisplayObject::Kind::Clip && ch->name == want) { r = static_cast<gfx::MovieClip*>(ch.get()); break; }
        if (!r || !r->script) break;
        rows.push_back(r);
    }
    const int n = (int)rows.size();
    if (n <= kSettingsRows) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    // The focused row (the menu class' _currentFocus); -1 = none of the rows (e.g. the mouse is elsewhere)
    int focus = -1;
    const V cf = vm.get(menu->script, "_currentFocus");
    for (int i = 0; i < n && cf.isObject(); ++i) if (cf.o == rows[(size_t)i]->script) focus = i;
    const V topV = vm.get(menu->script, "__wfcTop"), lastV = vm.get(menu->script, "__wfcFocus");
    int top = topV.isUndef() ? n - kSettingsRows : (int)vm.toNumber(topV);
    const int last = lastV.isUndef() ? -2 : (int)vm.toNumber(lastV);
    if (focus >= 0 && focus != last) {   // a new focus: bring it into the window
        if (focus < top) top = focus;
        if (focus > top + kSettingsRows - 1) top = focus - kSettingsRows + 1;
    }
    if (in.mouseWheel != 0) top -= in.mouseWheel > 0 ? 1 : -1;
    top = std::clamp(top, 0, n - kSettingsRows);
    vm.set(menu->script, "__wfcTop", V((double)top));
    vm.set(menu->script, "__wfcFocus", V((double)focus));
    // Row j of the window [top, top + kSettingsRows) takes the slot of row j + (n - kSettingsRows - top): the window's
    // last row sits where Commit Changes (the last row) was authored.
    const double shift = (double)(n - kSettingsRows - top) * kSettingsStep;
    for (int j = 0; j < n; ++j) {
        gfx::avm1::Object* o = rows[(size_t)j]->script;
        const double cur = vm.toNumber(vm.get(o, "_y"));
        const V set = vm.get(o, "__wfcSetY");
        double base = cur;   // the authored y, unless this function placed the row (a script move resets the base)
        if (!set.isUndef() && std::fabs(vm.toNumber(set) - cur) < 0.01) base = vm.toNumber(vm.get(o, "__wfcBaseY"));
        vm.set(o, "__wfcBaseY", V(base));
        const double y = base + shift;
        if (std::fabs(y - cur) > 0.01) vm.set(o, "_y", V(y));
        vm.set(o, "__wfcSetY", V(vm.toNumber(vm.get(o, "_y"))));
        const bool show = j >= top && j < top + kSettingsRows;
        if (rows[(size_t)j]->visible != show) vm.set(o, "_visible", V(show));
    }
}

void GfxPresenter::checkKillFeed(float dt) {
    if (!hud_ || feedManagerPath_.empty()) return;
    // Only while something can change: the 0.2 s reveal of a new line, then the shift into the slots (invisible work
    // otherwise: path resolution and AS property reads every frame).
    if (feedRevealIn_ <= 0.0f && feedCheckFor_ <= 0.0f) return;
    feedCheckFor_ -= dt;
    gfx::Player& p = hud_->player();
    gfx::avm1::VM& vm = p.vm();
    gfx::DisplayObject* d = p.resolveTarget(feedManagerPath_, p.root());
    if (!d || !d->script) return;
    gfx::avm1::Object* mgr = d->script;
    const auto alive = [&](const gfx::avm1::Value& v) { return v.isObject() && !vm.getV(v, "_name").isUndef(); };
    if (feedRevealIn_ > 0.0f && (feedRevealIn_ -= dt) <= 0.0f) {
        gfx::avm1::Value nl = vm.get(mgr, "__wfcPending");
        if (alive(nl)) vm.setV(nl, "_visible", gfx::avm1::Value(true));
    }
    // Assertion: visible feed lines keep the 22 px step (logged once per new minimum).
    std::vector<double> ys;
    for (const char* list : {"messageQueue", "__wfcHeld"}) {
        gfx::avm1::Value arr = vm.get(mgr, list);
        if (!arr.isObject()) continue;
        const int n = (int)vm.toNumber(vm.getV(arr, "length"));
        for (int i = 0; i < n; ++i) {
            gfx::avm1::Value v = vm.getV(arr, std::to_string(i));
            if (alive(v) && vm.toBool(vm.getV(v, "_visible")) && vm.toNumber(vm.getV(v, "_alpha")) > 1.0) ys.push_back(vm.toNumber(vm.getV(v, "_y")));
        }
    }
    std::sort(ys.begin(), ys.end());
    static const bool feedTrace = std::getenv("WFC_FEEDTRACE") != nullptr;   // TEST ONLY: frames while lines are between slots
    if (feedTrace) {
        static size_t lastCount = 0;
        if (ys.size() != lastCount) { lastCount = ys.size(); frontend::FlowTrace::emit("test.killFeedVisible", {{"lines", std::to_string(ys.size())}}); }
        bool moving = false;
        for (double y : ys) moving = moving || std::fabs(y / 22.0 - std::round(y / 22.0)) > 0.02;
        if (moving) { std::string l; for (double y : ys) l += std::to_string((int)std::lround(y)) + " "; frontend::FlowTrace::emit("test.killFeedShift", {{"y", l}}); }
    }
    for (size_t i = 1; i < ys.size(); ++i) {
        const float gap = (float)(ys[i] - ys[i - 1]);
        feedMinGapSeen_ = std::min(feedMinGapSeen_, gap);
        if (gap < 21.5f && gap < feedMinGapLogged_) {
            feedMinGapLogged_ = gap;
            frontend::FlowTrace::emit("hud.killFeedOverlap", {{"gap", std::to_string(gap)}, {"lines", std::to_string(ys.size())}});
        }
    }
}

// DEV TOOL (QA bot overlay): one HUD kill-feed line clip (mc_gameMessage_default: the HUD font and shadow) per label in
// a container on the HUD root, placed at the label's window pixel; its own 5 s fade is disabled. Removed when empty.
void GfxPresenter::syncWorldLabels() {
    if (!hud_) return;
    gfx::Player& p = hud_->player();
    gfx::avm1::VM& vm = p.vm();
    gfx::MovieClip* root = p.root();
    if (!root || !root->script) return;
    if (worldLabels_.empty() && !labelsShown_) return;   // nothing shown, nothing to remove
    gfx::avm1::Value box = vm.get(root->script, "__qaLabels_mc");
    if (worldLabels_.empty()) {
        if (box.isObject()) vm.callMethod(box, "removeMovieClip", {});
        labelsShown_ = false;
        return;
    }
    labelsShown_ = true;
    if (!box.isObject()) box = vm.callMethod(gfx::avm1::Value(root->script), "createEmptyMovieClip", {gfx::avm1::Value("__qaLabels_mc"), gfx::avm1::Value(90000)});
    if (!box.isObject()) return;
    const gfx::Matrix inv = GfxRendererGL::movieMatrix(p, viewW_, viewH_).inverse();
    // Team colours of the kill feed (Autobots blue, Decepticons red; none / FFA yellow) and a distance fade (nearest
    // 100 %, farthest kept 40 %).
    float dmin = 1e30f, dmax = 0.0f;
    for (const auto& l : worldLabels_) { dmin = std::min(dmin, l.depth); dmax = std::max(dmax, l.depth); }
    {   // diagnostics: the team values the labels carry (once, when the overlay first has labels)
        static bool logged = false;
        if (!logged && !worldLabels_.empty()) {
            logged = true;
            int t0 = 0, t1 = 0, tn = 0;
            for (const auto& l : worldLabels_) (l.team == 0 ? t0 : l.team == 1 ? t1 : tn)++;
            frontend::FlowTrace::emit("playtest.botOverlayTeams", {{"team0", std::to_string(t0)}, {"team1", std::to_string(t1)}, {"none", std::to_string(tn)}});
        }
    }
    // Labels are nearest-first: each takes its projected point, moved down a line while its (estimated) text box would
    // overlap an already placed label, so clustered bots stay readable (stage units: ~6 per character, 13 per line).
    struct PlacedBox { double x0, y0, x1, y1; };
    std::vector<PlacedBox> placed;
    auto placeLabel = [&](double x, double y, size_t chars) {
        const double w = 6.0 * (double)chars, h = 13.0;
        for (int step = 0; step < 16; ++step) {
            bool hit = false;
            for (const PlacedBox& b : placed) if (x < b.x1 && b.x0 < x + w && y < b.y1 && b.y0 < y + h) { hit = true; break; }
            if (!hit) break;
            y += h;
        }
        placed.push_back({x, y, x + w, y + h});
        return y;
    };
    size_t i = 0;
    for (; i < worldLabels_.size(); ++i) {
        const std::string name = "l" + std::to_string(i);
        gfx::avm1::Value c = vm.getV(box, name);
        if (!c.isObject()) {
            c = vm.callMethod(box, "attachMovie", {gfx::avm1::Value("mc_gameMessage_default"), gfx::avm1::Value(name), gfx::avm1::Value((int)i + 1)});
            if (!c.isObject()) return;
            vm.setV(c, "fadeOut", gfx::avm1::Value(vm.newFunction([](gfx::avm1::VM&, const gfx::avm1::Value&, gfx::avm1::Args&) { return gfx::avm1::Value(); }, "fadeOut")));
        }
        // Gameplay's ask: the full state line within 30 m (depth = view distance, metres), the bot's name only beyond it
        // (the text before " ["), so a far cluster stays readable.
        std::string shown = worldLabels_[i].text;
        if (worldLabels_[i].depth > 30.0f) {
            const size_t cut = shown.find(" [");
            if (cut != std::string::npos) shown.resize(cut);
        }
        const gfx::Point s = inv.apply({worldLabels_[i].x, worldLabels_[i].y});
        vm.setV(c, "_x", gfx::avm1::Value((double)(s.x / 20.0f)));
        vm.setV(c, "_y", gfx::avm1::Value(placeLabel(s.x / 20.0, s.y / 20.0, shown.size())));
        const float span = dmax - dmin;
        const double alpha = span > 1e-3f ? 100.0 - 60.0 * (worldLabels_[i].depth - dmin) / span : 100.0;
        vm.setV(c, "_alpha", gfx::avm1::Value(alpha));
        const int team = worldLabels_[i].team;
        const char* colour = team == 0 ? "#50B5D5" : team == 1 ? "#F03C3C" : "#FFFF66";
        gfx::avm1::Value txt = vm.getV(c, "message_txt");
        if (txt.isObject()) vm.setV(txt, "htmlText", gfx::avm1::Value(std::string("<font color='") + colour + "'>" + shown + "</font>"));
    }
    for (;; ++i) {   // labels that went away
        gfx::avm1::Value c = vm.getV(box, "l" + std::to_string(i));
        if (!c.isObject()) break;
        vm.callMethod(c, "removeMovieClip", {});
    }
    // Smart AI overlay geometry (playtest): a thin team-coloured line from each label to its target and a marker at its
    // action point - hold cover: filled square; to cover: square outline + line; hunt: "?" ring + dashed line; retreat:
    // arrow. Drawn with the movie's drawing API in one clip, cleared and redrawn every frame (screen-space, stage units).
    {
        gfx::avm1::Value g = vm.getV(box, "geom_mc");
        if (!g.isObject()) g = vm.callMethod(box, "createEmptyMovieClip", {gfx::avm1::Value("geom_mc"), gfx::avm1::Value(0)});
        if (g.isObject()) {
            using V = gfx::avm1::Value;
            vm.callMethod(g, "clear", {});
            auto toStage = [&](float x, float y, double& ox, double& oy) {
                const gfx::Point s = inv.apply({x, y}); ox = s.x / 20.0; oy = s.y / 20.0;
            };
            for (const auto& l : worldLabels_) {
                const double colour = l.team == 0 ? 0x50B5D5 : l.team == 1 ? 0xF03C3C : 0xFFFF66;
                double lx, ly; toStage(l.x, l.y, lx, ly);
                if (l.hasTarget) {
                    double tx, ty; toStage(l.tx, l.ty, tx, ty);
                    vm.callMethod(g, "lineStyle", {V(1.0), V(colour), V(70.0)});
                    vm.callMethod(g, "moveTo", {V(lx), V(ly)});
                    vm.callMethod(g, "lineTo", {V(tx), V(ty)});
                }
                if (l.action > 0) {
                    double ax, ay; toStage(l.ax, l.ay, ax, ay);
                    const double r = 6.0;
                    if (l.action == 4) {   // holding cover: filled square
                        vm.callMethod(g, "lineStyle", {V(1.0), V(colour), V(100.0)});
                        vm.callMethod(g, "beginFill", {V(colour), V(60.0)});
                        vm.callMethod(g, "moveTo", {V(ax - r), V(ay - r)});
                        vm.callMethod(g, "lineTo", {V(ax + r), V(ay - r)}); vm.callMethod(g, "lineTo", {V(ax + r), V(ay + r)});
                        vm.callMethod(g, "lineTo", {V(ax - r), V(ay + r)}); vm.callMethod(g, "lineTo", {V(ax - r), V(ay - r)});
                        vm.callMethod(g, "endFill", {});
                    } else if (l.action == 3) {   // moving to cover: square outline + the path line
                        vm.callMethod(g, "lineStyle", {V(1.0), V(colour), V(90.0)});
                        vm.callMethod(g, "moveTo", {V(lx), V(ly)}); vm.callMethod(g, "lineTo", {V(ax), V(ay)});
                        vm.callMethod(g, "moveTo", {V(ax - r), V(ay - r)});
                        vm.callMethod(g, "lineTo", {V(ax + r), V(ay - r)}); vm.callMethod(g, "lineTo", {V(ax + r), V(ay + r)});
                        vm.callMethod(g, "lineTo", {V(ax - r), V(ay + r)}); vm.callMethod(g, "lineTo", {V(ax - r), V(ay - r)});
                    } else if (l.action == 1) {   // hunt: dashed line to the search point, diamond there
                        vm.callMethod(g, "lineStyle", {V(1.0), V(colour), V(80.0)});
                        const double dx = ax - lx, dy = ay - ly, len = std::sqrt(dx * dx + dy * dy);
                        for (double t = 0; t < len; t += 12.0) {
                            const double t1 = std::min(len, t + 6.0);
                            vm.callMethod(g, "moveTo", {V(lx + dx * t / len), V(ly + dy * t / len)});
                            vm.callMethod(g, "lineTo", {V(lx + dx * t1 / len), V(ly + dy * t1 / len)});
                        }
                        vm.callMethod(g, "moveTo", {V(ax), V(ay - r)});
                        vm.callMethod(g, "lineTo", {V(ax + r), V(ay)}); vm.callMethod(g, "lineTo", {V(ax), V(ay + r)});
                        vm.callMethod(g, "lineTo", {V(ax - r), V(ay)}); vm.callMethod(g, "lineTo", {V(ax), V(ay - r)});
                    } else if (l.action == 2) {   // retreat: arrow from the bot to the retreat point
                        vm.callMethod(g, "lineStyle", {V(2.0), V(colour), V(90.0)});
                        vm.callMethod(g, "moveTo", {V(lx), V(ly)}); vm.callMethod(g, "lineTo", {V(ax), V(ay)});
                        const double dx = ax - lx, dy = ay - ly, len = std::max(1e-3, std::sqrt(dx * dx + dy * dy));
                        const double ux = dx / len, uy = dy / len;
                        vm.callMethod(g, "moveTo", {V(ax), V(ay)}); vm.callMethod(g, "lineTo", {V(ax - ux * 9 + uy * 5), V(ay - uy * 9 - ux * 5)});
                        vm.callMethod(g, "moveTo", {V(ax), V(ay)}); vm.callMethod(g, "lineTo", {V(ax - ux * 9 - uy * 5), V(ay - uy * 9 + ux * 5)});
                    }
                }
            }
        }
    }
}

void GfxPresenter::deliverKeys(const platform::InputFrame& in) {
    uint32_t now = in.uiDown, changed = now ^ prevUi_;
    prevUi_ = now;
    const bool sb = scoreboard_ && scoreboardShown_;
    if (loading_ || (movies_.empty() && !sb)) return;
    GfxMovie* focus = sb ? scoreboard_.get() : movies_.back().movie.get();
    for (Extra& e : extras_) if (e.focus) focus = e.movie.get();
    // Text entry (PC: TextPrompt_GFX input field for account names / character renaming): typed characters and the
    // editing keys without a UI action (Backspace, Delete) go to the focused movie; arrows / Home / End / Enter / Escape
    // arrive through their UI actions below and edit the field in Player::keyEvent.
    if (focus->hasTextFocus()) {
        for (char32_t c : in.text) focus->textInput(c);
        for (uint16_t vk : in.keyPresses)
            if (vk == 8 || vk == 46) { focus->key(vk, true); focus->key(vk, false); }
        if (!in.text.empty()) frontend::FlowTrace::emit("gfx.text", {{"movie", focus->object()}, {"chars", std::to_string(in.text.size())}});
    }
    if (!changed) return;
    for (int k = 0; k < (int)platform::UiKey::Count; ++k) {
        if (!(changed & (1u << k))) continue;
        bool down = now & (1u << k);
        frontend::FlowTrace::emit("gfx.key", {{"movie", focus->object()}, {"code", std::to_string(kFlashCode[k])},
                                             {"down", frontend::FlowTrace::boolean(down)}});
        focus->key(kFlashCode[k], down);
    }
}

namespace {
// PC ADAPTATION: SettingsMenu_GFX's Graphics button rebuilds graphicsMenuArray (Brightness, Resolution, Fullscreen,
// Texture Quality, VSync, Apply; writeGraphicsSettings reads listItem1..4 by index) and passes it straight to
// attachMovie('mc_subMenu', ..., {buildArray: graphicsMenuArray}). A Frame Rate Limit item is inserted before Apply in
// that init object (Player::attachHook), in the menu's own data-store form ({choiceArray, dataStore, hintText,
// panelWidth, text}): the lateral selector reads its value with DataStores.ReadValue and each step writes it and calls
// Game.ApplyProfileSettings (applied live). Not an original setting (the Xenon game ran 15-30 fps smoothed).
constexpr int kFrameLimitChoices[] = {30, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 280, 300, 360, 480, 500, 1000};
// Volumetric Lighting choices: Off / On on Realistic; otherwise the single "Realistic only" value carrying the stored choice.
void volumetricChoices(gfx::avm1::VM& vm, std::vector<gfx::avm1::Value>& out, bool realistic, bool stored) {
    out.clear();
    auto add = [&](int v, const char* label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", gfx::avm1::Value((double)v));
        vm.set(c, "FriendlyName", gfx::avm1::Value(std::string(label)));
        out.push_back(gfx::avm1::Value(c));
    };
    if (realistic) { add(0, "Off"); add(1, "On"); }
    else add(stored ? 1 : 0, "$WfcPC.Settings.VolumetricRealisticOnly");
}

// Push-to-Talk Key choices: the six presets, the player's own key when it is none of them, then "Custom..." (the capture).
void pttChoices(gfx::avm1::VM& vm, std::vector<gfx::avm1::Value>& out, const std::string& pttKey) {
    using V = gfx::avm1::Value;
    auto choice = [&](const std::string& value, const std::string& label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", V(value));
        vm.set(c, "FriendlyName", V(label));
        out.push_back(V(c));
    };
    bool preset = false;
    for (const char* k : {"V", "T", "B", "Caps Lock", "Mouse 4", "Mouse 5"}) { choice(k, k); preset = preset || pttKey == k; }
    if (!preset && !pttKey.empty()) choice(pttKey, pttKey);
    choice("__custom", "$WfcPC.Voice.PttCustom");
}

// Settings > Audio voice rows (PC EXTENSION, online only; ONLINE_MP_FLOWS.md 3.12, ONLINE_MP_UI_STRINGS.md 9b): appended to
// the original audio page (identified by its FX Volume row), in the page's own lateral-selector item form.
bool addVoiceRows(gfx::Player& p, const std::string& linkage, gfx::avm1::Object* init, const std::string& pttKey) {
    if (linkage != "mc_subMenu" || !init) return false;
    gfx::avm1::VM& vm = p.vm();
    gfx::avm1::Value a = vm.get(init, "buildArray");
    if (!a.isObject() || a.o->kind != gfx::avm1::ObjKind::Array || a.o->elems.empty()) return false;
    const gfx::avm1::Value first = a.o->elems.front();
    if (!first.isObject() || vm.toString(vm.get(first.o, "dataStore")) != "<OnlinePlayerData:ProfileData.FX Volume>") return false;
    using V = gfx::avm1::Value;
    auto choice = [&](std::vector<V>& v, const V& value, const std::string& label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", value);
        vm.set(c, "FriendlyName", V(label));
        v.push_back(V(c));
    };
    auto row = [&](const std::vector<V>& ch, const char* store, const char* hint, const char* label) {
        gfx::avm1::Object* item = vm.newPlain();
        vm.set(item, "choiceArray", V(vm.newArray(ch)));
        vm.set(item, "dataStore", V(std::string(store)));
        vm.set(item, "hintText", V(std::string(hint)));
        vm.set(item, "panelWidth", V(413.0));
        vm.set(item, "text", V(std::string(label)));
        a.o->elems.push_back(V(item));
    };
    std::vector<V> mode, aud, dev, ptt, vol, pvol, mic;
    choice(mode, V(0.0), "$WfcPC.Voice.VoiceOff"); choice(mode, V(1.0), "$WfcPC.Voice.VoicePushToTalk"); choice(mode, V(2.0), "$WfcPC.Voice.VoiceOpenMic");
    choice(aud, V(0.0), "$WfcPC.Voice.VoiceTeamRadio"); choice(aud, V(1.0), "$WfcPC.Voice.VoiceAllRadio");
    choice(aud, V(2.0), "$WfcPC.Voice.VoiceProximity"); choice(aud, V(3.0), "$WfcPC.Voice.VoiceRadioProximity");
    choice(dev, V(std::string()), "$WfcPC.Voice.InputDeviceDefault");   // the capture devices by name come from Systems' audio layer
    pttChoices(vm, ptt, pttKey);
    choice(mic, V(0.0), frontend::GameFlow::micMeterText(0.0f));
    for (int t = 0; t <= 100; t += 10) { choice(vol, V((double)t), std::to_string(t)); choice(pvol, V((double)t), std::to_string(t)); }
    row(mode, "<PCSettings:VoiceMode>", "$WfcPC.Voice.VoiceChatDesc", "$WfcPC.Voice.VoiceChat");
    row(aud, "<PCSettings:VoiceAudience>", "$WfcPC.Voice.VoiceChannelDesc", "$WfcPC.Voice.VoiceChannel");
    row(dev, "<PCSettings:VoiceDevice>", "$WfcPC.Voice.InputDeviceDesc", "$WfcPC.Voice.InputDevice");
    row(ptt, "<PCSettings:VoicePushToTalkKey>", "$WfcPC.Voice.PushToTalkKeyDesc", "$WfcPC.Voice.PushToTalkKey");
    row(vol, "<PCSettings:VoiceVolume>", "$WfcPC.Voice.VoiceVolumeDesc", "$WfcPC.Voice.VoiceVolume");
    row(pvol, "<PCSettings:ProximityVolume>", "$WfcPC.Voice.ProximityVolumeDesc", "$WfcPC.Voice.ProximityVolume");
    row(mic, "<PCSettings:VoiceMicTest>", "$WfcPC.Voice.MicTestDesc", "$WfcPC.Voice.MicTest");
    frontend::FlowTrace::emit("settings.voiceRows", {{"rows", "7"}, {"provenance", "PC EXTENSION"}});
    return true;
}

bool addFrameLimitItem(gfx::Player& p, const std::string& linkage, gfx::avm1::Object* init, int current,
                       const std::function<void()>& onRecommended, bool hdAvailable, bool fsr3Available, int upscalingCurrent,
                       bool frameGenAvailable, bool frameGenActive, bool rtAvailable, bool hdEnhancedAvailable, int vramMB, int hdCurrent,
                       bool rtNextMap, int rtCurrent, bool volumetricCurrent, bool volumetricAvailable) {
    if (linkage != "mc_subMenu" || !init) return false;
    gfx::avm1::VM& vm = p.vm();
    gfx::avm1::Value a = vm.get(init, "buildArray");
    if (!a.isObject() || a.o->kind != gfx::avm1::ObjKind::Array || a.o->elems.size() != 6) return false;
    gfx::avm1::Value last = a.o->elems.back();
    if (!last.isObject() || vm.toString(vm.get(last.o, "text")) != "$UIText.Settings.CommitButton") return false;
    std::vector<gfx::avm1::Value> choices;
    auto choice = [&](int hz, const std::string& label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", gfx::avm1::Value((double)hz));
        vm.set(c, "FriendlyName", gfx::avm1::Value(label));
        choices.push_back(gfx::avm1::Value(c));
    };
    bool listed = current == 0;
    for (int hz : kFrameLimitChoices) {
        if (!listed && current < hz) { choice(current, "Custom (" + std::to_string(current) + ")"); listed = true; }
        choice(hz, std::to_string(hz));
        listed = listed || hz == current;
    }
    if (!listed) choice(current, "Custom (" + std::to_string(current) + ")");
    choice(0, "Uncapped");   // the user's wording (Milestone E settings brief)
    // One lateral-selector row in the menu's own item form, inserted before Apply (the original rows keep their indices
    // for writeGraphicsSettings).
    auto row = [&](const std::vector<gfx::avm1::Value>& ch, const char* store, const char* hint, const char* label) {
        gfx::avm1::Object* item = vm.newPlain();
        vm.set(item, "choiceArray", gfx::avm1::Value(vm.newArray(ch)));
        vm.set(item, "dataStore", gfx::avm1::Value(std::string(store)));
        vm.set(item, "hintText", gfx::avm1::Value(std::string(hint)));
        vm.set(item, "panelWidth", gfx::avm1::Value(413.0));
        vm.set(item, "text", gfx::avm1::Value(std::string(label)));
        a.o->elems.insert(a.o->elems.end() - 1, gfx::avm1::Value(item));
    };
    row(choices, "<PCSettings:FrameLimit>", "Limit the maximum frames per second.", "Frame Rate Limit");
    // PC EXTENSION graphics options (default Off = the original look). Upscaling renders the 3D scene below the window
    // resolution and upscales it (FSR 1 spatial, or FSR 3.1 temporal where its runtime ships); HD Textures uses the HD
    // texture set where one exists.
    // Rendering applies them (IRenderer::setUpscaling / setHdTextures, see Application_Frontend.cpp).
    std::vector<gfx::avm1::Value> up, hd, af;
    auto add = [&](std::vector<gfx::avm1::Value>& v, int value, const char* label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", gfx::avm1::Value((double)value));
        vm.set(c, "FriendlyName", gfx::avm1::Value(std::string(label)));
        v.push_back(gfx::avm1::Value(c));
    };
    // (Labels fit the value column: "Performance" is shortened, as a full "FSR 3.1 Performance" runs past the panel.)
    add(up, 0, "Off"); add(up, 1, "FSR 1 Quality"); add(up, 2, "FSR 1 Balanced"); add(up, 3, "FSR 1 Perf.");
    static const char* const kFsr3[] = {"FSR 3.1 Quality", "FSR 3.1 Balanced", "FSR 3.1 Perf."};
    const bool fsr3Missing = !fsr3Available && upscalingCurrent >= 4;   // a stored FSR 3.1 choice without its runtime
    for (int m = 4; m <= 6; ++m)
        if (fsr3Available || m == upscalingCurrent) add(up, m, kFsr3[m - 4]);
    add(hd, 0, "Off");
    // HD levels, each where its root is installed (without either the row has the single value Off, cannot be changed).
    const bool hdMissing = (hdCurrent == 1 && !hdAvailable) || (hdCurrent == 2 && !hdEnhancedAvailable);
    if (hdAvailable || hdCurrent == 1) add(hd, 1, "Faithful");
    if (hdEnhancedAvailable || hdCurrent == 2) add(hd, 2, "Enhanced");
    add(af, 4, "4x"); add(af, 8, "8x"); add(af, 16, "16x");   // 4x = the original's filtering
    // Hints: one line, no wrap - at most ~58 characters stay over the title rule (measured; longer runs off the panel).
    // Facts measured by Rendering / Integration: FSR 1 costs almost nothing; FSR 3.1 gives the best image when the GPU
    // limits the frame rate and can lower fps on fast PCs; frame generation doubles the frames shown below the refresh
    // rate and adds a little input latency.
    row(up, "<PCSettings:Upscaling>",
        fsr3Missing     ? "FSR 3.1 is not installed: FSR 1 is used at the same scale."
        : fsr3Available ? "FSR 1 nearly free; FSR 3.1 best if GPU-bound, else slower."
                        : "Lower render resolution, upscaled. FSR 1 is nearly free.", "Upscaling");
    row(hd, "<PCSettings:HDTextures>",
        // below a level's memory floor: a warning only (VRAM unknown = no warning)
        hdMissing                                                             ? "That HD level is not installed: a lower one is used."
        : !hdAvailable && !hdEnhancedAvailable                                ? "The HD texture pack is not installed."
        : vramMB > 0 && vramMB < frontend::kHdTexturesMinVramMB                ? "May exceed this GPU's memory on large maps."
        : hdEnhancedAvailable && vramMB > 0 && vramMB < frontend::kHdEnhancedMinVramMB ? "Enhanced may exceed this GPU's memory on large maps."
        : hdEnhancedAvailable && rtAvailable && vramMB > 0 && vramMB < frontend::kHdEnhancedRtMinVramMB
            ? "Enhanced with Ray Tracing may exceed this GPU's memory."
        : hdEnhancedAvailable ? "Faithful: the original, sharper. Enhanced: new materials."
                              : "HD textures use more video memory. Applies on next map.", "HD Textures");
    row(af, "<PCSettings:Anisotropy>", "Sharper textures at grazing angles (4x is the original setting).", "Anisotropic Filtering");
    if (frameGenAvailable) {   // decided at start-up: a value different from this session's state says so
        std::vector<gfx::avm1::Value> fg;
        add(fg, 0, frameGenActive ? "Off (restart)" : "Off");
        add(fg, 1, frameGenActive ? "On" : "On (restart)");
        row(fg, "<PCSettings:FrameGeneration>",
            "Doubles frames below your refresh rate; adds a little lag.", "Frame Generation");   // restart: in the labels
    }
    if (rtAvailable) {   // PC EXTENSION; Off by default, never auto-detected (user decision 2026-10-12). Hint: Rendering /
        // Integration 2026-10-13 (Off keeps the RT data until the map changes; modes switch live on a map loaded with RT)
        std::vector<gfx::avm1::Value> rtv;
        // This map loaded without the RT scene: turning it on applies at the next map load (Off always applies live).
        // "(next)" keeps the labels inside the value column ("... (next map)" runs past it, like "FSR 3.1 Performance").
        add(rtv, 0, "Off"); add(rtv, 1, rtNextMap ? "Faithful (next)" : "Faithful"); add(rtv, 2, rtNextMap ? "Realistic (next)" : "Realistic");
        row(rtv, "<PCSettings:RayTracing>",
            rtNextMap ? "Turned on now, it starts with the next map."
                      : "Off frees its memory (~0.2-0.5 GB) when the map changes.", "Ray Tracing");
        // PC ADAPTATION (Integration, package 7): Rendering's X3 froxel volumetric lighting, Realistic only, default On.
        // While the Ray Tracing row is not on Realistic the row offers one value, "Realistic only", carrying the stored
        // choice (it cannot change); syncVolumetricRow switches the choices live as the Ray Tracing row moves.
        if (volumetricAvailable) {
            std::vector<gfx::avm1::Value> vl;
            volumetricChoices(vm, vl, rtCurrent == 2, volumetricCurrent);
            row(vl, "<PCSettings:VolumetricLighting>", "$WfcPC.Settings.VolumetricLightingDesc", "$WfcPC.Settings.VolumetricLighting");
        }
    }
    {   // PC EXTENSION (user decision, next head): the sun's lens flares (Rendering's renderer side); Original = the shipped look
        std::vector<gfx::avm1::Value> lf;
        add(lf, 0, "Off"); add(lf, 1, "$WfcPC.Settings.LensFlaresSubtle"); add(lf, 2, "$WfcPC.Settings.LensFlaresOriginal");
        row(lf, "<PCSettings:LensFlares>", "$WfcPC.Settings.LensFlaresDesc", "$WfcPC.Settings.LensFlares");
    }
    {   // PC EXTENSION (user decisions; Rendering setSmokeMode / setWaterMode): Original = the shipped look (default)
        std::vector<gfx::avm1::Value> sm, wa;
        add(sm, 0, "$WfcPC.Settings.LensFlaresOriginal"); add(sm, 1, "Enhanced");
        row(sm, "<PCSettings:Smoke>", "$WfcPC.Settings.SmokeDesc", "$WfcPC.Settings.Smoke");
        add(wa, 0, "$WfcPC.Settings.LensFlaresOriginal"); add(wa, 1, "Enhanced");
        row(wa, "<PCSettings:Water>", "$WfcPC.Settings.WaterDesc", "$WfcPC.Settings.Water");
    }
    {   // PC EXTENSION (user playtest 2026-10-13): the FPS counter in the top-right corner
        std::vector<gfx::avm1::Value> fc;
        add(fc, 0, "Off"); add(fc, 1, "On");
        row(fc, "<PCSettings:ShowFps>", "Shows frames per second in the top-right corner.", "FPS Counter");
    }
    {   // PC EXTENSION action row in the original button form (as Commit Changes / Brightness: mc_panelButton +
        // clickFunction): re-applies the auto-detected preset.
        gfx::avm1::Object* item = vm.newPlain();
        vm.set(item, "text", gfx::avm1::Value(std::string("Recommended Settings")));
        vm.set(item, "panelWidth", gfx::avm1::Value(413.0));
        vm.set(item, "hintText", gfx::avm1::Value(std::string("Detect this PC's hardware and apply the best settings it can hold.")));
        vm.set(item, "listMenuOverride", gfx::avm1::Value(std::string("mc_panelButton")));
        std::function<void()> cb = onRecommended;
        vm.set(item, "clickFunction", gfx::avm1::Value(vm.newFunction([cb](gfx::avm1::VM&, const gfx::avm1::Value&, gfx::avm1::Args&) {
            if (cb) cb();
            return gfx::avm1::Value();
        }, "recommendedSettingsClick")));
        a.o->elems.insert(a.o->elems.end() - 1, gfx::avm1::Value(item));
    }
    frontend::FlowTrace::emit("settings.frameLimitItem", {{"current", std::to_string(current)}, {"choices", std::to_string(choices.size())},
                                                          {"provenance", "PC ADAPTATION"}});
    frontend::FlowTrace::emit("settings.pcExtensionRows", {{"rows", std::string("Upscaling,HD Textures,Anisotropic Filtering,") + (frameGenAvailable ? "Frame Generation," : "") + (rtAvailable ? "Ray Tracing," : "") + (rtAvailable && volumetricAvailable ? "Volumetric Lighting," : "") + "Recommended Settings"}, {"provenance", "PC EXTENSION"}});
    return true;
}
}

// PC ADAPTATION (user playtest 2026-10-13): the Private Match bot settings are rows of the mode's Game Mode Options menu
// (PartyLobby_GFX mc_menuMultiplayerGameModeSettings), served as TnGameSettings fields by DataStores
// (GameFlow::botOptions): the movie builds them as its own lateral selectors after the mode's rows. Here each one applies
// its value as it changes, as the lobby rows did (Player Limit re-ranges the count rows and the map-aware Extended counts
// show at once); Create Game's WriteValue then finds them unchanged. No bot rows on the following lobby screen.
namespace {
gfx::MovieClip* findClipNamed(gfx::MovieClip* c, const std::string& name, int depth = 0) {
    if (!c || depth > 10) return nullptr;
    for (auto& ch : c->children) {
        if (!ch.second || ch.second->kind != gfx::DisplayObject::Kind::Clip || ch.second->removed) continue;
        auto* m = static_cast<gfx::MovieClip*>(ch.second.get());
        if (m->name == name) return m;
        if (gfx::MovieClip* f = findClipNamed(m, name, depth + 1)) return f;
    }
    return nullptr;
}
}

// A lateral-selector row of a settings page by its dataStore markup (the menu's items keep their init properties).
namespace {
gfx::avm1::Object* findRowByStore(gfx::avm1::VM& vm, gfx::MovieClip* c, const std::string& store, int depth = 0) {
    if (!c || depth > 12) return nullptr;
    for (auto& ch : c->children) {
        if (!ch.second || ch.second->kind != gfx::DisplayObject::Kind::Clip || ch.second->removed) continue;
        auto* m = static_cast<gfx::MovieClip*>(ch.second.get());
        if (m->script && vm.toString(vm.get(m->script, "dataStore")) == store) return m->script;
        if (gfx::avm1::Object* f = findRowByStore(vm, m, store, depth + 1)) return f;
    }
    return nullptr;
}
}

// The Audio page's live rows: the Mic Test meter (the session's input level) and the Push-to-Talk Key row after a capture
// (the new key selected) or a cancel (the old key back).
void GfxPresenter::syncAudioPage(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    if (gfx::avm1::Object* mic = findRowByStore(vm, p.root(), "<PCSettings:VoiceMicTest>")) {
        const std::string bars = frontend::GameFlow::micMeterText(flow.micLevel());
        if (bars != micMeterShown_) {
            gfx::avm1::Object* c = vm.newPlain();
            vm.set(c, "Value", V(0.0));
            vm.set(c, "FriendlyName", V(bars));
            vm.callMethod(V(mic), "createSelectionData", {V(vm.newArray({V(c)}))});
            vm.set(mic, "currentSelectionIndex", V(0.0));
            micMeterShown_ = bars;
        }
    }
    if (flow.pttCaptureActive()) return;
    gfx::avm1::Object* ptt = findRowByStore(vm, p.root(), "<PCSettings:VoicePushToTalkKey>");
    if (!ptt) return;
    const V cur = vm.get(ptt, "currentSelectionData");
    const std::string& key = flow.profile().voice.pttKey;
    if (cur.isObject() && vm.toString(vm.get(cur.o, "Value")) == key) return;
    std::vector<V> ch;
    pttChoices(vm, ch, key);
    double index = 0;
    for (size_t i = 0; i < ch.size(); ++i) if (vm.toString(vm.get(ch[i].o, "Value")) == key) index = (double)i;
    vm.callMethod(V(ptt), "createSelectionData", {V(vm.newArray(ch))});
    vm.set(ptt, "currentSelectionIndex", V(index));
    frontend::FlowTrace::emit("settings.pttRow", {{"key", key}});
}

// The "Online Play" prompt's hand-offs (ONLINE_MP_FIRST_RUN.md step 2), over the party lobby:
// - Change / Choose Name: the Accounts screen's own prompt (AccountsMenu_GFX: NewAccountTitle / NewAccountMessage, 15
//   characters) -> Account.CreateAccount (which shows the original result box), then the prompt again;
// - Voice Settings: SettingsMenu.swf loaded into the party lobby as FrontEnd / the pause menu load it (it gives the input
//   back to the menu that opened it on close), then its Audio button.
void GfxPresenter::runFirstRunUi(gfx::Player& p, frontend::GameFlow& flow, const std::string& movie) {
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    if (firstRunFooterHidden_ == &p) {   // the settings menu closed (it restores the lobby menu's alpha): the footer back
        gfx::MovieClip* anchor = findClipNamed(p.root(), "menuAnchor_mc");
        if (!anchor || !anchor->script || vm.toNumber(vm.get(anchor->script, "_alpha")) >= 99.0) {
            if (gfx::MovieClip* footer = findClipNamed(p.root(), "footer_mc"); footer && footer->script) vm.set(footer->script, "_alpha", V(100.0));
            firstRunFooterHidden_ = nullptr;
        }
    }
    if (firstRunAudioPending_ == &p) {
        if (gfx::MovieClip* audio = findClipNamed(p.root(), "audioBtn_mc"); audio && audio->script && vm.get(audio->script, "onClick").isObject()) {
            vm.callMethod(V(audio->script), "onClick", {});
            firstRunAudioPending_ = nullptr;
            frontend::FlowTrace::emit("online.firstRun", {{"state", "audio page"}});
        } else if (++firstRunAudioFrames_ > 600) firstRunAudioPending_ = nullptr;
    }
    const frontend::GameFlow::FirstRunUi req = flow.takeFirstRunUi();
    if (req == frontend::GameFlow::FirstRunUi::NamePrompt) {
        vm.set(vm.global, "WfcFirstRunName", V(vm.newFunction([this, &flow, movie](gfx::avm1::VM& v, const V&, gfx::avm1::Args& a) -> V {
            const std::string name = a.empty() ? std::string() : v.toString(a[0]);
            rt_.bridge(movie, "Account.CreateAccount", {name});
            rt_.bridge(movie, "Account.Login", {name});   // the chosen name is the online name at once (D1)
            flow.firstRunNameEntered();
            return V();
        }, "WfcFirstRunName", 1)));
        const V util = vm.get(vm.global, "HmUtility");
        if (util.isObject())
            vm.callMethod(util, "DisplayTextPrompt", {V(std::string("$UIText.MessagePrompts.NewAccountTitle")),
                                                      V(std::string("$UIText.MessagePrompts.NewAccountMessage")), V(std::string()),
                                                      V(std::string("_global.WfcFirstRunName")), V(15.0)});
    } else if (req == frontend::GameFlow::FirstRunUi::VoiceSettings && p.root() && p.root()->script) {
        const V clip = vm.callMethod(V(p.root()->script), "createEmptyMovieClip", {V(std::string("wfcSettingsMenu_mc")), V(90020.0)});
        if (clip.isObject() && clip.o->display && clip.o->display->kind == gfx::DisplayObject::Kind::Clip) {
            if (gfx::MovieClip* anchor = findClipNamed(p.root(), "menuAnchor_mc"); anchor && anchor->script) {
                vm.set(clip.o, "hideClip", V(anchor->script));   // the settings menu restores it on close
                vm.set(anchor->script, "_alpha", V(0.0));
                // the lobby's footer (Back / XP hints, the hint line) would show through: hidden until the menu is back
                if (gfx::MovieClip* footer = findClipNamed(p.root(), "footer_mc"); footer && footer->script) {
                    vm.set(footer->script, "_alpha", V(0.0));
                    firstRunFooterHidden_ = &p;
                }
            }
            p.loadMovieInto(static_cast<gfx::MovieClip*>(clip.o->display), "../Settings/SettingsMenu.swf", nullptr);
            firstRunAudioPending_ = &p;
            firstRunAudioFrames_ = 0;
        }
    }
}

// ONLINE_LOBBY_FLOW_V2 1.1: the party lobby's Private Match row hosts at once (the last-used mode; offline the same lobby
// with bots) instead of opening the mode list and the options page.
void GfxPresenter::syncHostNowRow(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::MovieClip* host = findClipNamed(p.root(), "hostGame_mc");
    if (!host || !host->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    if (vm.toBool(vm.get(host->script, "__wfcHostNow"))) return;
    vm.set(host->script, "__wfcHostNow", V(true));
    vm.set(host->script, "onClick", V(vm.newFunction([&flow](gfx::avm1::VM&, const V&, gfx::avm1::Args&) -> V {
        flow.call("Online.HostPrivateNow");
        return V();
    }, "onClick", 0)));
    frontend::FlowTrace::emit("lobby.hostNowRow", {{"provenance", "PC ADAPTATION (lobby flow v2)"}});
}

// ONLINE_LOBBY_FLOW_V2 2.2: the host's Game Settings row (a duplicate of the lobby's Create a Character button, inserted
// under Select Map) and its page: the original Settings sub-menu (SettingsMenu.swf mc_subMenu, lateral rows written as they
// change) with Game Mode and the rows of GameFlow::lobbySettingRows. Back closes the whole page (_global.closeSettings).
void GfxPresenter::attachGameSettingsPage(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    if (!gsMenu_) return;
    const V parent = vm.get(gsMenu_, "_parent");
    if (!parent.isObject()) return;
    auto choice = [&](std::vector<V>& v, const std::string& value, const std::string& label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", V(value));
        vm.set(c, "FriendlyName", V(label));
        v.push_back(V(c));
    };
    std::vector<V> rows;
    auto row = [&](const std::vector<V>& ch, const std::string& store, const std::string& text, const std::string& hint) {
        gfx::avm1::Object* item = vm.newPlain();
        vm.set(item, "choiceArray", V(vm.newArray(ch)));
        vm.set(item, "dataStore", V(store));
        vm.set(item, "hintText", V(hint));
        vm.set(item, "panelWidth", V(413.0));
        vm.set(item, "text", V(text));
        rows.push_back(V(item));
    };
    std::vector<V> modes;
    for (const std::string& t : frontend::GameFlow::lobbyModes()) choice(modes, t, rt_.catalog().modeFriendlyName(t));
    row(modes, "<WfcLobby:GameMode>", "$UIText.Lobby.GameMode", "$WfcPC.Lobby.GameModeDesc");
    for (const frontend::GameFlow::LobbySettingRow& r : flow.lobbySettingRows()) {
        std::vector<V> ch;
        for (const std::string& v : r.values) choice(ch, v, v);
        row(ch, r.store, r.label, "$WfcPC.Lobby.GameSettingsDesc");
    }
    gfx::avm1::Object* init = vm.newPlain();
    vm.set(init, "footer", vm.get(parent.o, "footer_mc"));
    vm.set(init, "buildArray", V(vm.newArray(rows)));
    vm.set(init, "listItemSpacing", V(31.0));
    vm.set(init, "listMenuItemID", V(std::string("mc_lateralSelectorInc")));
    vm.set(init, "_backMenuRef", V(gsMenu_));
    vm.set(init, "titleText", V(std::string("$WfcPC.Lobby.GameSettings")));
    vm.set(init, "_y", vm.get(parent.o, "subMenuPtY"));
    vm.set(init, "_x", vm.get(parent.o, "subMenuPtX"));
    const V depth = vm.callMethod(parent, "getNextHighestDepth", {});
    vm.callMethod(parent, "attachMovie", {V(std::string("mc_subMenu")), V(std::string("subMenu_mc")), depth, V(init)});
    vm.set(gsMenu_, "_alpha", V(0.0));
    gsMode_ = flow.lobby().gameModeTag;
    frontend::FlowTrace::emit("lobby.gameSettingsPage", {{"mode", gsMode_}, {"rows", std::to_string(rows.size())}});
}

void GfxPresenter::syncGameSettings(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    const bool host = flow.sessionRole() != frontend::GameFlow::SessionRole::Member && flow.level() == frontend::LevelKind::GameLobby &&
                      flow.lobby().playlistId < 0;
    // the row (host only)
    gfx::MovieClip* cc = findClipNamed(p.root(), "customCharacters_mc");
    gfx::MovieClip* sm = findClipNamed(p.root(), "selectMap_mc");
    if (host && cc && sm && cc->script && sm->script && cc->parent && cc->parent->script &&
        !vm.get(cc->parent->script, "gameSettings_mc").isObject()) {
        gfx::avm1::Object* menu = cc->parent->script;
        const double smY = vm.toNumber(vm.get(sm->script, "_y")), ccY = vm.toNumber(vm.get(cc->script, "_y"));
        const double pitch = ccY > smY ? ccY - smY : 20.0;
        for (auto& [depth, child] : cc->parent->children) {
            auto* mc = dynamic_cast<gfx::MovieClip*>(child.get());
            if (!mc || !mc->script || mc == sm) continue;
            const double y = vm.toNumber(vm.get(mc->script, "_y"));
            if (y > smY + 1.0) vm.set(mc->script, "_y", V(y + pitch));
        }
        gfx::avm1::Object* init = vm.newPlain();
        vm.set(init, "_y", V(smY + pitch));
        gfx::MovieClip* dup = p.duplicate(cc, "gameSettings_mc", 951, init);
        if (dup && dup->script) {
            gfx::avm1::Object* o = dup->script;
            vm.set(o, "text", V(std::string("$WfcPC.Lobby.GameSettings")));
            const V lab = vm.get(o, "label_txt");
            if (lab.isObject()) vm.set(lab.o, "htmlText", V(std::string("$WfcPC.Lobby.GameSettings")));
            const V below = vm.get(sm->script, "focusDown");
            vm.set(o, "focusUp", V(std::string("selectMap_mc")));
            vm.set(o, "focusDown", below);
            vm.set(sm->script, "focusDown", V(std::string("gameSettings_mc")));
            if (below.isString()) { const V next = vm.get(menu, vm.toString(below)); if (next.isObject()) vm.set(next.o, "focusUp", V(std::string("gameSettings_mc"))); }
            vm.set(o, "onOver", V(vm.newFunction([menu](gfx::avm1::VM& v, const V&, gfx::avm1::Args&) -> V {
                const V w = v.get(menu, "HintWidget");
                if (w.isObject()) v.set(w.o, "HintText", V(std::string("$WfcPC.Lobby.GameSettingsDesc")));
                return V();
            }, "onOver", 0)));
            vm.set(o, "onClick", V(vm.newFunction([this](gfx::avm1::VM&, const V&, gfx::avm1::Args&) -> V {
                gsOpenRequested_ = true;
                return V();
            }, "onClick", 0)));
            frontend::FlowTrace::emit("lobby.gameSettingsRow", {{"built", "true"}});
        }
    }
    // the page
    if (gsOpenRequested_ && gsState_ == 0 && host && p.root() && p.root()->script) {
        gsOpenRequested_ = false;
        const V clip = vm.callMethod(V(p.root()->script), "createEmptyMovieClip", {V(std::string("wfcGameSettings_mc")), V(90030.0)});
        if (clip.isObject() && clip.o->display && clip.o->display->kind == gfx::DisplayObject::Kind::Clip) {
            if (gfx::MovieClip* anchor = findClipNamed(p.root(), "menuAnchor_mc"); anchor && anchor->script) {
                vm.set(clip.o, "hideClip", V(anchor->script));   // the settings menu restores it on close
                vm.set(anchor->script, "_alpha", V(0.0));
            }
            p.loadMovieInto(static_cast<gfx::MovieClip*>(clip.o->display), "../Settings/SettingsMenu.swf", nullptr);
            gsPlayer_ = &p; gsState_ = 1; gsFrames_ = 0; gsMenu_ = nullptr;
        }
    }
    if (gsPlayer_ != &p) return;
    gfx::MovieClip* gsClip = findClipNamed(p.root(), "wfcGameSettings_mc");
    if (gsState_ == 1) {   // the settings movie initialised: its top menu is the Back target, the page goes over it
        gfx::MovieClip* audio = gsClip ? findClipNamed(gsClip, "audioBtn_mc") : nullptr;
        if (audio && audio->parent && audio->parent->script && vm.get(audio->script, "onClick").isObject()) {
            gsMenu_ = audio->parent->script;
            attachGameSettingsPage(p, flow);
            gsState_ = 2;
        } else if (++gsFrames_ > 600) { gsState_ = 0; gsPlayer_ = nullptr; }
        return;
    }
    if (gsState_ == 2) {
        gfx::MovieClip* sub = gsClip ? findClipNamed(gsClip, "subMenu_mc") : nullptr;
        const bool close = !sub || sub->removed || !host || flow.lobby().countingDown;   // Back, a member, or the countdown
        if (close) {
            if (sub && !sub->removed) vm.callMethod(V(sub->script), "removeMovieClip", {});
            vm.callMethod(V(vm.global), "closeSettings", {});
            gsState_ = 0; gsPlayer_ = nullptr; gsMenu_ = nullptr;
            frontend::FlowTrace::emit("lobby.gameSettingsPage", {{"closed", "true"}});
        } else if (flow.lobby().gameModeTag != gsMode_) {   // a new mode: its own rows
            vm.callMethod(V(sub->script), "removeMovieClip", {});
            attachGameSettingsPage(p, flow);
        }
    }
}

// PC ADAPTATION (ONLINE_LOBBY_FLOW_V2 4.1, the user's Halo Reach vote): in a public lobby the vote takes the Select Map
// row's place - one row per option (map name and live tally) plus None of these in round 1, in the lobby's own button
// form (duplicates of customCharacters_mc, as the Game Settings row). Accept on a row votes; the player's own vote is
// marked; once the vote closed the winner reads "Next map: ..." and the rows no longer take votes. Host and members alike.
void GfxPresenter::syncVoteRows(gfx::Player& p, frontend::GameFlow& flow) {
    const frontend::GameFlow::MapVote& v = flow.mapVote();
    if (flow.level() != frontend::LevelKind::GameLobby || v.round == 0) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    gfx::MovieClip* cc = findClipNamed(p.root(), "customCharacters_mc");
    gfx::MovieClip* sm = findClipNamed(p.root(), "selectMap_mc");
    if (!cc || !sm || !cc->script || !sm->script || !cc->parent || !cc->parent->script) return;
    gfx::avm1::Object* menu = cc->parent->script;
    constexpr int kRows = 4;   // 3 options + None of these (round 2: 3, the last row hidden)
    if (!vm.get(menu, "wfcVote0_mc").isObject()) {
        // Select Map is the map preview tile; the menu rows follow it at one pitch (Create a Character's distance below it).
        // The vote rows take the slots right under the tile and every row below moves down by them (captures 2026-10-23:
        // row 0 at the tile's own y hid under the preview; a pitch from other sibling clips stacked the rows on one line).
        const double smY = vm.toNumber(vm.get(sm->script, "_y")), ccY = vm.toNumber(vm.get(cc->script, "_y"));
        const double pitch = ccY - smY > 1.0 && ccY - smY < 80.0 ? ccY - smY : 31.0;
        const double top = smY + pitch;
        for (auto& [depth, child] : cc->parent->children) {
            auto* mc = dynamic_cast<gfx::MovieClip*>(child.get());
            if (!mc || !mc->script || mc == sm) continue;
            const double y = vm.toNumber(vm.get(mc->script, "_y"));
            if (y > smY + 1.0) vm.set(mc->script, "_y", V(y + pitch * kRows));
        }
        // Select Map (the preview tile) stays: it shows the leading option; its focus now leads into the vote rows
        const V above = V(std::string("selectMap_mc")), below = vm.get(sm->script, "focusDown");
        for (int i = 0; i < kRows; ++i) {
            const std::string name = "wfcVote" + std::to_string(i) + "_mc";
            gfx::avm1::Object* init = vm.newPlain();
            vm.set(init, "_y", V(top + pitch * i));
            gfx::MovieClip* dup = p.duplicate(cc, name, 960 + i, init);
            if (!dup || !dup->script) return;
            gfx::avm1::Object* o = dup->script;
            vm.set(o, "focusUp", i == 0 ? above : V("wfcVote" + std::to_string(i - 1) + "_mc"));
            vm.set(o, "focusDown", i == kRows - 1 ? below : V("wfcVote" + std::to_string(i + 1) + "_mc"));
            vm.set(o, "onOver", V(vm.newFunction([menu](gfx::avm1::VM& vv, const V&, gfx::avm1::Args&) -> V {
                const V w = vv.get(menu, "HintWidget");
                if (w.isObject()) vv.set(w.o, "HintText", V(std::string("$WfcPC.Lobby.VoteRowDesc")));
                return V();
            }, "onOver", 0)));
            frontend::GameFlow* fl = &flow;
            vm.set(o, "onClick", V(vm.newFunction([fl, i](gfx::avm1::VM&, const V&, gfx::avm1::Args&) -> V {
                fl->castMapVote(i);
                return V();
            }, "onClick", 0)));
        }
        vm.set(sm->script, "focusDown", V(std::string("wfcVote0_mc")));
        if (below.isString()) { const V b = vm.get(menu, vm.toString(below)); if (b.isObject()) vm.set(b.o, "focusUp", V("wfcVote" + std::to_string(kRows - 1) + "_mc")); }
        vm.set(menu, "currentFocus", V(std::string("wfcVote0_mc")));   // the vote is the lobby's first business
        frontend::FlowTrace::emit("lobby.voteRows", {{"built", "true"}, {"rows", std::to_string(kRows)}});
    }
    // labels: "<map>   <votes>", the own vote marked, the winner once closed
    const std::string none = rt_.catalog().localizeKey("$WfcPC.Lobby.VoteNone"), next = rt_.catalog().localizeKey("$WfcPC.Lobby.VoteNext");
    for (int i = 0; i < kRows; ++i) {
        const V row = vm.get(menu, "wfcVote" + std::to_string(i) + "_mc");
        if (!row.isObject()) continue;
        const bool shown = i < (int)v.counts.size() && (v.open || (i < (int)v.maps.size() && v.maps[(size_t)i] == v.winner));
        std::string label;
        if (shown) {
            std::string name = none;
            if (i < (int)v.maps.size()) {
                const frontend::MapInfo* mi = rt_.catalog().mapById(v.maps[(size_t)i]);
                name = mi && !mi->friendlyName.empty() ? mi->friendlyName : std::to_string(v.maps[(size_t)i]);
            }
            label = !v.open ? next + " " + name
                            : (flow.myMapVote() == i ? "> " : "") + name + "   " + std::to_string(v.counts[(size_t)i]);
        }
        if (vm.toBool(vm.get(row.o, "_visible")) != shown) vm.set(row.o, "_visible", V(shown));
        if (vm.toString(vm.get(row.o, "text")) != label) {
            vm.set(row.o, "text", V(label));
            const V lab = vm.get(row.o, "label_txt");
            if (lab.isObject()) vm.set(lab.o, "htmlText", V(label));
        }
    }
}

void GfxPresenter::syncVolumetricRow(gfx::Player& p) {
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    gfx::avm1::Object* rt = findRowByStore(vm, p.root(), "<PCSettings:RayTracing>");
    gfx::avm1::Object* vol = rt ? findRowByStore(vm, p.root(), "<PCSettings:VolumetricLighting>") : nullptr;
    if (!rt || !vol) return;   // the page is not built (yet) or was left
    const V cur = vm.get(rt, "currentSelectionData");
    const bool realistic = cur.isObject() && (int)vm.toNumber(vm.get(cur.o, "Value")) == 2;
    if ((realistic ? 1 : 0) == volumetricGateShown_) return;
    // The stored choice: the row's own current value (Off / On), or the value the single "Realistic only" entry carries.
    const V vcur = vm.get(vol, "currentSelectionData");
    const bool stored = vcur.isObject() ? vm.toNumber(vm.get(vcur.o, "Value")) != 0.0 : rt_.flow().profile().display.volumetricLighting;
    std::vector<V> ch;
    volumetricChoices(vm, ch, realistic, stored);
    vm.callMethod(V(vol), "createSelectionData", {V(vm.newArray(ch))});
    vm.set(vol, "currentSelectionIndex", V(realistic ? (stored ? 1.0 : 0.0) : 0.0));
    if (volumetricGateShown_ >= 0)
        frontend::FlowTrace::emit("settings.volumetricRow", {{"offered", realistic ? "Off,On" : "Realistic only"},
                                                             {"stored", frontend::FlowTrace::boolean(stored)}, {"provenance", "PC ADAPTATION"}});
    volumetricGateShown_ = realistic ? 1 : 0;
}

// PC EXTENSION (versus "Iconic Characters"): GameLobby_GFX's iconic menu (mc_menuMultiplayerIconicPC) is Escalation's; in a
// versus lobby its Escalation Info row (escalationTut_mc, the Escalation tutorial) is removed and the rows below close up
// one pitch; focus skips it. Escalation itself (GameTeamStatus 2) keeps the row.
void GfxPresenter::syncIconicVersusMenu(gfx::Player& p) {
    gfx::MovieClip* tut = findClipNamed(p.root(), "escalationTut_mc");
    if (!tut || !tut->script || !tut->parent || !tut->visible) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    gfx::avm1::Object* menu = tut->parent->script;
    const double y = vm.toNumber(vm.get(tut->script, "_y"));
    double nextY = 0.0;   // the pitch: the nearest row below
    for (auto& [depth, child] : tut->parent->children) {
        auto* mc = dynamic_cast<gfx::MovieClip*>(child.get());
        if (!mc || mc == tut || !mc->script) continue;
        const double cy = vm.toNumber(vm.get(mc->script, "_y"));
        if (cy > y + 1.0 && (nextY == 0.0 || cy < nextY)) nextY = cy;
    }
    const double pitch = nextY > 0.0 ? nextY - y : 0.0;
    for (auto& [depth, child] : tut->parent->children) {
        auto* mc = dynamic_cast<gfx::MovieClip*>(child.get());
        if (!mc || mc == tut || !mc->script) continue;
        const double cy = vm.toNumber(vm.get(mc->script, "_y"));
        if (cy > y + 1.0) vm.set(mc->script, "_y", V(cy - pitch));
    }
    vm.set(tut->script, "_visible", V(false));
    const V up = vm.get(tut->script, "focusUp"), down = vm.get(tut->script, "focusDown");
    if (menu && up.isString()) { const V a = vm.get(menu, vm.toString(up)); if (a.isObject()) vm.set(a.o, "focusDown", down); }
    if (menu && down.isString()) { const V b = vm.get(menu, vm.toString(down)); if (b.isObject()) vm.set(b.o, "focusUp", up); }
    frontend::FlowTrace::emit("lobby.iconicVersusMenu", {{"removed", "escalationTut_mc"}, {"provenance", "PC EXTENSION"}});
}

void GfxPresenter::syncJoinGameRow(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::MovieClip* host = findClipNamed(p.root(), "hostGame_mc");
    if (!host || !host->script || !host->parent || !host->parent->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    gfx::avm1::Object* menu = host->parent->script;
    if (vm.get(menu, "joinGame_mc").isObject()) return;   // built for this menu instance
    gfx::avm1::Object* init = vm.newPlain();
    const double hostY = vm.toNumber(vm.get(host->script, "_y"));
    vm.set(init, "_y", V(hostY + 31.0));   // the menu's row pitch (findGame 0, hostGame 31)
    // inserted, not laid over: the rows below Host Game (Create a Character, Teletran I, Friends List) move down one pitch
    for (auto& [depth, child] : host->parent->children) {
        auto* mc = dynamic_cast<gfx::MovieClip*>(child.get());
        if (!mc || mc == host || !mc->script) continue;
        const double y = vm.toNumber(vm.get(mc->script, "_y"));
        if (y > hostY + 1.0) vm.set(mc->script, "_y", V(y + 31.0));
    }
    gfx::MovieClip* dup = p.duplicate(host, "joinGame_mc", 950, init);
    if (!dup || !dup->script) return;
    gfx::avm1::Object* o = dup->script;
    const std::string label = "$UIText.OnlineFriends.JoinGame";   // the original "Join Game"
    vm.set(o, "text", V(label));
    const V lab = vm.get(o, "label_txt");
    if (lab.isObject()) vm.set(lab.o, "htmlText", V(label));
    // focus: Host Game -> Join Game -> whatever followed Host Game
    const V below = vm.get(host->script, "focusDown");
    vm.set(o, "focusUp", V(std::string("hostGame_mc")));
    vm.set(o, "focusDown", below);
    vm.set(host->script, "focusDown", V(std::string("joinGame_mc")));
    if (below.isString()) {
        const V next = vm.get(menu, vm.toString(below));
        if (next.isObject()) vm.set(next.o, "focusUp", V(std::string("joinGame_mc")));
    }
    vm.set(o, "onOver", V(vm.newFunction([menu](gfx::avm1::VM& v, const V&, gfx::avm1::Args&) -> V {
        const V w = v.get(menu, "HintWidget");
        if (w.isObject()) v.set(w.o, "HintText", V(std::string("$WfcPC.Online.JoinGameDesc")));
        return V();
    }, "onOver", 0)));
    // TextPrompt_GFX through the original HmUtility.DisplayTextPrompt(Title, Description, DefaultText, Callback, MaxChars)
    vm.set(vm.global, "WfcJoinTransmit", V(vm.newFunction([&flow](gfx::avm1::VM& v, const V&, gfx::avm1::Args& a) -> V {
        flow.call("Online.JoinGame", {a.empty() ? std::string() : v.toString(a[0])});
        return V();
    }, "WfcJoinTransmit", 1)));
    vm.set(o, "onClick", V(vm.newFunction([](gfx::avm1::VM& v, const V&, gfx::avm1::Args&) -> V {
        const V util = v.get(v.global, "HmUtility");
        if (util.isObject())
            v.callMethod(util, "DisplayTextPrompt", {V(std::string("$UIText.OnlineFriends.JoinGame")), V(std::string("$WfcPC.Online.JoinPrompt")),
                                                    V(std::string()), V(std::string("_global.WfcJoinTransmit")), V(64.0)});
        return V();
    }, "onClick", 0)));
    frontend::FlowTrace::emit("online.joinGameRow", {{"built", "true"}, {"provenance", "PC EXTENSION"}});
}

void GfxPresenter::syncJoinPanel(gfx::Player& p, frontend::GameFlow& flow, const platform::InputFrame& in) {
    gfx::MovieClip* root = p.root();
    if (!root || !root->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    const bool show = flow.sessionRole() == frontend::GameFlow::SessionRole::Host && !flow.joinCode().empty();
    V panel = vm.get(root->script, "wfcJoinPanel_mc");
    if (!show) { if (panel.isObject()) vm.callMethod(panel, "removeMovieClip", {}); return; }
    if (!panel.isObject()) {
        // the right column under the team lists (GameLobby_GFX stage 1120 x 720: teams x ~650-1050, status line y ~630)
        panel = vm.callMethod(V(root->script), "createEmptyMovieClip", {V(std::string("wfcJoinPanel_mc")), V(90010.0)});
        if (!panel.isObject()) return;
        vm.set(panel.o, "_x", V(655.0));
        vm.set(panel.o, "_y", V(470.0));
        const char* names[] = {"code_txt", "address_txt", "copy_txt", "privacy_txt"};
        for (int i = 0; i < 4; ++i) {
            V t = vm.callMethod(panel, "createTextField", {V(std::string(names[i])), V((double)(i + 1)), V(0.0), V(22.0 * i), V(420.0), V(22.0)});
            if (!t.isObject()) continue;
            vm.set(t.o, "html", V(true));
            vm.set(t.o, "embedFonts", V(true));
            vm.set(t.o, "selectable", V(false));
        }
    }
    auto setLine = [&](const char* name, const std::string& text, const char* colour, int size) {
        const V t = vm.get(panel.o, name);
        if (t.isObject())
            vm.set(t.o, "htmlText", V(std::string("<font face='$NormalFont' size='") + std::to_string(size) + "' color='" + colour + "'>" + text + "</font>"));
    };
    auto sub = [](std::string s, const char* tok, const std::string& v) { const size_t at = s.find(tok); if (at != std::string::npos) s.replace(at, 2, v); return s; };
    const frontend::Catalog& cat = rt_.catalog();
    bool copied = false;
    for (uint16_t k : in.keyPresses) if (k == 'C') copied = platform::setClipboardText(flow.joinCode());
    if (copied) { codeCopiedFor_ = 2.0f; frontend::FlowTrace::emit("online.joinCodeCopied", {{"code", flow.joinCode()}}); }
    setLine("code_txt", sub(cat.localizeKey("$WfcPC.Online.JoinCode"), "`c", flow.joinCode()), "#FFFFFF", 18);
    setLine("address_txt", sub(cat.localizeKey("$WfcPC.Online.JoinAddress"), "`a", flow.joinAddress()), "#C8C8C8", 16);
    setLine("copy_txt", cat.localizeKey(codeCopiedFor_ > 0.0f ? "$WfcPC.Online.CodeCopied" : "$WfcPC.Online.CopyCode"), "#8AF0FF", 14);
    setLine("privacy_txt", cat.localizeKey("$WfcPC.Online.IpPrivacy"), "#A0A0A0", 13);
    if (codeCopiedFor_ > 0.0f) codeCopiedFor_ -= 1.0f / 60.0f;
}

void GfxPresenter::syncModeOptionBots(gfx::Player& p, frontend::GameFlow& flow) {
    gfx::MovieClip* menu = findClipNamed(p.root(), "menuMultiplayerGameModeSettings_mc");
    if (!menu || !menu->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    const V items = vm.get(menu->script, "itemArray");
    if (!items.isObject() || items.o->kind != gfx::avm1::ObjKind::Array) return;
    int hooked = 0;
    for (const V& it : items.o->elems) {
        if (!it.isObject()) continue;
        const std::string sf = vm.toString(vm.get(it.o, "selectionField"));
        if (sf.size() < 18 || sf.rfind("<TnGameSettings:", 0) != 0) continue;
        const frontend::GameFlow::BotOption* o = frontend::GameFlow::botOption(sf.substr(16, sf.size() - 17));
        if (!o) continue;
        const std::string field = o->field;
        if (!vm.toBool(vm.get(it.o, "__wfcBots"))) {
            vm.set(it.o, "__wfcBots", V(true));
            if (flow.online() && (field == "autobot" || field == "decepticon" || field == "enemy")) {
                // online (Systems 10.5): a leaver's seat goes to a bot when bot rows are on - the row says so
                // the page has no hint widget of its own: the party lobby's footer hint (footer_mc.hint_mc), as its menus use
                gfx::avm1::Object* page = menu->script;
                gfx::avm1::Object* root = p.root() ? p.root()->script : nullptr;
                vm.set(it.o, "onOver", V(vm.newFunction([page, root](gfx::avm1::VM& v, const V&, gfx::avm1::Args&) -> V {
                    V w;
                    if (root) { const V f = v.get(root, "footer_mc"); if (f.isObject()) w = v.get(f.o, "hint_mc"); }
                    if (!w.isObject()) w = v.get(page, "HintWidget");
                    if (w.isObject()) v.set(w.o, "HintText", V(std::string("$WfcPC.Online.BotsLeaverDesc")));
                    return V();
                }, "onOver", 0)));
            }
            vm.set(it.o, "selectionUpdated", V(vm.newFunction(
                [&flow, field](gfx::avm1::VM& v, const V& self, gfx::avm1::Args&) -> V {
                    if (!self.isObject()) return V();
                    const int idx = (int)v.toNumber(v.get(self.o, "currentSelectionIndex"));
                    if (idx >= 0 && idx != flow.botValue(field)) flow.setBotSetting(field, idx);
                    return V();
                }, "selectionUpdated", 0)));
            ++hooked;
        }
        // The rows follow the profile: a new range after a Player Limit change, the counts it recommends.
        const int maxV = std::max(0, flow.botMax(field));
        const V data = vm.get(it.o, "selectionData");
        const int have = data.isObject() && data.o->kind == gfx::avm1::ObjKind::Array ? (int)data.o->elems.size() : -1;
        if (have != maxV + 1) {
            std::vector<V> ch;
            for (int v = 0; v <= maxV; ++v) {
                gfx::avm1::Object* c = vm.newPlain();
                vm.set(c, "FriendlyName", V(flow.botChoiceLabel(field, v)));
                ch.push_back(V(c));
            }
            vm.callMethod(it, "createSelectionData", {V(vm.newArray(ch))});
        }
        const int want = std::clamp(flow.botValue(field), 0, maxV);
        if ((int)vm.toNumber(vm.get(it.o, "currentSelectionIndex")) != want) vm.set(it.o, "currentSelectionIndex", V((double)want));
    }
    if (hooked > 0)
        frontend::FlowTrace::emit("lobby.modeOptionBots", {{"rows", std::to_string(hooked)}, {"mode", flow.lobby().gameModeTag},
                                                           {"provenance", "PC ADAPTATION"}});
}

void GfxPresenter::update(frontend::GameFlow& flow, const platform::InputFrame& in, float dt) {
    extendedMatch_ = flow.matchValues().players.size() > 10;
    core::prof::Scope prof("ui.update");
    UiProfScope uiProf(g_uiUpdate);
    syncMovies(flow);
    syncPopup(flow);
    for (size_t i = 0; i < keyQueue_.size();) {
        if (--keyQueue_[i].frames > 0) { ++i; continue; }
        injectKey(keyQueue_[i].code, keyQueue_[i].down);
        keyQueue_.erase(keyQueue_.begin() + (long)i);
    }
    if (!cursor_) {
        cursor_ = std::make_unique<GfxMovie>();
        bool ok = cursor_->open(lib_, &rt_.catalog(), "UI_GFxMouseCursor_p.Cursor_GFX_1",
                                [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                                [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
        frontend::FlowTrace::emit("gfx.cursor", {{"opened", frontend::FlowTrace::boolean(ok)}});
        if (!ok) cursor_.reset();
    }
    deliverKeys(in);
    deliverMouse(in);
    if (!stickCallbacks_.empty()) {
        // Left stick for registered callbacks: the pad (XInput +Y up -> +Y down) or, on PC, the movement keys (W / A / S / D,
        // the PC left-stick equivalent) and the held arrow keys as full deflection [PC ADAPTATION: the shipped PC
        // binding of the picker cursor is native; on WIN the movie only adds mouse handlers to the palette arrows].
        float sx = in.padLX, sy = -in.padLY;
        if (in.isDown(platform::Button::Left)) sx = -1;
        if (in.isDown(platform::Button::Right)) sx = 1;
        if (in.isDown(platform::Button::Forward)) sy = -1;
        if (in.isDown(platform::Button::Back)) sy = 1;
        if (in.uiIsDown(platform::UiKey::Left)) sx = -1;
        if (in.uiIsDown(platform::UiKey::Right)) sx = 1;
        if (in.uiIsDown(platform::UiKey::Up)) sy = -1;
        if (in.uiIsDown(platform::UiKey::Down)) sy = 1;
        std::vector<StickCallback> cbs = stickCallbacks_;
        for (const StickCallback& c : cbs)
            for (Open& op : movies_)
                if (op.object == c.movie) op.movie->invoke(c.path, {Value((double)sx), Value((double)sy)});
    }
    if (cursor_) cursor_->advance(dt);
#if WFC_DLSS_PACKAGE
    syncPackageCredit();
#endif
    if (hud_) { hud_->player().setViewport((float)viewW_, (float)viewH_); hud_->advance(dt); hudInterpUpdate(*hud_); if (extendedMatch_) checkKillFeed(dt); syncWorldLabels(); }
    if (scoreboard_ && scoreboardShown_) { scoreboard_->advance(dt); scrollPlayerList(scoreboard_->player(), scoreScroll_, in, dt); } else scoreScroll_ = 0.0f;
    if (scoreboard_) syncIconicPlayerList(scoreboard_->player(), flow);   // the Tab scoreboard (#7)
    // PlayerList_GFX _global.GivePlayerListInput (RE audit 3): the scoreboard's rows take the pad / keys while it is up, so a
    // row's A (online: Mute / Unmute) works; online only, so versus stays as it was.
    {
        const bool want = scoreboard_ && scoreboardShown_ && flow.online();
        if (want != playerListInputGiven_ && scoreboard_) {
            scoreboard_->invoke("_global.GivePlayerListInput", {Value(want)});
            playerListInputGiven_ = want;
            frontend::FlowTrace::emit("gfx.playerListInput", {{"given", frontend::FlowTrace::boolean(want)}});
        }
    }
    if (loading_) { loading_->advance(dt); loadingTime_ += dt; }
    // Movies may open / close others from their scripts: iterate over a snapshot of the objects.
    std::vector<std::string>& objs = objsScratch_;
    objs.clear();
    for (const Open& o : movies_) objs.push_back(o.object);
    // Every movie gets the viewport (Stage.width / height and onResize: the menus size their backgrounds from it).
    for (Open& op : movies_) op.movie->player().setViewport((float)viewW_, (float)viewH_);
    for (Extra& e : extras_) e.movie->player().setViewport((float)viewW_, (float)viewH_);
    if (scoreboard_) scoreboard_->player().setViewport((float)viewW_, (float)viewH_);
    if (loading_) loading_->player().setViewport((float)viewW_, (float)viewH_);
    for (const std::string& o : objs)
        for (Open& op : movies_) if (op.object == o) { op.movie->advance(dt); break; }
    {   // the end-of-match View Scores list scrolls like the in-match scoreboard
        bool endStats = false;
        for (Open& op : movies_)
            if (op.object.find("EndGameStats_GFX") != std::string::npos) { endStats = true; scrollPlayerList(op.movie->player(), endScoreScroll_, in, dt); }
        if (!endStats) endScoreScroll_ = 0.0f;
    }
    // Movie-opened movies: closes requested during their own script run are applied here; advance the rest.
    for (const std::string& o : deferredErase_)
        for (size_t i = extras_.size(); i-- > 0;) if (extras_[i].object == o) { extras_.erase(extras_.begin() + (long)i); shapesStale_ = true; }
    deferredErase_.clear();
    for (Extra& e : extras_) e.movie->advance(dt);
    for (size_t i = 0; i < pendingCalls_.size();) {   // movieCall: once the (new) movie has run a frame
        PendingCall& pc = pendingCalls_[i];
        GfxMovie* target = openMovieNamed(pc.movie);   // the flow's movie or an extra; closed meanwhile -> dropped
        if (!target && pc.waitFrames-- > 0) { ++i; continue; }
        if (!target) {
            frontend::FlowTrace::emit("gfx.movieCallDropped", {{"movie", pc.movie}, {"fn", pc.fn}, {"why", "movie not open"}});
            pendingCalls_.erase(pendingCalls_.begin() + (long)i);
            continue;
        }
        if (!pc.advanced) { pc.advanced = true; ++i; continue; }
        Args a;
        for (const frontend::BridgeValue& b : pc.args) a.push_back(toValue(target->player().vm(), b));
        target->invoke(pc.fn, a);
        frontend::FlowTrace::emit("gfx.movieCall", {{"movie", pc.movie}, {"fn", pc.fn}});
        pendingCalls_.erase(pendingCalls_.begin() + (long)i);
    }
    frameLimitShown_ = flow.profile().display.frameLimit;
    if (graphicsPagePlayer_) syncVolumetricRow(*graphicsPagePlayer_);
    if (audioPagePlayer_) syncAudioPage(*audioPagePlayer_, flow);
    for (Open& op : movies_) {
        if (flow.online() && op.object.find("PartyLobby_GFX") != std::string::npos) syncJoinGameRow(op.movie->player(), flow);
        if (flow.online() && op.object.find("PartyLobby_GFX") != std::string::npos) runFirstRunUi(op.movie->player(), flow, op.object);
        if (op.object.find("PartyLobby_GFX") != std::string::npos) syncHostNowRow(op.movie->player(), flow);   // lobby flow v2
        if (op.object.find("GameLobby_GFX") != std::string::npos) syncGameSettings(op.movie->player(), flow);
        if (op.object.find("GameLobby_GFX") != std::string::npos) syncVoteRows(op.movie->player(), flow);
        if (op.object.find("PauseMenu_GFX") != std::string::npos) syncPauseChooseCharacter(op.movie->player(), flow);
        syncIconicPlayerList(op.movie->player(), flow);   // the countdown roster / end-of-match list (#7)
        scrollSettingsSubMenu(op.movie->player(), in);   // any settings page (FrontEnd, pause menu, lobby Voice / Game Settings)
        if (flow.iconicMode() && flow.lobby().gameTeamStatus != 2 && op.object.find("GameLobby_GFX") != std::string::npos &&
            (!flow.lobby().settings || flow.lobby().settings->teamTypeValue == 3))
            syncIconicVersusMenu(op.movie->player());
        if (flow.online() && op.object.find("GameLobby_GFX") != std::string::npos) syncJoinPanel(op.movie->player(), flow, in);
        if (op.object.find("PartyLobby_GFX") != std::string::npos) syncModeOptionBots(op.movie->player(), flow);
        if (op.object.find("GameLobby_GFX") == std::string::npos) continue;
        // the selected map's name centred on its preview, above the frame (ui/MapLabel.h, user playtest 2026-10-13)
        const ui::MapLabelResult ml = ui::centreMapLabels(op.movie->player());
        if (ml.raisedTo >= 0)
            frontend::FlowTrace::emit("lobby.mapLabel", {{"above", "frame_mc"}, {"depth", std::to_string(ml.raisedTo)},
                                                         {"frameW", std::to_string((int)ml.frameW)}, {"provenance", "PC ADAPTATION"}});
    }
    for (Open& op : movies_) {
        gfx::Player& p = op.movie->player();
        if (!p.attachHook)
            p.attachHook = [this, &p](const std::string& linkage, gfx::avm1::Object* init) {
                if (addFrameLimitItem(p, linkage, init, frameLimitShown_, [this] { recommendedSettingsPressed(); }, rt_.hdTextureLevelAvailable(1),
                                      rt_.fsr3Available(), rt_.flow().profile().display.upscaling,
                                      rt_.frameGenerationAvailable(), rt_.frameGenerationActive(), rt_.rayTracingAvailable(),
                                      rt_.hdTextureLevelAvailable(2), rt_.vramMB(), rt_.flow().profile().display.hdTextures,
                                      rt_.rayTracingOnAppliesNextMap(), rt_.flow().profile().display.rayTracing,
                                      rt_.flow().profile().display.volumetricLighting, rt_.volumetricLightingAvailable()))
                    graphicsPagePlayer_ = &p;
                else if (rt_.flow().online() && addVoiceRows(p, linkage, init, rt_.flow().profile().voice.pttKey))   // online only
                    audioPagePlayer_ = &p;
            };
    }
    // Deferred engine -> AS invokes (the presenter's own and the flow's, e.g. _global.MovieEnded).
    for (const auto& iv : rt_.flow().takeUiInvokes()) deferred_.push_back({iv.first, iv.second, {}});
    std::vector<Deferred> due;
    due.swap(deferred_);
    for (Deferred& d : due) {
        for (Open& op : movies_)
            if (op.object == d.movie) { op.movie->invoke(d.fn, d.args); frontend::FlowTrace::emit("gfx.invoke", {{"movie", d.movie}, {"fn", d.fn}}); }
    }
    // Data-store change callbacks (HmWidget.updateDSValue(markup, value) by target path).
    for (const auto& c : rt_.dataStores().poll()) {
        if (hud_ && hud_->object() == c.movie) { hud_->invoke(c.callback, {Value(c.markup), Value(c.value)}); continue; }
        if (scoreboard_ && scoreboard_->object() == c.movie) { if (scoreboardShown_) scoreboard_->invoke(c.callback, {Value(c.markup), Value(c.value)}); continue; }
        for (Open& op : movies_)
            if (op.object == c.movie) {
                Value r = op.movie->invoke(c.callback, {Value(c.markup), Value(c.value)});
                frontend::FlowTrace::emit("gfx.dsCallback", {{"movie", c.movie}, {"markup", c.markup}, {"value", c.value}, {"callback", c.callback}});
                (void)r;
            }
    }
}

void GfxPresenter::draw(const frontend::GameFlow& flow, int w, int h) {
    core::prof::Scope prof("ui.draw");
    // DEV TOOL WFC_GFXMEM=<seconds>: per open movie the AVM1 heap / graveyard sizes, and the renderer / atom table totals,
    // every n seconds (leak sweeps: a series that only grows across cycles is a leak).
    static const double memEvery = std::getenv("WFC_GFXMEM") ? std::atof(std::getenv("WFC_GFXMEM")) : 0.0;
    if (memEvery > 0.0) {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - last).count() >= memEvery) {
            last = now;
            std::string per;
            auto add = [&](const char* tag, GfxMovie* m) {
                if (!m) return;
                per += std::string(" ") + tag + "=" + std::to_string(m->player().vm().heapSize()) + "/" + std::to_string(m->player().graveyard.size());
            };
            add("hud", hud_.get());
            add(scoreboardShown_ ? "scores" : "scores(closed)", scoreboard_.get());
            add("loading", loading_.get());
            for (Open& o : movies_) add(o.object.c_str(), o.movie.get());
            for (Extra& e : extras_) add(e.object.c_str(), e.movie.get());
            LOG_INFO("gfxmem shapes=%zu textures=%zu atoms=%zu movies(heap/graveyard):%s", gl_.cachedShapes(), gl_.textures(),
                     gfx::avm1::atomCount(), per.c_str());
        }
    }
    UiProfScope uiProf(g_uiDraw);
    g_uiFrame.tick(frontend::uiStateName(flow.ui().state()));
    if (!glReady_) { glReady_ = gl_.init(); if (!glReady_) return; }
    viewW_ = w; viewH_ = h;
    gfx::Player::hostViewportW = (float)w;   // a noScale movie opened later lays out for this viewport
    gfx::Player::hostViewportH = (float)h;
    if (shapesStale_) { gl_.forgetShapes(); shapesStale_ = false; }
    const bool fpsOn = rt_.flow().profile().display.showFps;
    if (!fpsOn && fps_) { fps_.reset(); fpsMeter_.reset(); fpsW_ = fpsH_ = 0; fpsText_.clear(); }
    if (movies_.empty() && !loading_ && !video_ && !(hud_ && hudVisible_) && !(scoreboard_ && scoreboardShown_) && !fpsOn) return;
    gl_.begin(w, h);
    if (video_ && !videoOver_) gl_.drawVideo(video_, videoW_, videoH_, videoSerial_);
    auto drawMovie = [&](GfxMovie& m) {
        items_.clear();
        gfx::Player& p = m.player();
        p.buildRenderList(GfxRendererGL::movieMatrix(p, w, h), items_);
        gl_.draw(items_);
    };
    static const bool emptyLayer = std::getenv("WFC_GFX_EMPTY") != nullptr;   // diagnostics: composite only
    if (emptyLayer) {}
    else if (loading_) drawMovie(*loading_);
    else {
        if (hud_ && hudVisible_) drawMovie(*hud_);
        if (scoreboard_ && scoreboardShown_) drawMovie(*scoreboard_);
        for (Open& o : movies_) drawMovie(*o.movie);
        for (Extra& e : extras_) drawMovie(*e.movie);
    }
    if (video_ && videoOver_) gl_.drawVideo(video_, videoW_, videoH_, videoSerial_);
#if WFC_DLSS_PACKAGE
    if (credit_ && creditShown_) drawMovie(*credit_);
#endif
    if (fpsOn) {
        drawFps(w, h);
        if (fps_) drawMovie(*fps_);
    }
    if (cursor_ && !videoOver_) drawMovie(*cursor_);
    gl_.end();
}

#if WFC_DLSS_PACKAGE
// PC EXTENSION (DLSS package builds): one small grey line in the bottom-right corner while the start-up movie, a boot
// logo movie or the credits movie is up; no timing of its own. Sized from the window: ~1.3 % of its height (14 px at 1080p), a 2 % margin.
void GfxPresenter::syncPackageCredit() {
    const frontend::GameFlow& flow = rt_.flow();
    const std::string& movie = flow.kismetMovie().empty() ? flow.scriptMovie() : flow.kismetMovie();
    bool shown = frontend::packageCreditShown(frontend::kDlssPackage, rt_.startupMovieUp(),
                                              rt_.fullScreenMovie() && rt_.movieOpen(), movie);
    // Between two boot logos no movie plays for a few frames (Systems measured 0.05-0.06 s): the line stays up for up to
    // 0.25 s there instead of blinking. A non-logo movie (FMV_intro) or the end of the credits turns it off at once.
    const auto now = std::chrono::steady_clock::now();
    if (shown) { creditOnLogo_ = frontend::isStartupLogoMovie(movie) || rt_.startupMovieUp(); creditGapFrom_ = now; }
    else if (creditShown_ && creditOnLogo_ && movie.empty() && std::chrono::duration<double>(now - creditGapFrom_).count() < 0.25)
        shown = true;
    if (shown != creditShown_) {
        creditShown_ = shown;
        frontend::FlowTrace::emit("package.credit", {{"line", "NVIDIA DLSS"}, {"shown", frontend::FlowTrace::boolean(shown)},
                                                     {"movie", rt_.startupMovieUp() ? "startup" : movie},
                                                     {"provenance", "PC EXTENSION"}});
    }
    if (!shown || creditFailed_) return;
    if (!credit_) {
        if (!openOverlayMovie(credit_, "dlss_txt")) { creditFailed_ = true; return; }
        creditW_ = creditH_ = 0;
    }
    gfx::Player& p = credit_->player();
    gfx::MovieClip* root = p.root();
    if (!root || !root->script) return;
    if (creditW_ == viewW_ && creditH_ == viewH_) return;
    creditW_ = viewW_; creditH_ = viewH_;
    p.setViewport((float)viewW_, (float)viewH_);
    const gfx::Matrix m = GfxRendererGL::movieMatrix(p, viewW_, viewH_);
    const gfx::Matrix inv = m.inverse();
    const float px = std::max(11.0f, (float)viewH_ * 0.013f), margin = (float)viewH_ * 0.02f;
    const float lineH = px * 1.6f;
    const gfx::Point a = inv.apply({margin, (float)viewH_ - margin - lineH}), b = inv.apply({(float)viewW_ - margin, (float)viewH_ - margin});
    const double perUnit = std::max(1e-3, (double)m.a * 20.0);   // window pixels per stage unit
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    V t = vm.callMethod(V(root->script), "createTextField", {V("dlss_txt"), V(1000), V(a.x / 20.0), V(a.y / 20.0),
                                                              V((b.x - a.x) / 20.0), V((b.y - a.y) / 20.0)});
    if (!t.isObject()) return;
    vm.setV(t, "html", V(true));
    vm.setV(t, "embedFonts", V(true));
    vm.setV(t, "selectable", V(false));
    vm.setV(t, "_alpha", V(75.0));
    const int size = (int)std::lround(px / perUnit);
    vm.setV(t, "htmlText", V(std::string("<p align='right'><font face='$NormalFont' size='") + std::to_string(std::max(1, size)) +
                             "' color='#C8C8C8'>" + frontend::kDlssCreditText + "</font></p>"));
}
#endif

bool GfxPresenter::openOverlayMovie(std::unique_ptr<GfxMovie>& m, const char* keep) {
    m = std::make_unique<GfxMovie>();
    if (!m->open(lib_, &rt_.catalog(), "UI_GFxMouseCursor_p.Cursor_GFX_1",
                 [](GfxMovie&, const std::string&, gfx::avm1::Args&) { return gfx::avm1::Value(); },
                 [](GfxMovie&, const std::string&, const std::string&) {})) {
        m.reset();
        return false;
    }
    m->advance(1.0f / m->frameRate());   // frame 1 (the root script object); never advanced again
    if (gfx::MovieClip* root = m->player().root())   // the cursor art stays hidden
        for (auto& c : root->children) if (c.second && c.second->name != keep) c.second->visible = false;
    return true;
}

// PC EXTENSION (user playtest 2026-10-13): the FPS counter, top-right, "144 FPS  6.9 ms" refreshed 4 times a second
// (frontend::FpsMeter: the presented-frame interval, Rendering's guidance), white with the HUD's soft drop shadow, ~1.8 %
// of the window height (19 px at 1080p), a 1.5 % margin. Drawn in the UI layer (after post-processing).
void GfxPresenter::drawFps(int w, int h) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    fpsMeter_.frame(t);
    if (fpsFailed_) return;
    if (!fps_) {
        if (!openOverlayMovie(fps_, "fps_txt")) { fpsFailed_ = true; return; }
        fpsW_ = fpsH_ = 0;
        frontend::FlowTrace::emit("settings.fpsCounter", {{"shown", "true"}, {"provenance", "PC EXTENSION"}});
    }
    gfx::Player& p = fps_->player();
    gfx::MovieClip* root = p.root();
    if (!root || !root->script) return;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    const double perUnit = std::max(1e-3, (double)GfxRendererGL::movieMatrix(p, w, h).a * 20.0);   // window px per stage unit
    const float px = std::max(12.0f, (float)h * 0.018f), margin = (float)h * 0.015f;
    if (fpsW_ != w || fpsH_ != h) {
        fpsW_ = w; fpsH_ = h;
        fpsText_.clear();
        p.setViewport((float)w, (float)h);
        const gfx::Matrix inv = GfxRendererGL::movieMatrix(p, w, h).inverse();
        const float boxW = px * 12.0f;   // "1000 FPS  1.0 ms" fits
        const gfx::Point a = inv.apply({(float)w - margin - boxW, margin}), b = inv.apply({(float)w - margin, margin + px * 1.6f});
        V f = vm.callMethod(V(root->script), "createTextField", {V("fps_txt"), V(1000), V(a.x / 20.0), V(a.y / 20.0),
                                                                  V((b.x - a.x) / 20.0), V((b.y - a.y) / 20.0)});
        if (!f.isObject()) { fpsFailed_ = true; return; }
        vm.setV(f, "html", V(true));
        vm.setV(f, "embedFonts", V(true));
        vm.setV(f, "selectable", V(false));
        vm.setV(f, "shadowColor", V(0.0));
        vm.setV(f, "shadowAlpha", V(0.8));
        vm.setV(f, "shadowStrength", V(2.0));
        vm.setV(f, "shadowDistance", V(0.0));
        vm.setV(f, "shadowBlurX", V(4.0));
        vm.setV(f, "shadowBlurY", V(4.0));
    }
    const std::string text = fpsMeter_.text();
    if (text == fpsText_) return;
    fpsText_ = text;
    const V f = vm.get(root->script, "fps_txt");
    if (!f.isObject()) return;
    vm.setV(f, "htmlText", V(std::string("<p align='right'><font face='$NormalFont' size='") +
                             std::to_string(std::max(1, (int)std::lround(px / perUnit))) + "' color='#FFFFFF'>" + text + "</font></p>"));
}

void GfxPresenter::recommendedSettingsPressed() {
    rt_.applyRecommendedGraphics("recommended");
    rt_.applyDisplaySettings();
    frameLimitShown_ = rt_.flow().profile().display.frameLimit;
    // Back out of the Graphics page and open it again (the menu's own navigation rebuilds it with the new values).
    keyQueue_ = {{2, 27, true}, {3, 27, false}, {10, 13, true}, {11, 13, false}};
    frontend::FlowTrace::emit("settings.recommendedPressed", {{"provenance", "PC EXTENSION"}});
}

void GfxPresenter::injectKey(int code, bool down) {
    if (movies_.empty()) return;
    GfxMovie* m = movies_.back().movie.get();
    for (Extra& e : extras_) if (e.focus) m = e.movie.get();
    // A key event on a focused input field also types its character, as in Flash Player (automation key:<code>; real
    // keyboards deliver text through WM_CHAR).
    if (down && code >= 32 && code < 127 && m->hasTextFocus()) m->textInput((char32_t)code);
    m->key(code, down);
}

std::vector<std::string> GfxPresenter::openMovieObjects() const {
    std::vector<std::string> v;
    for (const Open& o : movies_) v.push_back(o.object);
    if (loading_) v.push_back(loading_->object());
    return v;
}

std::string GfxPresenter::dumpMovie(const std::string& object) const {
    for (const Open& o : movies_) if (o.object == object) return o.movie->dumpTree();
    if (loading_ && loading_->object() == object) return loading_->dumpTree();
    return "";
}

} // namespace ui
