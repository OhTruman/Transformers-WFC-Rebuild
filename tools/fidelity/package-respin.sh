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
P=$(sed -n 's/.*penultimate \([0-9a-f]*\),.*/\1/p' $GD/../../package-gate-$g.out 2>/dev/null | head -1)
[ -n "$P" ] || P=$(sed -n '1s/.*(penultimate \([0-9a-f]*\),.*/\1/p' $GD/PACKAGE-GATE.md 2>/dev/null)   # (a gated head that was itself a respin)
p=${P:0:7}
export PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 WFC_MUTE=1 WFC_NOINTERACT=1
st() { echo "$(date +%T) $*" | tee -a $L; pool_guard; }
# pool guard (Integration 2026-10-17, driver nonpaged-pool leak): at every step boundary, above GATE_POOL_MAX_GB (9.5) nonpaged
# pool the respin stops cleanly (hold removed; per-step results stay on disk) - rerun with SKIP_BUILD=1 after the reboot
pool_guard() { [ $WHATIF = 1 ] && return; local gb; gb=$(powershell -NoProfile -Command "[math]::Round((Get-Counter '\Memory\Pool Nonpaged Bytes').CounterSamples[0].CookedValue/1GB,2)" | tr -d '\r')
  echo "  (pool $gb GB)" >> $L; if awk "BEGIN{exit !($gb > ${GATE_POOL_MAX_GB:-9.5})}"; then rm -f work/PERF_HOLD; echo "$(date +%T) POOL GUARD: nonpaged pool $gb GB > ${GATE_POOL_MAX_GB:-9.5} - stopped at the step boundary (hold removed)" | tee -a $L; exit 3; fi; }
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
for m in 501 502 503 504 507 508 509 510; do st "1 regress map $m"
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
# RERUN_1B=1 (a respin that changes the Realistic path, e.g. X3): the 1b Realistic perf rows are rerun on the respun head in the same
# hold (Realistic + HD 2 on hd_dev_full, 2560x1440, 31 v 32, 1 rep, 5 user maps - X3 rides on its defaults) instead of being carried
if [ -n "$RERUN_1B" ]; then
  HDR=$(cygpath -w "${GATE_HD_ROOT:-F:/Transformers Rebuild/Rebuild-Systems/work/hd_dev_full}" 2>/dev/null); run "mkdir -p $O/rtperf2"
  for m in 508 510 507 504 509; do
    run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/rtperf2/map-$m -Map $m -Arms 'realistic|work/ab/fast_$r|WFC_RT_MODE=2;WFC_HDTEXTURES=2;WFC_HD_ROOT=$HDR' -Steps 6000 -From 2400 -Width 2560 -Height 1440 -RenderSize 2560x1440 -PlayerBot 1 -Reps 1 -Seed 1234 > $O/rtperf2/map-$m.out 2>&1"
  done
fi
run "rm -f work/PERF_HOLD"; st "2b TIMED DONE (hold removed)"
for f in map-501 map-501-5v5; do [ -f $O/bodies/$f.out ] && grep -oE "lockstep\.[a-z]+\.r[12]: [a-z]+: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+, workload parity [A-Za-z]+; frame p50 / p90 / p99 [0-9.]+ / [0-9.]+" $O/bodies/$f.out | sed -E "s/lockstep\.([a-z]+\.r[12]).*same match as 'base' ([A-Za-z]+).*p99 ([0-9.]+) \/ ([0-9.]+)/$f \1 same=\2 p50 \3 p90 \4/" | tee -a $L; done
# ---- 3 report: carry leak / 1b / penultimate FAST / eye pass from the gated head
for d in leak rtperf2 fast_$p; do run "[ -e $O/$d ] || cmd //c mklink //J \"\$(cygpath -w $O/$d)\" \"\$(cygpath -w $GD/$d)\" >/dev/null"; done
# EYE_FROM=<eyepass name> (e.g. x3_cand12): the eye pass to carry - the sheet of the candidate exe the respun head was built from;
# default: the gated head's
run "[ -e work/eyepass/$r ] || cmd //c mklink //J \"\$(cygpath -w work/eyepass/$r)\" \"\$(cygpath -w work/eyepass/${EYE_FROM:-$g})\" >/dev/null"
st "3 report (leak, 1b, eye pass and FAST $p carried from the gated head $g)"
run "'F:/Transformers Rebuild/AssetTools/bin/py/python.exe' tools/fidelity/package_gate_report.py $O $r $p | tee -a $L"
run "'F:/Transformers Rebuild/AssetTools/bin/py/python.exe' -c \"import sys; p=sys.argv[1]; t=open(p,encoding='utf-8').read().split(chr(10)); t.insert(1, chr(10)+'Respin of $g: the regress and the delta FAST are new on $r; leak, the 1b Realistic perf rows, the eye pass and the penultimate FAST $p are carried from $g (the respin cannot move them).'); open(p,'w',encoding='utf-8').write(chr(10).join(t))\" $O/PACKAGE-GATE.md"
st "PACKAGE RESPIN DONE"
