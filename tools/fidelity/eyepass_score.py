# Eye-pass difference score (Integration 2026-10-16): how far each mode's pictures are from Off, per map, HUD masked.
#   python eyepass_score.py <eyepass dir> "<maps>" "<modes>"   (the first mode is the reference; appends to EYEPASS.md, writes score.json)
# Per step pair (same sim step, same camera) on the scene pixels (HUD masked: top bar, kill feed, crosshair):
#   dL    mean |luminance delta| (0-255, Rec.709 on sRGB values)
#   dB    mean signed luminance change (mode - ref; < 0 = darker)
#   C     RMS contrast ratio  std(L mode) / std(L ref)          (1.00 = same contrast)
#   dM    colourfulness delta (Hasler-Suesstrunk M, mode - ref)
#   chg%  pixels whose max channel delta > 24 (of 255)
#   align correlation of the two edge maps below the top bar (lighting-independent structure): a frame-alignment check.
#         Calibrated on the 0a9c497f proof: same-moment pairs 0.68-0.97, neighbouring captures (+60 frames) 0.07-0.67;
#         < 0.62 marks the pair MISALIGNED and leaves it out of the means. (The HUD bar is translucent, so it can't be used.)
# The per-map row is the mean over aligned pairs; the bar for package 6 is that these grow row by row (night-and-day vs Off).
import json, os, sys
import numpy as np
from PIL import Image
O, maps, modes = sys.argv[1], sys.argv[2].split(), sys.argv[3].split()
ref, others = modes[0], modes[1:]
NAMES = {"508": "Streets", "510": "Gorge", "507": "Debris", "504": "Rust", "509": "Molten", "501": "Berth", "502": "Seed", "503": "Complex"}
THR, ALIGN_MIN, SCALE = 24, 0.62, 4

def load(p):
    im = Image.open(p).convert("RGB"); w, h = im.size
    return np.asarray(im.resize((w // SCALE, h // SCALE), Image.BOX), dtype=np.float32)

def masks(h, w):
    scene = np.ones((h, w), bool)
    scene[: int(h * 0.13), :] = False                                    # top bar: health, score, timer, ammo
    scene[int(h * 0.62): int(h * 0.97), : int(w * 0.32)] = False          # kill feed
    scene[int(h * 0.46): int(h * 0.54), int(w * 0.47): int(w * 0.53)] = False   # crosshair
    return scene

def edges(a):
    l = lum(a)[int(a.shape[0] * 0.13):, :]
    m = np.abs(np.diff(l, axis=1))[:-1] + np.abs(np.diff(l, axis=0))[:, :-1]; m = m - m.mean()
    return m / (np.linalg.norm(m) + 1e-6)

def lum(a): return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
def colourful(a):
    rg = a[..., 0] - a[..., 1]; yb = 0.5 * (a[..., 0] + a[..., 1]) - a[..., 2]
    return float(np.sqrt(rg.std() ** 2 + yb.std() ** 2) + 0.3 * np.sqrt(rg.mean() ** 2 + yb.mean() ** 2))

def shots(d):
    p = os.path.join(d, "shots")
    return {f[1:6]: os.path.join(p, f) for f in sorted(os.listdir(p)) if f.endswith(".bmp")} if os.path.isdir(p) else {}

res, lines = {}, []
for m in maps:
    S = {mo: shots(os.path.join(O, f"{m}-{mo}")) for mo in modes}
    res[m] = {}
    for mo in others:
        steps = sorted(set(S[ref]) & set(S[mo])); rows, mis = [], []
        for st in steps:
            a, b = load(S[ref][st]), load(S[mo][st])
            if a.shape != b.shape: mis.append(st); continue
            scene = masks(*a.shape[:2])
            if float((edges(a) * edges(b)).sum()) < ALIGN_MIN: mis.append(st); continue
            la, lb = lum(a)[scene], lum(b)[scene]
            rows.append((float(np.abs(lb - la).mean()), float((lb - la).mean()), float(lb.std() / max(la.std(), 1e-3)),
                         colourful(b[scene]) - colourful(a[scene]), float((np.abs(b - a).max(-1)[scene] > THR).mean() * 100)))
        mean = [round(float(np.mean([r[i] for r in rows])), 3) for i in range(5)] if rows else None
        res[m][mo] = {"pairs": len(rows), "misaligned": mis, "dL": mean and mean[0], "dB": mean and mean[1], "C": mean and mean[2], "dM": mean and mean[3], "chg": mean and mean[4]}
lab = {"off": "Off", "real": "Realistic", "realhd": "Realistic + Enhanced materials"}
lines += ["", f"## Difference from {lab.get(ref, ref)} (scene pixels, HUD masked; mean over same-moment pairs)", "",
          "Bigger = more visibly different from the original. Contrast 1.00 = the same contrast as Off.", "",
          "| map | mode | pairs | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % | misaligned |", "|---|---|---|---|---|---|---|---|---|"]
for m in maps:
    for mo in others:
        r = res[m][mo]
        if r["dL"] is None: lines.append(f"| {NAMES.get(m, m)} ({m}) | {lab.get(mo, mo)} | 0 | - | - | - | - | - | {len(r['misaligned'])} |"); continue
        lines.append(f"| {NAMES.get(m, m)} ({m}) | {lab.get(mo, mo)} | {r['pairs']} | {r['dL']:.1f} | {r['dB']:+.1f} | {r['C']:.2f} | {r['dM']:+.1f} | {r['chg']:.1f} | {len(r['misaligned']) or ''} |")
for mo in others:
    v = [res[m][mo] for m in maps if res[m][mo]["dL"] is not None]
    if v: lines.append(f"| **all maps** | {lab.get(mo, mo)} | {sum(x['pairs'] for x in v)} | {np.mean([x['dL'] for x in v]):.1f} | {np.mean([x['dB'] for x in v]):+.1f} | {np.mean([x['C'] for x in v]):.2f} | {np.mean([x['dM'] for x in v]):+.1f} | {np.mean([x['chg'] for x in v]):.1f} | |")
# contrast floors (user 2026-10-16 via Integration: a volumetric candidate must keep the package 6 checkpoint's look): EYE_CONTRAST_MIN
# "real=1.28,realhd=1.30" (the default) - the all-map mean contrast ratio per mode against its floor; a FLOOR line per mode
floors = dict(kv.split("=") for kv in os.environ.get("EYE_CONTRAST_MIN", "real=1.28,realhd=1.30").split(",") if "=" in kv)
fl_lines = []
for mo in others:
    v = [res[m][mo]["C"] for m in maps if res[m][mo]["C"] is not None]
    if v and mo in floors:
        c, f = float(np.mean(v)), float(floors[mo])
        fl_lines.append(f"- contrast floor {lab.get(mo, mo)}: all-map {c:.2f} vs floor {f:.2f} -> {'OK' if c >= f - 1e-9 else 'BELOW FLOOR'}")
if fl_lines: lines += [""] + fl_lines
json.dump(res, open(os.path.join(O, "score.json"), "w"), indent=1)
md = os.path.join(O, "EYEPASS.md"); t = open(md).read() if os.path.exists(md) else f"# Eye pass - {os.path.basename(O)}\n"
t = t.split("\n## Difference from")[0].rstrip("\n") + "\n" + "\n".join(lines) + "\n"
open(md, "w").write(t); print("\n".join(lines))
