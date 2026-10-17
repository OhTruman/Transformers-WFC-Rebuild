# Eye-pass sheets (eyepass.sh): per map, the same match steps side by side in each mode, labelled.
#   python eyepass_sheets.py <outdir> "<maps>" "<modes>"
# sheet_<map>.png: one row per captured step (thumbnails); pick_<map>.png: 4 picked steps at half size.
# Before composing, the modes' per-step SIMHASH sequences are compared with the first mode: a step after the first differing
# step shows a different simulation (labelled SIM-DIVERGED), not only a different look.
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

out, maps, modes = sys.argv[1], sys.argv[2].split(), sys.argv[3].split()
NAMES = {"off": "Off (original)", "faithful": "Faithful (RT 1)", "real": "Realistic", "realhd": "Realistic + Enhanced materials"}
MAPNAME = {"501": "Seed", "502": "Berth", "503": "Complex", "504": "Rust", "507": "Debris", "508": "Streets", "509": "Molten", "510": "Gorge"}
try:
    FONT = ImageFont.truetype("arial.ttf", 28); SMALL = ImageFont.truetype("arial.ttf", 18)
except Exception:
    FONT = SMALL = ImageFont.load_default()

def shots(d):
    p = os.path.join(d, "shots")
    return {f[1:6]: os.path.join(p, f) for f in sorted(os.listdir(p))} if os.path.isdir(p) else {}

def simhash(d):
    p = os.path.join(d, "simhash.txt"); h = {}
    if os.path.exists(p):
        for l in open(p):
            parts = l.split()
            if len(parts) == 3: h[int(parts[1])] = parts[2]
    return h

def label(im, text, sub=""):
    dr = ImageDraw.Draw(im); dr.rectangle([0, 0, im.width, 44 if not sub else 70], fill=(0, 0, 0))
    dr.text((12, 8), text, fill=(255, 255, 255), font=FONT)
    if sub: dr.text((12, 44), sub, fill=(255, 200, 80), font=SMALL)
    return im

def flash_score(path):   # warm / white near-saturated pixels: muzzle flashes, explosions, tracers
    a = np.asarray(Image.open(path).convert("RGB").resize((480, 270)), dtype=np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return int((((r > 220) & (g > 150)) | ((r > 230) & (g > 230) & (b > 230))).sum())

def detail_score(path):  # lit structure: mean gradient magnitude
    a = np.asarray(Image.open(path).convert("L").resize((480, 270)), dtype=np.float32)
    return float(np.abs(np.diff(a, axis=0)).mean() + np.abs(np.diff(a, axis=1)).mean())

report = []
for m in maps:
    dirs = {mo: os.path.join(out, f"{m}-{mo}") for mo in modes}
    S = {mo: shots(d) for mo, d in dirs.items()}
    common = sorted(set.intersection(*[set(s) for s in S.values()])) if S else []
    if not common:
        report.append(f"{m}: no common steps"); continue
    H0 = simhash(dirs[modes[0]]); split = {}
    for mo in modes[1:]:
        h = simhash(dirs[mo]); first = next((k for k in sorted(H0) if k in h and h[k] != H0[k]), None); split[mo] = first
    title = f"{MAPNAME.get(m, m)} ({m})"
    # contact sheet
    tw, th = 640, 360; rows = []
    for st in common:
        row = Image.new("RGB", (tw * len(modes), th))
        for i, mo in enumerate(modes):
            im = Image.open(S[mo][st]).convert("RGB").resize((tw, th))
            sd = split.get(mo); sub = f"SIM-DIVERGED (sim differs from step {sd})" if sd is not None and int(st) >= sd else ""
            row.paste(label(im, f"{NAMES.get(mo, mo)} - step {int(st)}", sub), (i * tw, 0))
        rows.append(row)
    sheet = Image.new("RGB", (tw * len(modes), th * len(rows)))
    for i, r in enumerate(rows): sheet.paste(r, (0, i * th))
    sheet.save(os.path.join(out, f"sheet_{m}.png"))
    # picks: most flash, most detail (Off frames), and two evenly spaced
    off = modes[0]
    fl = max(common, key=lambda s: flash_score(S[off][s])); de = max(common, key=lambda s: detail_score(S[off][s]))
    even = [common[len(common) // 3], common[2 * len(common) // 3]]
    picks = []
    for s in [fl, de] + even:
        if s not in picks: picks.append(s)
    pw, ph = 1280, 720
    pick = Image.new("RGB", (pw * len(modes), ph * len(picks)))
    for j, st in enumerate(picks):
        why = "firefight (most muzzle flash)" if st == fl else "most lit detail" if st == de else "even spacing"
        for i, mo in enumerate(modes):
            im = Image.open(S[mo][st]).convert("RGB").resize((pw, ph))
            sd = split.get(mo); sub = f"SIM-DIVERGED (sim differs from step {sd})" if sd is not None and int(st) >= sd else why
            pick.paste(label(im, f"{title} - {NAMES.get(mo, mo)} - step {int(st)}", sub), (i * pw, j * ph))
    pick.save(os.path.join(out, f"pick_{m}.png"))
    report.append(f"{m}: {len(common)} steps x {len(modes)} modes; picks {picks}; sim split vs {off}: " +
                  ", ".join(f"{mo} {'none' if v is None else v}" for mo, v in split.items()))
open(os.path.join(out, "EYEPASS.md"), "w").write("# Eye pass - " + os.path.basename(out) + "\n\n" + "\n".join("- " + r for r in report) + "\n")
print("\n".join(report))
