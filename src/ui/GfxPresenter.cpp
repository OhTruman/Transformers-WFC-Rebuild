#include "ui/GfxPresenter.h"
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

void GfxPresenter::movieCall(const std::string& movie, const std::string& fn, const std::vector<frontend::BridgeValue>& args) {
    bool open = false;
    for (const Extra& e : extras_) open = open || e.object == movie;
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
    // Close movies the flow no longer has open.
    for (size_t i = movies_.size(); i-- > 0;) {
        if (std::find(want.begin(), want.end(), movies_[i].object) == want.end()) {
            rt_.dataStores().forgetMovie(movies_[i].object);
            botRowsBuilt_.erase(&movies_[i].movie->player());   // keyed by the Player's address: gone with the movie
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
            for (const std::string& t : L.engageTexts) loading_->invoke("_global.AddEngageText", {Value(t)});
            frontend::FlowTrace::emit("gfx.loadingMovie", {{"movie", L.gfxMovie}, {"title", L.title}, {"message", L.message}});
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
    size_t i = 0;
    for (; i < worldLabels_.size(); ++i) {
        const std::string name = "l" + std::to_string(i);
        gfx::avm1::Value c = vm.getV(box, name);
        if (!c.isObject()) {
            c = vm.callMethod(box, "attachMovie", {gfx::avm1::Value("mc_gameMessage_default"), gfx::avm1::Value(name), gfx::avm1::Value((int)i + 1)});
            if (!c.isObject()) return;
            vm.setV(c, "fadeOut", gfx::avm1::Value(vm.newFunction([](gfx::avm1::VM&, const gfx::avm1::Value&, gfx::avm1::Args&) { return gfx::avm1::Value(); }, "fadeOut")));
        }
        const gfx::Point s = inv.apply({worldLabels_[i].x, worldLabels_[i].y});
        vm.setV(c, "_x", gfx::avm1::Value((double)(s.x / 20.0f)));
        vm.setV(c, "_y", gfx::avm1::Value((double)(s.y / 20.0f)));
        const float span = dmax - dmin;
        const double alpha = span > 1e-3f ? 100.0 - 60.0 * (worldLabels_[i].depth - dmin) / span : 100.0;
        vm.setV(c, "_alpha", gfx::avm1::Value(alpha));
        const int team = worldLabels_[i].team;
        const char* colour = team == 0 ? "#50B5D5" : team == 1 ? "#F03C3C" : "#FFFF66";
        gfx::avm1::Value txt = vm.getV(c, "message_txt");
        if (txt.isObject()) vm.setV(txt, "htmlText", gfx::avm1::Value(std::string("<font color='") + colour + "'>" + worldLabels_[i].text + "</font>"));
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
void addFrameLimitItem(gfx::Player& p, const std::string& linkage, gfx::avm1::Object* init, int current,
                       const std::function<void()>& onRecommended, bool hdAvailable) {
    if (linkage != "mc_subMenu" || !init) return;
    gfx::avm1::VM& vm = p.vm();
    gfx::avm1::Value a = vm.get(init, "buildArray");
    if (!a.isObject() || a.o->kind != gfx::avm1::ObjKind::Array || a.o->elems.size() != 6) return;
    gfx::avm1::Value last = a.o->elems.back();
    if (!last.isObject() || vm.toString(vm.get(last.o, "text")) != "$UIText.Settings.CommitButton") return;
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
    // resolution and upscales it (FSR 1 spatial upscaling); HD Textures uses the HD texture set where one exists.
    // Rendering applies them (IRenderer::setUpscaling / setHdTextures, see Application_Frontend.cpp).
    std::vector<gfx::avm1::Value> up, hd, af;
    auto add = [&](std::vector<gfx::avm1::Value>& v, int value, const char* label) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", gfx::avm1::Value((double)value));
        vm.set(c, "FriendlyName", gfx::avm1::Value(std::string(label)));
        v.push_back(gfx::avm1::Value(c));
    };
    add(up, 0, "Off"); add(up, 1, "FSR 1 Quality"); add(up, 2, "FSR 1 Balanced"); add(up, 3, "FSR 1 Performance");
    add(hd, 0, "Off");
    if (hdAvailable) add(hd, 1, "On");   // without the pack the row has the single value Off (cannot be changed)
    add(af, 4, "4x"); add(af, 8, "8x"); add(af, 16, "16x");   // 4x = the original's filtering
    row(up, "<PCSettings:Upscaling>", "Render the 3D scene at a lower resolution and upscale it (higher frame rate).", "Upscaling");
    row(hd, "<PCSettings:HDTextures>",
        hdAvailable ? "Use high-resolution textures (more video memory). Applies on the next map load."
                    : "The HD texture pack is not installed.", "HD Textures");
    row(af, "<PCSettings:Anisotropy>", "Sharper textures at grazing angles (4x is the original setting).", "Anisotropic Filtering");
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
    frontend::FlowTrace::emit("settings.pcExtensionRows", {{"rows", "Upscaling,HD Textures,Anisotropic Filtering,Recommended Settings"}, {"provenance", "PC EXTENSION"}});
}
}

// PC ADAPTATION: Private Match bot rows. GameLobby_GFX's menu (lobby_mc.menuAnchor_mc.menu_mc: Start Game, Select Map,
// Create a Character, Teletran I, Friends List; HmMenu navigation through each row's focusUp / focusDown names) gets
// lateral selectors below its last row, duplicated from its own Select Map selector (HmLateralSelector: text,
// displaySelection, createSelectionData, selectionUpdated, onOver -> HintWidget.HintText): Player Limit (ORIGINAL 5 v 5 /
// EXTENDED Custom Game), then team modes Autobot Bots + Decepticon Bots, free-for-all Bots, and Bot Difficulty; other
// modes none. Rebuilt when the mode's kind changes; when the limit or the human's faction changes only the count rows'
// choices are refreshed in place (focus stays). The values are GameFlow's (persisted; sent in the launch URL).
namespace {
std::vector<gfx::avm1::Value> botChoices(gfx::avm1::VM& vm, const std::string& field, int maxV, bool teams) {
    std::vector<gfx::avm1::Value> out;
    static const char* kDiff[] = {"EASY", "MEDIUM", "HARD", "EXPERT"};   // EXPERT: PC EXTENSION (Gameplay's Expert AI)
    for (int v = 0; v <= maxV; ++v) {
        gfx::avm1::Object* c = vm.newPlain();
        vm.set(c, "Value", gfx::avm1::Value((double)v));
        std::string label = std::to_string(v);
        if (field == "difficulty") label = kDiff[std::min(v, 3)];
        else if (field == "ai") label = v ? "SMART" : "CLASSIC";
        else if (field == "extended") label = v ? (teams ? "EXTENDED (32 V 32)" : "EXTENDED") : (teams ? "ORIGINAL (5 V 5)" : "ORIGINAL (10)");
        vm.set(c, "FriendlyName", gfx::avm1::Value(label));
        out.push_back(gfx::avm1::Value(c));
    }
    return out;
}
// The hint line (lobby_mc.footer_mc.hint_mc) sits below the menu; three added rows fit above it, each further row moves it
// down one row (23) so it never overlaps. Re-applied every frame against the y the footer's own timeline last set (the
// footer animates in; the runtime lets a timeline move a script-changed instance, unlike Flash).
void placeBotHint(gfx::Player& p, int rows) {
    gfx::DisplayObject* h = p.resolveTarget("lobby_mc.footer_mc.hint_mc", p.root());
    if (!h || !h->script) return;
    gfx::avm1::VM& vm = p.vm();
    const double offset = 23.0 * std::max(0, rows - 3);
    const double cur = vm.toNumber(vm.get(h->script, "_y"));
    gfx::avm1::Value last = vm.get(h->script, "__wfcHintSet");
    // the timeline's y: the current one unless it is still the value this function set
    double base = cur;
    if (!last.isUndef() && std::fabs(vm.toNumber(last) - cur) < 0.01) base = vm.toNumber(vm.get(h->script, "__wfcHintY"));
    vm.set(h->script, "__wfcHintY", gfx::avm1::Value(base));
    vm.set(h->script, "_y", gfx::avm1::Value(base + offset));
    vm.set(h->script, "__wfcHintSet", gfx::avm1::Value(vm.toNumber(vm.get(h->script, "_y"))));
}
int botValue(const frontend::LocalProfile::Bots& b, const std::string& f) {
    return f == "autobot" ? b.autobot : f == "decepticon" ? b.decepticon : f == "enemy" ? b.enemy : f == "extended" ? (b.extended ? 1 : 0)
         : f == "ai" ? b.aiEffective() : b.difficulty;
}
}

void GfxPresenter::syncBotRows(gfx::Player& p, frontend::GameFlow& flow) {
    using frontend::GameFlow;
    const int kind = (int)flow.botRows();
    gfx::DisplayObject* menuD = p.resolveTarget("lobby_mc.menuAnchor_mc.menu_mc", p.root());
    if (!menuD || !menuD->script) { botRowsBuilt_.erase(&p); return; }
    gfx::avm1::VM& vm = p.vm();
    gfx::avm1::Object* menu = menuD->script;
    static const char* kNames[] = {"botLimit_mc", "botAutobot_mc", "botDecepticon_mc", "botEnemy_mc", "botDifficulty_mc", "botAI_mc"};
    const bool teams = kind == (int)GameFlow::BotRows::Teams;
    const frontend::LocalProfile::Bots& b = flow.profile().bots;
    const int limitKey = (b.extended ? 1 : 0) * 2 + flow.humanFaction();
    auto it = botRowsBuilt_.find(&p);
    const bool present = vm.get(menu, "botLimit_mc").isObject();
    if (it != botRowsBuilt_.end() && it->second / 10 == kind && (present || kind == (int)GameFlow::BotRows::None)) {
        placeBotHint(p, kind == (int)GameFlow::BotRows::None ? 0 : (kind == (int)GameFlow::BotRows::Teams ? 4 : 3));
        if (it->second % 10 != limitKey) {   // limit / faction change: the count rows' ranges, in place
            for (const char* n : {"botAutobot_mc", "botDecepticon_mc", "botEnemy_mc"}) {
                gfx::avm1::Value r = vm.get(menu, n);
                if (!r.isObject()) continue;
                const std::string field = std::string(n) == "botAutobot_mc" ? "autobot" : std::string(n) == "botDecepticon_mc" ? "decepticon" : "enemy";
                const int maxV = flow.botMax(field);
                vm.callMethod(r, "createSelectionData", {gfx::avm1::Value(vm.newArray(botChoices(vm, field, maxV, teams)))});
                vm.set(r.o, "currentSelectionIndex", gfx::avm1::Value((double)std::clamp(botValue(b, field), 0, maxV)));
            }
            it->second = kind * 10 + limitKey;
            frontend::FlowTrace::emit("lobby.botRows", {{"refresh", "limits"}, {"extended", frontend::FlowTrace::boolean(b.extended)},
                                                        {"humanFaction", std::to_string(flow.humanFaction())}});
        }
        // Counts the flow changed itself (map-aware Extended counts on a map change): the rows follow the profile.
        for (const char* n : {"botAutobot_mc", "botDecepticon_mc", "botEnemy_mc"}) {
            gfx::avm1::Value r = vm.get(menu, n);
            if (!r.isObject()) continue;
            const std::string field = std::string(n) == "botAutobot_mc" ? "autobot" : std::string(n) == "botDecepticon_mc" ? "decepticon" : "enemy";
            const int maxV = flow.botMax(field), want = std::clamp(botValue(b, field), 0, maxV);
            if ((int)vm.toNumber(vm.get(r.o, "currentSelectionIndex")) == want) continue;
            vm.callMethod(r, "createSelectionData", {gfx::avm1::Value(vm.newArray(botChoices(vm, field, maxV, teams)))});
            vm.set(r.o, "currentSelectionIndex", gfx::avm1::Value((double)want));
            frontend::FlowTrace::emit("lobby.botRows", {{"refresh", field}, {"value", std::to_string(want)}});
        }
        return;
    }
    for (const char* n : kNames) {   // a mode-kind change: the old rows go
        gfx::avm1::Value r = vm.get(menu, n);
        if (r.isObject()) vm.callMethod(r, "removeMovieClip", {});
    }
    gfx::avm1::Value invite = vm.get(menu, "invite_mc");
    botRowsBuilt_[&p] = kind * 10 + limitKey;
    if (kind == (int)GameFlow::BotRows::None) {
        if (invite.isObject()) vm.set(invite.o, "focusDown", gfx::avm1::Value());
        placeBotHint(p, 0);
        return;
    }
    gfx::DisplayObject* src = p.resolveTarget("selectMap_mc", menuD);
    if (!src || !invite.isObject()) return;
    struct Row { const char* name; std::string field, label, hint; };
    std::vector<Row> rows;
    rows.push_back({kNames[0], "extended", "Player Limit", "Original: 10 players (5 v 5). Extended: up to 64 players (PC Custom Game)."});
    if (teams) {
        rows.push_back({kNames[1], "autobot", "Autobot Bots", "AI players on the Autobot team."});
        rows.push_back({kNames[2], "decepticon", "Decepticon Bots", "AI players on the Decepticon team."});
    } else {
        rows.push_back({kNames[3], "enemy", "Bots", "AI opponents in the match."});
    }
    rows.push_back({kNames[4], "difficulty", "Bot Difficulty", "How tough the AI plays."});
    rows.push_back({kNames[5], "ai", "Bot AI", "Smart: the improved bot AI. Classic: the original game's bot behaviour."});   // PC EXTENSION
    const float y0 = vm.toNumber(vm.get(invite.o, "_y")) + 23.0f;
    // The menu column has room for four rows above the original hint line; five (teams + Bot AI) sit 20 apart.
    const float pitch = rows.size() >= 5 ? 20.0f : 23.0f;
    std::string prev = "invite_mc";
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        gfx::avm1::Object* init = vm.newPlain();
        vm.set(init, "text", gfx::avm1::Value(row.label));
        vm.set(init, "_y", gfx::avm1::Value((double)(y0 + pitch * (float)i)));
        gfx::MovieClip* dup = p.duplicate(src, row.name, 900 + (int)i, init);
        if (!dup || !dup->script) continue;
        gfx::avm1::Object* o = dup->script;
        // HmLateralSelector copies its text to label_txt when constructed; the duplicate keeps the source's label.
        vm.set(o, "text", gfx::avm1::Value(row.label));
        gfx::avm1::Value lab = vm.get(o, "label_txt");
        if (lab.isObject()) vm.set(lab.o, "htmlText", gfx::avm1::Value(row.label));
        vm.set(o, "displaySelection", gfx::avm1::Value(true));
        vm.set(o, "loopNavigation", gfx::avm1::Value(false));
        vm.set(o, "focusUp", gfx::avm1::Value(prev));
        vm.set(o, "focusDown", gfx::avm1::Value());
        gfx::avm1::Value prevObj = vm.get(menu, prev);
        if (prevObj.isObject()) vm.set(prevObj.o, "focusDown", gfx::avm1::Value(std::string(row.name)));
        prev = row.name;
        const int maxV = flow.botMax(row.field);
        vm.callMethod(gfx::avm1::Value(o), "createSelectionData", {gfx::avm1::Value(vm.newArray(botChoices(vm, row.field, maxV, teams)))});
        vm.set(o, "currentSelectionIndex", gfx::avm1::Value((double)std::clamp(botValue(b, row.field), 0, maxV)));
        const std::string field = row.field, hint = row.hint;
        vm.set(o, "selectionUpdated", gfx::avm1::Value(vm.newFunction(
            [&flow, field](gfx::avm1::VM& v, const gfx::avm1::Value& self, gfx::avm1::Args&) -> gfx::avm1::Value {
                gfx::avm1::Value d = self.isObject() ? v.get(self.o, "currentSelectionData") : gfx::avm1::Value();
                if (d.isObject()) flow.setBotSetting(field, (int)v.toNumber(v.get(d.o, "Value")));
                return gfx::avm1::Value();
            }, "selectionUpdated", 0)));
        vm.set(o, "onOver", gfx::avm1::Value(vm.newFunction(
            [menu, hint, field, &flow](gfx::avm1::VM& v, const gfx::avm1::Value&, gfx::avm1::Args&) -> gfx::avm1::Value {
                gfx::avm1::Value w = v.get(menu, "HintWidget");
                // the count rows / Player Limit add the selected map's recommendation (map-aware Extended counts)
                std::string text = hint;
                const std::string rec = field == "difficulty" || field == "ai" ? std::string() : flow.botRecommendationText();
                if (!rec.empty()) text = rec + ". " + hint;
                if (w.isObject()) v.set(w.o, "HintText", gfx::avm1::Value(text));
                return gfx::avm1::Value();
            }, "onOver", 0)));
    }
    placeBotHint(p, (int)rows.size());
    frontend::FlowTrace::emit("lobby.botRows", {{"kind", teams ? "teams" : "ffa"}, {"rows", std::to_string(rows.size())},
                                                {"extended", frontend::FlowTrace::boolean(b.extended)}, {"provenance", "PC ADAPTATION"}});
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
    if (hud_) { hud_->player().setViewport((float)viewW_, (float)viewH_); hud_->advance(dt); hudInterpUpdate(*hud_); if (extendedMatch_) checkKillFeed(dt); syncWorldLabels(); }
    if (scoreboard_ && scoreboardShown_) { scoreboard_->advance(dt); scrollPlayerList(scoreboard_->player(), scoreScroll_, in, dt); } else scoreScroll_ = 0.0f;
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
        Extra* target = nullptr;
        for (Extra& e : extras_) if (e.object == pc.movie) target = &e;
        if (!target) { pendingCalls_.erase(pendingCalls_.begin() + (long)i); continue; }
        if (!pc.advanced) { pc.advanced = true; ++i; continue; }
        Args a;
        for (const frontend::BridgeValue& b : pc.args) a.push_back(toValue(target->movie->player().vm(), b));
        target->movie->invoke(pc.fn, a);
        frontend::FlowTrace::emit("gfx.movieCall", {{"movie", pc.movie}, {"fn", pc.fn}});
        pendingCalls_.erase(pendingCalls_.begin() + (long)i);
    }
    frameLimitShown_ = flow.profile().display.frameLimit;
    for (Open& op : movies_) if (op.object.find("GameLobby_GFX") != std::string::npos) syncBotRows(op.movie->player(), flow);
    for (Open& op : movies_) {
        gfx::Player& p = op.movie->player();
        if (!p.attachHook)
            p.attachHook = [this, &p](const std::string& linkage, gfx::avm1::Object* init) {
                addFrameLimitItem(p, linkage, init, frameLimitShown_, [this] { recommendedSettingsPressed(); }, rt_.hdTexturesAvailable());
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
    if (movies_.empty() && !loading_ && !video_ && !(hud_ && hudVisible_) && !(scoreboard_ && scoreboardShown_)) return;
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
    if (cursor_ && !videoOver_) drawMovie(*cursor_);
    gl_.end();
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
