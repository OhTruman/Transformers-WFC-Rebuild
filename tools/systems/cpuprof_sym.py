"""Symbolise a WFC_CPUPROF report (wfc_cpuprof.txt) with the exe's linker map (wfc_rebuild.map, lld format).
Usage: python cpuprof_sym.py <wfc_cpuprof.txt> <wfc_rebuild.map> [window index | all] [top N]
Prints, for the chosen report window(s): self time per function (the leaf frame) and inclusive time per function (anywhere in
the sampled stack, counted once per sample), as % of the samples, plus the source object file."""
import bisect, re, sys
from collections import Counter

def load_map(path):
    secs, names = [], {}
    for ln in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'([0-9a-f]{8})\s+([0-9a-f]{8})\s+(\d+)\s+(.*\S)\s*$', ln)
        if not m: continue
        addr, size, rest = int(m.group(1), 16), int(m.group(2), 16), m.group(4).strip()
        if '.obj:(' in rest and size > 0:
            secs.append((addr, size, re.sub(r'.*/src/', 'src/', rest.split('.obj:(')[0])))
        elif size == 0 and not rest.startswith('.') and addr not in names:
            names[addr] = rest
    secs.sort()
    return secs, [a for a, _, _ in secs], names

def lookup(mp, off):
    secs, keys, names = mp
    i = bisect.bisect_right(keys, off) - 1
    if i < 0: return '?'
    a, sz, obj = secs[i]
    if off >= a + sz: return '? [after %s]' % obj
    return '%s  [%s]' % (names.get(a, '(static in %s)' % obj)[:100], obj)

def windows(path):
    cur = None
    for ln in open(path, encoding='utf-8', errors='replace'):
        if ln.startswith('== context:'):
            cur = {'ctx': ln[12:].strip(), 'head': '', 'stacks': []}
            yield_list.append(cur)
        elif cur is not None and ln.startswith('== t '):
            cur['head'] = ln.strip()
        elif cur is not None and ' samples:' in ln:
            n, rest = ln.split(' samples:', 1)
            cur['stacks'].append((int(n), [int(x[3:], 16) if x.startswith('+0x') else None for x in rest.split()]))

yield_list = []
if __name__ == '__main__':
    rep, mapf = sys.argv[1], sys.argv[2]
    which = sys.argv[3] if len(sys.argv) > 3 else 'all'
    topn = int(sys.argv[4]) if len(sys.argv) > 4 else 25
    windows(rep)
    mp = load_map(mapf)
    sel = yield_list if which == 'all' else [yield_list[int(which)]]
    self_c, incl_c, total = Counter(), Counter(), 0
    for w in sel:
        for n, pcs in w['stacks']:
            total += n
            fns = [lookup(mp, pc) if pc is not None else 'ext (outside the exe: driver / system)' for pc in pcs]
            self_c[fns[0]] += n
            for fn in set(fns): incl_c[fn] += n
    print('windows %d, samples in the listed top stacks %d' % (len(sel), total))
    if sel: print('context of the last window:', sel[-1]['ctx'][:120])
    print('\nSELF (leaf):')
    for fn, n in self_c.most_common(topn): print('  %5.1f %%  %s' % (100.0 * n / max(1, total), fn))
    print('\nINCLUSIVE:')
    for fn, n in incl_c.most_common(topn): print('  %5.1f %%  %s' % (100.0 * n / max(1, total), fn))
