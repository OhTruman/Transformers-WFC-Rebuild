"""Eye-pass audit analyser (effects + audio) over tools/systems/eyepass_run.ps1 logs (WFC_LOGTIME, CUELOG, AUDIOTIME, MUZZLELOG):
per map, the diagnostic warnings by kind with their first time, muzzle-flash vs fire-sound sync for the local player (each
'MUZZLE hand=' line paired with the next 'CUE SHOOT ev0 ... owner=1' / '... .SHOOT ev0 ... owner=1' line), dropped sounds (no
channel), streamed-cue decode waits, and the match start time (first InProgress) so a time maps to a match step at 60 Hz.
argv[1] = eyepass dir, argv[2] = out json."""
import json, os, re, sys, collections, statistics
root = sys.argv[1]
T = re.compile(r'^\s*([0-9]+\.[0-9]+) \[(\w+)\s*\] (.*)$')
KINDS = [('effect not drawn', r'weapon fx: .* not reconstructed|fx: template .* not in this map'),
         ('effect dropped (guard)', r'fx guard:'),
         ('particle module not decoded', r'map fx: .* has no decoded'),
         ('material fallback', r'using glTF fallback|shader compile failed'),
         ('sound dropped (no channel)', r'no channel for the local player'),
         ('sound missing (no cue)', r'sound cue .* not in table|localized wave .* not played'),
         ('sound late (decode wait)', r'main thread waited .* on the worker decode'),
         ('level audio op skipped', r'level audio: .*(not applied|skipped|depth exceeded)'),
         ('decals missing', r'no triangles baked from .*decals'),
         ('GPU frame spike', r'GPU frame spike')]
res = {}
for m in sorted(os.listdir(root)):
    p = os.path.join(root, m, 'wfc.log')
    if not os.path.isfile(p):
        continue
    start = None; kinds = collections.defaultdict(list); muz = []; shoot = []; streamed = 0
    for line in open(p, encoding='utf-8', errors='replace'):
        g = T.match(line)
        if not g:
            continue
        t, lvl, msg = float(g.group(1)), g.group(2), g.group(3)
        if start is None and msg.startswith('MATCH start '):
            start = t
        if msg.startswith('MUZZLE hand='):
            muz.append(t)
        if re.match(r'CUE (\S+\.)?SHOOT(_LOW_AMMO)? ev0 .* owner=1 ', msg):
            shoot.append(t)
        if 'decoding on the worker' in msg:
            streamed += 1
        if lvl in ('warn', 'error'):
            for k, rx in KINDS:
                if re.search(rx, msg):
                    kinds[k].append((t, msg[:220])); break
    deltas = []
    j = 0
    for t in muz:
        while j < len(shoot) and shoot[j] < t - 0.002:
            j += 1
        if j < len(shoot) and shoot[j] - t < 0.25:
            deltas.append(round((shoot[j] - t) * 1000.0, 1))
    res[m] = {'match_start_s': start,
              'kinds': {k: {'count': len(v), 'first_s': v[0][0], 'first_step': int((v[0][0] - start) * 60) if start else None, 'example': v[0][1]}
                        for k, v in kinds.items()},
              'muzzle_shots': len(muz), 'paired_with_fire_cue': len(deltas),
              'sync_ms': {'median': statistics.median(deltas) if deltas else None, 'max': max(deltas) if deltas else None,
                          'over_33ms': sum(1 for d in deltas if d > 33.4)},
              'streamed_cues': streamed}
    s = res[m]
    print('%-16s start %s s | shots %d paired %d sync median %s ms max %s ms (>2 frames: %d) | streamed %d | %s' % (
        m, start, s['muzzle_shots'], s['paired_with_fire_cue'], s['sync_ms']['median'], s['sync_ms']['max'], s['sync_ms']['over_33ms'], streamed,
        ', '.join('%s %d' % (k, v['count']) for k, v in s['kinds'].items())))
json.dump(res, open(sys.argv[2], 'w', encoding='utf-8'), indent=1)
