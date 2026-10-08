// Clean-room reconstruction — ActionScript 2 virtual machine (AVM1, SWF 5-8 action model) for the shipped WFC
// Scaleform movies. It executes the movies' own bytecode (DoAction / DoInitAction / clip events / functions);
// nothing in the menus is re-implemented natively. Semantics follow the SWF 8 action model (SWF >= 7: case
// sensitive, undefined -> NaN in arithmetic, "" -> NaN).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx {
class DisplayObject;
class Player;
}

namespace gfx::avm1 {

class Object;
class VM;

enum class VType : uint8_t { Undefined, Null, Bool, Number, String, Object };

struct Value {
    VType t = VType::Undefined;
    bool b = false;
    uint32_t atom = 0;   // a constant-pool string's property-name atom (0: none; set by Push, carried by copies)
    double n = 0.0;
    std::string s;
    Object* o = nullptr;

    Value() = default;
    static Value undef() { return Value(); }
    static Value null() { Value v; v.t = VType::Null; return v; }
    Value(bool v) : t(VType::Bool), b(v) {}
    Value(double v) : t(VType::Number), n(v) {}
    Value(int v) : t(VType::Number), n(v) {}
    Value(const char* v) : t(VType::String), s(v) {}
    Value(const std::string& v) : t(VType::String), s(v) {}
    Value(Object* v) : t(v ? VType::Object : VType::Null), o(v) {}

    bool isUndef() const { return t == VType::Undefined; }
    bool isNull() const { return t == VType::Null; }
    bool isNullish() const { return t == VType::Undefined || t == VType::Null; }
    bool isObject() const { return t == VType::Object && o; }
    bool isString() const { return t == VType::String; }
    bool isNumber() const { return t == VType::Number; }
};

using Args = std::vector<Value>;
using NativeFn = std::function<Value(VM& vm, const Value& self, Args& args)>;

enum PropFlag : uint8_t { DontEnum = 1, DontDelete = 2, ReadOnly = 4 };

struct Property {
    Value v;
    Object* getter = nullptr;
    Object* setter = nullptr;
    uint8_t flags = 0;
};

enum class ObjKind : uint8_t { Plain, Function, Array, Clip, TextField, Super, Boxed, Native };

// Script function body (DefineFunction / DefineFunction2).
struct ScriptCode {
    std::shared_ptr<std::vector<uint8_t>> code;
    size_t start = 0, length = 0;
    bool v2 = false;
    uint8_t regCount = 0;
    uint16_t flags = 0;                     // DefineFunction2 preload/suppress flags
    std::vector<std::pair<uint8_t, std::string>> params;   // (register or 0, name)
    std::string name;
    mutable int8_t activationFree = -1;     // VM::call: -1 unknown, 1 = the activation object can never be observed
};

// Property names as atoms: every name stored as a property is interned once (process-wide, main thread); property
// indexes hash the small integer, and a lookup walking a prototype chain resolves the name once instead of hashing the
// string again at every level. atomFind returns 0 for a name never interned - no object can have such a property.
struct ScriptCode;
class VM;
struct Value;
// A constant pool with each string's atom (computed once when the ConstantPool action runs).
struct ConstPool : std::vector<std::string> { std::vector<uint32_t> atoms; };
// Native ports of the shared ActionScript library (Avm1NativeLib.cpp): replaces v when it is the known body.
bool nativeLibraryOverride(VM& vm, const std::string& name, Value& v);
uint64_t canonicalFunctionHash(const ScriptCode& sc, const std::vector<std::string>* pool);
uint32_t atomIntern(const std::string& s);
uint32_t atomFind(const std::string& s);

class Object {
public:
    explicit Object(ObjKind k = ObjKind::Plain) : kind(k) {}
    virtual ~Object() = default;

    ObjKind kind;
    Object* proto = nullptr;                 // __proto__
    std::vector<std::pair<std::string, Property>> props;
    // Lookup by atom: slotAtoms[i] is props[i]'s atom. Small objects (most: rows, cells, tween entries, transforms) are
    // scanned linearly; the hash index is built only past kLinearProps (a hash node per property was the main allocation
    // cost of building the PlayerList). props alone keeps the order (enumeration).
    static constexpr size_t kLinearProps = 8;
    std::vector<uint32_t> slotAtoms;
    std::unordered_map<uint32_t, uint32_t> index;   // atom -> props slot (only when props.size() > kLinearProps)
    bool marked = false;
    bool zombie = false;                     // diagnostics (WFC_GFX_GCCHECK): collected but kept to catch later use
    std::string className;                   // diagnostics / typeof ("movieclip")

    // Function data (kind == Function).
    NativeFn native;
    std::shared_ptr<ScriptCode> script;
    std::vector<Object*> scope;              // captured scope chain (innermost last)
    std::shared_ptr<ConstPool> pool;         // constant pool active at definition
    gfx::DisplayObject* defTarget = nullptr; // timeline the function was defined in (for _root/_parent preload)
    bool isConstructorOnly = false;

    // Array data (kind == Array).
    std::vector<Value> elems;

    // Clip / TextField binding.
    gfx::DisplayObject* display = nullptr;

    // Super object (kind == Super).
    Object* superThis = nullptr;
    Object* superProto = nullptr;

    // Boxed primitive (kind == Boxed): String / Number / Boolean objects, Date (n = ms).
    Value boxed;

    // Native-side payload (ColorTransform, Matrix, BitmapData, MovieClipLoader ...).
    std::string nativeType;
    std::shared_ptr<void> payload;

    // Watchpoints (Object.watch).
    std::map<std::string, std::pair<Object*, Value>> watches;

    int slotOf(uint32_t a) const {
        if (!a) return -1;
        if (!index.empty()) { auto it = index.find(a); return it == index.end() ? -1 : (int)it->second; }
        for (size_t i = 0; i < slotAtoms.size(); ++i) if (slotAtoms[i] == a) return (int)i;
        return -1;
    }
    Property* findOwnA(uint32_t a) { const int i = slotOf(a); return i < 0 ? nullptr : &props[(size_t)i].second; }
    const Property* findOwnA(uint32_t a) const { const int i = slotOf(a); return i < 0 ? nullptr : &props[(size_t)i].second; }
    Property* findOwn(const std::string& k) { return props.empty() ? nullptr : findOwnA(atomFind(k)); }
    const Property* findOwn(const std::string& k) const { return props.empty() ? nullptr : findOwnA(atomFind(k)); }
    Property& own(const std::string& k) { return ownA(atomIntern(k), k); }
    Property& ownA(uint32_t a, const std::string& k) {   // a = atomIntern(k)
        const int i = slotOf(a);
        if (i >= 0) return props[(size_t)i].second;
        if (props.empty()) { props.reserve(4); slotAtoms.reserve(4); }
        props.push_back({k, Property{}});
        slotAtoms.push_back(a);
        if (props.size() > kLinearProps) {
            if (index.empty()) for (size_t j = 0; j < slotAtoms.size(); ++j) index[slotAtoms[j]] = (uint32_t)j;
            else index[a] = (uint32_t)(props.size() - 1);
        }
        return props.back().second;
    }
    void clearProps() { props.clear(); slotAtoms.clear(); index.clear(); }
    bool removeOwn(const std::string& k);
    void setRaw(const std::string& k, const Value& v, uint8_t flags = 0) { setRawA(atomIntern(k), k, v, flags); }
    void setRawA(uint32_t a, const std::string& k, const Value& v, uint8_t flags = 0) { Property& p = ownA(a, k); p.v = v; p.flags = flags; p.getter = p.setter = nullptr; }
};

// Exceptions thrown by AS "throw" (Value) cross native frames as this type.
struct ScriptThrow { Value v; };

class VM {
public:
    explicit VM(gfx::Player* player);
    ~VM();

    gfx::Player* player() const { return player_; }

    // ---- heap ----
    Object* newObject(Object* proto = nullptr);
    Object* newPlain();                                   // proto = Object.prototype
    Object* newArray(const std::vector<Value>& elems = {});
    Object* newFunction(NativeFn fn, const std::string& name = "", int length = 0);
    Object* newScriptFunction(const std::shared_ptr<ScriptCode>& code, const std::vector<Object*>& scope,
                              const std::shared_ptr<ConstPool>& pool, gfx::DisplayObject* target);
    Object* newClipObject(gfx::DisplayObject* d, Object* proto);
    size_t heapSize() const { return heap_.size(); }
    void collect(const std::vector<Object*>& extraRoots);   // mark-sweep from globals + extra roots

    // ---- conversions ----
    double toNumber(const Value& v);
    std::string toString(const Value& v);
    bool toBool(const Value& v) const;
    Value toPrimitive(const Value& v, bool hintString = false);
    Object* toObject(const Value& v);                      // boxes primitives (property access on them)
    int32_t toInt32(const Value& v) { return toInt32(toNumber(v)); }
    static int32_t toInt32(double d);
    static std::string numberToString(double d, int radix = 10);
    std::string typeOf(const Value& v) const;
    bool looseEquals(const Value& a, const Value& b);
    bool strictEquals(const Value& a, const Value& b) const;
    Value lessThan(const Value& a, const Value& b);       // true / false / undefined
    bool instanceOf(const Value& v, Object* ctor);

    // ---- properties ----
    Value get(Object* o, const std::string& key) { return get(o, key, 0u); }   // with __proto__ chain, getters, clip/textfield virtuals
    Value get(Object* o, const std::string& key, uint32_t atomHint);           // atomHint: key's atom when known (0: look up)
    Value getV(const Value& base, const std::string& key) { return getV(base, key, 0u); }
    Value getV(const Value& base, const std::string& key, uint32_t atomHint);
    void set(Object* o, const std::string& key, const Value& v) { set(o, key, v, 0u); }
    void set(Object* o, const std::string& key, const Value& v, uint32_t atomHint);
    void setV(const Value& base, const std::string& key, const Value& v);
    bool has(Object* o, const std::string& key) { return has(o, key, 0u); }
    bool has(Object* o, const std::string& key, uint32_t atomHint);          // atomHint: key's atom when known (0: look up)
    bool deleteProp(Object* o, const std::string& key);
    Object* findOwner(Object* o, const std::string& key);  // object in the chain that owns key
    Object* findOwnerA(Object* o, uint32_t atom);           // the same by the key's atom (0: none)
    std::vector<std::string> enumerate(Object* o);

    // ---- calls ----
    Value call(const Value& fn, const Value& self, Args& args, Object* superProto = nullptr);
    Value callMethod(const Value& base, const std::string& name, Args args) { return callMethod(base, name, std::move(args), 0u); }
    Value callMethod(const Value& base, const std::string& name, Args args, uint32_t atomHint);
    Value construct(Object* ctor, Args& args);
    // Run an action block on a timeline (DoAction / DoInitAction / clip event body).
    void runBlock(const std::shared_ptr<std::vector<uint8_t>>& code, size_t start, size_t len, gfx::DisplayObject* target,
                  Object* thisObj = nullptr);
    // Engine -> AS: call "path.to.function" (dot path from _root / _global) with arguments.
    Value invokePath(const std::string& path, Args args, gfx::DisplayObject* root);

    // ---- well-known objects ----
    Object* global = nullptr;
    Object* objectProto = nullptr;
    Object* functionProto = nullptr;
    Object* arrayProto = nullptr;
    Object* stringProto = nullptr;
    Object* numberProto = nullptr;
    Object* booleanProto = nullptr;
    Object* movieClipProto = nullptr;
    Object* textFieldProto = nullptr;
    Object* textFormatCtor = nullptr;
    Object* dateProto = nullptr;
    Object* errorProto = nullptr;
    Object* objectCtor = nullptr;
    Object* arrayCtor = nullptr;
    Object* keyObj = nullptr;
    Object* stageObj = nullptr;
    Object* mouseObj = nullptr;

    // Linkage name -> registered AS2 class constructor (Object.registerClass), per movie definition.
    std::map<std::string, Object*> registeredClasses;

    int swfVersion = 8;
    // $version: HmUtility.Platform reads the prefix (XBOX360 / PS3 / WIN) and the movies branch on it. Set by the
    // host before players are created (defaultVersionString).
    static std::string defaultVersionString;
    std::string versionString = defaultVersionString;
    bool traceEnabled = true;
    // The timeline whose code is running (DoAction / clip event / function target); Selection.setFocus resolves
    // path strings against it.
    gfx::DisplayObject* currentTarget = nullptr;
    long long instructions = 0;

    // Callbacks into the host.
    std::function<Value(const std::string& name, Args& args)> externalCall;
    std::function<void(const std::string& cmd, const std::string& arg)> fsCommand;
    std::function<void(const std::string& msg)> traceSink;

private:
    friend struct Interp;
    void installBuiltins();
    void installDisplayBuiltins();
    void mark(Object* o);
    void markValue(const Value& v) { if (v.t == VType::Object && v.o) mark(v.o); }

    gfx::Player* player_;
    std::vector<std::unique_ptr<Object>> heap_;
    std::vector<std::unique_ptr<Object>> zombies_;   // WFC_GFX_GCCHECK
public:
    void zombieUse(Object* o, const char* op, const std::string& key);
private:
    int depth_ = 0;
};

} // namespace gfx::avm1
