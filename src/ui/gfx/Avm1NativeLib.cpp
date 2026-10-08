// Clean-room reconstruction - native ports of hot shared ActionScript library functions.
//
// The shared component library (SharedComponents, carried by ~30 movies) defines a tween engine in ActionScript:
// _global.addInterp / updateInterpObjects / findInterpValue. The scoreboard's PlayerList tweens all 64 rows every second
// and the menus tween constantly, so the engine was a large share of UI script time. A function assigned to _global
// under one of these names is replaced by its native port ONLY when its canonical bytecode (operations, with constant
// pool references resolved to their strings, plus register count / flags / parameter count) hashes to the known
// shared-library version; any other body stays interpreted. The ports follow the bytecode operation by operation with
// the interpreter's own semantics (Add2 / Subtract / Multiply / Divide / Less2 / Equals2 / StrictEquals / ToInteger, the
// method calls), so results are identical. WFC_NONATIVEINTERP=1 keeps the scripts (A/B).
#include "ui/gfx/Avm1.h"
#include "ui/gfx/Display.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>

namespace gfx::avm1 {

// Canonical form: every action (op byte, and its payload with Push pool references replaced by the string), FNV-1a 64.
uint64_t canonicalFunctionHash(const ScriptCode& sc, const std::vector<std::string>* pool) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    auto mixStr = [&](const std::string& s) { for (unsigned char ch : s) mix(ch); mix(0); };
    mix((uint8_t)sc.regCount); mix((uint8_t)(sc.flags & 0xFF)); mix((uint8_t)(sc.flags >> 8)); mix((uint8_t)sc.params.size());
    const uint8_t* a = sc.code->data() + sc.start;
    size_t p = 0, n = sc.length;
    while (p < n) {
        const uint8_t op = a[p++];
        mix(op);
        if (op < 0x80) continue;
        if (p + 2 > n) break;
        const size_t len = (size_t)(a[p] | (a[p + 1] << 8));
        p += 2;
        if (p + len > n) break;
        if (op != 0x96) { for (size_t i = 0; i < len; ++i) mix(a[p + i]); p += len; continue; }
        size_t q = p, e = p + len;
        while (q < e) {
            const uint8_t t = a[q++];
            if (t == 8 || t == 9) {
                size_t idx = t == 8 ? a[q] : (size_t)(a[q] | (a[q + 1] << 8));
                q += t == 8 ? 1 : 2;
                mix(0);   // as a string literal
                mixStr(pool && idx < pool->size() ? (*pool)[idx] : std::string());
                continue;
            }
            mix(t);
            size_t sz = t == 0 ? 0 : (t == 1 || t == 7) ? 4 : t == 6 ? 8 : (t == 4 || t == 5) ? 1 : 0;
            if (t == 0) { while (q < e && a[q]) mix(a[q++]); if (q < e) { mix(0); ++q; } continue; }
            for (size_t i = 0; i < sz && q < e; ++i) mix(a[q++]);
        }
        p = e;
    }
    return h;
}

namespace {

// The shared library's findInterpValue (812 bytes): canonical hash of the SharedComponents version.
constexpr uint64_t kFindInterpValueHash = 0x35ffa920f1ebb41bull;   // PlayerList / InGameStats / ... (same canonical body)

struct Ops {
    VM& vm;
    double num(const Value& v) { return vm.toNumber(v); }
    Value sub(const Value& x, const Value& b) { return Value(num(x) - num(b)); }
    Value mul(const Value& x, const Value& b) { return Value(num(x) * num(b)); }
    Value div(const Value& x, const Value& b) {
        const double bb = num(b), xx = num(x);
        return Value(bb == 0 && vm.swfVersion < 5 ? std::numeric_limits<double>::quiet_NaN() : xx / bb);
    }
    Value add2(const Value& x0, const Value& b0) {
        Value b = vm.toPrimitive(b0), x = vm.toPrimitive(x0);
        if (x.t == VType::String || b.t == VType::String) return Value(vm.toString(x) + vm.toString(b));
        return Value(vm.toNumber(x) + vm.toNumber(b));
    }
    Value toInteger(const Value& x) {
        const double v = num(x);
        return Value(std::isfinite(v) ? std::trunc(v) : (std::isnan(v) ? 0.0 : v));
    }
    Value pow(const Value& base, const Value& e) { return Value(std::pow(num(base), num(e))); }   // Math.pow(arg0, arg1)
};

// findInterpValue(_propInit, _propDest, _timeInit, _timeNow, _timeDest, _animType, _interpCurve, _overShoot): the
// bytecode's registers r1 elapsed, r5 init, r4 change, r3 duration, r2 overshoot, r7 curve.
Value nativeFindInterpValueImpl(VM& vm, Args& a);

// WFC_INTERPVERIFY=1 (diagnostics): every call also runs the original script (kept as _global.__wfcScript_findInterpValue)
// and logs a result that differs (the port must be exact).
Value nativeFindInterpValue(VM& vm, const Value& self, Args& a) {
    static const bool verify = std::getenv("WFC_INTERPVERIFY") != nullptr;
    Value r = nativeFindInterpValueImpl(vm, a);
    if (verify) {
        static long calls = 0, bad = 0;
        Value orig = vm.get(vm.global, "__wfcScript_findInterpValue");
        Args copy = a;
        Value s = vm.call(orig, self, copy);
        const bool same = vm.strictEquals(r, s) || (r.t == VType::Number && s.t == VType::Number && std::isnan(r.n) && std::isnan(s.n));
        ++calls;
        if (!same && ++bad <= 20) LOG_WARN("interpverify findInterpValue MISMATCH native=%s script=%s (anim %s)", vm.toString(r).c_str(), vm.toString(s).c_str(), vm.toString(a.size() > 5 ? a[5] : Value()).c_str());
        if (calls % 2000 == 0) LOG_INFO("interpverify findInterpValue %ld calls, %ld mismatches", calls, bad);
    }
    return r;
}

Value nativeFindInterpValueImpl(VM& vm, Args& a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Value::undef(); };
    Ops o{vm};
    const Value r10 = arg(0), r13 = arg(1), r8 = arg(2), r12 = arg(3), r11 = arg(4), r9 = arg(5), r6 = arg(6), r14 = arg(7);
    Value r1 = o.sub(r12, r8);
    const Value r5 = r10;
    const Value r4 = o.sub(r13, r10);
    const Value r3 = o.sub(r11, r8);
    const Value r2 = r14;
    const Value r7 = r6;
    bool linear = vm.looseEquals(r6, Value(0.0));
    if (!linear) linear = vm.toBool(Value(vm.looseEquals(vm.callMethod(r9, "toLowerCase", {}), Value("linear"))));
    if (linear) return o.add2(o.div(o.mul(r4, r1), r3), r5);
    const Value r0 = vm.callMethod(r9, "toLowerCase", {});
    const Value one(1.0), two(2.0), zero(0.0), half(0.5);
    if (vm.strictEquals(r0, Value("easein"))) {
        r1 = o.div(r1, r3);
        Value v = o.mul(r4, r1);
        v = o.mul(v, o.pow(r1, r6));
        Value q = o.sub(o.mul(o.add2(r2, one), r1), r2);
        return o.add2(o.mul(v, q), r5);
    }
    if (vm.strictEquals(r0, Value("easeout"))) {
        if (vm.toBool(vm.lessThan(r6, two))) {
            const Value m = o.sub(zero, r4);
            r1 = o.div(r1, r3);
            Value v = o.mul(m, r1);
            Value q = o.add2(o.mul(o.add2(r2, one), o.sub(r1, two)), r2);
            return o.add2(o.mul(v, q), r5);
        }
        const bool even = vm.looseEquals(o.div(o.toInteger(r6), two), o.toInteger(o.div(o.toInteger(r6), two)));
        if (even) {
            r1 = o.sub(o.div(r1, r3), one);
            Value v = o.mul(r1, o.pow(r1, o.sub(r6, one)));
            Value q = o.add2(o.mul(o.add2(r2, one), r1), r2);
            v = o.add2(o.mul(v, q), one);
            return o.add2(o.mul(r4, v), r5);
        }
        const Value m = o.sub(zero, r4);
        r1 = o.sub(o.div(r1, r3), one);
        Value v = o.mul(r1, o.pow(r1, o.sub(r6, one)));
        Value q = o.add2(o.mul(o.add2(r2, one), r1), r2);
        v = o.sub(o.mul(v, q), one);
        return o.add2(o.mul(m, v), r5);
    }
    if (vm.strictEquals(r0, Value("easeinout"))) {
        // CallFunction "findInterpValue": the name as the scope chain resolves it from the library's timeline (_global).
        const Value self = vm.get(vm.global, "findInterpValue");
        if (vm.toBool(vm.lessThan(r1, o.div(r3, two)))) {
            Args sa{zero, r4, zero, o.mul(r1, two), r3, Value("easein"), r7, r2};
            return o.add2(o.mul(vm.call(self, Value::undef(), sa), half), r5);
        }
        Args sa{zero, r4, zero, o.sub(o.mul(r1, two), r3), r3, Value("easeout"), r7, r2};
        return o.add2(o.add2(o.mul(vm.call(self, Value::undef(), sa), half), o.mul(r4, half)), r5);
    }
    return Value::undef();
}

// ---- updateInterpObjects (1417 bytes) ----
// Runs every frame while tweens live. Its working variables (q, objProp, newValue, rVal, gVal, bVal) are undeclared,
// so the script resolves them through its scope chain (they land on the library's timeline object, where callbacks it
// calls could see them): the port reads / writes them the same way, in the same order, through the original
// function's scope (the interpreter's lookupVar / setVariable rules; the activation holds nothing: this / arguments
// suppressed, no locals). Registers: r1 = _global (preloaded).
constexpr uint64_t kUpdateInterpObjectsHash = 0x03d91ab538964457ull;

struct ScopeVars {
    VM& vm;
    Object* fn;   // the original script function (scope chain, defining timeline)
    Value self;
    Value get(const std::string& n) {
        for (auto it = fn->scope.rbegin(); it != fn->scope.rend(); ++it) {
            Object* s = *it;
            if (!s) continue;
            if (s == vm.global) break;
            if (vm.has(s, n)) return vm.get(s, n);
        }
        if (vm.has(vm.global, n)) return vm.get(vm.global, n);
        return Value::undef();
    }
    Object* targetObj() {
        gfx::DisplayObject* t = fn->defTarget;
        if (!t && self.isObject() && self.o->display && self.o->display->kind == gfx::DisplayObject::Kind::Clip) t = self.o->display;
        if (!t || (t->removed && !fn->defTarget)) t = vm.player()->root();
        return t ? vm.player()->scriptObject(t) : nullptr;
    }
    // CallFunction <name>(args): the name through the scope chain, 'this' = the calling timeline.
    Value callFunction(const std::string& n, Args args) {
        Value f = get(n);
        if (!f.isObject() || f.o->kind != ObjKind::Function) return Value::undef();
        Object* t = targetObj();
        return vm.call(f, t ? Value(t) : Value::undef(), args);
    }
    void set(const std::string& n, const Value& v) {
        for (auto it = fn->scope.rbegin(); it != fn->scope.rend(); ++it) {
            Object* s = *it;
            if (!s || s == vm.global) continue;
            if (vm.has(s, n)) { vm.set(s, n, v); return; }
        }
        for (auto it = fn->scope.rbegin(); it != fn->scope.rend(); ++it) {
            Object* s = *it;
            if (s && s->kind == ObjKind::Clip) { vm.set(s, n, v); return; }
        }
        gfx::DisplayObject* t = fn->defTarget;
        if (!t && self.isObject() && self.o->display && self.o->display->kind == gfx::DisplayObject::Kind::Clip) t = self.o->display;
        if (!t || (t->removed && !fn->defTarget)) t = vm.player()->root();
        if (Object* to = t ? vm.player()->scriptObject(t) : nullptr) { vm.set(to, n, v); return; }
        vm.set(vm.global, n, v);
    }
};

// GetMember / SetMember with a Value name, as the interpreter's ops do it.
Value getMember(VM& vm, const Value& o, const Value& nameV) {
    if (nameV.t == VType::Number && o.t == VType::Object && o.o && o.o->kind == ObjKind::Array && !o.o->zombie &&
        nameV.n >= 0.0 && nameV.n < 1e9 && nameV.n == std::floor(nameV.n)) {
        const size_t i = (size_t)nameV.n;
        return i < o.o->elems.size() ? o.o->elems[i] : Value::undef();
    }
    if (nameV.t == VType::String) return vm.getV(o, nameV.s, nameV.atom);
    return vm.getV(o, nameV.t == VType::Number ? VM::numberToString(nameV.n) : vm.toString(nameV));
}
Value getMember(VM& vm, const Value& o, const char* name) { return vm.getV(o, name); }
void setMember(VM& vm, const Value& o, const Value& nameV, const Value& v) {
    if (nameV.t == VType::Number && o.isObject() && o.o->kind == ObjKind::Array && !o.o->zombie && o.o->watches.empty() &&
        nameV.n >= 0.0 && nameV.n == std::floor(nameV.n) && nameV.n < (double)o.o->elems.size()) {
        o.o->elems[(size_t)nameV.n] = v;
        return;
    }
    if (!o.isObject()) return;
    if (nameV.t == VType::String) vm.set(o.o, nameV.s, v, nameV.atom);
    else vm.set(o.o, nameV.t == VType::Number ? VM::numberToString(nameV.n) : vm.toString(nameV), v);
}
void setMember(VM& vm, const Value& o, const char* name, const Value& v) { if (o.isObject()) vm.set(o.o, name, v); }

Value nativeUpdateInterpObjects(VM& vm, Object* orig, const Value& self) {
    ScopeVars sv{vm, orig, self};
    Ops op{vm};
    const Value r1(vm.global);
    const Value one(1.0), hundred(100.0);
    // interpController.currentTime = getTimer()
    setMember(vm, getMember(vm, r1, "interpController"), "currentTime", Value(std::floor(vm.player()->timeMs())));
    sv.set("q", Value(0.0));
    auto splice = [&]() {
        Value qv = sv.get("q");
        Value list = getMember(vm, getMember(vm, r1, "interpController"), "propInterpList");
        vm.callMethod(list, "splice", {qv, one});
        sv.set("q", Value(vm.toNumber(sv.get("q")) - 1.0));
    };
    auto objProp = [&]() { return sv.get("objProp"); };
    auto curTime = [&]() { return getMember(vm, getMember(vm, r1, "interpController"), "currentTime"); };
    // findInterpValue(propInit, propDest, timeInit, currentTime, timeDest, animType, curve, overShoot), its pushes in
    // the bytecode's order (overShoot first, propInit last).
    auto interp = [&](const char* sub) {
        Value os = getMember(vm, objProp(), "_overShoot");
        Value cu = getMember(vm, objProp(), "_interpCurve");
        Value an = getMember(vm, objProp(), "_animType");
        Value td = getMember(vm, objProp(), "_timeDest");
        Value ct = curTime();
        Value ti = getMember(vm, objProp(), "_timeInit");
        Value pd = getMember(vm, objProp(), "_propDest");
        if (sub) pd = getMember(vm, pd, sub);
        Value pi = getMember(vm, objProp(), "_propInit");
        if (sub) pi = getMember(vm, pi, sub);
        return vm.callMethod(r1, "findInterpValue", {pi, pd, ti, ct, td, an, cu, os});
    };
    auto applyColour = [&](const Value& red, const Value& green, const Value& blue) {
        setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "redMultiplier", red);
        setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "greenMultiplier", green);
        setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "blueMultiplier", blue);
        setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "alphaMultiplier",
                  op.div(getMember(vm, getMember(vm, objProp(), "_targ"), "_alpha"), hundred));
        setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "objTrans"), "colorTransform",
                  getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"));
    };
    for (int guard = 0; guard < 1000000; ++guard) {
        // while (q < interpController.propInterpList.length)
        Value qv = sv.get("q");
        Value len = getMember(vm, getMember(vm, getMember(vm, r1, "interpController"), "propInterpList"), "length");
        if (!vm.toBool(vm.lessThan(qv, len))) break;
        sv.set("objProp", getMember(vm, getMember(vm, getMember(vm, r1, "interpController"), "propInterpList"), sv.get("q")));
        if (vm.looseEquals(getMember(vm, getMember(vm, objProp(), "_targ"), "_name"), Value::undef())) {
            splice();                                            // the target went away
        } else {
            bool assign = true;
            if (!vm.toBool(vm.lessThan(curTime(), getMember(vm, objProp(), "_timeDest")))) {
                // finished: the destination
                const Value r0 = getMember(vm, objProp(), "_prop");
                if (vm.strictEquals(r0, Value("callback"))) {
                    if (vm.looseEquals(Value(vm.typeOf(getMember(vm, objProp(), "_propDest"))), Value("function")))
                        vm.callMethod(objProp(), "_propDest", {});
                } else if (vm.strictEquals(r0, Value("color"))) {
                    Value pd = getMember(vm, objProp(), "_propDest");
                    // the bytecode reads _propDest.red / green / blue afresh for each multiplier
                    setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "redMultiplier",
                              getMember(vm, getMember(vm, objProp(), "_propDest"), "red"));
                    setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "greenMultiplier",
                              getMember(vm, getMember(vm, objProp(), "_propDest"), "green"));
                    setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "blueMultiplier",
                              getMember(vm, getMember(vm, objProp(), "_propDest"), "blue"));
                    setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"), "alphaMultiplier",
                              op.div(getMember(vm, getMember(vm, objProp(), "_targ"), "_alpha"), hundred));
                    setMember(vm, getMember(vm, getMember(vm, objProp(), "_targ"), "objTrans"), "colorTransform",
                              getMember(vm, getMember(vm, objProp(), "_targ"), "colorTrans"));
                    (void)pd;
                }
                sv.set("newValue", getMember(vm, objProp(), "_propDest"));
                splice();
            } else {
                const Value r0 = getMember(vm, objProp(), "_prop");
                if (vm.strictEquals(r0, Value("color"))) {
                    sv.set("rVal", interp("red"));
                    sv.set("gVal", interp("green"));
                    sv.set("bVal", interp("blue"));
                    applyColour(sv.get("rVal"), sv.get("gVal"), sv.get("bVal"));
                    // newValue = {red: rVal, green: gVal, blue: bVal} (InitObject sets them red, green, blue)
                    Value rv = sv.get("rVal"), gv = sv.get("gVal"), bv = sv.get("bVal");
                    Object* nv = vm.newPlain();
                    vm.set(nv, "red", rv); vm.set(nv, "green", gv); vm.set(nv, "blue", bv);
                    sv.set("newValue", Value(nv));
                } else {
                    sv.set("newValue", interp(nullptr));
                }
            }
            if (assign) {
                // objProp._targ[objProp._prop] = newValue
                Value targ = getMember(vm, objProp(), "_targ");
                Value prop = getMember(vm, objProp(), "_prop");
                setMember(vm, targ, prop, sv.get("newValue"));
            }
        }
        sv.set("q", Value(vm.toNumber(sv.get("q")) + 1.0));
    }
    if (vm.looseEquals(getMember(vm, getMember(vm, getMember(vm, r1, "interpController"), "propInterpList"), "length"), Value(0.0)))
        vm.callMethod(r1, "removeInterpController", {});
    return Value::undef();
}

// ---- addInterp (921 bytes) ----
// addInterp(targetObj, interpTime, animType, interpCurve, overShoot, interpParams). Registers: r3 targetObj, r17 time,
// r16 animType, r19 curve, r18 overShoot, r2 params, r1 _global, r20 the enumerated property. The dedupe counter n and
// the colour channels rVal / gVal / bVal are undeclared variables (scope chain / timeline, as in updateInterpObjects).
constexpr uint64_t kAddInterpHash = 0x2b272695c978e5bfull;   // PlayerList / InGameStats / ... (same canonical body)

Value nativeAddInterp(VM& vm, Object* orig, const Value& self, Args& a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Value::undef(); };
    ScopeVars sv{vm, orig, self};
    Ops op{vm};
    const Value r1(vm.global), r3 = arg(0), r2 = arg(5), r19 = arg(3), r18 = arg(4);
    Value r17 = arg(1), r16 = arg(2);
    const Value one(1.0), two(2.0), zero(0.0);
    if (vm.looseEquals(r17, Value::undef())) r17 = one;
    {
        bool c = vm.looseEquals(r16, Value::undef());
        if (!c) c = vm.looseEquals(r16, Value(""));
        if (c) r16 = Value("linear");
    }
    if (vm.looseEquals(getMember(vm, r1, "interpController"), Value::undef())) vm.callMethod(r1, "createInterpController", {});
    setMember(vm, getMember(vm, r1, "interpController"), "currentTime", Value(std::floor(vm.player()->timeMs())));
    // for (r20 in interpParams): Enumerate2 pushes the keys in enumeration order; the loop pops them (reverse order)
    std::vector<std::string> keys;
    if (r2.isObject()) keys = vm.enumerate(r2.o);
    auto list = [&]() { return getMember(vm, getMember(vm, r1, "interpController"), "propInterpList"); };
    for (auto kit = keys.rbegin(); kit != keys.rend(); ++kit) {
        const Value r20(*kit);
        sv.set("n", zero);
        for (int guard = 0; guard < 1000000; ++guard) {
            if (!vm.toBool(vm.lessThan(sv.get("n"), getMember(vm, list(), "length")))) break;
            if (vm.looseEquals(getMember(vm, getMember(vm, list(), sv.get("n")), "_targ"), r3) &&
                vm.looseEquals(getMember(vm, getMember(vm, list(), sv.get("n")), "_prop"), r20)) {
                Value nv = sv.get("n");
                vm.callMethod(list(), "splice", {nv, one});
                sv.set("n", Value(vm.toNumber(sv.get("n")) - 1.0));
            }
            sv.set("n", Value(vm.toNumber(sv.get("n")) + 1.0));
        }
        if (vm.looseEquals(r20, Value("color"))) {
            // targetObj.colorTrans = new flash.geom.ColorTransform(); targetObj.objTrans = new flash.geom.Transform(targetObj)
            {
                Value geom = getMember(vm, sv.get("flash"), "geom");
                Value ctor = vm.getV(geom, "ColorTransform");
                Args none;
                setMember(vm, r3, "colorTrans", ctor.isObject() ? vm.construct(ctor.o, none) : Value::undef());
            }
            {
                Value geom = getMember(vm, sv.get("flash"), "geom");
                Value ctor = vm.getV(geom, "Transform");
                Args ta{r3};
                setMember(vm, r3, "objTrans", ctor.isObject() ? vm.construct(ctor.o, ta) : Value::undef());
            }
            if (vm.looseEquals(Value(vm.typeOf(getMember(vm, r2, r20))), Value("number")))
                setMember(vm, r2, r20, vm.callMethod(getMember(vm, r2, r20), "toString", {Value(16.0)}));
            for (int guard = 0; guard < 64; ++guard) {
                if (!vm.toBool(vm.lessThan(getMember(vm, getMember(vm, r2, r20), "length"), Value(6.0)))) break;
                setMember(vm, r2, r20, op.add2(Value("0"), getMember(vm, r2, r20)));
            }
            auto channel = [&](double from) {
                Value sub = vm.callMethod(getMember(vm, r2, r20), "substr", {Value(from), two});
                return op.div(sv.callFunction("parseInt", {op.add2(Value("0x"), sub)}), Value(255.0));
            };
            sv.set("rVal", channel(0));
            sv.set("gVal", channel(2));
            sv.set("bVal", channel(4));
            {
                Value rv = sv.get("rVal"), gv = sv.get("gVal"), bv = sv.get("bVal");
                Object* o = vm.newPlain();
                vm.set(o, "red", rv); vm.set(o, "green", gv); vm.set(o, "blue", bv);
                setMember(vm, r2, r20, Value(o));
            }
            {
                auto mult = [&](const char* m) { return getMember(vm, getMember(vm, getMember(vm, r3, "objTrans"), "colorTransform"), m); };
                Value rr = mult("redMultiplier"), gg = mult("greenMultiplier"), bb = mult("blueMultiplier");
                Object* o = vm.newPlain();
                vm.set(o, "red", rr); vm.set(o, "green", gg); vm.set(o, "blue", bb);
                setMember(vm, r3, "color", Value(o));
            }
        }
        // propInterpList.push({_targ, _prop, _propInit, _propDest, _timeInit, _timeDest, _animType, _overShoot,
        // _interpCurve}) - the values in push order, set in that order (InitObject)
        Value pInit = getMember(vm, r3, r20);
        Value pDest = getMember(vm, r2, r20);
        Value tInit = getMember(vm, getMember(vm, r1, "interpController"), "currentTime");
        Value tDest = op.add2(getMember(vm, getMember(vm, r1, "interpController"), "currentTime"), op.mul(r17, Value(1000.0)));
        Object* e = vm.newPlain();
        vm.set(e, "_targ", r3); vm.set(e, "_prop", r20); vm.set(e, "_propInit", pInit); vm.set(e, "_propDest", pDest);
        vm.set(e, "_timeInit", tInit); vm.set(e, "_timeDest", tDest); vm.set(e, "_animType", r16);
        vm.set(e, "_overShoot", r18); vm.set(e, "_interpCurve", r19);
        vm.callMethod(list(), "push", {Value(e)});
    }
    return Value::undef();
}

// ---- Data-store reads (PlayerList: one call per cell, ~1000 per rebuild) ----
// HmInterfaceDataStores.prototype.ReadCollectionValue / ReadCollectionBoolValue(Markup, ColumnTag, RowNum):
//   return flash.external.ExternalInterface.call("DataStores.ReadCollection[Bool]Value", Markup, ColumnTag, RowNum)
constexpr uint64_t kReadCollectionValueHash = 0xa7f8f93f5bd5f6d9ull;
constexpr uint64_t kReadCollectionBoolValueHash = 0x9368d67978bf4855ull;
Value nativeReadCollection(VM& vm, Object* orig, const Value& self, Args& a, const char* call) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Value::undef(); };
    ScopeVars sv{vm, orig, self};
    Value ei = getMember(vm, getMember(vm, sv.get("flash"), "external"), "ExternalInterface");
    return vm.callMethod(ei, "call", {Value(call), arg(0), arg(1), arg(2)});
}

// AssignDataStoreRead(DataObject, DSMarkup, Column "Name:Type", Row) - PlayerList's per-cell read.
constexpr uint64_t kAssignDataStoreReadHash = 0xfbbabacac57d575cull;
Value nativeAssignDataStoreRead(VM& vm, Object* orig, const Value& self, Args& a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : Value::undef(); };
    ScopeVars sv{vm, orig, self};
    const Value r3 = arg(0), r5 = arg(1), r6 = arg(2), r4 = arg(3);
    if (vm.toBool(getMember(vm, r3, "IsConnecting"))) return Value::undef();
    const Value r2 = vm.callMethod(r6, "split", {Value(":")});
    const Value r1 = getMember(vm, r2, Value(0.0));
    {
        const bool eq = vm.looseEquals(r1, Value("CurrentCharacterString"));
        Value c(eq);
        if (eq) c = sv.get("ShouldCycleSpecialtyLevels");
        if (vm.toBool(c)) return Value::undef();
    }
    const Value r0 = getMember(vm, r2, Value(1.0));
    auto ds = [&]() { return getMember(vm, sv.get("HmExternalInterface"), "DataStores"); };
    if (vm.strictEquals(r0, Value("Number"))) {
        Value raw = vm.callMethod(ds(), "ReadCollectionValue", {r5, r1, r4});
        setMember(vm, r3, r1, sv.callFunction("parseInt", {raw}));
    } else if (vm.strictEquals(r0, Value("Boolean"))) {
        setMember(vm, r3, r1, vm.callMethod(ds(), "ReadCollectionBoolValue", {r5, r1, r4}));
    } else {
        setMember(vm, r3, r1, vm.callMethod(ds(), "ReadCollectionValue", {r5, r1, r4}));
    }
    return Value::undef();
}

// ---- UpdatePlayerEntry(PlayerInfo) (586 bytes): PlayerList's per-row update ----
// Find the row clip whose [PlayerElementId] matches (attach + CreateRowElements a new one otherwise), then copy every
// PlayerInfo field onto it (and its panel in iconic mode), except CurrentCharacterString while specialties cycle.
constexpr uint64_t kUpdatePlayerEntryHash = 0x6b42021d552baf71ull;
Value nativeUpdatePlayerEntry(VM& vm, Object* orig, const Value& self, Args& a) {
    ScopeVars sv{vm, orig, self};
    Ops op{vm};
    const Value r3 = a.empty() ? Value::undef() : a[0];
    Value r1 = Value::undef();
    bool found = false;
    Value r2(0.0);
    for (int guard = 0; guard < 1000000; ++guard) {
        if (!vm.toBool(vm.lessThan(r2, getMember(vm, sv.get("PlayerListElements"), "length")))) break;
        Value mine = getMember(vm, getMember(vm, sv.get("PlayerListElements"), r2), sv.get("PlayerElementId"));
        Value theirs = getMember(vm, r3, sv.get("PlayerElementId"));
        if (vm.looseEquals(mine, theirs)) {
            r1 = getMember(vm, sv.get("PlayerListElements"), r2);
            found = true;
            break;
        }
        r2 = Value(vm.toNumber(r2) + 1.0);
    }
    // IconicMode && !DisableIconicDisplay, with the bytecode's value chain
    auto iconic = [&]() {
        Value v = sv.get("IconicMode");
        if (vm.toBool(v)) v = Value(!vm.toBool(sv.get("DisableIconicDisplay")));
        return vm.toBool(v);
    };
    if (!found) {
        const bool ic = iconic();
        Value list = sv.get("playerList_mc");
        Value depth = vm.callMethod(list, "getNextHighestDepth", {});
        Value name = op.add2(op.add2(Value(ic ? "iconicEntry" : "playerEntry"), sv.get("PlayerEntryCount")), Value("_mc"));
        r1 = vm.callMethod(sv.get("playerList_mc"), "attachMovie", {Value(ic ? "mc_iconicEntry" : "mc_playerEntry"), name, depth});
        if (ic) sv.callFunction("CreateRowElements", {getMember(vm, r1, "panel_mc"), sv.get("PlayerEntryDisplayModel"), Value(44.0)});
        else sv.callFunction("CreateRowElements", {r1, sv.get("PlayerEntryDisplayModel")});
        setMember(vm, r1, "_y", sv.get("ListElementPosition"));
        vm.callMethod(sv.get("PlayerListElements"), "push", {r1});
        sv.set("PlayerEntryCount", Value(vm.toNumber(sv.get("PlayerEntryCount")) + 1.0));
    }
    std::vector<std::string> keys;
    if (r3.isObject()) keys = vm.enumerate(r3.o);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        const Value r5(*it);
        Value c = sv.get("ShouldCycleSpecialtyLevels");
        if (vm.toBool(c)) c = Value(vm.looseEquals(r5, Value("CurrentCharacterString")));
        if (vm.toBool(c)) c = Value(vm.looseEquals(getMember(vm, r1, r5), Value::undef()));
        if (vm.toBool(c)) {
            setMember(vm, r1, r5, getMember(vm, sv.get("SpecialtyCycleOrder"), sv.get("CurrentSpecialtyCycle")));
        } else if (iconic()) {
            setMember(vm, r1, r5, getMember(vm, r3, r5));
            setMember(vm, getMember(vm, r1, "panel_mc"), r5, getMember(vm, r3, r5));
        } else {
            setMember(vm, r1, r5, getMember(vm, r3, r5));
        }
    }
    return r1;
}

// ---- BuildPlayerList() (1541 bytes): PlayerList's once-a-second layout pass ----
constexpr uint64_t kBuildPlayerListHash = 0x0f878fb7f23831f4ull;
Value nativeBuildPlayerList(VM& vm, Object* orig, const Value& self, Args&) {
    ScopeVars sv{vm, orig, self};
    Ops op{vm};
    const Value undef = Value::undef(), one(1.0), zero(0.0);
    auto inc = [&](const Value& v) { return Value(vm.toNumber(v) + 1.0); };
    auto dec = [&](const Value& v) { return Value(vm.toNumber(v) - 1.0); };
    auto iconic = [&]() {
        Value v = sv.get("IconicMode");
        if (vm.toBool(v)) v = Value(!vm.toBool(sv.get("DisableIconicDisplay")));
        return vm.toBool(v);
    };
    sv.set("ListElementPosition", zero);
    {
        Value ctor = sv.get("Array");
        Args none;
        sv.set("FocusElements", ctor.isObject() ? vm.construct(ctor.o, none) : undef);
    }
    // rows: headers / entries / spacers, each tweened to its slot
    Value r7 = zero;
    for (int guard = 0; guard < 1000000; ++guard) {
        if (!vm.toBool(vm.lessThan(r7, getMember(vm, sv.get("PlayerListData"), "length")))) break;
        Value r2 = undef, r5 = undef;
        auto data = [&]() { return getMember(vm, sv.get("PlayerListData"), r7); };
        if (!vm.looseEquals(getMember(vm, data(), "TeamName"), undef)) {
            r2 = sv.callFunction("UpdateTeamHeader", {data()});
            r5 = Value(54.0);
        } else if (!vm.looseEquals(getMember(vm, data(), "TeamIndex"), undef)) {
            r2 = sv.callFunction("UpdateTeamHeader", {data()});
            r5 = Value(20.0);
        } else if (!vm.looseEquals(getMember(vm, data(), "PlayerName"), undef)) {
            r2 = sv.callFunction("UpdatePlayerEntry", {data()});
            r5 = Value(iconic() ? 104.0 : 25.0);
        } else {
            r5 = data();
        }
        if (!vm.looseEquals(r2, undef)) {
            Object* params = vm.newPlain();
            vm.set(params, "_y", sv.get("ListElementPosition"));
            vm.callMethod(r2, "interp", {Value(0.4), Value("easeout"), Value(3.0), Value(params)});
            if (vm.looseEquals(getMember(vm, r2, "__proto__"), getMember(vm, sv.get("HmButton"), "prototype")))
                vm.callMethod(sv.get("FocusElements"), "push", {r2});
        }
        sv.set("ListElementPosition", op.add2(sv.get("ListElementPosition"), r5));
        r7 = inc(r7);
    }
    // focus links (wrapping)
    auto fe = [&]() { return sv.get("FocusElements"); };
    r7 = zero;
    for (int guard = 0; guard < 1000000; ++guard) {
        if (!vm.toBool(vm.lessThan(r7, getMember(vm, fe(), "length")))) break;
        setMember(vm, getMember(vm, fe(), r7), "ListIndex", r7);
        if (vm.toBool(vm.lessThan(op.sub(r7, one), zero)))
            setMember(vm, getMember(vm, fe(), r7), "focusUp",
                      getMember(vm, getMember(vm, fe(), op.sub(getMember(vm, fe(), "length"), one)), "_name"));
        else
            setMember(vm, getMember(vm, fe(), r7), "focusUp", getMember(vm, getMember(vm, fe(), op.sub(r7, one)), "_name"));
        // Greater(r7 + 1, length - 1)
        if (vm.toBool(vm.lessThan(op.sub(getMember(vm, fe(), "length"), one), op.add2(r7, one))))
            setMember(vm, getMember(vm, fe(), r7), "focusDown", getMember(vm, getMember(vm, fe(), zero), "_name"));
        else
            setMember(vm, getMember(vm, fe(), r7), "focusDown", getMember(vm, getMember(vm, fe(), op.add2(r7, one)), "_name"));
        r7 = inc(r7);
    }
    // rows no longer in the data: removed (moving the focus off a removed focused row)
    auto ple = [&]() { return sv.get("PlayerListElements"); };
    r7 = zero;
    for (int guard = 0; guard < 1000000; ++guard) {
        if (!vm.toBool(vm.lessThan(r7, getMember(vm, ple(), "length")))) break;
        bool r6 = false;
        Value r1 = zero;
        for (int g2 = 0; g2 < 1000000; ++g2) {
            if (!vm.toBool(vm.lessThan(r1, getMember(vm, sv.get("PlayerListData"), "length")))) break;
            Value c(!vm.looseEquals(getMember(vm, getMember(vm, ple(), r7), sv.get("PlayerElementId")), undef));
            if (vm.toBool(c))
                c = Value(vm.looseEquals(getMember(vm, getMember(vm, ple(), r7), sv.get("PlayerElementId")),
                                         getMember(vm, getMember(vm, sv.get("PlayerListData"), r1), sv.get("PlayerElementId"))));
            if (vm.toBool(c)) { r6 = true; break; }
            c = Value(!vm.looseEquals(getMember(vm, getMember(vm, ple(), r7), sv.get("HeaderElementId")), undef));
            if (vm.toBool(c))
                c = Value(vm.looseEquals(getMember(vm, getMember(vm, ple(), r7), sv.get("HeaderElementId")),
                                         getMember(vm, getMember(vm, sv.get("PlayerListData"), r1), sv.get("HeaderElementId"))));
            if (vm.toBool(c)) { r6 = true; break; }
            r1 = inc(r1);
        }
        if (!r6) {
            if (vm.looseEquals(getMember(vm, ple(), r7), getMember(vm, sv.get("playerList_mc"), "currentFocus"))) {
                Value r4 = getMember(vm, getMember(vm, ple(), r7), "ListIndex");
                Value r3 = getMember(vm, fe(), r4);
                for (int g3 = 0; g3 < 1000000 && vm.looseEquals(getMember(vm, r3, "_name"), undef); ++g3) {
                    r4 = dec(r4);
                    r3 = getMember(vm, fe(), r4);
                }
                sv.set("SelectedPlayerEntry", r3);
                sv.callFunction("UpdateCurrentFocus", {});
            }
            vm.callMethod(getMember(vm, ple(), r7), "removeMovieClip", {});
            vm.callMethod(ple(), "splice", {r7, one});
            r7 = dec(r7);
        }
        r7 = inc(r7);
    }
    // nothing selected: the local player's row, else the first
    if (vm.looseEquals(sv.get("SelectedPlayerEntry"), undef)) {
        r7 = zero;
        for (int guard = 0; guard < 1000000; ++guard) {
            if (!vm.toBool(vm.lessThan(r7, getMember(vm, fe(), "length")))) break;
            if (vm.looseEquals(getMember(vm, getMember(vm, fe(), r7), "PlayerName"), sv.get("LocalPlayerName"))) {
                sv.set("SelectedPlayerEntry", getMember(vm, fe(), r7));
                sv.callFunction("UpdateCurrentFocus", {});
                return undef;
            }
            r7 = inc(r7);
        }
        sv.set("SelectedPlayerEntry", getMember(vm, fe(), zero));
        sv.callFunction("UpdateCurrentFocus", {});
    }
    return undef;
}

// ---- UpdatePlayerListData() (1610 bytes): PlayerList's once-a-second data pass ----
// Players (and teams) read cell by cell from the data stores, sorted, grouped under their team headers (spacers /
// the unassigned group as the movie lays them out), then BuildPlayerList and the specialty-cycling switch.
constexpr uint64_t kUpdatePlayerListDataHash = 0x06b555848a7ddf74ull;
Value nativeUpdatePlayerListData(VM& vm, Object* orig, const Value& self, Args&) {
    ScopeVars sv{vm, orig, self};
    const Value undef = Value::undef(), one(1.0), zero(0.0);
    auto inc = [&](const Value& v) { return Value(vm.toNumber(v) + 1.0); };
    auto dec = [&](const Value& v) { return Value(vm.toNumber(v) - 1.0); };
    auto less = [&](const Value& a, const Value& b) { return vm.toBool(vm.lessThan(a, b)); };
    auto newObj = [&](const char* ctorName) {   // NewObject <name> with no arguments
        Value ctor = sv.get(ctorName);
        Args none;
        return ctor.isObject() ? vm.construct(ctor.o, none) : undef;
    };
    auto bitOr = [&](const Value& x, const Value& b) { return Value((double)(vm.toInt32(x) | vm.toInt32(b))); };
    auto arrConst = [&](const char* n) { return getMember(vm, sv.get("Array"), n); };
    auto rowCount = [&](const char* markup) {
        return vm.callMethod(getMember(vm, sv.get("HmExternalInterface"), "DataStores"), "GetCollectionRowCount", {Value(markup)});
    };
    // rows of a collection: one object per row, each column assigned by AssignDataStoreRead
    auto readRows = [&](const Value& into, const Value& count, const char* markup, const char* columnsVar) {
        Value r6 = zero;
        for (int guard = 0; guard < 1000000 && less(r6, count); ++guard) {
            Value row = newObj("Object");
            Value r2 = zero;
            for (int g2 = 0; g2 < 1000000; ++g2) {
                if (!less(r2, getMember(vm, sv.get(columnsVar), "length"))) break;
                Value col = getMember(vm, sv.get(columnsVar), r2);
                sv.callFunction("AssignDataStoreRead", {row, Value(markup), col, r6});
                r2 = inc(r2);
            }
            vm.callMethod(into, "push", {row});
            r6 = inc(r6);
        }
    };
    // !IconicMode || DisableIconicDisplay, with the bytecode's value chain
    auto plainLayout = [&]() {
        Value v(!vm.toBool(sv.get("IconicMode")));
        if (!vm.toBool(v)) v = sv.get("DisableIconicDisplay");
        return vm.toBool(v);
    };
    auto pld = [&]() { return sv.get("PlayerListData"); };

    const Value r1 = newObj("Array");
    {
        Value r9 = rowCount("<CurrentGame:Players>");
        readRows(r1, r9, "<CurrentGame:Players>", "PlayerColumnsToGet");
    }
    {
        // the InitArray operands in push order (Array.X read left to right); elements are the reverse
        Value ci = arrConst("CASEINSENSITIVE");
        Value n1 = arrConst("NUMERIC");
        Value nA = arrConst("NUMERIC"), dA = arrConst("DESCENDING");
        Value nd1 = bitOr(nA, dA);
        Value nB = arrConst("NUMERIC"), dB = arrConst("DESCENDING");
        Value nd2 = bitOr(nB, dB);
        Value opts(vm.newArray({zero, nd2, nd1, n1, ci}));
        Value fields(vm.newArray({Value("IsConnecting"), Value("Score"), Value("Kills"), Value("Deaths"), Value("PlayerName")}));
        vm.callMethod(r1, "sortOn", {fields, opts});
    }
    bool r8 = false;
    {
        Value r6 = zero;
        for (int guard = 0; guard < 1000000 && less(r6, getMember(vm, r1, "length")); ++guard) {
            if (less(getMember(vm, getMember(vm, r1, r6), "TeamID"), Value(255.0))) { r8 = true; break; }
            r6 = inc(r6);
        }
    }
    const Value r3 = newObj("Array");
    if (r8) {
        Value r10 = rowCount("<CurrentGame:Teams>");
        readRows(r3, r10, "<CurrentGame:Teams>", "TeamColumnsToGet");
        Value n1 = arrConst("NUMERIC");
        Value nA = arrConst("NUMERIC"), dA = arrConst("DESCENDING");
        Value nd = bitOr(nA, dA);
        Value opts(vm.newArray({nd, n1}));
        Value fields(vm.newArray({Value("Score"), Value("TeamIndex")}));
        vm.callMethod(r3, "sortOn", {fields, opts});
    }
    sv.set("PlayerListData", newObj("Array"));
    // the team's players, moved out of r1 into PlayerListData after the header
    auto moveTeam = [&](const Value& r6) {
        Value r2 = zero;
        for (int guard = 0; guard < 1000000 && less(r2, getMember(vm, r1, "length")); ++guard) {
            if (vm.looseEquals(getMember(vm, getMember(vm, r1, r2), "TeamID"), getMember(vm, getMember(vm, r3, r6), "TeamIndex"))) {
                vm.callMethod(pld(), "push", {getMember(vm, r1, r2)});
                vm.callMethod(r1, "splice", {r2, one});
                r2 = dec(r2);
            }
            r2 = inc(r2);
        }
    };
    {
        Value r6 = zero;
        for (int guard = 0; guard < 1000000 && less(r6, getMember(vm, r3, "length")); ++guard) {
            if (vm.looseEquals(sv.get("GameTeamStatus"), Value("GTS_TeamGame"))) {
                vm.callMethod(pld(), "push", {getMember(vm, r3, r6)});
                moveTeam(r6);
                vm.callMethod(pld(), "push", {Value(30.0)});
            } else {
                bool r5 = false;
                Value r2 = zero;
                for (int g2 = 0; g2 < 1000000 && less(r2, getMember(vm, r1, "length")); ++g2) {
                    if (vm.looseEquals(getMember(vm, getMember(vm, r1, r2), "TeamID"), getMember(vm, getMember(vm, r3, r6), "TeamIndex"))) {
                        r5 = true;
                        break;
                    }
                    r2 = inc(r2);
                }
                if (r5) {
                    vm.callMethod(pld(), "push", {getMember(vm, r3, r6)});
                    moveTeam(r6);
                    if (plainLayout()) vm.callMethod(pld(), "push", {Value(30.0)});
                }
            }
            r6 = inc(r6);
        }
    }
    // players left without a team: under a TeamIndex 255 header (plain layout), then each
    if (vm.toBool(vm.lessThan(zero, getMember(vm, r1, "length")))) {
        if (plainLayout()) {
            Object* hdr = vm.newPlain();
            vm.set(hdr, "TeamIndex", Value(255.0));
            vm.callMethod(pld(), "push", {Value(hdr)});
        }
        Value r6 = zero;
        for (int guard = 0; guard < 1000000 && less(r6, getMember(vm, r1, "length")); ++guard) {
            vm.callMethod(pld(), "push", {getMember(vm, r1, r6)});
            r6 = inc(r6);
        }
    }
    sv.callFunction("BuildPlayerList", {});
    if (vm.toBool(sv.get("ShouldCycleSpecialtyLevels"))) {
        if (!vm.toBool(sv.get("SpecialtiesAreCycling"))) {
            sv.callFunction("CycleSpecialtyLevels", {});
            sv.set("SpecialtiesAreCycling", Value(true));
        }
    } else {
        sv.set("SpecialtiesAreCycling", Value(false));
    }
    return undef;
}

}  // namespace

// Called when a script function is assigned to _global.<name>: the native port when the body is the known version.
bool nativeLibraryOverride(VM& vm, const std::string& name, Value& v) {
    static const bool off = std::getenv("WFC_NONATIVEINTERP") != nullptr;
    static const bool logHash = std::getenv("WFC_AVMHASH") != nullptr;   // DEV TOOL: log candidates' hashes
    if (off || !v.isObject() || !v.o->script) return false;
    if (name != "findInterpValue" && name != "updateInterpObjects" && name != "addInterp" && name != "AssignDataStoreRead" &&
        name != "ReadCollectionValue" && name != "ReadCollectionBoolValue" && name != "UpdatePlayerEntry" &&
        name != "BuildPlayerList" && name != "UpdatePlayerListData") return false;
    const uint64_t h = canonicalFunctionHash(*v.o->script, v.o->pool.get());
    if (logHash) LOG_INFO("avmhash %s %016llx", name.c_str(), (unsigned long long)h);
    if (name == "findInterpValue" && kFindInterpValueHash && h == kFindInterpValueHash) {
        vm.global->setRaw("__wfcScript_" + name, v, DontEnum);   // the script, for WFC_INTERPVERIFY (and rooted)
        v = Value(vm.newFunction(nativeFindInterpValue, name, 8));
        return true;
    }
    if (name == "updateInterpObjects" && h == kUpdateInterpObjectsHash) {
        vm.global->setRaw("__wfcScript_" + name, v, DontEnum);   // rooted: its scope chain and timeline are used
        Object* orig = v.o;
        v = Value(vm.newFunction([orig](VM& m, const Value& self, Args&) {
            static const bool verify = std::getenv("WFC_INTERPVERIFY") != nullptr;
            if (verify) return m.call(Value(orig), self, *std::make_unique<Args>());   // verify runs: the script itself
            return nativeUpdateInterpObjects(m, orig, self);
        }, name, 0));
        return true;
    }
    auto bindDs = [&](Value (*fnp)(VM&, Object*, const Value&, Args&)) {
        // the script stays reachable from the native closure (and is kept alive by the binding object below)
        Object* orig = v.o;
        Object* f = vm.newFunction([orig, fnp](VM& m, const Value& self, Args& args) {
            static const bool verify = std::getenv("WFC_INTERPVERIFY") != nullptr;
            if (verify) return m.call(Value(orig), self, args);
            return fnp(m, orig, self, args);
        }, name, (int)orig->script->params.size());
        f->setRaw("__wfcScript", Value(orig), DontEnum);   // GC root for the original (its scope chain is used)
        v = Value(f);
        return true;
    };
    if (name == "AssignDataStoreRead" && h == kAssignDataStoreReadHash) return bindDs(nativeAssignDataStoreRead);
    if (name == "UpdatePlayerEntry" && h == kUpdatePlayerEntryHash) return bindDs(nativeUpdatePlayerEntry);
    if (name == "BuildPlayerList" && h == kBuildPlayerListHash) return bindDs(nativeBuildPlayerList);
    if (name == "UpdatePlayerListData" && h == kUpdatePlayerListDataHash) return bindDs(nativeUpdatePlayerListData);
    if (name == "ReadCollectionValue" && h == kReadCollectionValueHash)
        return bindDs([](VM& m, Object* o, const Value& self, Args& a) { return nativeReadCollection(m, o, self, a, "DataStores.ReadCollectionValue"); });
    if (name == "ReadCollectionBoolValue" && h == kReadCollectionBoolValueHash)
        return bindDs([](VM& m, Object* o, const Value& self, Args& a) { return nativeReadCollection(m, o, self, a, "DataStores.ReadCollectionBoolValue"); });
    if (name == "addInterp" && kAddInterpHash && h == kAddInterpHash) {
        vm.global->setRaw("__wfcScript_" + name, v, DontEnum);
        Object* orig = v.o;
        v = Value(vm.newFunction([orig](VM& m, const Value& self, Args& args) {
            static const bool verify = std::getenv("WFC_INTERPVERIFY") != nullptr;
            if (verify) return m.call(Value(orig), self, args);
            return nativeAddInterp(m, orig, self, args);
        }, name, 6));
        return true;
    }
    return false;
}

}  // namespace gfx::avm1
