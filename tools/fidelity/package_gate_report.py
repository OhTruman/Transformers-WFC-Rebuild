# PACKAGE-GATE.md for package-gate.sh: one table from the regress / leak / eyepass / FAST outputs.
#   python package_gate_report.py <gate dir> <final short> <penultimate short>
import csv, json, os, re, sys
O, f, p = sys.argv[1:4]
base_name = os.environ.get("GATE_BASE_NAME", "398b27ef")
lines = [f"# Package gate - final {f} (penultimate {p}, base {base_name})", "", "Base and final play the same match (Classic SIMHASH identical over the measured steps); the final draws more by design, so the regress reads the frame cost of the package-6 features at the same match.", ""]
verdicts = []

# 1 regress: per map best p90 per arm; rule: nolights - base <= +0.15, head - nolights <= +0.35
rows, ok, nbis = [], True, 0
for m in ['501', '502', '503', '504', '507', '508', '509', '510']:
    rp = os.path.join(O, 'regress', f'map-{m}', 'report.json')
    if not os.path.exists(rp): rows.append(f"| {m} | - | - | - | - | - | - | missing |"); ok = False; continue
    d = json.load(open(rp, encoding='utf-8-sig')); L = d['results'] if isinstance(d, dict) and 'results' in d else d
    best, bad, work = {}, [], {}
    for r in L:
        x = re.search(r'p50 / p90 / p99 ([\d.]+) / ([\d.]+)', str(r.get('note'))); mm = re.match(r'lockstep\.(\w+)\.r(\d)$', r['id'])
        if x and mm: best[mm.group(1)] = min(best.get(mm.group(1), 99), float(x.group(2)))
        # a head arm vs the base is the same match but draws more by design (new features: death bodies, effect lights, RT rows) -
        # lockstep-ab reads that UNKNOWN (workload parity False); here it counts when the match is the same, with the draws printed.
        # Contamination / client-rect / a different match / head vs lights-off parity stay blocking.
        note = str(r.get('note'))
        wl = re.search(r"same match as '(\w+)' True, workload parity False", note)
        if r['id'].endswith(('contaminated', 'client_rect')): bad.append(r['id'].split('.', 1)[1])
        elif r.get('status') == 'UNKNOWN' and wl and mm and mm.group(1) != 'base':
            dr = re.search(r'draws (\d+) \(dyn (\d+), fx (\d+)\), binds (\d+)', note); work[mm.group(1)] = dr.groups() if dr else None
        elif r.get('status') == 'UNKNOWN': bad.append(r['id'].split('.', 1)[1])
        if mm and mm.group(1) == 'base':
            dr = re.search(r'draws (\d+) \(dyn (\d+), fx (\d+)\), binds (\d+)', note); work.setdefault('base', dr.groups() if dr else None)
    b, h, n = best.get('base'), best.get('head'), best.get('nolights')
    if None in (b, h, n): rows.append(f"| {m} | {b} | {h} | {n} | - | - | - | incomplete |"); ok = False; continue
    # Integration 2026-10-16: both bars are REPORT rows (+0.35 ms p90 each, RT off); a map above either bar gets a per-switch
    # bisect before the hand-over (WFC_ switches: effect lights, death bodies, tiled lights), not a block. Blocking: contamination,
    # client rect, a different match, final vs lights-off parity -> UNKNOWN.
    r1, r2 = n - b, h - n; v = "UNKNOWN" if bad else ("PASS" if r1 <= 0.35 and r2 <= 0.35 else "BISECT"); ok &= v != "UNKNOWN"; nbis += v == "BISECT"
    wb, wh = work.get('base'), work.get('head')
    wtxt = f"draws {wb[0]} -> {wh[0]} (dyn {wb[1]} -> {wh[1]}, fx {wb[2]} -> {wh[2]}), binds {wb[3]} -> {wh[3]}" if wb and wh else "same"
    rows.append(f"| {m} | {b:.2f} | {h:.2f} | {n:.2f} | {r1:+.2f} | {r2:+.2f} | {wtxt} | {v}{' (' + ', '.join(bad) + ')' if bad else ''} |")
lines += ["## 1. Regress (3-arm lockstep, RT off, Classic, best p90 of 2, ms)", "",
          "| map | base | final | final lights off | lights off - base (report, <= +0.35) | lights cost (report, <= +0.35) | workload base -> final (same match) | verdict |", "|---|---|---|---|---|---|---|---|"] + rows + [""]
verdicts.append(("regress", ("UNKNOWN (blocking row)" if not ok else (f"REPORT - {nbis} map(s) above +0.35: per-switch bisect before the hand-over" if nbis else "PASS"))))

# 1b Realistic perf row: report-only; flag p50 > 16.7 ms (the user's 60 fps floor at 1440p). Contamination / client rect / HD not on -> UNKNOWN
rrows, flags, rbad = [], 0, 0
for m in ['508', '510', '507', '504', '509']:
    rp = os.path.join(O, 'rtperf2', f'map-{m}', 'report.json')   # (rtperf2 = the rerun with HD 2 applied, 2026-10-16; else the first run)
    if not os.path.exists(rp): rp = os.path.join(O, 'rtperf', f'map-{m}', 'report.json')
    if not os.path.exists(rp): rrows.append(f"| {m} | - | - | missing |"); rbad += 1; continue
    d = json.load(open(rp, encoding='utf-8-sig')); L = d['results'] if isinstance(d, dict) and 'results' in d else d
    row = next((r for r in L if r['id'] == 'lockstep.realistic.r1'), None); x = re.search(r'p50 / p90 / p99 ([\d.]+) / ([\d.]+)', str(row and row.get('note')))
    blk = [r['id'].split('.', 1)[1] for r in L if r['id'].endswith(('contaminated', 'client_rect'))] + (["HD not on"] if row and 'HD requested but not on' in str(row.get('note')) else []) + (["no modern material maps"] if row and 'no modern material maps' in str(row.get('note')) else []) + (["Modern not on"] if row and "no 'Modern materials on" in str(row.get('note')) else []) + (["PBR programs failed"] if row and 'PBR programs failed' in str(row.get('note')) else [])
    if not x: rrows.append(f"| {m} | - | - | incomplete |"); rbad += 1; continue
    p50, p90 = float(x.group(1)), float(x.group(2)); fl = p50 > 16.7; flags += fl
    rrows.append(f"| {m} | {p50:.2f} | {p90:.2f} | {'UNKNOWN (' + ', '.join(blk) + ')' if blk else ('FLAG: p50 > 16.7 ms (below 60 fps)' if fl else 'ok')} |"); rbad += bool(blk)
lines += ["## 1b. Realistic perf (package 6 defaults + HD Enhanced, 2560x1440, 31 v 32, 1 rep, report-only)", "", "| map | p50 ms | p90 ms | note |", "|---|---|---|---|"] + rrows + [""]
verdicts.append(("Realistic perf", "UNKNOWN (blocking row)" if rbad else (f"REPORT - FLAG {flags} map(s) p50 > 16.7 ms" if flags else "REPORT - all maps >= 60 fps (p50)")))

# 2 leak: lobby dips of GPU dedicated
lk = os.path.join(O, 'leak', 'sampler.csv')
if os.path.exists(lk):
    r = list(csv.DictReader(open(lk))); pm = [float(x['private_mb']) for x in r]; g = [float(x['gpu_ded_mb'] or 0) for x in r]
    dips, cur, inm = [], None, False
    for i, v in enumerate(pm):
        if v > 5800:
            if cur: dips.append(cur); cur = None
            inm = True
        elif inm and 1000 < v < 5000:
            if cur is None or v < cur[1]: cur = (i, v)
    if cur: dips.append(cur)
    gd = [g[i] for i, _ in dips]; tail = gd[1:]
    grow = (max(tail) - min(tail)) if len(tail) >= 2 else None
    v = "PASS" if grow is not None and grow <= 250 and (len(tail) < 3 or tail[-1] - tail[0] <= 200) else "CHECK"
    lines += ["## 2. R4 leak rows (lobby GPU dedicated MB after each map change)", "", " / ".join(f"{x:.0f}" for x in gd) + f"  -> {v} (plateau after change 1: spread {grow if grow is None else round(grow)} MB)", ""]
    verdicts.append(("leak", v))
else:
    lines += ["## 2. R4 leak rows", "", "missing", ""]; verdicts.append(("leak", "missing"))

# 3 eyepass
ep = os.path.join('work', 'eyepass', f, 'EYEPASS.md')
lines += ["## 3. Eye pass", "", (open(ep).read().split("\n", 2)[-1] if os.path.exists(ep) else "missing") + f"\nSheets: work/eyepass/{f}/sheet_<map>.png, pick_<map>.png", ""]
verdicts.append(("eyepass", "sheets" if os.path.exists(ep) else "missing"))

# 4 FAST
for tag in (p, f):
    fg = os.path.join(O, f'fast_{tag}', 'FAST-GATE.md'); t = open(fg, encoding='utf-8').read() if os.path.exists(fg) else ""
    m = re.search(r'\*\*(PASS[^*]*)\*\*', t); head = re.search(r'^# .*$', t, re.M)
    prod = re.search(r'## PRODUCT FAIL: (\d+)', t)
    lines += [f"## 4. FAST {tag}{' (delta)' if tag == f else ' (full)'}", "", (m.group(1) if m else "missing"), ""]
    verdicts.append((f"FAST {tag}", "PASS" if prod and prod.group(1) == '0' else ("missing" if not t else f"PRODUCT {prod.group(1) if prod else '?'}")))
lines = lines[:4] + ["| part | verdict |", "|---|---|"] + [f"| {k} | {v} |" for k, v in verdicts] + [""] + lines[4:]
open(os.path.join(O, 'PACKAGE-GATE.md'), 'w', encoding='utf-8').write("\n".join(lines) + "\n")
print("\n".join(lines[:4 + 2 + len(verdicts)]))
