#include "ui/gfx/Avm1.h"
#include "ui/gfx/Display.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>

namespace gfx::avm1 {

namespace {
struct AtomTable {
    std::unordered_map<std::string, uint32_t> ids;
    std::vector<const std::string*> names;
};
AtomTable& atoms() { static AtomTable t; return t; }
}

uint32_t atomIntern(const std::string& s) {
    AtomTable& t = atoms();
    auto it = t.ids.find(s);
    if (it != t.ids.end()) return it->second;
    const uint32_t id = (uint32_t)t.ids.size() + 1;
    auto ins = t.ids.emplace(s, id).first;
    t.names.push_back(&ins->first);
    return id;
}

size_t atomCount() { return atoms().names.size(); }

uint32_t atomFind(const std::string& s) {
    const AtomTable& t = atoms();
    auto it = t.ids.find(s);
    return it == t.ids.end() ? 0 : it->second;
}

bool Object::removeOwn(const std::string& k) {
    const int i = slotOf(atomFind(k));
    if (i < 0) return false;
    props.erase(props.begin() + i);
    slotAtoms.erase(slotAtoms.begin() + i);
    index.clear();
    if (props.size() > kLinearProps) for (size_t j = 0; j < slotAtoms.size(); ++j) index[slotAtoms[j]] = (uint32_t)j;
    return true;
}

std::string VM::defaultVersionString = "XBOX360 8,0,0,0";

VM::VM(gfx::Player* player) : player_(player) {
    installBuiltins();
    installDisplayBuiltins();
}

VM::~VM() = default;

// ---------------------------------------------------------------------------------------------------------------
// Heap

Object* VM::newObject(Object* proto) {
    heap_.push_back(std::make_unique<Object>(ObjKind::Plain));
    Object* o = heap_.back().get();
    o->proto = proto;
    return o;
}

Object* VM::newPlain() { return newObject(objectProto); }

Object* VM::newArray(const std::vector<Value>& elems) {
    Object* o = newObject(arrayProto);
    o->kind = ObjKind::Array;
    o->elems = elems;
    return o;
}

Object* VM::newFunction(NativeFn fn, const std::string& name, int length) {
    Object* f = newObject(functionProto);
    f->kind = ObjKind::Function;
    f->native = std::move(fn);
    f->className = name;
    (void)length;
    return f;
}

Object* VM::newScriptFunction(const std::shared_ptr<ScriptCode>& code, const std::vector<Object*>& scope,
                              const std::shared_ptr<ConstPool>& pool, gfx::DisplayObject* target) {
    Object* f = newObject(functionProto);
    f->kind = ObjKind::Function;
    f->script = code;
    f->scope = scope;
    f->pool = pool;
    f->defTarget = target;
    f->className = code->name;
    Object* proto = newPlain();
    proto->setRaw("constructor", Value(f), DontEnum);
    f->setRaw("prototype", Value(proto), DontEnum);
    return f;
}

Object* VM::newClipObject(gfx::DisplayObject* d, Object* proto) {
    Object* o = newObject(proto);
    o->kind = d->kind == gfx::DisplayObject::Kind::Text ? ObjKind::TextField : ObjKind::Clip;
    o->display = d;
    return o;
}

void VM::mark(Object* o) {
    std::vector<Object*> stack{o};
    while (!stack.empty()) {
        Object* x = stack.back();
        stack.pop_back();
        if (!x || x->marked) continue;
        x->marked = true;
        if (x->proto) stack.push_back(x->proto);
        for (auto& [k, p] : x->props) {
            if (p.v.t == VType::Object && p.v.o) stack.push_back(p.v.o);
            if (p.getter) stack.push_back(p.getter);
            if (p.setter) stack.push_back(p.setter);
        }
        for (Object* s : x->scope) stack.push_back(s);
        for (const Value& v : x->elems) if (v.t == VType::Object && v.o) stack.push_back(v.o);
        if (x->superThis) stack.push_back(x->superThis);
        if (x->superProto) stack.push_back(x->superProto);
        if (x->boxed.t == VType::Object && x->boxed.o) stack.push_back(x->boxed.o);
        for (auto& [k, w] : x->watches) { stack.push_back(w.first); if (w.second.t == VType::Object) stack.push_back(w.second.o); }
    }
}

void VM::collect(const std::vector<Object*>& extraRoots, const std::function<bool(VM&)>& extend) {
    const auto gcT0 = std::chrono::steady_clock::now();
    for (auto& o : heap_) o->marked = false;
    for (Object* r : {global, objectProto, functionProto, arrayProto, stringProto, numberProto, booleanProto, movieClipProto,
                      textFieldProto, textFormatCtor, dateProto, errorProto, objectCtor, arrayCtor, keyObj, stageObj, mouseObj})
        if (r) mark(r);
    for (auto& [k, c] : registeredClasses) mark(c);
    for (Object* r : extraRoots) mark(r);
    size_t before = heap_.size();
    // Objects bound to live display objects stay alive (their display owns them).
    for (auto& o : heap_)
        if (!o->marked && o->display && !o->display->removed) mark(o.get());
    if (extend) for (int guard = 0; guard < 64 && extend(*this); ++guard) {}
    static const bool gcCheck = std::getenv("WFC_GFX_GCCHECK") != nullptr;
    if (gcCheck) {   // diagnostics: keep collected objects as zombies and report any later use
        for (auto& o : heap_) if (!o->marked) { o->zombie = true; zombies_.push_back(std::move(o)); }
    }
    heap_.erase(std::remove_if(heap_.begin(), heap_.end(), [](const std::unique_ptr<Object>& o) { return !o || !o->marked; }), heap_.end());
    if (before - heap_.size() > 10000)
        LOG_INFO("AVM1 gc: %zu -> %zu objects (%.2f ms)", before, heap_.size(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gcT0).count());
}

// ---------------------------------------------------------------------------------------------------------------
// Conversions

int32_t VM::toInt32(double d) {
    if (!std::isfinite(d) || d == 0) return 0;
    double t = std::trunc(d);
    double m = std::fmod(t, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    uint32_t u = (uint32_t)m;
    return (int32_t)u;
}

std::string VM::numberToString(double d, int radix) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return "0";
    if (radix != 10 && radix >= 2 && radix <= 36) {
        int64_t v = (int64_t)d;
        bool neg = v < 0;
        uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
        std::string s;
        do { int dg = (int)(u % (uint64_t)radix); s += (char)(dg < 10 ? '0' + dg : 'a' + dg - 10); u /= (uint64_t)radix; } while (u);
        if (neg) s += '-';
        std::reverse(s.begin(), s.end());
        return s;
    }
    if (std::fabs(d) < 1e15 && d == std::trunc(d)) {
        // Whole numbers: the same digits "%.0f" prints, without snprintf (hot: indices, counters, data-store rows).
        int64_t v = (int64_t)d;
        char b[24];
        int p = 23;
        b[p] = 0;
        const bool neg = v < 0;
        uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
        do { b[--p] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
        if (neg) b[--p] = '-';
        return std::string(b + p, (size_t)(23 - p));
    }
    char b[64];
    std::snprintf(b, sizeof b, "%.15g", d);
    std::string s = b;
    size_t e = s.find('e');
    if (e != std::string::npos) {
        // "1e+21" style: strip leading zeros of the exponent.
        std::string mant = s.substr(0, e), ex = s.substr(e + 1);
        char sign = ex[0] == '-' ? '-' : '+';
        if (ex[0] == '-' || ex[0] == '+') ex = ex.substr(1);
        while (ex.size() > 1 && ex[0] == '0') ex = ex.substr(1);
        s = mant + "e" + sign + ex;
    }
    return s;
}

static double parseNumber(const std::string& str) {
    size_t a = 0, b = str.size();
    while (a < b && std::isspace((unsigned char)str[a])) ++a;
    while (b > a && std::isspace((unsigned char)str[b - 1])) --b;
    if (a == b) return std::numeric_limits<double>::quiet_NaN();
    std::string s = str.substr(a, b - a);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        char* end = nullptr;
        long long v = std::strtoll(s.c_str() + 2, &end, 16);
        if (end && *end == 0) return (double)v;
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (s == "Infinity" || s == "+Infinity") return INFINITY;
    if (s == "-Infinity") return -INFINITY;
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (!end || *end != 0) return std::numeric_limits<double>::quiet_NaN();
    return d;
}

Value VM::toPrimitive(const Value& v, bool hintString) {
    if (v.t != VType::Object || !v.o) return v;
    Object* o = v.o;
    if (o->kind == ObjKind::Boxed && o->boxed.t != VType::Object) {
        if (o->nativeType == "Date" && hintString) {
            Value r = callMethod(v, "toString", {});
            if (r.t != VType::Object) return r;
        }
        return o->boxed;
    }
    if (o->kind == ObjKind::Clip || o->kind == ObjKind::TextField) {
        if (o->display) return Value(o->display->targetPath());
    }
    const char* first = hintString ? "toString" : "valueOf";
    const char* second = hintString ? "valueOf" : "toString";
    for (const char* m : {first, second}) {
        Value f = get(o, m);
        if (f.isObject() && f.o->kind == ObjKind::Function) {
            Args a;
            Value r = call(f, v, a);
            if (r.t != VType::Object) return r;
        }
    }
    return Value(o->kind == ObjKind::Function ? "[type Function]" : "[object Object]");
}

double VM::toNumber(const Value& v) {
    switch (v.t) {
    case VType::Undefined: case VType::Null: return swfVersion >= 7 ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    case VType::Bool: return v.b ? 1.0 : 0.0;
    case VType::Number: return v.n;
    case VType::String: return parseNumber(v.s);
    case VType::Object: {
        Value p = toPrimitive(v, false);
        if (p.t == VType::Object) return std::numeric_limits<double>::quiet_NaN();
        return toNumber(p);
    }
    }
    return 0;
}

std::string VM::toString(const Value& v) {
    switch (v.t) {
    case VType::Undefined: return swfVersion >= 7 ? "undefined" : "";
    case VType::Null: return "null";
    case VType::Bool: return v.b ? "true" : "false";
    case VType::Number: return numberToString(v.n);
    case VType::String: return v.s;
    case VType::Object: {
        if (!v.o) return "null";
        Value p = toPrimitive(v, true);
        if (p.t == VType::Object) return "[object Object]";
        return toString(p);
    }
    }
    return "";
}

bool VM::toBool(const Value& v) const {
    switch (v.t) {
    case VType::Undefined: case VType::Null: return false;
    case VType::Bool: return v.b;
    case VType::Number: return v.n != 0 && !std::isnan(v.n);
    case VType::String:
        if (swfVersion >= 7) return !v.s.empty();
        return parseNumber(v.s) != 0;
    case VType::Object: return v.o != nullptr;
    }
    return false;
}

Object* VM::toObject(const Value& v) {
    switch (v.t) {
    case VType::Object: return v.o;
    case VType::String: { Object* o = newObject(stringProto); o->kind = ObjKind::Boxed; o->boxed = v; o->nativeType = "String"; return o; }
    case VType::Number: { Object* o = newObject(numberProto); o->kind = ObjKind::Boxed; o->boxed = v; o->nativeType = "Number"; return o; }
    case VType::Bool: { Object* o = newObject(booleanProto); o->kind = ObjKind::Boxed; o->boxed = v; o->nativeType = "Boolean"; return o; }
    default: return nullptr;
    }
}

std::string VM::typeOf(const Value& v) const {
    switch (v.t) {
    case VType::Undefined: return "undefined";
    case VType::Null: return "null";
    case VType::Bool: return "boolean";
    case VType::Number: return "number";
    case VType::String: return "string";
    case VType::Object:
        if (!v.o) return "null";
        if (v.o->kind == ObjKind::Function) return "function";
        if (v.o->kind == ObjKind::Clip) return "movieclip";
        return "object";
    }
    return "undefined";
}

bool VM::strictEquals(const Value& a, const Value& b) const {
    if (a.t != b.t) return false;
    switch (a.t) {
    case VType::Undefined: case VType::Null: return true;
    case VType::Bool: return a.b == b.b;
    case VType::Number: return a.n == b.n;
    case VType::String: return a.s == b.s;
    case VType::Object: return a.o == b.o;
    }
    return false;
}

bool VM::looseEquals(const Value& a, const Value& b) {
    if (a.t == b.t) return strictEquals(a, b);
    if (a.isNullish() && b.isNullish()) return true;
    if (a.isNullish() || b.isNullish()) return false;
    if (a.t == VType::Number && b.t == VType::String) return a.n == toNumber(b);
    if (a.t == VType::String && b.t == VType::Number) return toNumber(a) == b.n;
    if (a.t == VType::Bool) return looseEquals(Value(a.b ? 1.0 : 0.0), b);
    if (b.t == VType::Bool) return looseEquals(a, Value(b.b ? 1.0 : 0.0));
    if ((a.t == VType::Number || a.t == VType::String) && b.t == VType::Object) return looseEquals(a, toPrimitive(b));
    if (a.t == VType::Object && (b.t == VType::Number || b.t == VType::String)) return looseEquals(toPrimitive(a), b);
    return false;
}

Value VM::lessThan(const Value& a, const Value& b) {
    Value pa = toPrimitive(a), pb = toPrimitive(b);
    if (pa.t == VType::String && pb.t == VType::String) return Value(pa.s < pb.s);
    double x = toNumber(pa), y = toNumber(pb);
    if (std::isnan(x) || std::isnan(y)) return Value::undef();
    return Value(x < y);
}

bool VM::instanceOf(const Value& v, Object* ctor) {
    if (!v.isObject() || !ctor) return false;
    Value protoV = get(ctor, "prototype");
    Object* proto = protoV.isObject() ? protoV.o : nullptr;
    int guard = 0;
    for (Object* p = v.o->proto; p && guard < 256; p = p->proto, ++guard) {
        if (p == proto) return true;
        // ImplementsOp interfaces.
        if (const Property* impl = p->findOwn("__implements__"))
            if (impl->v.isObject())
                for (const Value& iv : impl->v.o->elems) if (iv.o == ctor) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Properties

static bool isIndex(const std::string& k, size_t& out) {
    if (k.empty() || k.size() > 9) return false;
    for (char c : k) if (c < '0' || c > '9') return false;
    if (k.size() > 1 && k[0] == '0') return false;
    out = (size_t)std::atoll(k.c_str());
    return true;
}

Object* VM::findOwner(Object* o, const std::string& key) { return findOwnerA(o, atomFind(key)); }

Object* VM::findOwnerA(Object* o, uint32_t a) {
    if (!a) return nullptr;
    int guard = 0;
    for (Object* p = o; p && guard < 256; p = p->proto, ++guard)
        if (p->findOwnA(a)) return p;
    return nullptr;
}

bool VM::has(Object* o, const std::string& key, uint32_t atomHint) {
    if (!o) return false;
    if (findOwnerA(o, atomHint ? atomHint : atomFind(key))) return true;
    if (o->kind == ObjKind::Array) { size_t i; if (key == "length" || (isIndex(key, i) && i < o->elems.size())) return true; }
    if ((o->kind == ObjKind::Clip || o->kind == ObjKind::TextField) && o->display) {
        Value tmp;
        extern bool displayGetProp(VM&, gfx::DisplayObject*, const std::string&, Value&);
        if (displayGetProp(*this, o->display, key, tmp)) return true;
    }
    return false;
}

bool displayGetProp(VM& vm, gfx::DisplayObject* d, const std::string& key, Value& out);
bool displaySetProp(VM& vm, gfx::DisplayObject* d, const std::string& key, const Value& v);

void VM::zombieUse(Object* o, const char* op, const std::string& key) {
    std::string props;
    for (size_t i = 0; i < o->props.size() && i < 8; ++i) props += o->props[i].first + ",";
    LOG_WARN("AVM1 GCCHECK: %s '%s' on a collected object (kind %d class '%s' native '%s' display %p props %s)", op, key.c_str(),
             (int)o->kind, o->className.c_str(), o->nativeType.c_str(), (void*)o->display, props.c_str());
}

Value VM::get(Object* o, const std::string& key, uint32_t atomHint) {
    if (!o) return Value::undef();
    if (o->zombie) zombieUse(o, "get", key);
    if (key == "__proto__") return o->proto ? Value(o->proto) : Value::undef();
    if (o->kind == ObjKind::Array) {
        if (key == "length") return Value((double)o->elems.size());
        size_t i;
        if (isIndex(key, i)) return i < o->elems.size() ? o->elems[i] : Value::undef();
    }
    if (o->kind == ObjKind::Boxed && o->boxed.t == VType::String && key == "length") {
        extern size_t utf8Length(const std::string&);
        return Value((double)utf8Length(o->boxed.s));
    }
    if (o->kind == ObjKind::Super) {
        Object* owner = findOwner(o->superProto, key);
        if (!owner) return Value::undef();
        Property* p = owner->findOwn(key);
        if (p->getter) { Args a; return call(Value(p->getter), Value(o->superThis), a); }
        return p->v;
    }
    // Own property (with getter).
    const uint32_t atom = atomHint ? atomHint : atomFind(key);
    if (Property* p = o->findOwnA(atom)) {
        if (p->getter) { Args a; return call(Value(p->getter), Value(o), a); }
        return p->v;
    }
    // Display object: children by instance name, then display properties.
    if ((o->kind == ObjKind::Clip || o->kind == ObjKind::TextField) && o->display) {
        Value out;
        if (displayGetProp(*this, o->display, key, out)) return out;
    }
    int guard = 0;
    for (Object* p = o->proto; p && guard < 256; p = p->proto, ++guard) {
        if (Property* pr = p->findOwnA(atom)) {
            if (pr->getter) { Args a; return call(Value(pr->getter), Value(o), a); }
            return pr->v;
        }
    }
    // __resolve
    if (key != "__resolve") {
        Value r = Value::undef();
        static const uint32_t kResolve = atomIntern("__resolve");
        Object* owner = nullptr;
        { int g = 0; for (Object* q = o; q && g < 256; q = q->proto, ++g) if (q->findOwnA(kResolve)) { owner = q; break; } }
        if (owner) {
            Value f = owner->findOwnA(kResolve)->v;
            if (f.isObject() && f.o->kind == ObjKind::Function) { Args a{Value(key)}; r = call(f, Value(o), a); }
        }
        return r;
    }
    return Value::undef();
}

Value VM::getV(const Value& base, const std::string& key, uint32_t atomHint) {
    if (base.isNullish()) return Value::undef();
    if (base.t == VType::String) {
        if (key == "length") { extern size_t utf8Length(const std::string&); return Value((double)utf8Length(base.s)); }
        return get(stringProto, key);
    }
    if (base.t == VType::Number) return get(numberProto, key);
    if (base.t == VType::Bool) return get(booleanProto, key);
    return get(base.o, key, atomHint);
}

void VM::set(Object* o, const std::string& key, const Value& vIn, uint32_t atomHint) {
    if (!o) return;
    if (o->zombie) zombieUse(o, "set", key);
    Value v = vIn;
    // DEV TOOL: WFC_AVMDUMPFN=<name>: a script function assigned to _global.<name> has its bytecode and constant pool
    // written to avmfn_<name>.bin / .pool.txt (native ports are checked against the exact operations).
    if (o == global && v.isObject() && v.o->kind == ObjKind::Function && v.o->script) {
        static const char* want = std::getenv("WFC_AVMDUMPFN");
        if (want && (std::string(",") + want + ",").find("," + key + ",") != std::string::npos) {
            const ScriptCode& sc = *v.o->script;
            if (FILE* fb = std::fopen(("avmfn_" + key + ".bin").c_str(), "wb")) {
                std::fwrite(sc.code->data() + sc.start, 1, sc.length, fb);
                std::fclose(fb);
            }
            if (FILE* fp = std::fopen(("avmfn_" + key + ".pool.txt").c_str(), "w")) {
                std::fprintf(fp, "v2=%d regs=%d flags=%04x params=", sc.v2 ? 1 : 0, (int)sc.regCount, (unsigned)sc.flags);
                for (const auto& pr : sc.params) std::fprintf(fp, "%d:%s ", (int)pr.first, pr.second.c_str());
                std::fputc('\n', fp);
                if (v.o->pool) for (size_t i = 0; i < v.o->pool->size(); ++i) std::fprintf(fp, "%zu %s\n", i, (*v.o->pool)[i].c_str());
                std::fclose(fp);
            }
        }
    }
    if (key == "__proto__") { o->proto = v.isObject() ? v.o : nullptr; return; }
    if (v.isObject() && v.o->kind == ObjKind::Function && v.o->script) nativeLibraryOverride(*this, key, v);   // by name + bytecode hash
    if (!o->watches.empty()) {
        auto w = o->watches.find(key);
        if (w != o->watches.end()) {
            Value old = get(o, key);
            Args a{Value(key), old, v, w->second.second};
            v = call(Value(w->second.first), Value(o), a);
        }
    }
    if (o->kind == ObjKind::Array) {
        if (key == "length") {
            double n = toNumber(v);
            if (n >= 0 && n < 1e7) o->elems.resize((size_t)n);
            return;
        }
        size_t i;
        if (isIndex(key, i)) {
            if (i >= o->elems.size()) o->elems.resize(i + 1);
            o->elems[i] = v;
            return;
        }
    }
    if (o->kind == ObjKind::Super) { set(o->superThis, key, v); return; }
    const uint32_t atom = atomHint ? atomHint : atomFind(key);
    if (Property* p = o->findOwnA(atom)) {
        if (p->setter) { Args a{v}; call(Value(p->setter), Value(o), a); return; }
        if (p->getter) return;   // getter without setter: read only
        if (p->flags & ReadOnly) return;
        p->v = v;
        return;
    }
    if ((o->kind == ObjKind::Clip || o->kind == ObjKind::TextField) && o->display)
        if (displaySetProp(*this, o->display, key, v)) return;
    // Inherited setter (addProperty on a prototype).
    int guard = 0;
    for (Object* p = o->proto; p && guard < 256; p = p->proto, ++guard) {
        if (Property* pr = p->findOwnA(atom)) {
            if (pr->setter) { Args a{v}; call(Value(pr->setter), Value(o), a); return; }
            if (pr->getter) return;
            break;
        }
    }
    o->own(key).v = v;
}

void VM::setV(const Value& base, const std::string& key, const Value& v) {
    if (base.isObject()) set(base.o, key, v);
}

bool VM::deleteProp(Object* o, const std::string& key) {
    if (!o) return false;
    if (Property* p = o->findOwn(key)) {
        if (p->flags & DontDelete) return false;
        return o->removeOwn(key);
    }
    return false;
}

std::vector<std::string> VM::enumerate(Object* o) {
    std::vector<std::string> keys;
    std::set<std::string> seen;
    int guard = 0;
    for (Object* p = o; p && guard < 256; p = p->proto, ++guard) {
        if (p->kind == ObjKind::Array)
            for (size_t i = 0; i < p->elems.size(); ++i) {
                std::string k = std::to_string(i);
                if (seen.insert(k).second) keys.push_back(k);
            }
        for (const auto& [k, pr] : p->props) {
            if (!seen.insert(k).second) continue;
            if (!(pr.flags & DontEnum)) keys.push_back(k);
        }
    }
    // Clips also enumerate their named children (for-in over a MovieClip lists instances).
    if ((o->kind == ObjKind::Clip) && o->display && o->display->kind == gfx::DisplayObject::Kind::Clip) {
        auto* mc = static_cast<gfx::MovieClip*>(o->display);
        for (auto& [d, ch] : mc->children)
            if (!ch->name.empty() && seen.insert(ch->name).second) keys.push_back(ch->name);
    }
    // Flash enumerates the most recently added properties first.
    std::reverse(keys.begin(), keys.end());
    return keys;
}

// ---------------------------------------------------------------------------------------------------------------
// Calls

Value VM::callMethod(const Value& base, const std::string& name, Args args, uint32_t atomHint) {
    const uint32_t a = atomHint ? atomHint : atomFind(name);
    Value f = getV(base, name, a);
    if (!f.isObject() || f.o->kind != ObjKind::Function) return Value::undef();
    Object* owner = base.isObject() ? findOwnerA(base.o, a) : nullptr;
    return call(f, base, args, owner ? owner->proto : nullptr);
}

Value VM::construct(Object* ctor, Args& args) {
    if (!ctor || ctor->kind != ObjKind::Function) return Value::undef();
    static const uint32_t aProto = atomIntern("prototype"), aCtorU = atomIntern("__constructor__"), aCtor = atomIntern("constructor");
    static const std::string kCtorU = "__constructor__", kCtor = "constructor";
    Value protoV = get(ctor, "prototype", aProto);
    Object* obj = newObject(protoV.isObject() ? protoV.o : objectProto);
    obj->setRawA(aCtorU, kCtorU, Value(ctor), DontEnum);
    obj->setRawA(aCtor, kCtor, Value(ctor), DontEnum);
    Object* superProto = protoV.isObject() ? protoV.o->proto : nullptr;
    Value r = call(Value(ctor), Value(obj), args, superProto);
    if (ctor->native && r.isObject()) return r;   // native constructors build their own object
    if (ctor->script && r.isObject() && r.o != obj) return r;
    return Value(obj);
}

Value VM::invokePath(const std::string& path, Args args, gfx::DisplayObject* root) {
    // "_global.SetLevelText" / "_root.menu.fn" / "a.b.c" (relative to _root).
    std::vector<std::string> parts;
    size_t s = 0;
    for (size_t i = 0; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '.') { parts.push_back(path.substr(s, i - s)); s = i + 1; }
    if (parts.empty()) return Value::undef();
    Value base;
    size_t i = 0;
    if (parts[0] == "_global") { base = Value(global); i = 1; }
    else if (parts[0] == "_root" || parts[0] == "_level0") { base = Value(player_->scriptObject(root)); i = 1; }
    else base = Value(player_->scriptObject(root));
    for (; i + 1 < parts.size(); ++i) base = getV(base, parts[i]);
    if (base.isNullish() && parts.size() == 1) base = Value(global);
    Value f = getV(base, parts.back());
    if ((!f.isObject() || f.o->kind != ObjKind::Function) && parts.size() == 1) {
        f = get(global, parts.back());
        base = Value(global);
    }
    if (!f.isObject() || f.o->kind != ObjKind::Function) return Value::undef();
    try {
        return call(f, base, args);
    } catch (const ScriptThrow& t) {
        LOG_WARN("AVM1 uncaught exception in %s: %s", path.c_str(), toString(t.v).c_str());
        return Value::undef();
    }
}

} // namespace gfx::avm1
