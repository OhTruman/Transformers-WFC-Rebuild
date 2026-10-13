# Symbolise a WFC_CPUPROF main-thread sample file (wfc_cpuprof.txt; "+0x<rva>" frames, "ext" = outside the exe) with the build's
# linker map, over a time window of the process, and print the top self frames (first exe frame; "[->ext]" = the time is in
# driver / OS code below it) and the top inclusive exe functions.
#   python tools/fidelity/cpuprof_sym.py <wfc_rebuild.map> <wfc_cpuprof.txt> <t_from> <t_to> [top]
import bisect, collections, re, sys
mapf, prof, t0, t1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
top = int(sys.argv[5]) if len(sys.argv) > 5 else 16
syms = []
for line in open(mapf, encoding='utf-8', errors='replace'):   # same symbol rows as tools/render/gltrace_sym.py
    p = line.split(None, 4)
    if len(p) >= 4 and re.fullmatch(r'[0-9a-f]{8}', p[0]) and p[1] == '00000000' and p[2] == '0':
        syms.append((int(p[0], 16), (p[3] + (' ' + p[4] if len(p) > 4 else '')).strip()))
syms.sort(); addrs = [a for a, _ in syms]
def name(rva):
    i = bisect.bisect_right(addrs, rva) - 1
    return '?' if i < 0 else re.sub(r'\(.*', '', syms[i][1].replace('(anonymous namespace)', '{anon}'))[-95:]
self_c = collections.Counter(); incl = collections.Counter(); tot = 0; win = False; nwin = 0
for l in open(prof, encoding='utf-8', errors='replace'):
    m = re.match(r'== t ([\d.]+) s', l)
    if m:
        win = t0 < float(m.group(1)) <= t1; nwin += win; continue
    m = re.match(r'(\d+) samples: (.*)', l)
    if not m or not win: continue
    n = int(m.group(1)); fr = m.group(2).split(); tot += n
    own = [f for f in fr if f.startswith('+0x')]
    if own: self_c[name(int(own[0][3:], 16)) + ('' if fr[0].startswith('+') else '  [->ext]')] += n
    else: self_c['(no exe frame)'] += n
    for f in set(name(int(x[3:], 16)) for x in own): incl[f] += n
print('window t %.0f-%.0f s: %d reports, %d samples' % (t0, t1, nwin, tot))
print('-- top self (first exe frame)')
for k, v in self_c.most_common(top): print('%6.1f%%  %s' % (100.0 * v / max(tot, 1), k))
print('-- top inclusive')
for k, v in incl.most_common(top): print('%6.1f%%  %s' % (100.0 * v / max(tot, 1), k))
