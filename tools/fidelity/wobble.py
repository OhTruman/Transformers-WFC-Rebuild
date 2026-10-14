# Weapon wobble metric (P0-1, user playtest 2026-10-13: "the gun wobbles / interlaced left-right when strafing").
# Input: a directory of consecutive frames (f####.bmp, WFC_SHOTEVERY) of a steady strafe at a fixed display rate.
# Per 64x64 tile, the shift between consecutive frames by phase correlation (sub-pixel, parabolic peak). A smooth
# presentation moves every tile by a steady amount per frame (the background scrolls evenly, the camera-locked character
# and its gun stay put), so the second difference of each tile's shift series is ~0. A part that hops on the 60 Hz step
# pattern (the gun drawn at the current step's socket while the body is interpolated) shows non-zero second differences
# and per-frame shifts that flip sign. Reported per tile: jitter = mean |d2x| (px), flips = share of frames whose x step
# changes sign (|step| > 0.25 px). The top tiles say WHERE it wobbles; heat.png shows it over the middle frame.
#   python wobble.py <framesdir> <out.json> [--tile 64]
import sys, os, json
import numpy as np
from PIL import Image

def load(p):
    return np.asarray(Image.open(p).convert('L'), dtype=np.float32)

def tiles_of(img, t):
    h, w = img.shape
    ny, nx = h // t, w // t
    a = img[:ny * t, :nx * t].reshape(ny, t, nx, t).transpose(0, 2, 1, 3)
    return a, ny, nx

def subpix(c, i, n):
    # parabolic interpolation around index i along an axis of length n (circular)
    l, m, r = c[(i - 1) % n], c[i], c[(i + 1) % n]
    d = l - 2 * m + r
    off = 0.5 * (l - r) / d if abs(d) > 1e-9 else 0.0
    v = i + off
    return v - n if v > n / 2 else v

def main():
    src, out = sys.argv[1], sys.argv[2]
    t = int(sys.argv[sys.argv.index('--tile') + 1]) if '--tile' in sys.argv else 64
    files = sorted(f for f in os.listdir(src) if f.lower().endswith('.bmp'))
    if len(files) < 12:
        json.dump({"status": "UNKNOWN", "note": f"{len(files)} frames"}, open(out, 'w')); print("UNKNOWN: too few frames"); return
    win = np.outer(np.hanning(t), np.hanning(t)).astype(np.float32)
    prevF = None; dxs = []; dys = []; peaks = []
    for f in files:
        a, ny, nx = tiles_of(load(os.path.join(src, f)), t)
        a = a - a.mean(axis=(2, 3), keepdims=True)
        F = np.fft.fft2(a * win)
        if prevF is not None:
            R = F * np.conj(prevF); R /= np.abs(R) + 1e-6
            c = np.real(np.fft.ifft2(R))                      # (ny, nx, t, t)
            flat = c.reshape(ny, nx, -1); k = flat.argmax(axis=2); pk = flat.max(axis=2)
            iy, ix = np.divmod(k, t)
            dx = np.zeros((ny, nx), np.float32); dy = np.zeros((ny, nx), np.float32)
            for y in range(ny):
                for x in range(nx):
                    cc = c[y, x]
                    dx[y, x] = subpix(cc[iy[y, x], :], ix[y, x], t)
                    dy[y, x] = subpix(cc[:, ix[y, x]], iy[y, x], t)
            dxs.append(dx); dys.append(dy); peaks.append(pk)
        prevF = F
    DX = np.stack(dxs); DY = np.stack(dys); PK = np.stack(peaks)       # (frames-1, ny, nx)
    # the steady strafe: the longest run of frame pairs whose view moves (median tile |dx| > 0.5 px), minus 6 pairs at each
    # end (acceleration / the wall); spawn teleports, the countdown and the stop are outside it (frame timing varies per run)
    texture0 = PK.mean(axis=0) > 0.08
    gm = np.array([np.median(np.abs(DX[i][texture0])) if texture0.any() else 0 for i in range(DX.shape[0])])
    mv = gm > 0.5; best = (0, 0); i = 0
    while i < len(mv):
        if mv[i]:
            j0 = i
            while i < len(mv) and mv[i]: i += 1
            if i - j0 > best[1] - best[0]: best = (j0, i)
        else: i += 1
    a0, a1 = best[0] + 6, best[1] - 6
    if a1 - a0 < 10:
        json.dump({"status": "UNKNOWN", "note": f"no steady strafe: longest moving run {best[1] - best[0]} frame pairs"}, open(out, 'w'))
        print(f"UNKNOWN: no steady strafe (longest moving run {best[1] - best[0]} frame pairs)"); return
    DX, DY, PK = DX[a0:a1], DY[a0:a1], PK[a0:a1]
    window = [files[a0], files[a1]]
    d2 = np.abs(np.diff(DX, n=2, axis=0)) + np.abs(np.diff(DY, n=2, axis=0))
    jitter = np.median(d2, axis=0)                                   # (median: a single mismatched peak is not a wobble)
    s = np.sign(np.where(np.abs(DX) > 0.25, DX, 0))
    flips = ((s[1:] * s[:-1]) < 0).mean(axis=0)
    texture = PK.mean(axis=0)                                         # low = flat / no structure: unreliable tile
    valid = texture > 0.08
    j = np.where(valid, jitter, 0)
    order = np.argsort(j.ravel())[::-1][:8]
    top = [{"tile": [int(i % j.shape[1]), int(i // j.shape[1])], "px": [int(i % j.shape[1]) * t, int(i // j.shape[1]) * t],
            "jitter_px": round(float(jitter.ravel()[i]), 3), "flips": round(float(flips.ravel()[i]), 3),
            "mean_dx": round(float(DX.reshape(DX.shape[0], -1)[:, i].mean()), 3)} for i in order]
    bg = float(np.median(np.abs(DX[:, valid]).mean(axis=0))) if valid.any() else 0.0
    res = {"frames": len(files), "steady": window, "steady_pairs": int(a1 - a0), "tile": t, "median_tile_jitter_px": round(float(np.median(jitter[valid])), 3) if valid.any() else None,
           "max_tile_jitter_px": top[0]["jitter_px"] if top else None, "max_tile_flips": top[0]["flips"] if top else None,
           "median_abs_dx_px": round(bg, 3), "top": top,
           "grid_jitter": [[round(float(v), 3) for v in row] for row in jitter], "grid_flips": [[round(float(v), 3) for v in row] for row in flips]}
    json.dump(res, open(out, 'w'), indent=1)
    # heat map over the middle frame
    mid = load(os.path.join(src, files[len(files) // 2]))
    rgb = np.stack([mid] * 3, axis=2) * 0.6
    norm = np.clip(j / max(1.0, float(j.max())), 0, 1)
    for y in range(j.shape[0]):
        for x in range(j.shape[1]):
            rgb[y * t:(y + 1) * t, x * t:(x + 1) * t, 0] += 255 * 0.4 * norm[y, x]
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(os.path.splitext(out)[0] + "_heat.png")
    print(f"frames {len(files)}, steady {window[0]}-{window[1]}: tile jitter median {res['median_tile_jitter_px']} px, max {res['max_tile_jitter_px']} px "
          f"(flips {res['max_tile_flips']}) at {top[0]['px'] if top else '-'}; background |dx| {res['median_abs_dx_px']} px/frame")

main()
