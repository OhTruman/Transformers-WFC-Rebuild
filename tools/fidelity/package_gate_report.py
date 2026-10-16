# PACKAGE-GATE.md for package-gate.sh: one table from the regress / leak / eyepass / FAST outputs.
#   python package_gate_report.py <gate dir> <final short> <penultimate short>
import csv, json, os, re, sys
O, f, p = sys.argv[1:4]
lines = [f"# Package gate - final {f} (penultimate {p}, base 398b27ef)", ""]
verdicts = []

# 1 regress: per map best p90 per arm; rule: nolights - base <= +0.15, head - nolights <= +0.35
rows, ok = [], True
for m in ['501', '502', '503', '504', '507', '508', '509', '510']:
    rp = os.path.join(O, 'regress', f'map-{m}', 'report.json')
    if not os.path.exists(rp): rows.append(f"| {m} | - | - | - | - | - | missing |"); ok = False; continue
    d = json.load(open(rp, encoding='utf-8-sig')); L = d['results'] if isinstance(d, dict) and 'results' in d else d
    best, bad = {}, []
    for r in L:
        x = re.search(r'p50 / p90 / p99 ([\d.]+) / ([\d.]+)', str(r.get('note'))); mm = re.match(r'lockstep\.(\w+)\.r(\d)$', r['id'])
        if x and mm: best[mm.group(1)] = min(best.get(mm.group(1), 99), float(x.group(2)))
        if r['id'].endswith(('contaminated', 'client_rect')) or r.get('status') == 'UNKNOWN': bad.append(r['id'].split('.', 1)[1])
    b, h, n = best.get('base'), best.get('head'), best.get('nolights')
    if None in (b, h, n): rows.append(f"| {m} | {b} | {h} | {n} | - | - | incomplete |"); ok = False; continue
    r1, r2 = n - b, h - n; v = "PASS" if r1 <= 0.15 and r2 <= 0.35 and not bad else ("UNKNOWN" if bad else "FAIL"); ok &= v == "PASS"
    rows.append(f"| {m} | {b:.2f} | {h:.2f} | {n:.2f} | {r1:+.2f} | {r2:+.2f} | {v}{' (' + ', '.join(bad) + ')' if bad else ''} |")
lines += ["## 1. Regress (3-arm lockstep, RT off, Classic, best p90 of 2, ms)", "",
          "| map | base 398b27ef | final | final lights off | lights off - base (<= +0.15) | lights cost (<= +0.35) | verdict |", "|---|---|---|---|---|---|---|"] + rows + [""]
verdicts.append(("regress", "PASS" if ok else "FAIL"))

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
lines = lines[:2] + ["| part | verdict |", "|---|---|"] + [f"| {k} | {v} |" for k, v in verdicts] + [""] + lines[2:]
open(os.path.join(O, 'PACKAGE-GATE.md'), 'w', encoding='utf-8').write("\n".join(lines) + "\n")
print("\n".join(lines[:2 + 2 + len(verdicts)]))
