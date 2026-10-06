// AVM1 bytecode interpreter (SWF 4-8 actions).
#include "ui/gfx/Avm1.h"
#include "ui/gfx/Display.h"
#include "core/Log.h"
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

namespace gfx::avm1 {

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

namespace {

std::mt19937& rng() { static std::mt19937 r(12345); return r; }

struct Ctx {
    std::shared_ptr<std::vector<uint8_t>> keep;
    const uint8_t* code = nullptr;
    size_t end = 0;
    std::shared_ptr<std::vector<std::string>> pool;
    std::vector<Value> regs;
    std::vector<Object*> scope;              // [_global, timeline clip, ..., activation] innermost last
    std::vector<Object*> withs;              // With objects, innermost last
    Object* activation = nullptr;            // locals target (DefineLocal); null for timeline code
    Value thisv;
    gfx::DisplayObject* target = nullptr;    // timeline for frame ops (SetTarget changes)
    gfx::DisplayObject* origTarget = nullptr;
    Object* superProto = nullptr;
    Object* callee = nullptr;
    std::vector<Value> stack;
    bool returned = false;
    Value retval;
};

enum class Flow { End, Return, Jump };

} // namespace

struct Interp {
    VM& vm;

    Value pop(Ctx& c) {
        if (c.stack.empty()) return Value::undef();
        Value v = std::move(c.stack.back());
        c.stack.pop_back();
        return v;
    }
    void push(Ctx& c, Value v) { c.stack.push_back(std::move(v)); }

    gfx::Player* player() { return vm.player(); }

    Object* targetObj(Ctx& c) { return c.target ? player()->scriptObject(c.target) : nullptr; }

    gfx::MovieClip* targetClip(Ctx& c) {
        return c.target && c.target->kind == gfx::DisplayObject::Kind::Clip ? static_cast<gfx::MovieClip*>(c.target) : nullptr;
    }

    // ---- variable paths ("/a/b:var", "_root.a.b", "a.b") ----
    bool isPath(const std::string& n) const {
        return n.find(':') != std::string::npos || n.find('/') != std::string::npos || n.find('.') != std::string::npos;
    }

    // Resolve "a.b.c" (first segment through the scope chain) or slash syntax to (object, final name).
    bool resolvePath(Ctx& c, const std::string& path, Value& baseOut, std::string& nameOut) {
        std::string p = path, var;
        size_t colon = p.rfind(':');
        if (colon != std::string::npos) { var = p.substr(colon + 1); p = p.substr(0, colon); }
        if (p.find('/') != std::string::npos || colon != std::string::npos) {
            gfx::DisplayObject* d = player()->resolveTarget(p, c.target);
            if (!d) return false;
            baseOut = Value(player()->scriptObject(d));
            nameOut = var;
            if (var.empty()) { nameOut.clear(); }
            return true;
        }
        // dot path
        std::vector<std::string> parts;
        size_t s = 0;
        for (size_t i = 0; i <= p.size(); ++i)
            if (i == p.size() || p[i] == '.') { parts.push_back(p.substr(s, i - s)); s = i + 1; }
        if (parts.size() < 2) return false;
        Value base = lookupVar(c, parts[0]);
        for (size_t i = 1; i + 1 < parts.size(); ++i) base = vm.getV(base, parts[i]);
        baseOut = base;
        nameOut = parts.back();
        return true;
    }

    Value specialVar(Ctx& c, const std::string& n, bool& found) {
        found = true;
        if (n == "this") return c.thisv;
        if (n == "_global") return Value(vm.global);
        if (n == "_root" || n == "_level0") {
            gfx::DisplayObject* t = c.target ? c.target : player()->root();
            return Value(player()->scriptObject(t ? t->rootClip() : player()->root()));
        }
        if (n == "_parent") {
            if (c.target && c.target->parent) return Value(player()->scriptObject(c.target->parent));
            return Value::undef();
        }
        if (n == "super" && c.superProto) {
            Object* s = vm.newObject(nullptr);
            s->kind = ObjKind::Super;
            s->superThis = c.thisv.isObject() ? c.thisv.o : nullptr;
            s->superProto = c.superProto;
            return Value(s);
        }
        found = false;
        return Value::undef();
    }

    Value lookupVar(Ctx& c, const std::string& n) {
        for (auto it = c.withs.rbegin(); it != c.withs.rend(); ++it)
            if (vm.has(*it, n)) return vm.get(*it, n);
        for (auto it = c.scope.rbegin(); it != c.scope.rend(); ++it) {
            Object* s = *it;
            if (!s) continue;
            if (s == vm.global) break;
            if (vm.has(s, n)) return vm.get(s, n);
        }
        bool found;
        Value sv = specialVar(c, n, found);
        if (found) return sv;
        if (vm.has(vm.global, n)) return vm.get(vm.global, n);
        return Value::undef();
    }

    Value getVariable(Ctx& c, const std::string& n) {
        if (isPath(n)) {
            Value base; std::string name;
            if (!resolvePath(c, n, base, name)) return Value::undef();
            if (name.empty()) return base;
            return vm.getV(base, name);
        }
        return lookupVar(c, n);
    }

    void setVariable(Ctx& c, const std::string& n, const Value& v) {
        if (isPath(n)) {
            Value base; std::string name;
            if (resolvePath(c, n, base, name) && !name.empty()) vm.setV(base, name, v);
            return;
        }
        for (auto it = c.withs.rbegin(); it != c.withs.rend(); ++it)
            if (vm.has(*it, n)) { vm.set(*it, n, v); return; }
        for (auto it = c.scope.rbegin(); it != c.scope.rend(); ++it) {
            Object* s = *it;
            if (!s || s == vm.global) continue;
            if (vm.has(s, n)) { vm.set(s, n, v); return; }
        }
        // Not declared anywhere: the current timeline (the innermost clip object in the scope chain).
        for (auto it = c.scope.rbegin(); it != c.scope.rend(); ++it) {
            Object* s = *it;
            if (s && (s->kind == ObjKind::Clip)) { vm.set(s, n, v); return; }
        }
        if (Object* t = targetObj(c)) { vm.set(t, n, v); return; }
        vm.set(vm.global, n, v);
    }

    void defineLocal(Ctx& c, const std::string& n, const Value& v) {
        Object* where = c.activation ? c.activation : nullptr;
        if (!where) {
            for (auto it = c.scope.rbegin(); it != c.scope.rend(); ++it)
                if (*it && (*it)->kind == ObjKind::Clip) { where = *it; break; }
        }
        if (!where) where = targetObj(c);
        if (where) vm.set(where, n, v);
    }

    // ---- frame / clip helpers ----
    gfx::DisplayObject* targetFromValue(Ctx& c, const Value& v) {
        if (v.isObject() && v.o->display) return v.o->display;
        std::string s = vm.toString(v);
        if (s.empty()) return c.target;
        return player()->resolveTarget(s, c.target);
    }

    // ---- function definition ----
    Object* defineFunction(Ctx& c, const uint8_t* body, size_t len, bool v2, size_t afterPos, size_t& codeSize) {
        auto sc = std::make_shared<ScriptCode>();
        size_t p = 0;
        auto rstr = [&]() { std::string s; while (p < len && body[p]) s += (char)body[p++]; ++p; return s; };
        auto ru16 = [&]() { uint16_t v = (uint16_t)(body[p] | (body[p + 1] << 8)); p += 2; return v; };
        sc->name = rstr();
        int n = ru16();
        sc->v2 = v2;
        if (v2) {
            sc->regCount = body[p++];
            sc->flags = ru16();
            for (int i = 0; i < n; ++i) { uint8_t r = body[p++]; std::string nm = rstr(); sc->params.push_back({r, nm}); }
        } else {
            for (int i = 0; i < n; ++i) sc->params.push_back({0, rstr()});
        }
        codeSize = ru16();
        sc->code = c.keep;
        sc->start = afterPos;
        sc->length = codeSize;
        std::vector<Object*> scope = c.scope;
        for (Object* w : c.withs) scope.push_back(w);
        gfx::DisplayObject* defTarget = c.target;
        return vm.newScriptFunction(sc, scope, c.pool, defTarget);
    }

    // ---- main loop over [pc, end) ----
    Flow run(Ctx& c, size_t pc, size_t end, size_t& jumpTo) {
        const uint8_t* d = c.code;
        while (pc < end) {
            if (++vm.instructions % 4096 == 0 && vm.instructions > 50'000'000) {
                LOG_WARN("AVM1 instruction budget exceeded; aborting script");
                return Flow::Return;
            }
            uint8_t op = d[pc];
            {   // diagnostics: WFC_GFX_OPTRACE=<target path substring> logs every op run on that timeline
                static const char* ot = std::getenv("WFC_GFX_OPTRACE");
                if (ot && c.target && c.target->targetPath().find(ot) != std::string::npos)
                    LOG_INFO("AVM1 op %04zx %02x stack %zu top %s", pc - pcLow_, op, c.stack.size(),
                             c.stack.empty() ? "-" : vm.toString(c.stack.back()).substr(0, 40).c_str());
            }
            size_t opPos = pc;
            ++pc;
            uint16_t len = 0;
            if (op >= 0x80) { len = (uint16_t)(d[pc] | (d[pc + 1] << 8)); pc += 2; }
            const uint8_t* a = d + pc;
            size_t next = pc + len;
            if (op == 0) return Flow::End;
            switch (op) {
            // ---------------- SWF 3 ----------------
            case 0x04: if (auto* mc = targetClip(c)) player()->gotoFrame(mc, mc->frame + 1, false); break;
            case 0x05: if (auto* mc = targetClip(c)) player()->gotoFrame(mc, std::max(0, mc->frame - 1), false); break;
            case 0x06: if (auto* mc = targetClip(c)) mc->playing = true; break;
            case 0x07: if (auto* mc = targetClip(c)) mc->playing = false; break;
            case 0x08: case 0x09: break;
            case 0x81: if (auto* mc = targetClip(c)) player()->gotoFrame(mc, a[0] | (a[1] << 8), false); break;
            case 0x83: {   // GetURL
                std::string url((const char*)a), tgt((const char*)a + url.size() + 1);
                fsOrUrl(url, tgt);
                break;
            }
            case 0x8A: break;   // WaitForFrame (all frames loaded)
            case 0x8B: {   // SetTarget
                std::string t((const char*)a);
                setTarget(c, t.empty() ? c.origTarget : player()->resolveTarget(t, c.origTarget));
                break;
            }
            case 0x8C: if (auto* mc = targetClip(c)) {
                int f = player()->resolveFrame(mc, Value(std::string((const char*)a)));
                if (f >= 0) player()->gotoFrame(mc, f, false);
                break;
            }
            // ---------------- SWF 4 ----------------
            case 0x96: pushOp(c, a, len); break;
            case 0x17: pop(c); break;
            case 0x0A: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(x + b)); break; }
            case 0x0B: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(x - b)); break; }
            case 0x0C: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(x * b)); break; }
            case 0x0D: {
                double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c));
                push(c, Value(b == 0 && vm.swfVersion < 5 ? std::numeric_limits<double>::quiet_NaN() : x / b));
                break;
            }
            case 0x0E: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(x == b)); break; }
            case 0x0F: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(x < b)); break; }
            case 0x10: { bool b = vm.toBool(pop(c)), x = vm.toBool(pop(c)); push(c, Value(x && b)); break; }
            case 0x11: { bool b = vm.toBool(pop(c)), x = vm.toBool(pop(c)); push(c, Value(x || b)); break; }
            case 0x12: push(c, Value(!vm.toBool(pop(c)))); break;
            case 0x13: { std::string b = vm.toString(pop(c)), x = vm.toString(pop(c)); push(c, Value(x == b)); break; }
            case 0x14: case 0x31: push(c, Value((double)utf8Length(vm.toString(pop(c))))); break;
            case 0x15: case 0x35: {   // StringExtract (1-based)
                int count = (int)vm.toNumber(pop(c)), idx = (int)vm.toNumber(pop(c));
                std::u16string s = toU16(vm.toString(pop(c)));
                idx = std::max(0, idx - 1);
                if (idx > (int)s.size()) idx = (int)s.size();
                if (count < 0 || idx + count > (int)s.size()) count = (int)s.size() - idx;
                push(c, Value(fromU16(s.substr((size_t)idx, (size_t)count))));
                break;
            }
            case 0x29: { std::string b = vm.toString(pop(c)), x = vm.toString(pop(c)); push(c, Value(x < b)); break; }
            case 0x18: { double v = vm.toNumber(pop(c)); push(c, Value(std::isfinite(v) ? std::trunc(v) : (std::isnan(v) ? 0.0 : v))); break; }
            case 0x32: case 0x36: { std::u16string s = toU16(vm.toString(pop(c))); push(c, Value(s.empty() ? 0.0 : (double)s[0])); break; }
            case 0x33: case 0x37: { std::u16string s(1, (char16_t)vm.toNumber(pop(c))); push(c, Value(fromU16(s))); break; }
            case 0x21: { std::string b = vm.toString(pop(c)), x = vm.toString(pop(c)); push(c, Value(x + b)); break; }
            case 0x99: {   // Jump
                int16_t off = (int16_t)(a[0] | (a[1] << 8));
                size_t to = (size_t)((long)next + off);
                if (to < pcLow_ || to > end) { jumpTo = to; return Flow::Jump; }
                pc = to;
                continue;
            }
            case 0x9D: {   // If
                int16_t off = (int16_t)(a[0] | (a[1] << 8));
                if (vm.toBool(pop(c))) {
                    size_t to = (size_t)((long)next + off);
                    if (to < pcLow_ || to > end) { jumpTo = to; return Flow::Jump; }
                    pc = to;
                    continue;
                }
                break;
            }
            case 0x9E: {   // Call (frame actions of a frame)
                Value f = pop(c);
                if (auto* mc = targetClip(c)) {
                    int fr = player()->resolveFrame(mc, f);
                    if (fr >= 0 && mc->sprite && fr < (int)mc->sprite->frames.size())
                        for (const auto& ab : mc->sprite->frames[(size_t)fr].actions)
                            vm.runBlock(ab.code, 0, ab.code->size(), mc);
                }
                break;
            }
            case 0x1C: { std::string n = vm.toString(pop(c)); push(c, getVariable(c, n)); break; }
            case 0x1D: { Value v = pop(c); std::string n = vm.toString(pop(c)); setVariable(c, n, v); break; }
            case 0x9A: {   // GetURL2
                std::string tgt = vm.toString(pop(c)), url = vm.toString(pop(c));
                fsOrUrl(url, tgt);
                break;
            }
            case 0x9F: {   // GotoFrame2
                uint8_t flags = a[0];
                int bias = (flags & 2) ? (a[1] | (a[2] << 8)) : 0;
                Value f = pop(c);
                gfx::MovieClip* mc = targetClip(c);
                if (f.t == VType::String) {
                    std::string s = f.s;
                    size_t colon = s.rfind(':');
                    if (colon != std::string::npos) {
                        gfx::DisplayObject* t = player()->resolveTarget(s.substr(0, colon), c.target);
                        mc = t && t->kind == gfx::DisplayObject::Kind::Clip ? static_cast<gfx::MovieClip*>(t) : nullptr;
                        f = Value(s.substr(colon + 1));
                    }
                }
                if (mc) {
                    int fr = player()->resolveFrame(mc, f);
                    if (fr >= 0) player()->gotoFrame(mc, fr + bias, flags & 1);
                }
                break;
            }
            case 0x20: setTarget(c, targetFromValue(c, pop(c))); break;
            case 0x22: {   // GetProperty
                int idx = (int)vm.toNumber(pop(c));
                gfx::DisplayObject* t = targetFromValue(c, pop(c));
                push(c, t ? vm.get(player()->scriptObject(t), propName(idx)) : Value::undef());
                break;
            }
            case 0x23: {
                Value v = pop(c);
                int idx = (int)vm.toNumber(pop(c));
                gfx::DisplayObject* t = targetFromValue(c, pop(c));
                if (t) vm.set(player()->scriptObject(t), propName(idx), v);
                break;
            }
            case 0x24: {   // CloneSprite
                int depth = (int)vm.toNumber(pop(c));
                std::string name = vm.toString(pop(c));
                gfx::DisplayObject* src = targetFromValue(c, pop(c));
                if (src) player()->duplicate(src, name, depth - gfx::kDepthOffset, nullptr);
                break;
            }
            case 0x25: { gfx::DisplayObject* t = targetFromValue(c, pop(c)); if (t && t->scripted) player()->removeObject(t); break; }
            case 0x27: { pop(c); if (vm.toBool(pop(c))) { pop(c); pop(c); pop(c); pop(c); } break; }   // StartDrag
            case 0x28: break;
            case 0x8D: pop(c); break;   // WaitForFrame2
            case 0x26: { std::string m = vm.toString(pop(c)); if (vm.traceSink) vm.traceSink(m); break; }
            case 0x34: push(c, Value(std::floor(player()->timeMs()))); break;
            case 0x30: { int mx = (int)vm.toNumber(pop(c)); push(c, Value(mx > 0 ? (double)(rng()() % (unsigned)mx) : 0.0)); break; }
            // ---------------- SWF 5 ----------------
            case 0x3D: {   // CallFunction
                std::string name = vm.toString(pop(c));
                Args args = popArgs(c);
                Value f = getVariable(c, name);
                if (f.isObject() && f.o->kind == ObjKind::Function) {
                    // A plain call's `this` is the calling timeline (AS1/AS2: foo() on _root sees this == _level0).
                    Object* t = targetObj(c);
                    Value self = t ? Value(t) : Value::undef();
                    push(c, vm.call(f, self, args));
                } else {
                    static const bool log = std::getenv("WFC_GFX_MISSINGFN") != nullptr;   // diagnostics
                    if (log) LOG_INFO("AVM1 CallFunction %s: not a function (target %s)", name.c_str(), c.target ? c.target->targetPath().c_str() : "-");
                    push(c, Value::undef());
                }
                break;
            }
            case 0x52: {   // CallMethod
                Value nameV = pop(c);
                Value obj = pop(c);
                Args args = popArgs(c);
                push(c, callMethodOp(c, obj, nameV, args));
                break;
            }
            case 0x88: {   // ConstantPool
                auto pool = std::make_shared<std::vector<std::string>>();
                uint16_t n = (uint16_t)(a[0] | (a[1] << 8));
                size_t p = 2;
                for (int i = 0; i < n && p < len; ++i) {
                    std::string s((const char*)a + p);
                    p += s.size() + 1;
                    pool->push_back(s);
                }
                c.pool = pool;
                break;
            }
            case 0x9B: case 0x8E: {
                size_t codeSize = 0;
                Object* f = defineFunction(c, a, len, op == 0x8E, next, codeSize);
                const std::string& nm = f->script->name;
                if (!nm.empty()) defineLocal(c, nm, Value(f));
                else push(c, Value(f));
                pc = next + codeSize;
                continue;
            }
            case 0x3C: { Value v = pop(c); std::string n = vm.toString(pop(c)); defineLocal(c, n, v); break; }
            case 0x41: {
                std::string n = vm.toString(pop(c));
                Object* where = c.activation;
                // `var x;` declares without overwriting an existing variable (e.g. one set by an initObject).
                if (!where) {
                    for (auto it = c.scope.rbegin(); it != c.scope.rend(); ++it)
                        if (*it && (*it)->kind == ObjKind::Clip) { where = *it; break; }
                    if (!where) where = targetObj(c);
                }
                if (where && !vm.has(where, n)) vm.set(where, n, Value::undef());
                break;
            }
            case 0x3A: {   // Delete
                std::string n = vm.toString(pop(c));
                Value o = pop(c);
                push(c, Value(o.isObject() ? vm.deleteProp(o.o, n) : false));
                break;
            }
            case 0x3B: {   // Delete2
                std::string n = vm.toString(pop(c));
                bool done = false;
                for (auto it = c.scope.rbegin(); it != c.scope.rend() && !done; ++it)
                    if (*it && (*it)->findOwn(n)) done = vm.deleteProp(*it, n);
                push(c, Value(done));
                break;
            }
            case 0x46: {   // Enumerate
                Value o = getVariable(c, vm.toString(pop(c)));
                push(c, Value::null());
                if (o.isObject()) for (const std::string& k : vm.enumerate(o.o)) push(c, Value(k));
                break;
            }
            case 0x55: {   // Enumerate2
                Value o = pop(c);
                push(c, Value::null());
                if (o.isObject()) for (const std::string& k : vm.enumerate(o.o)) push(c, Value(k));
                break;
            }
            case 0x49: { Value b = pop(c), x = pop(c); push(c, Value(vm.looseEquals(x, b))); break; }
            case 0x4E: {   // GetMember
                Value nameV = pop(c);
                Value o = pop(c);
                push(c, vm.getV(o, keyOf(nameV)));
                break;
            }
            case 0x42: {   // InitArray
                int n = (int)vm.toNumber(pop(c));
                std::vector<Value> el;
                for (int i = 0; i < n && i < 100000; ++i) el.push_back(pop(c));
                push(c, Value(vm.newArray(el)));
                break;
            }
            case 0x43: {   // InitObject
                int n = (int)vm.toNumber(pop(c));
                Object* o = vm.newPlain();
                std::vector<std::pair<std::string, Value>> kv;
                for (int i = 0; i < n && i < 100000; ++i) { Value v = pop(c); std::string k = vm.toString(pop(c)); kv.push_back({k, v}); }
                for (auto it = kv.rbegin(); it != kv.rend(); ++it) vm.set(o, it->first, it->second);
                push(c, Value(o));
                break;
            }
            case 0x53: {   // NewMethod
                Value nameV = pop(c);
                Value obj = pop(c);
                Args args = popArgs(c);
                Value ctor = (nameV.isNullish() || (nameV.isString() && nameV.s.empty())) ? obj : vm.getV(obj, keyOf(nameV));
                push(c, ctor.isObject() ? vm.construct(ctor.o, args) : Value::undef());
                break;
            }
            case 0x40: {   // NewObject
                std::string name = vm.toString(pop(c));
                Args args = popArgs(c);
                Value ctor = getVariable(c, name);
                push(c, ctor.isObject() ? vm.construct(ctor.o, args) : Value::undef());
                break;
            }
            case 0x4F: {   // SetMember
                Value v = pop(c);
                Value nameV = pop(c);
                Value o = pop(c);
                if (o.isObject()) vm.set(o.o, keyOf(nameV), v);
                break;
            }
            case 0x45: {   // TargetPath
                Value v = pop(c);
                push(c, v.isObject() && v.o->display ? Value(v.o->display->targetPath()) : Value::undef());
                break;
            }
            case 0x94: {   // With
                uint16_t size = (uint16_t)(a[0] | (a[1] << 8));
                Value o = pop(c);
                size_t blockStart = next, blockEnd = next + size;
                if (o.isObject()) c.withs.push_back(o.o);
                size_t savedLow = pcLow_;
                pcLow_ = blockStart;
                size_t jt = 0;
                Flow f = run(c, blockStart, blockEnd, jt);
                pcLow_ = savedLow;
                if (o.isObject()) c.withs.pop_back();
                if (f == Flow::Return) return f;
                if (f == Flow::Jump) {
                    if (jt < pcLow_ || jt > end) { jumpTo = jt; return Flow::Jump; }
                    pc = jt;
                    continue;
                }
                pc = blockEnd;
                continue;
            }
            case 0x4A: push(c, Value(vm.toNumber(pop(c)))); break;
            case 0x4B: push(c, Value(vm.toString(pop(c)))); break;
            case 0x44: push(c, Value(vm.typeOf(pop(c)))); break;
            case 0x47: {   // Add2
                Value b = vm.toPrimitive(pop(c)), x = vm.toPrimitive(pop(c));
                if (x.t == VType::String || b.t == VType::String) push(c, Value(vm.toString(x) + vm.toString(b)));
                else push(c, Value(vm.toNumber(x) + vm.toNumber(b)));
                break;
            }
            case 0x48: { Value b = pop(c), x = pop(c); push(c, vm.lessThan(x, b)); break; }
            case 0x67: { Value b = pop(c), x = pop(c); push(c, vm.lessThan(b, x)); break; }
            case 0x3F: { double b = vm.toNumber(pop(c)), x = vm.toNumber(pop(c)); push(c, Value(std::fmod(x, b))); break; }
            case 0x60: { int32_t b = vm.toInt32(pop(c)), x = vm.toInt32(pop(c)); push(c, Value((double)(x & b))); break; }
            case 0x61: { int32_t b = vm.toInt32(pop(c)), x = vm.toInt32(pop(c)); push(c, Value((double)(x | b))); break; }
            case 0x62: { int32_t b = vm.toInt32(pop(c)), x = vm.toInt32(pop(c)); push(c, Value((double)(x ^ b))); break; }
            case 0x63: { int32_t b = vm.toInt32(pop(c)), x = vm.toInt32(pop(c)); push(c, Value((double)(int32_t)((uint32_t)x << (b & 31)))); break; }
            case 0x64: { int32_t b = vm.toInt32(pop(c)), x = vm.toInt32(pop(c)); push(c, Value((double)(x >> (b & 31)))); break; }
            case 0x65: { int32_t b = vm.toInt32(pop(c)); uint32_t x = (uint32_t)vm.toInt32(pop(c)); push(c, Value((double)(x >> (b & 31)))); break; }
            case 0x50: push(c, Value(vm.toNumber(pop(c)) + 1)); break;
            case 0x51: push(c, Value(vm.toNumber(pop(c)) - 1)); break;
            case 0x4C: { Value v = c.stack.empty() ? Value::undef() : c.stack.back(); push(c, v); break; }
            case 0x4D: { Value b = pop(c), x = pop(c); push(c, b); push(c, x); break; }
            case 0x3E: c.retval = pop(c); c.returned = true; return Flow::Return;
            case 0x87: {   // StoreRegister
                uint8_t r = a[0];
                if (r < c.regs.size()) c.regs[r] = c.stack.empty() ? Value::undef() : c.stack.back();
                break;
            }
            // ---------------- SWF 6 / 7 ----------------
            case 0x54: {   // InstanceOf
                Value ctor = pop(c), o = pop(c);
                push(c, Value(ctor.isObject() && vm.instanceOf(o, ctor.o)));
                break;
            }
            case 0x66: { Value b = pop(c), x = pop(c); push(c, Value(vm.strictEquals(x, b))); break; }
            case 0x68: { std::string b = vm.toString(pop(c)), x = vm.toString(pop(c)); push(c, Value(x > b)); break; }
            case 0x69: {   // Extends
                Value superC = pop(c), subC = pop(c);
                if (superC.isObject() && subC.isObject()) {
                    Value sp = vm.get(superC.o, "prototype");
                    Object* proto = vm.newObject(sp.isObject() ? sp.o : vm.objectProto);
                    proto->setRaw("__constructor__", superC, DontEnum);
                    subC.o->setRaw("prototype", Value(proto), DontEnum);
                }
                break;
            }
            case 0x2B: {   // CastOp
                Value o = pop(c), ctor = pop(c);
                push(c, ctor.isObject() && vm.instanceOf(o, ctor.o) ? o : Value::null());
                break;
            }
            case 0x2C: {   // ImplementsOp
                Value ctor = pop(c);
                int n = (int)vm.toNumber(pop(c));
                std::vector<Value> ifs;
                for (int i = 0; i < n && i < 1000; ++i) ifs.push_back(pop(c));
                if (ctor.isObject()) {
                    Value pr = vm.get(ctor.o, "prototype");
                    if (pr.isObject()) pr.o->setRaw("__implements__", Value(vm.newArray(ifs)), DontEnum);
                }
                break;
            }
            case 0x2A: throw ScriptThrow{pop(c)};
            case 0x8F: {   // Try
                uint8_t flags = a[0];
                uint16_t trySize = (uint16_t)(a[1] | (a[2] << 8)), catchSize = (uint16_t)(a[3] | (a[4] << 8)),
                         finSize = (uint16_t)(a[5] | (a[6] << 8));
                bool catchInReg = flags & 4;
                std::string catchName;
                uint8_t catchReg = 0;
                if (catchInReg) catchReg = a[7]; else catchName = std::string((const char*)a + 7);
                size_t tryStart = next, catchStart = tryStart + trySize, finStart = catchStart + catchSize, after = finStart + finSize;
                Flow f = Flow::End;
                size_t jt = 0;
                bool threw = false;
                Value exc;
                size_t savedStack = c.stack.size();
                try {
                    size_t savedLow = pcLow_; pcLow_ = tryStart;
                    f = run(c, tryStart, catchStart, jt);
                    pcLow_ = savedLow;
                } catch (const ScriptThrow& t) { threw = true; exc = t.v; c.stack.resize(std::min(c.stack.size(), savedStack)); }
                if (threw && (flags & 1)) {
                    if (catchInReg) { if (catchReg < c.regs.size()) c.regs[catchReg] = exc; }
                    else defineLocal(c, catchName, exc);
                    size_t savedLow = pcLow_; pcLow_ = catchStart;
                    f = run(c, catchStart, finStart, jt);
                    pcLow_ = savedLow;
                    threw = false;
                }
                if (finSize) {
                    size_t savedLow = pcLow_; pcLow_ = finStart;
                    size_t jt2 = 0;
                    Flow f2 = run(c, finStart, after, jt2);
                    pcLow_ = savedLow;
                    if (f2 != Flow::End) { if (f2 == Flow::Jump) { jumpTo = jt2; } return f2; }
                }
                if (threw) throw ScriptThrow{exc};
                if (f == Flow::Return) return f;
                if (f == Flow::Jump) { if (jt < pcLow_ || jt > end) { jumpTo = jt; return Flow::Jump; } pc = jt; continue; }
                pc = after;
                continue;
            }
            default:
                if (!unknownLogged_[op]) { unknownLogged_[op] = true; LOG_WARN("AVM1 unsupported action 0x%02X at %zu", op, opPos); }
                break;
            }
            pc = next;
        }
        return Flow::End;
    }

    size_t pcLow_ = 0;
    bool unknownLogged_[256] = {};

    std::string keyOf(const Value& v) {
        if (v.t == VType::Number) return VM::numberToString(v.n);
        return vm.toString(v);
    }

    Args popArgs(Ctx& c) {
        int n = (int)vm.toNumber(pop(c));
        Args args;
        for (int i = 0; i < n && i < 10000; ++i) args.push_back(pop(c));
        return args;
    }

    Value callMethodOp(Ctx& c, const Value& obj, const Value& nameV, Args& args) {
        static const bool logMissing = std::getenv("WFC_GFX_MISSINGFN") != nullptr;   // diagnostics
        if (logMissing && !obj.isObject() && nameV.isString())
            LOG_INFO("AVM1 CallMethod %s on %s (target %s)", nameV.s.c_str(), vm.typeOf(obj).c_str(), c.target ? c.target->targetPath().c_str() : "-");
        bool noName = nameV.isNullish() || (nameV.isString() && nameV.s.empty());
        if (noName) {
            // Call the object itself; super() calls the superclass constructor with the current this.
            if (obj.isObject() && obj.o->kind == ObjKind::Super) {
                Value ctor = vm.get(obj.o->superProto, "__constructor__");
                if (!ctor.isObject()) ctor = vm.get(obj.o->superProto, "constructor");
                Object* sp = nullptr;
                if (ctor.isObject()) { Value pr = vm.get(ctor.o, "prototype"); sp = pr.isObject() ? pr.o->proto : nullptr; }
                return vm.call(ctor, Value(obj.o->superThis), args, sp);
            }
            return vm.call(obj, Value::undef(), args);
        }
        std::string name = keyOf(nameV);
        if (obj.isObject() && obj.o->kind == ObjKind::Super) {
            Object* owner = vm.findOwner(obj.o->superProto, name);
            Value f = owner ? owner->findOwn(name)->v : Value::undef();
            return vm.call(f, Value(obj.o->superThis), args, owner ? owner->proto : nullptr);
        }
        Value f = vm.getV(obj, name);
        if (!f.isObject() || f.o->kind != ObjKind::Function) return Value::undef();
        Object* owner = obj.isObject() ? vm.findOwner(obj.o, name) : nullptr;
        return vm.call(f, obj, args, owner ? owner->proto : nullptr);
    }

    void setTarget(Ctx& c, gfx::DisplayObject* t) {
        if (!t) t = c.origTarget;
        Object* oldObj = c.target ? player()->scriptObject(c.target) : nullptr;
        c.target = t;
        Object* newObj = t ? player()->scriptObject(t) : nullptr;
        for (auto& s : c.scope) if (s == oldObj) s = newObj;
    }

    void fsOrUrl(const std::string& url, const std::string& target) {
        if (url.rfind("FSCommand:", 0) == 0 || url.rfind("fscommand:", 0) == 0) {
            if (vm.fsCommand) vm.fsCommand(url.substr(10), target);
        } else {
            LOG_INFO("AVM1 getURL ignored: %s (%s)", url.c_str(), target.c_str());
        }
    }

    static const char* propName(int idx) {
        static const char* n[] = {"_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha", "_visible",
                                  "_width", "_height", "_rotation", "_target", "_framesloaded", "_name", "_droptarget",
                                  "_url", "_highquality", "_focusrect", "_soundbuftime", "_quality", "_xmouse", "_ymouse"};
        return idx >= 0 && idx < 22 ? n[idx] : "";
    }

    void pushOp(Ctx& c, const uint8_t* a, uint16_t len) {
        size_t p = 0;
        while (p < len) {
            uint8_t t = a[p++];
            switch (t) {
            case 0: { std::string s((const char*)a + p); p += s.size() + 1; push(c, Value(s)); break; }
            case 1: { float f; std::memcpy(&f, a + p, 4); p += 4; push(c, Value((double)f)); break; }
            case 2: push(c, Value::null()); break;
            case 3: push(c, Value::undef()); break;
            case 4: { uint8_t r = a[p++]; push(c, r < c.regs.size() ? c.regs[r] : Value::undef()); break; }
            case 5: push(c, Value(a[p++] != 0)); break;
            case 6: {
                uint8_t b[8];
                std::memcpy(b, a + p + 4, 4);
                std::memcpy(b + 4, a + p, 4);
                double d; std::memcpy(&d, b, 8);
                p += 8;
                push(c, Value(d));
                break;
            }
            case 7: { int32_t v; std::memcpy(&v, a + p, 4); p += 4; push(c, Value((double)v)); break; }
            case 8: { uint8_t i = a[p++]; push(c, c.pool && i < c.pool->size() ? Value((*c.pool)[i]) : Value::undef()); break; }
            case 9: { uint16_t i = (uint16_t)(a[p] | (a[p + 1] << 8)); p += 2; push(c, c.pool && i < c.pool->size() ? Value((*c.pool)[i]) : Value::undef()); break; }
            default: return;
            }
        }
    }

    static std::u16string toU16(const std::string& s) {
        std::u16string o;
        for (size_t i = 0; i < s.size();) {
            unsigned char ch = (unsigned char)s[i];
            uint32_t cp; int n;
            if (ch < 0x80) { cp = ch; n = 1; }
            else if ((ch & 0xE0) == 0xC0) { cp = ch & 0x1F; n = 2; }
            else if ((ch & 0xF0) == 0xE0) { cp = ch & 0x0F; n = 3; }
            else { cp = ch & 0x07; n = 4; }
            for (int k = 1; k < n && i + (size_t)k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3F);
            i += (size_t)n;
            o.push_back((char16_t)(cp > 0xFFFF ? 0xFFFD : cp));
        }
        return o;
    }
    static std::string fromU16(const std::u16string& s) {
        std::string o;
        for (char16_t ch : s) {
            uint32_t cp = ch;
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
            else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        }
        return o;
    }
};

// ---------------------------------------------------------------------------------------------------------------

Value VM::call(const Value& fnV, const Value& self, Args& args, Object* superProto) {
    if (!fnV.isObject() || fnV.o->kind != ObjKind::Function) return Value::undef();
    Object* fn = fnV.o;
    if (fn->zombie) zombieUse(fn, "call", fn->className);
    if (self.isObject() && self.o->zombie) zombieUse(self.o, "call this", fn->className);
    if (depth_ > 200) { LOG_WARN("AVM1 call depth exceeded"); return Value::undef(); }
    if (depth_ == 0) instructions = 0;   // per top-level entry budget
    struct DepthGuard { int& d; DepthGuard(int& x) : d(x) { ++d; } ~DepthGuard() { --d; } } guard(depth_);
    if (fn->native) return fn->native(*this, self, args);
    if (!fn->script) return Value::undef();
    const ScriptCode& sc = *fn->script;
    Ctx c;
    c.keep = sc.code;
    c.code = sc.code->data();
    c.pool = fn->pool;
    c.scope = fn->scope;
    Object* act = newObject(nullptr);
    c.activation = act;
    c.scope.push_back(act);
    c.thisv = self.isNullish() ? Value::undef() : self;
    // _parent / _root / unqualified timeline calls inside a function use the timeline the function was defined on
    // (its scope), not `this`. Functions defined outside any timeline fall back to the receiving clip.
    // A function whose defining timeline was removed keeps that (removed) timeline: its "" target operations act on the
    // dead clip, as in Flash, not on the root. (CustomTransformers' weapon menu background registers a Stage listener
    // that outlives it; its onResize sets _width / _height = Stage size + 30 - on the root that shrank and shifted the
    // whole Create a Character movie after a weapon slot visit.) Only functions without a defining timeline fall back.
    c.target = fn->defTarget;
    if (!c.target && self.isObject() && self.o->display && self.o->display->kind == gfx::DisplayObject::Kind::Clip)
        c.target = self.o->display;
    if (!c.target || (c.target->removed && !fn->defTarget)) c.target = player_->root();
    c.origTarget = c.target;
    c.superProto = superProto;
    c.callee = fn;
    Object* argsObj = newArray(args);
    argsObj->setRaw("callee", Value(fn), DontEnum);
    if (sc.v2) {
        c.regs.assign(std::max<size_t>(sc.regCount, 1) + 1, Value::undef());
        uint8_t r = 1;
        uint16_t f = sc.flags;
        if (f & 0x0001) { c.regs[r++] = c.thisv; }
        if (f & 0x0004) { c.regs[r++] = Value(argsObj); }
        if (f & 0x0010) {
            Object* s = newObject(nullptr);
            s->kind = ObjKind::Super;
            s->superThis = self.isObject() ? self.o : nullptr;
            s->superProto = superProto;
            c.regs[r++] = Value(s);
        }
        if (f & 0x0040) c.regs[r++] = Value(player_->scriptObject(c.target ? c.target->rootClip() : player_->root()));
        if (f & 0x0080) c.regs[r++] = c.target && c.target->parent ? Value(player_->scriptObject(c.target->parent)) : Value::undef();
        if (f & 0x0100) c.regs[r++] = Value(global);
        if (!(f & 0x0008) && !(f & 0x0004)) act->setRaw("arguments", Value(argsObj));
        if (!(f & 0x0002) && !(f & 0x0001)) act->setRaw("this", c.thisv);
        for (size_t i = 0; i < sc.params.size(); ++i) {
            Value v = i < args.size() ? args[i] : Value::undef();
            if (sc.params[i].first && sc.params[i].first < c.regs.size()) c.regs[sc.params[i].first] = v;
            else act->setRaw(sc.params[i].second, v);
        }
        if (r > c.regs.size()) c.regs.resize(r);
    } else {
        c.regs.assign(4, Value::undef());
        act->setRaw("arguments", Value(argsObj));
        act->setRaw("this", c.thisv);
        if (superProto) {
            Object* s = newObject(nullptr);
            s->kind = ObjKind::Super;
            s->superThis = self.isObject() ? self.o : nullptr;
            s->superProto = superProto;
            act->setRaw("super", Value(s));
        }
        for (size_t i = 0; i < sc.params.size(); ++i)
            act->setRaw(sc.params[i].second, i < args.size() ? args[i] : Value::undef());
    }
    Interp in{*this};
    in.pcLow_ = sc.start;
    size_t jt = 0;
    c.end = sc.start + sc.length;
    struct TargetScope { VM& v; gfx::DisplayObject* prev; ~TargetScope() { v.currentTarget = prev; } } ts{*this, currentTarget};
    currentTarget = c.target;
    in.run(c, sc.start, c.end, jt);
    return c.returned ? c.retval : Value::undef();
}

void VM::runBlock(const std::shared_ptr<std::vector<uint8_t>>& code, size_t start, size_t len, gfx::DisplayObject* target,
                  Object* thisObj) {
    if (!code || len == 0) return;
    Ctx c;
    c.keep = code;
    c.code = code->data();
    c.end = start + len;
    c.regs.assign(4, Value::undef());
    Object* tObj = target ? player_->scriptObject(target) : nullptr;
    c.scope = {global};
    if (tObj) c.scope.push_back(tObj);
    c.thisv = thisObj ? Value(thisObj) : (tObj ? Value(tObj) : Value::undef());
    c.target = target;
    c.origTarget = target;
    Interp in{*this};
    in.pcLow_ = start;
    size_t jt = 0;
    instructions = 0;
    struct TargetScope { VM& v; gfx::DisplayObject* prev; ~TargetScope() { v.currentTarget = prev; } } ts{*this, currentTarget};
    currentTarget = target;
    try {
        in.run(c, start, start + len, jt);
    } catch (const ScriptThrow& t) {
        LOG_WARN("AVM1 uncaught exception: %s", toString(t.v).c_str());
    }
}

} // namespace gfx::avm1
