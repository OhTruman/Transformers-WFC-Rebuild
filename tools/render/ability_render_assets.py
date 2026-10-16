"""Ability / killstreak assets (Barrier, Sentry, Ammo Crate, Roller, Guided Missile ...) from AssetTools'
manifests/mp_content/ability_assets.json (a reference closure per ability: may over-include slightly).
Their particle systems join every map's effect library (build_map_fx) and their materials the compiled set
(build_render_data), so Gameplay can spawn / draw them on any map with original materials.

Usage: ability_render_assets.py --materials | --particles   prints object paths, one per line
"""
import json
import os
import sys

# AssetTools: WFC_ASSETTOOLS (a tools copy outside the worktree layout), else three levels up from tools/render. A missing
# manifest is an error - an empty result silently dropped the ability / turret FX systems from every map's FX build.
ASSETTOOLS = os.environ.get('WFC_ASSETTOOLS') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
                                                              'AssetTools')
MANIFEST = os.path.join(ASSETTOOLS, 'manifests', 'mp_content', 'ability_assets.json')


def _assets():
    if not os.path.exists(MANIFEST):
        raise SystemExit('ability_render_assets: %s not found (set WFC_ASSETTOOLS to the AssetTools folder)' % MANIFEST)
    return json.load(open(MANIFEST, encoding='utf-8')).get('abilities') or {}


def particles():
    return sorted({x['object'] for e in _assets().values() for x in (e.get('assets') or {}).get('ParticleSystem', [])})


def materials():
    return sorted({x['object'] for e in _assets().values() for k in ('Material', 'MaterialInstanceConstant')
                   for x in (e.get('assets') or {}).get(k, [])})


if __name__ == '__main__':
    print('\n'.join(particles() if '--particles' in sys.argv else materials()))
