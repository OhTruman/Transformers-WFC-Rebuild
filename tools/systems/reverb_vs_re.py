"""Compare the generated Kismet reverb ops (LevelAudio.inc) with RE's MP_REVERB_ROOMS_RE per-map json (read only):
per map the SeqAct_Reverb action ids and their presets, and the UnTouched-sourced actions. argv[1] = LevelAudio.inc,
argv[2] = RE-Workspace/notes/data/mp_reverb."""
import re, json, os, sys

s = open(sys.argv[1], encoding='utf-8').read()
blob = ''.join(re.findall(r'R"WFCJSON\((.*?)\)WFCJSON"', s, re.S))
dec = json.JSONDecoder()
docs, i = [], 0
while i < len(blob):
    while i < len(blob) and blob[i] != '{':
        i += 1
    if i >= len(blob):
        break
    o, i = dec.raw_decode(blob, i)
    docs.append(o)
ours = {d['map']: d for d in docs if d.get('map', '').startswith('MP_')}
tot_re = tot_ours = 0
for f in sorted(os.listdir(sys.argv[2])):
    if not f.startswith('MP_'):
        continue
    re_d = json.load(open(os.path.join(sys.argv[2], f), encoding='utf-8'))
    m = re_d['map']
    from collections import Counter
    re_act = Counter((a['action'], a.get('preset') or '') for a in re_d['actions'] if a.get('class') == 'SeqAct_Reverb')
    k = ours.get(m, {}).get('kismet', {})
    our_act = Counter((o.get('id', '').split('|')[-1].split('.')[-1], o.get('preset') or '') for o in k.get('ops', []) if o.get('type') == 'reverb')
    tot_re += sum(re_act.values())
    tot_ours += sum(our_act.values())
    print('%-18s RE %2d ours %2d | (action, preset) only RE %s | only ours %s' % (m, sum(re_act.values()), sum(our_act.values()),
          sorted((re_act - our_act).elements()), sorted((our_act - re_act).elements())))
print('total RE %d ours %d' % (tot_re, tot_ours))
