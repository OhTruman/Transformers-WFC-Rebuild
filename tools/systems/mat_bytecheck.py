"""Item 6 byte check: per map, the materials_glsl.json built with the unpatched tools (argv[1] root) vs the patched tools
(argv[2] root): existing materials byte-identical after normalising each run's output dir in texture paths, changed, new,
missing. argv[3:] = maps."""
import json, os, re, sys
base_root, new_root, maps = sys.argv[1], sys.argv[2], sys.argv[3:]
def norm(o, root):
    return json.dumps(o, sort_keys=True).replace(root.replace('\\', '/').rstrip('/'), '<OUT>')
ok = True
for m in maps:
    a = json.load(open(os.path.join(base_root, m, 'materials_glsl.json'), encoding='utf-8'))
    b = json.load(open(os.path.join(new_root, m, 'materials_glsl.json'), encoding='utf-8'))
    common = set(a) & set(b)
    changed = sorted(k for k in common if norm(a[k], base_root) != norm(b[k], new_root))
    new, missing = sorted(set(b) - set(a)), sorted(set(a) - set(b))
    err = [k for k in new if b[k].get('error')]
    print('%-16s existing %d: identical %d, changed %d; new %d (errors %d); missing %d%s' % (
        m, len(common), len(common) - len(changed), len(changed), len(new), len(err), len(missing), ('  CHANGED: ' + ', '.join(changed[:5])) if changed else ''))
    ok &= not changed and not missing and not err
print('ALL CLEAN' if ok else 'NOT CLEAN')
