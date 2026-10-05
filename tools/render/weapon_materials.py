"""Weapon materials for the render data, from the AssetTools weapon export.

    python weapon_materials.py      -> prints material object paths, one per line

Every exported weapon (ExtractedAssets/VerticalSlice/Weapons/<id>/weapon*.json, AssetTools vs_weapon_export.py) lists
its mesh's material slots. M42: the render data compiled only weapon materials that map geometry happened to reference
(pickups, crates) plus the Ion Blaster, so every other held weapon (Sniper Rifle / Null Ray, Assault Rifle, ...) drew
its glTF fallback material (untextured). Like character_materials.py, the list is generic: only materials cooked into
the map compile, the rest are reported by build_materials.
"""
import glob
import json
import os

WEAPONS = r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Weapons'


def materials():
    out = []
    for f in sorted(glob.glob(os.path.join(WEAPONS, '*', 'weapon*.json'))):
        try:
            d = json.load(open(f, encoding='utf-8'))
        except Exception:
            continue
        for m in ((d.get('mesh') or {}).get('materials') or []):
            mat = m.get('material')
            if mat and mat not in out:
                out.append(mat)
    return out


if __name__ == '__main__':
    for m in materials():
        print(m)
