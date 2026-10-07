import re, subprocess, sys, collections
import os; SP = os.environ.get('ALLOCPROF_DIR', '.')  # where allocprof<N>.txt and the RelWithDebInfo exe live
EXE = os.environ.get('ALLOCPROF_EXE', SP + '/wfc_rebuild.exe')
A2L = os.environ.get('LLVM_ADDR2LINE', 'llvm-addr2line')
BASE = 0x140000000
which = sys.argv[1]
txt = open(SP + '/allocprof%s.txt' % which).read()
blocks = txt.split('== t ')
last = blocks[-1].splitlines()
rows = []
for ln in last[1:]:
    m = re.match(r'(\d+) samples \(x(\d+)\):(.*)', ln)
    if not m: continue
    offs = re.findall(r'\+0x([0-9a-f]+)', m.group(3))
    rows.append((int(m.group(1)), int(m.group(2)), offs))
addrs = sorted({o for _, _, offs in rows for o in offs})
args = [A2L, '-f', '-C', '-e', EXE] + ['0x%x' % (BASE + int(o, 16) - 1) for o in addrs]
out = subprocess.run(args, capture_output=True, text=True).stdout.splitlines()
sym = {}
for i, o in enumerate(addrs):
    fn = out[2 * i] if 2 * i < len(out) else '?'
    loc = out[2 * i + 1] if 2 * i + 1 < len(out) else '?'
    loc = re.sub(r'.*[/\\](src[/\\])', r'\1', loc)
    sym[o] = (fn, loc)
skip = ('operator new', 'std::', '__', 'allocator', 'basic_string', 'vector<', 'function<', '_M_', 'malloc')
total = sum(c for c, _, _ in rows)
print('top stacks (samples; first frame in project code):')
agg = collections.Counter()
for c, every, offs in rows:
    frames = [sym[o] for o in offs]
    first = next((f for f in frames if '/src/' in f[1].replace('\\', '/') or f[1].startswith('src')), None)
    key = (first[0][:90] + '  @ ' + first[1].split(' ')[0]) if first else '?'
    agg[key] += c
for k, c in agg.most_common(25):
    print('%6d  %4.1f%%  %s' % (c, 100.0 * c / max(1, total), k))
