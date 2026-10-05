#include "ui/GfxPresenter.h"
#include "core/Log.h"
#include "frontend/FlowTrace.h"

#include <algorithm>
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
    // $version prefix = the SKU the movies branch on (HmUtility.Platform). The version digits are UNKNOWN.
    gfx::avm1::VM::defaultVersionString = rt_.platform() + " 8,0,0,0";
    return true;
}

gfx::Player* GfxPresenter::focusPlayer() {
    if (loading_) return &loading_->player();
    if (scoreboard_) return &scoreboard_->player();
    return movies_.empty() ? nullptr : &movies_.back().movie->player();
}

void GfxPresenter::setHud(bool open, bool visible) {
    if (!open) {
        if (hud_) { rt_.dataStores().forgetMovie(hud_->object()); shapesStale_ = true; }
        hud_.reset(); hudVisible_ = false; return;
    }
    if (!hud_) {
        hud_ = std::make_unique<GfxMovie>();
        bool ok = hud_->open(lib_, &rt_.catalog(), frontend::HudController::kMovie,
                             [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                             [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
        frontend::FlowTrace::emit("gfx.movie", {{"movie", frontend::HudController::kMovie}, {"opened", frontend::FlowTrace::boolean(ok)}});
        if (!ok) { hud_.reset(); return; }
    }
    hudVisible_ = visible;
}

void GfxPresenter::setScoreboard(bool open) {
    if (!open) {
        if (scoreboard_) { rt_.dataStores().forgetMovie(scoreboard_->object()); shapesStale_ = true; }
        scoreboard_.reset();
        return;
    }
    if (scoreboard_) return;
    scoreboard_ = std::make_unique<GfxMovie>();
    bool ok = scoreboard_->open(lib_, &rt_.catalog(), "UI_GFxInGameStats_p.InGameStats_GFX_1",
                                [this](GfxMovie& mv, const std::string& fn, Args& a) { return bridge(mv, fn, a); },
                                [this](GfxMovie& mv, const std::string& c, const std::string& a) { fsCommand(mv, c, a); });
    frontend::FlowTrace::emit("gfx.movie", {{"movie", "UI_GFxInGameStats_p.InGameStats_GFX_1"}, {"opened", frontend::FlowTrace::boolean(ok)}});
    if (!ok) scoreboard_.reset();
}

void GfxPresenter::hudCall(const std::string& fn, const std::vector<frontend::BridgeValue>& args) {
    if (!hud_) return;
    Args a;
    for (const frontend::BridgeValue& b : args) a.push_back(toValue(hud_->player().vm(), b));
    hud_->invoke(fn, a);
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
    GfxMovie* focus = scoreboard_ ? scoreboard_.get() : (movies_.empty() ? nullptr : movies_.back().movie.get());
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
    r.push_back({"scoreboard", scoreboard_ ? "1" : "0"});
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

void GfxPresenter::deliverKeys(const platform::InputFrame& in) {
    uint32_t now = in.uiDown, changed = now ^ prevUi_;
    prevUi_ = now;
    if (loading_ || (movies_.empty() && !scoreboard_)) return;
    GfxMovie* focus = scoreboard_ ? scoreboard_.get() : movies_.back().movie.get();
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

void GfxPresenter::update(frontend::GameFlow& flow, const platform::InputFrame& in, float dt) {
    syncMovies(flow);
    syncPopup(flow);
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
    if (hud_) { hud_->player().setViewport((float)viewW_, (float)viewH_); hud_->advance(dt); hudInterpUpdate(*hud_); }
    if (scoreboard_) scoreboard_->advance(dt);
    if (loading_) { loading_->advance(dt); loadingTime_ += dt; }
    // Movies may open / close others from their scripts: iterate over a snapshot of the objects.
    std::vector<std::string> objs;
    for (const Open& o : movies_) objs.push_back(o.object);
    // Every movie gets the viewport (Stage.width / height and onResize: the menus size their backgrounds from it).
    for (Open& op : movies_) op.movie->player().setViewport((float)viewW_, (float)viewH_);
    for (Extra& e : extras_) e.movie->player().setViewport((float)viewW_, (float)viewH_);
    if (scoreboard_) scoreboard_->player().setViewport((float)viewW_, (float)viewH_);
    if (loading_) loading_->player().setViewport((float)viewW_, (float)viewH_);
    for (const std::string& o : objs)
        for (Open& op : movies_) if (op.object == o) { op.movie->advance(dt); break; }
    // Movie-opened movies: closes requested during their own script run are applied here; advance the rest.
    for (const std::string& o : deferredErase_)
        for (size_t i = extras_.size(); i-- > 0;) if (extras_[i].object == o) { extras_.erase(extras_.begin() + (long)i); shapesStale_ = true; }
    deferredErase_.clear();
    for (Extra& e : extras_) e.movie->advance(dt);
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
        if (scoreboard_ && scoreboard_->object() == c.movie) { scoreboard_->invoke(c.callback, {Value(c.markup), Value(c.value)}); continue; }
        for (Open& op : movies_)
            if (op.object == c.movie) {
                Value r = op.movie->invoke(c.callback, {Value(c.markup), Value(c.value)});
                frontend::FlowTrace::emit("gfx.dsCallback", {{"movie", c.movie}, {"markup", c.markup}, {"value", c.value}, {"callback", c.callback}});
                (void)r;
            }
    }
}

void GfxPresenter::draw(const frontend::GameFlow& flow, int w, int h) {
    (void)flow;
    if (!glReady_) { glReady_ = gl_.init(); if (!glReady_) return; }
    viewW_ = w; viewH_ = h;
    gfx::Player::hostViewportW = (float)w;   // a noScale movie opened later lays out for this viewport
    gfx::Player::hostViewportH = (float)h;
    if (shapesStale_) { gl_.forgetShapes(); shapesStale_ = false; }
    if (movies_.empty() && !loading_ && !video_ && !(hud_ && hudVisible_) && !scoreboard_) return;
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
        if (scoreboard_) drawMovie(*scoreboard_);
        for (Open& o : movies_) drawMovie(*o.movie);
        for (Extra& e : extras_) drawMovie(*e.movie);
    }
    if (video_ && videoOver_) gl_.drawVideo(video_, videoW_, videoH_, videoSerial_);
    if (cursor_ && !videoOver_) drawMovie(*cursor_);
    gl_.end();
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
