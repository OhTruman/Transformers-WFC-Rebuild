# Per-trial view restricted to highlight trials (our spec contributes > 20 % of the response): fitted S/ours, fitted P/ours,
# and the residual of the original against ours, D,S free (our N,P), and S,P free (our D,N).
import sys, numpy as np
exec(open('breakdown.py').read().split('rows = []')[0].replace('z = np.load(sys.argv[1], allow_pickle=True)', 'z = np.load(sys.argv[1], allow_pickle=True)'))
print(sys.argv[1].split('dump_')[-1])
for j in range(T):
    Kj, Vj = K[:, :, j], Vt[:, j]; Dj, Sj, Nj, Pj = D[:, j], S[:, j], N[:, j], P[j]
    w2, sp = basis(Nj, Pj, Vj)
    Ms = sc * (sp[:, None] * Sj[None]); Mo = model(Dj, Sj, Nj, Pj, Vj)
    share = np.linalg.norm(Ms) / max(np.linalg.norm(Mo), 1e-12)
    if share < 0.2: continue
    A = np.stack([w2, sp], 1)
    DS = np.array([np.linalg.lstsq(sc[:, c:c + 1] * A, Kj[:, c], rcond=None)[0] for c in range(3)])
    g = lambda q: (model(Dj, q[:3], Nj, np.exp(q[3]), Vj) - Kj).ravel()
    qc = least_squares(g, list(Sj) + [np.log(max(Pj, 1e-3))]).x
    print('trial %2d spec share %.2f | |K| orig %.3f ours %.3f | resid ours %.3f | D,S free: D/ours %.3f S/ours %s | S,P free resid %.3f S/ours %s P %.1f -> %.1f' % (
        j, share, np.linalg.norm(Kj), np.linalg.norm(Mo), rel(Mo, Kj), np.median(DS[:, 0] / Dj), np.round(DS[:, 1] / Sj, 3),
        rel(model(Dj, qc[:3], Nj, np.exp(qc[3]), Vj), Kj), np.round(qc[:3] / Sj, 3), Pj, np.exp(qc[3])))
