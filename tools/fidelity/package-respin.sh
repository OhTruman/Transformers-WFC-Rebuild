#!/bin/bash
# PACKAGE RESPIN (package 6, Integration 2026-10-16): after a respin of the gated head, re-gate only what the respin can move -
#   0 build    Debug + Release of the respun head (m05 build-target, -j 4, third_party) + link-render
#   1 regress  the timed 8-map 3-arm regress vs the gate base (PERF_HOLD + clear card, as package-gate.sh)
#   2 FAST     delta FAST on the respun head vs the gated head (rows from the changed paths; the rest reused from the gated head's FAST)
#   3 report   PACKAGE-GATE.md for the respun head: the regress + delta FAST are new; leak, eye pass, 1b Realistic perf and the
#              penultimate FAST are carried from the gated head's gate dir (named as carried in the report header)
#   bash tools/fidelity/package-respin.sh <respun sha> <gated sha> [--whatif]     (env: GATE_REGRESS_NOTE = the regress attribution)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
R=${1:?respun sha}; G=${2:?gated sha}; WHATIF=0; [ "$3" = "--whatif" ] && WHATIF=1
r=${R:0:7}; g=${G:0:7}; GD=work/package-gate/$g; O=work/package-gate/$r; mkdir -p $O/regress $O/bodies; L=$O/gate.log
BASE=${GATE_BASE:-work/ab/fast_398b27e_rise}; export GATE_BASE_NAME=${GATE_BASE_NAME:-"398b27ef + yaw + drive-probe + steer + walkable + rise-snap 63368d05"}
P=$(sed -n 's/.*penultimate \([0-9a-f]*\),.*/\1/p' $GD/../../package-gate-$g.out 2>/dev/null | head -1); p=${P:0:7}
export PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 WFC_MUTE=1 WFC_NOINTERACT=1
st() { echo "$(date +%T) $*" | tee -a $L; }
run() { if [ $WHATIF = 1 ]; then echo "WHATIF: $*" | tee -a $L; else eval "$@"; fi; }
busy() { powershell -NoProfile -Command "@(Get-CimInstance Win32_Process | ? { (\$_.Name -eq 'python.exe' -and \"\$(\$_.ExecutablePath) \$(\$_.CommandLine)\" -like '*Transformers Rebuild*' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') -or (\$_.Name -like 'wfc_rebuild*' -and \"\$(\$_.ExecutablePath)\" -notlike '*Rebuild-Experimental*') }).Count" | tr -d '\r'; }
st "PACKAGE RESPIN $R (gated $G, penultimate $P), base $GATE_BASE_NAME$( [ $WHATIF = 1 ] && echo ' (WHATIF)')"
for d in "$BASE/build-release/bin" "$GD/leak" "$GD/rtperf2" "$GD/fast_$p" "$GD/fast_$g"; do [ -e "$d" ] || { st "missing $d"; [ $WHATIF = 1 ] || exit 1; }; done
# ---- 0 build (CPU)
if [ -z "$SKIP_BUILD" ]; then
  st "0 build $r"
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref $R -Name fast_$r -Jobs 4 -ThirdParty work/ab/_third_party > $O/build_$r.log 2>&1"
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target work/ab/fast_$r -Alias m9c_$r >> $O/build_$r.log 2>&1 || { echo 'build $r lacks NOINTERACT or the RT shaders'; exit 1; }"
fi
[ -n "$BUILD_ONLY" ] && { st "BUILD_ONLY: done"; exit 0; }
# ---- 1 regress (timed)
st "1 regress: hold set; waiting for a clear card"; run "touch work/PERF_HOLD; sleep 60; until [ \"\$(busy)\" = 0 ]; do sleep 15; done"
st "1 regress START"
for m in 501 502 503 504 507 508 509 510; do
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/regress/map-$m -Map $m -Arms 'base|$BASE|;;head|work/ab/fast_$r|;;nolights|work/ab/fast_$r|WFC_NOTRANSIENTLIGHTS=1' -Steps 9600 -From 600 -RenderSize 3840x2160 -PlayerBot 1 -Reps 2 -Seed 1234 > $O/regress/map-$m.out 2>&1"
done
run "rm -f work/PERF_HOLD"; st "1 regress TIMED DONE (hold removed)"
# ---- 2 delta FAST vs the gated head
DS=$(powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/delta-steps.ps1 -Base $G -Head $R | tail -1 | tr -d '\r')
st "2 delta FAST $r: rows '${DS:-none}' rerun, the rest reused from $g"
run "WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1 powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/fast-gate.ps1 -Ref $R -Prev $G -Name fast_$r -OutDir $O/fast_$r -Steps '${DS:-none}' -ReuseFrom $GD/fast_$g > $O/fast_$r.log 2>&1"
# ---- 2b per-body cost + 5v5 (timed; Integration 2026-10-16): Berth, RT off, 3840x2160, same match, 2 reps -
#      63 bots: base / respun head / respun head + WFC_NOCORPSE=1 (the bodies' share); 5v5 (the original lobby default): base / head
st "2b per-body + 5v5: hold set"; run "touch work/PERF_HOLD; sleep 60; until [ \"\$(busy)\" = 0 ]; do sleep 15; done"
NC=";;nocorpse|work/ab/fast_$r|WFC_NOCORPSE=1"   # (only if the respun exe has Gameplay's switch - else the arm would silently equal the head)
[ $WHATIF = 1 ] || grep -q WFC_NOCORPSE work/ab/fast_$r/build-release/bin/wfc_rebuild.exe || { NC=""; st "2b: the respun exe has no WFC_NOCORPSE - nocorpse arm skipped"; }
run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/bodies/map-501 -Map 501 -Arms 'base|$BASE|;;head|work/ab/fast_$r|$NC' -Steps 9600 -From 600 -RenderSize 3840x2160 -PlayerBot 1 -Reps 2 -Seed 1234 > $O/bodies/map-501.out 2>&1"
run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/bodies/map-501-5v5 -Map 501 -Bots 5 -Arms 'base|$BASE|;;head|work/ab/fast_$r|' -Steps 9600 -From 600 -RenderSize 3840x2160 -PlayerBot 1 -Reps 2 -Seed 1234 > $O/bodies/map-501-5v5.out 2>&1"
run "rm -f work/PERF_HOLD"; st "2b TIMED DONE (hold removed)"
for f in map-501 map-501-5v5; do [ -f $O/bodies/$f.out ] && grep -oE "lockstep\.[a-z]+\.r[12]: [a-z]+: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+, workload parity [A-Za-z]+; frame p50 / p90 / p99 [0-9.]+ / [0-9.]+" $O/bodies/$f.out | sed -E "s/lockstep\.([a-z]+\.r[12]).*same match as 'base' ([A-Za-z]+).*p99 ([0-9.]+) \/ ([0-9.]+)/$f \1 same=\2 p50 \3 p90 \4/" | tee -a $L; done
# ---- 3 report: carry leak / 1b / penultimate FAST / eye pass from the gated head
for d in leak rtperf2 fast_$p; do run "[ -e $O/$d ] || cmd //c mklink //J \"\$(cygpath -w $O/$d)\" \"\$(cygpath -w $GD/$d)\" >/dev/null"; done
run "[ -e work/eyepass/$r ] || cmd //c mklink //J \"\$(cygpath -w work/eyepass/$r)\" \"\$(cygpath -w work/eyepass/$g)\" >/dev/null"
st "3 report (leak, 1b, eye pass and FAST $p carried from the gated head $g)"
run "'F:/Transformers Rebuild/AssetTools/bin/py/python.exe' tools/fidelity/package_gate_report.py $O $r $p | tee -a $L"
run "'F:/Transformers Rebuild/AssetTools/bin/py/python.exe' -c \"import sys; p=sys.argv[1]; t=open(p,encoding='utf-8').read().split(chr(10)); t.insert(1, chr(10)+'Respin of $g: the regress and the delta FAST are new on $r; leak, the 1b Realistic perf rows, the eye pass and the penultimate FAST $p are carried from $g (the respin cannot move them).'); open(p,'w',encoding='utf-8').write(chr(10).join(t))\" $O/PACKAGE-GATE.md"
st "PACKAGE RESPIN DONE"
