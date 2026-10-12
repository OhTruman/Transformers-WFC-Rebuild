# Term breakdown from a per-trial dump: which of D (diffuse), S (spec colour), N (normal), P (power) must change for our lightmap
# response to reproduce the original's K_i = a * Scale_i * (D w_i^2 + S sp_i(N, P)).
import sys, numpy as np
class _R: pass
def least_squares(f, x0, iters=200):
    """Levenberg-Marquardt with a numeric Jacobian (no scipy in this python)."""
    x = np.array(x0, float); lam = 1e-3; r = f(x); c = r @ r
    for _ in range(iters):
        J = np.stack([(f(x + e) - r) / 1e-6 for e in np.eye(len(x)) * 1e-6], 1)
        A = J.T @ J; g = J.T @ r
        while True:
            dx = -np.linalg.solve(A + lam * np.diag(np.diag(A) + 1e-12), g)
            r2 = f(x + dx); c2 = r2 @ r2
            if np.isfinite(c2) and c2 < c: x, r, c, lam = x + dx, r2, c2, lam * 0.3; break
            lam *= 10
            if lam > 1e12: break
        if lam > 1e12 or np.abs(dx).max() < 1e-10: break
    o = _R(); o.x = x; return o
B3 = np.array([[0.0, 0.81649658, 0.57735027], [-0.70710678, -0.40824829, 0.57735027], [0.70710678, -0.40824829, 0.57735027]])
z = np.load(sys.argv[1], allow_pickle=True)
K, D, S, P, N, Vt, sc, a = z['Kt'], z['D'], z['S'], np.asarray(z['P'], float), z['N'], z['Vt'], z['lmscale'], float(z['fog'])
T = K.shape[2]; K = K / (a if a else 1.0)
def basis(Nj, Pj, Vj):
    Nj = Nj / np.linalg.norm(Nj); R = -Vj + 2 * Nj * (Nj @ Vj)
    w = B3 @ Nj; H = Nj[None] + B3; H /= np.linalg.norm(H, axis=1, keepdims=True)
    return w ** 2, np.power(np.maximum(np.clip(H @ R, 0, 1), 1e-4), Pj)
def model(Dj, Sj, Nj, Pj, Vj): w2, sp = basis(Nj, Pj, Vj); return sc * (w2[:, None] * Dj[None] + sp[:, None] * Sj[None])
def rel(M, Kj): return np.linalg.norm(M - Kj) / max(np.linalg.norm(Kj), 1e-12)
rows = []
for j in range(T):
    Kj, Vj = K[:, :, j], Vt[:, j]; Dj, Sj, Nj, Pj = D[:, j], S[:, j], N[:, j], P[j]
    r_ours = rel(model(Dj, Sj, Nj, Pj, Vj), Kj)
    # (a) our N, P; D and S free per channel (3 eq / 2 unknowns per channel)
    w2, sp = basis(Nj, Pj, Vj); A = np.stack([w2, sp], 1)
    DS = np.array([np.linalg.lstsq(sc[:, c:c + 1] * A, Kj[:, c], rcond=None)[0] for c in range(3)])   # (3 ch, 2)
    r_a = rel(model(DS[:, 0], DS[:, 1], Nj, Pj, Vj), Kj)
    # (b) our D, S; N (2 angles) and P free
    th0 = np.arccos(np.clip(Nj[2] / np.linalg.norm(Nj), -1, 1)); ph0 = np.arctan2(Nj[1], Nj[0])
    f = lambda q: (model(Dj, Sj, np.array([np.sin(q[0]) * np.cos(q[1]), np.sin(q[0]) * np.sin(q[1]), np.cos(q[0])]), np.exp(q[2]), Vj) - Kj).ravel()
    q = least_squares(f, [th0, ph0, np.log(max(Pj, 1e-3))]).x
    Nb = np.array([np.sin(q[0]) * np.cos(q[1]), np.sin(q[0]) * np.sin(q[1]), np.cos(q[0])])
    r_b = rel(model(Dj, Sj, Nb, np.exp(q[2]), Vj), Kj)
    # (c) our D, N; S per channel and P free
    g = lambda q: (model(Dj, q[:3], Nj, np.exp(q[3]), Vj) - Kj).ravel()
    qc = least_squares(g, list(Sj) + [np.log(max(Pj, 1e-3))]).x; r_c = rel(model(Dj, qc[:3], Nj, np.exp(qc[3]), Vj), Kj)
    rows.append((r_ours, r_a, r_b, r_c, DS[:, 0] / np.maximum(np.abs(Dj), 1e-6), DS[:, 1] / np.maximum(np.abs(Sj), 1e-6),
                 np.degrees(np.arccos(np.clip(Nb @ (Nj / np.linalg.norm(Nj)), -1, 1))), np.exp(q[2]) / max(Pj, 1e-6), qc[:3] / np.maximum(np.abs(Sj), 1e-6), np.exp(qc[3]) / max(Pj, 1e-6), Pj))
R = np.array([r[:4] for r in rows])
print(sys.argv[1].split('dump_')[-1], 'mode', z['mode'], 'vlm', z['vlm'], 'fog', round(a, 3), 'T', T)
print('median rel residual: ours %.3f | (a) D,S free %.3f | (b) N,P free %.3f | (c) S,P free %.3f' % tuple(np.median(R, 0)))
ok = R[:, 0] > 1e-3
print('(a) fitted D/ours median per ch', np.round(np.median(np.array([r[4] for r in rows])[ok], 0), 3), ' S/ours', np.round(np.median(np.array([r[5] for r in rows])[ok], 0), 3))
print('(b) normal tilt deg median %.2f p90 %.2f, P/ours median %.3f' % (np.median([r[6] for r in rows]), np.percentile([r[6] for r in rows], 90), np.median([r[7] for r in rows])))
print('(c) S/ours median', np.round(np.median(np.array([r[8] for r in rows]), 0), 3), ' P/ours median %.3f; our P range %.1f-%.1f' % (np.median([r[9] for r in rows]), min(r[10] for r in rows), max(r[10] for r in rows)))
