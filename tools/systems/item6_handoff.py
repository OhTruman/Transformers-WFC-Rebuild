"""Item 6 handoff: per map, the patched build_materials outputs from Systems' render copy (argv[1]) re-pointed at the
integration render root (texture paths <argv[1]>/<map>/ -> F:/Transformers Rebuild/Rebuild/work/render/<map>/), plus the
textures the build exported (tex/), into argv[2]/<map>. Then a check against the CURRENT integration data per map: every
existing material byte-identical to Rebuild/work/render's, the new keys listed. Writes argv[2]/MANIFEST.json. argv[3:] = maps."""
import json, os, shutil, sys
src_root, out_root, maps = sys.argv[1].replace('\\', '/').rstrip('/'), sys.argv[2], sys.argv[3:]
INT = 'F:/Transformers Rebuild/Rebuild/work/render'
FILES = ('materials_glsl.json', 'slot_materials.json', 'material_instance_actors.json', 'energy_death.json', 'impact_decals.json')
man = {'generated_by': 'tools/systems/item6_handoff.py', 'source': src_root, 'target_root': INT, 'maps': {}}
for m in maps:
    od = os.path.join(out_root, m)
    os.makedirs(od, exist_ok=True)
    for f in FILES:
        p = os.path.join(src_root, m, f)
        if not os.path.exists(p):
            continue
        t = open(p, encoding='utf-8').read().replace(src_root + '/' + m + '/', INT + '/' + m + '/')
        open(os.path.join(od, f), 'w', encoding='utf-8', newline='\n').write(t)
    tex_src, tex_new = os.path.join(src_root, m, 'tex'), []
    if os.path.isdir(tex_src):
        for f in os.listdir(tex_src):
            if not os.path.exists(os.path.join(INT, m, 'tex', f)):
                os.makedirs(os.path.join(od, 'tex'), exist_ok=True)
                shutil.copyfile(os.path.join(tex_src, f), os.path.join(od, 'tex', f)); tex_new.append(f)
    a = json.load(open(os.path.join(INT, m, 'materials_glsl.json'), encoding='utf-8'))
    b = json.load(open(os.path.join(od, 'materials_glsl.json'), encoding='utf-8'))
    common = set(a) & set(b)
    changed = sorted(k for k in common if a[k] != b[k])
    new, gone = sorted(set(b) - set(a)), sorted(set(a) - set(b))
    still = [k for k in json.dumps(b).split('"') if src_root in k][:3]
    man['maps'][m] = {'existing': len(common), 'identical_to_current': len(common) - len(changed), 'changed_vs_current': changed[:20],
                      'new': len(new), 'new_keys': new, 'only_in_current': gone, 'new_textures': tex_new, 'stale_paths': still}
    print('%-16s vs current Rebuild/work/render: existing %d, identical %d, changed %d; new %d; only in current %d; new tex %d; stale paths %d' % (
        m, len(common), len(common) - len(changed), len(changed), len(new), len(gone), len(tex_new), len(still)))
json.dump(man, open(os.path.join(out_root, 'MANIFEST.json'), 'w', encoding='utf-8'), indent=1)
