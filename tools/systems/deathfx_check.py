"""Check Gameplay's DEATHFX log lines (WFC_DEATHFXLOG / WFC_CORPSETEST) against DeathFx.inc: per gib line, the entry's
expected gib templates, chunks, skeletal gib and decal; per play line, the expected PlayEffect templates; socket fallbacks
counted. argv: <DeathFx.inc> <wfc.log> [...]"""
import re, sys, collections
inc = open(sys.argv[1], encoding='utf-8').read()
rows = {}
for m in re.finditer(r'\{"(\w+)", "(\w+)", "[^"]*", (\d), \{(.*?)\}, (true|false), "[^"]*", (true|false), (true|false), (\d), \{(.*?)\}, (true|false)\},', inc):
    bp, e, nfx, fx, ch, sk, dc, npl, pl, pr = m.groups()
    rows.setdefault(e, []).append({'bp': bp, 'fx': re.findall(r'"([\w.]+)", [-\d.]+f', fx), 'chunks': ch == 'true', 'skel': sk == 'true', 'decal': dc == 'true',
                                  'play': re.findall(r'\{"([\w.]+)", "(\w*)"', pl), 'random': pr == 'true'})
gib = re.compile(r'DEATHFX p(\d+) (\S+) entry (\S+) gib: fx \[(.*?)\] chunks (\d+)/(\d+) skel (\d+)/(\d+) decal (\d)')
play = re.compile(r'DEATHFX p(\d+) entry (\S+) play: \[(.*?)\]')
n = collections.Counter(); bad = []; fb = collections.Counter(); per = collections.Counter()
for log in sys.argv[2:]:
    for line in open(log, encoding='utf-8', errors='replace'):
        g = gib.search(line)
        if g:
            p, ch, e, fx, ca, cp, sa, sp, dp = g.groups(); n['gib'] += 1; per[e] += 1
            got = [x.split('@')[0] for x in fx.split(', ') if x]
            ok = any(sorted(got) == sorted(r['fx']) and (int(cp) > 0) == r['chunks'] and (int(sp) > 0) == r['skel'] and (dp == '1') == r['decal'] for r in rows.get(e, []))
            if not ok: bad.append(('gib', e, ch, got, cp, sp, dp, [(r['bp'], r['fx'], r['chunks'], r['skel'], r['decal']) for r in rows.get(e, [])]))
            if ca != cp or sa != sp: n['gib_actual_lt_planned'] += 1
            continue
        q = play.search(line)
        if q:
            p, e, lst = q.groups(); n['play'] += 1
            items = [x for x in lst.split(', ') if x]
            got = sorted(x.split('@')[0] for x in items)
            for x in items:
                s = x.split('@')[1] if '@' in x else ''
                fb['fallback' if s == 'fallback' else 'socket'] += 1
            ok = any((len(got) == 1 and got[0] in [t for t, _ in r['play']]) if r['random'] else got == sorted(t for t, _ in r['play']) for r in rows.get(e, []))   # PET_Random: exactly one
            if not ok: bad.append(('play', e, got, [(r['bp'], r['play']) for r in rows.get(e, [])]))
print('gib lines %d, play lines %d, entries %s' % (n['gib'], n['play'], dict(per)))
print('mismatches vs DeathFx.inc: %d; actual < planned (chunks / skel) %d; play effects at socket %d, fallback %d' % (len(bad), n['gib_actual_lt_planned'], fb['socket'], fb['fallback']))
for b in bad[:12]: print('  MISMATCH', b)
