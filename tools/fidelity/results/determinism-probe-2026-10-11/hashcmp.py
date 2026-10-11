# hashcmp.py <logA> <logB>: per match (from 'match: launched'), the SIMHASH stream as a list; first differing index, with the
# steps until InProgress-ish context. The step counter is process-global, so streams are aligned at the match launch.
import sys, re
def streams(p):
    out = []; cur = None; events = []
    for l in open(p, encoding="utf-8", errors="replace"):
        if "match: launched" in l: cur = []; out.append((cur, [])); continue
        if cur is None: continue
        m = re.search(r"SIMHASH (\d+) ([0-9a-f]{16})$", l.strip())
        if m: cur.append(m.group(2)); continue
        if "MATCH start" in l or "to=InGame" in l or "MATCH spawn player=0 " in l: out[-1][1].append((len(cur), l.strip()[8:90]))
    return out
a, b = streams(sys.argv[1]), streams(sys.argv[2])
for k in range(min(len(a), len(b))):
    sa, sb = a[k][0], b[k][0]; n = min(len(sa), len(sb))
    # identical-prefix length, also allowing the stream to start a few steps later in one run (load yields)
    best = (0, 0)
    for off in range(-120, 121):
        i = 0
        while 0 <= i + off < len(sb) and i < len(sa) and sa[i] == sb[i + off]: i += 1
        if i > best[0]: best = (i, off)
    same = best[0]
    print(f"match {k+1}: {len(sa)} / {len(sb)} steps; identical for the first {same} steps (offset {best[1]}) = {same/60:.1f} s"
          f"{' - WHOLE MATCH IDENTICAL' if same >= n - abs(best[1]) - 1 else ''}; A events {a[k][1][:2]}")
