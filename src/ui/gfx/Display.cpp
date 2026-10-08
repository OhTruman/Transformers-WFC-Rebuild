// GFx display list, timelines and player (SWF 8 / AS2 execution order).
#include "ui/gfx/Display.h"
#include "core/FrameProfile.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gfx {

using avm1::Args;
using avm1::Object;
using avm1::Value;

namespace {
enum ClipEvent : uint32_t {
    EvLoad = 1u << 0, EvEnterFrame = 1u << 1, EvUnload = 1u << 2, EvMouseMove = 1u << 3, EvMouseDown = 1u << 4,
    EvMouseUp = 1u << 5, EvKeyDown = 1u << 6, EvKeyUp = 1u << 7, EvInitialize = 1u << 9, EvConstruct = 1u << 18,
};

// Nonzero winding of the shape's fill edges at p (local twips), per fill style; strokes by distance to the segment.
bool pointInShape(const ShapeDef* s, const Point& p) {
    if (!s) return false;
    const Rect& b = s->bounds;
    const float slop = 40.0f;   // strokes extend past the edge bounds by up to half their width
    if (p.x < b.xmin - slop || p.x > b.xmax + slop || p.y < b.ymin - slop || p.y > b.ymax + slop) return false;
    std::map<std::pair<int, int>, int> wind;
    auto isLeft = [](const Point& a, const Point& c, const Point& q) { return (c.x - a.x) * (q.y - a.y) - (q.x - a.x) * (c.y - a.y); };
    auto cross = [&](const Point& a, const Point& c) -> int {
        if (a.y <= p.y) { if (c.y > p.y && isLeft(a, c, p) > 0) return 1; }
        else if (c.y <= p.y && isLeft(a, c, p) < 0) return -1;
        return 0;
    };
    for (const ShapePath& path : s->paths) {
        for (size_t i = 1; i < path.pts.size(); ++i) {
            const Point& a = path.pts[i - 1];
            const Point& c = path.pts[i];
            if (path.fill1) wind[{path.styleSet, path.fill1}] += cross(a, c);
            if (path.fill0) wind[{path.styleSet, path.fill0}] += cross(c, a);
            if (path.line > 0) {
                float hw = 10.0f;
                if ((size_t)path.styleSet < s->lineSets.size() && (size_t)(path.line - 1) < s->lineSets[(size_t)path.styleSet].size())
                    hw = std::max(10.0f, s->lineSets[(size_t)path.styleSet][(size_t)(path.line - 1)].width * 0.5f);
                float dx = c.x - a.x, dy = c.y - a.y, l2 = dx * dx + dy * dy;
                float t = l2 > 0 ? std::max(0.0f, std::min(1.0f, ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2)) : 0.0f;
                float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
                if (ex * ex + ey * ey <= hw * hw) return true;
            }
        }
    }
    for (const auto& [k, w] : wind) if (w != 0) return true;
    return false;
}

bool pointInRect(const Rect& r, const Point& p) { return p.x >= r.xmin && p.x <= r.xmax && p.y >= r.ymin && p.y <= r.ymax; }

Rect transformRect(const Rect& r, const Matrix& m) {
    Point p[4] = {m.apply({r.xmin, r.ymin}), m.apply({r.xmax, r.ymin}), m.apply({r.xmin, r.ymax}), m.apply({r.xmax, r.ymax})};
    Rect o{p[0].x, p[0].y, p[0].x, p[0].y};
    for (auto& q : p) { o.xmin = std::min(o.xmin, q.x); o.ymin = std::min(o.ymin, q.y); o.xmax = std::max(o.xmax, q.x); o.ymax = std::max(o.ymax, q.y); }
    return o;
}

bool rectValid(const Rect& r) { return !(r.xmin == 0 && r.xmax == 0 && r.ymin == 0 && r.ymax == 0); }

Rect unionRect(const Rect& a, const Rect& b) {
    if (!rectValid(a)) return b;
    if (!rectValid(b)) return a;
    return {std::min(a.xmin, b.xmin), std::min(a.ymin, b.ymin), std::max(a.xmax, b.xmax), std::max(a.ymax, b.ymax)};
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// DisplayObject

void DisplayObject::syncComponents() {
    if (componentsValid) return;
    xscale = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b) * 100.0f;
    yscale = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d) * 100.0f;
    if (matrix.a * matrix.d - matrix.b * matrix.c < 0) yscale = -yscale;
    rotationDeg = std::atan2(matrix.b, matrix.a) * 180.0f / 3.14159265358979f;
    componentsValid = true;
}

void DisplayObject::applyComponents() {
    float r = rotationDeg * 3.14159265358979f / 180.0f;
    float sx = xscale / 100.0f, sy = yscale / 100.0f;
    float cs = std::cos(r), sn = std::sin(r);
    matrix.a = sx * cs; matrix.b = sx * sn;
    matrix.c = -sy * sn; matrix.d = sy * cs;
    componentsValid = true;
}

Rect DisplayObject::boundsIn(const Matrix& m) const {
    if (kind == Kind::Clip) {
        const auto* mc = static_cast<const MovieClip*>(this);
        Rect r;
        for (const auto& [d, ch] : mc->children) {
            if (ch->clipDepth) continue;
            r = unionRect(r, ch->boundsIn(m * ch->matrix));
        }
        if (mc->drawing && rectValid(mc->drawing->bounds)) r = unionRect(r, transformRect(mc->drawing->bounds, m));
        return r;
    }
    Rect lb = localBounds();
    if (!rectValid(lb)) return {};
    return transformRect(lb, m);
}

Matrix DisplayObject::worldMatrix() const {
    Matrix m = matrix;
    for (const DisplayObject* p = parent; p; p = p->parent) m = p->matrix * m;
    return m;
}

CXForm DisplayObject::worldCx() const {
    CXForm c = cx;
    for (const DisplayObject* p = parent; p; p = p->parent) c = p->cx * c;
    return c;
}

bool DisplayObject::worldVisible() const {
    for (const DisplayObject* p = this; p; p = p->parent) if (!p->visible) return false;
    return true;
}

std::string DisplayObject::targetPath() const {
    if (!parent) return "_level0";
    return parent->targetPath() + "." + name;
}

std::string DisplayObject::slashPath() const {
    if (!parent) return "/";
    std::string p = parent->slashPath();
    return (p == "/" ? "" : p) + "/" + name;
}

MovieClip* DisplayObject::rootClip() {
    DisplayObject* d = this;
    while (d->parent) d = d->parent;
    return d->kind == Kind::Clip ? static_cast<MovieClip*>(d) : nullptr;
}

DisplayObject* MovieClip::childAtDepth(int d) const {
    auto it = children.find(d);
    return it == children.end() ? nullptr : it->second.get();
}

DisplayObject* MovieClip::childByName(const std::string& n) const {
    if (n.empty()) return nullptr;
    for (const auto& [d, ch] : children) if (ch->name == n) return ch.get();
    return nullptr;
}

int MovieClip::nextHighestDepth() const {
    int best = 0;
    for (const auto& [d, ch] : children) best = std::max(best, d - kDepthOffset + 1);
    return best;
}

Rect MovieClip::localBounds() const { return boundsIn(Matrix{}); }

// ---------------------------------------------------------------------------------------------------------------
// Player

float Player::hostViewportW = 0, Player::hostViewportH = 0;

void Player::setViewport(float w, float h) {
    // onResize when the visible area changes (every scale mode: the menus lay out their full-screen backgrounds from it),
    // and for listeners registered since the last notification (a menu adds its listener in its first frame, after
    // the host has set the viewport) [GFx behaviour, HIGH].
    bool changed = w != viewportW || h != viewportH;
    if (!changed && resizeNotified_ >= stageListeners.size()) return;
    viewportW = w;
    viewportH = h;
    resizeNotified_ = stageListeners.size();
    std::vector<avm1::Object*> ls = stageListeners;
    for (avm1::Object* l : ls) {
        try {
            vm_->callMethod(avm1::Value(l), "onResize", {});
        } catch (const avm1::ScriptThrow& t) {
            LOG_WARN("GFX Stage onResize threw: %s", vm_->toString(t.v).c_str());
        }
        drainActions();
    }
}

Player::Player() : vm_(std::make_unique<avm1::VM>(this)) {
    vm_->traceSink = [this](const std::string& m) { LOG_INFO("GFX trace [%s] %s", movieName.c_str(), m.c_str()); };
}

Player::~Player() = default;

namespace {
// Parsed movies are immutable after load: one process-wide cache, so reopening a movie (the respawn screen on every
// death, its ButtonIcons / Fonts_EFIGS imports) parses nothing and the renderer's shape caches stay valid.
std::map<std::string, std::shared_ptr<MovieDef>>& sharedDefs() {
    static std::map<std::string, std::shared_ptr<MovieDef>> defs;
    return defs;
}
}

std::shared_ptr<const MovieDef> Player::loadDef(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = defs_.find(path);
    if (it != defs_.end()) return it->second;
    auto& shared = sharedDefs();
    auto sh = shared.find(path);
    if (sh != shared.end()) { defs_[path] = sh->second; return sh->second; }
    auto d = std::make_shared<MovieDef>();
    if (!d->load(path)) { defs_[path] = nullptr; shared[path] = nullptr; return nullptr; }
    defs_[path] = d;
    shared[path] = d;
    return d;
}

bool Player::loadRoot(const std::string& path) {
    rootDef_ = loadDef(path);
    if (!rootDef_) return false;
    movieName = rootDef_->baseName();
    rootOwner_ = std::make_unique<MovieClip>(this);
    root_ = rootOwner_.get();
    root_->def = rootDef_;
    root_->sprite = &rootDef_->root;
    root_->isRootOfMovie = true;
    root_->name = "";
    root_->script = vm_->newClipObject(root_, vm_->movieClipProto);
    stageWidth = (rootDef_->stage.xmax - rootDef_->stage.xmin) / 20.0f;
    stageHeight = (rootDef_->stage.ymax - rootDef_->stage.ymin) / 20.0f;
    vm_->swfVersion = rootDef_->version;
    root_->frame = -1;
    applyFrameTags(root_, 0, true);
    root_->frame = 0;
    drainActions();
    return true;
}

avm1::Object* Player::scriptObject(DisplayObject* d) {
    if (!d) return nullptr;
    if (!d->script) {
        Object* proto = d->kind == DisplayObject::Kind::Text ? vm_->textFieldProto : vm_->movieClipProto;
        d->script = vm_->newClipObject(d, proto);
    }
    return d->script;
}

// ---- library resolution ----

bool Player::resolveCharacter(const std::shared_ptr<const MovieDef>& def, uint16_t id, std::shared_ptr<const MovieDef>& outDef,
                              uint16_t& outId, int depthGuard) {
    if (!def || depthGuard > 8) return false;
    const CharDef* c = def->character(id);
    if (!c) return false;
    if (c->type != CharType::Imported) { outDef = def; outId = id; return true; }
    std::string path = resolveMovieUrl ? resolveMovieUrl(c->importUrl, def->dir()) : "";
    if (path.empty()) return false;
    std::shared_ptr<const MovieDef> lib = loadDef(path);
    if (!lib) return false;
    // A shared library's class definitions (DoInitAction) run when its symbols are first used.
    if (!initRun_.count(lib.get())) {
        initRun_[lib.get()];
        for (const Frame& f : lib->root.frames)
            for (const auto& [sid, ab] : f.initActions) {
                if (initRun_[lib.get()].insert(sid).second) vm_->runBlock(ab.code, 0, ab.code->size(), root_);
            }
    }
    auto it = lib->exports.find(c->importName);
    if (it == lib->exports.end()) return false;
    return resolveCharacter(lib, it->second, outDef, outId, depthGuard + 1);
}

bool Player::resolveExport(const std::shared_ptr<const MovieDef>& def, const std::string& name,
                           std::shared_ptr<const MovieDef>& outDef, uint16_t& outId) {
    if (!def) return false;
    auto it = def->exports.find(name);
    if (it != def->exports.end()) return resolveCharacter(def, it->second, outDef, outId);
    for (const ImportDef& im : def->imports)
        if (im.name == name) return resolveCharacter(def, im.id, outDef, outId);
    // GFx: attachMovie also finds the exports of the libraries this movie imports (e.g. SharedIcons, SharedComponents).
    std::set<std::string> urls;
    for (const ImportDef& im : def->imports) {
        if (!urls.insert(im.url).second) continue;
        std::string path = resolveMovieUrl ? resolveMovieUrl(im.url, def->dir()) : "";
        std::shared_ptr<const MovieDef> lib = path.empty() ? nullptr : loadDef(path);
        if (!lib) continue;
        auto e = lib->exports.find(name);
        if (e == lib->exports.end()) continue;
        if (!initRun_.count(lib.get())) {
            initRun_[lib.get()];
            for (const Frame& f : lib->root.frames)
                for (const auto& [sid, ab] : f.initActions)
                    if (initRun_[lib.get()].insert(sid).second) vm_->runBlock(ab.code, 0, ab.code->size(), root_);
        }
        return resolveCharacter(lib, e->second, outDef, outId);
    }
    return false;
}

bool Player::findExportedBitmap(const std::string& linkage, std::shared_ptr<const MovieDef>& outDef, uint16_t& outId) {
    auto isBitmap = [](const std::shared_ptr<const MovieDef>& d, uint16_t id) {
        const CharDef* c = d ? d->character(id) : nullptr;
        return c && c->type == CharType::Bitmap;
    };
    if (resolveExport(rootDef_, linkage, outDef, outId) && isBitmap(outDef, outId)) return true;
    std::vector<std::shared_ptr<const MovieDef>> loaded;
    for (auto& [k, d] : defs_) if (d) loaded.push_back(d);
    for (auto& d : loaded) if (resolveExport(d, linkage, outDef, outId) && isBitmap(outDef, outId)) return true;
    // Libraries imported by the loaded movies (e.g. ButtonIcons.swf through SharedComponents).
    for (auto& d : loaded)
        for (const ImportDef& im : d->imports) {
            std::string path = resolveMovieUrl ? resolveMovieUrl(im.url, d->dir()) : "";
            std::shared_ptr<const MovieDef> lib = path.empty() ? nullptr : loadDef(path);
            if (lib && resolveExport(lib, linkage, outDef, outId) && isBitmap(outDef, outId)) return true;
        }
    return false;
}

const FontDef* Player::resolveFont(const std::string& nameIn, bool bold, bool italic, const MovieDef* def) {
    std::string name = nameIn;
    auto fm = fontMap.find(name);
    if (fm != fontMap.end()) name = fm->second;
    if (def) { int i = def->findFont(name, bold, italic); if (i >= 0) return &def->fonts[(size_t)i]; }
    if (!fontLibPath.empty()) {
        std::shared_ptr<const MovieDef> lib = loadDef(fontLibPath);
        if (lib) {
            int i = lib->findFont(name, bold, italic);
            if (i >= 0) return &lib->fonts[(size_t)i];
            if (!lib->fonts.empty() && !name.empty()) {
                static std::set<std::string> warned;
                if (warned.insert(name).second) LOG_WARN("GFX font '%s' not in the font library; using '%s'", name.c_str(), lib->fonts[0].name.c_str());
                return &lib->fonts[0];
            }
        }
    }
    if (def && !def->fonts.empty()) return &def->fonts[0];
    return nullptr;
}

std::string Player::translate(const std::string& text) const {
    if (text.size() > 1 && text[0] == '$' && translator) {
        std::string t = translator(text);
        if (!t.empty()) return t;
    }
    return text;
}

// ---- instantiation ----

DisplayObject* Player::instantiate(MovieClip* parent, const std::shared_ptr<const MovieDef>& defIn, uint16_t charIdIn, int depth,
                                   const std::string& name, bool scripted) {
    std::shared_ptr<const MovieDef> def;
    uint16_t charId = 0;
    if (!resolveCharacter(defIn, charIdIn, def, charId)) {
        LOG_WARN("GFX %s: character %u not resolved", defIn ? defIn->baseName().c_str() : "?", charIdIn);
        return nullptr;
    }
    const CharDef* cd = def->character(charId);
    std::unique_ptr<DisplayObject> obj;
    switch (cd->type) {
    case CharType::Sprite: {
        auto mc = std::make_unique<MovieClip>(this);
        mc->sprite = &def->sprites[(size_t)cd->index];
        obj = std::move(mc);
        break;
    }
    case CharType::Shape: {
        auto s = std::make_unique<ShapeInstance>(this);
        s->shape = &def->shapes[(size_t)cd->index];
        obj = std::move(s);
        break;
    }
    case CharType::Morph: {
        auto s = std::make_unique<ShapeInstance>(this);
        s->morph = def->morphs[(size_t)cd->index].get();
        obj = std::move(s);
        break;
    }
    case CharType::StaticText: {
        auto s = std::make_unique<StaticTextInstance>(this);
        s->text = &def->staticTexts[(size_t)cd->index];
        obj = std::move(s);
        break;
    }
    case CharType::Bitmap: {
        auto b = std::make_unique<BitmapInstance>(this);
        b->bitmap = &def->bitmaps[(size_t)cd->index];
        std::string ext = externalTexture ? externalTexture(b->bitmap->exportName) : "";
        b->path = ext.empty() ? b->bitmap->resolvedPath : ext;
        b->width = b->bitmap->targetWidth;
        b->height = b->bitmap->targetHeight;
        obj = std::move(b);
        break;
    }
    case CharType::EditText: {
        auto t = std::make_unique<TextField>(this);
        const EditTextDef& e = def->editTexts[(size_t)cd->index];
        t->edit = &e;
        t->bounds = e.bounds;
        t->html = e.html;
        t->multiline = e.multiline;
        t->wordWrap = e.wordWrap;
        t->selectable = !e.noSelect;
        t->border = e.border;
        t->password = e.password;
        t->autoSize = e.autoSize ? "left" : "none";
        t->variable = e.variable;
        t->maxChars = e.maxLength;
        t->inputType = !e.readOnly;
        TextFormatSpan& f = t->newFormat;
        f.size = e.height / 20.0f;
        if (e.hasColor) f.color = e.color;
        f.align = e.align;
        f.leftMargin = e.leftMargin / 20.0f; f.rightMargin = e.rightMargin / 20.0f;
        f.indent = e.indent / 20.0f; f.leading = e.leading / 20.0f;
        if (e.hasFont) {
            const CharDef* fc = def->character(e.fontId);
            if (fc && fc->type == CharType::Font) f.font = def->fonts[(size_t)fc->index].name;
            else if (fc && fc->type == CharType::Imported) f.font = fc->importName;
        } else if (e.hasFontClass) f.font = e.fontClass;
        t->def = def;
        t->formats = {f};
        if (e.hasText) {
            std::string txt = translate(e.initialText);
            if (e.html) t->setHtmlText(txt); else t->setPlainText(txt);
            if (e.initialText.size() > 1 && e.initialText[0] == '$') { t->srcKey = e.initialText; t->srcHtml = e.html; }
        }
        obj = std::move(t);
        break;
    }
    default:
        return nullptr;
    }
    obj->def = def;
    obj->charId = charId;
    obj->depth = depth;
    obj->name = name;
    obj->parent = parent;
    obj->scripted = scripted;
    DisplayObject* raw = obj.get();
    auto it = parent->children.find(depth);
    if (it != parent->children.end()) { unloadClip(it->second.get()); graveyard.push_back(std::move(it->second)); parent->children.erase(it); }
    parent->children[depth] = std::move(obj);
    if (raw->kind == DisplayObject::Kind::Clip) {
        // Registered AS2 class (Object.registerClass(linkage, ctor)) sets the instance prototype.
        auto en = def->exportNames.find(charId);
        Object* proto = vm_->movieClipProto;
        if (en != def->exportNames.end()) {
            auto rc = vm_->registeredClasses.find(en->second);
            if (std::getenv("WFC_GFX_CLASSLOG")) LOG_INFO("GFX place %s char %d linkage %s class %s", name.c_str(), (int)charId, en->second.c_str(), rc != vm_->registeredClasses.end() ? "yes" : "NO");
            if (rc != vm_->registeredClasses.end()) {
                Value p = vm_->get(rc->second, "prototype");
                if (p.isObject()) proto = p.o;
            }
        }
        raw->script = vm_->newClipObject(raw, proto);
    }
    return raw;
}

void Player::constructClip(MovieClip* mc, Object* initObj) {
    if (mc->constructed) return;
    mc->constructed = true;
    Object* so = scriptObject(mc);
    if (initObj)
        for (const std::string& k : vm_->enumerate(initObj)) vm_->set(so, k, vm_->get(initObj, k));
    // onClipEvent(initialize/construct), then the registered class constructor (proto.constructor).
    for (const ClipAction& ca : mc->clipActions)
        if (ca.events & (EvInitialize | EvConstruct)) vm_->runBlock(ca.code.code, 0, ca.code.code->size(), mc);
    auto en = mc->def ? mc->def->exportNames.find(mc->charId) : std::map<uint16_t, std::string>::const_iterator{};
    if (mc->def && en != mc->def->exportNames.end()) {
        auto rc = vm_->registeredClasses.find(en->second);
        if (rc != vm_->registeredClasses.end()) {
            Args a;
            Value pr = vm_->get(rc->second, "prototype");
            so->setRaw("__constructor__", Value(rc->second), avm1::DontEnum);
            try {
                vm_->call(Value(rc->second), Value(so), a, pr.isObject() ? pr.o->proto : nullptr);
            } catch (const avm1::ScriptThrow& t) {
                LOG_WARN("GFX constructor threw: %s", vm_->toString(t.v).c_str());
            }
        }
    }
}

void Player::placeObject(MovieClip* mc, const PlaceCmd& pc, int frame) {
    DisplayObject* existing = mc->childAtDepth(pc.depth);
    if (pc.move && existing && !pc.hasChar) {
        if (pc.hasMatrix) { existing->matrix = pc.m; existing->componentsValid = false; }
        if (pc.hasCx) existing->cx = pc.cx;
        if (pc.hasRatio) existing->ratio = pc.ratio;
        if (pc.hasName) existing->name = pc.name;
        if (pc.hasClipDepth) existing->clipDepth = pc.clipDepth;
        if (pc.hasBlend) existing->blend = pc.blend;
        return;
    }
    if (!pc.hasChar) return;
    Matrix keepM = existing ? existing->matrix : Matrix{};
    CXForm keepC = existing ? existing->cx : CXForm{};
    std::string keepName = existing ? existing->name : std::string();
    DisplayObject* d = instantiate(mc, mc->def, pc.charId, pc.depth, pc.hasName ? pc.name : keepName, false);
    if (!d) return;
    d->placeFrame = frame;
    // Flash names unnamed timeline movie clips "instanceN" (targetPath / dot paths rely on it).
    if (d->name.empty() && d->kind == DisplayObject::Kind::Clip) d->name = "instance" + std::to_string(++instanceCounter_);
    d->matrix = pc.hasMatrix ? pc.m : (pc.move ? keepM : Matrix{});
    d->cx = pc.hasCx ? pc.cx : (pc.move ? keepC : CXForm{});
    d->ratio = pc.ratio;
    d->clipDepth = pc.hasClipDepth ? pc.clipDepth : 0;
    d->blend = pc.blend;
    if (d->kind == DisplayObject::Kind::Clip) {
        auto* child = static_cast<MovieClip*>(d);
        child->clipActions = pc.clipActions;
        for (const ClipAction& ca : pc.clipActions) child->clipEventMask |= ca.events;
        // Its first frame: children first, then (depth-first) constructors, then queued frame actions / onLoad.
        child->frame = -1;
        applyFrameTags(child, 0, true);
        child->frame = 0;
        constructClip(child, nullptr);
        queueAction([this, child]() { if (!child->removed) dispatchClipEvent(child, "onLoad", EvLoad); });
    }
}

void Player::applyFrameTags(MovieClip* mc, int frame, bool runActions) {
    if (!mc->sprite || frame < 0 || frame >= (int)mc->sprite->frames.size()) return;
    const Frame& fr = mc->sprite->frames[(size_t)frame];
    // DoInitAction: once per definition and sprite id, before the frame's display list and actions.
    for (const auto& [sid, ab] : fr.initActions) {
        auto& done = initRun_[mc->def.get()];
        if (done.insert(sid).second) vm_->runBlock(ab.code, 0, ab.code->size(), mc);
    }
    // The frame's DoActions are queued before the children it places queue theirs: the parent's frame script runs
    // first (with its new children already instantiated), then the children's first frames - Flash 8 order (e.g.
    // EndGameStats_GFX: each XP panel's script sets SpecialtyFriendlyName that its title child reads via _parent).
    if (runActions) {
        for (const ActionBlock& ab : fr.actions) {
            auto code = ab.code;
            queueAction([this, mc, code]() { if (!mc->removed) vm_->runBlock(code, 0, code->size(), mc); });
        }
    }
    for (const ControlTag& ct : fr.tags) {
        if (ct.kind == ControlTag::Place) placeObject(mc, ct.place, frame);
        else {
            auto it = mc->children.find(ct.depth);
            if (it != mc->children.end() && it->first < kDepthOffset) {
                unloadClip(it->second.get());
                graveyard.push_back(std::move(it->second));
                mc->children.erase(it);
            }
        }
    }
}

int Player::resolveFrame(MovieClip* mc, const Value& f) {
    if (!mc) return -1;
    if (f.t == avm1::VType::Number) {
        double n = f.n;
        if (std::isnan(n)) return -1;
        return std::max(0, (int)n - 1);
    }
    std::string s = vm_->toString(f);
    if (mc->sprite) {
        auto it = mc->sprite->labels.find(s);
        if (it != mc->sprite->labels.end()) return it->second;
    }
    char* end = nullptr;
    long n = std::strtol(s.c_str(), &end, 10);
    if (end && *end == 0 && !s.empty()) return std::max(0, (int)n - 1);
    return -1;
}

void Player::gotoFrame(MovieClip* mc, int target, bool play) {
    if (!mc) return;
    mc->playing = play;
    int total = mc->totalFrames();
    if (!mc->sprite) return;
    target = std::max(0, std::min(total - 1, target));
    if (target == mc->frame) return;
    if (target > mc->frame) {
        for (int f = mc->frame + 1; f <= target; ++f) applyFrameTags(mc, f, f == target);
        mc->frame = target;
        return;
    }
    // Backward: rebuild the timeline-placed children from frame 0 up to the target frame. Instances whose placement
    // (depth, character, placing frame) is unchanged keep their state.
    struct State { PlaceCmd pc; int placed = -1; bool live = false; };
    std::map<int, State> st;
    for (int f = 0; f <= target && f < (int)mc->sprite->frames.size(); ++f) {
        for (const ControlTag& ct : mc->sprite->frames[(size_t)f].tags) {
            if (ct.kind == ControlTag::Remove) { st.erase(ct.depth); continue; }
            const PlaceCmd& p = ct.place;
            State& s = st[p.depth];
            if (p.hasChar && (!p.move || !s.live || s.pc.charId != p.charId)) {
                PlaceCmd n = p;
                if (p.move && s.live) { if (!p.hasMatrix) { n.m = s.pc.m; n.hasMatrix = s.pc.hasMatrix; } if (!p.hasCx) { n.cx = s.pc.cx; n.hasCx = s.pc.hasCx; } }
                s.pc = n; s.placed = f; s.live = true;
            } else if (s.live) {
                if (p.hasMatrix) { s.pc.m = p.m; s.pc.hasMatrix = true; }
                if (p.hasCx) { s.pc.cx = p.cx; s.pc.hasCx = true; }
                if (p.hasName) { s.pc.name = p.name; s.pc.hasName = true; }
                if (p.hasRatio) s.pc.ratio = p.ratio;
                if (p.hasClipDepth) { s.pc.clipDepth = p.clipDepth; s.pc.hasClipDepth = true; }
            }
        }
    }
    std::vector<int> toRemove;
    for (auto& [d, ch] : mc->children) {
        if (d >= kDepthOffset) continue;
        auto it = st.find(d);
        if (it == st.end() || it->second.placed != ch->placeFrame || it->second.pc.charId != ch->charId) toRemove.push_back(d);
    }
    for (int d : toRemove) {
        auto it = mc->children.find(d);
        unloadClip(it->second.get());
        graveyard.push_back(std::move(it->second));
        mc->children.erase(it);
    }
    for (auto& [d, s] : st) {
        DisplayObject* ch = mc->childAtDepth(d);
        if (ch) {
            if (s.pc.hasMatrix) { ch->matrix = s.pc.m; ch->componentsValid = false; }
            if (s.pc.hasCx) ch->cx = s.pc.cx;
            continue;
        }
        PlaceCmd pc = s.pc;
        pc.move = false;
        placeObject(mc, pc, s.placed);
    }
    mc->frame = target;
    for (const ActionBlock& ab : mc->sprite->frames[(size_t)target].actions) {
        auto code = ab.code;
        queueAction([this, mc, code]() { if (!mc->removed) vm_->runBlock(code, 0, code->size(), mc); });
    }
}

void Player::queueFrameActions(MovieClip* mc) {
    if (!mc->sprite || mc->frame < 0 || mc->frame >= (int)mc->sprite->frames.size()) return;
    for (const ActionBlock& ab : mc->sprite->frames[(size_t)mc->frame].actions) {
        auto code = ab.code;
        queueAction([this, mc, code]() { if (!mc->removed) vm_->runBlock(code, 0, code->size(), mc); });
    }
}

void Player::runInitActions(MovieClip* mc, int frame) { (void)mc; (void)frame; }

void Player::drainActions() {
    core::prof::Scope prof("gfx.actions");
    int guard = 0;
    while (!actionQueue_.empty() && guard++ < 100000) {
        auto fn = std::move(actionQueue_.front());
        actionQueue_.pop_front();
        fn();
    }
}

// ---- script-created instances ----

MovieClip* Player::attachMovie(MovieClip* parent, const std::string& linkage, const std::string& name, int asDepth, Object* initObj) {
    std::shared_ptr<const MovieDef> def;
    uint16_t id = 0;
    if (!resolveExport(parent->def, linkage, def, id) && !resolveExport(rootDef_, linkage, def, id)) {
        LOG_WARN("GFX attachMovie: linkage '%s' not found", linkage.c_str());
        return nullptr;
    }
    const CharDef* cd = def->character(id);
    if (!cd || cd->type != CharType::Sprite) return nullptr;
    // instantiate() resolves within `def`; place it with the defining library.
    DisplayObject* d = instantiate(parent, def, id, asDepth + kDepthOffset, name, true);
    if (!d || d->kind != DisplayObject::Kind::Clip) return nullptr;
    auto* mc = static_cast<MovieClip*>(d);
    mc->frame = -1;
    // initObject properties and the registered class constructor run before the clip's first frame actions.
    applyFrameTags(mc, 0, true);
    mc->frame = 0;
    constructClip(mc, initObj);
    if (std::getenv("WFC_GFX_CLASSLOG")) LOG_INFO("GFX attachMovie %s as %s depth %d -> %s", linkage.c_str(), name.c_str(), asDepth, mc->targetPath().c_str());
    queueAction([this, mc]() { if (!mc->removed) dispatchClipEvent(mc, "onLoad", EvLoad); });
    return mc;
}

MovieClip* Player::createEmptyMovieClip(MovieClip* parent, const std::string& name, int asDepth) {
    auto mc = std::make_unique<MovieClip>(this);
    MovieClip* raw = mc.get();
    raw->def = parent->def;
    raw->depth = asDepth + kDepthOffset;
    raw->name = name;
    raw->parent = parent;
    raw->scripted = true;
    raw->frame = 0;
    raw->constructed = true;
    auto it = parent->children.find(raw->depth);
    if (it != parent->children.end()) { unloadClip(it->second.get()); graveyard.push_back(std::move(it->second)); parent->children.erase(it); }
    parent->children[raw->depth] = std::move(mc);
    raw->script = vm_->newClipObject(raw, vm_->movieClipProto);
    return raw;
}

TextField* Player::createTextField(MovieClip* parent, const std::string& name, int asDepth, float x, float y, float w, float h) {
    auto t = std::make_unique<TextField>(this);
    TextField* raw = t.get();
    raw->def = parent->def;
    raw->depth = asDepth + kDepthOffset;
    raw->name = name;
    raw->parent = parent;
    raw->scripted = true;
    raw->matrix.tx = x * 20; raw->matrix.ty = y * 20;
    raw->bounds = {0, 0, w * 20, h * 20};
    raw->newFormat.font = "Times New Roman";
    raw->newFormat.size = 12;
    raw->formats = {raw->newFormat};
    auto it = parent->children.find(raw->depth);
    if (it != parent->children.end()) { unloadClip(it->second.get()); graveyard.push_back(std::move(it->second)); parent->children.erase(it); }
    parent->children[raw->depth] = std::move(t);
    scriptObject(raw);
    return raw;
}

BitmapInstance* Player::attachBitmap(MovieClip* parent, Object* bitmapData, int asDepth, bool smoothing) {
    auto b = std::make_unique<BitmapInstance>(this);
    BitmapInstance* raw = b.get();
    raw->depth = asDepth + kDepthOffset;
    raw->parent = parent;
    raw->scripted = true;
    raw->smoothing = smoothing;
    raw->path = vm_->toString(vm_->get(bitmapData, "__path"));
    // Keep the bitmap definition: Self.SetExternalTextureWithPath remaps by its export name.
    if (auto ref = std::static_pointer_cast<std::pair<std::shared_ptr<const MovieDef>, uint16_t>>(bitmapData->payload)) {
        const CharDef* cd = ref->first ? ref->first->character(ref->second) : nullptr;
        if (cd && cd->type == CharType::Bitmap) {
            raw->bitmap = &ref->first->bitmaps[(size_t)cd->index];
            raw->def = ref->first;
            std::string ext = externalTexture ? externalTexture(raw->bitmap->exportName) : "";
            if (!ext.empty()) raw->path = ext;
        }
    }
    raw->width = (int)vm_->toNumber(vm_->get(bitmapData, "width"));
    raw->height = (int)vm_->toNumber(vm_->get(bitmapData, "height"));
    // External textures replace the 32x32 placeholder of the movie: their natural size is used (GFx draws the
    // texture at the image's target size; the movie scales the clip afterwards).
    parent->children[raw->depth] = std::move(b);
    return raw;
}

MovieClip* Player::duplicate(DisplayObject* src, const std::string& name, int asDepth, Object* initObj) {
    if (!src || !src->parent || src->kind != DisplayObject::Kind::Clip) return nullptr;
    DisplayObject* d = instantiate(src->parent, src->def, src->charId, asDepth + kDepthOffset, name, true);
    if (!d || d->kind != DisplayObject::Kind::Clip) return nullptr;
    auto* mc = static_cast<MovieClip*>(d);
    mc->matrix = src->matrix;
    mc->cx = src->cx;
    mc->clipActions = static_cast<MovieClip*>(src)->clipActions;
    mc->frame = -1;
    applyFrameTags(mc, 0, true);
    mc->frame = 0;
    constructClip(mc, initObj);
    queueAction([this, mc]() { if (!mc->removed) dispatchClipEvent(mc, "onLoad", EvLoad); });
    return mc;
}

void Player::unloadChildren(MovieClip* mc) {
    // As before, no onUnload here: only the lifetime changes (marked removed, kept alive).
    std::function<void(DisplayObject*)> markRemoved = [&](DisplayObject* d) {
        d->removed = true;
        if (d->kind == DisplayObject::Kind::Clip)
            for (auto& [dd, ch] : static_cast<MovieClip*>(d)->children) markRemoved(ch.get());
    };
    for (auto& [d, ch] : mc->children) { markRemoved(ch.get()); graveyard.push_back(std::move(ch)); }
    mc->children.clear();
}

void Player::unloadClip(DisplayObject* d) {
    if (!d || d->removed) return;
    d->removed = true;
    if (d->kind == DisplayObject::Kind::Clip) {
        auto* mc = static_cast<MovieClip*>(d);
        for (auto& [dd, ch] : mc->children) unloadClip(ch.get());
        dispatchClipEvent(mc, "onUnload", EvUnload);
    }
}

void Player::removeObject(DisplayObject* d) {
    if (!d || !d->parent || d->removed) return;
    MovieClip* p = d->parent;
    auto it = p->children.find(d->depth);
    if (it == p->children.end() || it->second.get() != d) return;
    std::unique_ptr<DisplayObject> owned = std::move(it->second);
    p->children.erase(it);
    unloadClip(owned.get());
    graveyard.push_back(std::move(owned));
}

bool Player::swapDepths(DisplayObject* d, int newDepth) {
    if (!d || !d->parent) return false;
    MovieClip* p = d->parent;
    if (newDepth == d->depth) return true;
    auto it = p->children.find(d->depth);
    if (it == p->children.end()) return false;
    std::unique_ptr<DisplayObject> self = std::move(it->second);
    p->children.erase(it);
    auto other = p->children.find(newDepth);
    if (other != p->children.end()) {
        std::unique_ptr<DisplayObject> o = std::move(other->second);
        p->children.erase(other);
        o->depth = d->depth;
        p->children[o->depth] = std::move(o);
    }
    self->depth = newDepth;
    p->children[newDepth] = std::move(self);
    return true;
}

void Player::loadMovieInto(MovieClip* target, const std::string& url, Object* loader) {
    loads_.push_back({target, url, loader});
}

void Player::processLoads() {
    core::prof::Scope prof("movie.loadClip");
    std::vector<PendingLoad> loads;
    loads.swap(loads_);
    for (PendingLoad& l : loads) {
        if (!l.target || l.target->removed) continue;
        std::string path = resolveMovieUrl ? resolveMovieUrl(l.url, l.target->def ? l.target->def->dir() : "") : "";
        std::shared_ptr<const MovieDef> def = path.empty() ? nullptr : loadDef(path);
        auto notify = [&](const char* ev, Args a) {
            if (!l.loader) return;
            Value ls = vm_->get(l.loader, "__listeners");
            std::vector<Value> list = ls.isObject() ? ls.o->elems : std::vector<Value>{};
            for (const Value& li : list) vm_->callMethod(li, ev, a);
            vm_->callMethod(Value(l.loader), ev, a);
        };
        Value tv(scriptObject(l.target));
        if (!def) {
            LOG_WARN("GFX loadClip '%s' failed (no movie)", l.url.c_str());
            notify("onLoadError", {tv, Value("URLNotFound")});
            continue;
        }
        LOG_INFO("GFX loadClip %s -> %s", l.url.c_str(), def->baseName().c_str());
        if (std::getenv("WFC_GFX_FORCEGC")) forceGcFrames_ = 3;   // DEV TOOL: collect on the next advances
        for (auto& [d, ch] : l.target->children) { unloadClip(ch.get()); graveyard.push_back(std::move(ch)); }
        l.target->children.clear();
        Object* so = scriptObject(l.target);
        so->clearProps();
        so->proto = vm_->movieClipProto;
        l.target->def = def;
        l.target->sprite = &def->root;
        l.target->isRootOfMovie = true;
        l.target->playing = true;
        notify("onLoadStart", {tv});
        notify("onLoadProgress", {tv, Value(1), Value(1)});
        notify("onLoadComplete", {tv, Value(200)});
        drainActions();
        l.target->frame = -1;
        applyFrameTags(l.target, 0, true);
        l.target->frame = 0;
        drainActions();
        notify("onLoadInit", {tv});
        drainActions();
    }
}

// ---- events ----

void Player::dispatchClipEvent(MovieClip* mc, const std::string& methodName, uint32_t flag) {
    if (!mc) return;
    for (const ClipAction& ca : mc->clipActions)
        if (ca.events & flag) vm_->runBlock(ca.code.code, 0, ca.code.code->size(), mc);
    Object* so = mc->script;
    if (!so) return;
    Value f = vm_->get(so, methodName);
    if (f.isObject() && f.o->kind == avm1::ObjKind::Function) {
        Args a;
        try {
            vm_->call(f, Value(so), a);
        } catch (const avm1::ScriptThrow& t) {
            LOG_WARN("GFX %s threw: %s", methodName.c_str(), vm_->toString(t.v).c_str());
        }
    }
}

int Player::addInterval(Value target, const std::string& method, double ms, Args args, bool once) {
    Interval iv;
    iv.id = nextInterval_++;
    iv.target = target;
    iv.method = method;
    iv.ms = std::isfinite(ms) ? std::max(0.0, ms) : 0.0;
    iv.next = timeMs_ + std::max(1.0, iv.ms);
    iv.args = std::move(args);
    iv.once = once;
    intervals_.push_back(iv);
    return iv.id;
}

void Player::clearInterval(int id) {
    for (auto& iv : intervals_) if (iv.id == id) iv.dead = true;
}

void Player::tickIntervals() {
    for (size_t i = 0; i < intervals_.size(); ++i) {
        int fired = 0;
        while (!intervals_[i].dead && timeMs_ >= intervals_[i].next && fired < 8) {
            Interval iv = intervals_[i];
            // setInterval(clip, "method", ms) on a clip removed since: Flash finds no method on a removed clip, so the
            // interval does nothing (e.g. Customize mc_loading never clears its close interval; it removes itself).
            if (!iv.method.empty() && iv.target.isObject() && iv.target.o->display && iv.target.o->display->removed) {
                intervals_[i].dead = true;
                break;
            }
            intervals_[i].next += std::max(1.0, iv.ms);
            if (iv.once) intervals_[i].dead = true;
            ++fired;
            try {
                if (iv.method.empty()) vm_->call(iv.target, Value::undef(), iv.args);
                else vm_->callMethod(iv.target, iv.method, iv.args);
            } catch (const avm1::ScriptThrow& t) {
                LOG_WARN("GFX interval threw: %s", vm_->toString(t.v).c_str());
            }
            drainActions();
        }
    }
    intervals_.erase(std::remove_if(intervals_.begin(), intervals_.end(), [](const Interval& iv) { return iv.dead; }), intervals_.end());
}

void Player::keyEvent(int keyCode, bool down) {
    if (down) textEditKey(keyCode);
    lastKeyCode = keyCode;
    lastAscii = keyCode == 13 ? 13 : keyCode == 27 ? 27 : (keyCode >= 32 && keyCode < 127 ? keyCode : 0);
    if (down) keysDown.insert(keyCode); else keysDown.erase(keyCode);
    std::vector<Object*> ls = keyListeners;
    for (Object* l : ls) {
        try {
            vm_->callMethod(Value(l), down ? "onKeyDown" : "onKeyUp", {});
        } catch (const avm1::ScriptThrow& t) {
            LOG_WARN("GFX key listener threw: %s", vm_->toString(t.v).c_str());
        }
        drainActions();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Text entry (Flash input TextField: Selection focus, caret, onChanged, maxChars, variable binding)

void Player::setTextFocus(TextField* tf) {
    if (tf && (tf->removed || !tf->editable())) tf = nullptr;
    if (tf != focusText_) LOG_INFO("GFX text focus -> %s", tf ? tf->targetPath().c_str() : "(none)");
    focusText_ = tf;
    caret_ = tf ? tf->chars.size() : 0;
    vm_->global->setRaw("__selectionFocus", tf ? avm1::Value(tf->targetPath()) : avm1::Value::null(), avm1::DontEnum);
}

void Player::setCaretIndex(size_t i) {
    TextField* tf = textFocus();
    caret_ = tf ? std::min(i, tf->chars.size()) : 0;
}

void Player::inputChanged(TextField* tf) {
    tf->htmlSource.clear();
    tf->layoutDirty = true;
    // Two-way variable binding: the typed text becomes the variable's value.
    if (!tf->variable.empty() && tf->parent) {
        std::string path = tf->variable, name = path;
        DisplayObject* target = tf->parent;
        size_t dot = path.find_last_of(".:/");
        if (dot != std::string::npos) { name = path.substr(dot + 1); target = resolveTarget(path.substr(0, dot), tf->parent); }
        if (target) {
            std::string s = tf->plainText();
            vm_->set(scriptObject(target), name, avm1::Value(s));
            tf->variableShown = s;
        }
    }
    try {
        vm_->callMethod(avm1::Value(scriptObject(tf)), "onChanged", {avm1::Value(scriptObject(tf))});
    } catch (const avm1::ScriptThrow& t) {
        LOG_WARN("GFX onChanged threw: %s", vm_->toString(t.v).c_str());
    }
    drainActions();
}

bool Player::textInput(char32_t c) {
    TextField* tf = textFocus();
    if (!tf || c < 32 || c == 127 || c > 0xFFFF) return false;   // the embedded fonts cover the BMP only
    if (tf->maxChars > 0 && (int)tf->chars.size() >= tf->maxChars) return true;
    caret_ = std::min(caret_, tf->chars.size());
    if (tf->formats.empty()) tf->formats = {tf->newFormat};
    int fmt = tf->charFormat.empty() ? 0 : tf->charFormat[caret_ > 0 ? caret_ - 1 : 0];
    tf->chars.insert(tf->chars.begin() + (long)caret_, (char16_t)c);
    tf->charFormat.insert(tf->charFormat.begin() + (long)caret_, fmt);
    ++caret_;
    inputChanged(tf);
    return true;
}

bool Player::textEditKey(int keyCode) {
    TextField* tf = textFocus();
    if (!tf) return false;
    caret_ = std::min(caret_, tf->chars.size());
    switch (keyCode) {
    case 8:   // Backspace
        if (caret_ == 0) return true;
        --caret_;
        tf->chars.erase(tf->chars.begin() + (long)caret_);
        if (caret_ < tf->charFormat.size()) tf->charFormat.erase(tf->charFormat.begin() + (long)caret_);
        inputChanged(tf);
        return true;
    case 46:  // Delete
        if (caret_ >= tf->chars.size()) return true;
        tf->chars.erase(tf->chars.begin() + (long)caret_);
        if (caret_ < tf->charFormat.size()) tf->charFormat.erase(tf->charFormat.begin() + (long)caret_);
        inputChanged(tf);
        return true;
    case 37: if (caret_ > 0) --caret_; return true;                       // Left
    case 39: if (caret_ < tf->chars.size()) ++caret_; return true;        // Right
    case 36: caret_ = 0; return true;                                     // Home
    case 35: caret_ = tf->chars.size(); return true;                      // End
    default: return false;
    }
}

TextField* Player::inputFieldAt(MovieClip* mc, const Point& world, const Matrix& parent) {
    if (!mc || mc->removed || !mc->visible) return nullptr;
    (void)parent;
    TextField* found = nullptr;
    for (auto& [depth, ch] : mc->children) {   // later (higher depth) children are on top
        if (ch->removed || !ch->visible) continue;
        if (ch->kind == DisplayObject::Kind::Clip) {
            if (TextField* t = inputFieldAt(static_cast<MovieClip*>(ch.get()), world, parent)) found = t;
        } else if (ch->kind == DisplayObject::Kind::Text) {
            auto* tf = static_cast<TextField*>(ch.get());
            if (!tf->editable()) continue;
            Point local = tf->worldMatrix().inverse().apply(world);
            if (local.x >= tf->bounds.xmin && local.x <= tf->bounds.xmax && local.y >= tf->bounds.ymin && local.y <= tf->bounds.ymax)
                found = tf;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------------------------
// Mouse

bool Player::hitGeometry(const DisplayObject* d, const Point& world, bool ignoreVisible) const {
    if (!d || d->removed) return false;
    if (!ignoreVisible && !d->visible) return false;
    if (d->maskedBy && !d->maskedBy->removed && !hitGeometry(d->maskedBy, world, true)) return false;
    Point p = d->worldMatrix().inverse().apply(world);
    switch (d->kind) {
    case DisplayObject::Kind::Shape: return pointInShape(static_cast<const ShapeInstance*>(d)->current(), p);
    case DisplayObject::Kind::Text:
    case DisplayObject::Kind::StaticText:
    case DisplayObject::Kind::Bitmap: return pointInRect(d->localBounds(), p);
    case DisplayObject::Kind::Clip: {
        const auto* mc = static_cast<const MovieClip*>(d);
        if (mc->drawing && pointInShape(mc->drawing.get(), p)) return true;
        // Timeline masks (clipDepth): a child masked by a layer is hit only inside the mask.
        std::vector<const DisplayObject*> masks;
        for (const auto& [depth, ch] : mc->children) {
            if (ch->removed) continue;
            if (ch->clipDepth > 0) { masks.push_back(ch.get()); continue; }
            if (ch->usedAsMask) continue;
            bool clipped = false;
            for (const DisplayObject* m : masks)
                if (depth > m->depth && depth <= m->clipDepth && !hitGeometry(m, world, true)) { clipped = true; break; }
            if (!clipped && hitGeometry(ch.get(), world, ignoreVisible)) return true;
        }
        return false;
    }
    }
    return false;
}

bool Player::hitTestPoint(const DisplayObject* d, float stageX, float stageY, bool shapeFlag) const {
    Point w{stageX * 20.0f, stageY * 20.0f};
    if (shapeFlag) return hitGeometry(d, w, true);
    return pointInRect(d->boundsIn(d->worldMatrix()), w);
}

bool Player::isButtonClip(MovieClip* mc) {
    if (!mc->script) return false;
    static const char* kHandlers[] = {"onPress", "onRelease", "onReleaseOutside", "onRollOver", "onRollOut", "onDragOver", "onDragOut"};
    for (const char* h : kHandlers) {
        Value f = vm_->get(mc->script, h);
        if (f.isObject() && f.o->kind == avm1::ObjKind::Function) return true;
    }
    return false;
}

MovieClip* Player::findButton(MovieClip* mc, const Point& world) {
    // Topmost first: children in reverse depth order; a button clip captures its subtree (Flash 8).
    std::vector<const DisplayObject*> masks;
    for (const auto& [depth, ch] : mc->children) if (!ch->removed && ch->clipDepth > 0) masks.push_back(ch.get());
    for (auto it = mc->children.rbegin(); it != mc->children.rend(); ++it) {
        DisplayObject* ch = it->second.get();
        if (ch->removed || !ch->visible || ch->usedAsMask || ch->clipDepth > 0 || ch->kind != DisplayObject::Kind::Clip) continue;
        bool clipped = false;
        for (const DisplayObject* m : masks)
            if (it->first > m->depth && it->first <= m->clipDepth && !hitGeometry(m, world, true)) { clipped = true; break; }
        if (clipped) continue;
        if (ch->maskedBy && !ch->maskedBy->removed && !hitGeometry(ch->maskedBy, world, true)) continue;
        auto* c = static_cast<MovieClip*>(ch);
        if (isButtonClip(c)) {
            if (!c->enabled) continue;
            Value ha = vm_->get(c->script, "hitArea");
            bool hit = ha.isObject() && ha.o->display ? hitGeometry(ha.o->display, world, true) : hitGeometry(c, world, false);
            if (hit) return c;
            continue;
        }
        if (MovieClip* b = findButton(c, world)) return b;
    }
    return nullptr;
}

void Player::callHandler(MovieClip* mc, const char* name) {
    if (!mc || mc->removed || !mc->script) return;
    Value f = vm_->get(mc->script, name);
    if (!f.isObject() || f.o->kind != avm1::ObjKind::Function) return;
    try {
        Args none;
        vm_->call(f, Value(mc->script), none);
    } catch (const avm1::ScriptThrow& t) {
        LOG_WARN("GFX %s threw: %s", name, vm_->toString(t.v).c_str());
    }
    drainActions();
}

void Player::broadcastMouse(const char* method, uint32_t flag, const Args& args) {
    std::vector<Object*> ls = mouseListeners;
    for (Object* l : ls) {
        try {
            vm_->callMethod(Value(l), method, args);
        } catch (const avm1::ScriptThrow& t) {
            LOG_WARN("GFX mouse listener threw: %s", vm_->toString(t.v).c_str());
        }
        drainActions();
    }
    if (!flag || !root_) return;
    std::vector<MovieClip*> clips;
    collectEnterFrame(root_, clips);
    for (MovieClip* mc : clips) {
        if (mc->removed) continue;
        dispatchClipEvent(mc, method, flag);
        drainActions();
    }
}

void Player::updateHover() {
    if (!root_) return;
    MovieClip* now = mouseInside_ ? findButton(root_, {mouseX * 20.0f, mouseY * 20.0f}) : nullptr;
    if (pressed_) {
        // While pressed only the pressed button tracks the pointer (onDragOut / onDragOver).
        bool over = now == pressed_;
        if (over != pressedOver_) {
            pressedOver_ = over;
            callHandler(pressed_, over ? "onDragOver" : "onDragOut");
        }
        return;
    }
    if (now == hover_) return;
    MovieClip* old = hover_;
    hover_ = now;
    if (old && !old->removed) callHandler(old, "onRollOut");
    if (now) callHandler(now, "onRollOver");
}

void Player::mouseMove(float x, float y) {
    bool moved = !mouseInside_ || x != mouseX || y != mouseY;
    mouseX = x; mouseY = y;
    mouseInside_ = true;
    if (moved) broadcastMouse("onMouseMove", EvMouseMove, {});
    updateHover();
}

void Player::mouseLeave() {
    if (!mouseInside_) return;
    mouseInside_ = false;
    updateHover();
}

void Player::mouseButton(bool down) {
    if (down == mouseDown_) return;
    mouseDown_ = down;
    if (down) {
        // Clicking an input field focuses it (caret at the end); clicking anywhere else clears text focus.
        if (root_) {
            Point world{mouseX * 20.0f, mouseY * 20.0f};   // twips, as findButton
            TextField* hit = mouseInside_ ? inputFieldAt(root_, world, Matrix{}) : nullptr;
            if (hit != textFocus()) setTextFocus(hit);
        }
        broadcastMouse("onMouseDown", EvMouseDown, {});
        updateHover();
        if (hover_ && !hover_->removed) {
            pressed_ = hover_;
            pressedOver_ = true;
            callHandler(pressed_, "onPress");
        }
        return;
    }
    broadcastMouse("onMouseUp", EvMouseUp, {});
    if (pressed_) {
        MovieClip* p = pressed_;
        bool over = pressedOver_;
        pressed_ = nullptr;
        callHandler(p, over ? "onRelease" : "onReleaseOutside");
        hover_ = over ? p : nullptr;   // recomputed below (rolls over whatever is under the pointer now)
    }
    updateHover();
}

void Player::mouseWheel(int delta) {
    if (delta) broadcastMouse("onMouseWheel", 0, {Value(delta)});
}

// TextField.variable: the field shows the bound variable (path relative to the field's parent timeline, e.g.
// "_parent.Score" from a PlayerList cell) - Flash updates it every frame.
void Player::syncVariableText(MovieClip* mc) {
    for (auto& [depth, ch] : mc->children) {
        if (ch->removed) continue;
        if (ch->kind == DisplayObject::Kind::Clip) { syncVariableText(static_cast<MovieClip*>(ch.get())); continue; }
        if (ch->kind != DisplayObject::Kind::Text) continue;
        auto* tf = static_cast<TextField*>(ch.get());
        if (tf->variable.empty()) continue;
        std::string path = tf->variable, name = path;
        DisplayObject* target = mc;
        size_t dot = path.find_last_of(".:/");
        if (dot != std::string::npos) {
            name = path.substr(dot + 1);
            target = resolveTarget(path.substr(0, dot), mc);
        }
        if (!target) continue;
        Value v = vm_->get(scriptObject(target), name);
        if (v.isUndef()) continue;
        std::string s = vm_->toString(v);
        if (s == tf->variableShown) continue;
        tf->variableShown = s;
        if (tf->html) tf->setHtmlText(s); else tf->setPlainText(s);
    }
}

void Player::collectEnterFrame(MovieClip* mc, std::vector<MovieClip*>& out) {
    // Nothing runs while collecting, so the children map is walked directly (no per-node copy).
    out.push_back(mc);
    for (auto& [d, ch] : mc->children)
        if (ch->kind == DisplayObject::Kind::Clip) collectEnterFrame(static_cast<MovieClip*>(ch.get()), out);
}

void Player::advanceClip(MovieClip* mc) {
    if (mc->removed) return;
    std::vector<MovieClip*> kids;
    for (auto& [d, ch] : mc->children)
        if (ch->kind == DisplayObject::Kind::Clip) kids.push_back(static_cast<MovieClip*>(ch.get()));
    if (mc->playing && mc->sprite && mc->totalFrames() > 1) {
        int next = mc->frame + 1;
        if (next >= mc->totalFrames()) { gotoFrame(mc, 0, true); }
        else { applyFrameTags(mc, next, true); mc->frame = next; }
    }
    for (MovieClip* k : kids) if (!k->removed) advanceClip(k);
}

void Player::advance(float dt) {
    if (!root_) return;
    core::prof::Scope prof("+gfx.advance");
    timeMs_ += dt * 1000.0;
    // enterFrame handlers, then the timelines' new frames.
    std::vector<MovieClip*> clips;
    clips.reserve(lastClipCount_);
    collectEnterFrame(root_, clips);
    lastClipCount_ = clips.size();
    static const std::string kOnEnterFrame = "onEnterFrame";   // looked up on every clip every frame: no temporary
    {
        core::prof::Scope p1("+gfx.enterFrame");
        for (MovieClip* mc : clips) {
            if (mc->removed) continue;
            dispatchClipEvent(mc, kOnEnterFrame, EvEnterFrame);
            drainActions();
        }
    }
    { core::prof::Scope p2("+gfx.timeline"); advanceClip(root_); drainActions(); }
    { core::prof::Scope p3("gfx.varText"); syncVariableText(root_); }
    { core::prof::Scope p4("+gfx.intervals"); tickIntervals(); }
    { core::prof::Scope p5("+gfx.loads"); processLoads(); drainActions(); }
    // Garbage collection: every few seconds (roots: display objects, listeners, intervals, queued work).
    // Per movie: a counter shared by all players (a function static) made the same movie take every 300th tick while
    // the open-movie count divided 300, so the others collected only when that count changed (heaps of ~200k objects).
    static const bool noGc = std::getenv("WFC_GFX_NO_GC") != nullptr;   // diagnostics: never collect
    // DEV TOOL WFC_GFX_FORCEGC=<n>: a full collection on the 3 advances after every loadClip (the loaded movie's init
    // actions have run by then) and, for n > 1, every n advances regardless of heap size - stale-object hunting.
    static const int forceEvery = std::getenv("WFC_GFX_FORCEGC") ? std::atoi(std::getenv("WFC_GFX_FORCEGC")) : 0;
    ++gcCounter_;
    bool forced = false;
    if (forceGcFrames_ > 0) { --forceGcFrames_; forced = true; }
    if (forceEvery > 1 && gcCounter_ % forceEvery == 0) forced = true;
    if (!noGc && (forced || (gcCounter_ % 300 == 0 && vm_->heapSize() > 20000))) {
        std::vector<Object*> roots;
        for (Object* o : keyListeners) roots.push_back(o);
        for (Object* o : stageListeners) roots.push_back(o);
        for (Object* o : mouseListeners) roots.push_back(o);
        for (auto& iv : intervals_) { if (iv.target.isObject()) roots.push_back(iv.target.o); for (auto& a : iv.args) if (a.isObject()) roots.push_back(a.o); }
        for (auto& l : loads_) if (l.loader) roots.push_back(l.loader);
        std::vector<MovieClip*> all;
        collectEnterFrame(root_, all);
        for (MovieClip* mc : all) if (mc->script) roots.push_back(mc->script);
        // Removed instances and everything under them: their script objects stay valid while scripts still refer to
        // them (a removed clip's children were collected before and crashed a later stale access).
        std::function<void(DisplayObject*)> keep = [&](DisplayObject* d) {
            if (d->script) roots.push_back(d->script);
            if (d->kind == DisplayObject::Kind::Clip)
                for (auto& [dd, ch] : static_cast<MovieClip*>(d)->children) keep(ch.get());
        };
        for (auto& g : graveyard) keep(g.get());
        core::prof::Scope p6("gfx.gc");
        vm_->collect(roots);
    }
}

DisplayObject* Player::resolveTarget(const std::string& pathIn, DisplayObject* base) {
    std::string path = pathIn;
    if (path.empty()) return base;
    DisplayObject* cur = base ? base : root_;
    auto step = [&](const std::string& seg) -> bool {
        if (seg.empty() || seg == ".") return true;
        if (seg == "..") { cur = cur ? cur->parent : nullptr; return cur != nullptr; }
        if (seg == "_root" || seg == "_level0") { cur = cur ? cur->rootClip() : root_; return true; }
        if (seg == "_parent") { cur = cur ? cur->parent : nullptr; return cur != nullptr; }
        if (seg == "this") return true;
        if (!cur || cur->kind != DisplayObject::Kind::Clip) return false;
        DisplayObject* n = static_cast<MovieClip*>(cur)->childByName(seg);
        if (!n) {
            // a script variable holding a clip reference
            Object* so = scriptObject(cur);
            if (const avm1::Property* p = so ? so->findOwn(seg) : nullptr)
                if (p->v.isObject() && p->v.o->display) n = p->v.o->display;
        }
        if (!n) return false;
        cur = n;
        return true;
    };
    bool slash = path.find('/') != std::string::npos;
    char sep = slash ? '/' : '.';
    size_t i = 0;
    if (slash && path[0] == '/') { cur = cur ? cur->rootClip() : root_; i = 1; }
    while (i <= path.size()) {
        size_t j = path.find(sep, i);
        if (j == std::string::npos) j = path.size();
        if (!step(path.substr(i, j - i))) return nullptr;
        i = j + 1;
    }
    return cur;
}

// ---- rendering traversal ----

void Player::buildRenderList(const Matrix& base, std::vector<RenderItem>& out) {
    core::prof::Scope prof("gfx.renderList");
    renderBase_ = base;
    if (root_) renderObject(root_, base, CXForm{}, out);
}

void Player::renderObject(const DisplayObject* d, const Matrix& m, const CXForm& cx, std::vector<RenderItem>& out) {
    if (d->removed) return;
    if (!renderingMask_ && (!d->visible || d->usedAsMask)) return;
    Matrix wm = m * d->matrix;
    CXForm wc = cx * d->cx;
    if (!renderingMask_ && wc.ma <= 0 && wc.aa <= 0) return;
    bool scriptMask = d->maskedBy && !d->maskedBy->removed;
    if (scriptMask) {
        // setMask(): the mask clip in its own world position (it may live anywhere in the tree).
        RenderItem b; b.type = RenderItem::MaskBegin; out.push_back(b);
        std::vector<RenderItem> tmp;
        bool saved = renderingMask_;
        renderingMask_ = true;
        const DisplayObject* mk = d->maskedBy;
        renderObject(mk, mk->parent ? renderBase_ * mk->parent->worldMatrix() : renderBase_, CXForm{}, tmp);
        renderingMask_ = saved;
        for (auto& it : tmp) if (it.type == RenderItem::Shape || it.type == RenderItem::Glyph || it.type == RenderItem::Bitmap) out.push_back(it);
        RenderItem e; e.type = RenderItem::MaskEnd; out.push_back(e);
    }
    switch (d->kind) {
    case DisplayObject::Kind::Shape: {
        RenderItem it; it.type = RenderItem::Shape; it.shape = static_cast<const ShapeInstance*>(d)->current(); it.m = wm; it.cx = wc; it.owner = d;
        if (it.shape) out.push_back(it);
        break;
    }
    case DisplayObject::Kind::Bitmap: {
        RenderItem it; it.type = RenderItem::Bitmap; it.bitmap = static_cast<const BitmapInstance*>(d); it.m = wm; it.cx = wc; it.owner = d;
        out.push_back(it);
        break;
    }
    case DisplayObject::Kind::StaticText: {
        const auto* st = static_cast<const StaticTextInstance*>(d);
        if (!st->text) break;
        Matrix tm = wm * st->text->m;
        float x = 0, y = 0;
        uint16_t font = 0; RGBA color; float height = 0;
        for (const TextRecord& r : st->text->records) {
            if (r.hasFont) { font = r.fontId; height = r.height; }
            if (r.hasColor) color = r.color;
            if (r.hasX) x = r.x;
            if (r.hasY) y = r.y;
            const CharDef* fc = d->def->character(font);
            const FontDef* f = fc && fc->type == CharType::Font ? &d->def->fonts[(size_t)fc->index] : nullptr;
            for (const TextRecordGlyph& g : r.glyphs) {
                if (f && g.glyph >= 0 && g.glyph < (int)f->glyphs.size()) {
                    float s = height / f->emSize();
                    RenderItem it; it.type = RenderItem::Glyph; it.shape = &f->glyphs[(size_t)g.glyph];
                    it.m = tm * Matrix{s, 0, 0, s, x, y}; it.cx = wc; it.glyphColor = color; it.owner = d;
                    out.push_back(it);
                }
                x += g.advance;
            }
        }
        break;
    }
    case DisplayObject::Kind::Text: {
        auto* tf = const_cast<TextField*>(static_cast<const TextField*>(d));
        tf->layout();
        if (tf->boxShape && (tf->border || tf->background)) {
            RenderItem it; it.type = RenderItem::Shape; it.shape = tf->boxShape.get(); it.m = wm; it.cx = wc; it.owner = d;
            out.push_back(it);
        }
        // Scaleform text drop shadow (TextField.shadow* extension: DropShadowFilter on the glyphs): the renderer blurs
        // the glyph items that follow this marker.
        size_t shadowAt = (size_t)-1;
        if (!renderingMask_ && tf->shadowAlpha > 0 && tf->shadowStrength > 0) {
            RenderItem sh; sh.type = RenderItem::TextShadow; sh.m = wm; sh.cx = wc; sh.owner = d;
            shadowAt = out.size();
            out.push_back(sh);
        }
        for (const GlyphRun& g : tf->glyphs) {
            if (!g.image.empty()) {
                RenderItem it; it.type = RenderItem::Image; it.imagePath = g.image; it.imgW = g.imgW; it.imgH = g.imgH;
                it.m = wm * Matrix{1, 0, 0, 1, g.x, g.y}; it.cx = wc; it.owner = d;
                out.push_back(it);
                continue;
            }
            if (!g.font || g.glyph < 0 || g.glyph >= (int)g.font->glyphs.size()) continue;
            float s = g.size / g.font->emSize();
            RenderItem it; it.type = RenderItem::Glyph; it.shape = &g.font->glyphs[(size_t)g.glyph];
            it.m = wm * Matrix{s, 0, 0, s, g.x, g.y}; it.cx = wc; it.glyphColor = g.color; it.owner = d;
            out.push_back(it);
            if (shadowAt != (size_t)-1) ++out[shadowAt].count;
        }
        if (shadowAt != (size_t)-1 && out[shadowAt].count == 0) out.erase(out.begin() + (long)shadowAt);
        // The focused input field's caret (blinking about twice a second, as Flash Player's).
        if (tf == textFocus() && std::fmod(timeMs_, 1060.0) < 530.0) {
            if (!caretShape_) {
                caretShape_ = std::make_shared<ShapeDef>();
                caretShape_->bounds = Rect{0, 0, 1, 1};
                caretShape_->fillSets.emplace_back();
                caretShape_->lineSets.emplace_back();
                FillStyle f;
                f.color = RGBA{255, 255, 255, 255};
                caretShape_->fillSets[0].push_back(f);
                ShapePath p;
                p.pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
                p.fill1 = 1;
                caretShape_->paths.push_back(p);
            }
            RenderItem it; it.type = RenderItem::Shape; it.shape = caretShape_.get(); it.owner = d;
            TextField::CaretPos cp;
            size_t ci = std::min(caret_, tf->chars.size());
            if (ci < tf->caretPos.size()) cp = tf->caretPos[ci];
            else { cp.x = tf->bounds.xmin + 40; cp.y = tf->bounds.ymin + 40; cp.h = tf->newFormat.size * 20.0f; }
            RGBA col = tf->formats.empty() ? tf->newFormat.color : tf->formats[0].color;
            CXForm tint = wc;
            tint.mr = col.r / 255.0f * wc.mr; tint.mg = col.g / 255.0f * wc.mg; tint.mb = col.b / 255.0f * wc.mb;
            it.m = wm * Matrix{20.0f, 0, 0, cp.h, cp.x, cp.y}; it.cx = tint;
            out.push_back(it);
        }
        break;
    }
    case DisplayObject::Kind::Clip: {
        const auto* mc = static_cast<const MovieClip*>(d);
        if (mc->drawing) {
            RenderItem it; it.type = RenderItem::Shape; it.shape = mc->drawing.get(); it.m = wm; it.cx = wc; it.owner = d;
            out.push_back(it);
        }
        std::vector<int> maskEnds;
        for (const auto& [depth, ch] : mc->children) {
            while (!maskEnds.empty() && depth > maskEnds.back()) {
                RenderItem p; p.type = RenderItem::MaskPop; out.push_back(p);
                maskEnds.pop_back();
            }
            if (ch->clipDepth > 0) {
                RenderItem b; b.type = RenderItem::MaskBegin; out.push_back(b);
                std::vector<RenderItem> tmp;
                bool saved = renderingMask_;
                renderingMask_ = true;
                renderObject(ch.get(), wm, CXForm{}, tmp);
                renderingMask_ = saved;
                for (auto& it : tmp) if (it.type == RenderItem::Shape || it.type == RenderItem::Glyph || it.type == RenderItem::Bitmap) out.push_back(it);
                RenderItem e; e.type = RenderItem::MaskEnd; out.push_back(e);
                maskEnds.push_back(ch->clipDepth);
                continue;
            }
            // Clips used as script masks are not drawn themselves.
            renderObject(ch.get(), wm, wc, out);
        }
        while (!maskEnds.empty()) { RenderItem p; p.type = RenderItem::MaskPop; out.push_back(p); maskEnds.pop_back(); }
        break;
    }
    }
    if (scriptMask) { RenderItem p; p.type = RenderItem::MaskPop; out.push_back(p); }
}

} // namespace gfx
