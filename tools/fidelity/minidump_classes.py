import json, os, re, sys, collections, datetime
D = json.load(open(sys.argv[1])); seen = set(); C = collections.defaultdict(list)
def s(n): return re.sub(r'\+0x[0-9a-f]+$', '', n.split('!', 1)[-1])
for r in D:
    p = os.path.normcase(os.path.abspath(r['dump']))
    if p in seen or 'threads' not in r or not r['threads']: continue
    seen.add(p); m = r['threads'][0]; log = ' | '.join(r['log'])
    ph = re.search(r"render thread last at '([^']+)'", log); ph = ph.group(1) if ph else 'no watchdog line in the log beside it'
    fatal = 'GPU driver reset' in log or 'exiting' in log
    e = r.get('exception')
    if e: key = ('CRASH', e['code'] + ' in ' + (s(e['address']) if '!' in e['address'] else e['address'].split('+')[0]), '')
    else:
        fr = ([s(m['rip'])] if m['rip'].startswith('wfc_rebuild.exe!') else []) + [s(c) for c in m['callers'] if not c.startswith('wfc_rebuild.exe!__')]
        key = ('HANG' + (' (fatal: exited)' if fatal else ' (recovered or killed)'), f"phase '{ph}'", 'main in ' + m['rip'].split('+')[0].split('!')[0] + (': ' + ' <- '.join(fr[:3]) if r['symbolized'] else ''))
    t = datetime.datetime.fromtimestamp(os.path.getmtime(r['dump'])).strftime('%m-%d')
    lane = p.split('transformers rebuild' + os.sep)[-1].split(os.sep)[0]
    C[key].append(f"{t} {lane} {os.path.basename(os.path.dirname(r['dump']))}")
for k, v in sorted(C.items(), key=lambda x: (x[0][1], -len(x[1]))):
    print(f"{len(v):3d} {k[0]} | {k[1]} | {k[2][:260]}\n      {'; '.join(sorted(set(v))[:5])}{' ...' if len(v) > 5 else ''}")
