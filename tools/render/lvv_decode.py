"""Decoder for the WFC LightsVisibilitiesVolume blob, per the native recovery
(RE-Workspace notes/MILESTONE03_RENDERING_LIGHTVIS_SHADOW.md, ReverseEngineering b52dca9):

  u8 bHasOctree; Node(root); f32 center[3]; f32 rootHalf; u32 unk18; f32 finestHalf;
  i32 nLights + nLights * FGuid(4 x u32); i32 nCorners + nCorners * u16; i32 nSamples + nSamples * u8;
  pair pool: for each sample (count & 0x7F) x u32  (light = int16(rec >> 16), vis = (rec & 0xFFFF) / 65535)
  Node: 8 x i32 corner ids (low 16 bits; 0xFFFF none), i32 hasData, u8 hasChildren, [8 x Node]
All big-endian. Used by tools (validation) and mirrored in src/render/LightVisibilityVolume.cpp.
"""
import json
import os
import struct
import sys


class Node:
    __slots__ = ('corners', 'has_data', 'children')


def decode(b):
    o = 0
    has = b[o]; o += 1
    if not has:
        return None
    nodes = 0

    def node():
        nonlocal o, nodes
        n = Node()
        n.corners = [struct.unpack_from('>i', b, o + 4 * k)[0] & 0xFFFF for k in range(8)]; o += 32
        n.has_data = struct.unpack_from('>i', b, o)[0] != 0; o += 4
        hc = b[o]; o += 1
        nodes += 1
        n.children = [node() for _ in range(8)] if hc else None
        return n

    root = node()
    cx, cy, cz, half = struct.unpack_from('>4f', b, o); o += 16
    unk18 = struct.unpack_from('>I', b, o)[0]; o += 4
    finest = struct.unpack_from('>f', b, o)[0]; o += 4
    nl = struct.unpack_from('>i', b, o)[0]; o += 4
    guids = [list(struct.unpack_from('>4I', b, o + 16 * k)) for k in range(nl)]; o += 16 * nl
    nc = struct.unpack_from('>i', b, o)[0]; o += 4
    remap = list(struct.unpack_from('>%dH' % nc, b, o)); o += 2 * nc
    ns = struct.unpack_from('>i', b, o)[0]; o += 4
    counts = list(b[o:o + ns]); o += ns
    total = sum(c & 0x7F for c in counts)
    pairs_bytes = len(b) - o
    halved = False
    if total * 4 != pairs_bytes and sum((c >> 1) & 0x7F for c in counts) * 4 == pairs_bytes:
        counts = [c >> 1 for c in counts]; halved = True; total = sum(c & 0x7F for c in counts)
    pool = list(struct.unpack_from('>%dI' % (pairs_bytes // 4), b, o))
    starts = []
    acc = 0
    for c in counts:
        starts.append(acc); acc += c & 0x7F
    return {'root': root, 'nodes': nodes, 'center': (cx, cy, cz), 'half': half, 'unk18': unk18, 'finest': finest,
            'guids': guids, 'remap': remap, 'counts': counts, 'starts': starts, 'pool': pool,
            'pairs_total': total, 'pairs_bytes': pairs_bytes, 'halved': halved, 'head_bytes': o}


def validate(rd):
    L = json.load(open(os.path.join(rd, 'lighting.json')))
    v = L['light_visibility_volumes'][0]
    b = open(os.path.join(rd, v['file']), 'rb').read()
    d = decode(b)
    out = {'nodes': d['nodes'], 'center_ue': d['center'], 'root_half': d['half'], 'unk18': d['unk18'],
           'finest_half': d['finest'], 'lights': len(d['guids']), 'corners': len(d['remap']),
           'samples': len(d['counts']), 'pairs': d['pairs_total'], 'pair_bytes_ok': d['pairs_total'] * 4 == d['pairs_bytes'],
           'count_halving_needed': d['halved'], 'bit7_samples': sum(1 for c in d['counts'] if c & 0x80)}
    lg = {tuple(l['light_guid']): i for i, l in enumerate(L['lights'])}
    out['guids_bound'] = sum(1 for g in d['guids'] if tuple(g) in lg)
    out['guids_distinct'] = len({tuple(g) for g in d['guids']})
    asc = True
    for s, c in enumerate(d['counts']):
        idx = [(d['pool'][d['starts'][s] + k] >> 16) for k in range(c & 0x7F)]
        if any(idx[k] >= idx[k + 1] for k in range(len(idx) - 1)):
            asc = False; break
    out['indices_strictly_ascending'] = asc
    out['max_remap'] = max(d['remap']) if d['remap'] else -1
    return out


if __name__ == '__main__':
    print(json.dumps(validate(sys.argv[1]), indent=1))
