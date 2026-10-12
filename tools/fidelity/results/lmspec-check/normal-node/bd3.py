# Which trials carry the residual: |M - K| per trial, largest first, with our spec share and the per-basis values.
import sys, numpy as np
exec(open('breakdown.py').read().split('rows = []')[0])
E = []; tot = 0; totk = 0
for j in range(T):
    Kj, Vj = K[:, :, j], Vt[:, j]; Mo = model(D[:, j], S[:, j], N[:, j], P[j], Vt[:, j])
    e = np.linalg.norm(Mo - Kj); tot += e * e; totk += np.linalg.norm(Kj) ** 2
    w2, sp = basis(N[:, j], P[j], Vj); E.append((e, j, np.linalg.norm(Kj), np.linalg.norm(Mo), sp, w2))
print(sys.argv[1].split('dump_')[-1], 'overall rel %.4f (fog %.3f)' % (np.sqrt(tot / totk), a))
for e, j, k, m, sp, w2 in sorted(E, reverse=True)[:5]:
    Nj = N[:, j] / np.linalg.norm(N[:, j])
    print('trial %2d err %.3f |K| %.3f |ours| %.3f  sp %s  w2 %s  N %s  V %s  P %.1f  S %s  D %s' % (j, e, k, m, np.round(sp, 4), np.round(w2, 3), np.round(Nj, 3), np.round(Vt[:, j], 3), P[j], np.round(S[:, j], 3), np.round(D[:, j], 3)))
    print('      K orig', np.round(K[:, :, j], 4).tolist(), '\n      ours  ', np.round(model(D[:, j], S[:, j], N[:, j], P[j], Vt[:, j]), 4).tolist())
