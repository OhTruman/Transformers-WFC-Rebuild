#!/bin/bash
# FINAL COMBINED GATE (Integration 2026-10-23): the last broad pass on the cut candidate = the combined timed gate + the stability set.
#   phase pre  (CPU, no hold): build the head; build the regress base = af4d0731 + every sim-only diff (FINAL_BASE_ITEMS: the four
#              gatebase patches + Gameplay's sim-only diffs for spread / dodge / bot skill / leave / Escalation / campaign ...);
#              headless same-match base vs head on Streets (TDM) and Remnant (ESC) + the negative control (hover-only base must differ);
#              then the stability set on the head exe (work/udp/batch.ps1 -SkipBuild: 10 maps x 300 s TDM, both ESC maps, D5 + A1,
#              CAMPAIGNTEST / KISMETTEST / VOICETEST, the net rows).
#   phase gate (timed; only after Integration's GO + the acks; PERF_HOLD + a clear card): the render data frozen (one snapshot for all
#              arms), then 10 MP maps (501-510; 505 / 506 in ESC) x {2560x1440, 1920x1080} x {base, head}, lockstep 600-9600, seed 1234,
#              PlayerBot, 2 reps; CONTAMINATED maps rerun whole once; the pool guard at every step; the hold removed at the end.
#   bash tools/fidelity/final-gate.sh <head sha> pre|gate [--whatif]
#   env: FINAL_BASE_ITEMS (patch files / commits for gate-base.sh), FINAL_BASE (an already built + verified base), FINAL_MAPS, FINAL_RES
cd "F:/Transformers Rebuild/Rebuild-Experimental" || exit 1
H=${1:?head sha}; PH=${2:?pre or gate}; WHATIF=0; [ "$3" = "--whatif" ] && WHATIF=1; h=${H:0:7}
O=work/finalgate/$h; mkdir -p $O; L=$O/finalgate.log
export WFC_MUTE=1 WFC_NOINTERACT=1 PYTHONDONTWRITEBYTECODE=1 WFC_GATE_IGNORE_HOLD=1 WFC_GATE_GPU_GRACE_MIN=0
GB=tools/fidelity/gatebase
ITEMS=${FINAL_BASE_ITEMS:-"$GB/hover_cfca1717_srcgame.patch $GB/headless_runner_be54be6d_af4d.patch $GB/TRANSACT_sim_only_af4d.patch $GB/WEAPON_sim_only_af4d.patch"}
BN=final_$h; BASE=${FINAL_BASE:-work/ab/fast_af4d073_$BN}; HEAD=work/ab/fast_$h
MAPS=${FINAL_MAPS:-"501 502 503 504 505 506 507 508 509 510"}; RES=${FINAL_RES:-"2560x1440 1920x1080"}
st() { echo "$(date +%T) $*" | tee -a $L; pool_guard; }
pool_guard() { [ $WHATIF = 1 ] && return; local gb; gb=$(powershell -NoProfile -Command "[math]::Round((Get-Counter '\Memory\Pool Nonpaged Bytes').CounterSamples[0].CookedValue/1GB,2)" | tr -d '[:space:]')
  echo "  (pool $gb GB)" >> $L; if awk "BEGIN{exit !($gb > ${GATE_POOL_MAX_GB:-9.5})}"; then rm -f work/PERF_HOLD; echo "$(date +%T) POOL GUARD: $gb GB - stopped (hold removed)" | tee -a $L; exit 3; fi; }
run() { if [ $WHATIF = 1 ]; then echo "WHATIF: $*" | tee -a $L; else eval "$@"; fi; }
busy() { powershell -NoProfile -Command "\$n = 'python.exe','node.exe','sqlite3.exe','cmake.exe','ninja.exe','cl.exe','clang.exe','clang++.exe','clang-cl.exe','link.exe','lld-link.exe','dxc.exe','msbuild.exe'; @(Get-CimInstance Win32_Process | ? { ((\$n -contains \$_.Name) -and \"\$(\$_.CommandLine)\" -notlike '*Rebuild-Experimental*final-gate*') -or (\$_.Name -like 'wfc_rebuild*' -and \"\$(\$_.ExecutablePath)\" -notlike '*Rebuild-Experimental*') }).Count" | tr -d '[:space:]'; }
recontam() { [ $WHATIF = 1 ] && return; ls $1/*/CONTAMINATED.txt >/dev/null 2>&1 || return 0
  st "  $(basename $1): CONTAMINATED rep - rerun whole"; rm -rf ${1}_contam; mv $1 ${1}_contam; mv $1.out ${1}_contam.out.txt 2>/dev/null; eval "$2"
  ls $1/*/CONTAMINATED.txt >/dev/null 2>&1 && st "  $(basename $1): still CONTAMINATED - report row UNKNOWN"; return 0; }
if [ "$PH" = pre ]; then
  st "FINAL GATE pre: head $H; base af4d0731 + $(echo $ITEMS | sed "s#$GB/##g")$( [ $WHATIF = 1 ] && echo ' (WHATIF)')"
  [ -f $HEAD/build-release/bin/wfc_rebuild.exe ] || run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/m05/build-target.ps1 -Ref $H -Name fast_$h -Jobs 4 -NoDebug -ThirdParty work/ab/_third_party > $O/build_head.log 2>&1; powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/link-render.ps1 -Target $HEAD >> $O/build_head.log 2>&1"
  [ $WHATIF = 1 ] || [ -f $HEAD/build-release/bin/wfc_rebuild.exe ] || { st "head build failed ($O/build_head.log)"; exit 1; }
  [ $WHATIF = 1 ] || grep -q "WFC_OPUS=1" $HEAD/build-release/build.ninja || { st "head built without Opus (third_party/opus missing)"; exit 1; }
  NEG=${FINAL_NEG:-work/ab/fast_af4d073_neg_hover}   # the negative control: af4d0731 + hover + the headless runner only (no sim diffs)
  [ -f $NEG/build-release/bin/wfc_rebuild.exe ] || run "BASE_REF=af4d0731 bash tools/fidelity/gate-base.sh neg_hover $GB/hover_cfca1717_srcgame.patch $GB/headless_runner_be54be6d_af4d.patch > $O/build_neg.out 2>&1"
  if [ -z "$FINAL_SKIP_BASE" ]; then   # FINAL_SKIP_BASE=1: no base yet (FINAL_BASE_ITEMS pending) - head, control and stability only
  [ -f $BASE/build-release/bin/wfc_rebuild.exe ] || run "BASE_REF=af4d0731 bash tools/fidelity/gate-base.sh $BN $ITEMS > $O/build_base.out 2>&1"
  [ $WHATIF = 1 ] || [ -f $BASE/build-release/bin/wfc_rebuild.exe ] || { st "base build failed ($O/build_base.out)"; exit 1; }
  st "same match (headless): Streets TDM + Remnant ESC + BrokenHope ESC, base vs head; negative control = the hover-only base"
  for m in 508 506 505; do run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $O/samematch-$m -Map $m -Arms 'base|$BASE|WFC_NULLRENDER=1;;head|$HEAD|WFC_NULLRENDER=1;;neg|$NEG|WFC_NULLRENDER=1' -Steps 9600 -From 600 -PlayerBot 1 -Reps 1 -Seed 1234 > $O/samematch-$m.out 2>&1"
    sm=$(grep -oE "lockstep\.head\.r1: head: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+" $O/samematch-$m.out 2>/dev/null); ng=$(grep -oE "lockstep\.neg\.r1: neg: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+" $O/samematch-$m.out 2>/dev/null)
    st "map $m same match: ${sm:-?}; negative control: ${ng:-?}"
    [ $WHATIF = 1 ] || case "$sm" in *True) ;; *) st "STOP: the base does not play the head's match on $m - not gating (a sim-only diff is missing)"; exit 2;; esac
  done
  fi
  if [ -z "$FINAL_SKIP_STABILITY" ]; then
  st "stability set on the head exe (work/udp/batch.ps1 -SkipBuild)"
  run "powershell -NoProfile -ExecutionPolicy Bypass -File work/udp/batch.ps1 -Ref $H -Tag $h -SkipBuild > $O/stability.out 2>&1; cp work/udp/batch_$h.log $O/stability.log"
  fi
  st "FINAL GATE pre DONE: base $( [ -n "$FINAL_SKIP_BASE" ] && echo "NOT built yet (FINAL_SKIP_BASE)" || echo "$BASE verified"); stability $O/stability.log - ready for Integration's GO"
elif [ "$PH" = gate ]; then
  [ $WHATIF = 1 ] || [ -f $BASE/build-release/bin/wfc_rebuild.exe ] || { st "run 'pre' first"; exit 1; }
  st "FINAL GATE timed: head $H vs base $BASE; maps $MAPS; res $RES$( [ $WHATIF = 1 ] && echo ' (WHATIF)')"
  RD0=""
  if [ "${GATE_RENDER_SNAPSHOT:-1}" != 0 ]; then
    run "powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/matb_root.ps1 -Name final_$h -NoCandidate >> $L 2>&1" || { st "render snapshot failed"; exit 1; }
    RD0="WFC_RENDER_DATA=$(cygpath -w "$PWD/work/matb/final_${h}_root")"; st "render data frozen: $RD0"
  fi
  st "hold set; waiting for a clear card"; run "touch work/PERF_HOLD; sleep 60; until [ \"\$(busy)\" = 0 ]; do sleep 15; done"
  for res in $RES; do for m in $MAPS; do
    D=$O/regress-$res/map-$m; mkdir -p $O/regress-$res
    CMD="powershell -NoProfile -ExecutionPolicy Bypass -File tools/fidelity/lockstep-ab.ps1 -OutDir $D -Map $m -Arms 'base|$BASE|$RD0;;head|$HEAD|$RD0' -Steps 9600 -From 600 -RenderSize $res -PlayerBot 1 -Reps 2 -Seed 1234 > $D.out 2>&1"
    st "regress $res map $m"; run "$CMD"; recontam $D "$CMD"
  done; done
  run "rm -f work/PERF_HOLD"; st "TIMED DONE (hold removed)"
  for res in $RES; do for m in $MAPS; do f=$O/regress-$res/map-$m.out; [ -f $f ] && grep -oE "lockstep\.head\.r[12]: head: steps [0-9]+ \([0-9-]+\), same match as 'base' [A-Za-z]+.{0,40}frame p50 / p90 / p99 [0-9.]+ / [0-9.]+ / [0-9.]+" $f | sed "s/^/$res $m /" | tee -a $L; done; done
  st "FINAL GATE DONE: $O"
fi
