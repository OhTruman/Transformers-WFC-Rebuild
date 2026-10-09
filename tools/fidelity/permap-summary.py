# Per-map 300 fps verdict summary (Integration 2026-10-09): reads work/permap/v-<id>-{ov,rp}-{1080,2160}/capacity.csv and prints the
# second-match row of each population (warm caches) as one markdown table: p50 / p90 / p99 / 1 % low / > 16.7 / > 33 ms counts,
# the % of frames under 3.33 ms, the split (CPU submit / GPU wait / sim step / map FX) and MET (p90 <= 3.33 ms).
#   python tools/fidelity/permap-summary.py work/permap 501 502 ...
import csv, os, sys
root, ids = sys.argv[1], sys.argv[2:]
print("| map | view | 3D size | players | p50 | p90 | p99 | 1 % low fps | >16.7 / >33 | <=3.33 ms % | CPU submit | GPU wait | sim step | map FX | p90 <= 3.33 |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
for i in ids:
    for kind, view in (("ov", "overview"), ("rp", "real play")):
        for size in ("1080", "2160"):
            f = os.path.join(root, "v-%s-%s-%s" % (i, kind, size), "capacity.csv")
            if not os.path.exists(f): continue
            rows = [r for r in csv.DictReader(open(f, encoding="utf-8-sig")) if r.get("match") == "2"]
            for r in sorted(rows, key=lambda r: int(r["participants"] or 0)):
                met = "MET" if r["p90_ms"] and float(r["p90_ms"]) <= 3.33 else "**MISS**"
                g = lambda k: r.get(k) or "-"
                print("| %s | %s | %s | %s | %s | %s | %s | %s | %s / %s | %s | %s | %s | %s | %s | %s |" % (
                    i, view, "3840x2160" if size == "2160" else "1920x1080", r["participants"], g("p50_ms"), g("p90_ms"), g("p99_ms"),
                    g("low1_fps"), g("over_16_7"), g("over_33"), g("pct_under_3_33"), g("cpu_submit_ms"), g("gpu_wait_ms"),
                    g("sim_step_ms"), g("fx_ms"), met))
