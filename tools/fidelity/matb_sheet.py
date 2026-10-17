# Material eye pass sheets (Systems' material translator fix (B), Integration 2026-10-17): the same lockstep match frames captured
# with two render-data roots (before = integration work/render, after = the candidate root), per map x mode.
#   - per map: sheet_<map>.png - the frames with the largest before / after difference (up to --top per mode), a row each:
#     before | after | difference mask, plus up to 3 enlarged crops where the mask lands (before above after), and a 360 reference
#     column when --ref <dir> holds <map>*.png / .jpg
#   - MATB.md: per map x mode: frames compared, frames that changed, pixels changed %, mean luminance shift in the changed pixels,
#     the SIMHASH check (both arms must have played the same match), and an empty "read" column for the eye judgement
#   python matb_sheet.py <before eyepass dir> <after eyepass dir> <out dir> "<maps>" "<modes>" [--ref dir] [--top 4] [--materials scan_classes.json]
import os, sys, json, glob
import numpy as np
from PIL import Image, ImageDraw, ImageFont
a = sys.argv[1:]; opt = {}
for k in ("--ref", "--top", "--materials", "--sub"):
    if k in a: i = a.index(k); opt[k] = a[i + 1]; del a[i:i + 2]
BEF, AFT, OUT, MAPS, MODES = a[0], a[1], a[2], a[3].split(), a[4].split(); TOP = int(opt.get("--top", 4)); os.makedirs(OUT, exist_ok=True)
NAMES = {"501": "Seed", "502": "Berth", "503": "Complex", "504": "Rust", "505": "BrokenHope", "506": "Remnant", "507": "Debris",
         "508": "Streets", "509": "Molten", "510": "Gorge"}
try: F1, F2 = ImageFont.truetype("arial.ttf", 22), ImageFont.truetype("arial.ttf", 15)
except Exception: F1 = F2 = ImageFont.load_default()
MATS = {}   # map dir name (MP_IAC_Streets) -> the expected-change materials on it: scan_classes.json classes.value_change x the
            # per-map lists in scan_list.json beside it (Systems' matscan)
MAPDIR = {"501": "MP_IAC_Seed", "502": "MP_IAC_Berth", "503": "MP_UND_Complex", "504": "MP_IAC_Rust", "507": "MP_ORB_Debris",
          "508": "MP_IAC_Streets", "509": "MP_KON_Molten", "510": "MP_UND_Gorge"}
if opt.get("--materials") and os.path.exists(opt["--materials"]):
    try:
        vc = set(json.load(open(opt["--materials"], encoding="utf-8"))["classes"]["value_change"])
        sl = json.load(open(os.path.join(os.path.dirname(opt["--materials"]), "scan_list.json"), encoding="utf-8"))
        for mat, maps in sl.items():
            if mat in vc:
                for md in maps: MATS.setdefault(md, []).append(mat.split(".")[-1])
    except Exception as ex: print("materials list unreadable:", ex)
def lum(x): return 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]
def boxes(mask, n=3, pad=24):
    """the n largest changed regions (coarse 32-px grid flood fill) as (x0, y0, x1, y1)"""
    h, w = mask.shape; g = 32; gh, gw = (h + g - 1) // g, (w + g - 1) // g
    cells = np.zeros((gh, gw), int)
    for y in range(gh):
        for x in range(gw): cells[y, x] = mask[y * g:(y + 1) * g, x * g:(x + 1) * g].sum()
    on = cells > g * g * 0.04; seen = np.zeros_like(on); out = []
    for y in range(gh):
        for x in range(gw):
            if on[y, x] and not seen[y, x]:
                st = [(y, x)]; seen[y, x] = True; ys, xs, s = [], [], 0
                while st:
                    cy, cx = st.pop(); ys.append(cy); xs.append(cx); s += cells[cy, cx]
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < gh and 0 <= nx < gw and on[ny, nx] and not seen[ny, nx]: seen[ny, nx] = True; st.append((ny, nx))
                out.append((s, max(0, min(xs) * g - pad), max(0, min(ys) * g - pad), min(w, (max(xs) + 1) * g + pad), min(h, (max(ys) + 1) * g + pad)))
    return [b[1:] for b in sorted(out, reverse=True)[:n]]
rows_md = ["# Material eye pass: before (integration render data) vs after (candidate)", "", f"before: `{BEF}`  after: `{AFT}`", "",
           "| map | mode | same match (SIMHASH) | frames | frames changed | pixels changed % (mean over changed frames) | luminance shift in changed px | expected-change materials on the map | read |",
           "|---|---|---|---|---|---|---|---|---|"]
W = 960
for m in MAPS:
    tiles = []
    for mo in MODES:
        db, da = os.path.join(BEF, f"{m}-{mo}"), os.path.join(AFT, f"{m}-{mo}")
        sb = open(os.path.join(db, "simhash.txt")).read().split() if os.path.exists(os.path.join(db, "simhash.txt")) else []
        sa = open(os.path.join(da, "simhash.txt")).read().split() if os.path.exists(os.path.join(da, "simhash.txt")) else []
        same = "yes" if sb and sb == sa else ("no SIMHASH" if not sb or not sa else "DIFFERENT")
        stats = []
        for fb in sorted(glob.glob(os.path.join(db, opt.get("--sub", "shots"), "*.bmp"))):
            fa = os.path.join(da, opt.get("--sub", "shots"), os.path.basename(fb))
            if not os.path.exists(fa): continue
            A, B = np.asarray(Image.open(fb).convert("RGB"), np.float32), np.asarray(Image.open(fa).convert("RGB"), np.float32)
            if A.shape != B.shape: continue
            d = np.abs(A - B).max(-1); mask = d > 12; frac = mask.mean()
            sh = float((lum(B) - lum(A))[mask].mean()) if mask.any() else 0.0
            stats.append((frac, sh, fb, fa, mask))
        ch = [s for s in stats if s[0] > 0.0005]
        mats = sorted(set(MATS.get(MAPDIR.get(m, m), [])))
        rows_md.append(f"| {NAMES.get(m, m)} ({m}) | {mo} | {same} | {len(stats)} | {len(ch)} | {np.mean([s[0] for s in ch]) * 100 if ch else 0:.2f} | "
                       f"{np.mean([s[1] for s in ch]) if ch else 0:+.1f} | {len(mats)}{(': ' + ', '.join(mats[:6]) + (' ...' if len(mats) > 6 else '')) if mats else ''} | |")
        for frac, sh, fb, fa, mask in sorted(ch, key=lambda s: -s[0])[:TOP]:
            tiles.append((mo, frac, sh, fb, fa, mask))
    if not tiles: print(f"{m}: no changed frames"); continue
    ref = sorted(glob.glob(os.path.join(opt["--ref"], f"*{NAMES.get(m, m)}*.*")))[:1] if opt.get("--ref") else []
    cols = 3 + (1 if ref else 0); CH = 260; H = W * 9 // 16
    sheet = Image.new("RGB", (W * cols, (H + 40 + CH) * len(tiles)), "black"); dr = ImageDraw.Draw(sheet)
    for i, (mo, frac, sh, fb, fa, mask) in enumerate(tiles):
        y = i * (H + 40 + CH); ib, ia = Image.open(fb).convert("RGB"), Image.open(fa).convert("RGB"); sx = W / ib.width
        mk = Image.fromarray((mask * 255).astype(np.uint8)).convert("RGB")
        for j, (im, lab) in enumerate(((ib, "before"), (ia, "after (candidate)"), (mk, f"changed {frac * 100:.2f} %, lum {sh:+.1f}"))):
            sheet.paste(im.resize((W, H), Image.BOX), (j * W, y + 40)); dr.text((j * W + 8, y + 8), f"{NAMES.get(m, m)} {mo} step {os.path.splitext(os.path.basename(fb))[0]} - {lab}", fill="white", font=F1)
        if ref:
            r = Image.open(ref[0]).convert("RGB"); sheet.paste(r.resize((W, H), Image.BOX), (3 * W, y + 40)); dr.text((3 * W + 8, y + 8), f"360 reference: {os.path.basename(ref[0])}", fill=(255, 200, 80), font=F1)
        x = 0
        for (x0, y0, x1, y1) in boxes(mask):
            cb, ca = ib.crop((x0, y0, x1, y1)), ia.crop((x0, y0, x1, y1)); s = min((CH - 20) / 2 / max(1, y1 - y0), 600 / max(1, x1 - x0))
            tw, th = max(1, int((x1 - x0) * s)), max(1, int((y1 - y0) * s))
            sheet.paste(cb.resize((tw, th), Image.LANCZOS), (x, y + 40 + H + 4)); sheet.paste(ca.resize((tw, th), Image.LANCZOS), (x, y + 40 + H + 8 + th))
            dr.text((x + 4, y + 40 + H + 4), "before", fill="yellow", font=F2); dr.text((x + 4, y + 40 + H + 8 + th), "after", fill="cyan", font=F2)
            x += tw + 12
            if x > W * cols - 200: break
    sheet.save(os.path.join(OUT, f"sheet_{m}.png")); print(f"{m}: sheet_{m}.png ({len(tiles)} frames)")
open(os.path.join(OUT, "MATB.md"), "w", encoding="utf-8").write("\n".join(rows_md) + "\n"); print("\n".join(rows_md))
