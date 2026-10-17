"""The MP map_fx_runtime gap for Systems' body-notify / ability FX: every template the MP chassis can reach through their body
notifies (AnimNotifies.inc rows of their clip sources whose socket the body mesh has) plus the ability / buff templates, that a
map's map_fx_runtime.json 'systems' lacks. argv[1] = a tree's src/game dir, argv[2] = the render root (read only). Prints rows."""
import json, re, os, sys, collections

src, R = sys.argv[1], sys.argv[2]
row = re.compile(r'\{"([^"]*)", "([^"]*)", ([\d.]+)f, (true|false), "([^"]*)", "([^"]*)", (\d+)\}')
eff = collections.defaultdict(set)
for m in row.finditer(open(os.path.join(src, 'AnimNotifies.inc'), encoding='utf-8').read()):
    if m.group(4) == 'true' and m.group(1).startswith('TR_'):
        eff[(m.group(1), m.group(2))].add((m.group(5), m.group(6)))
sock = {(m.group(1), m.group(2)) for m in re.finditer(r'\{"([^"]*)", "([^"]*)", "([^"]*)",', open(os.path.join(src, 'MeshSockets.inc'), encoding='utf-8').read())}


def key(g):
    p = g.replace(chr(92), '/').split('/')
    return p[-2] + '.' + os.path.splitext(p[-1])[0] if len(p) >= 2 else ''


root = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Characters'
reach = collections.defaultdict(set)
for ch in sorted(os.listdir(root)):
    p = os.path.join(root, ch, 'character.json')
    if ch.startswith('Minion') or not os.path.isfile(p):
        continue
    c = json.load(open(p, encoding='utf-8'))
    for form in ('robot', 'vehicle'):
        mesh = key(c.get(form, {}).get('source_gltf', '') or '')
        for a in c.get(form, {}).get('animations', []) or []:
            s = a.get('source_animset') or ''
            q = a.get('sequence') or a.get('name') or ''
            for what, socket in eff.get((s, q), ()):
                if (mesh, socket) in sock:
                    reach[what].add(ch)
ability = {'FX_Multiplayer_p.FX.Drain_Blast_FX': 'TnAbilityDrain.OnTriggerEffect', 'FX_Multiplayer_p.FX.WarCry_AOE_FX': 'TnAbilityWarcry.OnTriggerEffect',
           'FX_Multiplayer_p.FX.DrainRay_Beam_FX': 'TnBuffDrainTarget beam (Beam2)', 'FX_Multiplayer_p.FX.DecoyTrap_FX': 'TnBuffDecoyTrap victim FX'}
maps = ['MP_ESC_BrokenHope', 'MP_ESC_Remnant', 'MP_IAC_Berth', 'MP_IAC_Rust', 'MP_IAC_Seed', 'MP_IAC_Streets', 'MP_KON_Molten',
        'MP_ORB_Debris', 'MP_UND_Complex', 'MP_UND_Gorge']
have = {m: set(json.load(open(os.path.join(R, m, 'map_fx_runtime.json'), encoding='utf-8'))['systems']) for m in maps}
n = 0
for t in sorted(set(reach) | set(ability)):
    miss = [m for m in maps if t not in have[m]]
    if not miss:
        continue
    n += 1
    why = ability.get(t) or 'body notify; MP chassis %d (%s%s)' % (len(reach[t]), ', '.join(sorted(reach[t])[:4]), '...' if len(reach[t]) > 4 else '')
    print('| %s | %s | %s |' % (t, 'all 10' if len(miss) == 10 else ', '.join(x[3:] for x in miss), why))
print('templates missing somewhere: %d (reachable body-notify templates %d, ability %d)' % (n, len(reach), len(ability)))
