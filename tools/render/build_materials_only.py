"""Materials-only regeneration: build_render_data.ps1 step 3 (materials_glsl.json with every extra material list)
for maps whose other render data already exists, without re-running lighting / umodel lightmap exports.

    python build_materials_only.py <render root with the existing map dirs> <out root> MAP [MAP ...]

<out root>/<MAP> receives bsp.glb / decals.glb copied from <render root>/<MAP> (build_materials reads their material
lists) and build_materials.py's outputs (materials_glsl.json, slot_materials.json, material_instance_actors.json,
missing textures under tex/). <out root> may equal <render root> (in-place regeneration of the materials only).
"""
import os, sys, shutil, subprocess

H = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def lines(args):
    r = subprocess.run([PY] + args, capture_output=True, text=True, check=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip()]


def main():
    src_root, out_root, maps = sys.argv[1], sys.argv[2], sys.argv[3:]
    fx = [l.strip() for l in open(os.path.join(H, 'fx_materials.txt')) if l.strip()]
    ui = [l.strip() for l in open(os.path.join(H, 'ui_materials.txt')) if l.strip()]
    # Item 6 (Systems): the weapon / prop materials of dynamic meshes no map world references (SYSTEMS_MATC58_AUDIT, MP first)
    sx = [l.split('#')[0].strip() for l in open(os.path.join(H, 'systems_extra_materials.txt'), encoding='utf-8')]
    sx = [l for l in sx if l]
    chars = lines([os.path.join(H, 'character_materials.py')])
    weapons = lines([os.path.join(H, 'weapon_materials.py')])
    defrag = lines([os.path.join(H, 'energy_death_materials.py'), '--list'])
    abil = lines([os.path.join(H, 'ability_render_assets.py'), '--materials'])
    for m in maps:
        out = os.path.join(out_root, m)
        os.makedirs(out, exist_ok=True)
        if os.path.abspath(out) != os.path.abspath(os.path.join(src_root, m)):
            for g in ('bsp.glb', 'decals.glb'):
                s = os.path.join(src_root, m, g)
                if os.path.exists(s): shutil.copyfile(s, os.path.join(out, g))
        scene = lines([os.path.join(H, 'scene_materials.py'), m])
        trans = lines([os.path.join(H, 'transforming_materials.py'), m])   # (the MP doors / bridges)
        fxlib = [l for l in lines([os.path.join(H, 'build_map_fx.py'), '--list-materials', m]) if '.' in l and ' ' not in l]
        args = [os.path.join(H, 'build_materials.py'), m, out] + chars + ['WEP_IonBlaster_p.WEP_IonBlaster_MATINST'] + \
               weapons + fx + ui + scene + fxlib + defrag + abil + sx + trans
        print('== %s: %d extra materials' % (m, len(args) - 3), flush=True)
        subprocess.run([PY] + args, check=True)


if __name__ == '__main__':
    main()
