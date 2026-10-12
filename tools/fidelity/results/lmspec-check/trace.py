import sys
sys.path.insert(0, r"F:/Transformers Rebuild/RE-Workspace/tools")
import xenostrace as T
d = open(sys.argv[1], 'rb').read(); H = int(sys.argv[2], 0)
tgt, sc, ss, lit, out = T.run(d, H, seed_const_name=sys.argv[3], seed_sampler_name=sys.argv[4], maxlen=600)
print('seed c%s tf%s' % (sc, ss)); print('literals', {k: tuple(round(x, 5) for x in v) for k, v in lit.items()})
print('\n'.join(out))
