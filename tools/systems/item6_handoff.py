"""Item 6 handoff: per map, materials_glsl.json = the CURRENT integration file (Rebuild/work/render/<map>) plus the materials the
patched tools added (the keys of the patched build argv[1]/<map> that the unpatched build argv[2]/<map> lacks), their texture
paths re-pointed from argv[1]/<map>/ to Rebuild/work/render/<map>/; plus the textures the build exported that the current tex/
lacks. Merged, never replaced: the current files carry keys from newer lists the patched build does not produce. Checks: every
current entry value-identical in the merged file; existing entries of the two builds identical (output path normalised).
Output argv[3]/<map>/ and argv[3]/MANIFEST.json (existing maps kept). argv[4:] = maps."""
import json, os, shutil, sys
maps = sys.argv[4:]
new_root, base_root, out_root =(sys.argv[i].replace('\\', '/').rstrip('/') for i in (1, 2, 3))
INT = 'F:/Transformers Rebuild/Rebuild/work/render'
mpath = os.path.join(out_root, 'MANIFEST.json')
man = json.load(open(mpath, encoding='utf-8')) if os.path.exists(mpath) else {'generated_by': 'tools/systems/item6_handoff.py', 'target_root': INT, 'maps': {}}
def norm(o, root):
    return json.dumps(o, sort_keys=True).replace(root, '<OUT>')
for m in maps:
    od = os.path.join(out_root, m)
    os.makedirs(od, exist_ok=True)
    gen = json.load(open(f'{new_root}/{m}/materials_glsl.json', encoding='utf-8'))
    ref = json.load(open(f'{base_root}/{m}/materials_glsl.json', encoding='utf-8'))
    cur = json.load(open(f'{INT}/{m}/materials_glsl.json', encoding='utf-8'))
    common = set(gen) & set(ref)
    changed = sorted(k for k in common if norm(gen[k], new_root) != norm(ref[k], base_root))
    added = sorted(set(gen) - set(ref))
    errors = [k for k in added if gen[k].get('error')]
    merged = dict(cur)
    for k in added:
        if k not in merged:
            merged[k] = json.loads(json.dumps(gen[k]).replace(f'{new_root}/{m}/', f'{INT}/{m}/'))
    json.dump(merged, open(os.path.join(od, 'materials_glsl.json'), 'w', encoding='utf-8'), indent=1)
    chk = json.load(open(os.path.join(od, 'materials_glsl.json'), encoding='utf-8'))
    same_cur = sum(1 for k in cur if chk.get(k) == cur[k])
    stale = new_root in json.dumps(chk)
    tex_new = []
    tsrc = f'{new_root}/{m}/tex'
    if os.path.isdir(tsrc):
        for f in os.listdir(tsrc):
            if not os.path.exists(f'{INT}/{m}/tex/{f}'):
                os.makedirs(os.path.join(od, 'tex'), exist_ok=True)
                shutil.copyfile(f'{tsrc}/{f}', os.path.join(od, 'tex', f)); tex_new.append(f)
    man['maps'][m] = {'build_existing': len(common), 'build_identical': len(common) - len(changed), 'build_changed': changed[:20],
                      'added': len(added), 'added_errors': errors, 'current_keys': len(cur), 'current_identical': same_cur,
                      'merged_keys': len(chk), 'new_textures': tex_new, 'stale_paths': stale, 'new_keys': added}
    print('%-18s build: existing %d identical %d changed %d, added %d (errors %d) | merged %d = current %d (identical %d) + %d | new tex %d | stale paths %s' % (
        m, len(common), len(common) - len(changed), len(changed), len(added), len(errors), len(chk), len(cur), same_cur, len(chk) - len(cur), len(tex_new), stale))
json.dump(man, open(mpath, 'w', encoding='utf-8'), indent=1)
