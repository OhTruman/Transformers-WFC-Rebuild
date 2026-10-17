import sys, re, collections
sys.path.insert(0, r'F:/Transformers Rebuild/Rebuild-Systems/work/hdrclamp')
import xenos_dis as X
b = open(sys.argv[1], 'rb').read(); off = int(sys.argv[2])
sh = [s for s in X.shaders(b) if s.off == off][0]
lines = sh.disasm()
print('consts:', ', '.join('%s:%s%d' % (c['name'], c['set'][0], c['reg']) for c in sh.consts if 'Uniform' in c['name']))
use = collections.defaultdict(collections.Counter)
for l in lines:
    for m in re.finditer(r'-?\|?c(\d+)\{(UniformVector_\d+)\}\|?\.([xyzw]{4})', l):
        use[m.group(2)][m.group(3)] += 1
for k in sorted(use, key=lambda s: int(s.split('_')[1])): print(k, dict(use[k]))
if len(sys.argv) > 3:
    for l in lines:
        if 'UniformVector' in l: print('   ', l[:150])
