"""Energy-death ("Defrag") materials of every MP chassis form (RE pass 5 s12 addenda 24 / 26).

TnFormBlueprint.EnergyDeathMaterial is the MaterialInstanceConstant (parent TR_AllShader_p.Release.TR_Defrag_MAT)
swapped onto the form's mesh at an energy death; its scalar 'Defrag' runs 1 -> 0. The renderer sees a body's
material packages, so the table is keyed by the package of the form's SkeletalMesh (two forms borrow another
chassis' instance: Scattershot -> Warpath, Onslaught -> Ironhide). Defrag instances that no scanned blueprint names
are keyed by their own package.

Usage: energy_death_materials.py            writes tools/render/energy_death_materials.json from all MP maps
       energy_death_materials.py --list     prints the material paths (build_render_data material list)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages, VS_MAPS  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'energy_death_materials.json')


def scan():
    mics, by_pkg = {}, {}
    maps = sorted(m for m in os.listdir(VS_MAPS) if m.startswith('MP_'))
    for m in maps:
        for pk in map_packages(m)[0]:
            r = Repo([pk])
            p = r.pkgs[0]
            for i, e in enumerate(p.exports):
                cn = p.class_name(e)
                path = p.object_path(i + 1)
                if cn == 'MaterialInstanceConstant' and 'defrag' in path.lower():
                    par = ((r.obj(path) or {}).get('Parent') or {}).get('ref', '')
                    if par.lower().endswith('tr_defrag_mat'):
                        mics[path.lower()] = path
                elif cn.endswith('FormBlueprint'):
                    o = r.obj(path) or {}
                    ed = (o.get('EnergyDeathMaterial') or {}).get('ref')
                    sk = (o.get('SkeletalMesh') or {}).get('ref')
                    if ed and sk:
                        by_pkg[sk.split('.')[0].lower()] = ed
    for path in mics.values():                       # unnamed by a scanned blueprint: its own package
        by_pkg.setdefault(path.split('.')[0].lower(), path)
    return sorted(mics.values()), dict(sorted(by_pkg.items()))


if __name__ == '__main__':
    if '--list' in sys.argv:
        d = json.load(open(OUT, encoding='utf-8'))
        print('\n'.join(d['materials']))
    else:
        mats, by_pkg = scan()
        json.dump({'generated_by': 'tools/render/energy_death_materials.py', 'materials': mats, 'by_package': by_pkg},
                  open(OUT, 'w', encoding='utf-8', newline='\n'), indent=1)
        print('energy death materials: %d, packages: %d' % (len(mats), len(by_pkg)))
