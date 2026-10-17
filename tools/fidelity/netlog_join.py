# Out-of-band desync check for the netcode harness (NETCODE_HARNESS.md section 3): joins the peers' WFC_NET_LOG csv files on tick
# and reports the first tick where the SIMHASH differs, whether the merged command-bundle hashes already differed before it (a
# transport / merge bug, not a sim one), stall ticks and bytes per peer. Columns are read by header name (ONLINE_MP_ARCHITECTURE.md
# section 9: one line per tick, buffered): tick, simhash, bundle, stall_ms, bytes_in, bytes_out (extra columns ignored).
#   python netlog_join.py <peer A csv> <peer B csv> [...] [--json out.json]
# exit 0 = all peers agree on every common tick; 1 = SIMHASH split; 2 = bundles split first (transport); 3 = unreadable / no common ticks
import csv, json, sys
args = sys.argv[1:]; jout = None
if "--json" in args: i = args.index("--json"); jout = args[i + 1]; del args[i:i + 2]
peers = {}
for p in args:
    try:
        with open(p, newline="", encoding="utf-8-sig") as f:
            r = csv.DictReader(f); rows = {}
            for row in r:
                k = {c.strip().lower(): (v or "").strip() for c, v in row.items() if c}
                if k.get("tick", "").lstrip("-").isdigit(): rows[int(k["tick"])] = k
        peers[p] = rows
    except OSError as e:
        print(f"unreadable {p}: {e}"); sys.exit(3)
common = sorted(set.intersection(*(set(r) for r in peers.values()))) if peers else []
if not common: print("no common ticks"); sys.exit(3)
names = list(peers); first_hash = first_bundle = None
for t in common:
    hs = {peers[n][t].get("simhash") for n in names}; bs = {peers[n][t].get("bundle") for n in names if peers[n][t].get("bundle")}
    if first_bundle is None and len(bs) > 1: first_bundle = t
    if first_hash is None and len(hs) > 1: first_hash = t
    if first_hash is not None and first_bundle is not None: break
rep = {"peers": names, "ticks_common": [common[0], common[-1], len(common)], "first_simhash_split": first_hash,
       "first_bundle_split": first_bundle, "per_peer": {}}
for n in names:
    rows = peers[n]; st = [float(rows[t].get("stall_ms") or 0) for t in common]
    bi = sum(float(rows[t].get("bytes_in") or 0) for t in common); bo = sum(float(rows[t].get("bytes_out") or 0) for t in common)
    secs = len(common) / 60.0
    rep["per_peer"][n] = {"stall_ticks": sum(1 for s in st if s > 0), "stall_ms_total": round(sum(st), 1), "stall_ms_max": max(st) if st else 0,
                          "bytes_in_per_s": round(bi / secs, 1), "bytes_out_per_s": round(bo / secs, 1)}
if first_hash is None: verdict, code = "AGREE: SIMHASH identical on every common tick", 0
elif first_bundle is not None and first_bundle <= first_hash: verdict, code = f"TRANSPORT / MERGE: command bundles differ from tick {first_bundle} (SIMHASH from {first_hash})", 2
else: verdict, code = f"DESYNC: SIMHASH differs from tick {first_hash}; command bundles identical up to it (a sim determinism bug)", 1
rep["verdict"] = verdict
print(verdict); print(json.dumps(rep["per_peer"], indent=1))
if first_hash is not None:
    for n in names: print(f"  {n}: tick {first_hash} simhash {peers[n][first_hash].get('simhash')}")
if jout: json.dump(rep, open(jout, "w"), indent=1)
sys.exit(code)
