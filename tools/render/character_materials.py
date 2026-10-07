"""Character (chassis) materials for the render data, from the AssetTools roster.

    python character_materials.py [--default]      -> prints material object paths, one per line

Source: AssetTools manifests/mp_content/mp_characters.json (33 chassis; robot / vehicle skeletal meshes with their
material slots resolved in the mesh package family). Selected: every chassis cooked into every MP map
(in_all_mp_maps) plus the six extra bodies shipped as XP unlocks (Gameplay 1ca3bf5 / Frontend 34b2f50, user
decision, PC ADAPTATION: cooked into no MP map; build_materials adds their standalone packages as fallbacks), or with
--default only the ones available by default (LockedChassis false). The renderer's
character path is material-driven (TnCharacterApplier runtime params for TR_ / WEP_ packages), so a roster change
needs no Optimus-specific code: the build compiles these materials for each map.
"""
import json, os, sys

ROSTER = r'F:/Transformers Rebuild/AssetTools/manifests/mp_content/mp_characters.json'


def materials(default_only=False):
    d = json.load(open(ROSTER, encoding='utf-8'))
    out = []
    for c in d['characters']:
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


def cooked_copies(material):
    """Cooked packages (roster cooked_copies, AssetTools) that carry a chassis material's objects: the six extra
    bodies' own TR_* packages are seekfree stubs, their materials are cooked into campaign map packages."""
    pkg = material.split('.')[0].lower()
    d = json.load(open(ROSTER, encoding='utf-8'))
    for c in d['characters']:
        dirs = [x.split('/')[-1].lower() for x in c.get('extracted_package_dirs') or []]
        if pkg in dirs:
            # the packages holding the full robot / vehicle mesh exports (AssetTools asset_cooked_copies) first,
            # then the definition copies (cooked_copies)
            assets = next((v for k, v in c.items() if k.startswith('asset_cooked_copies')), None) or {}
            copies = []
            for p in (assets.get('robot') or []) + (assets.get('vehicle') or []) +                     ((c.get('cooked_copies') or {}).get('packages') or []):
                if p + '.xxx' not in copies: copies.append(p + '.xxx')
            # then those maps' persistent levels (Laserbeak's VH_ material is cooked into A2_KON_BASE_m, not the
            # roster's A2_KON_Soundwave_Design_m, which only carries its definitions)
            bases = []
            for cp in copies:
                b = '_'.join(cp.split('_')[:2]) + '_BASE_m.xxx'
                if b not in bases: bases.append(b)
            return copies + bases
    return []


if __name__ == '__main__':
    print('\n'.join(materials('--default' in sys.argv)))
