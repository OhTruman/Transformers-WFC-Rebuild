// AVM1 <-> display list: MovieClip / TextField prototypes, display properties, Color, flash.geom.Transform.
#include "ui/gfx/Avm1.h"
#include "ui/gfx/Display.h"
#include "core/Log.h"
#include <cstdlib>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace gfx::avm1 {

namespace {

Value arg(const Args& a, size_t i) { return i < a.size() ? a[i] : Value::undef(); }

Object* method(VM& vm, Object* o, const std::string& name, NativeFn fn) {
    Object* f = vm.newFunction(std::move(fn), name);
    o->setRaw(name, Value(f), DontEnum);
    return f;
}

gfx::MovieClip* clipOf(const Value& self) {
    if (!self.isObject() || !self.o->display || self.o->display->removed) return nullptr;
    gfx::DisplayObject* d = self.o->display;
    return d->kind == gfx::DisplayObject::Kind::Clip ? static_cast<gfx::MovieClip*>(d) : nullptr;
}

gfx::TextField* textOf(const Value& self) {
    if (!self.isObject() || !self.o->display || self.o->display->removed) return nullptr;
    gfx::DisplayObject* d = self.o->display;
    return d->kind == gfx::DisplayObject::Kind::Text ? static_cast<gfx::TextField*>(d) : nullptr;
}

std::string lowerStr(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

gfx::RGBA rgbFrom(double v) {
    uint32_t c = (uint32_t)(int64_t)v;
    gfx::RGBA r; r.r = (uint8_t)(c >> 16); r.g = (uint8_t)(c >> 8); r.b = (uint8_t)c; r.a = 255;
    return r;
}
double rgbTo(const gfx::RGBA& c) { return (double)((c.r << 16) | (c.g << 8) | c.b); }

Object* boundsObject(VM& vm, const gfx::Rect& r) {
    Object* o = vm.newPlain();
    o->setRaw("xMin", Value(r.xmin / 20.0)); o->setRaw("xMax", Value(r.xmax / 20.0));
    o->setRaw("yMin", Value(r.ymin / 20.0)); o->setRaw("yMax", Value(r.ymax / 20.0));
    return o;
}

// TextFormat object <-> span.
Object* formatToObject(VM& vm, const gfx::TextFormatSpan& f) {
    Args none;
    Value v = vm.construct(vm.textFormatCtor, none);
    Object* o = v.o;
    o->setRaw("font", Value(f.font)); o->setRaw("size", Value((double)f.size)); o->setRaw("color", Value(rgbTo(f.color)));
    o->setRaw("bold", Value(f.bold)); o->setRaw("italic", Value(f.italic)); o->setRaw("underline", Value(f.underline));
    const char* al[] = {"left", "right", "center", "justify"};
    o->setRaw("align", Value(al[std::max(0, std::min(3, f.align))]));
    o->setRaw("leftMargin", Value((double)f.leftMargin)); o->setRaw("rightMargin", Value((double)f.rightMargin));
    o->setRaw("indent", Value((double)f.indent)); o->setRaw("leading", Value((double)f.leading));
    o->setRaw("letterSpacing", Value((double)f.letterSpacing)); o->setRaw("kerning", Value(f.kerning));
    o->setRaw("url", Value(f.url));
    return o;
}

void applyFormatObject(VM& vm, Object* o, gfx::TextFormatSpan& f) {
    auto has = [&](const char* k) { Value v = vm.get(o, k); return !v.isNullish(); };
    if (has("font")) f.font = vm.toString(vm.get(o, "font"));
    if (has("size")) f.size = (float)vm.toNumber(vm.get(o, "size"));
    if (has("color")) f.color = rgbFrom(vm.toNumber(vm.get(o, "color")));
    if (has("bold")) f.bold = vm.toBool(vm.get(o, "bold"));
    if (has("italic")) f.italic = vm.toBool(vm.get(o, "italic"));
    if (has("underline")) f.underline = vm.toBool(vm.get(o, "underline"));
    if (has("align")) {
        std::string a = lowerStr(vm.toString(vm.get(o, "align")));
        f.align = a == "right" ? 1 : a == "center" ? 2 : a == "justify" ? 3 : 0;
    }
    if (has("leftMargin")) f.leftMargin = (float)vm.toNumber(vm.get(o, "leftMargin"));
    if (has("rightMargin")) f.rightMargin = (float)vm.toNumber(vm.get(o, "rightMargin"));
    if (has("indent")) f.indent = (float)vm.toNumber(vm.get(o, "indent"));
    if (has("leading")) f.leading = (float)vm.toNumber(vm.get(o, "leading"));
    if (has("letterSpacing")) f.letterSpacing = (float)vm.toNumber(vm.get(o, "letterSpacing"));
    if (has("kerning")) f.kerning = vm.toBool(vm.get(o, "kerning"));
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Display properties

bool displayGetProp(VM& vm, gfx::DisplayObject* d, const std::string& key, Value& out) {
    if (!d) return false;
    gfx::Player* p = vm.player();
    // Named children first (instance names).
    if (d->kind == gfx::DisplayObject::Kind::Clip) {
        auto* mc = static_cast<gfx::MovieClip*>(d);
        if (gfx::DisplayObject* ch = mc->childByName(key)) { out = Value(p->scriptObject(ch)); return true; }
    }
    if (key.empty()) return false;
    std::string k = key[0] == '_' ? lowerStr(key) : key;
    if (k[0] == '_') {
        d->syncComponents();
        if (k == "_x") { out = Value(d->matrix.tx / 20.0); return true; }
        if (k == "_y") { out = Value(d->matrix.ty / 20.0); return true; }
        if (k == "_xscale") { out = Value((double)d->xscale); return true; }
        if (k == "_yscale") { out = Value((double)d->yscale); return true; }
        if (k == "_rotation") { out = Value((double)d->rotationDeg); return true; }
        if (k == "_alpha") { out = Value(d->cx.ma * 100.0); return true; }
        if (k == "_visible") { out = Value(d->visible); return true; }
        if (k == "_width" || k == "_height") {
            gfx::Rect b = d->boundsIn(d->matrix);
            out = Value(b.empty() && b.xmin == 0 && b.xmax == 0 ? 0.0 : (k == "_width" ? (b.xmax - b.xmin) : (b.ymax - b.ymin)) / 20.0);
            return true;
        }
        if (k == "_name") { out = Value(d->name); return true; }
        if (k == "_parent") { out = d->parent ? Value(p->scriptObject(d->parent)) : Value::undef(); return true; }
        if (k == "_root") { out = Value(p->scriptObject(d->rootClip())); return true; }
        if (k == "_target") { out = Value(d->slashPath()); return true; }
        if (k == "_currentframe") {
            out = Value(d->kind == gfx::DisplayObject::Kind::Clip ? (double)std::max(1, static_cast<gfx::MovieClip*>(d)->frame + 1) : 1.0);
            return true;
        }
        if (k == "_totalframes" || k == "_framesloaded") {
            out = Value(d->kind == gfx::DisplayObject::Kind::Clip ? (double)static_cast<gfx::MovieClip*>(d)->totalFrames() : 1.0);
            return true;
        }
        if (k == "_url") { out = Value(d->def ? d->def->path() : std::string()); return true; }
        if (k == "_droptarget") { out = Value(""); return true; }
        if (k == "_focusrect") { out = Value::null(); return true; }
        if (k == "_quality") { out = Value("HIGH"); return true; }
        if (k == "_highquality") { out = Value(1); return true; }
        if (k == "_soundbuftime") { out = Value(5); return true; }
        if (k == "_xmouse" || k == "_ymouse") {
            // The pointer in this object's local space (stage pixels -> twips -> inverse world -> pixels).
            gfx::Point p = d->worldMatrix().inverse().apply({d->player->mouseX * 20.0f, d->player->mouseY * 20.0f});
            out = Value((k == "_xmouse" ? p.x : p.y) / 20.0);
            return true;
        }
        if (k == "_lockroot") { out = Value(false); return true; }
        return false;
    }
    if (d->kind == gfx::DisplayObject::Kind::Clip) {
        auto* mc = static_cast<gfx::MovieClip*>(d);
        if (k == "enabled") { out = Value(mc->enabled); return true; }
        if (k == "transform") {
            Value ctor = vm.getV(vm.getV(vm.getV(Value(vm.global), "flash"), "geom"), "Transform");
            if (ctor.isObject()) { Args a{Value(p->scriptObject(d))}; out = vm.construct(ctor.o, a); return true; }
        }
        return false;
    }
    if (d->kind == gfx::DisplayObject::Kind::Text) {
        auto* tf = static_cast<gfx::TextField*>(d);
        if (k == "text") { out = Value(tf->plainText()); return true; }
        if (k == "htmlText") { out = Value(tf->html ? tf->htmlText() : tf->plainText()); return true; }
        if (k == "html") { out = Value(tf->html); return true; }
        if (k == "textColor") { out = Value(rgbTo(tf->newFormat.color)); return true; }
        if (k == "autoSize") { out = Value(tf->autoSize); return true; }
        if (k == "wordWrap") { out = Value(tf->wordWrap); return true; }
        if (k == "multiline") { out = Value(tf->multiline); return true; }
        if (k == "textWidth" || k == "textHeight") { tf->layout(); out = Value((k == "textWidth" ? tf->textWidth : tf->textHeight) / 20.0); return true; }
        if (k == "length") { out = Value((double)tf->chars.size()); return true; }
        if (k == "maxChars") { out = tf->maxChars ? Value((double)tf->maxChars) : Value::null(); return true; }
        if (k == "selectable") { out = Value(tf->selectable); return true; }
        if (k == "embedFonts") { out = Value(tf->embedFonts); return true; }
        if (k == "border") { out = Value(tf->border); return true; }
        if (k == "background") { out = Value(tf->background); return true; }
        if (k == "condenseWhite") { out = Value(tf->condenseWhite); return true; }
        if (k == "variable") { out = tf->variable.empty() ? Value::null() : Value(tf->variable); return true; }
        if (k == "scroll" || k == "maxscroll" || k == "bottomScroll") { out = Value(1); return true; }
        if (k == "hscroll" || k == "maxhscroll") { out = Value(0); return true; }
        if (k == "type") { out = Value(tf->inputType ? "input" : "dynamic"); return true; }
        if (k == "verticalAlign") { out = Value(tf->verticalAlign); return true; }
        if (k == "textAutoSize") { out = Value(tf->textAutoSize); return true; }
        if (k == "shadowAlpha") { out = Value((double)tf->shadowAlpha); return true; }
        return false;
    }
    return false;
}

bool displaySetProp(VM& vm, gfx::DisplayObject* d, const std::string& key, const Value& v) {
    if (!d || key.empty()) return false;
    std::string k = key[0] == '_' ? lowerStr(key) : key;
    auto num = [&]() { return vm.toNumber(v); };
    if (k[0] == '_') {
        double n = num();
        d->syncComponents();
        if (k == "_x") { if (std::isfinite(n)) d->matrix.tx = (float)(n * 20.0); return true; }
        if (k == "_y") { if (std::isfinite(n)) d->matrix.ty = (float)(n * 20.0); return true; }
        if (k == "_xscale") { if (std::isfinite(n)) { d->xscale = (float)n; d->applyComponents(); } return true; }
        if (k == "_yscale") { if (std::isfinite(n)) { d->yscale = (float)n; d->applyComponents(); } return true; }
        if (k == "_rotation") {
            if (std::isfinite(n)) { n = std::fmod(n, 360.0); if (n > 180) n -= 360; else if (n < -180) n += 360; d->rotationDeg = (float)n; d->applyComponents(); }
            return true;
        }
        if (k == "_alpha") { if (std::isfinite(n)) d->cx.ma = (float)(n / 100.0); return true; }
        if (k == "_visible") { d->visible = vm.toBool(v); return true; }
        if ((k == "_width" || k == "_height") && d->kind == gfx::DisplayObject::Kind::Text) {
            // TextField: _width / _height resize the field box (the text is not scaled).
            if (!std::isfinite(n)) return true;
            auto* tf = static_cast<gfx::TextField*>(d);
            float sx = std::max(1e-4f, std::fabs(d->matrix.scaleX())), sy = std::max(1e-4f, std::fabs(d->matrix.scaleY()));
            if (k == "_width") tf->bounds.xmax = tf->bounds.xmin + (float)(n * 20.0) / sx;
            else tf->bounds.ymax = tf->bounds.ymin + (float)(n * 20.0) / sy;
            tf->layoutDirty = true;
            return true;
        }
        if (k == "_width" || k == "_height") {
            if (!std::isfinite(n)) return true;
            gfx::Rect lb = d->localBounds();
            float lw = (lb.xmax - lb.xmin), lh = (lb.ymax - lb.ymin);
            if (k == "_width" && lw > 0) { float rad = d->rotationDeg * 3.14159265f / 180.0f;
                float w = lw * std::fabs(std::cos(rad)) + lh * std::fabs(std::sin(rad)) * d->yscale / 100.0f;
                if (std::fabs(std::cos(rad)) > 1e-4f) d->xscale = (float)((n * 20.0 - (w - lw * std::fabs(std::cos(rad)))) / (lw * std::fabs(std::cos(rad))) * 100.0); }
            if (k == "_height" && lh > 0) d->yscale = (float)(n * 20.0 / lh * 100.0);
            d->applyComponents();
            return true;
        }
        if (k == "_name") { d->name = vm.toString(v); return true; }
        if (k == "_focusrect" || k == "_quality" || k == "_highquality" || k == "_soundbuftime" || k == "_lockroot") return true;
        if (k == "_currentframe" || k == "_totalframes" || k == "_framesloaded" || k == "_target" || k == "_url" ||
            k == "_parent" || k == "_root" || k == "_droptarget" || k == "_xmouse" || k == "_ymouse")
            return true;   // read-only
        return false;
    }
    if (d->kind == gfx::DisplayObject::Kind::Clip) {
        auto* mc = static_cast<gfx::MovieClip*>(d);
        if (k == "enabled") { mc->enabled = vm.toBool(v); return false; }   // also stored (scripts read it back as a plain prop)
        return false;
    }
    if (d->kind == gfx::DisplayObject::Kind::Text) {
        auto* tf = static_cast<gfx::TextField*>(d);
        gfx::Player* p = vm.player();
        if (k == "text") { tf->setPlainText(p->translate(vm.toString(v))); return true; }
        if (k == "htmlText") {
            std::string s = p->translate(vm.toString(v));
            if (tf->html) tf->setHtmlText(s); else tf->setPlainText(s);
            return true;
        }
        if (k == "html") { tf->html = vm.toBool(v); return true; }
        if (k == "textColor") {
            tf->newFormat.color = rgbFrom(num());
            for (auto& f : tf->formats) f.color = tf->newFormat.color;
            tf->layoutDirty = true;
            return true;
        }
        if (k == "autoSize") {
            if (v.t == VType::Bool) tf->autoSize = v.b ? "left" : "none";
            else tf->autoSize = lowerStr(vm.toString(v));
            tf->layoutDirty = true;
            return true;
        }
        if (k == "wordWrap") { tf->wordWrap = vm.toBool(v); tf->layoutDirty = true; return true; }
        if (k == "multiline") { tf->multiline = vm.toBool(v); tf->layoutDirty = true; return true; }
        if (k == "maxChars") { tf->maxChars = (int)num(); return true; }
        if (k == "selectable") { tf->selectable = vm.toBool(v); return true; }
        if (k == "type") { tf->inputType = vm.toString(v) == "input"; return true; }
        if (k == "embedFonts") { tf->embedFonts = vm.toBool(v); return true; }
        if (k == "border") { tf->border = vm.toBool(v); return true; }
        if (k == "background") { tf->background = vm.toBool(v); return true; }
        if (k == "condenseWhite") { tf->condenseWhite = vm.toBool(v); return true; }
        if (k == "variable") { tf->variable = vm.toString(v); return true; }
        if (k == "verticalAlign") { tf->verticalAlign = lowerStr(vm.toString(v)); tf->layoutDirty = true; return true; }
        if (k == "textAutoSize") { tf->textAutoSize = lowerStr(vm.toString(v)); tf->layoutDirty = true; return true; }
        if (k == "shadowAlpha") { tf->shadowAlpha = (float)num(); return true; }
        if (k == "shadowDistance") { tf->shadowDistance = (float)num(); return true; }
        if (k == "shadowAngle") { tf->shadowAngle = (float)num(); return true; }
        if (k == "shadowColor") { tf->shadowColor = (uint32_t)num(); return true; }
        if (k == "shadowBlurX") { tf->shadowBlurX = (float)num(); return true; }
        if (k == "shadowBlurY") { tf->shadowBlurY = (float)num(); return true; }
        if (k == "shadowStrength") { tf->shadowStrength = (float)num(); return true; }
        return false;
    }
    return false;
}

void VM::installDisplayBuiltins() {
    VM& vm = *this;
    // ---------------- MovieClip ----------------
    movieClipProto = newObject(objectProto);
    Object* mcCtor = newFunction([](VM&, const Value& self, Args&) -> Value { return self; }, "MovieClip");
    mcCtor->setRaw("prototype", Value(movieClipProto), DontEnum);
    movieClipProto->setRaw("constructor", Value(mcCtor), DontEnum);
    global->setRaw("MovieClip", Value(mcCtor), DontEnum);
    Object* P = movieClipProto;

    method(vm, P, "attachMovie", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        Object* init = arg(a, 3).isObject() ? a[3].o : nullptr;
        gfx::MovieClip* r = vm.player()->attachMovie(mc, vm.toString(arg(a, 0)), vm.toString(arg(a, 1)), (int)vm.toNumber(arg(a, 2)), init);
        return r ? Value(vm.player()->scriptObject(r)) : Value::undef();
    });
    method(vm, P, "createEmptyMovieClip", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::MovieClip* r = vm.player()->createEmptyMovieClip(mc, vm.toString(arg(a, 0)), (int)vm.toNumber(arg(a, 1)));
        return r ? Value(vm.player()->scriptObject(r)) : Value::undef();
    });
    method(vm, P, "createTextField", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::TextField* t = vm.player()->createTextField(mc, vm.toString(arg(a, 0)), (int)vm.toNumber(arg(a, 1)),
                                                        (float)vm.toNumber(arg(a, 2)), (float)vm.toNumber(arg(a, 3)),
                                                        (float)vm.toNumber(arg(a, 4)), (float)vm.toNumber(arg(a, 5)));
        return t ? Value(vm.player()->scriptObject(t)) : Value::undef();
    });
    method(vm, P, "duplicateMovieClip", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        Object* init = arg(a, 2).isObject() ? a[2].o : nullptr;
        gfx::MovieClip* r = vm.player()->duplicate(mc, vm.toString(arg(a, 0)), (int)vm.toNumber(arg(a, 1)), init);
        return r ? Value(vm.player()->scriptObject(r)) : Value::undef();
    });
    method(vm, P, "removeMovieClip", [](VM& vm, const Value& self, Args&) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        // Only script-created clips, or clips moved to the script depth range, can be removed.
        if (mc && std::getenv("WFC_GFX_CLASSLOG")) LOG_INFO("GFX removeMovieClip %s", mc->targetPath().c_str());
        if (mc && mc->parent && mc->depth >= gfx::kDepthOffset && mc->depth < gfx::kDepthOffset + 1048576) vm.player()->removeObject(mc);
        return Value::undef();
    });
    method(vm, P, "unloadMovie", [](VM& vm, const Value& self, Args&) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (mc) {
            for (auto& [d, ch] : mc->children) { ch->removed = true; }
            mc->children.clear();
            mc->sprite = nullptr;
            mc->frame = 0;
        }
        (void)vm;
        return Value::undef();
    });
    method(vm, P, "getNextHighestDepth", [](VM&, const Value& self, Args&) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        return mc ? Value((double)mc->nextHighestDepth()) : Value::undef();
    });
    method(vm, P, "getDepth", [](VM&, const Value& self, Args&) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        return Value((double)(self.o->display->depth - gfx::kDepthOffset));
    });
    method(vm, P, "getInstanceAtDepth", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::DisplayObject* d = mc->childAtDepth((int)vm.toNumber(arg(a, 0)) + gfx::kDepthOffset);
        return d ? Value(vm.player()->scriptObject(d)) : Value::undef();
    });
    method(vm, P, "swapDepths", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        gfx::DisplayObject* d = self.o->display;
        Value t = arg(a, 0);
        int depth;
        if (t.isObject() && t.o->display) {
            if (t.o->display->parent != d->parent) return Value::undef();
            depth = t.o->display->depth;
        } else depth = (int)vm.toNumber(t) + gfx::kDepthOffset;
        vm.player()->swapDepths(d, depth);
        return Value::undef();
    });
    auto gotoFn = [](bool play) {
        return [play](VM& vm, const Value& self, Args& a) -> Value {
            gfx::MovieClip* mc = clipOf(self);
            if (!mc) return Value::undef();
            int f = vm.player()->resolveFrame(mc, arg(a, 0));
            if (f >= 0) vm.player()->gotoFrame(mc, f, play);
            return Value::undef();
        };
    };
    method(vm, P, "gotoAndPlay", gotoFn(true));
    method(vm, P, "gotoAndStop", gotoFn(false));
    method(vm, P, "play", [](VM&, const Value& self, Args&) -> Value { if (auto* mc = clipOf(self)) mc->playing = true; return Value::undef(); });
    method(vm, P, "stop", [](VM&, const Value& self, Args&) -> Value { if (auto* mc = clipOf(self)) mc->playing = false; return Value::undef(); });
    method(vm, P, "nextFrame", [](VM& vm, const Value& self, Args&) -> Value {
        if (auto* mc = clipOf(self)) vm.player()->gotoFrame(mc, mc->frame + 1, false);
        return Value::undef();
    });
    method(vm, P, "prevFrame", [](VM& vm, const Value& self, Args&) -> Value {
        if (auto* mc = clipOf(self)) vm.player()->gotoFrame(mc, std::max(0, mc->frame - 1), false);
        return Value::undef();
    });
    method(vm, P, "loadMovie", [](VM& vm, const Value& self, Args& a) -> Value {
        if (auto* mc = clipOf(self)) vm.player()->loadMovieInto(mc, vm.toString(arg(a, 0)), nullptr);
        return Value::undef();
    });
    auto boundsFn = [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        gfx::DisplayObject* d = self.o->display;
        gfx::Matrix m;   // into target coordinate space
        Value t = arg(a, 0);
        if (t.isObject() && t.o->display) m = t.o->display->worldMatrix().inverse() * d->worldMatrix();
        else if (t.isUndef()) m = gfx::Matrix{};
        else m = d->worldMatrix();
        return Value(boundsObject(vm, d->boundsIn(m)));
    };
    method(vm, P, "getBounds", boundsFn);
    method(vm, P, "getRect", boundsFn);
    method(vm, P, "localToGlobal", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display || !arg(a, 0).isObject()) return Value::undef();
        Object* pt = a[0].o;
        gfx::Point p{(float)vm.toNumber(vm.get(pt, "x")) * 20, (float)vm.toNumber(vm.get(pt, "y")) * 20};
        gfx::Point q = self.o->display->worldMatrix().apply(p);
        vm.set(pt, "x", Value(q.x / 20.0)); vm.set(pt, "y", Value(q.y / 20.0));
        return Value::undef();
    });
    method(vm, P, "globalToLocal", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display || !arg(a, 0).isObject()) return Value::undef();
        Object* pt = a[0].o;
        gfx::Point p{(float)vm.toNumber(vm.get(pt, "x")) * 20, (float)vm.toNumber(vm.get(pt, "y")) * 20};
        gfx::Point q = self.o->display->worldMatrix().inverse().apply(p);
        vm.set(pt, "x", Value(q.x / 20.0)); vm.set(pt, "y", Value(q.y / 20.0));
        return Value::undef();
    });
    method(vm, P, "hitTest", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display) return Value(false);
        gfx::DisplayObject* d = self.o->display;
        gfx::Rect b = d->boundsIn(d->worldMatrix());
        if (a.size() >= 2)   // hitTest(x, y, shapeFlag): stage coordinates
            return Value(d->player->hitTestPoint(d, (float)vm.toNumber(a[0]), (float)vm.toNumber(a[1]), a.size() > 2 && vm.toBool(a[2])));
        Value t = arg(a, 0);
        if (!t.isObject() || !t.o->display) return Value(false);
        gfx::Rect o = t.o->display->boundsIn(t.o->display->worldMatrix());
        return Value(!(o.xmin > b.xmax || o.xmax < b.xmin || o.ymin > b.ymax || o.ymax < b.ymin));
    });
    method(vm, P, "setMask", [](VM&, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        Value m = arg(a, 0);
        if (self.o->display->maskedBy) self.o->display->maskedBy->usedAsMask = false;
        self.o->display->maskedBy = m.isObject() ? m.o->display : nullptr;
        if (self.o->display->maskedBy) self.o->display->maskedBy->usedAsMask = true;
        return Value::undef();
    });
    method(vm, P, "attachBitmap", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc || !arg(a, 0).isObject()) return Value::undef();
        bool smoothing = a.size() > 3 ? vm.toBool(a[3]) : false;
        vm.player()->attachBitmap(mc, a[0].o, (int)vm.toNumber(arg(a, 1)), smoothing);
        return Value::undef();
    });
    method(vm, P, "getURL", [](VM& vm, const Value&, Args& a) -> Value {
        std::string url = vm.toString(arg(a, 0));
        if (url.rfind("FSCommand:", 0) == 0 && vm.fsCommand) vm.fsCommand(url.substr(10), vm.toString(arg(a, 1)));
        return Value::undef();
    });
    for (const char* n : {"startDrag", "stopDrag", "attachAudio", "setFocus"})
        method(vm, P, n, [](VM&, const Value&, Args&) -> Value { return Value::undef(); });
    method(vm, P, "getSWFVersion", [](VM& vm, const Value&, Args&) -> Value { return Value((double)vm.swfVersion); });
    method(vm, P, "getBytesLoaded", [](VM&, const Value&, Args&) -> Value { return Value(1); });
    method(vm, P, "getBytesTotal", [](VM&, const Value&, Args&) -> Value { return Value(1); });
    // Drawing API.
    method(vm, P, "clear", [](VM&, const Value& self, Args&) -> Value {
        if (auto* mc = clipOf(self)) { mc->drawing.reset(); mc->drawFill = mc->drawLine = 0; }
        return Value::undef();
    });
    auto ensureDrawing = [](gfx::MovieClip* mc) -> gfx::ShapeDef& {
        if (!mc->drawing) { mc->drawing = std::make_shared<gfx::ShapeDef>(); mc->drawing->fillSets.emplace_back(); mc->drawing->lineSets.emplace_back(); }
        return *mc->drawing;
    };
    method(vm, P, "beginFill", [ensureDrawing](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::ShapeDef& s = ensureDrawing(mc);
        gfx::FillStyle fs;
        fs.color = rgbFrom(vm.toNumber(arg(a, 0)));
        double alpha = a.size() > 1 ? vm.toNumber(a[1]) : 100;
        fs.color.a = (uint8_t)std::max(0.0, std::min(255.0, alpha * 2.55));
        s.fillSets.back().push_back(fs);
        mc->drawFill = (int)s.fillSets.back().size();
        return Value::undef();
    });
    method(vm, P, "lineStyle", [ensureDrawing](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::ShapeDef& s = ensureDrawing(mc);
        if (a.empty() || a[0].isUndef()) { mc->drawLine = 0; return Value::undef(); }
        gfx::LineStyle ls;
        ls.width = (float)vm.toNumber(a[0]) * 20;
        ls.color = rgbFrom(vm.toNumber(arg(a, 1)));
        double alpha = a.size() > 2 ? vm.toNumber(a[2]) : 100;
        ls.color.a = (uint8_t)std::max(0.0, std::min(255.0, alpha * 2.55));
        s.lineSets.back().push_back(ls);
        mc->drawLine = (int)s.lineSets.back().size();
        return Value::undef();
    });
    method(vm, P, "moveTo", [ensureDrawing](VM& vm, const Value& self, Args& a) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        ensureDrawing(mc);
        mc->drawPen = {(float)vm.toNumber(arg(a, 0)) * 20, (float)vm.toNumber(arg(a, 1)) * 20};
        gfx::ShapePath p; p.fill1 = mc->drawFill; p.line = mc->drawLine; p.pts.push_back(mc->drawPen);
        mc->drawing->paths.push_back(p);
        return Value::undef();
    });
    auto lineTo = [ensureDrawing](VM& vm, const Value& self, Args& a, bool curve) -> Value {
        gfx::MovieClip* mc = clipOf(self);
        if (!mc) return Value::undef();
        gfx::ShapeDef& s = ensureDrawing(mc);
        if (s.paths.empty() || s.paths.back().fill1 != mc->drawFill || s.paths.back().line != mc->drawLine) {
            gfx::ShapePath p; p.fill1 = mc->drawFill; p.line = mc->drawLine; p.pts.push_back(mc->drawPen);
            s.paths.push_back(p);
        }
        gfx::Point to;
        if (curve) {
            gfx::Point c{(float)vm.toNumber(arg(a, 0)) * 20, (float)vm.toNumber(arg(a, 1)) * 20};
            to = {(float)vm.toNumber(arg(a, 2)) * 20, (float)vm.toNumber(arg(a, 3)) * 20};
            gfx::Point p0 = mc->drawPen;
            for (int i = 1; i <= 12; ++i) {
                float t = i / 12.0f, u = 1 - t;
                s.paths.back().pts.push_back({u * u * p0.x + 2 * u * t * c.x + t * t * to.x, u * u * p0.y + 2 * u * t * c.y + t * t * to.y});
            }
        } else {
            to = {(float)vm.toNumber(arg(a, 0)) * 20, (float)vm.toNumber(arg(a, 1)) * 20};
            s.paths.back().pts.push_back(to);
        }
        mc->drawPen = to;
        s.bounds.expand(to.x, to.y);
        return Value::undef();
    };
    method(vm, P, "lineTo", [lineTo](VM& vm, const Value& self, Args& a) { return lineTo(vm, self, a, false); });
    method(vm, P, "curveTo", [lineTo](VM& vm, const Value& self, Args& a) { return lineTo(vm, self, a, true); });
    method(vm, P, "endFill", [](VM&, const Value& self, Args&) -> Value { if (auto* mc = clipOf(self)) mc->drawFill = 0; return Value::undef(); });
    method(vm, P, "beginGradientFill", [](VM&, const Value&, Args&) -> Value { return Value::undef(); });

    // ---------------- TextField ----------------
    textFieldProto = newObject(objectProto);
    Object* tfCtor = newFunction([](VM&, const Value& self, Args&) -> Value { return self; }, "TextField");
    tfCtor->setRaw("prototype", Value(textFieldProto), DontEnum);
    textFieldProto->setRaw("constructor", Value(tfCtor), DontEnum);
    global->setRaw("TextField", Value(tfCtor), DontEnum);
    Object* T = textFieldProto;
    method(vm, T, "setTextFormat", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::TextField* tf = textOf(self);
        if (!tf) return Value::undef();
        Object* fmt = nullptr;
        size_t b = 0, e = tf->chars.size();
        if (a.size() == 1 && a[0].isObject()) fmt = a[0].o;
        else if (a.size() == 2 && a[1].isObject()) { fmt = a[1].o; b = (size_t)std::max(0.0, vm.toNumber(a[0])); e = b + 1; }
        else if (a.size() >= 3 && a[2].isObject()) { fmt = a[2].o; b = (size_t)std::max(0.0, vm.toNumber(a[0])); e = (size_t)std::max(0.0, vm.toNumber(a[1])); }
        if (!fmt) return Value::undef();
        e = std::min(e, tf->chars.size());
        if (tf->chars.empty()) { applyFormatObject(vm, fmt, tf->newFormat); tf->layoutDirty = true; return Value::undef(); }
        for (size_t i = b; i < e; ++i) {
            gfx::TextFormatSpan f = tf->formats[(size_t)tf->charFormat[i]];
            applyFormatObject(vm, fmt, f);
            tf->formats.push_back(f);
            tf->charFormat[i] = (int)tf->formats.size() - 1;
        }
        tf->layoutDirty = true;
        return Value::undef();
    });
    method(vm, T, "getTextFormat", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::TextField* tf = textOf(self);
        if (!tf) return Value::undef();
        size_t i = a.empty() ? 0 : (size_t)std::max(0.0, vm.toNumber(a[0]));
        if (tf->chars.empty() || i >= tf->chars.size()) return Value(formatToObject(vm, tf->newFormat));
        return Value(formatToObject(vm, tf->formats[(size_t)tf->charFormat[i]]));
    });
    method(vm, T, "setNewTextFormat", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::TextField* tf = textOf(self);
        if (tf && arg(a, 0).isObject()) applyFormatObject(vm, a[0].o, tf->newFormat);
        return Value::undef();
    });
    // GFx extension: TextField.setImageSubstitutions([{subString, image: BitmapData, width, height, baseLineY}, ...]).
    method(vm, T, "setImageSubstitutions", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::TextField* tf = textOf(self);
        if (!tf) return Value::undef();
        tf->imageSubs.clear();
        Value list = arg(a, 0);
        std::vector<Value> items;
        if (list.isObject() && list.o->kind == ObjKind::Array) items = list.o->elems;
        else if (list.isObject()) items.push_back(list);
        for (const Value& it : items) {
            if (!it.isObject()) continue;
            gfx::TextField::ImageSub sub;
            std::string key = vm.toString(vm.get(it.o, "subString"));
            for (unsigned char ch : key) sub.key.push_back((char16_t)ch);
            Value img = vm.get(it.o, "image");
            if (!img.isObject()) continue;
            sub.path = vm.toString(vm.get(img.o, "__path"));
            sub.natH = (float)vm.toNumber(vm.get(img.o, "height"));
            Value w = vm.get(it.o, "width"), h = vm.get(it.o, "height"), b = vm.get(it.o, "baseLineY");
            sub.w = w.isUndef() ? (float)vm.toNumber(vm.get(img.o, "width")) : (float)vm.toNumber(w);
            sub.h = h.isUndef() ? sub.natH : (float)vm.toNumber(h);
            sub.baseLineY = b.isUndef() ? sub.natH : (float)vm.toNumber(b);
            tf->imageSubs.push_back(sub);
        }
        tf->layoutDirty = true;
        return Value::undef();
    });
    method(vm, T, "getNewTextFormat", [](VM& vm, const Value& self, Args&) -> Value {
        gfx::TextField* tf = textOf(self);
        return tf ? Value(formatToObject(vm, tf->newFormat)) : Value::undef();
    });
    method(vm, T, "replaceText", [](VM& vm, const Value& self, Args& a) -> Value {
        gfx::TextField* tf = textOf(self);
        if (!tf) return Value::undef();
        std::string t = tf->plainText();
        size_t b = (size_t)std::max(0.0, vm.toNumber(arg(a, 0))), e = (size_t)std::max(0.0, vm.toNumber(arg(a, 1)));
        if (b > t.size()) b = t.size();
        if (e > t.size()) e = t.size();
        t.replace(b, e - b, vm.toString(arg(a, 2)));
        tf->setPlainText(t);
        return Value::undef();
    });
    method(vm, T, "removeTextField", [](VM& vm, const Value& self, Args&) -> Value {
        if (self.isObject() && self.o->display) vm.player()->removeObject(self.o->display);
        return Value::undef();
    });
    method(vm, T, "getDepth", [](VM&, const Value& self, Args&) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        return Value((double)(self.o->display->depth - gfx::kDepthOffset));
    });
    method(vm, T, "swapDepths", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject() || !self.o->display) return Value::undef();
        vm.player()->swapDepths(self.o->display, (int)vm.toNumber(arg(a, 0)) + gfx::kDepthOffset);
        return Value::undef();
    });
    method(vm, T, "getLineMetrics", [](VM& vm, const Value& self, Args&) -> Value {
        gfx::TextField* tf = textOf(self);
        Object* o = vm.newPlain();
        if (tf) { tf->layout(); o->setRaw("width", Value(tf->textWidth / 20.0)); o->setRaw("height", Value(tf->textHeight / 20.0)); }
        return Value(o);
    });

    // ---------------- Color (legacy) ----------------
    Object* colorProto = newPlain();
    Object* colorCtor = newFunction([](VM&, const Value& self, Args& a) -> Value {
        if (self.isObject() && !a.empty()) self.o->setRaw("__target", a[0], DontEnum);
        return self;
    }, "Color");
    colorCtor->setRaw("prototype", Value(colorProto), DontEnum);
    colorProto->setRaw("constructor", Value(colorCtor), DontEnum);
    global->setRaw("Color", Value(colorCtor), DontEnum);
    auto colorTarget = [](VM& vm, const Value& self) -> gfx::DisplayObject* {
        Value t = vm.getV(self, "__target");
        if (t.isObject() && t.o->display) return t.o->display;
        return t.isString() ? vm.player()->resolveTarget(t.s, vm.player()->root()) : nullptr;
    };
    method(vm, colorProto, "setRGB", [colorTarget](VM& vm, const Value& self, Args& a) -> Value {
        if (gfx::DisplayObject* d = colorTarget(vm, self)) {
            gfx::RGBA c = rgbFrom(vm.toNumber(arg(a, 0)));
            d->cx.mr = d->cx.mg = d->cx.mb = 0;
            d->cx.ar = c.r; d->cx.ag = c.g; d->cx.ab = c.b;
        }
        return Value::undef();
    });
    method(vm, colorProto, "getRGB", [colorTarget](VM& vm, const Value& self, Args&) -> Value {
        gfx::DisplayObject* d = colorTarget(vm, self);
        if (!d) return Value(0);
        return Value((double)(((int)d->cx.ar << 16) | ((int)d->cx.ag << 8) | (int)d->cx.ab));
    });
    method(vm, colorProto, "setTransform", [colorTarget](VM& vm, const Value& self, Args& a) -> Value {
        gfx::DisplayObject* d = colorTarget(vm, self);
        if (!d || !arg(a, 0).isObject()) return Value::undef();
        Object* t = a[0].o;
        auto f = [&](const char* k, float& dst, float scale) { Value v = vm.get(t, k); if (!v.isUndef()) dst = (float)(vm.toNumber(v) * scale); };
        f("ra", d->cx.mr, 0.01f); f("ga", d->cx.mg, 0.01f); f("ba", d->cx.mb, 0.01f); f("aa", d->cx.ma, 0.01f);
        f("rb", d->cx.ar, 1); f("gb", d->cx.ag, 1); f("bb", d->cx.ab, 1); f("ab", d->cx.aa, 1);
        return Value::undef();
    });
    method(vm, colorProto, "getTransform", [colorTarget](VM& vm, const Value& self, Args&) -> Value {
        gfx::DisplayObject* d = colorTarget(vm, self);
        Object* o = vm.newPlain();
        if (d) {
            o->setRaw("ra", Value(d->cx.mr * 100.0)); o->setRaw("ga", Value(d->cx.mg * 100.0)); o->setRaw("ba", Value(d->cx.mb * 100.0));
            o->setRaw("aa", Value(d->cx.ma * 100.0)); o->setRaw("rb", Value((double)d->cx.ar)); o->setRaw("gb", Value((double)d->cx.ag));
            o->setRaw("bb", Value((double)d->cx.ab)); o->setRaw("ab", Value((double)d->cx.aa));
        }
        return Value(o);
    });

    // ---------------- flash.geom.Transform(mc) ----------------
    Object* geom = get(get(global, "flash").o, "geom").o;
    Object* trProto = newPlain();
    Object* trCtor = newFunction([](VM&, const Value& self, Args& a) -> Value {
        if (self.isObject() && !a.empty()) { self.o->setRaw("__target", a[0], DontEnum); self.o->nativeType = "Transform"; }
        return self;
    }, "Transform");
    trCtor->setRaw("prototype", Value(trProto), DontEnum);
    trProto->setRaw("constructor", Value(trCtor), DontEnum);
    geom->setRaw("Transform", Value(trCtor), DontEnum);
    auto trTarget = [](VM& vm, const Value& self) -> gfx::DisplayObject* {
        Value t = vm.getV(self, "__target");
        return t.isObject() ? t.o->display : nullptr;
    };
    {
        Property& p = trProto->own("colorTransform");
        p.flags = DontEnum;
        p.getter = newFunction([trTarget](VM& vm, const Value& self, Args&) -> Value {
            gfx::DisplayObject* d = trTarget(vm, self);
            Value ctor = vm.getV(vm.getV(vm.getV(Value(vm.global), "flash"), "geom"), "ColorTransform");
            Args a;
            if (d) a = {Value(d->cx.mr), Value(d->cx.mg), Value(d->cx.mb), Value(d->cx.ma), Value(d->cx.ar), Value(d->cx.ag), Value(d->cx.ab), Value(d->cx.aa)};
            return ctor.isObject() ? vm.construct(ctor.o, a) : Value::undef();
        });
        p.setter = newFunction([trTarget](VM& vm, const Value& self, Args& a) -> Value {
            gfx::DisplayObject* d = trTarget(vm, self);
            Value c = arg(a, 0);
            if (!d || !c.isObject()) return Value::undef();
            auto n = [&](const char* k) { return (float)vm.toNumber(vm.get(c.o, k)); };
            d->cx.mr = n("redMultiplier"); d->cx.mg = n("greenMultiplier"); d->cx.mb = n("blueMultiplier"); d->cx.ma = n("alphaMultiplier");
            d->cx.ar = n("redOffset"); d->cx.ag = n("greenOffset"); d->cx.ab = n("blueOffset"); d->cx.aa = n("alphaOffset");
            return Value::undef();
        });
    }
    {
        Property& p = trProto->own("matrix");
        p.flags = DontEnum;
        p.getter = newFunction([trTarget](VM& vm, const Value& self, Args&) -> Value {
            gfx::DisplayObject* d = trTarget(vm, self);
            Value ctor = vm.getV(vm.getV(vm.getV(Value(vm.global), "flash"), "geom"), "Matrix");
            Args a;
            if (d) a = {Value(d->matrix.a), Value(d->matrix.b), Value(d->matrix.c), Value(d->matrix.d), Value(d->matrix.tx / 20.0), Value(d->matrix.ty / 20.0)};
            return ctor.isObject() ? vm.construct(ctor.o, a) : Value::undef();
        });
        p.setter = newFunction([trTarget](VM& vm, const Value& self, Args& a) -> Value {
            gfx::DisplayObject* d = trTarget(vm, self);
            Value m = arg(a, 0);
            if (!d || !m.isObject()) return Value::undef();
            auto n = [&](const char* k) { return (float)vm.toNumber(vm.get(m.o, k)); };
            d->matrix.a = n("a"); d->matrix.b = n("b"); d->matrix.c = n("c"); d->matrix.d = n("d");
            d->matrix.tx = n("tx") * 20; d->matrix.ty = n("ty") * 20;
            d->componentsValid = false;
            return Value::undef();
        });
    }
}

} // namespace gfx::avm1
