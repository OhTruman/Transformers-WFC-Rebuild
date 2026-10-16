#!/bin/bash
# PACKAGE GATE (package 6 combined final gate; user 2026-10-16 via Integration): ONE start on the final head runs, in order,
#   0 builds   Debug + Release of the final and the penultimate head (m05 build-target, -j 4, third_party, integration render data;
#              CPU only - run it before the GPU block with BUILD_ONLY=1 if wanted)
#   1 regress  3-arm lockstep on all 8 maps, 2 reps, RT off, Classic: base 398b27ef / final / final + WFC_NOTRANSIENTLIGHTS=1
#              (TIMED: PERF_HOLD + clear card). REPORT rows (Integration 2026-10-16): lights-off arm vs base <= +0.35 ms p90 and the lights cost
#              (final - lights-off) <= +0.35 per map; a map above either bar gets a per-switch bisect before the hand-over, not a block
#   1b RT perf Realistic + HD Enhanced with the package 6 defaults, 2560x1440, 31 v 32, 1 rep, 5 user maps (report; flag p50 > 16.7 ms)
#   2 leak     R4 leak rows on the final head (Realistic + GI, 5 map changes; lobby GPU dedicated must plateau)
#   3 eyepass  Off | Realistic | Realistic + Enhanced materials sheets on the 5 user maps
#   4 FAST     full FAST on the penultimate head, then a delta FAST on the final head (rows whose inputs changed, delta-steps.ps1;
#              the rest reused from the penultimate run)
# and writes work/package-gate/<final>/PACKAGE-GATE.md (one table) at the end. One game exe at a time throughout.
#   bash tools/fidelity/package-gate.sh <final sha> <penultimate sha> [--whatif]      (env: BUILD_ONLY=1, SKIP_BUILD=1, SWITCH_FLIPS=1)
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
F=${1:?final sha}; P=${2:?penultimate sha}; WHATIF=0; [ "$3" = "--whatif" ] && WHATIF=1
f=${F:0:7}; p=${P:0:7}; BASE=${GATE_BASE:-work/ab/fast_398b27e_yaw_probe_steer}; export GATE_BASE_NAME=${GATE_BASE_NAME:-"398b27ef + yaw + drive-probe + steer 3de18d73"}
# (rebuild with tools/fidelity/gate-base.sh <name> <sim commits...> when another sim change lands; steer = gatebase/steer_3de18d73_vs_398b27ef.patch; verified same match vs 3de18d73 on Streets, and the yaw + drive-probe base diverges from it)
# (base = package 5 398b27ef + only Gameplay's bot vehicle yaw-rate fix 022e5e34, patch -p1, byte-checked, same m05 build-target: the yaw fix
#  changes the Classic match from step ~602, and lockstep arms must play the same match - verified 2026-10-16 vs a6e9ea67 on Streets)
O=work/package-gate/$f; mkdir -p $O/regress $O/rtperf; L=$O/gate.log
export PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0 WFC_MUTE=1 WFC_NOINTERACT=1
st() { echo "$(date +%T) $*" | tee -a $L; }
run() { if [ $WHATIF = 1 ]; then echo "WHATIF: $*" | tee -a $L; else eval "$@"; fi; }
busy() { powershell -NoProfile -Command "@(Get-CimInstance Win32_Process | ? { (\$_.Name -eq 'python.exe' -and \"\$(\$_.ExecutablePath) \$(\$_.CommandLine)\" -like '*Transformers Rebuild*' -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*') -or (\$_.Name -like 'wfc_rebuild*' -and \"\$(\$_.ExecutablePath)\" -notlike '*Rebuild-Experimental*') }).Count" | tr -d '\r'; }
mine() { powershell -NoProfile -Command "@(Get-CimInstance Win32_Process -Filter \"Name='wfc_rebuild.exe'\" | ? { \$_.ExecutablePath -like '*Rebuild-Experimental*' }).Count" | tr -d '\r'; }
st "PACKAGE GATE final $F, penultimate $P, base $GATE_BASE_NAME$( [ $WHATIF = 1 ] && echo ' (WHATIF: commands printed, nothing run)')"
[ -d "$BASE/build-release/bin" ] || { st "missing base $BASE"; [ $WHATIF = 1 ] || exit 1; }

# ---- 0 builds (CPU)
if [ -z "$SKIP_BUILD" ]; then for sha in $P $F; do s=${sha:0:7}
  st "0 build $s"
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref $sha -Name fast_$s -Jobs 4 -ThirdParty work/ab/_third_party > $O/build_$s.log 2>&1"
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target work/ab/fast_$s -Alias m9c_$s >> $O/build_$s.log 2>&1 || { echo 'build $s lacks NOINTERACT or the RT shaders'; exit 1; }"
done; fi
[ -n "$BUILD_ONLY" ] && { st "BUILD_ONLY: done"; exit 0; }

# ---- 1 regress (timed)
st "1 regress: hold set; waiting for a clear card"; run "touch work/PERF_HOLD; sleep 60; until [ \"\$(busy)\" = 0 ]; do sleep 15; done"
st "1 regress START"
for m in 501 502 503 504 507 508 509 510; do
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/regress/map-$m -Map $m -Arms 'base|$BASE|;;head|work/ab/fast_$f|;;nolights|work/ab/fast_$f|WFC_NOTRANSIENTLIGHTS=1' -Steps 9600 -From 600 -RenderSize 3840x2160 -PlayerBot 1 -Reps 2 -Seed 1234 > $O/regress/map-$m.out 2>&1"
done
# ---- 1b Realistic perf row (Integration 2026-10-16; still under the hold): the final head with the package 6 defaults + HD Enhanced,
#      2560x1440 window (NOINTERACT exact client size), 31 v 32 lockstep, 1 rep, the 5 user maps; report-only, flag p50 > 16.7 ms
HDR=${GATE_HD_ROOT:-"F:\Transformers Rebuild\AssetTools\out\hd\push_full_snap_20261016_b0-31\pack"}
st "1b Realistic perf row START"
for m in 508 510 507 504 509; do
  run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/rtperf/map-$m -Map $m -Arms 'realistic|work/ab/fast_$f|WFC_RT_MODE=2;WFC_HDTEXTURES=2;WFC_HD_ROOT=$HDR' -Steps 6000 -From 2400 -Width 2560 -Height 1440 -RenderSize 2560x1440 -PlayerBot 1 -Reps 1 -Seed 1234 > $O/rtperf/map-$m.out 2>&1"
done
run "rm -f work/PERF_HOLD"; st "1 regress + 1b TIMED DONE (hold removed); own exes $(mine)"

# ---- 2 R4 leak rows
st "2 leak"; run "bash work/sweep/r4leak2.sh $f 2 1 _gate > $O/leak.out 2>&1; cp -r work/sweep/r4leak_${f}_gate $O/leak 2>/dev/null"

# ---- 3 eye pass
st "3 eyepass"; run "bash tools/fidelity/eyepass.sh $f > $O/eyepass.out 2>&1"

# ---- 4 FAST: full on the penultimate, delta on the final
st "4 FAST full $p"; run "WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1 powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/fast-gate.ps1 -Ref $P -Name fast_$p -OutDir $O/fast_$p > $O/fast_$p.log 2>&1"
DS=$(powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/delta-steps.ps1 -Base $P -Head $F $( [ -n "$SWITCH_FLIPS" ] && echo -SwitchFlips) | tail -1 | tr -d '\r')
st "4 delta FAST $f: rows '$DS' rerun, the rest reused from $p"
run "WFC_GATE_GPU_POLICY=shared WFC_GATE_GPU_MAX_OTHERS=1 powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/fast-gate.ps1 -Ref $F -Prev $P -Name fast_$f -OutDir $O/fast_$f -Steps '${DS:-none}' -ReuseFrom $O/fast_$p > $O/fast_$f.log 2>&1"

# ---- table
st "report"; run "PYTHONDONTWRITEBYTECODE=1 'F:/Transformers Rebuild/AssetTools/bin/py/python.exe' tools/fidelity/package_gate_report.py $O $f $p | tee -a $L"
st "PACKAGE GATE DONE; own exes $(mine)"
