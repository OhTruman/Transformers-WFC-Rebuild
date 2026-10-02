"""Shared UE3 object access for the rendering extractors (read-only over the original cooked data).

Wraps AssetTools' ue3pkg/props readers (read-only reference) with a path index over a set of
packages and a few native-tail decoders recovered for WFC (licensee 144):
  - MaterialInstanceConstant static parameter set (static switches / component masks)
"""
import os, struct, sys

ASSETTOOLS = r'F:/Transformers Rebuild/AssetTools/scripts/wfc'
COOKED = r'F:/Transformers Rebuild/Game Dump/TransGame/CookedXenon'
CONTENT = r'F:/Transformers Rebuild/ExtractedAssets/content'
sys.path.insert(0, ASSETTOOLS)
import ue3pkg, props as propsmod  # noqa: E402


def tags_to_dict(tags):
    """Tagged property list -> dict. Static arrays (ArrayIndex) become {index: value}."""
    d = {}
    for t in tags or []:
        if 'index' in t or (t['name'] in d and isinstance(d.get(t['name']), dict) and '_static' in d[t['name']]):
            slot = d.setdefault(t['name'], {'_static': True})
            if not isinstance(slot, dict) or '_static' not in slot:
                slot = d[t['name']] = {'_static': True, 0: slot}
            slot[t.get('index', 0)] = t['value']
        elif t['name'] in d and t['name'] in ('UnpackMin', 'UnpackMax'):
            pass
        else:
            d[t['name']] = t['value']
    return d


def struct_dict(v):
    """A decoded struct value (list of tags) -> dict; passthrough otherwise."""
    if isinstance(v, list) and v and isinstance(v[0], dict) and 'name' in v[0] and 'type' in v[0]:
        return tags_to_dict(v)
    return v


class Repo:
    def __init__(self, packages):
        self.pkgs = [ue3pkg.Package(os.path.join(COOKED, p)) for p in packages]
        self.readers = [propsmod.PropReader(p) for p in self.pkgs]
        self.index = {}
        for k, p in enumerate(self.pkgs):
            for i in range(len(p.exports)):
                path = p.object_path(i + 1).lower()
                e = p.exports[i]
                prev = self.index.get(path)
                # prefer the copy with the largest serialized body (full cooked copy)
                if prev is None or e['serial_size'] > self.pkgs[prev[0]].exports[prev[1] - 1]['serial_size']:
                    self.index[path] = (k, i + 1)
        self._cache = {}

    def find(self, path):
        return self.index.get((path or '').lower())

    def cls(self, path):
        h = self.find(path)
        if not h: return None
        p = self.pkgs[h[0]]
        return p.class_name(p.exports[h[1] - 1])

    def tags(self, path):
        h = self.find(path)
        if not h: return None
        if path.lower() not in self._cache:
            try:
                t, used = self.readers[h[0]].read_object(h[1])
            except Exception:
                t, used = None, 0
            self._cache[path.lower()] = (t, used)
        return self._cache[path.lower()][0]

    def obj(self, path):
        t = self.tags(path)
        return tags_to_dict(t) if t is not None else None

    def native_tail(self, path):
        h = self.find(path)
        self.tags(path)
        used = self._cache[path.lower()][1]
        p = self.pkgs[h[0]]
        e = p.exports[h[1] - 1]
        return p, p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]

    # ------------------------------------------------------------------ MIC static params
    def switch_names(self):
        """ParameterName of every StaticSwitchParameter expression in the loaded packages."""
        if getattr(self, '_switch_names', None) is None:
            out = set()
            for k, (pi, ix) in self.index.items():
                pk = self.pkgs[pi]
                if pk.class_name(pk.exports[ix - 1]) == 'MaterialExpressionStaticSwitchParameter':
                    nm = (self.obj(pk.object_path(ix)) or {}).get('ParameterName')
                    if nm: out.add(nm)
            self._switch_names = out
        return self._switch_names

    def mic_static_params(self, path):
        """Decode the static parameter set that follows the compiled static-permutation resource
        in a MaterialInstanceConstant's native tail. Returns ({switch: bool}, {mask: (r,g,b,a)}).

        Layout located empirically: an int32 count N followed by N x
        [FName(8) Value(u32) bOverride(u32) FGuid(16)] whose names all resolve, then the
        component-mask array [FName R G B A bOverride FGuid]. We scan for the first position
        where such an array parses cleanly and names are StaticSwitchParameter names."""
        p, t = self.native_tail(path)
        names = p.names
        valid = self.switch_names()
        best = None
        for o in range(0, len(t) - 4, 4):
            n = struct.unpack_from('>i', t, o)[0]
            if not (1 <= n <= 64) or o + 4 + n * 32 > len(t): continue
            ok = True
            sw = {}
            for k in range(n):
                b = o + 4 + 32 * k
                ni, nn, val, ovr = struct.unpack_from('>iiII', t, b)
                if not (0 <= ni < len(names)) or nn != 0 or val > 1 or ovr > 1:
                    ok = False; break
                if names[ni] not in valid:          # must be a real StaticSwitchParameter name
                    ok = False; break
                sw[names[ni]] = (bool(val), bool(ovr))
            if not ok: continue
            # following component-mask array
            m = o + 4 + 32 * n
            masks = {}
            if m + 4 <= len(t):
                nm = struct.unpack_from('>i', t, m)[0]
                if 0 <= nm <= 64 and m + 4 + nm * 44 <= len(t):
                    for k in range(nm):
                        b = m + 4 + 44 * k
                        ni, nn, r, g, bb, a, ovr = struct.unpack_from('>iiIIIII', t, b)
                        if 0 <= ni < len(names):
                            masks[names[ni]] = ((r, g, bb, a), bool(ovr))
            best = (sw, masks)
            break
        return best or ({}, {})


def linear_from_srgb_byte(c):
    """UE3 FLinearColor(FColor): sRGB byte -> linear (pow 2.2 table)."""
    return (c / 255.0) ** 2.2
