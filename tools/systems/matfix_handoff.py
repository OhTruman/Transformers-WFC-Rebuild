"""Replace named materials in the integration materials_glsl.json with a fresh build's entries (e.g. after a matc fix),
everything else byte-identical. argv[1] = fresh build root (<root>/<map>/materials_glsl.json, from build_materials_only.py),
argv[2] = out root (<out>/<map>/materials_glsl.json + <out>/MATFIX_MANIFEST.json), argv[3] = comma list of material paths,
argv[4:] = maps. Checks: the fresh build vs the current file over the shared keys (output root normalised) - the report lists
every entry that differs, so "exactly the named entries changed" is verifiable; every non-named current entry identical
in the merged file; the named entries compile (error None) in the fresh build."""
import json, os, sys
new_root, out_root = (sys.argv[i].replace('\\', '/').rstrip('/') for i in (1, 2))
names = [n.strip() for n in sys.argv[3].split(',') if n.strip()]
maps = sys.argv[4:]
INT = 'F:/Transformers Rebuild/Rebuild/work/render'
man = {'generated_by': 'tools/systems/matfix_handoff.py', 'source': new_root, 'target_root': INT, 'materials': names, 'maps': {}}
for m in maps:
    gen = json.load(open(f'{new_root}/{m}/materials_glsl.json', encoding='utf-8'))
    cur_raw = open(f'{INT}/{m}/materials_glsl.json', encoding='utf-8').read()
    cur = json.loads(cur_raw)
    rel = os.environ.get('FRESH_PREFIX')   # the out root as build_materials_only.py was given it, when relative (e.g. 'out')
    def norm(o):
        t = json.dumps(o, sort_keys=True).replace(f'{new_root}/{m}/', f'{INT}/{m}/')
        return t.replace(f'"{rel}/{m}/', f'"{INT}/{m}/') if rel else t
    shared = sorted(set(gen) & set(cur))
    differ = [k for k in shared if norm(gen[k]) != json.dumps(cur[k], sort_keys=True)]
    only_new = sorted(set(gen) - set(cur)); only_cur = sorted(set(cur) - set(gen))
    merged = dict(cur)
    for n in names:
        if n in gen:
            merged[n] = json.loads(norm(gen[n]))
    od = os.path.join(out_root, m); os.makedirs(od, exist_ok=True)
    json.dump(merged, open(os.path.join(od, 'materials_glsl.json'), 'w', encoding='utf-8'), indent=1)
    chk = json.load(open(os.path.join(od, 'materials_glsl.json'), encoding='utf-8'))
    same = sum(1 for k in cur if k not in names and chk.get(k) == cur[k])
    man['maps'][m] = {'current_entries': len(cur), 'non_named_identical': same, 'non_named_total': len([k for k in cur if k not in names]),
                      'replaced': [n for n in names if n in gen], 'named_errors': {n: gen[n].get('error') for n in names if n in gen},
                      'fresh_vs_current_differ': differ, 'fresh_only': only_new[:20], 'current_only': only_cur[:20],
                      'stale_path': new_root in json.dumps(chk) or bool(rel and f'"{rel}/{m}/' in json.dumps(chk))}
    r = man['maps'][m]
    print('%-18s current %d | non-named identical %d/%d | replaced %d | fresh vs current differ %d %s | fresh-only %d current-only %d | stale %s' % (
        m, len(cur), same, r['non_named_total'], len(r['replaced']), len(differ), [d.split('.')[-1] for d in differ][:6],
        len(only_new), len(only_cur), r['stale_path']))
json.dump(man, open(os.path.join(out_root, 'MATFIX_MANIFEST.json'), 'w', encoding='utf-8'), indent=1)
