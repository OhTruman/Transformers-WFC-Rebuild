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
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <limits>

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

}  // namespace

// Called when a script function is assigned to _global.<name>: the native port when the body is the known version.
bool nativeLibraryOverride(VM& vm, const std::string& name, Value& v) {
    static const bool off = std::getenv("WFC_NONATIVEINTERP") != nullptr;
    static const bool logHash = std::getenv("WFC_AVMHASH") != nullptr;   // DEV TOOL: log candidates' hashes
    if (off || !v.isObject() || !v.o->script) return false;
    if (name != "findInterpValue") return false;
    const uint64_t h = canonicalFunctionHash(*v.o->script, v.o->pool.get());
    if (logHash) LOG_INFO("avmhash %s %016llx", name.c_str(), (unsigned long long)h);
    if (name == "findInterpValue" && kFindInterpValueHash && h == kFindInterpValueHash) {
        vm.global->setRaw("__wfcScript_" + name, v, DontEnum);   // the script, for WFC_INTERPVERIFY (and rooted)
        v = Value(vm.newFunction(nativeFindInterpValue, name, 8));
        return true;
    }
    return false;
}

}  // namespace gfx::avm1
