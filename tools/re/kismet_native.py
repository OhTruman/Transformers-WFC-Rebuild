"""Read-only: locate the native vtable and the Kismet op hooks of UE3 classes in default.xex.
Slots: +0x170 = per-class Activated (differs across SetBool / SetInt / Switch / Log ...), +0x16C = Update (latent), +0x17C = a
  second op hook (overridden only by latent / multi-input ops such as SeqAct_Conversation 0x827782F0). Chain:
  wide string "U<Class>\\0" in .rdata -> the class registration function that materialises its address (UClass ctor call)
  -> the InternalConstructor pointer it stores at 0x54(r1) -> the vtable address(es) materialised in that constructor
  (or one call deeper) -> slots.
Usage: python kismet_native.py <Class> [...]   (prints vtable, Activated / Update / DeActivated-candidates, raw slot dump with -v)"""
import sys, struct, bisect
sys.dont_write_bytecode = True
from xeximg import Image

img = Image()
t0, _ = img.range("_text"); _, _, text, _ = img.block("_text")
W = struct.unpack(">%dI" % (len(text) // 4), text[:len(text) // 4 * 4])
_, rbase, rdata, *_ = img.block("_rdata")
_, _, pdata, _ = img.block("_pdata")
FS = sorted((struct.unpack_from(">I", pdata, i)[0], ((struct.unpack_from(">I", pdata, i + 4)[0] >> 8) & 0x3FFFFF) * 4)
            for i in range(0, len(pdata) - 7, 8))
STARTS = [a for a, _ in FS]
SLOT = {"Activated": 0x170, "Update": 0x16C, "Hook17C": 0x17C}


def func_of(a):
    i = bisect.bisect_right(STARTS, a) - 1
    return FS[i] if i >= 0 else (0, 0)


def materialised(lo_a, hi_a, base=None, n=None):
    """Yield (insn_addr, value) for lis rX,hi ; (addi|subi|ori) rY,rX,lo pairs inside [lo_a, hi_a)."""
    s, e = (lo_a - t0) // 4, (hi_a - t0) // 4
    for i in range(s, e):
        ins = W[i]
        if ins >> 26 != 15 or ((ins >> 16) & 31) != 0: continue
        rd = (ins >> 21) & 31; hi = (ins & 0xFFFF) << 16
        for j in range(i + 1, min(i + 16, e)):
            x = W[j]; op = x >> 26
            if op in (14, 24) and ((x >> 16) & 31) == rd:
                lo = x & 0xFFFF
                if op == 14 and lo & 0x8000: lo -= 0x10000
                yield t0 + 4 * j, (hi + lo) & 0xFFFFFFFF, (x >> 21) & 31
                break


def u32(a):
    for name, base, data, *_ in img.blocks:
        if base <= a < base + len(data) - 3: return struct.unpack_from(">I", data, a - base)[0]
    return None


def is_code(v): return v is not None and t0 <= v < t0 + len(text)


def reg_function(cls):
    s = ("U" + cls + "\0").encode("utf-16-be")
    k = rdata.find(s)
    hits = []
    while k != -1:
        a = rbase + k
        if a % 2 == 0: hits.append(a)
        k = rdata.find(s, k + 1)
    return hits


_INDEX = None


def code_refs(target_lo, target_hi):
    global _INDEX
    if _INDEX is None:
        _INDEX = {}
        for ia, v, rd in materialised(t0, t0 + len(text)):
            _INDEX.setdefault(v, []).append(ia)
    return [(v, ia) for v in range(target_lo, target_hi) for ia in _INDEX.get(v, [])]


def find(cls, verbose=False):
    out = {"class": cls}
    names = reg_function(cls)
    if not names: out["error"] = "no U<Class> string"; return out
    regs = set()
    for a in names:
        for v, ia in code_refs(a, a + 4): regs.add(func_of(ia))
    out["registration"] = [hex(f[0]) for f in regs]
    ctors = set()
    for f, n in regs:
        # InternalConstructor = a code pointer materialised in the registration function and stored to the stack
        for ia, v, rd in materialised(f, f + n):
            if is_code(v) and func_of(v)[0] == v: ctors.add(v)
    out["ctor_candidates"] = [hex(c) for c in sorted(ctors)]
    vts = []
    for c in sorted(ctors):
        fa, fn = func_of(c)
        cands = [(ia, v) for ia, v, rd in materialised(fa, fa + fn) if rbase <= v < rbase + len(rdata)]
        # one call deeper (InternalConstructor -> C++ constructor)
        for i in range((fa - t0) // 4, (fa + fn - t0) // 4):
            x = W[i]
            if x >> 26 == 18 and x & 1:
                off = x & 0x3FFFFFC
                if off & 0x2000000: off -= 0x4000000
                tgt = t0 + 4 * i + off
                ga, gn = func_of(tgt)
                if ga == tgt and gn < 0x400:
                    cands += [(ia, v) for ia, v, rd in materialised(ga, ga + gn) if rbase <= v < rbase + len(rdata)]
        for ia, v in cands:
            if is_code(u32(v)) and is_code(u32(v + SLOT["Activated"])) and v not in [x[0] for x in vts]:
                vts.append((v, ia))
    out["vtables"] = []
    for v, ia in vts:
        e = {"vtable": hex(v), "ref": hex(ia)}
        for k, off in SLOT.items(): e[k] = hex(u32(v + off))
        if verbose: e["slots"] = [hex(u32(v + 4 * i) or 0) for i in range(0, 110)]
        out["vtables"].append(e)
    return out


if __name__ == "__main__":
    import json
    v = "-v" in sys.argv
    for c in [a for a in sys.argv[1:] if a != "-v"]:
        print(json.dumps(find(c, v)))
