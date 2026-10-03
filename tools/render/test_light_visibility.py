"""Regression test for the exported WFC LightsVisibilitiesVolume blob (structure facts only).

Usage: python test_light_visibility.py <work/render/<Map>>
Checks the confirmed facts the runtime relies on (render/LightVisibilityVolume.*) so a re-export or a
future native decoder cannot silently change them:
  * lighting.json lists the volume, its blob file exists with the recorded size
  * the blob ends in a (u16 light index, u16 visibility) pair stream (light < number of level lights)
  * every visibility is k/20 of 0xFFFF
  * the head starts with an octree node whose eight big-endian child indices are 0..7
When ReVa's layout lands, extend with cell lookups against known positions (e.g. under a lamp = 1.0).
"""
import json
import os
import struct
import sys


def main():
    rd = sys.argv[1]
    L = json.load(open(os.path.join(rd, 'lighting.json')))
    vols = L.get('light_visibility_volumes') or []
    assert vols, 'no light_visibility_volumes in lighting.json'
    nlights = len(L['lights'])
    for v in vols:
        b = open(os.path.join(rd, v['file']), 'rb').read()
        assert len(b) == v['bytes'], 'size mismatch %d vs %d' % (len(b), v['bytes'])
        o = len(b) - len(b) % 4
        while o >= 4 and struct.unpack_from('>H', b, o - 4)[0] < 300:
            o -= 4
        pairs = [struct.unpack_from('>HH', b, k) for k in range(o, len(b) - 3, 4)]
        assert pairs, 'no pair stream'
        assert max(p[0] for p in pairs) < nlights, 'light index beyond the level light list'
        for _, vis in pairs:
            step = round(vis * 20 / 0xFFFF)
            assert abs(vis - step * 0xFFFF // 20) <= 1, 'visibility 0x%04x not k/20' % vis
        kids = struct.unpack_from('>8i', b, 1)
        assert list(kids) == list(range(8)), 'root children %s' % (kids,)
        print('OK %s: %d bytes, %d pairs from %d, max light %d, root children 0..7'
              % (v['file'], len(b), len(pairs), o, max(p[0] for p in pairs)))


if __name__ == '__main__':
    main()
