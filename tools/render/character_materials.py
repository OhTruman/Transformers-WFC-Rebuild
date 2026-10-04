"""Character (chassis) materials for the render data, from the AssetTools roster.

    python character_materials.py [--default]      -> prints material object paths, one per line

Source: AssetTools manifests/mp_content/mp_characters.json (33 chassis; robot / vehicle skeletal meshes with their
material slots resolved in the mesh package family). Selected: every chassis cooked into every MP map
(in_all_mp_maps), or with --default only the ones available by default (LockedChassis false). The renderer's
character path is material-driven (TnCharacterApplier runtime params for TR_ / WEP_ packages), so a roster change
needs no Optimus-specific code: the build compiles these materials for each map.
"""
import json, os, sys

ROSTER = r'F:/Transformers Rebuild/AssetTools/manifests/mp_content/mp_characters.json'


def materials(default_only=False):
    d = json.load(open(ROSTER, encoding='utf-8'))
    out = []
    for c in d['characters']:
        if not c.get('in_all_mp_maps'):
            continue
        if default_only and (c.get('locks') or {}).get('LockedChassis'):
            continue
        for form in ('robot', 'vehicle'):
            f = c.get(form) or {}
            for k, v in f.items():
                if not k.startswith('material_slots'):
                    continue
                for slot in v or []:
                    m = slot.get('material') if isinstance(slot, dict) else None
                    if m and m not in out:
                        out.append(m)
    return out


if __name__ == '__main__':
    print('\n'.join(materials('--default' in sys.argv)))
