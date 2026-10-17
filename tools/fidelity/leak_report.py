# Leak-arm report: pool slope (least squares) and the per-tag nonpaged growth over a window of a leak-arm.sh folder, with each
# tag's owner (the driver binaries in System32\drivers that carry the tag string; FlightRecorder.sys = Malwarebytes EDR).
#   python leak_report.py <work/leak/<arm>> [from HH:MM:SS] [to HH:MM:SS]
import csv, os, sys, glob
o = sys.argv[1]; lo = sys.argv[2] if len(sys.argv) > 2 else "00:00:00"; hi = sys.argv[3] if len(sys.argv) > 3 else "99:99:99"
def ts(s): h, m, x = map(int, s.split(':')); return h * 3600 + m * 60 + x
p = [r for r in csv.DictReader(open(os.path.join(o, 'pool.csv'))) if lo <= r['time'] <= hi]
t = [ts(r['time']) for r in p]; v = [float(r['np_pool_mb']) for r in p]
if len(v) >= 3:
    mt, mv = sum(t) / len(t), sum(v) / len(v)
    s = sum((a - mt) * (b - mv) for a, b in zip(t, v)) / max(sum((a - mt) ** 2 for a in t), 1e-9) * 3600
    print(f"{os.path.basename(o)} {p[0]['time']}-{p[-1]['time']}: pool {v[0]:.0f} -> {v[-1]:.0f} MB ({v[-1] - v[0]:+.0f}); slope {s:+.0f} MB/h ({len(v)} samples)")
rows = [r for r in csv.DictReader(open(os.path.join(o, 'tags.csv'), encoding='utf-8-sig')) if lo <= r['time'][11:] <= hi]
tm = sorted({r['time'] for r in rows})
if len(tm) < 2: sys.exit("fewer than 2 tag snapshots")
a = {r['tag']: int(r['nonpaged']) for r in rows if r['time'] == tm[0]}
b = {r['tag']: int(r['nonpaged']) for r in rows if r['time'] == tm[-1]}
d = sorted(((b.get(k, 0) - a.get(k, 0), k) for k in set(a) | set(b)), reverse=True)
drv = glob.glob(r"C:\Windows\System32\drivers\*.sys")
cache = {}
def owner(tag):
    if tag in cache: return cache[tag]
    bt = tag.encode('latin-1', 'replace'); hits = []
    for f in drv:
        try:
            if bt in open(f, 'rb').read(): hits.append(os.path.basename(f))
        except OSError: pass
    cache[tag] = ",".join(hits[:3]) + ("..." if len(hits) > 3 else "") if hits else "?"; return cache[tag]
fr = sum(g for g, k in d if owner(k).startswith('FlightRecorder') and g)
tot = sum(g for g, k in d)
print(f"tags {tm[0][11:]} -> {tm[-1][11:]}: tagged nonpaged {tot / 1048576:+.1f} MB, of which FlightRecorder.sys {fr / 1048576:+.1f} MB")
for g, k in d[:12]: print(f"  {k:6s} {g / 1048576:+8.2f} MB  {owner(k)}")
for g, k in d[-3:]: print(f"  {k:6s} {g / 1048576:+8.2f} MB  {owner(k)}  (largest drop)")
