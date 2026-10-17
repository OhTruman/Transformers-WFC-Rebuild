"""Offline coverage of the body notify dispatch (Systems): per MP chassis (ExtractedAssets/VerticalSlice/Characters, read only),
the HmAnimNotify_PlayEffect rows reachable from its clip sources (character.json robot / vehicle animations: source_animset,
sequence) and whether the body mesh (robot / vehicle source_gltf -> "<pkg>.<mesh>") carries each row's socket (MeshSockets.inc).
argv[1] = the tree's src/game dir (AnimNotifies.inc, MeshSockets.inc)."""
import json, os, re, sys, collections

src = sys.argv[1]
root = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Characters'
row = re.compile(r'\{"([^"]*)", "([^"]*)", ([\d.]+)f, (true|false), "([^"]*)", "([^"]*)", (\d+)\}')
eff = collections.defaultdict(list)
for m in row.finditer(open(os.path.join(src, 'AnimNotifies.inc'), encoding='utf-8').read()):
    if m.group(4) == 'true' and m.group(1).startswith('TR_'):
        eff[(m.group(1), m.group(2))].append((float(m.group(3)), m.group(5), m.group(6), int(m.group(7))))
sock = set()
for m in re.finditer(r'\{"([^"]*)", "([^"]*)", "([^"]*)",', open(os.path.join(src, 'MeshSockets.inc'), encoding='utf-8').read()):
    sock.add((m.group(1), m.group(2)))


def key(g):
    parts = g.replace('\\', '/').split('/')
    return parts[-2] + '.' + os.path.splitext(parts[-1])[0] if len(parts) >= 2 else ''


tot = collections.Counter()
missing = collections.Counter()
for ch in sorted(os.listdir(root)):
    p = os.path.join(root, ch, 'character.json')
    if not os.path.isfile(p):
        continue
    c = json.load(open(p, encoding='utf-8'))
    n_ok = n_miss = 0
    tmpl = collections.Counter()
    for form in ('robot', 'vehicle'):
        mesh = key(c.get(form, {}).get('source_gltf', '') or '')
        seen = set()
        for a in c.get(form, {}).get('animations', []) or []:
            s = a.get('source_animset') or ''
            q = a.get('sequence') or a.get('name') or ''
            if not s or (s, q) in seen:
                continue
            seen.add((s, q))
            for t, what, socket, fl in eff.get((s, q), []):
                if (mesh, socket) in sock:
                    n_ok += 1
                    tmpl[what.split('.')[-1]] += 1
                else:
                    n_miss += 1
                    missing[(what.split('.')[-1], socket)] += 1
    tot['ok'] += n_ok
    tot['miss'] += n_miss
    print(f'{ch:14s} effects with a socket {n_ok:3d}, socket not on the body {n_miss:3d}  {", ".join(f"{k} x{v}" for k, v in tmpl.most_common(4))}')
print('total', dict(tot))
print('most common socket misses:', missing.most_common(8))
