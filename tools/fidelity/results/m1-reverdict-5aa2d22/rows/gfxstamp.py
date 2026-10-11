# gfxstamp.py <row dir>...: WFC_GPUSTAMPS sections of the timed match (match 2 = the segment after the first 'match: EndGame'),
# mean over that segment's 1200-frame reports of 'all' and 'slowest tenth' ms, for the window-size-dependent sections.
import sys, re, glob, statistics
want = ["frame start to present", "after the 3D renderer: GFx UI", "post: tonemap into the window", "2D: marker, reticle, screen batches",
        "HUD screen effect", "canvas tiles", "(present)", "distortion apply"]
for d in sys.argv[1:]:
    lg = sorted(glob.glob(d + "/*/wfc.log"))
    if not lg: print(d, "no log"); continue
    seg = 0; acc = {}
    for l in open(lg[0], encoding="utf-8", errors="replace"):
        if "match: EndGame" in l: seg += 1; continue
        if seg != 1 or "GPUSTAMPS" not in l: continue
        m = re.search(r"frame start to present ([\d.]+) ms, slowest tenth ([\d.]+)", l)
        if m: acc.setdefault(want[0], []).append((float(m.group(1)), float(m.group(2)))); continue
        m = re.search(r"GPUSTAMPS\s+(.+?)\s+all ([\d.]+)\s+slowest tenth ([\d.]+) ms", l)
        if m and m.group(1).strip() in want: acc.setdefault(m.group(1).strip(), []).append((float(m.group(2)), float(m.group(3))))
    n = len(acc.get(want[0], []))
    out = [f"{d.split('/')[-1]} ({n} reports)"]
    for k in want:
        if k in acc: v = acc[k]; out.append(f"  {k}: all {statistics.mean(x for x, _ in v):.3f}, slowest tenth {statistics.mean(y for _, y in v):.3f}")
    print("\n".join(out))
