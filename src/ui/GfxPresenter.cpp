#include "ui/GfxPresenter.h"
#include "core/Log.h"
#include "frontend/FlowTrace.h"

#include <algorithm>

namespace ui {

using gfx::avm1::Args;
using gfx::avm1::Value;

namespace {
Value toValue(const frontend::BridgeValue& b) {
    switch (b.kind) {
    case frontend::BridgeValue::Kind::Bool: return Value(b.b);
    case frontend::BridgeValue::Kind::Number: return Value(b.n);
    case frontend::BridgeValue::Kind::String: return Value(b.s);
    default: return Value();
    }
}

// UiKey -> Flash key code, per the movies' KeyListener.getFriendlyKey (XBOX360 branch) [CONFIRMED AS2]:
// arrows navigate, 13 A, 27 B, 112 X, 113 Y, 114 Start, 115 Back, 33 LB, 34 RB, 36 LT, 35 RT, 116/117 stick clicks.
// That the engine sends exactly these codes for the pad buttons is HIGH (native GFx key mapping not traced).
const int kFlashCode[(int)platform::UiKey::Count] = {38, 40, 37, 39, 13, 27, 112, 113, 114, 115, 33, 34, 36, 35, 116, 117};
} // namespace

bool GfxPresenter::init() {
    if (!lib_.load(frontend::Catalog::defaultManifestRoot(), frontend::Catalog::defaultExtractedRoot())) return false;
    // $version prefix = the SKU the movies branch on (HmUtility.Platform). The version digits are UNKNOWN.
    gfx::avm1::VM::defaultVersionString = rt_.platform() + " 8,0,0,0";
    return true;
}

gfx::Player* GfxPresenter::focusPlayer() {
    if (loading_) return &loading_->player();
    return movies_.empty() ? nullptr : &movies_.back().movie->player();
}

bool GfxPresenter::clipWindowCenter(const std::string& path, int& x, int& y) {
    gfx::Player* p = focusPlayer();
    if (!p) return false;
    gfx::DisplayObject* d = p->resolveTarget(path, p->root());
    if (!d || d->removed) return false;
    gfx::Rect b = d->boundsIn(d->worldMatrix());   // stage twips
    gfx::Point c = GfxRendererGL::stageMatrix(p->stageWidth, p->stageHeight, viewW_, viewH_)
                       .apply({(b.xmin + b.xmax) * 0.5f, (b.ymin + b.ymax) * 0.5f});
    x = (int)c.x; y = (int)c.y;
    return true;
}

void GfxPresenter::deliverMouse(const platform::InputFrame& in) {
    // Window pixels -> each movie's stage pixels (the inverse of the draw mapping; stages differ per movie).
    auto toStage = [&](gfx::Player& p, float& sx, float& sy) {
        gfx::Matrix inv = GfxRendererGL::stageMatrix(p.stageWidth, p.stageHeight, viewW_, viewH_).inverse();
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
    for (const Value& v : a) sa.push_back(m.player().vm().toString(v));
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
    if (fn.rfind("Self.", 0) == 0) { frontend::FlowTrace::emit("bridge.unhandled", {{"fn", fn}, {"movie", m.object()}}); return Value(); }
    return toValue(rt_.bridge(m.object(), fn, sa));
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
        loadingUrl_.clear();
    }
}

void GfxPresenter::deliverKeys(const platform::InputFrame& in) {
    uint32_t now = in.uiDown, changed = now ^ prevUi_;
    prevUi_ = now;
    if (!changed || loading_ || movies_.empty()) return;
    GfxMovie* focus = movies_.back().movie.get();
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
    if (cursor_) cursor_->advance(dt);
    if (loading_) { loading_->advance(dt); loadingTime_ += dt; }
    // Movies may open / close others from their scripts: iterate over a snapshot of the objects.
    std::vector<std::string> objs;
    for (const Open& o : movies_) objs.push_back(o.object);
    for (const std::string& o : objs)
        for (Open& op : movies_) if (op.object == o) { op.movie->advance(dt); break; }
    // Data-store change callbacks (HmWidget.updateDSValue(markup, value) by target path).
    for (const auto& c : rt_.dataStores().poll())
        for (Open& op : movies_)
            if (op.object == c.movie) {
                Value r = op.movie->invoke(c.callback, {Value(c.markup), Value(c.value)});
                frontend::FlowTrace::emit("gfx.dsCallback", {{"movie", c.movie}, {"markup", c.markup}, {"value", c.value}, {"callback", c.callback}});
                (void)r;
            }
}

void GfxPresenter::draw(const frontend::GameFlow& flow, int w, int h) {
    (void)flow;
    if (!glReady_) { glReady_ = gl_.init(); if (!glReady_) return; }
    viewW_ = w; viewH_ = h;
    if (movies_.empty() && !loading_ && !video_) return;
    gl_.begin(w, h);
    if (video_ && !videoOver_) gl_.drawVideo(video_, videoW_, videoH_, videoSerial_);
    auto drawMovie = [&](GfxMovie& m) {
        items_.clear();
        gfx::Player& p = m.player();
        p.buildRenderList(GfxRendererGL::stageMatrix(p.stageWidth, p.stageHeight, w, h), items_);
        gl_.draw(items_);
    };
    if (loading_) drawMovie(*loading_);
    else for (Open& o : movies_) drawMovie(*o.movie);
    if (video_ && videoOver_) gl_.drawVideo(video_, videoW_, videoH_, videoSerial_);
    if (cursor_ && !videoOver_) drawMovie(*cursor_);
    gl_.end();
}

void GfxPresenter::injectKey(int code, bool down) {
    if (movies_.empty()) return;
    movies_.back().movie->key(code, down);
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
