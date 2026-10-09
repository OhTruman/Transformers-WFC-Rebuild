// AVM1 built-in classes and global functions used by the WFC movies (SWF 8 player API subset).
#include "ui/gfx/Avm1.h"
#include "core/FrameProfile.h"
#include "ui/gfx/Display.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <random>

namespace gfx::avm1 {

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

Value arg(const Args& a, size_t i) { return i < a.size() ? a[i] : Value::undef(); }

Object* method(VM& vm, Object* o, const std::string& name, NativeFn fn) {
    Object* f = vm.newFunction(std::move(fn), name);
    o->setRaw(name, Value(f), DontEnum);
    return f;
}

Object* ctorWithProto(VM& vm, Object* target, const std::string& name, Object* proto, NativeFn fn) {
    Object* c = vm.newFunction(std::move(fn), name);
    c->setRaw("prototype", Value(proto), DontEnum);
    proto->setRaw("constructor", Value(c), DontEnum);
    if (target) target->setRaw(name, Value(c), DontEnum);
    return c;
}

void getterSetter(VM& vm, Object* o, const std::string& name, NativeFn get, NativeFn set) {
    Property& p = o->own(name);
    p.getter = vm.newFunction(std::move(get), "get " + name);
    p.setter = set ? vm.newFunction(std::move(set), "set " + name) : nullptr;
    p.flags = DontEnum;
}

std::u16string u16(const std::string& s) {
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

std::string u8(const std::u16string& s) {
    std::string o;
    for (char16_t ch : s) {
        uint32_t cp = ch;
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    return o;
}

std::string thisString(VM& vm, const Value& self) {
    if (self.isString()) return self.s;
    if (self.isObject() && self.o->kind == ObjKind::Boxed) return vm.toString(self.o->boxed);
    return vm.toString(self);
}

double clampIndex(double i, double len) {
    if (std::isnan(i)) return 0;
    if (i < 0) return std::max(0.0, len + i);
    return std::min(i, len);
}

std::mt19937& rng() { static std::mt19937 r((unsigned)std::chrono::steady_clock::now().time_since_epoch().count()); return r; }

} // namespace

void VM::installBuiltins() {
    objectProto = newObject(nullptr);
    functionProto = newObject(objectProto);
    functionProto->kind = ObjKind::Function;
    functionProto->native = [](VM&, const Value&, Args&) { return Value::undef(); };
    global = newObject(objectProto);
    VM& vm = *this;

    // ---------------- Object ----------------
    objectCtor = ctorWithProto(vm, global, "Object", objectProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (!a.empty() && a[0].isObject()) return a[0];
        if (!a.empty() && !a[0].isNullish()) return Value(vm.toObject(a[0]));
        return Value(vm.newPlain());
    });
    method(vm, objectProto, "hasOwnProperty", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value(false);
        std::string k = vm.toString(arg(a, 0));
        if (self.o->findOwn(k)) return Value(true);
        if (self.o->kind == ObjKind::Array) { if (k == "length") return Value(true); }
        return Value(false);
    });
    method(vm, objectProto, "toString", [](VM&, const Value& self, Args&) -> Value {
        if (self.isObject() && self.o->kind == ObjKind::Function) return Value("[type Function]");
        return Value("[object Object]");
    });
    method(vm, objectProto, "toLocaleString", [](VM&, const Value&, Args&) -> Value { return Value("[object Object]"); });
    method(vm, objectProto, "valueOf", [](VM&, const Value& self, Args&) -> Value { return self; });
    method(vm, objectProto, "addProperty", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value(false);
        std::string name = vm.toString(arg(a, 0));
        Value g = arg(a, 1), s = arg(a, 2);
        if (name.empty() || !g.isObject() || g.o->kind != ObjKind::Function) return Value(false);
        Property& p = self.o->own(name);
        p.getter = g.o;
        p.setter = s.isObject() && s.o->kind == ObjKind::Function ? s.o : nullptr;
        return Value(true);
    });
    method(vm, objectProto, "watch", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value(false);
        Value f = arg(a, 1);
        if (!f.isObject() || f.o->kind != ObjKind::Function) return Value(false);
        self.o->watches[vm.toString(arg(a, 0))] = {f.o, arg(a, 2)};
        return Value(true);
    });
    method(vm, objectProto, "unwatch", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value(false);
        return Value(self.o->watches.erase(vm.toString(arg(a, 0))) > 0);
    });
    method(vm, objectProto, "isPropertyEnumerable", [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value(false);
        const Property* p = self.o->findOwn(vm.toString(arg(a, 0)));
        return Value(p && !(p->flags & DontEnum));
    });
    method(vm, objectProto, "isPrototypeOf", [](VM&, const Value& self, Args& a) -> Value {
        Value v = arg(a, 0);
        if (!self.isObject() || !v.isObject()) return Value(false);
        for (Object* p = v.o->proto; p; p = p->proto) if (p == self.o) return Value(true);
        return Value(false);
    });
    method(vm, objectCtor, "registerClass", [](VM& vm, const Value&, Args& a) -> Value {
        std::string linkage = vm.toString(arg(a, 0));
        Value c = arg(a, 1);
        if (c.isObject() && c.o->kind == ObjKind::Function) vm.registeredClasses[linkage] = c.o;
        else vm.registeredClasses.erase(linkage);
        return Value(true);
    });

    // ---------------- Function ----------------
    Object* functionCtor = ctorWithProto(vm, global, "Function", functionProto, [](VM&, const Value&, Args&) { return Value::undef(); });
    (void)functionCtor;
    method(vm, functionProto, "call", [](VM& vm, const Value& self, Args& a) -> Value {
        Args rest(a.size() > 1 ? a.begin() + 1 : a.end(), a.end());
        return vm.call(self, arg(a, 0), rest);
    });
    method(vm, functionProto, "apply", [](VM& vm, const Value& self, Args& a) -> Value {
        Args rest;
        Value arr = arg(a, 1);
        if (arr.isObject() && arr.o->kind == ObjKind::Array) rest = arr.o->elems;
        return vm.call(self, arg(a, 0), rest);
    });

    // ---------------- Array ----------------
    arrayProto = newObject(objectProto);
    arrayCtor = ctorWithProto(vm, global, "Array", arrayProto, [](VM& vm, const Value&, Args& a) -> Value {
        if (a.size() == 1 && a[0].isNumber()) {
            Object* o = vm.newArray();
            double n = a[0].n;
            if (n >= 0 && n < 1e7) o->elems.resize((size_t)n);
            return Value(o);
        }
        return Value(vm.newArray(a));
    });
    arrayCtor->setRaw("CASEINSENSITIVE", Value(1), DontEnum | ReadOnly);
    arrayCtor->setRaw("DESCENDING", Value(2), DontEnum | ReadOnly);
    arrayCtor->setRaw("UNIQUESORT", Value(4), DontEnum | ReadOnly);
    arrayCtor->setRaw("RETURNINDEXEDARRAY", Value(8), DontEnum | ReadOnly);
    arrayCtor->setRaw("NUMERIC", Value(16), DontEnum | ReadOnly);
    auto arr = [](const Value& self) -> Object* { return self.isObject() && self.o->kind == ObjKind::Array ? self.o : nullptr; };
    method(vm, arrayProto, "push", [arr](VM&, const Value& self, Args& a) -> Value {
        Object* o = arr(self); if (!o) return Value::undef();
        for (auto& v : a) o->elems.push_back(v);
        return Value((double)o->elems.size());
    });
    method(vm, arrayProto, "pop", [arr](VM&, const Value& self, Args&) -> Value {
        Object* o = arr(self); if (!o || o->elems.empty()) return Value::undef();
        Value v = o->elems.back(); o->elems.pop_back(); return v;
    });
    method(vm, arrayProto, "shift", [arr](VM&, const Value& self, Args&) -> Value {
        Object* o = arr(self); if (!o || o->elems.empty()) return Value::undef();
        Value v = o->elems.front(); o->elems.erase(o->elems.begin()); return v;
    });
    method(vm, arrayProto, "unshift", [arr](VM&, const Value& self, Args& a) -> Value {
        Object* o = arr(self); if (!o) return Value::undef();
        o->elems.insert(o->elems.begin(), a.begin(), a.end());
        return Value((double)o->elems.size());
    });
    method(vm, arrayProto, "splice", [arr](VM& vm, const Value& self, Args& a) -> Value {
        Object* o = arr(self); if (!o) return Value::undef();
        double len = (double)o->elems.size();
        size_t start = (size_t)clampIndex(vm.toNumber(arg(a, 0)), len);
        size_t del = a.size() < 2 ? o->elems.size() - start
                                  : (size_t)std::max(0.0, std::min((double)(o->elems.size() - start), std::trunc(vm.toNumber(a[1]))));
        std::vector<Value> removed(o->elems.begin() + (long)start, o->elems.begin() + (long)(start + del));
        o->elems.erase(o->elems.begin() + (long)start, o->elems.begin() + (long)(start + del));
        if (a.size() > 2) o->elems.insert(o->elems.begin() + (long)start, a.begin() + 2, a.end());
        return Value(vm.newArray(removed));
    });
    method(vm, arrayProto, "slice", [arr](VM& vm, const Value& self, Args& a) -> Value {
        Object* o = arr(self); if (!o) return Value::undef();
        double len = (double)o->elems.size();
        size_t s = (size_t)clampIndex(a.empty() ? 0 : vm.toNumber(a[0]), len);
        size_t e = (size_t)clampIndex(a.size() < 2 || a[1].isUndef() ? len : vm.toNumber(a[1]), len);
        std::vector<Value> out;
        for (size_t i = s; i < e; ++i) out.push_back(o->elems[i]);
        return Value(vm.newArray(out));
    });
    method(vm, arrayProto, "concat", [arr](VM& vm, const Value& self, Args& a) -> Value {
        Object* o = arr(self);
        std::vector<Value> out = o ? o->elems : std::vector<Value>{};
        for (auto& v : a) {
            if (v.isObject() && v.o->kind == ObjKind::Array) out.insert(out.end(), v.o->elems.begin(), v.o->elems.end());
            else out.push_back(v);
        }
        return Value(vm.newArray(out));
    });
    method(vm, arrayProto, "join", [arr](VM& vm, const Value& self, Args& a) -> Value {
        Object* o = arr(self); if (!o) return Value("");
        std::string sep = a.empty() || a[0].isUndef() ? "," : vm.toString(a[0]);
        std::string s;
        for (size_t i = 0; i < o->elems.size(); ++i) { if (i) s += sep; s += vm.toString(o->elems[i]); }
        return Value(s);
    });
    method(vm, arrayProto, "toString", [arr](VM& vm, const Value& self, Args&) -> Value {
        Object* o = arr(self); if (!o) return Value("");
        std::string s;
        for (size_t i = 0; i < o->elems.size(); ++i) { if (i) s += ","; s += vm.toString(o->elems[i]); }
        return Value(s);
    });
    method(vm, arrayProto, "reverse", [arr](VM&, const Value& self, Args&) -> Value {
        Object* o = arr(self); if (o) std::reverse(o->elems.begin(), o->elems.end());
        return self;
    });
    auto sortImpl = [arr](VM& vm, const Value& self, Args& a, const std::vector<std::string>& fields) -> Value {
        Object* o = arr(self); if (!o) return Value::undef();
        Value cmp;
        int opts = 0;
        if (fields.empty() && !a.empty() && a[0].isObject() && a[0].o->kind == ObjKind::Function) { cmp = a[0]; opts = (int)vm.toNumber(arg(a, 1)); }
        else if (fields.empty() && !a.empty()) opts = (int)vm.toNumber(a[0]);
        // sortOn(fieldNames, options): options is a number for every field or (Flash 8 / GFx) an array with one set
        // per field - NUMERIC / DESCENDING / CASEINSENSITIVE per field, UNIQUESORT / RETURNINDEXEDARRAY from the first.
        // (The array form was read as NaN -> 0, so PlayerList's [0, NUMERIC|DESCENDING, ...] sorted everything as
        // ascending strings: an FFA list at 64 showed scores out of order.)
        std::vector<int> fieldOpts;
        auto num = [&](const Value& v) { double d = vm.toNumber(v); return std::isnan(d) ? 0 : (int)d; };
        if (!fields.empty() && a.size() > 1 && a[1].isObject() && a[1].o->kind == ObjKind::Array) {
            for (size_t i = 0; i < fields.size(); ++i) fieldOpts.push_back(i < a[1].o->elems.size() ? num(a[1].o->elems[i]) : 0);
            opts = fieldOpts.empty() ? 0 : (fieldOpts[0] & (4 | 8));
        }
        else if (!fields.empty() && a.size() > 1) opts = num(a[1]);
        std::vector<size_t> idx(o->elems.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        auto key = [&](const Value& v, const std::string& f) { return f.empty() ? v : vm.getV(v, f); };
        auto compare = [&](const Value& x, const Value& y) -> int {
            if (cmp.isObject()) { Args ca{x, y}; return (int)vm.toNumber(vm.call(cmp, Value::undef(), ca)); }
            const std::vector<std::string> fs = fields.empty() ? std::vector<std::string>{""} : fields;
            for (size_t k = 0; k < fs.size(); ++k) {
                const std::string& f = fs[k];
                const int fo = fieldOpts.empty() ? opts : fieldOpts[k];
                Value kx = key(x, f), ky = key(y, f);
                int r = 0;
                if (fo & 16) { double nx = vm.toNumber(kx), ny = vm.toNumber(ky); r = nx < ny ? -1 : nx > ny ? 1 : 0; }
                else {
                    std::string sx = vm.toString(kx), sy = vm.toString(ky);
                    if (fo & 1) { for (auto& ch : sx) ch = (char)std::tolower((unsigned char)ch); for (auto& ch : sy) ch = (char)std::tolower((unsigned char)ch); }
                    r = sx < sy ? -1 : sx > sy ? 1 : 0;
                }
                if (!fieldOpts.empty() && (fo & 2)) r = -r;   // per-field DESCENDING
                if (r) return r;
            }
            return 0;
        };
        std::stable_sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
            int r = compare(o->elems[i], o->elems[j]);
            return (opts & 2) ? r > 0 : r < 0;
        });
        if (opts & 8) {
            std::vector<Value> out;
            for (size_t i : idx) out.push_back(Value((double)i));
            return Value(vm.newArray(out));
        }
        std::vector<Value> sorted;
        for (size_t i : idx) sorted.push_back(o->elems[i]);
        o->elems = sorted;
        return self;
    };
    method(vm, arrayProto, "sort", [sortImpl](VM& vm, const Value& self, Args& a) { return sortImpl(vm, self, a, {}); });
    method(vm, arrayProto, "sortOn", [sortImpl](VM& vm, const Value& self, Args& a) {
        core::prof::Scope prof("avm.sortOn");
        std::vector<std::string> f;
        Value fv = arg(a, 0);
        if (fv.isObject() && fv.o->kind == ObjKind::Array) for (auto& e : fv.o->elems) f.push_back(vm.toString(e));
        else f.push_back(vm.toString(fv));
        return sortImpl(vm, self, a, f);
    });

    // ---------------- String ----------------
    stringProto = newObject(objectProto);
    Object* stringCtor = ctorWithProto(vm, global, "String", stringProto, [](VM& vm, const Value& self, Args& a) -> Value {
        std::string s = a.empty() ? "" : vm.toString(a[0]);
        if (self.isObject() && self.o->proto == vm.stringProto && self.o->kind == ObjKind::Plain) {
            self.o->kind = ObjKind::Boxed; self.o->boxed = Value(s); self.o->nativeType = "String";
            return self;
        }
        return Value(s);
    });
    method(vm, stringCtor, "fromCharCode", [](VM& vm, const Value&, Args& a) -> Value {
        std::u16string s;
        for (auto& v : a) s.push_back((char16_t)vm.toNumber(v));
        return Value(u8(s));
    });
    method(vm, stringProto, "toString", [](VM& vm, const Value& self, Args&) -> Value { return Value(thisString(vm, self)); });
    method(vm, stringProto, "valueOf", [](VM& vm, const Value& self, Args&) -> Value { return Value(thisString(vm, self)); });
    method(vm, stringProto, "charAt", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self));
        double i = vm.toNumber(arg(a, 0));
        if (std::isnan(i)) i = 0;
        if (i < 0 || i >= (double)s.size()) return Value("");
        return Value(u8(s.substr((size_t)i, 1)));
    });
    method(vm, stringProto, "charCodeAt", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self));
        double i = vm.toNumber(arg(a, 0));
        if (std::isnan(i)) i = 0;
        if (i < 0 || i >= (double)s.size()) return Value(kNaN);
        return Value((double)s[(size_t)i]);
    });
    method(vm, stringProto, "indexOf", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self)), f = u16(vm.toString(arg(a, 0)));
        double st = a.size() > 1 ? vm.toNumber(a[1]) : 0;
        if (std::isnan(st) || st < 0) st = 0;
        size_t r = s.find(f, (size_t)st);
        return Value(r == std::u16string::npos ? -1.0 : (double)r);
    });
    method(vm, stringProto, "lastIndexOf", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self)), f = u16(vm.toString(arg(a, 0)));
        size_t st = a.size() > 1 && !std::isnan(vm.toNumber(a[1])) ? (size_t)std::max(0.0, vm.toNumber(a[1])) : std::u16string::npos;
        size_t r = s.rfind(f, st);
        return Value(r == std::u16string::npos ? -1.0 : (double)r);
    });
    method(vm, stringProto, "substr", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self));
        double len = (double)s.size();
        double st = clampIndex(vm.toNumber(arg(a, 0)), len);
        double n = a.size() < 2 || a[1].isUndef() ? len - st : vm.toNumber(a[1]);
        if (std::isnan(n) || n < 0) n = 0;
        n = std::min(n, len - st);
        return Value(u8(s.substr((size_t)st, (size_t)n)));
    });
    method(vm, stringProto, "substring", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self));
        double len = (double)s.size();
        double st = vm.toNumber(arg(a, 0)), en = a.size() < 2 || a[1].isUndef() ? len : vm.toNumber(a[1]);
        if (std::isnan(st) || st < 0) st = 0;
        if (std::isnan(en) || en < 0) en = 0;
        st = std::min(st, len); en = std::min(en, len);
        if (st > en) std::swap(st, en);
        return Value(u8(s.substr((size_t)st, (size_t)(en - st))));
    });
    method(vm, stringProto, "slice", [](VM& vm, const Value& self, Args& a) -> Value {
        std::u16string s = u16(thisString(vm, self));
        double len = (double)s.size();
        double st = clampIndex(vm.toNumber(arg(a, 0)), len);
        double en = clampIndex(a.size() < 2 || a[1].isUndef() ? len : vm.toNumber(a[1]), len);
        if (en <= st) return Value("");
        return Value(u8(s.substr((size_t)st, (size_t)(en - st))));
    });
    method(vm, stringProto, "split", [](VM& vm, const Value& self, Args& a) -> Value {
        std::string s = thisString(vm, self);
        std::vector<Value> out;
        if (a.empty() || a[0].isUndef()) { out.push_back(Value(s)); return Value(vm.newArray(out)); }
        std::string d = vm.toString(a[0]);
        size_t limit = a.size() > 1 && !a[1].isUndef() ? (size_t)std::max(0.0, vm.toNumber(a[1])) : (size_t)-1;
        if (d.empty()) {
            std::u16string w = u16(s);
            for (char16_t c : w) { if (out.size() >= limit) break; out.push_back(Value(u8(std::u16string(1, c)))); }
        } else {
            size_t p = 0;
            for (;;) {
                if (out.size() >= limit) break;
                size_t q = s.find(d, p);
                if (q == std::string::npos) { out.push_back(Value(s.substr(p))); break; }
                out.push_back(Value(s.substr(p, q - p)));
                p = q + d.size();
            }
        }
        return Value(vm.newArray(out));
    });
    method(vm, stringProto, "toUpperCase", [](VM& vm, const Value& self, Args&) -> Value {
        std::u16string s = u16(thisString(vm, self));
        for (auto& c : s) {
            if (c >= 'a' && c <= 'z') c = (char16_t)(c - 32);
            else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c = (char16_t)(c - 32);
        }
        return Value(u8(s));
    });
    method(vm, stringProto, "toLowerCase", [](VM& vm, const Value& self, Args&) -> Value {
        std::u16string s = u16(thisString(vm, self));
        for (auto& c : s) {
            if (c >= 'A' && c <= 'Z') c = (char16_t)(c + 32);
            else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c = (char16_t)(c + 32);
        }
        return Value(u8(s));
    });
    method(vm, stringProto, "concat", [](VM& vm, const Value& self, Args& a) -> Value {
        std::string s = thisString(vm, self);
        for (auto& v : a) s += vm.toString(v);
        return Value(s);
    });

    // ---------------- Number / Boolean ----------------
    numberProto = newObject(objectProto);
    Object* numberCtor = ctorWithProto(vm, global, "Number", numberProto, [](VM& vm, const Value& self, Args& a) -> Value {
        double n = a.empty() ? 0.0 : vm.toNumber(a[0]);
        if (self.isObject() && self.o->proto == vm.numberProto && self.o->kind == ObjKind::Plain) {
            self.o->kind = ObjKind::Boxed; self.o->boxed = Value(n); self.o->nativeType = "Number";
            return self;
        }
        return Value(n);
    });
    numberCtor->setRaw("MAX_VALUE", Value(std::numeric_limits<double>::max()), DontEnum | ReadOnly);
    numberCtor->setRaw("MIN_VALUE", Value(std::numeric_limits<double>::denorm_min()), DontEnum | ReadOnly);
    numberCtor->setRaw("NaN", Value(kNaN), DontEnum | ReadOnly);
    numberCtor->setRaw("POSITIVE_INFINITY", Value(INFINITY), DontEnum | ReadOnly);
    numberCtor->setRaw("NEGATIVE_INFINITY", Value(-INFINITY), DontEnum | ReadOnly);
    method(vm, numberProto, "toString", [](VM& vm, const Value& self, Args& a) -> Value {
        double n = self.isNumber() ? self.n : (self.isObject() && self.o->kind == ObjKind::Boxed ? vm.toNumber(self.o->boxed) : vm.toNumber(self));
        int radix = a.empty() || a[0].isUndef() ? 10 : (int)vm.toNumber(a[0]);
        return Value(VM::numberToString(n, radix));
    });
    method(vm, numberProto, "valueOf", [](VM& vm, const Value& self, Args&) -> Value {
        if (self.isNumber()) return self;
        if (self.isObject() && self.o->kind == ObjKind::Boxed) return Value(vm.toNumber(self.o->boxed));
        return Value(vm.toNumber(self));
    });
    booleanProto = newObject(objectProto);
    ctorWithProto(vm, global, "Boolean", booleanProto, [](VM& vm, const Value& self, Args& a) -> Value {
        bool b = !a.empty() && vm.toBool(a[0]);
        if (self.isObject() && self.o->proto == vm.booleanProto && self.o->kind == ObjKind::Plain) {
            self.o->kind = ObjKind::Boxed; self.o->boxed = Value(b); self.o->nativeType = "Boolean";
            return self;
        }
        return Value(b);
    });
    method(vm, booleanProto, "toString", [](VM& vm, const Value& self, Args&) -> Value {
        bool b = self.t == VType::Bool ? self.b : (self.isObject() && self.o->kind == ObjKind::Boxed ? vm.toBool(self.o->boxed) : vm.toBool(self));
        return Value(b ? "true" : "false");
    });
    method(vm, booleanProto, "valueOf", [](VM& vm, const Value& self, Args&) -> Value {
        if (self.t == VType::Bool) return self;
        return Value(self.isObject() && self.o->kind == ObjKind::Boxed ? vm.toBool(self.o->boxed) : vm.toBool(self));
    });

    // ---------------- Math ----------------
    Object* math = newPlain();
    global->setRaw("Math", Value(math), DontEnum);
    auto m1 = [&](const char* name, double (*f)(double)) {
        method(vm, math, name, [f](VM& vm, const Value&, Args& a) -> Value { return Value(f(vm.toNumber(arg(a, 0)))); });
    };
    m1("abs", [](double x) { return std::fabs(x); });
    m1("acos", [](double x) { return std::acos(x); });
    m1("asin", [](double x) { return std::asin(x); });
    m1("atan", [](double x) { return std::atan(x); });
    m1("ceil", [](double x) { return std::ceil(x); });
    m1("cos", [](double x) { return std::cos(x); });
    m1("exp", [](double x) { return std::exp(x); });
    m1("floor", [](double x) { return std::floor(x); });
    m1("log", [](double x) { return std::log(x); });
    m1("round", [](double x) { return std::floor(x + 0.5); });
    m1("sin", [](double x) { return std::sin(x); });
    m1("sqrt", [](double x) { return std::sqrt(x); });
    m1("tan", [](double x) { return std::tan(x); });
    method(vm, math, "atan2", [](VM& vm, const Value&, Args& a) -> Value { return Value(std::atan2(vm.toNumber(arg(a, 0)), vm.toNumber(arg(a, 1)))); });
    method(vm, math, "pow", [](VM& vm, const Value&, Args& a) -> Value { return Value(std::pow(vm.toNumber(arg(a, 0)), vm.toNumber(arg(a, 1)))); });
    method(vm, math, "max", [](VM& vm, const Value&, Args& a) -> Value {
        if (a.empty()) return Value(-INFINITY);
        double r = -INFINITY;
        for (auto& v : a) { double x = vm.toNumber(v); if (std::isnan(x)) return Value(kNaN); r = std::max(r, x); }
        return Value(r);
    });
    method(vm, math, "min", [](VM& vm, const Value&, Args& a) -> Value {
        if (a.empty()) return Value(INFINITY);
        double r = INFINITY;
        for (auto& v : a) { double x = vm.toNumber(v); if (std::isnan(x)) return Value(kNaN); r = std::min(r, x); }
        return Value(r);
    });
    method(vm, math, "random", [](VM&, const Value&, Args&) -> Value { return Value(std::uniform_real_distribution<double>(0, 1)(rng())); });
    math->setRaw("PI", Value(3.141592653589793), DontEnum | ReadOnly);
    math->setRaw("E", Value(2.718281828459045), DontEnum | ReadOnly);
    math->setRaw("LN2", Value(0.6931471805599453), DontEnum | ReadOnly);
    math->setRaw("LN10", Value(2.302585092994046), DontEnum | ReadOnly);
    math->setRaw("LOG2E", Value(1.4426950408889634), DontEnum | ReadOnly);
    math->setRaw("LOG10E", Value(0.4342944819032518), DontEnum | ReadOnly);
    math->setRaw("SQRT1_2", Value(0.7071067811865476), DontEnum | ReadOnly);
    math->setRaw("SQRT2", Value(1.4142135623730951), DontEnum | ReadOnly);

    // ---------------- Date ----------------
    dateProto = newObject(objectProto);
    ctorWithProto(vm, global, "Date", dateProto, [](VM& vm, const Value& self, Args& a) -> Value {
        double ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
        if (a.size() == 1) ms = vm.toNumber(a[0]);
        Object* o = self.isObject() ? self.o : vm.newObject(vm.dateProto);
        o->kind = ObjKind::Boxed; o->boxed = Value(ms); o->nativeType = "Date";
        return Value(o);
    });
    auto dateField = [&](const char* name, int which) {
        method(vm, dateProto, name, [which](VM&, const Value& self, Args&) -> Value {
            if (!self.isObject()) return Value(kNaN);
            double ms = self.o->boxed.n;
            if (which == 0) return Value(ms);
            std::time_t t = (std::time_t)(ms / 1000.0);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            switch (which) {
            case 1: return Value((double)tm.tm_year + 1900);
            case 2: return Value((double)tm.tm_mon);
            case 3: return Value((double)tm.tm_mday);
            case 4: return Value((double)tm.tm_wday);
            case 5: return Value((double)tm.tm_hour);
            case 6: return Value((double)tm.tm_min);
            case 7: return Value((double)tm.tm_sec);
            case 8: return Value(std::fmod(ms, 1000.0));
            }
            return Value(kNaN);
        });
    };
    dateField("getTime", 0); dateField("valueOf", 0); dateField("getFullYear", 1); dateField("getMonth", 2); dateField("getDate", 3);
    dateField("getDay", 4); dateField("getHours", 5); dateField("getMinutes", 6); dateField("getSeconds", 7); dateField("getMilliseconds", 8);

    // ---------------- Error ----------------
    errorProto = newObject(objectProto);
    errorProto->setRaw("name", Value("Error"), DontEnum);
    errorProto->setRaw("message", Value("Error"), DontEnum);
    ctorWithProto(vm, global, "Error", errorProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (self.isObject() && !a.empty()) vm.set(self.o, "message", a[0]);
        return self;
    });
    method(vm, errorProto, "toString", [](VM& vm, const Value& self, Args&) -> Value { return Value(vm.toString(vm.getV(self, "message"))); });

    // ---------------- global functions ----------------
    method(vm, global, "trace", [](VM& vm, const Value&, Args& a) -> Value {
        if (vm.traceSink) vm.traceSink(vm.toString(arg(a, 0)));
        return Value::undef();
    });
    method(vm, global, "parseInt", [](VM& vm, const Value&, Args& a) -> Value {
        std::string s = vm.toString(arg(a, 0));
        int radix = a.size() > 1 && !a[1].isUndef() ? (int)vm.toNumber(a[1]) : 0;
        size_t i = 0;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        bool neg = false;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; ++i; }
        if ((radix == 0 || radix == 16) && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { radix = 16; i += 2; }
        else if (radix == 0 && i + 1 < s.size() && s[i] == '0') radix = 8;   // AS2 legacy octal
        if (radix == 0) radix = 10;
        if (radix < 2 || radix > 36) return Value(kNaN);
        double v = 0;
        bool any = false;
        for (; i < s.size(); ++i) {
            int d;
            char c = s[i];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
            else break;
            if (d >= radix) break;
            v = v * radix + d;
            any = true;
        }
        if (!any) return Value(kNaN);
        return Value(neg ? -v : v);
    });
    method(vm, global, "parseFloat", [](VM& vm, const Value&, Args& a) -> Value {
        std::string s = vm.toString(arg(a, 0));
        char* end = nullptr;
        const char* p = s.c_str();
        while (*p && std::isspace((unsigned char)*p)) ++p;
        double d = std::strtod(p, &end);
        if (end == p) return Value(kNaN);
        return Value(d);
    });
    method(vm, global, "isNaN", [](VM& vm, const Value&, Args& a) -> Value { return Value(std::isnan(vm.toNumber(arg(a, 0)))); });
    method(vm, global, "isFinite", [](VM& vm, const Value&, Args& a) -> Value { return Value(std::isfinite(vm.toNumber(arg(a, 0)))); });
    method(vm, global, "escape", [](VM& vm, const Value&, Args& a) -> Value {
        std::string s = vm.toString(arg(a, 0)), o;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '@' || c == '-' || c == '_' || c == '.' || c == '*' || c == '+' || c == '/') o += (char)c;
            else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
        }
        return Value(o);
    });
    method(vm, global, "unescape", [](VM& vm, const Value&, Args& a) -> Value {
        std::string s = vm.toString(arg(a, 0)), o;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else o += s[i];
        }
        return Value(o);
    });
    method(vm, global, "getTimer", [](VM& vm, const Value&, Args&) -> Value { return Value(std::floor(vm.player()->timeMs())); });
    method(vm, global, "getVersion", [](VM& vm, const Value&, Args&) -> Value { return Value(vm.versionString); });
    method(vm, global, "updateAfterEvent", [](VM&, const Value&, Args&) -> Value { return Value::undef(); });
    method(vm, global, "ASSetPropFlags", [](VM& vm, const Value&, Args& a) -> Value {
        Value o = arg(a, 0);
        if (!o.isObject()) return Value::undef();
        Value names = arg(a, 1);
        int setF = (int)vm.toNumber(arg(a, 2)), clrF = a.size() > 3 ? (int)vm.toNumber(a[3]) : 0;
        if (std::isnan(vm.toNumber(arg(a, 2)))) setF = 0;
        auto apply = [&](Property& p) { p.flags = (uint8_t)((p.flags & ~clrF) | (setF & 7)); };
        if (names.isNullish()) { for (auto& [k, p] : o.o->props) apply(p); return Value::undef(); }
        std::vector<std::string> list;
        if (names.isObject() && names.o->kind == ObjKind::Array) for (auto& e : names.o->elems) list.push_back(vm.toString(e));
        else {
            std::string s = vm.toString(names);
            size_t p = 0;
            for (;;) { size_t q = s.find(',', p); list.push_back(s.substr(p, q == std::string::npos ? q : q - p)); if (q == std::string::npos) break; p = q + 1; }
        }
        for (auto& n : list) if (Property* p = o.o->findOwn(n)) apply(*p);
        return Value::undef();
    });
    auto intervalFn = [](bool once) {
        return [once](VM& vm, const Value&, Args& a) -> Value {
            if (a.empty()) return Value::undef();
            if (a[0].isObject() && a[0].o->kind == ObjKind::Function) {
                double ms = vm.toNumber(arg(a, 1));
                Args rest(a.size() > 2 ? a.begin() + 2 : a.end(), a.end());
                return Value((double)vm.player()->addInterval(a[0], "", ms, rest, once));
            }
            double ms = vm.toNumber(arg(a, 2));
            Args rest(a.size() > 3 ? a.begin() + 3 : a.end(), a.end());
            return Value((double)vm.player()->addInterval(a[0], vm.toString(arg(a, 1)), ms, rest, once));
        };
    };
    method(vm, global, "setInterval", intervalFn(false));
    method(vm, global, "setTimeout", intervalFn(true));
    auto clearFn = [](VM& vm, const Value&, Args& a) -> Value { vm.player()->clearInterval((int)vm.toNumber(arg(a, 0))); return Value::undef(); };
    method(vm, global, "clearInterval", clearFn);
    method(vm, global, "clearTimeout", clearFn);
    global->setRaw("$version", Value(versionString), DontEnum);
    global->setRaw("NaN", Value(kNaN), DontEnum);
    global->setRaw("Infinity", Value(INFINITY), DontEnum);

    // ---------------- Key ----------------
    keyObj = newPlain();
    global->setRaw("Key", Value(keyObj), DontEnum);
    const std::pair<const char*, int> keys[] = {{"BACKSPACE", 8}, {"TAB", 9}, {"ENTER", 13}, {"SHIFT", 16}, {"CONTROL", 17},
        {"CAPSLOCK", 20}, {"ESCAPE", 27}, {"SPACE", 32}, {"PGUP", 33}, {"PGDN", 34}, {"END", 35}, {"HOME", 36}, {"LEFT", 37},
        {"UP", 38}, {"RIGHT", 39}, {"DOWN", 40}, {"INSERT", 45}, {"DELETEKEY", 46}};
    for (auto& [n, v] : keys) keyObj->setRaw(n, Value(v), DontEnum | ReadOnly);
    method(vm, keyObj, "getCode", [](VM& vm, const Value&, Args&) -> Value { return Value((double)vm.player()->lastKeyCode); });
    method(vm, keyObj, "getAscii", [](VM& vm, const Value&, Args&) -> Value { return Value((double)vm.player()->lastAscii); });
    method(vm, keyObj, "isDown", [](VM& vm, const Value&, Args& a) -> Value { return Value(vm.player()->keysDown.count((int)vm.toNumber(arg(a, 0))) > 0); });
    method(vm, keyObj, "isToggled", [](VM&, const Value&, Args&) -> Value { return Value(false); });
    auto addL = [](std::vector<Object*> Player::*list) {
        return [list](VM& vm, const Value&, Args& a) -> Value {
            Value l = arg(a, 0);
            if (!l.isObject()) return Value(false);
            auto& v = vm.player()->*list;
            if (std::find(v.begin(), v.end(), l.o) == v.end()) v.push_back(l.o);
            return Value(true);
        };
    };
    auto remL = [](std::vector<Object*> Player::*list) {
        return [list](VM& vm, const Value&, Args& a) -> Value {
            Value l = arg(a, 0);
            auto& v = vm.player()->*list;
            auto it = std::find(v.begin(), v.end(), l.o);
            if (it == v.end()) return Value(false);
            v.erase(it);
            return Value(true);
        };
    };
    method(vm, keyObj, "addListener", addL(&Player::keyListeners));
    method(vm, keyObj, "removeListener", remL(&Player::keyListeners));

    // ---------------- Stage ----------------
    stageObj = newPlain();
    global->setRaw("Stage", Value(stageObj), DontEnum);
    getterSetter(vm, stageObj, "width", [](VM& vm, const Value&, Args&) -> Value { return Value((double)vm.player()->stageViewW()); }, nullptr);
    getterSetter(vm, stageObj, "height", [](VM& vm, const Value&, Args&) -> Value { return Value((double)vm.player()->stageViewH()); }, nullptr);
    getterSetter(vm, stageObj, "scaleMode", [](VM& vm, const Value&, Args&) -> Value { return Value(vm.player()->scaleMode); },
                 [](VM& vm, const Value&, Args& a) -> Value { vm.player()->scaleMode = vm.toString(arg(a, 0)); return Value::undef(); });
    stageObj->setRaw("align", Value(""));
    stageObj->setRaw("showMenu", Value(false));
    method(vm, stageObj, "addListener", addL(&Player::stageListeners));
    method(vm, stageObj, "removeListener", remL(&Player::stageListeners));

    // ---------------- Mouse / Selection / System ----------------
    mouseObj = newPlain();
    global->setRaw("Mouse", Value(mouseObj), DontEnum);
    method(vm, mouseObj, "show", [](VM&, const Value&, Args&) -> Value { return Value(1); });
    method(vm, mouseObj, "hide", [](VM&, const Value&, Args&) -> Value { return Value(1); });
    method(vm, mouseObj, "addListener", addL(&Player::mouseListeners));
    method(vm, mouseObj, "removeListener", remL(&Player::mouseListeners));
    Object* selection = newPlain();
    global->setRaw("Selection", Value(selection), DontEnum);
    method(vm, selection, "getFocus", [](VM& vm, const Value&, Args&) -> Value {
        // Flash clears focus when the focused object is removed: a stale path would satisfy movie code that checks
        // Selection.getFocus() == targetPath(field) after the field is gone (TextPrompt_GFX's Enter listener outlives
        // the prompt and submitted the account name again from Extras).
        Value f = vm.get(vm.global, "__selectionFocus");
        if (f.isString() && vm.player()) {
            gfx::DisplayObject* d = vm.player()->resolveTarget(f.s, nullptr);
            if (!d || d->removed) { vm.set(vm.global, "__selectionFocus", Value::null()); return Value::null(); }
        }
        return f;
    });
    method(vm, selection, "setFocus", [](VM& vm, const Value&, Args& a) -> Value {
        // An instance or a target-path string (relative to the calling timeline, e.g. "inputText_mc.label_txt").
        Value v = arg(a, 0);
        gfx::DisplayObject* d = nullptr;
        if (v.isObject() && v.o->display) d = v.o->display;
        else if (v.isString() && vm.player()) d = vm.player()->resolveTarget(vm.toString(v), vm.currentTarget);
        if (d && d->kind == gfx::DisplayObject::Kind::Text && static_cast<gfx::TextField*>(d)->editable()) {
            vm.player()->setTextFocus(static_cast<gfx::TextField*>(d));
            return Value(true);
        }
        if (vm.player()) vm.player()->setTextFocus(nullptr);
        vm.global->setRaw("__selectionFocus", d ? Value(d->targetPath()) : Value::null(), DontEnum);
        return Value(d != nullptr);
    });
    method(vm, selection, "getCaretIndex", [](VM& vm, const Value&, Args&) -> Value {
        return vm.player() && vm.player()->textFocus() ? Value((double)vm.player()->caretIndex()) : Value(-1.0);
    });
    method(vm, selection, "setSelection", [](VM& vm, const Value&, Args& a) -> Value {
        if (vm.player()) vm.player()->setCaretIndex((size_t)std::max(0.0, vm.toNumber(arg(a, 1))));
        return Value::undef();
    });
    method(vm, selection, "getBeginIndex", [](VM& vm, const Value&, Args&) -> Value {
        return vm.player() && vm.player()->textFocus() ? Value((double)vm.player()->caretIndex()) : Value(-1.0);
    });
    method(vm, selection, "getEndIndex", [](VM& vm, const Value&, Args&) -> Value {
        return vm.player() && vm.player()->textFocus() ? Value((double)vm.player()->caretIndex()) : Value(-1.0);
    });
    method(vm, selection, "captureFocus", [](VM&, const Value&, Args&) -> Value { return Value::undef(); });
    Object* system = newPlain();
    global->setRaw("System", Value(system), DontEnum);
    Object* caps = newPlain();
    system->setRaw("capabilities", Value(caps));
    caps->setRaw("version", Value(versionString));
    caps->setRaw("os", Value("Xbox 360"));
    caps->setRaw("language", Value("en"));
    Object* security = newPlain();
    system->setRaw("security", Value(security));
    method(vm, security, "allowDomain", [](VM&, const Value&, Args&) -> Value { return Value::undef(); });
    system->setRaw("useCodepage", Value(false));

    // ---------------- flash.* packages ----------------
    Object* flash = newPlain();
    global->setRaw("flash", Value(flash), DontEnum);
    Object* geom = newPlain(); flash->setRaw("geom", Value(geom));
    Object* filters = newPlain(); flash->setRaw("filters", Value(filters));
    Object* external = newPlain(); flash->setRaw("external", Value(external));
    Object* display = newPlain(); flash->setRaw("display", Value(display));

    // ColorTransform(rm, gm, bm, am, ro, go, bo, ao)
    Object* ctProto = newPlain();
    Object* ctCtor = ctorWithProto(vm, geom, "ColorTransform", ctProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value::undef();
        const char* n[] = {"redMultiplier", "greenMultiplier", "blueMultiplier", "alphaMultiplier", "redOffset", "greenOffset",
                           "blueOffset", "alphaOffset"};
        for (int i = 0; i < 8; ++i) self.o->setRaw(n[i], Value(i < (int)a.size() ? vm.toNumber(a[(size_t)i]) : (i < 4 ? 1.0 : 0.0)));
        self.o->nativeType = "ColorTransform";
        return self;
    });
    (void)ctCtor;
    getterSetter(vm, ctProto, "rgb",
        [](VM& vm, const Value& self, Args&) -> Value {
            int r = (int)vm.toNumber(vm.getV(self, "redOffset")), g = (int)vm.toNumber(vm.getV(self, "greenOffset")),
                b = (int)vm.toNumber(vm.getV(self, "blueOffset"));
            return Value((double)(((r & 255) << 16) | ((g & 255) << 8) | (b & 255)));
        },
        [](VM& vm, const Value& self, Args& a) -> Value {
            int v = vm.toInt32(arg(a, 0));
            vm.setV(self, "redMultiplier", Value(0)); vm.setV(self, "greenMultiplier", Value(0)); vm.setV(self, "blueMultiplier", Value(0));
            vm.setV(self, "redOffset", Value((v >> 16) & 255)); vm.setV(self, "greenOffset", Value((v >> 8) & 255));
            vm.setV(self, "blueOffset", Value(v & 255));
            return Value::undef();
        });
    // Point / Rectangle / Matrix (data holders + the methods the movies use).
    Object* ptProto = newPlain();
    ctorWithProto(vm, geom, "Point", ptProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (self.isObject()) { self.o->setRaw("x", Value(vm.toNumber(a.empty() ? Value(0) : a[0]))); self.o->setRaw("y", Value(vm.toNumber(a.size() > 1 ? a[1] : Value(0)))); }
        return self;
    });
    Object* rcProto = newPlain();
    ctorWithProto(vm, geom, "Rectangle", rcProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (self.isObject()) {
            const char* n[] = {"x", "y", "width", "height"};
            for (int i = 0; i < 4; ++i) self.o->setRaw(n[i], Value(i < (int)a.size() ? vm.toNumber(a[(size_t)i]) : 0.0));
            self.o->nativeType = "Rectangle";
        }
        return self;
    });
    Object* mxProto = newPlain();
    ctorWithProto(vm, geom, "Matrix", mxProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (self.isObject()) {
            const char* n[] = {"a", "b", "c", "d", "tx", "ty"};
            double def[] = {1, 0, 0, 1, 0, 0};
            for (int i = 0; i < 6; ++i) self.o->setRaw(n[i], Value(i < (int)a.size() ? vm.toNumber(a[(size_t)i]) : def[i]));
            self.o->nativeType = "Matrix";
        }
        return self;
    });

    // Filters: accepted and stored (rendering of filters is PARTIAL).
    for (const char* f : {"DropShadowFilter", "GlowFilter", "BlurFilter", "BevelFilter", "GradientGlowFilter", "GradientBevelFilter",
                          "ColorMatrixFilter", "ConvolutionFilter", "DisplacementMapFilter"}) {
        Object* proto = newPlain();
        std::string name = f;
        ctorWithProto(vm, filters, f, proto, [name](VM& vm, const Value& self, Args& a) -> Value {
            if (self.isObject()) {
                self.o->nativeType = name;
                self.o->setRaw("__args", Value(vm.newArray(a)), DontEnum);
            }
            return self;
        });
        method(vm, proto, "clone", [](VM&, const Value& self, Args&) -> Value { return self; });
    }

    // ExternalInterface: the GFx bridge to the engine (UnrealScript binding classes).
    Object* ei = newPlain();
    external->setRaw("ExternalInterface", Value(ei));
    ei->setRaw("available", Value(true));
    method(vm, ei, "call", [](VM& vm, const Value&, Args& a) -> Value {
        std::string name = vm.toString(arg(a, 0));
        Args rest(a.size() > 1 ? a.begin() + 1 : a.end(), a.end());
        return vm.externalCall ? vm.externalCall(name, rest) : Value::undef();
    });
    method(vm, ei, "addCallback", [](VM& vm, const Value&, Args& a) -> Value {
        Object* cbs = vm.get(vm.global, "__externalCallbacks").isObject() ? vm.get(vm.global, "__externalCallbacks").o : nullptr;
        if (!cbs) { cbs = vm.newPlain(); vm.global->setRaw("__externalCallbacks", Value(cbs), DontEnum); }
        Object* rec = vm.newPlain();
        rec->setRaw("instance", arg(a, 1));
        rec->setRaw("method", arg(a, 2));
        cbs->setRaw(vm.toString(arg(a, 0)), Value(rec));
        return Value(true);
    });

    // BitmapData.loadBitmap(linkage): an exported bitmap of a loaded movie.
    Object* bdProto = newPlain();
    Object* bdCtor = ctorWithProto(vm, display, "BitmapData", bdProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (self.isObject()) { self.o->nativeType = "BitmapData"; self.o->setRaw("width", Value(vm.toNumber(arg(a, 0)))); self.o->setRaw("height", Value(vm.toNumber(arg(a, 1)))); }
        return self;
    });
    method(vm, bdCtor, "loadBitmap", [bdProto](VM& vm, const Value&, Args& a) -> Value {
        std::string linkage = vm.toString(arg(a, 0));
        std::shared_ptr<const MovieDef> def;
        uint16_t id = 0;
        Player* p = vm.player();
        if (!p->findExportedBitmap(linkage, def, id)) return Value::undef();
        const CharDef* cd = def->character(id);
        if (!cd || cd->type != CharType::Bitmap) return Value::undef();
        Object* o = vm.newObject(bdProto);
        o->nativeType = "BitmapData";
        o->payload = std::make_shared<std::pair<std::shared_ptr<const MovieDef>, uint16_t>>(def, id);
        const BitmapDef& b = def->bitmaps[(size_t)cd->index];
        std::string path = p->externalTexture ? p->externalTexture(b.exportName) : "";
        o->setRaw("__path", Value(path.empty() ? b.resolvedPath : path), DontEnum);
        o->setRaw("width", Value((double)b.targetWidth));
        o->setRaw("height", Value((double)b.targetHeight));
        return Value(o);
    });

    // MovieClipLoader
    Object* mclProto = newPlain();
    ctorWithProto(vm, global, "MovieClipLoader", mclProto, [](VM& vm, const Value& self, Args&) -> Value {
        if (self.isObject()) { self.o->nativeType = "MovieClipLoader"; self.o->setRaw("__listeners", Value(vm.newArray()), DontEnum); }
        return self;
    });
    method(vm, mclProto, "addListener", [](VM& vm, const Value& self, Args& a) -> Value {
        Value l = vm.getV(self, "__listeners");
        if (l.isObject() && arg(a, 0).isObject()) l.o->elems.push_back(a[0]);
        return Value(true);
    });
    method(vm, mclProto, "removeListener", [](VM& vm, const Value& self, Args& a) -> Value {
        Value l = vm.getV(self, "__listeners");
        if (l.isObject()) {
            auto& e = l.o->elems;
            e.erase(std::remove_if(e.begin(), e.end(), [&](const Value& v) { return v.o == arg(a, 0).o; }), e.end());
        }
        return Value(true);
    });
    method(vm, mclProto, "loadClip", [](VM& vm, const Value& self, Args& a) -> Value {
        std::string url = vm.toString(arg(a, 0));
        Value t = arg(a, 1);
        gfx::DisplayObject* d = t.isObject() ? t.o->display : vm.player()->resolveTarget(vm.toString(t), vm.player()->root());
        if (!d || d->kind != gfx::DisplayObject::Kind::Clip) return Value(false);
        vm.player()->loadMovieInto(static_cast<gfx::MovieClip*>(d), url, self.isObject() ? self.o : nullptr);
        return Value(true);
    });
    method(vm, mclProto, "unloadClip", [](VM& vm, const Value&, Args& a) -> Value {
        Value t = arg(a, 0);
        if (t.isObject() && t.o->display) vm.player()->removeObject(t.o->display);
        return Value(true);
    });
    method(vm, mclProto, "getProgress", [](VM& vm, const Value&, Args&) -> Value {
        Object* o = vm.newPlain();
        o->setRaw("bytesLoaded", Value(1)); o->setRaw("bytesTotal", Value(1));
        return Value(o);
    });

    // TextFormat
    Object* tfProto = newPlain();
    textFormatCtor = ctorWithProto(vm, global, "TextFormat", tfProto, [](VM& vm, const Value& self, Args& a) -> Value {
        if (!self.isObject()) return Value::undef();
        const char* n[] = {"font", "size", "color", "bold", "italic", "underline", "url", "target", "align", "leftMargin",
                           "rightMargin", "indent", "leading"};
        for (int i = 0; i < 13; ++i) self.o->setRaw(n[i], i < (int)a.size() ? a[(size_t)i] : Value::null());
        for (const char* x : {"blockIndent", "bullet", "tabStops", "letterSpacing", "kerning"}) self.o->setRaw(x, Value::null());
        self.o->nativeType = "TextFormat";
        return self;
    });

    // Sound (UI sound playback goes through the Sound.PlaySound bridge; this class only has to exist).
    Object* soundProto = newPlain();
    ctorWithProto(vm, global, "Sound", soundProto, [](VM&, const Value& self, Args&) -> Value { return self; });
    for (const char* n : {"attachSound", "start", "stop", "setVolume", "setPan", "loadSound"})
        method(vm, soundProto, n, [](VM&, const Value&, Args&) -> Value { return Value::undef(); });
    method(vm, soundProto, "getVolume", [](VM&, const Value&, Args&) -> Value { return Value(100); });

    // XML / LoadVars / SharedObject / LocalConnection: not used by the frontend path (constructible stubs).
    for (const char* n : {"XML", "LoadVars", "LocalConnection", "XMLNode", "ContextMenu", "ContextMenuItem", "PrintJob", "Camera",
                          "Microphone", "NetConnection", "NetStream", "Video", "Button"}) {
        Object* proto = newPlain();
        ctorWithProto(vm, global, n, proto, [](VM&, const Value& self, Args&) -> Value { return self; });
    }
    Object* so = newPlain();
    global->setRaw("SharedObject", Value(so), DontEnum);
    method(vm, so, "getLocal", [](VM& vm, const Value&, Args&) -> Value { Object* o = vm.newPlain(); o->setRaw("data", Value(vm.newPlain())); return Value(o); });
}

} // namespace gfx::avm1
