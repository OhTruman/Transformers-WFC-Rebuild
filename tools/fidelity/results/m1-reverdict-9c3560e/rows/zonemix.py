# zonemix.py <row dir>...: real-play rows - where the player bot spent the timed match (match 2) and how slow each zone was.
# Frame position comes from the SLOWFRAME 'f<N>' indices (absolute frames); AMB lines name the listener (player) zone; each frame
# between two zone lines is charged to the earlier zone. Pooled per-zone slow rates over all rows then 'predict' each row's slow %.
import sys, re, glob, collections
rows = []
for d in sys.argv[1:]:
    lg = sorted(glob.glob(d + "/*/wfc.log"))[0]
    seg = 0; live = False; zone = None; f = None; f0 = None; last = None
    frames = collections.Counter(); slow = collections.Counter(); vis = []; ins = []
    for l in open(lg, encoding="utf-8", errors="replace"):
        if "match: EndGame" in l:
            seg += 1
            if seg == 2: break
            continue
        if seg != 1: continue
        if "match: launched" in l: live = True; zone = None; f = None; last = None; continue
        if not live: continue
        m = re.search(r"SLOWFRAME f(\d+) ", l)
        if m:
            f = int(m.group(1))
            if last is None or f < last: last = f
            if zone: frames[zone] += f - last; slow[zone] += 1
            last = f; continue
        m = re.search(r"AMB inaudibleSkipped=\d+ zone=(\S+)", l)
        if m:
            if zone and last is not None: frames[zone] += f - last; last = f
            zone = m.group(1); continue
        m = re.search(r"PLAYERBOT perception: (\d+) enemies in sight range, \d+ in FOV, \d+ LOS checks, (\d+) visible", l)
        if m: ins.append(int(m.group(1))); vis.append(int(m.group(2)))
    tot = sum(frames.values()); ns = sum(slow.values())
    cap = open(d + "/capacity.csv").read().splitlines(); hdr = [h.strip('"') for h in cap[0].split(",")]
    r2 = [dict(zip(hdr, [c.strip('"') for c in ln.split(",")])) for ln in cap[1:]]; r2 = [r for r in r2 if r["match"] == "2"][0]
    rows.append(dict(d=d.rstrip("/").split("/")[-1], frames=frames, slow=slow, tot=tot, ns=ns, p90=float(r2["p90_ms"]),
                     under=float(r2["pct_under_3_33"]), ins=sum(ins) / max(1, len(ins)), vis=sum(vis) / max(1, len(vis))))
F = collections.Counter(); S = collections.Counter()
for r in rows: F.update(r["frames"]); S.update(r["slow"])
rate = {z: S[z] / F[z] if F[z] else 0.0 for z in F}
zs = [z for z, _ in F.most_common(8)]
print("pooled zone slow rate: " + ", ".join(f"{z} {rate[z]*100:.0f}% ({F[z]/sum(F.values())*100:.0f}% of time)" for z in zs))
for r in rows:
    pred = sum(r["frames"][z] * rate[z] for z in r["frames"]) / max(1, r["tot"])
    print(f"{r['d']}: p90 {r['p90']:.2f}, slow {100 - r['under']:.1f}% (log {r['ns'] / max(1, r['tot']) * 100:.1f}%), zone-mix prediction {pred * 100:.1f}%;"
          f" enemies in range {r['ins']:.1f}, visible {r['vis']:.1f}; time: " +
          ", ".join(f"{z} {r['frames'][z] / max(1, r['tot']) * 100:.0f}%" for z in zs))
