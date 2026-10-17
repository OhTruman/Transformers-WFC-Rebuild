#!/bin/bash
# X3 VOLUMETRICS CANDIDATE GATE (package 6 is held for it; user 2026-10-16 via Integration). One start per candidate:
#   1 eye pass   eyepass.sh on the candidate exe (match sheets + views + score at 2560x1440, Enhanced on hd_dev_full); the
#                contrast floors (Realistic >= 1.28, + Enhanced >= 1.30, all-map) print OK / BELOW FLOOR; the X3 defaults ride
#                on the 'RT Realistic defaults' line (x3=on ...) recorded per RT run
#   2 X3 cost    2-arm lockstep on the 5 user maps, Realistic (RT 2) + HD 2, 2560x1440, 31 v 32, 2 reps: candidate X3 on (its
#                defaults) vs WFC_X3=0 - p50 / p90 per arm and the delta (report; the 60 fps floor = p50 <= 16.7 ms)   [TIMED]
#   3 FAST       delta FAST vs the checkpoint 9350f19b when the candidate has a sha (CAND_SHA=...; builds it with build-target)
#   bash tools/fidelity/x3-gate.sh <name> <candidate exe>      (env: CAND_SHA, SKIP_EYE=1, SKIP_COST=1, MAPS="508 510 507 504 509")
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
N=${1:?name}; E=${2:?candidate exe}; MAPS=${MAPS:-"508 510 507 504 509"}
O=work/x3gate/$N; mkdir -p $O; L=$O/x3gate.log; PY="F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
export PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 WFC_MUTE=1 WFC_NOINTERACT=1
RD="F:/Transformers Rebuild/Rebuild/work/render"; HDR="F:\\Transformers Rebuild\\Rebuild-Systems\\work\\hd_dev_full"
st() { echo "$(date +%T) $*" | tee -a $L; }
busy() { powershell -NoProfile -Command "@(Get-CimInstance Win32_Process | ? { (\$_.Name -eq 'python.exe' -and \"\$(\$_.ExecutablePath) \$(\$_.CommandLine)\" -like '*Transformers Rebuild*' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') -or (\$_.Name -like 'wfc_rebuild*' -and \"\$(\$_.ExecutablePath)\" -notlike '*Rebuild-Experimental*') }).Count" | tr -d '\r'; }
[ -f "$E" ] || { st "no exe $E"; exit 1; }
for s in WFC_X3 WFC_BOOT_PROFILE WFC_NOINTERACT; do grep -q "$s" "$E" || { st "candidate exe lacks $s"; exit 1; }; done
# a read-only tree for the candidate (lockstep-ab needs <tree>\build-release\bin; render data linked)
T=work/ab/x3_$N; B=$(dirname "$(dirname "$E")")
if [ ! -e $T/build-release ]; then mkdir -p $T; cmd //c mklink //J "$(cygpath -w $T/build-release)" "$(cygpath -w "$B")" >/dev/null; powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target $T -Alias m9c_x3_$N >> $L 2>&1; fi
st "X3 GATE $N: $E"
# ---- 1 eye pass (untimed)
if [ -z "$SKIP_EYE" ]; then
  st "1 eye pass"
  EYE_EXE="$PWD/$T/build-release/bin/wfc_rebuild.exe" EYE_RENDER_DATA="$RD" bash tools/fidelity/eyepass.sh x3_$N "$MAPS" > $O/eyepass.out 2>&1
  st "1 eye pass done: $(grep -cE 'UNKNOWN|INVALID' $O/eyepass.out) UNKNOWN / INVALID; $(grep -m1 -oE 'x3=[^ ]+( [a-z0-9]+=[^ ]+){0,4}' work/eyepass/x3_$N/*-real/rt_defaults.txt 2>/dev/null | head -1)"
  grep -E "contrast floor" work/eyepass/x3_$N/EYEPASS.md | tee -a $L
fi
# ---- 2 X3 cost (timed: needs the hold granted by Integration)
if [ -z "$SKIP_COST" ]; then
  st "2 X3 cost: hold set; waiting for a clear card"; touch work/PERF_HOLD; sleep 60; until [ "$(busy)" = 0 ]; do sleep 15; done
  for m in $MAPS; do
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/cost/map-$m -Map $m -Arms "x3on|$T|WFC_RT_MODE=2;WFC_HDTEXTURES=2;WFC_HD_ROOT=$HDR;;x3off|$T|WFC_RT_MODE=2;WFC_HDTEXTURES=2;WFC_HD_ROOT=$HDR;WFC_X3=0" -Steps 6000 -From 2400 -Width 2560 -Height 1440 -RenderSize 2560x1440 -PlayerBot 1 -Reps 2 -Seed 1234 > $O/cost/map-$m.out 2>&1
    grep -oE "lockstep\.x3o[nf]+\.r[12]: [a-z0-9]+: steps [0-9]+ \([0-9-]+\), same match as '[a-z0-9]+' [A-Za-z]+, workload parity [A-Za-z]+; frame p50 / p90 / p99 [0-9.]+ / [0-9.]+" $O/cost/map-$m.out | sed -E "s/lockstep\.(x3o[nf]+\.r[12]).*same match as '[a-z0-9]+' ([A-Za-z]+).*p99 ([0-9.]+) \/ ([0-9.]+)/map-$m \1 same=\2 p50 \3 p90 \4/" | tee -a $L
  done
  rm -f work/PERF_HOLD; st "2 X3 cost TIMED DONE (hold removed)"
fi
# ---- 3 delta FAST vs the checkpoint (needs the candidate's sha)
if [ -n "$CAND_SHA" ]; then
  c=${CAND_SHA:0:7}; st "3 build $c + delta FAST vs 9350f19b"
  [ -d work/ab/fast_$c/build-release/bin ] || { powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref $CAND_SHA -Name fast_$c -Jobs 4 -ThirdParty work/ab/_third_party > $O/build_$c.log 2>&1; powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target work/ab/fast_$c -Alias m9c_$c >> $O/build_$c.log 2>&1; }
  DS=$(powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/delta-steps.ps1 -Base 9350f19b -Head $CAND_SHA | tail -1 | tr -d '\r')
  WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1 powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/fast-gate.ps1 -Ref $CAND_SHA -Prev 9350f19b -Name fast_$c -OutDir $O/fast_$c -Steps "${DS:-none}" -ReuseFrom work/package-gate/9350f19/fast_9350f19 > $O/fast_$c.log 2>&1
  st "3 FAST: $(grep -E '^## PRODUCT FAIL|^## HARNESS FAIL' $O/fast_$c/FAST-GATE.md | tr '\n' ' ')"
fi
st "X3 GATE DONE: $O (eye pass: work/eyepass/x3_$N)"
