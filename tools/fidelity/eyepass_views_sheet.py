# Sheets for the authored eye-pass views (eyepass-views.ps1 output): one sheet per map, a row per view (firefight, street,
# dark, reflective, lane) and a column per mode, labelled with the view's reason from tools/fidelity/views/<map>.txt; plus the
# per-view difference from the first mode (same metrics as eyepass_score.py; fixed cameras, so no alignment check) appended to
# <dir>/EYEPASS.md as "## Authored views".
#   python eyepass_views_sheet.py <views dir> "<maps>" "<modes>"
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
O, maps, modes = sys.argv[1], sys.argv[2].split(), sys.argv[3].split()
VDIR = sys.argv[4] if len(sys.argv) > 4 else None   # another views folder (e.g. Rendering's light-facing views)
TITLE = sys.argv[5] if len(sys.argv) > 5 else "Authored views"
NAMES = {"508": "Streets", "510": "Gorge", "507": "Debris", "504": "Rust", "509": "Molten"}
LAB = {"off": "Off (original)", "real": "Realistic", "realhd": "Realistic + Enhanced materials"}
VIEWS = ["firefight", "street", "dark", "reflective", "lane"]
HERE = os.path.dirname(os.path.abspath(__file__))
try: F1, F2 = ImageFont.truetype("arial.ttf", 26), ImageFont.truetype("arial.ttf", 17)
except Exception: F1 = F2 = ImageFont.load_default()
W, H = 1280, 720

def lum(a): return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
def colourful(a):
    rg = a[..., 0] - a[..., 1]; yb = 0.5 * (a[..., 0] + a[..., 1]) - a[..., 2]
    return float(np.sqrt(rg.std() ** 2 + yb.std() ** 2) + 0.3 * np.sqrt(rg.mean() ** 2 + yb.mean() ** 2))
def mask(h, w):
    m = np.ones((h, w), bool); m[int(h * 0.46): int(h * 0.54), int(w * 0.47): int(w * 0.53)] = False; return m   # crosshair only (no HUD in views)

rows = ["", f"## {TITLE} (fixed cameras from {VDIR or 'tools/fidelity/views'}; difference from " + LAB.get(modes[0], modes[0]) + ")", "",
        "| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |",
        "|---|---|---|---|---|---|---|---|"]
for m in maps:
    why = {}
    vf = os.path.join(VDIR or os.path.join(HERE, "views"), f"{m}.txt")
    if os.path.exists(vf):
        for l in open(vf):
            p = l.rstrip("\n").split(" ", 2)
            if len(p) >= 2 and not p[0][-3:-1] == "_w": why[p[0]] = p[2] if len(p) > 2 else ""
    names = [v for v in why] if VDIR else VIEWS   # (another folder: its own view names, in file order)
    vs = [v for v in names if any(os.path.exists(os.path.join(O, f"{m}-{mo}", f"{v}.bmp")) for mo in modes)]
    if not vs: continue
    sheet = Image.new("RGB", (W * len(modes), (H + 64) * len(vs)), "black"); dr = ImageDraw.Draw(sheet)
    for i, v in enumerate(vs):
        ref = None
        for j, mo in enumerate(modes):
            f = os.path.join(O, f"{m}-{mo}", f"{v}.bmp"); x, y = j * W, i * (H + 64)
            dr.text((x + 8, y + 4), f"{NAMES.get(m, m)} ({m}) - {LAB.get(mo, mo)} - {v}", fill="white", font=F1)
            dr.text((x + 8, y + 36), why.get(v, "")[:150], fill=(255, 200, 80), font=F2)
            if not os.path.exists(f): dr.text((x + 8, y + 80), "missing", fill="red", font=F1); continue
            if os.path.exists(os.path.join(O, f"{m}-{mo}", "INVALID")): dr.text((x + W - 260, y + 4), "INVALID SIZE", fill="red", font=F1)
            im = Image.open(f).convert("RGB"); sheet.paste(im.resize((W, H), Image.BOX), (x, y + 64))
            a = np.asarray(im.resize((im.width // 4, im.height // 4), Image.BOX), dtype=np.float32)
            if j == 0: ref = a; continue
            if ref is None or ref.shape != a.shape: continue
            k = mask(*a.shape[:2]); la, lb = lum(ref)[k], lum(a)[k]
            rows.append(f"| {NAMES.get(m, m)} ({m}) | {v} | {LAB.get(mo, mo)} | {np.abs(lb - la).mean():.1f} | {(lb - la).mean():+.1f} | {lb.std() / max(la.std(), 1e-3):.2f} | "
                        f"{colourful(a[k]) - colourful(ref[k]):+.1f} | {(np.abs(a - ref).max(-1)[k] > 24).mean() * 100:.1f} |")
    sheet.save(os.path.join(O, f"views_{m}.png")); print(f"{m}: views_{m}.png ({len(vs)} views x {len(modes)} modes)")
md = os.path.join(O, "EYEPASS.md"); t = open(md).read() if os.path.exists(md) else f"# Eye pass - {os.path.basename(O)}\n"
t = t.split("\n## " + TITLE)[0].rstrip("\n") + "\n" + "\n".join(rows) + "\n"
open(md, "w").write(t); print("\n".join(rows[-8:]))
