"""Beast light-probe grids for the character light environment (RE pass: MILESTONE03 lightvis addendum 2, RE 13fd546).

BeastLightEnvironmentVolume (enabled, RenderSource -> BeastLightEnvironmentBox): the box's native tail is
int32 BE count = PointsX * PointsY * PointsZ, then count x 27 BE floats (R[9], G[9], B[9] planar, stock UE3 L2 SH
order); point index = (z * PY + y) * PX + x. The volume maps world -> local by local = (P - Location) / DrawScale3D
(the box m_worldToLocal: diagonal 1 / DrawScale3D, translation -Location / DrawScale3D; no rotation in the MP data).
Only MP_ORB (Orbital Debris) has baked boxes; Berth / Gorge / Complex volumes have no RenderSource (skipped, as the
original does).

Usage: beast_probes.py <map> <out_dir>   writes <out_dir>/beast_probes.json ({"volumes": [...]}, empty when none)
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages  # noqa: E402


def _ref(v):
    return (v or {}).get('ref') if isinstance(v, dict) else None


def build(mapname):
    repo = Repo(map_packages(mapname)[0])
    vols = []
    for k in repo.index:
        if repo.cls(k) != 'BeastLightEnvironmentVolume':
            continue
        o = repo.obj(k) or {}
        if str(o.get('bEnabled', True)) in ('False', 'false'):
            continue
        box = _ref(o.get('RenderSource'))
        if not box or not repo.find(box):
            continue                                   # no baked box: the original skips the volume
        h = repo.find(box)
        p = repo.pkgs[h[0]]
        e = p.exports[h[1] - 1]
        props, used = repo.readers[h[0]].read_object(h[1])
        bo = repo.obj(box) or {}
        px, py, pz = int(bo['PointsX']), int(bo['PointsY']), int(bo['PointsZ'])
        nat = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        n = struct.unpack_from('>i', nat, 0)[0]
        if n != px * py * pz or len(nat) < 4 + n * 108:
            print('beast probes: %s: %d points vs %dx%dx%d, %d bytes - skipped' % (box, n, px, py, pz, len(nat)))
            continue
        sh = list(struct.unpack_from('>%df' % (n * 27), nat, 4))
        loc = [float(v) for v in (o.get('Location') or [0, 0, 0])]
        ds = float(o.get('DrawScale') or 1.0)
        s3 = [float(v) * ds for v in (o.get('DrawScale3D') or [1, 1, 1])]
        vols.append({'volume': k, 'box': box, 'priority': int(o.get('Priority') or 0), 'location': loc, 'scale': s3,
                     'points': [px, py, pz], 'sh': [round(v, 7) for v in sh]})
    return vols


if __name__ == '__main__':
    mapname, out = sys.argv[1], sys.argv[2]
    vols = build(mapname)
    json.dump({'generated_by': 'tools/render/beast_probes.py', 'volumes': vols},
              open(os.path.join(out, 'beast_probes.json'), 'w'))
    print('beast probes: %d volume(s) %s' % (len(vols), ', '.join('%s %dx%dx%d' % (v['box'].split('.')[-1], *v['points'])
                                                                  for v in vols)))
