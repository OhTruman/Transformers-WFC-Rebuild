"""Read-only: print the named display tree of a Scaleform .gfx/.swf (PlaceObject2/3 names nested through DefineSprite).
usage: gfxtree.py movie.gfx [root-instance-name]
Each line: indent, instance name, (character id, depth), tx/ty in px of the frame-1 placement. Exports are listed as linkage names.
"""
import sys, struct
sys.path.insert(0, __import__('os').path.dirname(__file__))
import gfxpool


class Bits:
    def __init__(s, b, o): s.b, s.p = b, o * 8
    def u(s, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((s.b[s.p >> 3] >> (7 - (s.p & 7))) & 1); s.p += 1
        return v
    def sg(s, n):
        v = s.u(n); return v - (1 << n) if n and v & (1 << (n - 1)) else v
    def off(s): return (s.p + 7) >> 3


def matrix(b, o):
    r = Bits(b, o)
    if r.u(1): n = r.u(5); r.u(n); r.u(n)
    if r.u(1): n = r.u(5); r.u(n); r.u(n)
    n = r.u(5)
    return r.sg(n) / 20.0, r.sg(n) / 20.0, r.off()


def places(b, o, ln, code):
    """(depth, char id or None, name or None, (tx, ty) or None) for one PlaceObject2/3."""
    f = b[o]; p = o + 1
    f2 = 0
    if code == 70: f2 = b[p]; p += 1
    depth = struct.unpack_from('<H', b, p)[0]; p += 2
    if code == 70 and (f2 & 0x08 or (f2 & 0x10 and f & 0x02)):
        e = b.index(b'\0', p); p = e + 1          # class name
    cid = None
    if f & 0x02: cid = struct.unpack_from('<H', b, p)[0]; p += 2
    xy = None
    if f & 0x04: tx, ty, p = matrix(b, p); xy = (tx, ty)
    if f & 0x08:                                   # color transform (with alpha): skip
        r = Bits(b, p); ha, hm = r.u(1), r.u(1); n = r.u(4)
        if hm: [r.u(n) for _ in range(4)]
        if ha: [r.u(n) for _ in range(4)]
        p = r.off()
    if f & 0x10: p += 2                            # ratio
    name = None
    if f & 0x20: e = b.index(b'\0', p); name = b[p:e].decode('latin1')
    return depth, cid, name, xy


def sprites(b):
    """char id -> list of frame-1 placements; also top-level (id 0)."""
    out = {}
    def walk(off, end, sid):
        lst = out.setdefault(sid, []); frame = 0
        for code, o, ln in gfxpool.tags(b, off, end):
            if code == 1: frame += 1
            elif code in (26, 70) and frame == 0: lst.append(places(b, o, ln, code))
            elif code == 39: walk(o + 4, o + ln, struct.unpack_from('<H', b, o)[0])
    walk(8 + gfxpool.rect_len(b, 8) + 4, len(b), 0)
    return out


def dump(sp, sid, ind, seen):
    for depth, cid, name, xy in sp.get(sid, []):
        if cid is None: continue
        pos = '' if xy is None else ' @(%.1f, %.1f)' % xy
        print('%s%s  [char %d, depth %d]%s' % ('  ' * ind, name or '-', cid, depth, pos))
        if cid in sp and cid not in seen and ind < 12:
            dump(sp, cid, ind + 1, seen | {cid})


if __name__ == '__main__':
    b = gfxpool.load(sys.argv[1]); sp = sprites(b)
    if len(sys.argv) > 2:
        want = sys.argv[2]
        for sid, lst in sp.items():
            for depth, cid, name, xy in lst:
                if name == want and cid is not None:
                    print('%s [char %d] in sprite %d' % (name, cid, sid)); dump(sp, cid, 1, {cid})
    else:
        dump(sp, 0, 0, set())
