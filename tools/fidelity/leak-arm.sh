#!/bin/bash
# NONPAGED-POOL LEAK ARM (package 7 item 4, Integration 2026-10-17): one arm of the leak series - a loop of same-match lockstep
# runs for <minutes> (or, with exe '-', no exe at all = the idle baseline), sampling \Memory\Pool Nonpaged Bytes and the
# per-tag pool (pooltags.ps1) every minute into <out>/pool.csv and <out>/tags.csv; prints the slope (MB/h) and the top growing tags.
#   bash tools/fidelity/leak-arm.sh <name> <minutes> <tree|-> "<arm env, ';'-separated>" [map 508] [size 2560x1440]
# Stops early (exit 3) above GATE_POOL_MAX_GB (9.5). WFC_MUTE=1 WFC_NOINTERACT=1 on every exe.
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:?name}; MIN=${2:?minutes}; T=${3:?tree or -}; ENVS=$4; M=${5:-508}; SZ=${6:-2560x1440}
O=work/leak/$N; mkdir -p $O; L=$O/arm.log; PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
export PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 WFC_MUTE=1 WFC_NOINTERACT=1
st() { echo "$(date +%T) $*" | tee -a $L; }
pool() { powershell -NoProfile -Command "[math]::Round((Get-Counter '\Memory\Pool Nonpaged Bytes').CounterSamples[0].CookedValue/1MB,1)" | tr -d '\r'; }
sample() { local p; p=$(pool); echo "$(date +%T),$p" >> $O/pool.csv
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/pooltags.ps1 -Top 0 -Csv "$(cygpath -w $O/tags.csv)" -Label $N > /dev/null 2>&1
  awk "BEGIN{exit !($p > ${GATE_POOL_MAX_GB:-9.5} * 1024)}" && { st "POOL GUARD: $p MB - arm stopped"; touch $O/STOPPED; return 1; }; return 0; }
[ -f $O/pool.csv ] || echo "time,np_pool_mb" > $O/pool.csv
st "LEAK ARM $N: $MIN min, tree $T, env '$ENVS', map $M, $SZ"
sample || exit 3; END=$(( $(date +%s) + MIN * 60 ))
# the sampler: every 60 s while the arm runs
( while [ $(date +%s) -lt $END ] && [ ! -f $O/STOPPED ]; do sleep 60; sample || break; done; touch $O/SAMPLER_DONE ) &
k=0
while [ $(date +%s) -lt $END ] && [ ! -f $O/STOPPED ]; do
  if [ "$T" = "-" ]; then sleep 20; continue; fi
  k=$((k + 1)); W=${SZ%x*}; H=${SZ#*x}
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/run-$k -Map $M -Arms "arm|$T|$ENVS" -Steps 9600 -From 600 -Width $W -Height $H -RenderSize $SZ -PlayerBot 1 -Reps 1 -Seed 1234 > $O/run-$k.out 2>&1
  st "run $k: $(grep -oE '(PASS|UNKNOWN|FAIL) lockstep\.arm\.r1' $O/run-$k.out | head -1); present: $(grep -m1 -oE 'PRESENT: [A-Za-z0-9 ]+' $O/run-$k/arm-r1/wfc.log 2>/dev/null)"
done
until [ -f $O/SAMPLER_DONE ]; do sleep 5; done; sample
"$PY" - "$O" "$N" <<'EOF' | tee -a $L
import csv, sys, collections
o, n = sys.argv[1], sys.argv[2]
def ts(s): h, m, x = map(int, s.split(':')); return h * 3600 + m * 60 + x
p = list(csv.DictReader(open(o + '/pool.csv')))
t = [ts(r['time']) for r in p]; t = [x + 86400 if x < t[0] else x for x in t]; v = [float(r['np_pool_mb']) for r in p]
if len(v) >= 3:
    mt, mv = sum(t) / len(t), sum(v) / len(v)
    s = sum((a - mt) * (b - mv) for a, b in zip(t, v)) / max(sum((a - mt) ** 2 for a in t), 1e-9) * 3600
    print(f"{n}: pool {v[0]:.0f} -> {v[-1]:.0f} MB over {(t[-1] - t[0]) / 60:.0f} min; slope {s:+.1f} MB/h (least squares, {len(v)} samples)")
try:
    rows = list(csv.DictReader(open(o + '/tags.csv', encoding='utf-8-sig')))
    tm = sorted({r['time'] for r in rows})
    if len(tm) >= 2:
        a = {r['tag']: (int(r['nonpaged']), int(r['np_outstanding'])) for r in rows if r['time'] == tm[0]}
        b = {r['tag']: (int(r['nonpaged']), int(r['np_outstanding'])) for r in rows if r['time'] == tm[-1]}
        d = sorted(((b[k][0] - a.get(k, (0, 0))[0], k, b[k][1] - a.get(k, (0, 0))[1]) for k in b), reverse=True)
        print(f"top nonpaged tag growth {tm[0]} -> {tm[-1]}:")
        for g, k, c in d[:10]: print(f"  {k:6s} {g / 1048576:+8.2f} MB  allocations outstanding {c:+d}")
except FileNotFoundError: print("no tags.csv (pooltags.ps1 refused?)")
EOF
st "LEAK ARM $N DONE"
